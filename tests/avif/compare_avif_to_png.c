#include <stdio.h>
#include "gd.h"
#include "gdtest.h"

#define TEST_FILENAME "sunset"
#define TEST_PNG_PATH "avif/" TEST_FILENAME ".png"

int main() {
	FILE *fp;
	gdImagePtr imageFromPng, imageFromAvif;
	void *avifImageDataPtr;
	int size;
	int failed = GD_FALSE;

// First, encode a PNG into an AVIF (with the GD format as an intermediary),
// then compare the result with the original PNG.

	fp = gdTestFileOpen(TEST_PNG_PATH);
	imageFromPng = gdImageCreateFromPng(fp);
	fclose(fp);
	gdTestAssertMsg(imageFromPng != NULL, "gdImageCreateFromPng failed\n");

	avifImageDataPtr = gdImageAvifPtrEx(imageFromPng, &size, 100, 10);
	gdTestAssertMsg(avifImageDataPtr != NULL, "gdImageAvifPtrEx failed\n");

	imageFromAvif = gdImageCreateFromAvifPtr(size, avifImageDataPtr);
	gdTestAssertMsg(imageFromAvif != NULL, "gdImageCreateFromAvifPtr failed\n");

	gdTestAssertMsg(
		gdTestImageCompareToImage(TEST_PNG_PATH, __LINE__, __FILE__, imageFromPng, imageFromAvif),
		"Encoded AVIF image did not match original PNG"
	);

// Then, decode an AVIF into a GD format, and compare that with the orginal PNG.

// TODO: Let's add this after figuring out why the previous tests are failing,
// which they're doing because R/G/B values can differ by 1.
}
