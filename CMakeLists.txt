cmake_minimum_required(VERSION 3.21)

# CMake must establish the Apple deployment target before enabling compilers.
# The controlled Node 24 runtime requires macOS >=13.5; editor-only builds
# keep their existing explicit platform/Qt policy.
if(CMAKE_HOST_APPLE AND NOTEPADNEXT_PLUGIN_NODE AND (NOT DEFINED NOTEPADNEXT_PLUGINS OR NOTEPADNEXT_PLUGINS))
    if(NOT CMAKE_OSX_DEPLOYMENT_TARGET)
        set(CMAKE_OSX_DEPLOYMENT_TARGET "13.5" CACHE STRING "Minimum macOS version" FORCE)
    elseif(CMAKE_OSX_DEPLOYMENT_TARGET VERSION_LESS "13.5")
        message(FATAL_ERROR "Bundled Node 24 requires CMAKE_OSX_DEPLOYMENT_TARGET >=13.5")
    endif()
endif()

project(NotepadNext
    VERSION 0.15
    DESCRIPTION "Cross-platform text editor"
    LANGUAGES CXX
)

set(APP_DISPLAY_NAME "Notepad Next ++")
set(APP_REPOSITORY_URL "https://github.com/KMerdan/notepad-next-plus-plus")

set(APP_DISTRIBUTION "" CACHE STRING "Distribution type")
string(TIMESTAMP CURRENT_YEAR "%Y")
set(APP_COPYRIGHT "Copyright 2019-${CURRENT_YEAR} Justin Dailey and contributors | fork modifications Copyright ${CURRENT_YEAR} KMerdan")

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
if(APPLE)
    enable_language(OBJCXX)
    set(CMAKE_OBJCXX_STANDARD 20)
    set(CMAKE_OBJCXX_STANDARD_REQUIRED ON)
endif()

add_compile_definitions(SCI_OWNREGEX)

if(MSVC)
    # https://stackoverflow.com/questions/78598141/first-stdmutexlock-crashes-in-application-built-with-latest-visual-studio
    add_compile_definitions(_DISABLE_CONSTEXPR_MUTEX_CONSTRUCTOR)
    add_compile_options(/guard:cf)
    add_link_options(/guard:cf)
endif()

if(UNIX AND NOT APPLE)
    add_compile_options(-fstack-protector-strong -D_FORTIFY_SOURCE=2)
    add_link_options(-Wl,-z,relro -Wl,-z,now)
endif()

find_package(Qt6
    REQUIRED
        COMPONENTS
            Core
            Gui
            Widgets
            Network
            PrintSupport
            Core5Compat
)

set(CMAKE_AUTORCC ON)
qt_standard_project_setup()

set(CPM_SOURCE_CACHE "${CMAKE_SOURCE_DIR}/.cpm-cache" CACHE PATH "CPM source cache directory")

option(NOTEPADNEXT_PLUGINS "Build the native plugin manager and process services" ON)
option(NOTEPADNEXT_PLUGIN_WEBVIEWS "Build Qt WebEngine plugin panels" OFF)
option(NOTEPADNEXT_BUILD_PLUGIN_TESTS "Build plugin contract and integration tests" OFF)
if((NOTEPADNEXT_PLUGINS OR NOTEPADNEXT_BUILD_PLUGIN_TESTS) AND NOT EXISTS "${CMAKE_SOURCE_DIR}/packages/plugin-sdk/schemas/manifest.schema.json")
    message(FATAL_ERROR "The pinned plugin SDK is missing. Run: git submodule update --init --recursive")
endif()

if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_PLUGIN_WEBVIEWS)
    find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets WebChannel)
endif()

add_subdirectory(thirdparty)
add_subdirectory(src)

if(NOTEPADNEXT_BUILD_PLUGIN_TESTS)
    enable_testing()
    find_package(Qt6 REQUIRED COMPONENTS Test)
    add_subdirectory(tests/plugin-sources)
    add_subdirectory(tests/plugin-packages)
    add_subdirectory(tests/plugin-rpc)
    add_subdirectory(tests/plugin-documents)
    add_subdirectory(tests/plugin-data)
    add_subdirectory(tests/plugin-diagnostics)
    if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_PLUGIN_WEBVIEWS)
        add_subdirectory(tests/plugin-webviews)
    endif()
    add_subdirectory(tests/plugin-application)
    if(APPLE)
        add_subdirectory(tests/macos-packaging)
    endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/Packaging.cmake)
