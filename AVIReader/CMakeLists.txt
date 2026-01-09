add_library(AVIReader SHARED AVIReader.cpp AVIReader.h)
target_include_directories(AVIReader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(AVIReader PRIVATE cxx_std_98)
target_compile_definitions(AVIReader PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
)
target_link_libraries(AVIReader PRIVATE CK2 VxMath)