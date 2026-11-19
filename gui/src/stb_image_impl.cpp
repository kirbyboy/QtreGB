// Single translation unit for the stb_image decoder used by the About logo.
// ImGuiFileDialog only compiles stb_image when USE_THUMBNAILS is enabled.
#include <cstdlib>

#define STBI_MALLOC(size) std::malloc(size)
#define STBI_REALLOC(pointer, size) std::realloc(pointer, size)
#define STBI_FREE(pointer) std::free(pointer)

#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#endif
#define STB_IMAGE_IMPLEMENTATION
#include "stb/stb_image.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
