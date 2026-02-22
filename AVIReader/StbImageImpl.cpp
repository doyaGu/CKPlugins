// ===========================================================================
// StbImageImpl.cpp -- single translation unit for stb_image.
//
// This file compiles the stb_image implementation exactly once into the
// AVIReader plugin.  Other .cpp files in this plugin can #include
// "stb_image.h" for declarations only (without defining the implementation
// macro).
//
// We configure stb_image for JPEG-only decoding to minimise binary size
// and compile time.
// ===========================================================================

// Only enable JPEG decoding; disable PNG, BMP, GIF, HDR, PIC, PNM, TGA.
#define STBI_ONLY_JPEG
#define STBI_NO_PNG
#define STBI_NO_BMP
#define STBI_NO_GIF
#define STBI_NO_HDR
#define STBI_NO_PIC
#define STBI_NO_PNM
#define STBI_NO_TGA

// We do not need file I/O through stb (we feed memory buffers).
#define STBI_NO_STDIO

#define STB_IMAGE_IMPLEMENTATION
#include "stb_image.h"
