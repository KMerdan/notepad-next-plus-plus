add_library(NotepadNextPluginRpc STATIC
    RpcFramer.cpp RpcFramer.h RpcEndpoint.cpp RpcEndpoint.h RpcProcess.cpp RpcProcess.h JsonIdMask.cpp JsonIdMask.h
    WireContract.cpp WireContract.h wire_contract.qrc
)
target_link_libraries(NotepadNextPluginRpc PUBLIC Qt6::Core)
target_include_directories(NotepadNextPluginRpc PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
set_target_properties(NotepadNextPluginRpc PROPERTIES AUTOMOC ON AUTORCC ON)
