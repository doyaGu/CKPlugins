add_library(AVIReader SHARED AVIReader.cpp AVIReader.h)
target_include_directories(AVIReader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(AVIReader PRIVATE cxx_std_98)
target_compile_definitions(AVIReader PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
)
target_link_libraries(AVIReader PRIVATE CK2 VxMath Vfw32)

# Set output directory for the plugin
set_target_properties(AVIReader PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Installation
include(GNUInstallDirs)

install(TARGETS AVIReader
    EXPORT CKPluginsTargets
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
)

install(FILES AVIReader.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/CKPlugins/AVIReader
)