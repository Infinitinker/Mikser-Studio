// The command bus: specs are validated and listed, params are checked and defaulted before the handler runs,
// unknown commands are reported, edits reach the undo stack, and signals fire for executed and failed commands.
#include "TestSupport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QDeadlineTimer>
#include <QMap>
#include <QThread>

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
    SECTION("ids too long for an MCP tool name, the bridge's own tool name, and tool name collisions")
    {
        CommandSpec spec = setSpec();
        spec.id = u"test."_s + QString(59, u'a');  // 64 characters.
        CHECK(bus.registerCommand(spec, okHandler()));
        spec.id = u"test."_s + QString(60, u'b');
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.id = u"recent.events"_s;
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));

        spec.id = u"demo.make_clip"_s;
        CHECK(bus.registerCommand(spec, okHandler()));
        spec.id = u"demo_make.clip"_s;  // Also the tool "demo_make_clip".
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        CHECK_FALSE(bus.contains(u"demo_make.clip"_s));
    }
    SECTION("read-only commands cannot ask first")
    {
        CommandSpec spec = setSpec();
        spec.undoable = false;
        spec.readOnly = true;
        spec.confirmation = u"Really?"_s;
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.confirmation.clear();
        spec.paramsSchema = json(
            R"({"type": "object", "properties": {"sure": {"type": "boolean", "default": false}}})");
        spec.confirmationParam = u"sure"_s;
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
    }
    SECTION("a confirmation parameter is a declared boolean that defaults to false")
    {
        CommandSpec spec = setSpec();
        spec.confirmationParam = u"overwrite"_s;
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));  // Not declared.
        spec.paramsSchema = json(
            R"({"type": "object", "properties": {"overwrite": {"type": "string", "default": "no"}}})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.paramsSchema = json(
            R"({"type": "object", "properties": {"overwrite": {"type": "boolean", "default": true}}})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.paramsSchema = json(R"({"type": "object", "properties": {"overwrite": {"type": "boolean"}}})");
        CHECK_FALSE(bus.registerCommand(spec, okHandler()));
        spec.paramsSchema = json(
            R"({"type": "object", "properties": {"overwrite": {"type": "boolean", "default": false}}})");
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
    CHECK(spec.value(u"confirmationParam") == QJsonValue(QString()));

    CommandSpec guarded = setSpec();
    guarded.id = u"test.guarded"_s;
    guarded.paramsSchema = json(R"({"type": "object", "properties": {"sure": {"type": "boolean", "default": false}}})");
    guarded.confirmation = u"Really set the value?"_s;
    guarded.confirmationParam = u"sure"_s;
    REQUIRE(bus.registerCommand(guarded, okHandler()));
    const QJsonObject guardedJson = toJson(*bus.spec(u"test.guarded"_s));
    CHECK(guardedJson.value(u"confirmation").toString() == u"Really set the value?"_s);
    CHECK(guardedJson.value(u"confirmationParam").toString() == u"sure"_s);

    CommandSpec reader = setSpec();
    reader.id = u"test.read_value"_s;
    reader.undoable = false;
    reader.readOnly = true;
    REQUIRE(bus.registerCommand(reader, okHandler()));
    const QJsonObject readerJson = toJson(*bus.spec(u"test.read_value"_s));
    CHECK(readerJson.value(u"readOnly").toBool());
    CHECK(readerJson.value(u"permission").toString() == u"project.read"_s);

    CHECK(spec.value(u"owner").toString() == u"core"_s);
    CHECK(spec.value(u"permission").toString() == u"project.write"_s);
    CHECK(spec.value(u"resultSchema").toObject().isEmpty());
    CHECK(spec.value(u"startsJob") == QJsonValue(false));
    CHECK(spec.value(u"openWorld") == QJsonValue(false));
    CHECK(spec.value(u"quiet") == QJsonValue(false));

    CommandSpec job = setSpec();
    job.id = u"test.start_job"_s;
    job.undoable = false;
    job.owner = u"hello"_s;
    job.resultSchema = objectResultSchema({{u"jobId"_s, u"string"_s}});
    job.permission = u"network"_s;
    job.startsJob = true;
    job.openWorld = true;
    job.quiet = true;
    REQUIRE(bus.registerCommand(job, okHandler()));
    const QJsonObject jobJson = toJson(*bus.spec(u"test.start_job"_s));
    CHECK(jobJson.value(u"owner").toString() == u"hello"_s);
    CHECK(jobJson.value(u"resultSchema").toObject() ==
          json(R"({"type": "object", "properties": {"jobId": {"type": "string"}}, "required": ["jobId"]})"));
    CHECK(jobJson.value(u"permission").toString() == u"network"_s);
    CHECK(jobJson.value(u"startsJob").toBool());
    CHECK(jobJson.value(u"openWorld").toBool());
    CHECK(jobJson.value(u"quiet").toBool());
}

TEST_CASE("CommandBus refuses an unknown permission, a result schema that isn't an object and no owner", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    CommandSpec permission = setSpec();
    permission.permission = u"everything"_s;
    CHECK_FALSE(bus.registerCommand(permission, okHandler()));
    CommandSpec result = setSpec();
    result.resultSchema = json(R"({"type": "array"})");
    CHECK_FALSE(bus.registerCommand(result, okHandler()));
    CommandSpec owner = setSpec();
    owner.owner.clear();
    CHECK_FALSE(bus.registerCommand(owner, okHandler()));
    CHECK_FALSE(bus.contains(u"test.set_value"_s));
}

TEST_CASE("Undo and redo describe their result", "[commands][undo]")
{
    UndoStack stack;
    CommandBus bus(stack);
    const QJsonObject text = objectResultSchema({{u"text"_s, u"string"_s}});
    CHECK(bus.spec(u"edit.undo"_s)->resultSchema == text);
    CHECK(bus.spec(u"edit.redo"_s)->resultSchema == text);
    CHECK(bus.spec(u"edit.undo"_s)->permission == u"project.write"_s);
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
    QObject::connect(&bus, &CommandBus::failed, [&](const QString& id, const QJsonObject&, const CommandError& error) {
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
    REQUIRE(
        bus.registerCommand(throwing, [](const QJsonObject&) -> CommandResult { throw std::runtime_error("boom"); }));

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

    // A run that needs the user's agreement is a question for the caller, not a failure.
    CommandSpec asking = setSpec();
    asking.id = u"test.ask"_s;
    REQUIRE(bus.registerCommand(asking, [](const QJsonObject&) -> CommandResult {
        return commandFailure(CommandErrorCode::NeedsConfirmation, u"Replace it?"_s);
    }));
    const auto asked = bus.execute(u"test.ask"_s, json(R"({"value": 2})"));
    REQUIRE_FALSE(asked);
    CHECK(asked.error().code == CommandErrorCode::NeedsConfirmation);
    CHECK(failures.size() == 2);
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

TEST_CASE("CommandBus unregisters an owner's commands and updates a spec for its owner", "[commands]")
{
    UndoStack stack;
    CommandBus bus(stack);
    for (const QString& id : {u"hello.greet"_s, u"hello.wave"_s}) {
        CommandSpec spec = setSpec();
        spec.id = id;
        spec.owner = u"hello"_s;
        REQUIRE(bus.registerCommand(spec, okHandler()));
    }
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    QStringList changed;
    QObject::connect(&bus, &CommandBus::commandChanged, [&](const QString& id) { changed.append(id); });

    CommandSpec renamed = *bus.spec(u"hello.greet"_s);
    renamed.title = u"Say hello"_s;
    REQUIRE(bus.updateSpec(renamed));
    CHECK(bus.spec(u"hello.greet"_s)->title == u"Say hello"_s);
    CHECK(changed == QStringList{u"hello.greet"_s});
    CHECK(bus.execute(u"hello.greet"_s, json(R"({"value": 1})")));

    CommandSpec stolen = *bus.spec(u"test.set_value"_s);
    stolen.owner = u"hello"_s;
    stolen.title = u"Mine now"_s;
    CHECK_FALSE(bus.updateSpec(stolen));
    CommandSpec broken = *bus.spec(u"hello.wave"_s);
    broken.title.clear();
    CHECK_FALSE(bus.updateSpec(broken));
    CommandSpec missing = setSpec();
    missing.id = u"hello.missing"_s;
    CHECK_FALSE(bus.updateSpec(missing));
    CHECK(changed.size() == 1);

    QStringList unregistered;
    QObject::connect(&bus, &CommandBus::commandUnregistered, [&](const QString& id) { unregistered.append(id); });
    bus.unregisterOwnedBy(u"hello"_s);
    CHECK(unregistered == QStringList{u"hello.greet"_s, u"hello.wave"_s});
    CHECK(bus.contains(u"test.set_value"_s));
}

TEST_CASE("An async command answers later through executeAsync", "[commands][async]")
{
    UndoStack stack;
    CommandBus bus(stack);
    QObject receiver;
    CommandDone held;
    CommandSpec later = setSpec();
    later.id = u"test.later"_s;
    REQUIRE(bus.registerAsyncCommand(
        later, [&held](const QJsonObject&, const CallContext&, CommandDone done) { held = std::move(done); }));
    REQUIRE(bus.spec(u"test.later"_s)->async);
    CHECK(toJson(*bus.spec(u"test.later"_s)).value(u"async").toBool());

    // execute() can't wait for it.
    const auto refused = bus.execute(u"test.later"_s, json(R"({"value": 1})"));
    REQUIRE_FALSE(refused.has_value());
    CHECK(refused.error().code == CommandErrorCode::NotAvailable);

    QStringList executed;
    QObject::connect(
        &bus, &CommandBus::executed,
        [&](const QString& id, const QJsonObject&, const QJsonObject&, const CallContext&) { executed.append(id); });
    std::optional<std::expected<QJsonObject, CommandError>> answer;
    const auto run = bus.executeAsync(u"test.later"_s, json(R"({"value": 1})"), CallContext::api(), &receiver,
                                      [&answer](auto result) { answer = std::move(result); });
    REQUIRE(held);
    CHECK(bus.isPending(run));
    CHECK_FALSE(answer.has_value());
    int value = 0;
    held(CommandOutcome{QJsonObject{{u"done"_s, true}}, std::make_unique<SetEdit>(value, 0, 4)});
    REQUIRE(answer.has_value());
    REQUIRE(answer->has_value());
    CHECK((*answer)->value(u"done"_s).toBool());
    CHECK(executed == QStringList{u"test.later"_s});
    CHECK(stack.canUndo());
    CHECK_FALSE(bus.isPending(run));

    // A second answer from the handler is dropped.
    held(CommandOutcome{QJsonObject{{u"again"_s, true}}, nullptr});
    CHECK(executed.size() == 1);

    // Commands that aren't async answer before executeAsync returns.
    answer.reset();
    int set = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(set)));
    bus.executeAsync(u"test.set_value"_s, json(R"({"value": 2})"), CallContext::ui(), &receiver,
                     [&answer](auto result) { answer = std::move(result); });
    REQUIRE(answer.has_value());
    CHECK(answer->has_value());
    CHECK(set == 2);
}

TEST_CASE("Canceling an async run answers Canceled and drops the handler's answer", "[commands][async]")
{
    UndoStack stack;
    CommandBus bus(stack);
    QObject receiver;
    CommandDone held;
    CommandSpec later = setSpec();
    later.id = u"test.later"_s;
    REQUIRE(bus.registerAsyncCommand(
        later, [&held](const QJsonObject&, const CallContext&, CommandDone done) { held = std::move(done); }));
    QList<CommandError> failures;
    QObject::connect(&bus, &CommandBus::failed,
                     [&](const QString&, const QJsonObject&, const CommandError& error, const CallContext&) {
                         failures.append(error);
                     });
    std::optional<std::expected<QJsonObject, CommandError>> answer;
    const auto run = bus.executeAsync(u"test.later"_s, json(R"({"value": 1})"), CallContext::ui(), &receiver,
                                      [&answer](auto result) { answer = std::move(result); });
    bus.cancel(run, u"Stopped waiting."_s);
    REQUIRE(answer.has_value());
    REQUIRE_FALSE(answer->has_value());
    CHECK(answer->error().code == CommandErrorCode::Canceled);
    CHECK(answer->error().message == u"Stopped waiting."_s);
    REQUIRE(failures.size() == 1);

    answer.reset();
    held(CommandOutcome{});
    CHECK_FALSE(answer.has_value());
    CHECK(failures.size() == 1);

    // Invalid params fail at once, before the handler runs.
    held = nullptr;
    bus.executeAsync(u"test.later"_s, json(R"({"value": -1})"), CallContext::ui(), &receiver,
                     [&answer](auto result) { answer = std::move(result); });
    REQUIRE(answer.has_value());
    CHECK(answer->error().code == CommandErrorCode::InvalidParams);
    CHECK_FALSE(held);
}

TEST_CASE("executeAsync from another thread runs on the bus's thread and answers on the receiver's",
          "[commands][async]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);  // Queued calls need an event dispatcher.
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    QThread* ranOn = nullptr;
    CommandSpec spec = setSpec();
    REQUIRE(bus.registerCommand(spec, [&value, &ranOn](const QJsonObject& params) -> CommandResult {
        ranOn = QThread::currentThread();
        value = params.value(u"value").toInt();
        return CommandOutcome{};
    }));

    QThread worker;
    QObject receiver;
    receiver.moveToThread(&worker);
    worker.start();
    std::atomic<QThread*> answeredOn = nullptr;
    QMetaObject::invokeMethod(&receiver, [&] {
        bus.executeAsync(u"test.set_value"_s, json(R"({"value": 7})"), CallContext::ui(), &receiver,
                         [&answeredOn](auto) { answeredOn = QThread::currentThread(); });
    });
    const QDeadlineTimer deadline(std::chrono::seconds(10));
    while (answeredOn.load() == nullptr && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    REQUIRE(answeredOn.load() != nullptr);
    CHECK(ranOn == bus.thread());
    CHECK(answeredOn.load() == &worker);
    CHECK(value == 7);
    worker.quit();
    worker.wait();
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
    CHECK(toString(CommandErrorCode::NeedsConfirmation) == u"needs_confirmation"_s);
    CHECK(toString(CommandErrorCode::PermissionDenied) == u"permission_denied"_s);
    CHECK(toString(CommandErrorCode::Canceled) == u"canceled"_s);
}

TEST_CASE("A run's call context reaches the signals and the commands its handler runs", "[commands][context]")
{
    UndoStack stack;
    CommandBus bus(stack);
    CommandSpec inner = setSpec();
    inner.id = u"test.inner"_s;
    inner.undoable = false;
    inner.paramsSchema = {};
    REQUIRE(bus.registerCommand(inner, okHandler()));
    CommandSpec outer = inner;
    outer.id = u"test.outer"_s;
    REQUIRE(bus.registerCommand(outer, [&bus](const QJsonObject&) -> CommandResult {
        CHECK(bus.runningContext() != nullptr);
        if (auto ran = bus.execute(u"test.inner"_s); !ran)
            return std::unexpected(ran.error());
        return CommandOutcome{};
    }));

    QMap<QString, QStringList> origins;
    QMap<QString, bool> confirmed;
    QObject::connect(&bus, &CommandBus::executed,
                     [&](const QString& id, const QJsonObject&, const QJsonObject&, const CallContext& context) {
                         origins.insert(id, context.origins);
                         confirmed.insert(id, context.confirmed);
                     });

    CallContext agreed = CallContext::api();
    agreed.confirmed = true;
    REQUIRE(bus.execute(u"test.outer"_s, {}, agreed));
    CHECK(origins.value(u"test.outer"_s) == QStringList{u"api"_s});
    CHECK(origins.value(u"test.inner"_s) == QStringList{u"api"_s});
    CHECK(confirmed.value(u"test.outer"_s));
    // The person agreed to the outer run, not to what its handler runs.
    CHECK_FALSE(confirmed.value(u"test.inner"_s));
    CHECK(bus.runningContext() == nullptr);

    REQUIRE(bus.execute(u"test.outer"_s));
    CHECK(origins.value(u"test.inner"_s) == QStringList{u"ui"_s});

    const CallContext plugin = CallContext::api().through(u"plugin:hello"_s, {u"project.read"_s, u"network"_s});
    CHECK(plugin.origins == QStringList{u"api"_s, u"plugin:hello"_s});
    CHECK(plugin.origin() == u"api"_s);
    CHECK(plugin.permissions == QStringList{u"project.read"_s, u"network"_s});
}

TEST_CASE("A caller without a command's permission is refused before it runs", "[commands][context]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    QList<CallContext> refused;
    QObject::connect(&bus, &CommandBus::failed,
                     [&](const QString&, const QJsonObject&, const CommandError&, const CallContext& context) {
                         refused.append(context);
                     });

    const CallContext reader = CallContext::ui().through(u"plugin:hello"_s, {u"project.read"_s});
    const auto denied = bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 3}}, reader);
    REQUIRE_FALSE(denied.has_value());
    CHECK(denied.error().code == CommandErrorCode::PermissionDenied);
    CHECK(denied.error().message.contains(u"“project.write”"_s));
    CHECK(denied.error().message.contains(u"the plugin “hello”"_s));
    CHECK(value == 0);
    REQUIRE(refused.size() == 1);
    CHECK(refused.constFirst().origins == QStringList{u"ui"_s, u"plugin:hello"_s});

    CHECK(bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 3}}, CallContext::api()));
    CHECK(value == 3);
}

TEST_CASE("A plugin can't agree for the person to a command that asks first", "[commands][context]")
{
    UndoStack stack;
    CommandBus bus(stack);
    int runs = 0;
    QJsonObject seen;
    const auto counting = [&runs, &seen](const QJsonObject& params) -> CommandResult {
        ++runs;
        seen = params;
        return CommandOutcome{};
    };
    CommandSpec always = setSpec();
    always.id = u"test.replace"_s;
    always.undoable = false;
    always.paramsSchema = {};
    always.confirmation = u"Replace the value?"_s;
    REQUIRE(bus.registerCommand(always, counting));
    CommandSpec sometimes = always;
    sometimes.id = u"test.discard"_s;
    sometimes.confirmation.clear();
    sometimes.paramsSchema = json(
        R"({"type": "object", "properties": {"sure": {"type": "boolean", "default": false}}})");
    sometimes.confirmationParam = u"sure"_s;
    REQUIRE(bus.registerCommand(sometimes, counting));

    const CallContext plugin = CallContext::api().through(u"plugin:hello"_s, allPermissions());
    const auto asked = bus.execute(u"test.replace"_s, {}, plugin);
    REQUIRE_FALSE(asked.has_value());
    CHECK(asked.error().code == CommandErrorCode::NeedsConfirmation);
    CHECK(asked.error().message == u"Replace the value?"_s);
    CHECK(runs == 0);

    CHECK(bus.execute(u"test.discard"_s, QJsonObject{{u"sure"_s, true}}, plugin));
    CHECK(runs == 1);
    CHECK(seen.value(u"sure"_s) == false);  // The plugin's own agreement is dropped.

    CallContext agreed = plugin;
    agreed.confirmed = true;  // As ms.ui.confirm gives it, for one call.
    CHECK(bus.execute(u"test.replace"_s, {}, agreed));
    CHECK(bus.execute(u"test.discard"_s, {}, agreed));
    CHECK(runs == 3);
    CHECK(seen.value(u"sure"_s) == true);

    // The API and the interface still run them as before: the person's client asks.
    CHECK(bus.execute(u"test.replace"_s, {}, CallContext::api()));
    CHECK(bus.execute(u"test.discard"_s, QJsonObject{{u"sure"_s, true}}, CallContext::ui()));
    CHECK(runs == 5);
    CHECK(seen.value(u"sure"_s) == true);
}

TEST_CASE("Commands that run each other without end stop at the depth limit", "[commands][context]")
{
    UndoStack stack;
    CommandBus bus(stack);
    CommandSpec loop = setSpec();
    loop.id = u"test.loop"_s;
    loop.undoable = false;
    loop.paramsSchema = {};
    std::size_t runs = 0;
    REQUIRE(bus.registerCommand(loop, [&bus, &runs](const QJsonObject&) -> CommandResult {
        ++runs;
        if (auto again = bus.execute(u"test.loop"_s); !again)
            return std::unexpected(again.error());
        return CommandOutcome{};
    }));

    const auto result = bus.execute(u"test.loop"_s);
    REQUIRE_FALSE(result.has_value());
    CHECK(result.error().code == CommandErrorCode::Failed);
    CHECK(result.error().message.contains(u"levels deep"_s));
    CHECK(runs == CommandBus::kMaxDepth);
    CHECK(bus.runningContext() == nullptr);
}

TEST_CASE("A transaction's changes are one undo step, and other callers' writes wait for it",
          "[commands][transactions]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);  // The deadline and the waiting runs need an event loop.
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    CommandSpec reader = setSpec();
    reader.id = u"test.read_value"_s;
    reader.undoable = false;
    reader.readOnly = true;
    REQUIRE(bus.registerCommand(reader, okHandler()));
    const CallContext plugin = CallContext::api().through(u"plugin:hello"_s, allPermissions());

    const auto transaction = bus.beginTransaction(u"Set twice"_s, plugin);
    REQUIRE(transaction);
    CHECK(bus.inTransaction());
    REQUIRE(bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 1}}, plugin));
    REQUIRE(bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 2}}, plugin));
    CHECK_FALSE(bus.beginTransaction(u"Another"_s, CallContext::ui()));

    // The person's write waits; a read doesn't.
    const auto refused = bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 9}}, CallContext::ui());
    REQUIRE_FALSE(refused);
    CHECK(refused.error().code == CommandErrorCode::NotAvailable);
    CHECK(refused.error().message.contains(u"the plugin “hello”"_s));
    CHECK(bus.execute(u"test.read_value"_s, QJsonObject{{u"value"_s, 0}}, CallContext::ui()));
    QObject receiver;
    std::optional<int> waited;
    bus.executeAsync(u"test.set_value"_s, QJsonObject{{u"value"_s, 5}}, CallContext::ui(), &receiver,
                     [&waited, &value](const auto& result) { waited = result ? value : -1; });
    CHECK_FALSE(waited.has_value());

    bus.commitTransaction(*transaction);
    CHECK_FALSE(bus.inTransaction());
    REQUIRE(waited.has_value());
    CHECK(*waited == 5);
    CHECK(stack.undoText() == u"Set Value"_s);
    REQUIRE(stack.undo());
    CHECK(value == 2);
    CHECK(stack.undoText() == u"Set twice"_s);
    CHECK(stack.undoOrigins() == QStringList{u"api"_s, u"plugin:hello"_s});
    REQUIRE(stack.undo());
    CHECK(value == 0);
}

TEST_CASE("A transaction is rolled back when asked and at its deadline", "[commands][transactions]")
{
    int argc = 0;
    QCoreApplication app(argc, nullptr);
    UndoStack stack;
    CommandBus bus(stack);
    int value = 0;
    REQUIRE(bus.registerCommand(setSpec(), setHandler(value)));
    const CallContext plugin = CallContext::api().through(u"plugin:hello"_s, allPermissions());
    QStringList reasons;
    QObject::connect(&bus, &CommandBus::transactionRolledBack, &bus,
                     [&reasons](CommandBus::TransactionId, const QString& reason) { reasons.append(reason); });

    const auto first = bus.beginTransaction(u"Fails halfway"_s, plugin);
    REQUIRE(first);
    REQUIRE(bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 4}}, plugin));
    bus.rollBackTransaction(*first, u"The plugin raised an error."_s);
    CHECK(value == 0);
    CHECK_FALSE(stack.canUndo());
    CHECK(reasons == QStringList{u"The plugin raised an error."_s});
    bus.commitTransaction(*first);  // Ended already: nothing happens.
    CHECK_FALSE(stack.canUndo());

    const auto slow = bus.beginTransaction(u"Too slow"_s, plugin);
    REQUIRE(slow);
    REQUIRE(bus.execute(u"test.set_value"_s, QJsonObject{{u"value"_s, 7}}, plugin));
    QObject receiver;
    bool waitedRan = false;
    bus.executeAsync(u"test.set_value"_s, QJsonObject{{u"value"_s, 3}}, CallContext::ui(), &receiver,
                     [&waitedRan](const auto& result) { waitedRan = result.has_value(); });
    const QDeadlineTimer deadline(CommandBus::kTransactionDeadline + std::chrono::seconds(3));
    while (bus.inTransaction() && !deadline.hasExpired())
        QCoreApplication::processEvents(QEventLoop::AllEvents, 20);
    REQUIRE_FALSE(bus.inTransaction());
    REQUIRE(reasons.size() == 2);
    CHECK(reasons.constLast().contains(u"took longer than 2 seconds"_s));
    CHECK(waitedRan);
    CHECK(value == 3);
    CHECK(stack.undoText() == u"Set Value"_s);
    REQUIRE(stack.undo());
    CHECK(value == 0);  // The rolled back 7 left no step.
    CHECK_FALSE(stack.canUndo());

    CHECK_FALSE(bus.beginTransaction(u"Reader"_s, CallContext::ui().through(u"plugin:reader"_s, {u"project.read"_s})));
}
