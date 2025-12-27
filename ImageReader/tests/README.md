# CKImageReader Tests

This directory contains unit tests for the CKImageReader library.

## Test Coverage

- **BMP Reader** - Tests various BMP formats and edge cases
- **PCX Reader** - Tests PCX image format support
- **TGA Reader** - Tests TGA image format support (including RLE compression)

## Structure

```
tests/
├── BmpReaderTests.cpp    # BMP format tests
├── PcxReaderTests.cpp    # PCX format tests
├── TgaReaderTests.cpp    # TGA format tests
├── TestMain.cpp          # Test entry point
├── TestFramework.h       # Test framework utilities
└── images/               # Test images organized by format
    ├── bmp/              # BMP test images
    ├── pcx/              # PCX test images
    └── tga/              # TGA test images
```

## Running Tests

Build and run tests using CMake:

```bash
# Configure and build
cmake -B build -S .
cmake --build build

# Run tests
ctest --test-dir build
```

Or run the test executable directly:

```bash
./build/Debug/ImageReaderTests.exe
```

## Origin

Test images/fixtures in `tests/images/` are adapted from the [image-rs/image](https://github.com/image-rs/image/tree/main/tests) project.
