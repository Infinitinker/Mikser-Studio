// Restricting a file, such as the discovery file with its session token, to the current user.
#pragma once

#include <QString>

#include <expected>

namespace mikserstudio::api {

// Makes an existing file readable and writable only by the current user: mode 0600 on Unix,
// a protected DACL with a single full-control entry for the current user's SID on Windows.
[[nodiscard]] std::expected<void, QString> restrictToOwner(const QString& path);

}  // namespace mikserstudio::api
