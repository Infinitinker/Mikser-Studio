#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>

#include <stdexcept>

using namespace Qt::StringLiterals;
using namespace mikserstudio::core;
using mikserstudio::tests::json;

namespace {

class SetEdit final : public UndoableEdit {
public:
    SetEdit(int& target, int before, int after)
        : m_target(target)
        , m_before(before)
        , m_after(after)
    {
    }
    void undo() override { m_target = m_before; }
    void redo() override { m_target = m_after; }
    [[nodiscard]] QString text() const override { return u"Set Value"_s; }

private:
    int& m_target;
    int m_before;
    int m_after;
};

CommandSpec setSpec()
{
    return CommandSpec{
        .id = u"test.set_value"_s,
        .title = u"Set Value"_s,
        .description = u"Sets the test value."_s,
        .category = u"Test"_s,
        .paramsSchema = json(R"({
            "type": "object",
            "properties": {
                "value": {"type": "integer", "minimum": 0},
                "label": {"type": "string", "default": "none"}
            },
            "required": ["value"],
            "additionalProperties": false
        })"),
        .shortcut = u"Ctrl+T"_s,
        .undoable = true,
    };
}

CommandHandler setHandler(int& target, QJsonObject* seenParams = nullptr)
{
    return [&target, seenParams](const QJsonObject& params) -> CommandResult {
        if (seenParams)
            *seenParams = params;
        const int before = target;
        target = params.value(u"value").toInt();
        return CommandOutcome{QJsonObject{{u"previous"_s, before}}, std::make_unique<SetEdit>(target, before, target)};
    };
}

CommandHandler okHandler()
{
    return [](const QJsonObject&) -> CommandResult { return CommandOutcome{}; };
}

}  // namespace

TEST_CASE("CommandBus registers the built-in undo and redo commands", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    REQUIRE(bus.spec(u"edit.undo"_s));
    REQUIRE(bus.spec(u"edit.redo"_s));
    CHECK(bus.spec(u"edit.undo"_s)->shortcut == u"Ctrl+Z"_s);
    CHECK(bus.spec(u"edit.redo"_s)->shortcut == u"Ctrl+Shift+Z"_s);
    CHECK(bus.spec(u"edit.undo"_s)->category == u"Edit"_s);
    CHECK_FALSE(bus.spec(u"edit.undo"_s)->undoable);
}

TEST_CASE("CommandBus rejects malformed specs", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);

    SECTION("bad ids")
    {
        for (const QString& id : {u"single"_s, u"Upper.case"_s, u"dash-ed.id"_s, u"trailing."_s, u".leading"_s,
                                  u"a..b"_s, u""_s, u"sp ace.x"_s}) {
            CommandSpec spec = setSpec();
            spec.id = id;
            CHECK_FALSE(bus.registerCommand(spec, okHandler()));
            CHECK_FALSE(bus.contains(id));
        }
        CommandSpec spec = setSpec();
        spec.id = u"a1.b_2.c3"_s;
        CHECK(bus.registerCommand(spec, okHandler()));
    }
    SECTION("schema that is not an object schema")
    {
        CommandSpec spec = setSpec();
        spec.paramsSchema = json(R"({"type": "string"})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.paramsSchema = json(R"({"properties": {}})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.paramsSchema = json(R"({"type": "object", "properties": {"x": 5}})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.paramsSchema = json(R"({"type": "object", "required": "x"})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
    }
    SECTION("read-only commands cannot be undoable")
    {
        CommandSpec spec = setSpec();
        spec.readOnly = true;
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.undoable = false;
        CHECK(bus.registerCommand(spec, okHandler()));
    }
    SECTION("missing title or handler, duplicate id")
    {
        CommandSpec spec = setSpec();
        spec.title = u"  "_s;
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        CHECK_FALSE(bus.registerCommand(setSpec(), CommandHandler()));
        CHECK(bus.registerCommand(setSpec(), okHandler()));
        CHECK_FALSE(bus.registerCommand(setSpec(), okHandler()));
    }
}

TEST_CASE("CommandBus lists specs sorted and converts them to JSON", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    CommandSpec other = setSpec();
    other.id = u"aaa.first"_s;
    REQUIRE(bus.registerCommand(other, okHandler()));

    const QList<CommandSpec> specs = bus.specs();
    REQUIRE(specs.size() == 4);
    CHECK(specs[0].id == u"aaa.first"_s);
    CHECK(specs[1].id == u"edit.redo"_s);
    CHECK(specs[2].id == u"edit.undo"_s);
    CHECK(specs[3].id == u"test.set_value"_s);

    const QJsonObject spec = toJson(*bus.spec(u"test.set_value"_s));
    CHECK(spec.value(u"id").toString() == u"test.set_value"_s);
    CHECK(spec.value(u"title").toString() == u"Set Value"_s);
    CHECK(spec.value(u"description").toString() == u"Sets the test value."_s);
    CHECK(spec.value(u"category").toString() == u"Test"_s);
    CHECK(spec.value(u"shortcut").toString() == u"Ctrl+T"_s);
    CHECK(spec.value(u"undoable").toBool());
    CHECK(spec.value(u"paramsSchema").toObject() == setSpec().paramsSchema);
    CHECK(spec.value(u"readOnly") == QJsonValue(false));
    CHECK(spec.value(u"confirmation") == QJsonValue(QString()));

    CommandSpec guarded = setSpec();
    guarded.id = u"test.guarded"_s;
    guarded.undoable = false;
    guarded.readOnly = true;
    guarded.confirmation = u"Really read the value?"_s;
    REQUIRE(bus.registerCommand(guarded, okHandler()));
    const QJsonObject guardedJson = toJson(*bus.spec(u"test.guarded"_s));
    CHECK(guardedJson.value(u"readOnly").toBool());
    CHECK(guardedJson.value(u"confirmation").toString() == u"Really read the value?"_s);
}

TEST_CASE("CommandBus fills an empty params schema with one that takes no params", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    CommandSpec spec = setSpec();
    spec.paramsSchema = {};
    REQUIRE(bus.registerCommand(spec, okHandler()));
    CHECK(bus.spec(spec.id)->paramsSchema.value(u"type").toString() == u"object"_s);
    CHECK(bus.execute(spec.id).has_value());
    const auto rejected = bus.execute(spec.id, json(R"({"extra": 1})"));
    REQUIRE_FALSE(rejected);
    CHECK(rejected.error().code == CommandErrorCode::InvalidParams);
}

TEST_CASE("CommandBus validates params and applies defaults before the handler", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    QJsonObject seen;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value, &seen)));

    SECTION("invalid params never reach the handler")
    {
        const auto missing = bus.execute(u"test.set_value"_s);
        REQUIRE_FALSE(missing);
        CHECK(missing.error().code == CommandErrorCode::InvalidParams);
        CHECK(missing.error().message.contains(u"/value: is required"_s));

        const auto negative = bus.execute(u"test.set_value"_s, json(R"({"value": -1})"));
        REQUIRE_FALSE(negative);
        CHECK(negative.error().message.contains(u"/value: must be >= 0"_s));

        const auto fraction = bus.execute(u"test.set_value"_s, json(R"({"value": 1.5})"));
        REQUIRE_FALSE(fraction);
        CHECK(fraction.error().message.contains(u"/value: must be an integer"_s));

        const auto extra = bus.execute(u"test.set_value"_s, json(R"({"value": 1, "bogus": true})"));
        REQUIRE_FALSE(extra);
        CHECK(extra.error().message.contains(u"/bogus"_s));
        CHECK(value == 0);
        CHECK(seen.isEmpty());
    }
    SECTION("defaults are applied")
    {
        const auto result = bus.execute(u"test.set_value"_s, json(R"({"value": 4})"));
        REQUIRE(result);
        CHECK(seen == json(R"({"value": 4, "label": "none"})"));
        CHECK(result->value(u"previous").toInt() == 0);
    }
}

TEST_CASE("CommandBus reports unknown commands", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    QString failedId;
    CommandError failedError;
    QObject::connect(&bus, &CommandBus::failed,
                     [&](const QString& id, const QJsonObject&, const CommandError& error) {
                         failedId = id;
                         failedError = error;
                     });
    const auto result = bus.execute(u"no.such_command"_s);
    REQUIRE_FALSE(result);
    CHECK(result.error().code == CommandErrorCode::UnknownCommand);
    CHECK(result.error().message.contains(u"no.such_command"_s));
    CHECK(failedId == u"no.such_command"_s);
    CHECK(failedError.code == CommandErrorCode::UnknownCommand);
}

TEST_CASE("CommandBus pushes edits so edit.undo and edit.redo work", "[commands][undo]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));

    const auto nothing = bus.execute(u"edit.undo"_s);
    REQUIRE_FALSE(nothing);
    CHECK(nothing.error().code == CommandErrorCode::NotAvailable);
    CHECK(bus.execute(u"edit.redo"_s).error().code == CommandErrorCode::NotAvailable);

    REQUIRE(bus.execute(u"test.set_value"_s, json(R"({"value": 7})")));
    REQUIRE(bus.execute(u"test.set_value"_s, json(R"({"value": 9})")));
    CHECK(value == 9);
    CHECK(stack.undoText() == u"Set Value"_s);

    const auto undone = bus.execute(u"edit.undo"_s);
    REQUIRE(undone);
    CHECK(undone->value(u"text").toString() == u"Set Value"_s);
    CHECK(value == 7);
    REQUIRE(bus.execute(u"edit.undo"_s));
    CHECK(value == 0);
    REQUIRE(bus.execute(u"edit.redo"_s));
    CHECK(value == 7);
}

TEST_CASE("CommandBus emits executed and failed", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    CommandSpec failing = setSpec();
    failing.id = u"test.fail"_s;
    REQUIRE(bus.registerCommand(failing, [](const QJsonObject&) -> CommandResult {
        return commandFailure(CommandErrorCode::Failed, u"It broke."_s);
    }));
    CommandSpec throwing = setSpec();
    throwing.id = u"test.throw"_s;
    REQUIRE(bus.registerCommand(throwing, [](const QJsonObject&) -> CommandResult {
        throw std::runtime_error("boom");
    }));

    QStringList executed;
    QJsonObject executedParams;
    QJsonObject executedResult;
    QList<CommandError> failures;
    QObject::connect(&bus, &CommandBus::executed,
                     [&](const QString& id, const QJsonObject& params, const QJsonObject& result) {
                         executed.append(id);
                         executedParams = params;
                         executedResult = result;
                     });
    QObject::connect(&bus, &CommandBus::failed,
                     [&](const QString&, const QJsonObject&, const CommandError& error) { failures.append(error); });

    REQUIRE(bus.execute(u"test.set_value"_s, json(R"({"value": 2})")));
    CHECK(executed == QStringList{u"test.set_value"_s});
    CHECK(executedParams == json(R"({"value": 2, "label": "none"})"));
    CHECK(executedResult == json(R"({"previous": 0})"));

    const auto failed = bus.execute(u"test.fail"_s, json(R"({"value": 2})"));
    REQUIRE_FALSE(failed);
    CHECK(failed.error().message == u"It broke."_s);
    const auto thrown = bus.execute(u"test.throw"_s, json(R"({"value": 2})"));
    REQUIRE_FALSE(thrown);
    CHECK(thrown.error().code == CommandErrorCode::Failed);
    CHECK(thrown.error().message.contains(u"boom"_s));
    REQUIRE(failures.size() == 2);
    CHECK(failures[0].code == CommandErrorCode::Failed);
    CHECK(executed.size() == 1);
    CHECK(stack.undoText() == u"Set Value"_s);
}

TEST_CASE("CommandBus delivers failed() over queued connections", "[commands]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);  // Queued calls need an event dispatcher.
    UndoStack stack;
    CommandBus bus(stack);
    CHECK(QMetaType::fromType<CommandError>().isRegistered());

    QObject receiver;
    QList<CommandError> failures;
    QObject::connect(
        &bus, &CommandBus::failed, &receiver,
        [&failures](const QString&, const QJsonObject&, const CommandError& error) { failures.append(error); },
        Qt::QueuedConnection);
    REQUIRE_FALSE(bus.execute(u"edit.undo"_s));
    CHECK(failures.isEmpty());
    QCoreApplication::sendPostedEvents();
    REQUIRE(failures.size() == 1);
    CHECK(failures[0].code == CommandErrorCode::NotAvailable);
    CHECK(failures[0].message == u"There is nothing to undo."_s);
}

TEST_CASE("A handler can group the commands it runs into one undo step", "[commands][undo]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    CommandSpec macro = setSpec();
    macro.id = u"test.macro"_s;
    macro.title = u"Macro"_s;
    REQUIRE(bus.registerCommand(macro, [&bus](const QJsonObject& params) -> CommandResult {
        UndoGroup group(bus.undoStack(), u"Run Macro"_s);
        const int target = params.value(u"value").toInt();
        for (const int step : {target / 2, target}) {
            if (auto done = bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, step}}); !done)
                return std::unexpected(done.error());
        }
        const auto blocked = bus.execute(u"edit.undo"_s);
        if (blocked || blocked.error().code != CommandErrorCode::NotAvailable)
            return commandFailure(CommandErrorCode::Failed, u"edit.undo ran inside an open group"_s);
        return CommandOutcome{QJsonObject{{u"undoMessage"_s, blocked.error().message}}, nullptr};
    }));

    REQUIRE(bus.execute(u"test.set_value"_s, json(R"({"value": 1})")));
    const auto ran = bus.execute(u"test.macro"_s, json(R"({"value": 8})"));
    REQUIRE(ran);
    CHECK(ran->value(u"undoMessage").toString().contains(u"combined change"_s));
    CHECK(value == 8);
    CHECK_FALSE(stack.isGrouping());
    CHECK(stack.undoText() == u"Run Macro"_s);

    REQUIRE(bus.execute(u"edit.undo"_s));
    CHECK(value == 1);
    CHECK(stack.undoText() == u"Set Value"_s);
    REQUIRE(bus.execute(u"edit.redo"_s));
    CHECK(value == 8);
}

TEST_CASE("CommandBus unregisters commands and emits registration signals", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    QStringList registered;
    QStringList unregistered;
    QObject::connect(&bus, &CommandBus::commandRegistered, [&](const QString& id) { registered.append(id); });
    QObject::connect(&bus, &CommandBus::commandUnregistered, [&](const QString& id) { unregistered.append(id); });

    REQUIRE(bus.registerCommand(setSpec(), okHandler()));
    bus.unregisterCommand(u"test.set_value"_s);
    bus.unregisterCommand(u"test.set_value"_s);
    CHECK(registered == QStringList{u"test.set_value"_s});
    CHECK(unregistered == QStringList{u"test.set_value"_s});
    CHECK_FALSE(bus.contains(u"test.set_value"_s));
    CHECK(bus.spec(u"test.set_value"_s) == nullptr);
    CHECK(bus.execute(u"test.set_value"_s, json(R"({"value": 1})")).error().code == CommandErrorCode::UnknownCommand);
}

TEST_CASE("A handler can unregister its own command while running", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    CommandSpec spec = setSpec();
    spec.paramsSchema = {};
    REQUIRE(bus.registerCommand(spec, [&bus, id = spec.id](const QJsonObject&) -> CommandResult {
        bus.unregisterCommand(id);
        return CommandOutcome{QJsonObject{{u"ran"_s, true}}, nullptr};
    }));
    const auto result = bus.execute(spec.id);
    REQUIRE(result);
    CHECK(result->value(u"ran").toBool());
    CHECK_FALSE(bus.contains(spec.id));
}

TEST_CASE("Command error codes have stable names", "[commands]")
{
    CHECK(toString(CommandErrorCode::UnknownCommand) == u"unknown_command"_s);
    CHECK(toString(CommandErrorCode::InvalidParams) == u"invalid_params"_s);
    CHECK(toString(CommandErrorCode::Failed) == u"failed"_s);
    CHECK(toString(CommandErrorCode::NotAvailable) == u"not_available"_s);
}
