// Reading a manifest's network entries.
#include "mikserstudio/core/NetworkEntry.h"

#include <QRegularExpression>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

std::expected<NetworkEntry, QString> parseNetworkEntry(const QString& text)
{
    // A name or IPv4 address, or an IPv6 address in brackets, then the port.
    static const QRegularExpression pattern(
        uR"(^(?:(\*|[a-z0-9]([a-z0-9-]*[a-z0-9])?(\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)*)|\[([0-9a-f:.]+)\]):(\*|[0-9]{1,5})$)"_s);
    const QString lower = text.trimmed().toLower();
    const QRegularExpressionMatch match = pattern.match(lower);
    const auto refused = [&text] {
        return std::unexpected(
            u"“%1” is not a network entry: write host:port, such as api.example.com:443, 127.0.0.1:11434 or *:443."_s
                .arg(text));
    };
    if (!match.hasMatch())
        return refused();
    NetworkEntry entry;
    entry.host = match.captured(1).isEmpty() ? match.captured(5) : match.captured(1);
    const QString port = match.captured(6);
    if (port != u"*") {
        const int number = port.toInt();
        if (number < 1 || number > 65535)
            return refused();
        entry.port = static_cast<quint16>(number);
    }
    return entry;
}

}  // namespace mikserstudio::core
