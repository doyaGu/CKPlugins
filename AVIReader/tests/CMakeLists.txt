# =============================================================================
# AVIReader unit tests
# =============================================================================
# Tests cover the three Virtools-independent layers:
#   - AviTypes.h    (pure constexpr/inline helpers)
#   - RiffReader     (RIFF file traversal)
#   - AviDemuxer     (AVI container parsing + frame index)
#   - FrameDecoder   (codec decode: Raw, RLE8, MJPEG, YUV422, CRAM)
#
# No CK2/VxMath dependency -- source files are compiled directly into each
# test executable.
# =============================================================================

include(CTest)
enable_testing()
include(GoogleTest)

# ---------------------------------------------------------------------------
# GoogleTest (reuse if already fetched by a sibling project)
# ---------------------------------------------------------------------------
include(FetchContent)
FetchContent_Declare(
    googletest
    GIT_REPOSITORY https://github.com/google/googletest.git
    GIT_TAG        v1.17.0
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
if (NOT TARGET gtest_main)
    FetchContent_MakeAvailable(googletest)
endif ()

# ---------------------------------------------------------------------------
# Shared AVIReader source files compiled into each test executable
# ---------------------------------------------------------------------------
set(AVIREADER_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(AVIREADER_COMMON_SOURCES
    "${AVIREADER_SRC_DIR}/RiffReader.cpp"
    "${AVIREADER_SRC_DIR}/AviDemuxer.cpp"
    "${AVIREADER_SRC_DIR}/FrameDecoder.cpp"
    "${AVIREADER_SRC_DIR}/StbImageImpl.cpp"
)

# ---------------------------------------------------------------------------
# Helper function
# ---------------------------------------------------------------------------
function(avireader_add_test TEST_NAME)
    cmake_parse_arguments(TEST "" "" "SOURCES;EXTRA_SOURCES" ${ARGN})

    add_executable(${TEST_NAME} ${TEST_SOURCES} ${TEST_EXTRA_SOURCES})
    target_include_directories(${TEST_NAME} PRIVATE
        "${AVIREADER_SRC_DIR}"
        "${CMAKE_CURRENT_SOURCE_DIR}"
    )
    target_link_libraries(${TEST_NAME} PRIVATE gtest_main gmock stb)
    set_target_properties(${TEST_NAME} PROPERTIES
        FOLDER "Tests/AVIReader"
        RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    )
    if (MSVC)
        target_compile_definitions(${TEST_NAME} PRIVATE _CRT_SECURE_NO_WARNINGS)
    endif ()
    gtest_discover_tests(${TEST_NAME})
endfunction()

# =============================================================================
# Test targets
# =============================================================================

avireader_add_test(AviTypesFourCcTest
    SOURCES AviTypesFourCcTest.cpp
)

avireader_add_test(RiffReaderTest
    SOURCES RiffReaderTest.cpp
    EXTRA_SOURCES "${AVIREADER_SRC_DIR}/RiffReader.cpp"
)

avireader_add_test(AviDemuxerTest
    SOURCES AviDemuxerTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(FrameDecoderFactoryTest
    SOURCES FrameDecoderFactoryTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(RawFrameDecoderTest
    SOURCES RawFrameDecoderTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(Rle8FrameDecoderTest
    SOURCES Rle8FrameDecoderTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(Rle4FrameDecoderTest
    SOURCES Rle4FrameDecoderTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(MjpegFrameDecoderTest
    SOURCES MjpegFrameDecoderTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(PackedYuv422FrameDecoderTest
    SOURCES PackedYuv422FrameDecoderTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)

avireader_add_test(Msvideo1FrameDecoderTest
    SOURCES Msvideo1FrameDecoderTest.cpp
    EXTRA_SOURCES ${AVIREADER_COMMON_SOURCES}
)
