add_library(NotepadNextPluginSources STATIC
    PluginMetadata.cpp PluginMetadata.h
    RegisteredResourceRegistry.cpp RegisteredResourceRegistry.h
    SourceEntryResolver.cpp SourceEntryResolver.h
    plugin_sources.qrc
)
target_link_libraries(NotepadNextPluginSources PUBLIC Qt6::Core Qt6::Network)
target_include_directories(NotepadNextPluginSources PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(NotepadNextPluginSources PROPERTIES AUTOMOC ON AUTORCC ON)
