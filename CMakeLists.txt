# =============================================================================
# CKPlugins - Virtools Plugin Collection
# =============================================================================
cmake_minimum_required(VERSION 3.16)

project(CKPlugins
        VERSION 2.1.0.14
        DESCRIPTION "Virtools Plugin Collection"
        LANGUAGES CXX
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

if (NOT WIN32)
    message(FATAL_ERROR "CKPlugins only supports Windows.")
endif ()

# =============================================================================
# C++ standard
# =============================================================================
if (NOT CMAKE_CXX_STANDARD)
    set(CMAKE_CXX_STANDARD 98)
endif ()
if (NOT DEFINED CMAKE_CXX_STANDARD_REQUIRED)
    set(CMAKE_CXX_STANDARD_REQUIRED NO)
endif ()
if (NOT DEFINED CMAKE_CXX_EXTENSIONS)
    set(CMAKE_CXX_EXTENSIONS YES)
endif ()

# =============================================================================
# Project options
# =============================================================================
option(CKPLUGINS_BUILD_AVIREADER "Build AviReader plugin" ON)
option(CKPLUGINS_BUILD_IMAGEREADER "Build ImageReader plugin" ON)
option(CKPLUGINS_BUILD_WAVREADER "Build WavReader plugin" ON)
option(CKPLUGINS_BUILD_VIRTOOLSLOADER "Build VirtoolsLoader plugin" ON)
option(CKPLUGINS_BUILD_STATIC "Build static libraries for plugins" OFF)
option(CKPLUGINS_BUILD_TESTS "Build unit tests" ${CKPLUGINS_IS_TOP_LEVEL})
option(CKPLUGINS_INSTALL "Generate install target" ${CKPLUGINS_IS_TOP_LEVEL})

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
                    Virtools_SDK
                    GIT_REPOSITORY https://github.com/doyaGu/Virtools-SDK-2.1.git
                    GIT_TAG main
            )
            message(STATUS "[CKPlugins] Downloading Virtools SDK")
            FetchContent_MakeAvailable(Virtools_SDK)
            set(VIRTOOLS_SDK_PATH "${virtools_sdk_SOURCE_DIR}" CACHE PATH "Path to the Virtools SDK" FORCE)
            set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
        else ()
            message(FATAL_ERROR "Virtools SDK location was not specified. Please set VIRTOOLS_SDK_PATH or enable VIRTOOLS_SDK_FETCH_FROM_GIT.")
        endif ()
    endif ()

    find_package(VirtoolsSDK REQUIRED HINTS "${VIRTOOLS_SDK_PATH}")
endif ()

# =============================================================================
# Track built components
# =============================================================================
set(CKPLUGINS_COMPONENTS "")

# =============================================================================
# Helper function for creating plugin targets
# =============================================================================
function(ckplugins_add_plugin TARGET_NAME)
    cmake_parse_arguments(PLUGIN "" "" "SOURCES;LINKS" ${ARGN})

    # Shared library (always built)
    add_library(${TARGET_NAME} SHARED ${PLUGIN_SOURCES})
    target_include_directories(${TARGET_NAME} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
    target_compile_features(${TARGET_NAME} PRIVATE cxx_std_98)
    target_link_libraries(${TARGET_NAME} PRIVATE CK2 VxMath ${PLUGIN_LINKS})
    set_target_properties(${TARGET_NAME} PROPERTIES
            RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
            LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
            ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
    )

    # Static library (optional)
    if (CKPLUGINS_BUILD_STATIC)
        # Filter out .rc files for static library
        set(STATIC_SOURCES ${PLUGIN_SOURCES})
        list(FILTER STATIC_SOURCES EXCLUDE REGEX "\\.rc$")

        add_library(${TARGET_NAME}Static STATIC ${STATIC_SOURCES})
        target_include_directories(${TARGET_NAME}Static PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        target_compile_features(${TARGET_NAME}Static PRIVATE cxx_std_98)
        target_link_libraries(${TARGET_NAME}Static PRIVATE CK2 VxMath ${PLUGIN_LINKS})
        set_target_properties(${TARGET_NAME}Static PROPERTIES
                ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
        )
    endif ()

    # Installation
    if (CKPLUGINS_INSTALL)
        install(TARGETS ${TARGET_NAME}
                RUNTIME DESTINATION Plugins
                LIBRARY DESTINATION Plugins
        )
        if (TARGET ${TARGET_NAME}Static)
            install(TARGETS ${TARGET_NAME}Static
                    ARCHIVE DESTINATION lib
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
