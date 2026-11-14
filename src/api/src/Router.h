// The routes of the /v1 local API: maps HTTP requests to command bus calls.
#pragma once

#include "ApiResponse.h"

#include <QByteArray>

#include <chrono>
#include <functional>

class QHttpServerRequest;
class QObject;

namespace mikserstudio::core {
class CommandBus;
}

namespace mikserstudio::api {

// Maps /v1 HTTP requests to CommandBus calls. Runs on the CommandBus thread.
class Router final {
public:
    using Answer = std::function<void(ApiResponse response)>;

    // How long a command run through POST may take before the API stops waiting for it.
    static constexpr std::chrono::seconds kRunTimeout{60};

    Router(core::CommandBus& bus, QByteArray token);

    // Answers `request` through `answer`: at once, or, for a command that answers later
    // (CommandSpec::async), when it has finished or after kRunTimeout. `receiver` is the
    // object on whose thread the answer comes; when it is destroyed first, no answer comes.
    void handle(const QHttpServerRequest& request, QObject* receiver, Answer answer) const;

private:
    // The answer to any request but running a command.
    [[nodiscard]] ApiResponse respond(const QHttpServerRequest& request, const QString& path) const;
    [[nodiscard]] ApiResponse health(const QHttpServerRequest& request) const;
    [[nodiscard]] ApiResponse listCommands(const QHttpServerRequest& request) const;
    [[nodiscard]] ApiResponse describeCommand(const QHttpServerRequest& request, const QString& id) const;
    void executeCommand(const QHttpServerRequest& request, const QString& id, QObject* receiver, Answer answer) const;

    core::CommandBus& m_bus;
    QByteArray m_token;
};

}  // namespace mikserstudio::api
