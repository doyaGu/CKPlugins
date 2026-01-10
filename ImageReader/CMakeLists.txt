# ImageReader plugin
# Implements BMP, TGA, and PCX format reading/writing
add_library(ImageReader SHARED
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
target_include_directories(ImageReader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(ImageReader PRIVATE cxx_std_98)
target_compile_definitions(ImageReader PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
)
target_link_libraries(ImageReader PRIVATE CK2 VxMath)

# Set output directory for the plugin
set_target_properties(ImageReader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Installation
include(GNUInstallDirs)

install(TARGETS ImageReader
    EXPORT CKPluginsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    ImageReader.h
    BmpReader.h
    TgaReader.h
    PcxReader.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CKPlugins/ImageReader
)

# =============================================================================
# Testing
# =============================================================================

if (BUILD_TESTS AND BUILD_TESTING)

    # Test executable - uses real Virtools SDK headers
    add_executable(ImageReaderTests
        tests/TestFramework.h
        tests/TestMain.cpp
        tests/BmpReaderTests.cpp
        tests/TgaReaderTests.cpp
        tests/PcxReaderTests.cpp
        # Include the reader sources directly for testing
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

    # Use C++11 for tests (CRC32 constexpr, etc.) while keeping library at C++98
    target_compile_features(ImageReaderTests PRIVATE cxx_std_11)
    target_compile_definitions(ImageReaderTests PRIVATE
        $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
        $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
    )

    # Define test data paths
    set(TEST_IMAGES_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/images")
    set(TEST_REFERENCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/tests/reference")
    set(TEST_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/tests/output")

    target_compile_definitions(ImageReaderTests PRIVATE
        TEST_IMAGES_DIR="${TEST_IMAGES_DIR}"
        TEST_REFERENCE_DIR="${TEST_REFERENCE_DIR}"
        TEST_OUTPUT_DIR="${TEST_OUTPUT_DIR}"
        VX_EXPORT=
    )

    # Register with CTest
    add_test(NAME ImageReaderTests COMMAND ImageReaderTests)

    # Create output directory
    file(MAKE_DIRECTORY ${TEST_OUTPUT_DIR})
endif()
