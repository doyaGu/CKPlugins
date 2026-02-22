ckplugins_add_plugin(AVIReader
        SOURCES
        AviTypes.h
        RiffReader.h
        RiffReader.cpp
        AviDemuxer.h
        AviDemuxer.cpp
        FrameDecoder.h
        FrameDecoder.cpp
        StbImageImpl.cpp
        AVIReader.cpp
        AVIReader.h
        LINKS
        stb
)

if (CKPLUGINS_BUILD_TESTS)
    add_subdirectory(tests)
endif ()