# ImageReader plugin - BMP, TGA, and PCX format reading/writing
ckplugins_add_plugin(ImageReader
        SOURCES
        ImageReader.cpp
        ImageReader.h
        BmpReader.h
        BmpReader.cpp
        TgaReader.h
        TgaReader.cpp
        PcxReader.h
        PcxReader.cpp
        ImageReader.rc
)

# =============================================================================
# Testing
# =============================================================================
if (BUILD_TESTS AND BUILD_TESTING)
    add_executable(ImageReaderTests
            tests/TestFramework.h
            tests/TestMain.cpp
            tests/BmpReaderTests.cpp
            tests/TgaReaderTests.cpp
            tests/PcxReaderTests.cpp
            ImageReader.h
            ImageReader.cpp
            BmpReader.h
            BmpReader.cpp
            TgaReader.h
            TgaReader.cpp
            PcxReader.h
            PcxReader.cpp
    )

    target_include_directories(ImageReaderTests PRIVATE
            "${CMAKE_CURRENT_SOURCE_DIR}"
            "${CMAKE_CURRENT_SOURCE_DIR}/tests"
            "${VIRTOOLS_SDK_PATH}/Include"
    )

    target_compile_features(ImageReaderTests PRIVATE cxx_std_11)
    target_compile_definitions(ImageReaderTests PRIVATE
            TEST_IMAGES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/images"
            TEST_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference"
            TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/output"
            VX_EXPORT=
    )

    add_test(NAME ImageReaderTests COMMAND ImageReaderTests)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/output")
endif ()
