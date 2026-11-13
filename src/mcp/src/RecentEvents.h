// The app's latest events, numbered, so an agent sees what the person changed: the recent_events tool.
#pragma once

#include <QDateTime>
#include <QJsonObject>
#include <QString>

#include <deque>
#include <optional>

namespace mikserstudio::mcp {

class RecentEvents {
public:
    // How many events are kept; older ones are dropped.
    static constexpr std::size_t kKept = 100;

    // The name of the tool, which no command's tool takes.
    static QString toolName();
    // The tool as tools/list lists it.
    static QJsonObject tool();

    void add(const QJsonObject& event, const QDateTime& received);

    // The tool's result for its arguments: the events numbered after `after`, if given, and the latest
    // number, to pass as `after` the next time. An `after` that is not a whole number from 0 to 2^53 is
    // nullopt.
    [[nodiscard]] std::optional<QJsonObject> result(const QJsonObject& arguments) const;

private:
    std::deque<QJsonObject> m_events;
    qint64 m_latest = 0;
};

}  // namespace mikserstudio::mcp
