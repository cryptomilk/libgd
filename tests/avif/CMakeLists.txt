IF(AVIF_FOUND)
LIST(APPEND TESTS_FILES
	avif_ptr_double_free
	avif_im2im
	avif_null
)

IF(PNG_FOUND)
LIST(APPEND TESTS_FILES
	png2avif
	compare_avif_to_png
)
ENDIF(PNG_FOUND)

IF(JPEG_FOUND)
LIST(APPEND TESTS_FILES
	avif2jpeg
	jpeg2avif
	jpeg2avifex
)
ENDIF(JPEG_FOUND)

ENDIF(AVIF_FOUND)

ADD_GD_TESTS()
