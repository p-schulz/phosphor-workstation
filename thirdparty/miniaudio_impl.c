/* Single translation unit holding the miniaudio implementation used by PHOSPHOR: device I/O
   (phosphor_platform) and decoders (sample import in phosphor_core). Ogg Vorbis comes from the
   stb_vorbis copy bundled with miniaudio (extras/stb_vorbis.c), included the way miniaudio's
   documentation describes: header first, implementation after miniaudio. Configuration macros are
   set on the target in cmake/Dependencies.cmake so every includer sees the same ones. */
#define STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"

#define MINIAUDIO_IMPLEMENTATION
#include "miniaudio.h"

#undef STB_VORBIS_HEADER_ONLY
#include "extras/stb_vorbis.c"
