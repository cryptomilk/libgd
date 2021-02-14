IF(AVIF_FOUND)
LIST(APPEND TESTS_FILES
	avif_ptr_double_free
	avif_im2im
	avif_null
	png2avif
)

IF(JPEG_FOUND)
LIST(APPEND TESTS_FILES
	avif2jpeg
	jpeg2avif
	jpeg2avifex
)
ENDIF(JPEG_FOUND)

ENDIF(AVIF_FOUND)

ADD_GD_TESTS()
