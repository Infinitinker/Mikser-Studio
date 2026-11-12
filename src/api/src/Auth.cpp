#include "Auth.h"

#include <QHttpServerRequest>
#include <QRandomGenerator>
#include <QUrlQuery>

#include <array>

using namespace Qt::StringLiterals;

namespace mikserstudio::api {

namespace {

constexpr qsizetype kMinTokenLength = 16;
constexpr qsizetype kMaxTokenLength = 512;

QByteArray bearerToken(const QHttpServerRequest& request)
{
    const QByteArray header = request.value("Authorization").trimmed();
    const qsizetype space = header.indexOf(' ');
    if (space < 0)
        return {};
    if (header.first(space).compare("Bearer", Qt::CaseInsensitive) != 0)
        return {};
    return header.sliced(space + 1).trimmed();
}

QByteArray queryToken(const QHttpServerRequest& request)
{
    return request.query().queryItemValue(u"token"_s, QUrl::FullyDecoded).toUtf8();
}

}  // namespace

QString generateToken()
{
    std::array<quint32, 8> words{};
    QRandomGenerator::system()->fillRange(words.data(), words.size());
    const auto bytes = QByteArray(reinterpret_cast<const char*>(words.data()), static_cast<qsizetype>(sizeof(words)));
    return QString::fromLatin1(bytes.toBase64(QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals));
}

std::expected<void, QString> checkToken(const QString& token)
{
    if (token.size() < kMinTokenLength || token.size() > kMaxTokenLength) {
        return std::unexpected(
            u"The API token must be %1 to %2 characters long. Leave it empty to generate one."_s.arg(kMinTokenLength)
                .arg(kMaxTokenLength));
    }
    for (const QChar c : token) {
        if (c.unicode() < 0x21 || c.unicode() > 0x7e)
            return std::unexpected(u"The API token may contain only visible ASCII characters (no spaces)."_s);
    }
    return {};
}

bool constantTimeEquals(QByteArrayView given, QByteArrayView expected)
{
    unsigned difference = given.size() == expected.size() ? 0U : 1U;
    for (qsizetype i = 0; i < expected.size(); ++i) {
        const auto g = i < given.size() ? static_cast<unsigned char>(given[i]) : 0U;
        difference |= g ^ static_cast<unsigned char>(expected[i]);
    }
    return difference == 0U;
}

bool isAuthorized(const QHttpServerRequest& request, const QByteArray& token, TokenSource source)
{
    const QByteArray fromHeader = bearerToken(request);
    if (!fromHeader.isEmpty())
        return constantTimeEquals(fromHeader, token);
    if (source == TokenSource::HeaderOrQuery) {
        const QByteArray fromQuery = queryToken(request);
        if (!fromQuery.isEmpty())
            return constantTimeEquals(fromQuery, token);
    }
    return false;
}

}  // namespace mikserstudio::api
