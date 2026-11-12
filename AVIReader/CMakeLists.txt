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

ckplugins_tests_enabled(AVIReader _module_tests_enabled)
if (_module_tests_enabled)
    add_subdirectory(tests)
endif ()