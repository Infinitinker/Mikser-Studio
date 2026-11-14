// The network entries of a plugin's manifest: the hosts and ports its jobs may connect to.
#pragma once

#include <QString>

#include <expected>
#include <optional>

namespace mikserstudio::core {

// One "host:port" of a manifest's "network". The host is a name, an IPv4 address, an IPv6 address
// in brackets ("[::1]") or "*", any host that isn't this computer; the port is a number or "*".
// This computer (localhost, 127.0.0.0/8, ::1) is reached only through an entry that names it.
struct NetworkEntry {
    QString host;  // Lower case, without brackets; "*" for any other computer.
    std::optional<quint16> port;  // nullopt for any port.
};

// The entry `text` names, or why it is not one, in the manifest's terms.
[[nodiscard]] std::expected<NetworkEntry, QString> parseNetworkEntry(const QString& text);

}  // namespace mikserstudio::core
