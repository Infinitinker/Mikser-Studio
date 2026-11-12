// The routes of the /v1 local API: maps HTTP requests to command bus calls.
#pragma once

#include "ApiResponse.h"

#include <QByteArray>

class QHttpServerRequest;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::api {

// Maps /v1 HTTP requests to CommandBus calls. Runs on the CommandBus thread.
class Router final {
public:
    Router(core::CommandBus& bus, QByteArray token);

    [[nodiscard]] ApiResponse handle(const QHttpServerRequest& request) const;

private:
    [[nodiscard]] ApiResponse health(const QHttpServerRequest& request) const;
    [[nodiscard]] ApiResponse listCommands(const QHttpServerRequest& request) const;
    [[nodiscard]] ApiResponse command(const QHttpServerRequest& request, const QString& id) const;
    [[nodiscard]] ApiResponse executeCommand(const QHttpServerRequest& request, const QString& id) const;

    core::CommandBus& m_bus;
    QByteArray m_token;
};

}  // namespace mikserstudio::api
