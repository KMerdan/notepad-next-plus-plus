// SPDX-License-Identifier: GPL-3.0-or-later
#ifndef PLUGINPROTOCOLPROFILE_H
#define PLUGINPROTOCOLPROFILE_H
#include <QJsonObject>
namespace PluginRpc {
inline QJsonObject profileLimits() {
    return {{"maxFrameBytes",16777216},{"maxHeaderBytes",8192},{"maxBatchEntries",64},
        {"maxPendingRequests",64},{"maxJsonDepth",128},{"maxIdTokenBytes",1024},
        {"maxSnapshotUtf8Bytes",2097152},{"maxHtmlUtf8Bytes",6291456},{"maxWebviewMessageBytes",65536},
        {"maxPluginDataBytes",1048576},{"maxPluginDataValueBytes",65536},{"maxPluginDataKeys",256},{"maxPluginDataDepth",32}};
}
}
#endif
