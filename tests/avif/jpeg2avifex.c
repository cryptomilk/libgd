/**
 * A short program which converts a .jpg file into a .avif file - 
 * just to get a little practice with the basic functionality.
 */

#ifdef HAVE_CONFIG_H
#include "config.h"
#endif /* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef HAVE_UNISTD_H
#include <unistd.h>
#endif

#include "gd.h"

#ifdef HAVE_LIBAVIF
#include <avif/avif.h>

static void usage() {
	fprintf(stderr, "Usage: jpeg2avifex [-q quality] [-s speed] infile.jpg outfile.avif\n");
	exit(1);
}

int main(int argc, char **argv)
{
	gdImagePtr im;
	FILE *in, *out;
	int c;
	int speed = -1, quality = -1; // use default values if unspecified
	char *infile, *outfile;

	if (argc < 3) {
		usage();
	}

	while ((c = getopt(argc, argv, "q:s:")) != -1) {
		switch (c) {
			case 'q':
				quality = atoi(optarg);
				break;

			case 's':
				speed = atoi(optarg);
				break;

			default:
				usage();
		}
	}

	if (optind > argc - 2)
		usage();

  infile = malloc((strlen(argv[optind]) + 1) * sizeof(char));
	strcpy(infile, argv[optind++]);

  outfile = malloc((strlen(argv[optind]) + 1) * sizeof(char));
	strcpy(outfile, argv[optind]);

	printf("Reading infile %s\n", infile);

	in = fopen(infile, "rb");
	if (!in) {
		fprintf(stderr, "Error: input file %s does not exist.\n", infile);
		exit(1);
	}

	im = gdImageCreateFromJpeg(in);
	fclose(in);
	if (!im) {
		fprintf(stderr, "Error: input file %s is not in JPEG format.\n", infile);
		exit(1);
	}

	out = fopen(outfile, "wb");
	if (!out) {
		fprintf(stderr, "Error: can't write to output file %s\n", outfile);
		gdImageDestroy(im);
		exit(1);
	}

	fprintf(stderr, "Encoding...\n");

	gdImageAvifEx(im, out, quality, speed);

  printf("Wrote outfile %s.\n", outfile);

	fclose(out);
	gdImageDestroy(im);
	gdFree(infile);
	gdFree(outfile);

	exit(0);
}

#endif