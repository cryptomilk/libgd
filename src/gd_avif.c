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
#include "gd.h"
#include "gd_errors.h"
#include "gdhelpers.h"

#ifdef HAVE_LIBAVIF
#include "avif.h"


/* Helper function definitions */

void destroyCtxAndAvifIO(avifIO *io);
avifIO *createAvifIOFromCtx(gdIOCtx * ctx);
avifResult readFromCtx(avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out);
void destroyCtxAndAvifIO(avifIO *io);
avifBool isAvifError(avifResult result, char * msg);


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

    On error, returns 0.
*/
BGD_DECLARE(gdImagePtr) gdImageCreateFromAvif (FILE * infile)
{
  gdImagePtr image;
  gdIOCtx *ctx = gdNewFileCtx(infile);
  
  if (!ctx) {
    return GD_FALSE;
  }

  image = gdImageCreateFromAvifCtx(ctx);
  ctx->gd_free(ctx);

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
  gdIOCtx *ctx = gdNewDynamicCtxEx(size, data, 0); //TODO: should the free flag really be false?

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
  //TODO: clean all these declarations up
  int width, height;
  uint8_t *filedata = NULL;
  uint8_t *argb = NULL;
  unsigned char *read, *temp;
  size_t size = 0, n;
  int x, y;
  uint8_t *p; 
  gdImagePtr im;
  avifResult result;
  avifIO *io;
  avifDecoder *decoder;
  // int returnCode = 1;

  decoder = avifDecoderCreate();
  io = createAvifIOFromCtx(ctx);

  avifRGBImage rgb;
  memset(&rgb, 0, sizeof(rgb));

  avifDecoderSetIO(decoder, io);

  result = avifDecoderParse(decoder);
  if (isAvifError(result, "Could not parse image")) {
    goto cleanup;
  }

  AVIF_DEBUG(printf("Succeeded in decoding image"));

  // Note again that, for an image sequence, we read only the first image, ignoring the rest.
  result = avifDecoderNextImage(decoder);
  if (isAvifError(result, "Could not decode image")) {
    goto cleanup;
  }

  avifRGBImageSetDefaults(&rgb, decoder->image);
  // Override YUV(A)->RGB(A) defaults here: depth, format, chromaUpsampling, ignoreAlpha, libYUVUsage, etc

  // Alternative: set rgb.pixels and rgb.rowBytes yourself, which should match your chosen rgb.format
  // Be sure to use uint16_t* instead of uint8_t* for rgb.pixels/rgb.rowBytes if (rgb.depth > 8)
  avifRGBImageAllocatePixels(&rgb);

  if (avifImageYUVToRGB(decoder->image, &rgb) != AVIF_RESULT_OK) {
    gd_error("gd-avif error: Conversion from YUV failed");
    goto cleanup;
  }

  // create a new gd image

  if (rgb.depth > 8) {
    uint16_t * firstPixel = (uint16_t *)rgb.pixels;
    printf(" * First pixel: RGBA(%u,%u,%u,%u)\n", firstPixel[0], firstPixel[1], firstPixel[2], firstPixel[3]);
  } else {
    uint8_t * firstPixel = rgb.pixels;
    printf(" * First pixel: RGBA(%u,%u,%u,%u)\n", firstPixel[0], firstPixel[1], firstPixel[2], firstPixel[3]);
  }


  /* do not use gdFree here, in case gdFree/alloc is mapped to something else than libc */

  cleanup:
    free(argb);
    avifRGBImageFreePixels(&rgb); // Only use in conjunction with avifRGBImageAllocatePixels()
    avifDecoderDestroy(decoder);
    gdImageDestroy(im);
    // more things to free up?
    return NULL;

  im->saveAlphaFlag = 1;
  return im;
}

/*** HELPER FUNCTIONS ***/


/* Check the result from an Avif function to see if it's an error.
   If so, decode the error and output it, and return true.
   Otherwise, return false.
*/
avifBool isAvifError(avifResult result, char * msg) {
  if (result != AVIF_RESULT_OK) {
    gd_error("gd-avif error: %s: %s", msg, avifResultToString(result));
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

avifIO *createAvifIOFromCtx(gdIOCtx * ctx) {
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
*/

avifResult readFromCtx(avifIO * io, uint32_t readFlags, uint64_t offset, size_t size, avifROData * out)
{
  void *dataBuf;
  gdIOCtx *ctx = io->data; // TODO: should I have cast this?

  //TODO: if we set sizeHint, this will be more efficient.

  if (offset > LONG_MAX || size > 0) {
    return AVIF_RESULT_IO_ERROR;
  }

  // Try to seek offset bytes forward. If we pass the end of the buffer, throw an error.
  if (!(ctx->seek(ctx, offset))) {
    return AVIF_RESULT_IO_ERROR;
  }

  gdRealloc(dataBuf, size);

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
void destroyCtxAndAvifIO(struct avifIO *io) {
  gdIOCtx *ctx = io->data; // TODO: should I have cast this?

  ctx->gd_free(ctx);
  avifFree(io);
}

// TODO: placeholder. Replace with the real deal.
BGD_DECLARE(void) gdImageAvif (gdImagePtr im, FILE * outFile)
{
  printf ("To come");
}


#else /* !HAVE_LIBAVIF */

static void _noAvifError(void)
{
  gd_error("AVIF image support has been disabled\n");
}

BGD_DECLARE(gdImagePtr) gdImageCreateFromAvif (FILE * ctx)
{
  _noAvifError();
  return NULL;
}

BGD_DECLARE(gdImagePtr) gdImageCreateFromAvifPtr (int size, void *data)
{
  _noAvifError();
  return NULL;
}

BGD_DECLARE(gdImagePtr) gdImageCreateFromAvifCtx (gdIOCtx * ctx)
{
  _noAvifError();
  return NULL;
}

BGD_DECLARE(void) gdImageAvifCtx (gdImagePtr im, gdIOCtx * outfile, int quality)
{
  _noAvifError();
}

BGD_DECLARE(void) gdImageAvifEx (gdImagePtr im, FILE * outFile, int quality)
{
  _noAvifError();
}

BGD_DECLARE(void) gdImageAvif (gdImagePtr im, FILE * outFile)
{
  _noAvifError();
}

BGD_DECLARE(void *) gdImageAvifPtr (gdImagePtr im, int *size)
{
  _noAvifError();
  return NULL;
}

BGD_DECLARE(void *) gdImageAvifPtrEx (gdImagePtr im, int *size, int quality)
{
  _noAvifError();
  return NULL;
}

#endif /* HAVE_LIBAVIF */