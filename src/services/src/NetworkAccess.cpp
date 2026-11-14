// Matching an address against network entries.
#include "NetworkAccess.h"

#include "mikserstudio/core/NetworkEntry.h"

#include <QHostAddress>

#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

// host:port as people write it, with an IPv6 address in brackets.
QString hostAndPort(const QString& host, int port)
{
    return (host.contains(u':') ? u"[%1]"_s.arg(host) : host) + u":"_s + QString::number(port);
}

}  // namespace

bool isThisComputer(const QString& host)
{
    const QString name = host.toLower();
    if (name == u"localhost" || name.endsWith(u".localhost"))
        return true;
    QHostAddress address;
    if (!address.setAddress(name))
        return false;
    bool isIpv4 = false;
    const quint32 ipv4 = address.toIPv4Address(&isIpv4);
    if (address.isLoopback() || (isIpv4 && (ipv4 >> 24U) == 127U))
        return true;
    return address == QHostAddress(QHostAddress::AnyIPv4) || address == QHostAddress(QHostAddress::AnyIPv6) ||
           (isIpv4 && ipv4 == 0U);
}

bool isWebAddress(const QUrl& url)
{
    return url.isValid() && (url.scheme() == u"http" || url.scheme() == u"https") && !url.host().isEmpty();
}

std::expected<void, QString> checkNetworkAccess(const QStringList& entries, const QUrl& url,
                                                std::optional<quint16> apiPort)
{
    if (!isWebAddress(url)) {
        return std::unexpected(
            u"“%1” is not an http address. Enter one with a host and port, such as http://127.0.0.1:11434."_s.arg(
                url.toString()));
    }
    const QString host = url.host().toLower();
    const int port = url.port(url.scheme() == u"https" ? 443 : 80);
    const bool local = isThisComputer(host);
    if (local && apiPort && std::cmp_equal(port, *apiPort)) {
        return std::unexpected(
            u"%1 is Mikser Studio's own API, which plugins can't connect to."_s.arg(hostAndPort(host, port)));
    }
    for (const QString& text : entries) {
        const auto entry = core::parseNetworkEntry(text);
        if (!entry || (entry->port && std::cmp_not_equal(*entry->port, port)))
            continue;
        if (entry->host == u"*" ? !local : (local ? isThisComputer(entry->host) : entry->host == host))
            return {};
    }
    return std::unexpected(
        u"The plugin may not connect to %1: its manifest's network entries (%2) don't include it."_s.arg(
            hostAndPort(host, port), entries.isEmpty() ? u"none"_s : entries.join(u", "_s)));
}

}  // namespace mikserstudio::services
