// Whether a job may connect to an address: the network entries of its owner's manifest, and never
// the app's own API.
#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

#include <expected>
#include <optional>

namespace mikserstudio::services {

// True for a host on this computer: localhost and its subdomains, 127.0.0.0/8, ::1, an IPv4 one
// of those inside IPv6, and the any-addresses (0.0.0.0, ::).
[[nodiscard]] bool isThisComputer(const QString& host);

// True for an http or https URL with a host.
[[nodiscard]] bool isWebAddress(const QUrl& url);

// Nothing when `url` is an http or https address that one of `entries` (core::NetworkEntry) allows
// and that isn't `apiPort` on this computer; otherwise why not, in user terms.
[[nodiscard]] std::expected<void, QString> checkNetworkAccess(const QStringList& entries, const QUrl& url,
                                                              std::optional<quint16> apiPort);

}  // namespace mikserstudio::services
