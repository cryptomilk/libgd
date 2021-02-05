/**
 * A short program which converts a .jpg file into a .avif file - 
 * just to get a little practice with the basic functionality.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>

#include "gd.h"

int main(int argc, char **argv)
{
	gdImagePtr im;
	FILE *in, *out;

	if (argc != 3) {
		fprintf(stderr, "Usage: jpeg2avif filename.jpg filename.avif\n");
		exit(1);
	}

	printf("Reading infile %s\n", argv[1]);

	in = fopen(argv[1], "rb");
	if (!in) {
		fprintf(stderr, "Error: input file %s does not exist.\n", argv[1]);
		exit(1);
	}

	im = gdImageCreateFromJpeg(in);
	fclose(in);
	if (!im) {
		fprintf(stderr, "Error: input file %s is not in JPEG format.\n", argv[1]);
		exit(1);
	}

	out = fopen(argv[2], "wb");
	if (!out) {
		fprintf(stderr, "Error: can't write to output file %s\n", argv[2]);
		gdImageDestroy(im);
		exit(1);
	}

	gdImageAvif(im, out);

  printf("Wrote oufile %s\n. Success!\n", argv[1]);

	// gdImageGd(im, out);
	fclose(out);
	gdImageDestroy(im);

	return 0;
}
