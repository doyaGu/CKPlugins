# WavReader plugin using dr_wav for WAV decoding
# dr_wav is a public domain single-header library
ckplugins_add_plugin(WavReader
        SOURCES
        WavReader.cpp
        WavReader.rc
)
