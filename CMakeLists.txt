cmake_minimum_required(VERSION 3.14)

project(CKPlugins VERSION 2.1.0.14 LANGUAGES CXX)

# Use folders to organize targets in an IDE
set_property(GLOBAL PROPERTY USE_FOLDERS ON)
set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")

if (NOT WIN32)
    message(FATAL_ERROR "Only support Windows.")
endif ()

# Use relative paths
if (WIN32)
    set(CMAKE_USE_RELATIVE_PATHS TRUE)
    set(CMAKE_SUPPRESS_REGENERATION TRUE)
endif ()

if (NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as no build type was specified")
    set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the build type (Debug/Release)" FORCE)
endif ()

if (CMAKE_INSTALL_PREFIX_INITIALIZED_TO_DEFAULT)
    set(CMAKE_INSTALL_PREFIX "${CMAKE_BINARY_DIR}/install" CACHE PATH "The root directory of the installation" FORCE)
    message(STATUS "Setting default install directory to ${CMAKE_INSTALL_PREFIX} as no install directory was specified")
endif ()

# Generate a CompilationDatabase (compile_commands.json)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

set(VIRTOOLS_SDK_PATH "" CACHE PATH "Path to the Virtools SDK")
option(VIRTOOLS_SDK_FETCH_FROM_GIT "Set to ON to fetch copy of SDK from git if not otherwise locatable" OFF)
set(VIRTOOLS_SDK_FETCH_FROM_GIT_PATH "" CACHE FILEPATH "Location to download SDK")

if (NOT VIRTOOLS_SDK_PATH)
    if (DEFINED ENV{VIRTOOLS_SDK_PATH})
        set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK_PATH}" CACHE PATH "Path to the Virtools SDK" FORCE)
    elseif (DEFINED ENV{VIRTOOLS_SDK})
        set(VIRTOOLS_SDK_PATH "$ENV{VIRTOOLS_SDK}" CACHE PATH "Path to the Virtools SDK" FORCE)
    endif ()
endif ()

if (NOT VIRTOOLS_SDK_PATH)
    if (NOT VIRTOOLS_SDK_FETCH_FROM_GIT)
        message(FATAL_ERROR "Virtools SDK location was not specified. Please set VIRTOOLS_SDK_PATH or set VIRTOOLS_SDK_FETCH_FROM_GIT to on to fetch from git.")
    else ()
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
        message(STATUS "Downloading Virtools SDK")
        FetchContent_MakeAvailable(Virtools_SDK)
        set(VIRTOOLS_SDK_PATH "${virtools_sdk_SOURCE_DIR}" CACHE PATH "Path to the Virtools SDK" FORCE)
        set(FETCHCONTENT_BASE_DIR ${FETCHCONTENT_BASE_DIR_SAVE})
    endif ()
endif ()

find_package(VirtoolsSDK REQUIRED HINTS "${VIRTOOLS_SDK_PATH}")

option(BUILD_IMAGEREADER "Build ImageReader plugin" ON)
option(BUILD_WAVREADER "Build WavReader plugin" ON)
option(BUILD_VIRTOOLSLOADER "Build VirtoolsLoader plugin" ON)
option(BUILD_TESTS "Build unit tests" ON)

if (BUILD_TESTS)
    include(CTest)
endif ()

if (BUILD_IMAGEREADER)
    add_subdirectory(ImageReader)
endif ()

if (BUILD_WAVREADER)
    add_subdirectory(WavReader)
endif ()

if (BUILD_VIRTOOLSLOADER)
    add_subdirectory(VirtoolsLoader)
endif ()
