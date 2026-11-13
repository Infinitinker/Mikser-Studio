// Where the running Mikser Studio publishes its local API: the discovery file api.json (docs/api.md).
#pragma once

#include <QByteArray>
#include <QString>
#include <QStringList>
#include <QUrl>

#include <optional>

namespace mikserstudio::mcp {

// What the discovery file says: where the API listens and the session token it needs.
struct Discovery {
    QUrl baseUrl;
    QUrl eventsUrl;
    QByteArray token;
};

// The instances the app knows (MIKSERSTUDIO_INSTANCE), as --instance names them: installed, dev and e2e.
QStringList instanceNames();

// The instance named by MIKSERSTUDIO_INSTANCE when it names one, otherwise the installed app.
QString defaultInstance();

// The discovery file of `instance`, one of instanceNames(): api.json in the folder the app keeps its data
// in, or in MIKSERSTUDIO_DATA_DIR when that is set.
QString discoveryFile(const QString& instance);

// The contents of the discovery file at `path`, or nullopt while there is no usable file: the app is not
// running.
std::optional<Discovery> readDiscovery(const QString& path);

}  // namespace mikserstudio::mcp
