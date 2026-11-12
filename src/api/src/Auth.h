// Session token handling of the local API: making and checking tokens, and deciding whether a request carries
// the right one.
#pragma once

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <expected>

class QHttpServerRequest;

namespace mikserstudio::api {

// A fresh session token: 32 bytes from the system CSPRNG, base64url without padding.
[[nodiscard]] QString generateToken();
// Accepts tokens of 16 to 512 visible ASCII characters (no spaces), so they fit in a header.
[[nodiscard]] std::expected<void, QString> checkToken(const QString& token);
// Compares in time that depends only on the length of `expected`, not on where they differ.
[[nodiscard]] bool constantTimeEquals(QByteArrayView given, QByteArrayView expected);

enum class TokenSource {
    HeaderOnly,  // "Authorization: Bearer <token>".
    HeaderOrQuery,  // Also "?token=<token>" (browsers cannot set headers on WebSockets).
};

[[nodiscard]] bool isAuthorized(const QHttpServerRequest& request, const QByteArray& token, TokenSource source);

}  // namespace mikserstudio::api
