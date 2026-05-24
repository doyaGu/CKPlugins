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

    if (TARGET CK2Static)
        target_include_directories(ImageReaderTests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../../CK2/include"
        )
        target_link_libraries(ImageReaderTests PRIVATE "$<TARGET_FILE:CK2Static>")
        add_dependencies(ImageReaderTests CK2Static)
    elseif (TARGET CK2)
        get_target_property(_ck2_test_includes CK2 INTERFACE_INCLUDE_DIRECTORIES)
        if (_ck2_test_includes)
            target_include_directories(ImageReaderTests PRIVATE ${_ck2_test_includes})
        endif ()
        target_link_libraries(ImageReaderTests PRIVATE CK2)
    elseif (VIRTOOLS_SDK_INCLUDE_DIR)
        target_include_directories(ImageReaderTests PRIVATE "${VIRTOOLS_SDK_INCLUDE_DIR}")
    elseif (VIRTOOLS_SDK_PATH)
        target_include_directories(ImageReaderTests PRIVATE "${VIRTOOLS_SDK_PATH}/Include")
    endif ()

    if (TARGET VxMathStatic)
        target_include_directories(ImageReaderTests PRIVATE
                "${CMAKE_CURRENT_SOURCE_DIR}/../../VxMath/include"
                "${CMAKE_CURRENT_SOURCE_DIR}/../../VxMath/deps/stb"
        )
        target_link_libraries(ImageReaderTests PRIVATE "$<TARGET_FILE:VxMathStatic>")
        add_dependencies(ImageReaderTests VxMathStatic)
    elseif (TARGET VxMath)
        get_target_property(_vxmath_test_includes VxMath INTERFACE_INCLUDE_DIRECTORIES)
        if (_vxmath_test_includes)
            target_include_directories(ImageReaderTests PRIVATE ${_vxmath_test_includes})
        endif ()
        target_link_libraries(ImageReaderTests PRIVATE VxMath)
    endif ()

    if (TARGET miniz)
        target_link_libraries(ImageReaderTests PRIVATE miniz)
    endif ()

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
