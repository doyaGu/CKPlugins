# CKPlugins

A collection of Virtools plugins extending file format support for the Virtools 3D development platform.

## Plugins

### ImageReader
Extends Virtools image reading capabilities with support for:
- **BMP** - Windows Bitmap format (various bit depths and compression)
- **PCX** - PC Paintbrush format
- **TGA** - Truevision TGA format (including RLE compression)

### WavReader
WAV audio file reader using dr_wav library. Supports:
- PCM (8/16/24/32-bit)
- IEEE float (32/64-bit)
- A-law, µ-law
- Microsoft ADPCM, IMA ADPCM

### VirtoolsLoader
Virtools composition loader for reading:
- Virtools compositions (.cmo files)
- Virtools objects (.nmo files)
- Virtools scripts (.nms files)
- Virtools player files (.vmo files)

## Building

**Requirements:**
- Windows OS
- Visual Studio
- CMake 3.14+
- Virtools SDK (set `VIRTOOLS_SDK_PATH` or enable `VIRTOOLS_SDK_FETCH_FROM_GIT`)

```bash
# Create and configure build directory
mkdir build
cd build
cmake ..

# Build all plugins
cmake --build . --config Release
```

Build outputs:
- `ImageReader.dll` - Image format reader
- `WavReader.dll` - WAV audio reader
- `VirtoolsLoader.dll` - Virtools composition loader

## Testing

Unit tests are available for ImageReader:

```bash
# Build tests
cmake --build . --config Release

# Run tests
ctest --config Release
```

## License

Apache License 2.0 - See [`LICENSE`](LICENSE) for details.
