// The recent events the bridge keeps for the recent_events tool.
#include "McpTestSupport.h"

#include "RecentEvents.h"

#include <catch2/catch_test_macros.hpp>

#include <QJsonArray>

using namespace Qt::StringLiterals;
using mikserstudio::mcp::RecentEvents;

TEST_CASE("recent events keep only the newest, numbered on", "[mcp]")
{
    RecentEvents recent;
    const QDateTime time = QDateTime::currentDateTimeUtc();
    for (std::size_t i = 0; i < RecentEvents::kKept + 5; ++i)
        recent.add({{u"type"_s, u"command.executed"_s}}, time);

    const QJsonObject all = recent.result({}).value();
    const QJsonArray events = all[u"events"_s].toArray();
    CHECK(static_cast<std::size_t>(events.size()) == RecentEvents::kKept);
    CHECK(events.first()[u"number"_s] == 6);
    CHECK(all[u"latest"_s] == static_cast<qint64>(RecentEvents::kKept + 5));
    CHECK(recent.result({{u"after"_s, all[u"latest"_s]}}).value()[u"events"_s].toArray().isEmpty());
}

TEST_CASE("recent events refuse an after that is not a whole number from 0 to 2^53", "[mcp]")
{
    const RecentEvents recent;
    CHECK_FALSE(recent.result({{u"after"_s, -1}}));
    CHECK_FALSE(recent.result({{u"after"_s, 1.5}}));
    CHECK_FALSE(recent.result({{u"after"_s, u"3"_s}}));
    CHECK_FALSE(recent.result({{u"after"_s, 1e20}}));
    CHECK(recent.result({{u"after"_s, 0}}));
}
