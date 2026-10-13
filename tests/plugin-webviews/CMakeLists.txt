cmake_minimum_required(VERSION 3.21)
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NotepadNextPluginWebviewTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    find_package(Qt6 REQUIRED COMPONENTS Core Widgets Core5Compat Test WebEngineWidgets WebChannel)
    qt_standard_project_setup()
    enable_testing()
    add_compile_definitions(SCI_OWNREGEX)
    add_subdirectory(../../thirdparty/scintilla scintilla)
    add_subdirectory(../../src/plugins/rpc plugin-rpc)
    add_subdirectory(../../src/plugins/documents plugin-documents)
    add_subdirectory(../../src/plugins/webviews plugin-webviews)
    add_subdirectory(../../src/plugins/runtime plugin-runtime)
endif()
if(NOT TARGET NotepadNextAppearance)
    add_subdirectory(../../src/appearance appearance)
endif()
add_executable(PluginWebviewTests PluginWebviewTests.cpp ../../src/QRegexSearch.cpp ../../src/QRegexSearch.h
    ../../src/dialogs/PluginManagerDialog.cpp ../../src/dialogs/PluginManagerDialog.h ../../src/resources.qrc)
target_include_directories(PluginWebviewTests PRIVATE ../../src/dialogs)
set(NOTEPADNEXT_TEST_NODE "" CACHE FILEPATH "Absolute Node 24 executable for integration tests")
target_link_libraries(PluginWebviewTests PRIVATE NotepadNextPluginRuntime NotepadNextAppearance Qt6::Test)
target_compile_definitions(PluginWebviewTests PRIVATE
    PLUGIN_TEST_NODE="${NOTEPADNEXT_TEST_NODE}"
    PLUGIN_TEST_HOST="${CMAKE_CURRENT_SOURCE_DIR}/../../packages/plugin-host/host.cjs"
    PLUGIN_TEST_PREVIEW="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/fixtures/contract-preview"
    PLUGIN_TEST_STAGER="${CMAKE_CURRENT_SOURCE_DIR}/../../tests/plugin-ci/stage-fixture.cjs"
)
add_test(NAME PluginWebviewTests COMMAND PluginWebviewTests -o -,txt -o "${CMAKE_BINARY_DIR}/PluginWebviewTests.qt.txt,txt")
