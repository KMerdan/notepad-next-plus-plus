add_subdirectory(appearance)
if(NOTEPADNEXT_PLUGINS OR NOTEPADNEXT_BUILD_PLUGIN_TESTS)
    add_subdirectory(plugins/sources)
    add_subdirectory(plugins/packages)
    add_subdirectory(plugins/rpc)
    add_subdirectory(plugins/documents)
endif()
if(NOTEPADNEXT_PLUGINS AND NOTEPADNEXT_PLUGIN_WEBVIEWS)
    find_package(Qt6 REQUIRED COMPONENTS WebEngineWidgets WebChannel)
    add_subdirectory(plugins/webviews)
endif()
if(NOTEPADNEXT_PLUGINS OR NOTEPADNEXT_BUILD_PLUGIN_TESTS)
    add_subdirectory(plugins/runtime)
endif()


qt_add_executable(NotepadNext
	WIN32
	MACOSX_BUNDLE
	ActionUtils.h
	NativeAppearance.h
	NativeAppearance.cpp
	ApplicationSettings.h
	ByteArrayUtils.h
	ColorPickerDelegate.h
	ComboBoxDelegate.h
	Converter.h
	DebugManager.h
	DefaultDirectoryManager.h
	DockedEditor.h
	DockedEditorTitleBar.h

	EditorManager.h
	EditorOperations.h
	EditorOperations.cpp
	FileOperations.h
	FileOperations.cpp
	SearchService.h
	EditorPrintPreviewRenderer.h
	FileDialogHelpers.h
	Finder.h
	FocusWatcher.h
	HtmlConverter.h
	IFaceTable.h
	IFaceTableMixer.h
	ISearchResultsHandler.h
	LanguageStylesModel.h
	LuaExtension.h
	LuaState.h
	Macro.h
	MacroListModel.h
	MacroManager.h
	MacroRecorder.h
	MacroStep.h
	MacroStepTableModel.h
	NotepadNextApplication.h
	NppImporter.h
	QRegexSearch.h
	RangeAllocator.h
	RecentFilesListManager.h
	RecentFilesListMenuBuilder.h
	RtfConverter.h
	SciIFaceTable.h
	ScintillaCommenter.h
	ScintillaEnums.h
	ScintillaNext.h
	SearchResultData.h
	SearchResultHighlighterDelegate.h
	SearchResultsCollector.h
	SelectionTracker.h
	SessionManager.h
	SpinBoxDelegate.h
	TranslationManager.h
	UndoAction.h
	ZoomEventWatcher.h
	resources.qrc
	scripts.qrc
	ApplicationSettings.cpp
	ColorPickerDelegate.cpp
	ComboBoxDelegate.cpp
	Converter.cpp
	DebugManager.cpp
	DefaultDirectoryManager.cpp
	DockedEditor.cpp

	EditorManager.cpp
	EditorPrintPreviewRenderer.cpp
	FileDialogHelpers.cpp
	Finder.cpp
	HtmlConverter.cpp
	IFaceTable.cpp
	IFaceTableMixer.cpp
	LanguageStylesModel.cpp
	LuaExtension.cpp
	LuaState.cpp
	Macro.cpp
	MacroListModel.cpp
	MacroManager.cpp
	MacroRecorder.cpp
	MacroStep.cpp
	MacroStepTableModel.cpp
	main.cpp
	NotepadNextApplication.cpp
	NppImporter.cpp
	QRegexSearch.cpp
	RangeAllocator.cpp
	RecentFilesListManager.cpp
	RecentFilesListMenuBuilder.cpp
	RtfConverter.cpp
	SciIFaceTable.cpp
	ScintillaCommenter.cpp
	TextEncoding.cpp
	TextEncoding.h
	ScintillaNext.cpp
	SearchResultHighlighterDelegate.cpp
	SearchResultsCollector.cpp
	SelectionTracker.cpp
	SessionManager.cpp
	SpinBoxDelegate.cpp
	TranslationManager.cpp
	UndoAction.cpp
	ZoomEventWatcher.cpp
	decorators/ApplicationDecorator.cpp
	decorators/ApplicationDecorator.h
	decorators/AutoCompletion.cpp
	decorators/AutoCompletion.h
	decorators/AutoIndentation.cpp
	decorators/AutoIndentation.h
	decorators/BetterMultiSelection.cpp
	decorators/BetterMultiSelection.h
	decorators/BookMarkDecorator.cpp
	decorators/BookMarkDecorator.h
	decorators/BraceMatch.cpp
	decorators/BraceMatch.h
	decorators/EditorConfigAppDecorator.cpp
	decorators/EditorConfigAppDecorator.h
	decorators/EditorDecorator.cpp
	decorators/EditorDecorator.h
	decorators/HighlightedScrollBar.cpp
	decorators/HighlightedScrollBar.h
	decorators/HTMLAutoCompleteDecorator.cpp
	decorators/HTMLAutoCompleteDecorator.h
	decorators/LineNumbers.cpp
	decorators/LineNumbers.h
	decorators/MarkerAppDecorator.cpp
	decorators/MarkerAppDecorator.h
	decorators/SmartHighlighter.cpp
	decorators/SmartHighlighter.h
	decorators/SurroundSelection.cpp
	decorators/SurroundSelection.h
	decorators/URLFinder.cpp
	decorators/URLFinder.h
	dialogs/ColumnEditorDialog.cpp
	dialogs/ColumnEditorDialog.h
	dialogs/ColumnEditorDialog.ui
	dialogs/FindReplaceDialog.cpp
	dialogs/FindReplaceDialog.h
	dialogs/FindReplaceDialog.ui
	dialogs/MacroEditorDialog.cpp
	dialogs/MacroEditorDialog.h
	dialogs/MacroEditorDialog.ui
	dialogs/MacroRunDialog.cpp
	dialogs/MacroRunDialog.h
	dialogs/MacroRunDialog.ui
	dialogs/MacroSaveDialog.cpp
	dialogs/MacroSaveDialog.h
	dialogs/MacroSaveDialog.ui
	dialogs/MainWindow.cpp
	dialogs/MainWindow.h
	dialogs/MainWindow.ui
	dialogs/PreferencesDialog.cpp
	dialogs/PreferencesDialog.h
	dialogs/PreferencesDialog.ui
	docks/DebugLogDock.cpp
	docks/DebugLogDock.h
	docks/DebugLogDock.ui
	docks/EditorInspectorDock.cpp
	docks/EditorInspectorDock.h
	docks/EditorInspectorDock.ui
	docks/FileListDock.cpp
	docks/FileListDock.h
	docks/FileListDock.ui
	docks/FolderAsWorkspaceDock.cpp
	docks/FolderAsWorkspaceDock.h
	docks/FolderAsWorkspaceDock.ui



	docks/LanguageInspectorDock.cpp
	docks/LanguageInspectorDock.h
	docks/LanguageInspectorDock.ui
	docks/LuaConsoleDock.cpp
	docks/LuaConsoleDock.h
	docks/LuaConsoleDock.ui
	docks/SearchResultsDock.cpp
	docks/SearchResultsDock.h
	docks/SearchResultsDock.ui
    widgets/EditorInfoStatusBar.cpp
	widgets/EditorInfoStatusBar.h
	widgets/FadingIndicator.cpp
	widgets/FadingIndicator.h
	widgets/QuickFindWidget.cpp
	widgets/QuickFindWidget.h
	widgets/QuickFindWidget.ui
	widgets/StatusLabel.cpp
	widgets/StatusLabel.h
	Sorter.h Sorter.cpp
	ScintillaSorter.h ScintillaSorter.cpp
	widgets/TabsQuickActionsBar.cpp
	widgets/TabsQuickActionsBar.h
	ShiftMiddleClickBlocker.h ShiftMiddleClickBlocker.cpp
	ShiftWheelToHorizontalScrollFilter.h ShiftWheelToHorizontalScrollFilter.cpp

)

set_target_properties(NotepadNext PROPERTIES
    OUTPUT_NAME "NotepadNextPlusPlus"
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/deploy/macos/info.plist
)

file(GLOB TS_FILES
	${CMAKE_SOURCE_DIR}/i18n/*.ts
)

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(NotepadNext
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/i18n"
)


target_include_directories(NotepadNext
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/decorators
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
        ${CMAKE_CURRENT_SOURCE_DIR}/docks
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
	configure_file(
		"${CMAKE_SOURCE_DIR}/deploy/windows/version.rc.in"
		"${CMAKE_BINARY_DIR}/version.rc"
		@ONLY
	)
	target_sources(NotepadNext PRIVATE "${CMAKE_BINARY_DIR}/version.rc")
	configure_file(
		"${CMAKE_SOURCE_DIR}/icon/NotepadNext.ico"
		"${CMAKE_BINARY_DIR}/NotepadNext.ico"
		COPYONLY
	)
endif()

target_link_libraries(NotepadNext
    PRIVATE NotepadNextAppearance
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::PrintSupport
        qtadvanceddocking-qt6
        QSimpleUpdater
        SingleApplication::SingleApplication
        editorconfig-core-qt
        lua
        lexilla
        scintilla
        uchardet
)

if(NOTEPADNEXT_PLUGINS)
    target_sources(NotepadNext PRIVATE NativeEditorServices.h NativeEditorServices.cpp)
    target_sources(NotepadNext PRIVATE dialogs/PluginManagerDialog.cpp dialogs/PluginManagerDialog.h)
    target_link_libraries(NotepadNext PRIVATE NotepadNextPluginSources NotepadNextPluginRuntime)
    target_compile_definitions(NotepadNext PRIVATE NOTEPADNEXT_PLUGINS)
    if(NOTEPADNEXT_PLUGIN_WEBVIEWS)
        target_link_libraries(NotepadNext PRIVATE NotepadNextPluginWebviews)
        target_compile_definitions(NotepadNext PRIVATE NOTEPADNEXT_PLUGIN_WEBVIEWS)
    endif()
endif()

include(${CMAKE_SOURCE_DIR}/cmake/PluginRuntime.cmake)

if(APPLE)
    target_link_libraries(NotepadNext PRIVATE "-framework AppKit" "-framework CoreGraphics")
    # Workaround for Qt 6.5/6.8 on macOS with newer Xcode that removed deprecated AGL framework
    # These linker flags prevent the linker from failing when Qt tries to reference AGL
    target_link_options(NotepadNext PRIVATE "LINKER:-flat_namespace" "LINKER:-undefined,suppress")
endif()

target_compile_definitions(NotepadNext
    PRIVATE
        ADS_STATIC
        QSIMPLEUPDATER_STATIC
        QAPPLICATION_CLASS=QApplication
        APP_VERSION="${PROJECT_VERSION}"
        APP_DISPLAY_NAME="${APP_DISPLAY_NAME}"
        APP_REPOSITORY_URL="${APP_REPOSITORY_URL}"
        APP_COPYRIGHT="${APP_COPYRIGHT}"
        APP_DISTRIBUTION="${APP_DISTRIBUTION}"
)
