message(STATUS "Configuring macOS packaging")

set(INSTALL_DIR ${CMAKE_BINARY_DIR}/install)

# Custom Info.plist
set_target_properties(NotepadNext PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/deploy/macos/info.plist
    MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0"
    MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}.0"
)

# Application icon
set(APP_ICON_MACOS ${CMAKE_SOURCE_DIR}/icon/NotepadNext.icns)

set_source_files_properties(${APP_ICON_MACOS}
    PROPERTIES MACOSX_PACKAGE_LOCATION "Resources"
)

target_sources(NotepadNext PRIVATE ${APP_ICON_MACOS})

set_target_properties(NotepadNext PROPERTIES
    MACOSX_BUNDLE_ICON_FILE NotepadNext.icns
)

install(TARGETS NotepadNext
    BUNDLE DESTINATION .
)

install(FILES ${APP_ICON_MACOS}
    DESTINATION NotepadNextPlusPlus.app/Contents/Resources
)

add_custom_target(install_local
    COMMAND ${CMAKE_COMMAND}
        --install ${CMAKE_BINARY_DIR}
        --prefix ${INSTALL_DIR}
    DEPENDS NotepadNext
)

find_program(MACDEPLOYQT_EXECUTABLE macdeployqt REQUIRED)

if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_PLUGIN_NODE)
    set(NOTEPADNEXT_MACOS_NODE_ARCHIVE "" CACHE FILEPATH "Official pinned Node tar.xz; verified before macOS plugin packaging")
    set(NOTEPADNEXT_MACOS_SIGN_IDENTITY "-" CACHE STRING "macOS signing identity; '-' produces a development package")
    set(NOTEPADNEXT_MACOS_NOTARY_PROFILE "" CACHE STRING "Optional preconfigured notarytool keychain profile; setting it submits artifacts to Apple")
    find_package(Python3 3.9 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
        get_filename_component(plugin_qt_prefix "${Qt6_DIR}/../../.." ABSOLUTE)
        set(plugin_package_arch "${CMAKE_OSX_ARCHITECTURES}")
        if(NOT plugin_package_arch)
            set(plugin_package_arch "${CMAKE_SYSTEM_PROCESSOR}")
        endif()
        add_custom_target(plugin_dmg
            COMMAND "${Python3_EXECUTABLE}" "${CMAKE_SOURCE_DIR}/scripts/package-macos.py"
                --app "$<TARGET_BUNDLE_DIR:NotepadNext>"
                --qt "${plugin_qt_prefix}"
                --node-archive "${NOTEPADNEXT_MACOS_NODE_ARCHIVE}"
                --arch "${plugin_package_arch}"
                --output-dir "${CMAKE_BINARY_DIR}/plugin-packages"
                --identity "${NOTEPADNEXT_MACOS_SIGN_IDENTITY}"
                --notary-profile "${NOTEPADNEXT_MACOS_NOTARY_PROFILE}"
                --dmg
            DEPENDS NotepadNext
            VERBATIM)
        add_custom_target(dmg DEPENDS plugin_dmg)
    else()
        add_custom_target(dmg
            COMMAND ${CMAKE_COMMAND} -E echo "macOS plugin packaging requires Python >=3.9 and the verified Node archive"
            COMMAND ${CMAKE_COMMAND} -E false)
    endif()
else()
    add_custom_target(dmg
        COMMAND ${MACDEPLOYQT_EXECUTABLE} ${INSTALL_DIR}/NotepadNextPlusPlus.app -dmg
        COMMAND ${CMAKE_COMMAND} -E rename ${INSTALL_DIR}/NotepadNextPlusPlus.dmg ${CMAKE_BINARY_DIR}/NotepadNextPlusPlus-v${PROJECT_VERSION}.dmg
        DEPENDS install_local)
endif()
