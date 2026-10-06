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
if ((CKPLUGINS_BUILD_TESTS OR BUILD_TESTS) AND BUILD_TESTING)
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
    )

    # Link the targets so their include dirs, CK_LIB/VX_LIB and transitive
    # dependencies (VxMath, miniz) come along as usage requirements.
    if (TARGET CK2Static)
        target_link_libraries(ImageReaderTests PRIVATE CK2Static)
    elseif (TARGET CK2)
        target_link_libraries(ImageReaderTests PRIVATE CK2)
    elseif (VIRTOOLS_SDK_INCLUDE_DIR)
        target_include_directories(ImageReaderTests PRIVATE "${VIRTOOLS_SDK_INCLUDE_DIR}")
    elseif (VIRTOOLS_SDK_PATH)
        target_include_directories(ImageReaderTests PRIVATE "${VIRTOOLS_SDK_PATH}/Include")
    endif ()

    if (TARGET VxMathStatic)
        target_link_libraries(ImageReaderTests PRIVATE VxMathStatic)
    elseif (TARGET VxMath)
        target_link_libraries(ImageReaderTests PRIVATE VxMath)
    endif ()

    target_compile_features(ImageReaderTests PRIVATE cxx_std_11)
    set_target_properties(ImageReaderTests PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    target_compile_definitions(ImageReaderTests PRIVATE
            TEST_IMAGES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/images"
            TEST_REFERENCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/tests/reference"
            TEST_OUTPUT_DIR="${CMAKE_CURRENT_BINARY_DIR}/tests/output"
            VX_EXPORT=
    )

    add_test(NAME ImageReaderTests COMMAND ImageReaderTests)
    file(MAKE_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/tests/output")
endif ()
