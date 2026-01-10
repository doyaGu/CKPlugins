# WavReader plugin using dr_wav for WAV decoding
# dr_wav is a public domain single-header library included in dr_wav.h
# No additional link libraries required (no WinMM/ACM dependencies)
add_library(WavReader SHARED WavReader.cpp WavReader.rc)
target_include_directories(WavReader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(WavReader PRIVATE cxx_std_98)
target_compile_definitions(WavReader PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
)
target_link_libraries(WavReader PRIVATE CK2 VxMath)

# Set output directory for the plugin
set_target_properties(WavReader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Installation
include(GNUInstallDirs)

install(TARGETS WavReader
    EXPORT CKPluginsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES
    WavReader.h
    dr_wav.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CKPlugins/WavReader
)
