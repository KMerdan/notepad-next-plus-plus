# The application never resolves plugin Node through PATH or environment.
# Supply a binary from the verified archive in runtime-lock.json for packaging.
set(NOTEPADNEXT_PLUGIN_NODE "" CACHE FILEPATH "Verified pinned Node binary to copy into the application")
if(NOTEPADNEXT_PLUGINS)
    set(plugin_profile_node false)
    set(plugin_profile_python false)
    set(plugin_profile_webviews false)
    if(NOTEPADNEXT_PLUGIN_NODE)
        set(plugin_profile_node true)
    endif()
    if(NOTEPADNEXT_PLUGIN_PYTHON)
        set(plugin_profile_python true)
    endif()
    if(NOTEPADNEXT_PLUGIN_WEBVIEWS)
        set(plugin_profile_webviews true)
    endif()
    configure_file("${CMAKE_SOURCE_DIR}/cmake/PluginBuildProfile.json.in"
        "${CMAKE_BINARY_DIR}/generated/plugin-host/build-profile.json" @ONLY)
    set(plugin_python_files
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/python/host.py"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/python/nnp_rpc.py"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/python/nnp_wire.py"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/python/nnp_client.py")
    set(plugin_vscode_files
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/vscode/documents.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/vscode/types.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/vscode/adapter.cjs")
    list(APPEND plugin_vscode_files "${CMAKE_SOURCE_DIR}/packages/plugin-host/vscode/webviews.cjs" "${CMAKE_SOURCE_DIR}/packages/plugin-host/vscode/persistence.cjs")
    # Host-only edits must refresh the bundle even when no C++ file changed.
    set_property(TARGET NotepadNext APPEND PROPERTY LINK_DEPENDS
        "${CMAKE_BINARY_DIR}/generated/plugin-host/build-profile.json"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/host.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/python-host.cjs"
        ${plugin_python_files}
        ${plugin_vscode_files}
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/client.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/parent-watch.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/rpc.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/protocol-profile.json"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/wire.cjs"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/wire.schema.json"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/wire-contract.json"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/runtime-lock.json"
        "${CMAKE_SOURCE_DIR}/packages/plugin-host/LICENSE")
    if(APPLE)
        set(plugin_host_destination "$<TARGET_BUNDLE_DIR:NotepadNext>/Contents/Resources/plugin-host")
        set(plugin_runtime_destination "$<TARGET_BUNDLE_DIR:NotepadNext>/Contents/Helpers")
        set(plugin_node_name nnp-node)
    else()
        set(plugin_host_destination "$<TARGET_FILE_DIR:NotepadNext>/plugins/host")
        set(plugin_runtime_destination "$<TARGET_FILE_DIR:NotepadNext>/plugins/runtime")
        if(WIN32)
            set(plugin_node_name node.exe)
        else()
            set(plugin_node_name node)
        endif()
    endif()
    add_custom_command(TARGET NotepadNext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_host_destination}"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different
            "${CMAKE_BINARY_DIR}/generated/plugin-host/build-profile.json"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/host.cjs"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/python-host.cjs"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/client.cjs"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/parent-watch.cjs"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/rpc.cjs"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/protocol-profile.json"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/wire.cjs"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/wire.schema.json"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/wire-contract.json"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/runtime-lock.json"
            "${CMAKE_SOURCE_DIR}/packages/plugin-host/LICENSE"
            "${plugin_host_destination}"
        VERBATIM)
    add_custom_command(TARGET NotepadNext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_host_destination}/python"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${plugin_python_files} "${plugin_host_destination}/python"
        VERBATIM)
    add_custom_command(TARGET NotepadNext POST_BUILD
        COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_host_destination}/vscode"
        COMMAND ${CMAKE_COMMAND} -E copy_if_different ${plugin_vscode_files} "${plugin_host_destination}/vscode"
        VERBATIM)
    get_property(plugin_uses_bundled_zlib GLOBAL PROPERTY NNP_PLUGIN_BUNDLED_ZLIB)
    if(plugin_uses_bundled_zlib)
        add_custom_command(TARGET NotepadNext POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E copy_if_different
                "${CMAKE_SOURCE_DIR}/doc/plugin-system/licenses/zlib-1.3.2-LICENSE"
                "${plugin_host_destination}/ZLIB-LICENSE"
            VERBATIM)
    endif()
    if(NOTEPADNEXT_PLUGIN_NODE)
        if(NOT IS_ABSOLUTE "${NOTEPADNEXT_PLUGIN_NODE}" OR NOT EXISTS "${NOTEPADNEXT_PLUGIN_NODE}")
            message(FATAL_ERROR "NOTEPADNEXT_PLUGIN_NODE must be an existing absolute file")
        endif()
        file(READ "${CMAKE_SOURCE_DIR}/packages/plugin-host/runtime-lock.json" node_lock)
        string(JSON pinned_node_version GET "${node_lock}" version)
        execute_process(COMMAND "${NOTEPADNEXT_PLUGIN_NODE}" --version
            RESULT_VARIABLE node_status OUTPUT_VARIABLE node_version OUTPUT_STRIP_TRAILING_WHITESPACE TIMEOUT 5)
        if(NOT node_status EQUAL 0 OR NOT node_version STREQUAL pinned_node_version)
            message(FATAL_ERROR "Plugin Node must be ${pinned_node_version}; got ${node_version}")
        endif()
        get_filename_component(node_binary_dir "${NOTEPADNEXT_PLUGIN_NODE}" DIRECTORY)
        if(WIN32)
            set(node_license "${node_binary_dir}/LICENSE")
        else()
            set(node_license "${node_binary_dir}/../LICENSE")
        endif()
        if(NOT EXISTS "${node_license}")
            message(FATAL_ERROR "The Node archive LICENSE is missing beside the supplied runtime")
        endif()
        add_custom_command(TARGET NotepadNext POST_BUILD
            COMMAND ${CMAKE_COMMAND} -E make_directory "${plugin_runtime_destination}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${NOTEPADNEXT_PLUGIN_NODE}" "${plugin_runtime_destination}/${plugin_node_name}"
            COMMAND ${CMAKE_COMMAND} -E copy_if_different "${node_license}" "${plugin_host_destination}/NODE-LICENSE"
            VERBATIM)
    else()
        message(STATUS "No bundled plugin Node configured; manager remains available, activation reports missing runtime")
    endif()
endif()
