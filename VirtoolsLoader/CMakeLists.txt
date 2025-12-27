add_library(VirtoolsLoader SHARED NemoLoader.cpp VirtoolsLoader.rc)
target_include_directories(VirtoolsLoader PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(VirtoolsLoader PRIVATE cxx_std_98)
target_compile_definitions(VirtoolsLoader PRIVATE
    $<$<C_COMPILER_ID:MSVC>:_CRT_SECURE_NO_WARNINGS>
    $<$<C_COMPILER_ID:MSVC>:_CRT_NONSTDC_NO_WARNINGS>
)
target_link_libraries(VirtoolsLoader PRIVATE CK2 VxMath)
