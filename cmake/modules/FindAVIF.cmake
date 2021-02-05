# - Find the native AVIF includes and library
#

# This module defines
#  AVIF_INCLUDE_DIR, where to find avif.h
#  AVIF_LIBRARIES, the libraries to link against to use AVIF.
#  AVIF_FOUND, If false, do not try to use AVIF.
# also defined, but not for general use are
#  AVIF_LIBRARY, where to find the AVIF library.
#
# AVIF needs an encoder. Several are available.
# You can choose an encoder by setting AVIF_ENCODER.
# Presently this is set to "aom", a standard encoder.
# This module also defines AVIF_ENCODER_LIBRARY
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are
# met:
#
#  * Redistributions of source code must retain the above copyright notice,
#    this list of conditions and the following disclaimer.
#
#  * Redistributions in binary form must reproduce the above copyright notice,
#    this list of conditions and the following disclaimer in the documentation
#    and/or other materials provided with the distribution.
#
#  * The names of Kitware, Inc., the Insight Consortium, or the names of
#    any consortium members, or of any contributors, may not be used to
#    endorse or promote products derived from this software without
#    specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER AND CONTRIBUTORS ``AS IS''
# AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
# IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
# ARE DISCLAIMED. IN NO EVENT SHALL THE AUTHORS OR CONTRIBUTORS BE LIABLE FOR
# ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
# DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
# SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
# CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
# OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

find_path(AVIF_INCLUDE_DIR avif.h
  /usr/local/include/avif
  /usr/include/avif
)

# Note that another AVIF encoder could be used here instead. libavif supports a few.
set (AVIF_ENCODER aom)

set(AVIF_NAMES ${AVIF_NAMES} avif)
find_library(AVIF_LIBRARY
  NAMES ${AVIF_NAMES}
  PATHS /usr/lib64 /usr/lib /usr/local/lib
)

if (AVIF_LIBRARY AND AVIF_INCLUDE_DIR)
  set(AVIF_INCLUDE_DIR ${AVIF_INCLUDE_DIR})
  set(AVIF_LIBRARIES ${AVIF_LIBRARY})
  set(AVIF_FOUND "YES")
endif (AVIF_LIBRARY AND AVIF_INCLUDE_DIR)

find_library(AVIF_ENCODER_LIBRARY
  NAMES aom
  PATHS /usr/lib64 /usr/lib /usr/local/lib
)

if (AVIF_ENCODER_LIBRARY)
  if (NOT AVIF_FIND_QUIETLY)
    message(STATUS "Found AVIF encoder: ${AVIF_ENCODER_LIBRARY}")
  endif (NOT AVIF_FIND_QUIETLY)
else (AVIF_ENCODER_LIBRARY)
  if (AVIF_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find AVIF encoder library")
  endif (AVIF_FIND_REQUIRED)
endif (AVIF_ENCODER_LIBRARY)

if (AVIF_FOUND)
  if (NOT AVIF_FIND_QUIETLY)
    message(STATUS "Found AVIF: ${AVIF_LIBRARY}")
  endif (NOT AVIF_FIND_QUIETLY)
else (AVIF_FOUND)
  if (AVIF_FIND_REQUIRED)
    message(FATAL_ERROR "Could not find AVIF library")
  endif (AVIF_FIND_REQUIRED)
endif (AVIF_FOUND)

mark_as_advanced(AVIF_INCLUDE_DIR AVIF_LIBRARY )
set(AVIF_LIBRARIES ${AVIF_LIBRARY} ${AVIF_ENCODER_LIBRARY})
