cmake_minimum_required(VERSION 3.14)

project(CKPlugins VERSION 2.1.0.14 LANGUAGES CXX)

# Determine if CKPlugins is built as a subproject (using add_subdirectory) or if it is the master project
if (CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
    set(CKPLUGINS_STANDALONE TRUE)
else ()
    set(CKPLUGINS_STANDALONE FALSE)
endif ()

# Use folders to organize targets in an IDE
if (CKPLUGINS_STANDALONE)
    set_property(GLOBAL PROPERTY USE_FOLDERS ON)
    set_property(GLOBAL PROPERTY PREDEFINED_TARGETS_FOLDER "CMakeTargets")
endif ()

if (NOT WIN32)
    message(FATAL_ERROR "Only support Windows.")
endif ()

# Use relative paths
if (WIN32 AND CKPLUGINS_STANDALONE)
    set(CMAKE_USE_RELATIVE_PATHS TRUE)
    set(CMAKE_SUPPRESS_REGENERATION TRUE)
endif ()

if (CKPLUGINS_STANDALONE)
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
endif ()

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

option(BUILD_AVIREADER "Build AviReader plugin" ON)
option(BUILD_IMAGEREADER "Build ImageReader plugin" ON)
option(BUILD_WAVREADER "Build WavReader plugin" ON)
option(BUILD_VIRTOOLSLOADER "Build VirtoolsLoader plugin" ON)
option(BUILD_TESTS "Build unit tests" ON)

# Track built components for package configuration
set(CKPLUGINS_COMPONENTS "")

if (BUILD_TESTS AND CKPLUGINS_STANDALONE)
    include(CTest)
endif ()

if (BUILD_AVIREADER)
    add_subdirectory(AviReader)
    list(APPEND CKPLUGINS_COMPONENTS "AVIReader")
endif ()

if (BUILD_IMAGEREADER)
    add_subdirectory(ImageReader)
    list(APPEND CKPLUGINS_COMPONENTS "ImageReader")
endif ()

if (BUILD_WAVREADER)
    add_subdirectory(WavReader)
    list(APPEND CKPLUGINS_COMPONENTS "WavReader")
endif ()

if (BUILD_VIRTOOLSLOADER)
    add_subdirectory(VirtoolsLoader)
    list(APPEND CKPLUGINS_COMPONENTS "VirtoolsLoader")
endif ()

# =============================================================================
# Installation
# =============================================================================

if (CKPLUGINS_STANDALONE)
    include(CMakePackageConfigHelpers)
    include(GNUInstallDirs)

    # Install export targets
    install(EXPORT CKPluginsTargets
        FILE CKPluginsTargets.cmake
        NAMESPACE CKPlugins::
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CKPlugins
    )

    # Generate package version file
    write_basic_package_version_file(
        "${CMAKE_CURRENT_BINARY_DIR}/CKPluginsConfigVersion.cmake"
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMajorVersion
    )

    # Create package configuration file
    configure_package_config_file(
        "${CMAKE_CURRENT_SOURCE_DIR}/cmake/CKPluginsConfig.cmake.in"
        "${CMAKE_CURRENT_BINARY_DIR}/CKPluginsConfig.cmake"
        INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CKPlugins
    )

    # Install package configuration files
    install(FILES
        "${CMAKE_CURRENT_BINARY_DIR}/CKPluginsConfig.cmake"
        "${CMAKE_CURRENT_BINARY_DIR}/CKPluginsConfigVersion.cmake"
        DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/CKPlugins
    )

    # Install project files
    install(FILES
        "${CMAKE_CURRENT_SOURCE_DIR}/README.md"
        "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE"
        "${CMAKE_CURRENT_SOURCE_DIR}/INSTALL.md"
        DESTINATION ${CMAKE_INSTALL_DOCDIR}
    )

    # =============================================================================
    # Packaging with CPack
    # =============================================================================
    
    set(CPACK_PACKAGE_NAME "CKPlugins")
    set(CPACK_PACKAGE_VENDOR "Virtools Community")
    set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Virtools Plugin Collection - AVI, Image, WAV readers and Virtools loader")
    set(CPACK_PACKAGE_VERSION_MAJOR ${PROJECT_VERSION_MAJOR})
    set(CPACK_PACKAGE_VERSION_MINOR ${PROJECT_VERSION_MINOR})
    set(CPACK_PACKAGE_VERSION_PATCH ${PROJECT_VERSION_PATCH})
    set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
    set(CPACK_PACKAGE_INSTALL_DIRECTORY "CKPlugins")
    set(CPACK_RESOURCE_FILE_LICENSE "${CMAKE_CURRENT_SOURCE_DIR}/LICENSE")
    set(CPACK_RESOURCE_FILE_README "${CMAKE_CURRENT_SOURCE_DIR}/README.md")
    
    # Archive settings (ZIP for Windows)
    set(CPACK_GENERATOR "ZIP")
    set(CPACK_ARCHIVE_COMPONENT_INSTALL ON)
    
    # Set package file name
    set(CPACK_PACKAGE_FILE_NAME "${CPACK_PACKAGE_NAME}-${CPACK_PACKAGE_VERSION}-win64")
    
    include(CPack)
    
endif ()
