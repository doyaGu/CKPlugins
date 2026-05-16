# =============================================================================
# CKPlugins - Virtools Plugin Collection
# =============================================================================
cmake_minimum_required(VERSION 3.16)

project(CKPlugins
        VERSION 2.1.0.14
        DESCRIPTION "Virtools Plugin Collection"
        LANGUAGES C CXX
)

# =============================================================================
# Determine if top-level project
# =============================================================================
if (CMAKE_VERSION VERSION_GREATER_EQUAL "3.21")
    if (PROJECT_IS_TOP_LEVEL)
        set(CKPLUGINS_IS_TOP_LEVEL ON)
    else ()
        set(CKPLUGINS_IS_TOP_LEVEL OFF)
    endif ()
else ()
    if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
        set(CKPLUGINS_IS_TOP_LEVEL ON)
    else ()
        set(CKPLUGINS_IS_TOP_LEVEL OFF)
    endif ()
endif ()

# =============================================================================
# Platform and build checks
# =============================================================================
if (CKPLUGINS_IS_TOP_LEVEL AND CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
    message(FATAL_ERROR "In-source builds not allowed. Run CMake from a separate directory: cmake -B build")
endif ()

# =============================================================================
# C++ standard
# =============================================================================
if (CKPLUGINS_IS_TOP_LEVEL)
    set(CMAKE_CXX_STANDARD 17)
    set(CMAKE_CXX_STANDARD_REQUIRED YES)
endif ()

# =============================================================================
# Project options
# =============================================================================
option(CKPLUGINS_BUILD_AVIREADER "Build AviReader plugin" ON)
option(CKPLUGINS_BUILD_IMAGEREADER "Build ImageReader plugin" ON)
option(CKPLUGINS_BUILD_WAVREADER "Build WavReader plugin" ON)
option(CKPLUGINS_BUILD_VIRTOOLSLOADER "Build VirtoolsLoader plugin" ON)
option(CKPLUGINS_BUILD_SHARED "Build shared libraries for plugins" ON)
option(CKPLUGINS_BUILD_STATIC "Build static libraries for plugins" OFF)
option(CKPLUGINS_BUILD_TESTS "Build unit tests" ${CKPLUGINS_IS_TOP_LEVEL})
option(CKPLUGINS_INSTALL "Generate install target" ${CKPLUGINS_IS_TOP_LEVEL})
option(CKPLUGINS_ENABLE_SDL3 "Build portable SDL3-compatible plugins" OFF)

if (NOT WIN32 AND NOT CKPLUGINS_ENABLE_SDL3)
    message(FATAL_ERROR "CKPlugins only supports non-Windows builds with CKPLUGINS_ENABLE_SDL3=ON.")
endif ()

if (CKPLUGINS_ENABLE_SDL3 AND NOT WIN32)
    set(CKPLUGINS_BUILD_AVIREADER OFF CACHE BOOL "Build AviReader plugin" FORCE)
endif ()

# =============================================================================
# CMake modules
# =============================================================================
list(PREPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# =============================================================================
# Top-level project settings
# =============================================================================
if (CKPLUGINS_IS_TOP_LEVEL)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")
    set(CMAKE_USE_RELATIVE_PATHS TRUE)
    set(CMAKE_SUPPRESS_REGENERATION TRUE)

    get_property(IS_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
    if (NOT IS_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
        message(STATUS "[CKPlugins] Setting build type to 'Release'")
        set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type" FORCE)
        set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
    endif ()

    if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
        set(CMAKE_INSTALL_PREFIX "${PROJECT_SOURCE_DIR}/install" CACHE PATH "Installation directory" FORCE)
        message(STATUS "[CKPlugins] Setting install directory to ${CMAKE_INSTALL_PREFIX}")
    endif ()

    set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
endif ()

# =============================================================================
# Compiler settings
# =============================================================================
if (MSVC)
    add_compile_definitions(
            _CRT_SECURE_NO_WARNINGS
            _CRT_NONSTDC_NO_WARNINGS
    )
endif ()

# =============================================================================
# Virtools SDK
# =============================================================================
if (NOT TARGET VxMath AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../VxMath/CMakeLists.txt")
    if (CKPLUGINS_BUILD_STATIC AND NOT CKPLUGINS_BUILD_SHARED)
        set(VXMATH_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(VXMATH_BUILD_STATIC ON CACHE BOOL "" FORCE)
    endif ()
    if (CKPLUGINS_INSTALL)
        set(VXMATH_INSTALL ON CACHE BOOL "Generate install target" FORCE)
    endif ()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../VxMath" "${CMAKE_CURRENT_BINARY_DIR}/VxMath")
endif ()

if (NOT TARGET CK2 AND EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CK2/CMakeLists.txt")
    if (CKPLUGINS_BUILD_STATIC AND NOT CKPLUGINS_BUILD_SHARED)
        set(CK2_BUILD_SHARED OFF CACHE BOOL "" FORCE)
        set(CK2_BUILD_STATIC ON CACHE BOOL "" FORCE)
    endif ()
    if (CKPLUGINS_INSTALL)
        set(CK2_INSTALL ON CACHE BOOL "Generate install target" FORCE)
    endif ()
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CK2" "${CMAKE_CURRENT_BINARY_DIR}/CK2")
endif ()

if (NOT TARGET VxMath OR NOT TARGET CK2)
    set(VIRTOOLS_SDK_PATH "" CACHE PATH "Path to the Virtools SDK")
    option(VIRTOOLS_SDK_FETCH_FROM_GIT "Fetch Virtools SDK from git if not found" OFF)
    set(VIRTOOLS_SDK_FETCH_FROM_GIT_PATH "" CACHE FILEPATH "Location to download SDK")

    if (NOT VIRTOOLS_SDK_PATH)
        if (DEFINED ENV{VIRTOOLS_SDK_PATH})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK_PATH}" CACHE PATH "Path to the Virtools SDK" FORCE)
        elseif (DEFINED ENV{VIRTOOLS_SDK})
            set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK}" CACHE PATH "Path to the Virtools SDK" FORCE)
        endif ()
    endif ()

    if (NOT VIRTOOLS_SDK_PATH)
        if (VIRTOOLS_SDK_FETCH_FROM_GIT)
            include(FetchContent)
            set(FETCHCONTENT_BASE_DIR_SAVE ${FETCHCONTENT_BASE_DIR})
            if (VIRTOOLS_SDK_FETCH_FROM_GIT_PATH)
                get_filename_component(FETCHCONTENT_BASE_DIR "${VIRTOOLS_SDK_FETCH_FROM_GIT_PATH}" REALPATH BASE_DIR "${CMAKE_SOURCE_DIR}")
            endif ()
            FetchContent_Declare(
                    virtools_sdk
                    GIT_REPOSITORY https://github.com/doyaGu/Virtools-SDK-2.1.git
                    GIT_TAG main
            )
            message(STATUS "[CKPlugins] Downloading Virtools SDK")
            FetchContent_MakeAvailable(virtools_sdk)
            set(VIRTOOLS_SDK_PATH "${virtools_sdk_SOURCE_DIR}" CACHE PATH "Path to the Virtools SDK" FORCE)
            set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
        else ()
            message(FATAL_ERROR "Virtools SDK location was not specified. Please set VIRTOOLS_SDK_PATH or enable VIRTOOLS_SDK_FETCH_FROM_GIT.")
        endif ()
    endif ()

    find_path(VIRTOOLS_SDK_INCLUDE_DIR CKAll.h
            HINTS $ENV{VIRTOOLS_SDK_PATH} ${VIRTOOLS_SDK_PATH}
            PATH_SUFFIXES Include Includes include includes
    )
    if (NOT VIRTOOLS_SDK_INCLUDE_DIR)
        message(FATAL_ERROR "Virtools SDK headers not found. Expected CKAll.h under ${VIRTOOLS_SDK_PATH}.")
    endif ()

    foreach (_lib IN ITEMS CK2 VxMath)
        find_library(VIRTOOLS_SDK_${_lib}_LIB NAMES ${_lib}
                HINTS $ENV{VIRTOOLS_SDK_PATH} ${VIRTOOLS_SDK_PATH}
                PATH_SUFFIXES Lib lib
        )
        if (NOT VIRTOOLS_SDK_${_lib}_LIB)
            message(FATAL_ERROR "Virtools SDK library '${_lib}' not found under ${VIRTOOLS_SDK_PATH}.")
        endif ()

        if (NOT TARGET ${_lib})
            add_library(${_lib} UNKNOWN IMPORTED)
            set_target_properties(${_lib} PROPERTIES
                    IMPORTED_LOCATION "${VIRTOOLS_SDK_${_lib}_LIB}"
                    INTERFACE_INCLUDE_DIRECTORIES "${VIRTOOLS_SDK_INCLUDE_DIR}"
            )
        endif ()
    endforeach ()
endif ()

# =============================================================================
# Track built components
# =============================================================================
set(CKPLUGINS_COMPONENTS "")

# =============================================================================
# Helper function for creating plugin targets
# =============================================================================
function(ckplugins_configure_version_resource OUT_VAR TARGET_NAME FILE_DESCRIPTION ORIGINAL_FILENAME)
    if (NOT CKPLUGINS_VERSION_OVERRIDE MATCHES "^[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+$")
        message(FATAL_ERROR "CKPLUGINS_VERSION_OVERRIDE must be a numeric four-part version")
    endif ()

    string(REPLACE "." ";" _ckplugins_version_parts "${CKPLUGINS_VERSION_OVERRIDE}")
    list(GET _ckplugins_version_parts 0 CKPLUGINS_RC_VERSION_MAJOR)
    list(GET _ckplugins_version_parts 1 CKPLUGINS_RC_VERSION_MINOR)
    list(GET _ckplugins_version_parts 2 CKPLUGINS_RC_VERSION_PATCH)
    list(GET _ckplugins_version_parts 3 CKPLUGINS_RC_VERSION_TWEAK)

    set(CKPLUGINS_RC_VERSION_STRING "${CKPLUGINS_VERSION_OVERRIDE}")
    set(CKPLUGINS_RC_TARGET "${TARGET_NAME}")
    set(CKPLUGINS_RC_DESCRIPTION "${FILE_DESCRIPTION}")
    set(CKPLUGINS_RC_ORIGINAL_FILENAME "${ORIGINAL_FILENAME}")
    set(_ckplugins_version_resource "${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}Version.generated.rc")

    configure_file(
            "${PROJECT_SOURCE_DIR}/Version.rc.in"
            "${_ckplugins_version_resource}"
            @ONLY
    )

    set(${OUT_VAR} "${_ckplugins_version_resource}" PARENT_SCOPE)
endfunction()

function(ckplugins_add_plugin TARGET_NAME)
    cmake_parse_arguments(PLUGIN "" "" "SOURCES;LINKS" ${ARGN})

    # Shared library
    if (CKPLUGINS_BUILD_SHARED)
        set(_plugin_sources ${PLUGIN_SOURCES})
        if (DEFINED CKPLUGINS_VERSION_OVERRIDE)
            list(FILTER _plugin_sources EXCLUDE REGEX "\\.rc$")
            if (WIN32)
                ckplugins_configure_version_resource(
                        _plugin_version_resource
                        ${TARGET_NAME}
                        "${TARGET_NAME} Plugin"
                        "${TARGET_NAME}.dll"
                )
                list(APPEND _plugin_sources "${_plugin_version_resource}")
            endif ()
        endif ()

        add_library(${TARGET_NAME} SHARED ${_plugin_sources})
        target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath ${PLUGIN_LINKS})
        set_target_properties(${TARGET_NAME} PROPERTIES
                RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
                LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        )
    endif ()

    # Static library (optional)
    if (CKPLUGINS_BUILD_STATIC)
        # Filter out .rc files for static library
        set(STATIC_SOURCES ${PLUGIN_SOURCES})
        list(FILTER STATIC_SOURCES EXCLUDE REGEX "\\.rc$")

        add_library(${TARGET_NAME}Static STATIC ${STATIC_SOURCES})
        target_include_directories(${TARGET_NAME}Static PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

        set(_ck2_dep CK2)
        if (TARGET CK2Static)
            set(_ck2_dep CK2Static)
        endif ()
        set(_vxmath_dep VxMath)
        if (TARGET VxMathStatic)
            set(_vxmath_dep VxMathStatic)
        endif ()

        target_link_libraries(${TARGET_NAME}Static PRIVATE ${_ck2_dep} ${_vxmath_dep} ${PLUGIN_LINKS})
        set_target_properties(${TARGET_NAME}Static PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        )
    endif ()

    # Installation
    if (CKPLUGINS_INSTALL)
        if (TARGET ${TARGET_NAME})
            install(TARGETS ${TARGET_NAME}
                    RUNTIME DESTINATION Plugins COMPONENT Runtime
                    LIBRARY DESTINATION Plugins COMPONENT Runtime
            )
        endif ()
        if (TARGET ${TARGET_NAME}Static)
            install(TARGETS ${TARGET_NAME}Static
                    ARCHIVE DESTINATION lib COMPONENT Development
            )
        endif ()
    endif ()
endfunction()

# =============================================================================
# Subdirectories
# =============================================================================
if (CKPLUGINS_BUILD_TESTS AND CKPLUGINS_IS_TOP_LEVEL)
    include(CTest)
endif ()

if (CKPLUGINS_BUILD_AVIREADER)
    add_subdirectory(AVIReader)
    list(APPEND CKPLUGINS_COMPONENTS "AVIReader")
endif ()

if (CKPLUGINS_BUILD_IMAGEREADER)
    add_subdirectory(ImageReader)
    list(APPEND CKPLUGINS_COMPONENTS "ImageReader")
endif ()

if (CKPLUGINS_BUILD_WAVREADER)
    add_subdirectory(WavReader)
    list(APPEND CKPLUGINS_COMPONENTS "WavReader")
endif ()

if (CKPLUGINS_BUILD_VIRTOOLSLOADER)
    add_subdirectory(VirtoolsLoader)
    list(APPEND CKPLUGINS_COMPONENTS "VirtoolsLoader")
endif ()

# =============================================================================
# Installation
# =============================================================================
if (CKPLUGINS_INSTALL)
    # Plugins are runtime components, no CMake targets export needed

    # Plugins are runtime components, no CMake config needed

    # CPack configuration
    set(CPACK_PACKAGE_NAME "CKPlugins")
    set(CPACK_PACKAGE_VENDOR "Virtools Community")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Virtools Plugin Collection")
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "CKPlugins")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")

    include(CPack)
endif ()

# =============================================================================
# Configuration summary
# =============================================================================
if (CKPLUGINS_IS_TOP_LEVEL)
    message(STATUS "")
    message(STATUS "============================================================")
    message(STATUS "CKPlugins Configuration Summary")
    message(STATUS "============================================================")
    message(STATUS "  Version:              ${PROJECT_VERSION}")
    message(STATUS "  Build Type:           ${CMAKE_BUILD_TYPE}")
    if (VIRTOOLS_SDK_PATH)
        message(STATUS "  Virtools SDK:         ${VIRTOOLS_SDK_PATH}")
    endif ()
    message(STATUS "  Components:           ${CKPLUGINS_COMPONENTS}")
    message(STATUS "  Install:              ${CKPLUGINS_INSTALL}")
    message(STATUS "  Install Prefix:       ${CMAKE_INSTALL_PREFIX}")
    message(STATUS "============================================================")
    message(STATUS "")
endif ()
