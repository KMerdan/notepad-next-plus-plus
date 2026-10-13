# Exercise the production MainWindow/Application source without introducing
# test flags, CLI options or storage overrides in the shipped executable.
get_target_property(app_sources NotepadNext SOURCES)
set(test_sources)
foreach(source IN LISTS app_sources)
    if(source MATCHES "(^|/)main\\.cpp$")
        continue()
    endif()
    if(NOT IS_ABSOLUTE "${source}" AND NOT source MATCHES "^\\$<")
        get_filename_component(source "${source}" ABSOLUTE BASE_DIR "${CMAKE_SOURCE_DIR}/src")
    endif()
    list(APPEND test_sources "${source}")
endforeach()
qt_add_executable(PluginApplicationTests MACOSX_BUNDLE PluginApplicationTests.cpp ${test_sources})
if(APPLE)
    set_target_properties(PluginApplicationTests PROPERTIES
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0")
endif()
get_target_property(app_includes NotepadNext INCLUDE_DIRECTORIES)
get_target_property(app_definitions NotepadNext COMPILE_DEFINITIONS)
get_target_property(app_libraries NotepadNext LINK_LIBRARIES)
target_include_directories(PluginApplicationTests PRIVATE ${app_includes})
target_compile_definitions(PluginApplicationTests PRIVATE ${app_definitions}
    PLUGIN_TEST_NODE="${NOTEPADNEXT_TEST_NODE}"
    PLUGIN_TEST_PREVIEW="${CMAKE_SOURCE_DIR}/tests/fixtures/contract-preview"
    PLUGIN_TEST_PYTHON="${NOTEPADNEXT_TEST_PYTHON}"
    PLUGIN_TEST_PYTHON_EXAMPLE="${CMAKE_SOURCE_DIR}/examples/python-inspector"
    PLUGIN_TEST_STAGER="${CMAKE_SOURCE_DIR}/tests/plugin-ci/stage-fixture.cjs"
    PLUGIN_TEST_VSCODE_SOURCE="${CMAKE_SOURCE_DIR}/tests/fixtures/legacy-hello-world"
    PLUGIN_TEST_VSCODE_WEBVIEW_SOURCE="${CMAKE_SOURCE_DIR}/tests/fixtures/legacy-webview"
    PLUGIN_TEST_LEGACY_STAGER="${CMAKE_SOURCE_DIR}/tests/plugin-ci/stage-fixture.cjs"
    PLUGIN_TEST_SCREENSHOT="${CMAKE_BINARY_DIR}/plugin-manager-installed.png")
target_link_libraries(PluginApplicationTests PRIVATE ${app_libraries} Qt6::Test)
get_target_property(app_link_dependencies NotepadNext LINK_DEPENDS)
if(app_link_dependencies)
    # A host-only change must relink this bundle too so its POST_BUILD copy
    # cannot silently leave the production integration test on an old adapter.
    set_property(TARGET PluginApplicationTests APPEND PROPERTY LINK_DEPENDS ${app_link_dependencies})
endif()
set_target_properties(PluginApplicationTests PROPERTIES AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/src/dialogs;${CMAKE_SOURCE_DIR}/src/widgets;${CMAKE_SOURCE_DIR}/src/docks")
add_dependencies(PluginApplicationTests NotepadNext)
if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_TEST_NODE)
    if(APPLE)
        set(test_host_source "$<TARGET_BUNDLE_DIR:NotepadNext>/Contents/Resources/plugin-host")
        set(test_host_destination "$<TARGET_BUNDLE_DIR:PluginApplicationTests>/Contents/Resources/plugin-host")
        set(test_node_destination "$<TARGET_BUNDLE_DIR:PluginApplicationTests>/Contents/Helpers/nnp-node")
    else()
        set(test_host_source "$<TARGET_FILE_DIR:NotepadNext>/plugins/host")
        set(test_host_destination "$<TARGET_FILE_DIR:PluginApplicationTests>/plugins/host")
        if(WIN32)
            set(test_node_destination "$<TARGET_FILE_DIR:PluginApplicationTests>/plugins/runtime/node.exe")
        else()
            set(test_node_destination "$<TARGET_FILE_DIR:PluginApplicationTests>/plugins/runtime/node")
        endif()
    endif()
    get_filename_component(test_runtime_destination "${test_node_destination}" DIRECTORY)
    add_custom_command(TARGET PluginApplicationTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${test_runtime_destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NOTEPADNEXT_TEST_NODE}" "${test_node_destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${test_host_source}" "${test_host_destination}"
        VERBATIM)
endif()
add_test(NAME PluginApplicationTests COMMAND PluginApplicationTests)
set_tests_properties(PluginApplicationTests PROPERTIES ENVIRONMENT "NNP_TEST_QT_LOG_FILE=${CMAKE_BINARY_DIR}/PluginApplicationTests.qt.txt")
if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_PLUGIN_WEBVIEWS AND NOTEPADNEXT_TEST_NODE)
    target_sources(PluginApplicationTests PRIVATE ApplicationExitProbe.cpp)
    target_compile_definitions(PluginApplicationTests PRIVATE PLUGIN_APPLICATION_EXIT_PROBE)
    add_executable(PluginApplicationExitTests ApplicationExitTests.cpp)
    # This process-only driver depends on the UI test bundle but owns no forms.
    # CMake 3.31 otherwise declares the dependency's UI outputs twice in Ninja.
    set_target_properties(PluginApplicationExitTests PROPERTIES AUTOUIC OFF)
    target_link_libraries(PluginApplicationExitTests PRIVATE NotepadNextPluginPackages NotepadNextPluginData Qt6::Test)
    target_compile_definitions(PluginApplicationExitTests PRIVATE EXIT_APPLICATION="$<TARGET_FILE:PluginApplicationTests>")
    add_dependencies(PluginApplicationExitTests PluginApplicationTests)
    add_test(NAME PluginApplicationExitTests COMMAND PluginApplicationExitTests -o -,txt -o "${CMAKE_BINARY_DIR}/PluginApplicationExitTests.qt.txt,txt")
    set_tests_properties(PluginApplicationExitTests PROPERTIES RUN_SERIAL TRUE TIMEOUT 90)
endif()
if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_TEST_NODE)
    set_tests_properties(PluginApplicationTests PROPERTIES DEPENDS "PluginPythonSampleBuild")
endif()

set(NOTEPADNEXT_PERFORMANCE_PLUGIN_PACKAGE "" CACHE PATH "Prebuilt Markdown package for opt-in consumer performance measurements")
option(NOTEPADNEXT_BUILD_PLUGIN_PERFORMANCE "Build opt-in production-application plugin performance measurements" OFF)
if(NOTEPADNEXT_BUILD_PLUGIN_PERFORMANCE AND APPLE AND NOTEPADNEXT_PLUGIN_WEBVIEWS AND NOTEPADNEXT_TEST_NODE)
    if(NOT EXISTS "${NOTEPADNEXT_PERFORMANCE_PLUGIN_PACKAGE}/notepadnext-plugin.json")
        message(FATAL_ERROR "Performance acceptance requires NOTEPADNEXT_PERFORMANCE_PLUGIN_PACKAGE pointing to a prebuilt Marketplace package.")
    endif()
    qt_add_executable(PluginPerformanceTests MACOSX_BUNDLE PluginPerformanceTests.cpp ${test_sources})
    target_include_directories(PluginPerformanceTests PRIVATE ${app_includes})
    target_compile_definitions(PluginPerformanceTests PRIVATE ${app_definitions}
        PERF_NODE="${NOTEPADNEXT_TEST_NODE}"
        PERF_MARKDOWN="${NOTEPADNEXT_PERFORMANCE_PLUGIN_PACKAGE}"
        PERF_STAGER="${CMAKE_SOURCE_DIR}/tests/plugin-ci/stage-fixture.cjs")
    target_link_libraries(PluginPerformanceTests PRIVATE ${app_libraries} Qt6::Test)
    set_target_properties(PluginPerformanceTests PROPERTIES
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0"
        AUTOUIC_SEARCH_PATHS "${CMAKE_SOURCE_DIR}/src/dialogs;${CMAKE_SOURCE_DIR}/src/widgets;${CMAKE_SOURCE_DIR}/src/docks")
    if(app_link_dependencies)
        set_property(TARGET PluginPerformanceTests APPEND PROPERTY LINK_DEPENDS ${app_link_dependencies})
    endif()
    add_dependencies(PluginPerformanceTests NotepadNext)
    add_custom_command(TARGET PluginPerformanceTests POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:PluginPerformanceTests>/Contents/Helpers"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NOTEPADNEXT_TEST_NODE}" "$<TARGET_BUNDLE_DIR:PluginPerformanceTests>/Contents/Helpers/nnp-node"
        COMMAND ${CMAKE_COMMAND} -E copy_directory "${test_host_source}" "$<TARGET_BUNDLE_DIR:PluginPerformanceTests>/Contents/Resources/plugin-host"
        VERBATIM)
    add_test(NAME PluginPerformanceTests COMMAND PluginPerformanceTests -o -,txt -o "${CMAKE_BINARY_DIR}/PluginPerformanceTests.qt.txt,txt")
    set_tests_properties(PluginPerformanceTests PROPERTIES RUN_SERIAL TRUE TIMEOUT 600 LABELS "performance")
endif()
