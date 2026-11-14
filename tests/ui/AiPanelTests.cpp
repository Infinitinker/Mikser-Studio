// The AI panel: it is disabled while its commands are missing, generates images through ai.run and text
// through the Ollama plugin's command, lists the AI jobs of jobs.list, and shows a failed request inline.
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include "AiJobRow.h"
#include "AiPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <QCoreApplication>
#include <QJsonArray>
#include <QLabel>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>
#include <QTimer>

using namespace Qt::StringLiterals;
using mikserstudio::core::CommandOutcome;
using mikserstudio::core::CommandResult;
using mikserstudio::core::CommandSpec;
using mikserstudio::ui::AiPanel;

namespace {

// The panel as the user sees it: the "not available" notice, the two prompts with their Generate
// buttons and the job rows, found by their object names; and whether it polls the jobs.
bool isAvailable(const AiPanel& panel)
{
    return panel.findChild<QLabel*>(u"unavailable"_s)->isHidden();
}

QPlainTextEdit* imagePromptEditor(const AiPanel& panel)
{
    return panel.findChild<QPlainTextEdit*>(u"imagePrompt"_s);
}

QPushButton* generateImageButton(const AiPanel& panel)
{
    return panel.findChild<QPushButton*>(u"generateImage"_s);
}

QPlainTextEdit* promptEditor(const AiPanel& panel)
{
    return panel.findChild<QPlainTextEdit*>(u"prompt"_s);
}

QPushButton* generateButton(const AiPanel& panel)
{
    return panel.findChild<QPushButton*>(u"generate"_s);
}

// The error the panel shows, or an empty string while it shows none.
QString errorText(const AiPanel& panel)
{
    const auto* label = panel.findChild<QLabel*>(u"error"_s);
    return label->isHidden() ? QString() : label->text();
}

bool isPolling(const AiPanel& panel)
{
    return panel.findChild<QTimer*>(u"jobPoll"_s)->isActive();
}

qsizetype jobCount(const AiPanel& panel)
{
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);  // Rows of finished jobs.
    return panel.findChildren<mikserstudio::ui::AiJobRow*>().size();
}

// What ai.run gets for `op` with `prompt`.
QJsonObject runParams(const QString& op, const QString& prompt)
{
    return QJsonObject{{u"op"_s, op}, {u"params"_s, QJsonObject{{u"prompt"_s, prompt}}}};
}

const auto kText = u"mikserstudio.ollama.generate_text"_s;
const QStringList kAiCommands{u"ai.run"_s, kText, u"jobs.list"_s, u"jobs.cancel"_s};

// The commands the panel runs, with their names and shapes; records what the panel sends.
struct FakeAi {
    explicit FakeAi(mikserstudio::core::CommandBus& commandBus)
        : bus(commandBus)
    {
    }

    mikserstudio::core::CommandBus& bus;
    QJsonArray jobs;
    QList<QPair<QString, QJsonObject>> calls;  // Except jobs.list polls.
    int polls = 0;

    void registerAll()
    {
        add(u"ai.run"_s, [this](const QJsonObject& params) {
            calls.append({u"ai.run"_s, params});
            return addJob(u"ai.image.generate"_s, u"core"_s, u"Generate image"_s);
        });
        add(kText, [this](const QJsonObject& params) {
            calls.append({kText, params});
            return addJob(kText, u"mikserstudio.ollama"_s, u"Generate text"_s);
        });
        add(u"jobs.list"_s, [this](const QJsonObject&) {
            ++polls;
            return QJsonObject{{u"jobs"_s, jobs}};
        });
        add(u"jobs.cancel"_s, [this](const QJsonObject& params) {
            calls.append({u"jobs.cancel"_s, params});
            return QJsonObject{};
        });
    }

    void unregisterAll(const QStringList& ids = kAiCommands)
    {
        for (const QString& id : ids)
            bus.unregisterCommand(id);
    }

    // A job the panel doesn't list: one that is part of another, or of a kind of the app's own.
    void addOtherJob(const QString& kind, const QString& parent)
    {
        jobs.append(QJsonObject{{u"id"_s, u"job-other-%1"_s.arg(jobs.size())},
                                {u"kind"_s, kind},
                                {u"owner"_s, u"mikserstudio.ollama"_s},
                                {u"parent"_s, parent},
                                {u"title"_s, u"Ollama: /api/generate"_s},
                                {u"state"_s, u"running"_s}});
    }

private:
    void add(const QString& id, std::function<QJsonObject(const QJsonObject&)> handler)
    {
        // Registered outside REQUIRE, whose expansion repeats the expression, which clang-tidy
        // takes for a second move of the handler.
        const bool registered = bus.registerCommand(
            CommandSpec{.id = id,
                        .title = id,
                        .description = id + u"."_s,
                        .category = u"AI"_s,
                        .paramsSchema = QJsonObject{{u"type"_s, u"object"_s}},
                        .shortcut = {},
                        .undoable = false},
            [run = std::move(handler)](const QJsonObject& params) -> CommandResult {
                return CommandOutcome{.result = run(params), .edit = nullptr};
            });
        REQUIRE(registered);
    }

    QJsonObject addJob(const QString& kind, const QString& owner, const QString& title)
    {
        const QString id = u"job-%1"_s.arg(jobs.size() + 1);
        jobs.append(QJsonObject{{u"id"_s, id},
                                {u"kind"_s, kind},
                                {u"owner"_s, owner},
                                {u"parent"_s, QString()},
                                {u"title"_s, title},
                                {u"state"_s, u"running"_s},
                                {u"progress"_s, 0.25},
                                {u"message"_s, u"Working"_s}});
        return QJsonObject{{u"jobId"_s, id}};
    }
};

struct AiFixture {
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    FakeAi ai{bus};
};

}  // namespace

TEST_CASE("AiPanel is disabled while the AI commands are missing", "[ai-panel]")
{
    AiFixture f;
    AiPanel panel(f.bus);
    imagePromptEditor(panel)->setPlainText(u"A red fox in the snow"_s);

    CHECK_FALSE(isAvailable(panel));
    CHECK_FALSE(generateImageButton(panel)->isEnabled());
    CHECK_FALSE(generateButton(panel)->isEnabled());
    CHECK_FALSE(imagePromptEditor(panel)->isEnabled());
    CHECK_FALSE(promptEditor(panel)->isEnabled());

    // Showing the panel does not poll commands that are not there.
    panel.show();
    CHECK_FALSE(isPolling(panel));
    panel.generateImage();
    CHECK(f.ai.calls.isEmpty());
    panel.hide();
}

TEST_CASE("AiPanel enables itself when the AI commands are registered and disables again without them", "[ai-panel]")
{
    AiFixture f;
    AiPanel panel(f.bus);
    REQUIRE_FALSE(isAvailable(panel));

    f.ai.registerAll();
    CHECK(isAvailable(panel));
    CHECK(imagePromptEditor(panel)->isEnabled());
    CHECK(promptEditor(panel)->isEnabled());
    CHECK_FALSE(generateImageButton(panel)->isEnabled());  // No prompt.
    CHECK_FALSE(generateButton(panel)->isEnabled());  // No prompt.

    imagePromptEditor(panel)->setPlainText(u"A red fox in the snow"_s);
    CHECK(generateImageButton(panel)->isEnabled());
    CHECK_FALSE(generateButton(panel)->isEnabled());  // Each card has its own prompt.
    promptEditor(panel)->setPlainText(u"Shot ideas"_s);
    CHECK(generateButton(panel)->isEnabled());

    panel.show();
    CHECK(isPolling(panel));

    // Without the Ollama plugin, only the image card works.
    f.ai.unregisterAll({kText});
    CHECK(isAvailable(panel));
    CHECK(generateImageButton(panel)->isEnabled());
    CHECK_FALSE(generateButton(panel)->isEnabled());
    CHECK_FALSE(promptEditor(panel)->isEnabled());

    f.ai.unregisterAll({u"ai.run"_s, u"jobs.list"_s, u"jobs.cancel"_s});
    CHECK_FALSE(isAvailable(panel));
    CHECK_FALSE(isPolling(panel));
    CHECK_FALSE(generateImageButton(panel)->isEnabled());
    CHECK_FALSE(generateButton(panel)->isEnabled());
    panel.hide();
}

TEST_CASE("AiPanel generates images and text and lists their jobs", "[ai-panel]")
{
    AiFixture f;
    f.ai.registerAll();
    AiPanel panel(f.bus);
    REQUIRE(isAvailable(panel));
    CHECK(jobCount(panel) == 0);

    imagePromptEditor(panel)->setPlainText(u"  A red fox in the snow  "_s);
    generateImageButton(panel)->click();
    REQUIRE(f.ai.calls.size() == 1);
    CHECK(f.ai.calls.at(0).first == u"ai.run"_s);
    CHECK(f.ai.calls.at(0).second == runParams(u"image.generate"_s, u"A red fox in the snow"_s));
    CHECK(imagePromptEditor(panel)->toPlainText().isEmpty());
    CHECK(jobCount(panel) == 1);

    promptEditor(panel)->setPlainText(u"  Shot ideas for the opening  "_s);
    generateButton(panel)->click();
    REQUIRE(f.ai.calls.size() == 2);
    CHECK(f.ai.calls.at(1).first == kText);
    CHECK(f.ai.calls.at(1).second == QJsonObject{{u"prompt"_s, u"Shot ideas for the opening"_s}});
    CHECK(promptEditor(panel)->toPlainText().isEmpty());
    CHECK(jobCount(panel) == 2);
    CHECK(errorText(panel).isEmpty());

    // The jobs that are part of the text job, and those of the app's own kinds, aren't listed.
    f.ai.addOtherJob(u"http.stream_ndjson"_s, u"job-2"_s);
    f.ai.addOtherJob(u"http.request"_s, QString());
    panel.refreshJobs();
    CHECK(jobCount(panel) == 2);

    // Jobs that disappear from jobs.list leave the list.
    f.ai.jobs.removeFirst();
    panel.refreshJobs();
    CHECK(jobCount(panel) == 1);
}

TEST_CASE("AiPanel shows a failed request inline", "[ai-panel]")
{
    AiFixture f;
    f.ai.registerAll();
    f.bus.unregisterCommand(kText);
    REQUIRE(f.bus.registerCommand(CommandSpec{.id = kText,
                                              .title = u"Generate text"_s,
                                              .description = u"Answers a prompt."_s,
                                              .category = u"AI"_s,
                                              .paramsSchema = QJsonObject{{u"type"_s, u"object"_s}},
                                              .shortcut = {},
                                              .undoable = false},
                                  [](const QJsonObject&) -> CommandResult {
                                      return mikserstudio::core::commandFailure(
                                          mikserstudio::core::CommandErrorCode::Failed, u"Ollama has no models."_s);
                                  }));
    AiPanel panel(f.bus);
    promptEditor(panel)->setPlainText(u"Hello"_s);
    panel.generateText();
    CHECK(errorText(panel) == u"Ollama has no models."_s);
    CHECK(promptEditor(panel)->toPlainText() == u"Hello"_s);  // Kept for another try.
}
