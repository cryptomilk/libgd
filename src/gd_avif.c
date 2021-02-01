/**
 * File: AVIF IO
 *
 * Read and write AVIF images.
 */

#define AVIF_DEBUG(s)
/* TODO: remove that */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include <math.h>
#include "gd.h"
#include "gd_errors.h"
#include "gdhelpers.h"

#ifdef HAVE_LIBAVIF
#include "avif.h"
#include "internal.h"

/*
  Working defaults for encoding.
  DEFAULT_CHROMA_SUBSAMPLING: 4:2:0 is commonly used for Chroma subsampling.
  DEFAULT_MIN_QUANTIZER, DEFAULT_MAX_QUANTIZER: 
  We need more testing to really know what quantizer settings are optimal,
  but teams at Google have been using minimum=10 and maximum=30 as a starting point.
  DEFAULT_QUALITY: following gd conventions, -1 indicates the default.
  DEFAULT_SPEED: AVIF_SPEED_DEFAULT is -1 - it simply tells the encoder to use the default speed.
*/

#define DEFAULT_CHROMA_SUBSAMPLING AVIF_PIXEL_FORMAT_YUV420
#define DEFAULT_QUANTIZER 30
#define DEFAULT_QUALITY -1
#define DEFAULT_SPEED AVIF_SPEED_DEFAULT

// This initial size for the gdIOCtx is standard among GD image conversion functions
#define NEW_DYNAMIC_CTX_SIZE 2048

// Our quality param ranges from 0 to 100
#define MAX_QUALITY 100

// For computing the number of tiles and threads to use for encoding
// Maximum threads are from libavif/contrib/gkd-pixbuf/loader.c
#define MIN_TILE_AREA (512 * 512)
#define MAX_TILES 6
#define MAX_THREADS 64


typedef struct {
  int tileRowsLog2;
  int tileColumnsLog2;
  int threads;
}
tilesAndThreads;

/* Helper function signatures */

static avifBool _gdImageAvifCtx (gdImagePtr im, gdIOCtx * outfile, int quality, int speed);
static void destroyCtxAndAvifIO(avifIO *io);
static avifIO *createAvifIOFromCtx(gdIOCtx * ctx);
static avifResult readFromCtx(avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out);
static void destroyCtxAndAvifIO(avifIO *io);
static avifBool isAvifError(avifResult result, char * msg);
static int quality2Quantizer(int quality);
static uint8_t convertTo8BitAlpha(uint8_t originalAlpha);
static void setEncoderTilesAndThreads(avifEncoder * encoder, avifRGBImage * rgb);


/*****************************************************************************
 * 
 *                            DECODING FUNCTIONS
 * 
 *****************************************************************************/

/*
  Function: gdImageCreateFromAvif

    <gdImageCreateFromAvif> decodes an AVIF image into GD's internal format.
    It creates a gdIOCtx struct from the file pointer it's passed.
    And then it relies on <gdImageCreateFromAvifCtx> to do all the real work.
    If the file contains an image sequence, we simply read the first one.
    We could throw an error instead...

  Parameters:

    ctx - pointer to the GD ctx object

  Returns:

    A pointer to the new image.  This will need to be
    destroyed with <gdImageDestroy> once it is no longer needed.

    On error, returns NULL.
*/
BGD_DECLARE(gdImagePtr) gdImageCreateFromAvif (FILE * infile)
{
  gdImagePtr image;
  gdIOCtx *ctx = gdNewFileCtx(infile);

  printf("in gdImageCreateFromAvif()\n");
  
  if (!ctx) {
    return GD_FALSE;
  }

  image = gdImageCreateFromAvifCtx(ctx);
  // ctx->gd_free(ctx);

  return image;
}

/*
  Function: gdImageCreateFromAvifPtr

    <gdImageCreateFromAvif> decodes an AVIF image into GD's internal format.
    It creates a gdIOCtx struct from the data it's passed, allocating space for the desired size.
    And then it relies on <gdImageCreateFromAvifCtx> to do all the real work.

  Parameters:

    size            - size of Avif data in bytes.
    data            - pointer to Avif data.

    On error, returns 0.
*/
BGD_DECLARE(gdImagePtr) gdImageCreateFromAvifPtr (int size, void *data)
{
  gdImagePtr image;
  gdIOCtx *ctx = gdNewDynamicCtxEx(size, data, 0);

  if (!ctx)
    return 0;

  image = gdImageCreateFromAvifCtx(ctx);
  ctx->gd_free(ctx);

  return image;
}

/*
  Function: gdImageCreateFromAvifCtx

    See <gdImageCreateFromAvif>.
*/
BGD_DECLARE(gdImagePtr) gdImageCreateFromAvifCtx (gdIOCtx * ctx)
{
  int x, y;
  uint8_t *p8; 
  uint16_t *p16;
  gdImage *im = NULL;
  avifResult result;
  avifIO *io;
  avifDecoder *decoder;
  avifRGBImage rgb;

  decoder = avifDecoderCreate();
  io = createAvifIOFromCtx(ctx);

  // memset(rgb, 0, sizeof(rgb));

  avifDecoderSetIO(decoder, io);

  result = avifDecoderParse(decoder);
  if (isAvifError(result, "Could not parse image")) {
    goto cleanup;
  }

  printf("Succeeded in decoding image\n");
  printf("Parsed AVIF: %ux%u (%ubpc)\n", decoder->image->width, decoder->image->height, decoder->image->depth);

  // Note again that, for an image sequence, we read only the first image, ignoring the rest.
  result = avifDecoderNextImage(decoder);
  if (isAvifError(result, "Could not decode image")) {
    goto cleanup;
  }

  // Set up the avifRGBImage. Use default settings unless there's demand to override these.
  avifRGBImageSetDefaults(&rgb, decoder->image);
  avifRGBImageAllocatePixels(&rgb);

  result = avifImageYUVToRGB(decoder->image, &rgb);
  if (isAvifError(result, "gd-avif error: Conversion from YUV to RGB failed")) {
    goto cleanup;
  }

  im = gdImageCreateTrueColor(decoder->image->width, decoder->image->height);
	if (!im) {
		gd_error("avif error: Could not create GD truecolor image");
		goto cleanup;
	}


// Now we need to read the pixels from the AVIF image and copy them into the GD image.
  // Image depth can be 8, 10, 12, or 16. But if depth>8, pixels are uint16_t.
// I think we should use tpixels because I think those are true color pixels. I think the pixels array is palette-based.
//TODO: can these two cases be combined?

  if (rgb.depth == 8) {
    for (y = 0, p8 = rgb.pixels; y < decoder->image->height; y++) {
      for (x = 0; x < decoder->image->width; x++) {
        register uint8_t r = *(p8++);
        register uint8_t g = *(p8++);
        register uint8_t b = *(p8++);
        register uint8_t a = *(p8++);
        im->tpixels[y][x] = gdTrueColorAlpha(r, g, b, a);
      }
    }
    
  } else {
    for (y = 0, p16 = (uint16_t *) rgb.pixels; y < decoder->image->height; y++) {
      for (x = 0; x < decoder->image->width; x++) {
        register uint16_t r = *(p16++);
        register uint16_t g = *(p16++);
        register uint16_t b = *(p16++);
        register uint16_t a = *(p16++);
        im->tpixels[y][x] = gdTrueColorAlpha(r, g, b, a);
      }
    }
  }

  /* do not use gdFree here, in case gdFree/alloc is mapped to something else than libc */

  cleanup:
    printf("Gonna do avifDecoderDestroy\n");
    avifDecoderDestroy(decoder);
    // avifFree(io);
    // avifRGBImageFreePixels(&rgb);
    // TODO: more things to free up?

    im->saveAlphaFlag = 1;
    return im;
}


/*****************************************************************************
 * 
 *                            ENCODING FUNCTIONS
 * 
 *****************************************************************************/

BGD_DECLARE(void) gdImageAvif (gdImagePtr im, FILE * outFile)
{
	return gdImageAvifEx(im, outFile, DEFAULT_QUALITY, AVIF_SPEED_DEFAULT);
}


BGD_DECLARE(void) gdImageAvifEx (gdImagePtr im, FILE * outFile, int quality, int speed)
{
	gdIOCtx *out = gdNewFileCtx(outFile);

	if (out == NULL) {
		return;
	}

	gdImageAvifCtx(im, out, quality, speed);
	out->gd_free(out);
}


BGD_DECLARE(void *) gdImageAvifPtr (gdImagePtr im, int *size)
{
	return gdImageAvifPtrEx(im, size, DEFAULT_QUALITY, AVIF_SPEED_DEFAULT);
}

BGD_DECLARE(void *) gdImageAvifPtrEx (gdImagePtr im, int *size, int quality, int speed)
{
	void * rv;
	gdIOCtx * out = gdNewDynamicCtx(NEW_DYNAMIC_CTX_SIZE, NULL);

	if (out == NULL) {
		return NULL;
	}

	if (_gdImageAvifCtx(im, out, quality, speed)) {
		rv = NULL;
	} else {
		rv = gdDPExtractData(out, size);
	}

	out->gd_free(out);
	return rv;
}


BGD_DECLARE(void) gdImageAvifCtx (gdImagePtr im, gdIOCtx * outfile, int quality, int speed)
{
  _gdImageAvifCtx(im, outfile, quality, speed);
}

/* 
   We need this because gdImageAvifCtx() can't return anything.
   And our functions that operate on a memory buffer need to know whether the encoding has succeeded.

   If we're passed the DEFAULT_QUALITY of -1, set the quantizer params to DEFAULT_QUANTIZER

   This function returns 0 on success, or 1 on failure.
 */
static avifBool _gdImageAvifCtx (gdImagePtr im, gdIOCtx * outfile, int quality, int speed)
{
  avifResult result;
  avifRGBImage rgb;
  tilesAndThreads theTilesAndThreads;
  avifRWData avifOutput = AVIF_DATA_EMPTY;
  avifBool failed = AVIF_FALSE;

  register uint32_t val;
  register uint8_t * p;
  register uint8_t a;
  int x, y;

	if (im == NULL) {
		return 1;
	}

	if (!gdImageTrueColor(im)) {
		gd_error("avif doesn't support palette images");
		return 1;
	}

  avifImage * avifIm = avifImageCreate(gdImageSX(im), gdImageSY(im), 8, DEFAULT_CHROMA_SUBSAMPLING);

  rgb.width = gdImageSX(im);
  rgb.height = gdImageSY(im);
  rgb.depth = 8;
  rgb.format = AVIF_RGB_FORMAT_RGBA;
  rgb.chromaUpsampling = AVIF_CHROMA_UPSAMPLING_AUTOMATIC;
  rgb.ignoreAlpha = AVIF_FALSE;
  rgb.pixels = NULL;
  avifRGBImageAllocatePixels(&rgb); // this allocates memory, and sets rgb.rowBytes and rgb.pixels

  // Parse RGB data from the GD image, and copy it into the AVIF RGB image.
  p = rgb.pixels;
  for (y = 0; y < rgb.height; y++) {
    for (x = 0; x < rgb.width; x++) {
      val = im->tpixels[y][x];

      a = convertTo8BitAlpha(gdTrueColorGetAlpha(val));

      *(p++) = gdTrueColorGetRed(val);
      *(p++) = gdTrueColorGetGreen(val);
      *(p++) = gdTrueColorGetBlue(val);
      *(p++) = a;
    }
  }

  result = avifImageRGBToYUV(avifIm, &rgb);
  if ((failed = isAvifError(result, "Could not convert image to YUV"))) {
    goto cleanup;
  }

  // Encode the image in AVIF format

  avifEncoder * encoder = avifEncoderCreate();
  int quantizerQuality = quality == DEFAULT_QUALITY ? 
                         DEFAULT_QUANTIZER : quality2Quantizer(quality);

  encoder->minQuantizer = quantizerQuality;
  encoder->maxQuantizer = quantizerQuality;
  encoder->minQuantizerAlpha = quantizerQuality;
  encoder->maxQuantizerAlpha = quantizerQuality;
  encoder->speed = speed;
  setEncoderTilesAndThreads(encoder, &rgb);

  result = avifEncoderAddImage(encoder, avifIm, 1, AVIF_ADD_IMAGE_FLAG_SINGLE); //TODO: why 1?
  if ((failed = isAvifError(result, "Could not encode image"))) {
    goto cleanup;
  }

  result = avifEncoderFinish(encoder, &avifOutput);
  if ((failed = isAvifError(result, "Could not finish encoding"))) {
    goto cleanup;
  }

  // Write the AVIF image bytes to the GD ctx.
  gdPutBuf(avifOutput.data, avifOutput.size, outfile);

  cleanup:
    if (rgb.pixels) {
      avifRGBImageFreePixels(&rgb);
    }

    if (encoder) {
      avifEncoderDestroy(encoder);
    }

    if (avifOutput.data) {
      avifRWDataFree(&avifOutput);
    }

    return failed;
}

/*****************************************************************************
 * 
 *                            HELPER FUNCTIONS
 * 
 *****************************************************************************/

/* Convert the quality param we expose to the quantity params used by libavif.
   The *Quantizer* params values can range from 0 to 63, with 0 = highest quality and 63 = worst.
   We make the scale 0-100, and we reverse this, so that 0 = worst quality and 100 = highest.

   Values below 0 are set to 0, and values below MAX_QUALITY are set to MAX_QUALITY.
   (Note that we assume the minimum quality value is 0.)
*/
//TODO: check to make sure values are in range. And round off the result.

static int quality2Quantizer(int quality) {
  int clampedQuality = AVIF_CLAMP(quality, 0, MAX_QUALITY);

  float scaleFactor = (float) AVIF_QUANTIZER_WORST_QUALITY / (float) MAX_QUALITY;

  return round(scaleFactor * (MAX_QUALITY - clampedQuality));
}

/*
  From gd_png.c:
    convert the 7-bit alpha channel to an 8-bit alpha channel.
    We do a little bit-flipping magic, repeating the MSB
    as the LSB, to ensure that 0 maps to 0 and
    127 maps to 255. We also have to invert to match
    PNG's convention in which 255 is opaque. 
*/

static uint8_t convertTo8BitAlpha(uint8_t originalAlpha) {
  return originalAlpha == 127 ? 
        0 : 
        255 - ((originalAlpha << 1) + (originalAlpha >> 6));
}

// algorithm from wtc@

static void setEncoderTilesAndThreads(avifEncoder * encoder, avifRGBImage * rgb) {
  int imageArea, tiles, tilesLog2;
  int tileRowsLog2, tileColumnsLog2, maxThreads;

  imageArea = rgb->width * rgb->height;

  tiles = ceil(imageArea / MIN_TILE_AREA);
  tiles = AVIF_MIN(tiles, MAX_TILES);
  tiles = AVIF_MIN(tiles, MAX_THREADS);

  tilesLog2 = floor(log10(tiles) / log10(2));

  // If the image's width is greater than the height, use more tile columns
  // than tile rows to make the tile size close to a square
    
  if (rgb->width >= rgb->height) {
    encoder->tileRowsLog2 = tilesLog2 / 2;
    encoder->tileColsLog2 = tilesLog2 - encoder->tileRowsLog2;
  } else {
    encoder->tileColsLog2 = tilesLog2 / 2;
    encoder->tileRowsLog2 = tilesLog2 - encoder->tileColsLog2;
  }

  // It's good to have one thread per tile.
  encoder->maxThreads = tiles;
}

/* Check the result from an Avif function to see if it's an error.
   If so, decode the error and output it, and return true.
   Otherwise, return false.
*/
static avifBool isAvifError(avifResult result, char * msg) {
  if (result != AVIF_RESULT_OK) {
    gd_error("avif error: %s: %s", msg, avifResultToString(result));
    return GD_TRUE;
  }

  return GD_FALSE;
}

/* Set up an avifIO object.
 * The functions in the gdIOCtx struct may refer to a file or a memory buffer. We don't care.
 * Our task is simply to assign avifIO functions to the proper functions from gdIOCtx.
 * The destroy function needs to destroy the avifIO object and anything else it uses.
*/

//TODO: make sure I allocate memory for my structs.
//TODO: deal with sizeHint, persistent.

static avifIO *createAvifIOFromCtx(gdIOCtx * ctx) {
  avifIO *io;

  io = (avifIO *) gdMalloc(sizeof(avifIO));
	if (io == NULL) {
		return NULL;
	}

  io->read = readFromCtx;
  io->write = NULL; // this function is currently unused; see avif.h
  io->destroy = destroyCtxAndAvifIO;
  io->sizeHint = 0; // sadly, we don't get this information from the gdIOCtx
  io->persistent = GD_FALSE; // This seems like the safe thing to do, but I don't know for sure. This is less efficient, because it means AVIF will make copies of all buffers.
  io->data = ctx;

  return io;
}


/*
 logic inspired by avifIOMemoryReaderRead() and avifIOFileReaderRead()
 implements the avifIOReadFunc interface by calling the relevant functions in the gdIOCtx.
 we don't know whether we're reading from a file or from memory. We don't have to know,
 since we rely on the helper functions in the gdIOCtx.
 Return an avifResult, which is really either an error code or OK.
 Assume we've stashed the gdIOCtx in io->data, as we do in createAvifIOFromCtx().

 We ignore readFlags, just as the avifIO*ReaderRead() functions do.

 If there's a problem, this returns an avifResult error.
 Of course this error shouldn't be returned by any top-level GD function in this file.
*/

static avifResult readFromCtx(avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out)
{
  void *dataBuf = NULL;
  gdIOCtx *ctx = (gdIOCtx *) io->data;

  //TODO: if we set sizeHint, this will be more efficient.

  if (offset > LONG_MAX || size < 0) {
    return AVIF_RESULT_IO_ERROR;
  }

  // Try to seek offset bytes forward. If we pass the end of the buffer, throw an error.
  if (!(ctx->seek(ctx, offset))) {
    return AVIF_RESULT_IO_ERROR;
  }

  dataBuf = gdRealloc(dataBuf, size);
  if (!dataBuf) {
    gd_error("avif decode: realloc failed");
    return AVIF_RESULT_UNKNOWN_ERROR;
  }

  // Read the number of bytes requested. If getBuf() returns a negative value, that means there was an error.
  int charsRead = ctx->getBuf(ctx, dataBuf, size);
  if (charsRead < 0) {
    return AVIF_RESULT_IO_ERROR;
  }

  out->data = dataBuf;
  out->size = charsRead;
  return AVIF_RESULT_OK;  // or AVIF_RESULT_TRUNCATED_DATA ....
}

// avif.h says this is optional, but it seemed easy to implement.
static void destroyCtxAndAvifIO(struct avifIO *io) {
  gdIOCtx *ctx = io->data; // TODO: should I have cast this?

  ctx->gd_free(ctx);
  avifFree(io);
}


#else /* !HAVE_LIBAVIF */

static void _noAvifError(void)
{
  gd_error("AVIF image support has been disabled\n");
  return NULL;
}

BGD_DECLARE(gdImagePtr) gdImageCreateFromAvif (FILE * ctx)
{
  return _noAvifError();
}

BGD_DECLARE(gdImagePtr) gdImageCreateFromAvifPtr (int size, void *data)
{
  return _noAvifError();
}

BGD_DECLARE(gdImagePtr) gdImageCreateFromAvifCtx (gdIOCtx * ctx)
{
  return _noAvifError();
}

BGD_DECLARE(void) gdImageAvifCtx (gdImagePtr im, gdIOCtx * outfile, int quality, int speed)
{
  return _noAvifError();
}

BGD_DECLARE(void) gdImageAvifEx (gdImagePtr im, FILE * outFile, int quality, int speed)
{
  return _noAvifError();
}

BGD_DECLARE(void) gdImageAvif (gdImagePtr im, FILE * outFile)
{
  return _noAvifError();
}

BGD_DECLARE(void *) gdImageAvifPtr (gdImagePtr im, int *size)
{
  return _noAvifError();
}

BGD_DECLARE(void *) gdImageAvifPtrEx (gdImagePtr im, int *size, int quality, int speed)
{
  return _noAvifError();
}

#endif /* HAVE_LIBAVIF */