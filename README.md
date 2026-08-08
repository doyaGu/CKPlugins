# CKPlugins

CKPlugins provides the file and media plugins used by the Virtools-compatible Ballanced runtime.

## Plugins

- **AVIReader**: RIFF/AVI demuxing and video-frame decoding, including RGB, RLE, MJPEG, Microsoft Video 1, and packed YUV paths
- **ImageReader**: BMP, PCX, and TGA image loading
- **WavReader**: WAV audio loading through dr_wav, including PCM, float, A-law, mu-law, and ADPCM formats
- **VirtoolsLoader**: Virtools composition and object formats such as CMO, NMO, NMS, and VMO

## Support scope

The instructions in this document describe CKPlugins' `sdl` branch. That branch is continuously built through [Ballanced](https://github.com/doyaGu/Ballanced) on Windows, Linux, and macOS. The Ballanced root presets define the supported full-runtime matrix.

Standalone builds use this repository's generic CMake flow. Output names are platform-native: `.dll` on Windows, `.so` on Linux, and `.dylib` on macOS.

## Building

### Recommended: Ballanced superproject

```bash
git clone --recurse-submodules https://github.com/doyaGu/Ballanced.git
cd Ballanced
cmake --preset linux-x64-runtime # choose the preset for your host
cmake --build --preset linux-x64-runtime-stage-release
```

The staged modules are installed under `build/<preset>/stage/Plugins/`.

### Standalone

Requirements:

- CMake 3.16+
- A desktop C++ toolchain
- CK2 and VxMath, preferably as adjacent `../CK2` and `../VxMath` checkouts
- A Virtools SDK supplied with `VIRTOOLS_SDK_PATH`, or `VIRTOOLS_SDK_FETCH_FROM_GIT=ON`, when local CK2/VxMath projects are unavailable
- Network access when test dependencies or the SDK must be fetched

```bash
cmake -S . -B build \
  -DCKPLUGINS_BUILD_TESTS=ON \
  -DVIRTOOLS_SDK_FETCH_FROM_GIT=ON
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

When building from a Ballanced source checkout, sibling CK2 and VxMath projects are detected automatically and the SDK-fetch option is unnecessary.

### CMake options

- `CKPLUGINS_BUILD_AVIREADER`
- `CKPLUGINS_BUILD_IMAGEREADER`
- `CKPLUGINS_BUILD_WAVREADER`
- `CKPLUGINS_BUILD_VIRTOOLSLOADER`
- `CKPLUGINS_BUILD_SHARED` / `CKPLUGINS_BUILD_STATIC`
- `CKPLUGINS_BUILD_TESTS`
- `CKPLUGINS_INSTALL`

## Testing

The current standalone suite covers AVIReader and ImageReader. WavReader and VirtoolsLoader are built but do not currently have equivalent standalone regression suites.

## Versioning

CKPlugins is versioned independently. Ballanced releases pin an exact plugin commit.

## License

Apache License 2.0. See [LICENSE](LICENSE).
