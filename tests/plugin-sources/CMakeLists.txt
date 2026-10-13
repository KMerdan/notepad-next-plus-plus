cmake_minimum_required(VERSION 3.21)

# The source subsystem can be exercised without building Scintilla or fetching
# the editor's third-party dependencies.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    project(NotepadNextPluginSourceTests LANGUAGES CXX)
    set(CMAKE_CXX_STANDARD 20)
    set(CMAKE_CXX_STANDARD_REQUIRED ON)
    find_package(Qt6 REQUIRED COMPONENTS Core Network Widgets Test)
    qt_standard_project_setup()
    enable_testing()
    add_subdirectory(../../src/plugins/sources plugin-sources)
    add_subdirectory(../../src/plugins/packages plugin-packages)
endif()

if(NOT TARGET NotepadNextAppearance)
    add_subdirectory(../../src/appearance appearance)
endif()
add_executable(PluginSourceTests
    PluginSourceTests.cpp
    ../../src/dialogs/PluginManagerDialog.cpp
    ../../src/dialogs/PluginManagerDialog.h
    ../../src/resources.qrc
)
target_include_directories(PluginSourceTests PRIVATE ../../src/dialogs)
target_link_libraries(PluginSourceTests PRIVATE NotepadNextPluginPackages NotepadNextAppearance Qt6::Widgets Qt6::Test)
target_compile_definitions(PluginSourceTests PRIVATE PLUGIN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../doc/plugin-system/examples")
set_target_properties(PluginSourceTests PROPERTIES AUTOMOC ON AUTORCC ON)
add_test(NAME PluginSourceTests COMMAND PluginSourceTests -o -,txt -o "${CMAKE_BINARY_DIR}/PluginSourceTests.qt.txt,txt")
set_tests_properties(PluginSourceTests PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

add_executable(PluginManagerPreview
    PluginManagerPreview.cpp
    ../../src/dialogs/PluginManagerDialog.cpp
    ../../src/dialogs/PluginManagerDialog.h
    ../../src/resources.qrc
)
target_include_directories(PluginManagerPreview PRIVATE ../../src/dialogs)
target_link_libraries(PluginManagerPreview PRIVATE NotepadNextPluginPackages NotepadNextAppearance Qt6::Widgets)
target_compile_definitions(PluginManagerPreview PRIVATE PLUGIN_FIXTURES_DIR="${CMAKE_CURRENT_SOURCE_DIR}/../../doc/plugin-system/examples")
set_target_properties(PluginManagerPreview PROPERTIES AUTOMOC ON AUTORCC ON)
