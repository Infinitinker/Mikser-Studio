#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/ClipSelection.h"

#include "AiPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <QComboBox>
#include <QJsonArray>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QSignalSpy>

using namespace Qt::StringLiterals;
using mikserstudio::core::CommandOutcome;
using mikserstudio::core::CommandResult;
using mikserstudio::core::CommandSpec;
using mikserstudio::ui::AiPanel;

namespace {

const QStringList kAiCommands{u"ai.upscale_clip"_s, u"ai.generate_text"_s, u"ai.jobs"_s, u"ai.cancel_job"_s};

// The ai.* commands with the AI module's names and shapes; records what the panel sends.
struct FakeAi {
    explicit FakeAi(mikserstudio::core::CommandBus& commandBus)
        : bus(commandBus)
    {
    }

    mikserstudio::core::CommandBus& bus;
    QJsonArray jobs;
    QList<QPair<QString, QJsonObject>> calls;  // Except ai.jobs polls.
    int polls = 0;

    void registerAll()
    {
        add(u"ai.upscale_clip"_s, [this](const QJsonObject& params) {
            calls.append({u"ai.upscale_clip"_s, params});
            return addJob(u"Upscale"_s);
        });
        add(u"ai.generate_text"_s, [this](const QJsonObject& params) {
            calls.append({u"ai.generate_text"_s, params});
            return addJob(u"Generate text"_s);
        });
        add(u"ai.jobs"_s, [this](const QJsonObject&) {
            ++polls;
            return QJsonObject{{u"jobs"_s, jobs}};
        });
        add(u"ai.cancel_job"_s, [this](const QJsonObject& params) {
            calls.append({u"ai.cancel_job"_s, params});
            return QJsonObject{};
        });
    }

    void unregisterAll()
    {
        for (const QString& id : kAiCommands)
            bus.unregisterCommand(id);
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

    QJsonObject addJob(const QString& title)
    {
        const QString id = u"job-%1"_s.arg(jobs.size() + 1);
        jobs.append(QJsonObject{{u"id"_s, id},
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
    mikserstudio::core::ProjectModel project;
    mikserstudio::ui::ClipSelection selection{project};
    FakeAi ai{bus};

    AiFixture()
    {
        mikserstudio::core::MediaItem media;
        media.id = u"media-v"_s;
        media.path = u"D:/media/a.mp4"_s;
        media.name = u"a.mp4"_s;
        media.info.kind = mikserstudio::core::MediaKind::Video;
        media.info.durationFrames = 100;
        media.info.hasVideo = true;
        REQUIRE(project.addMedia(media));
        REQUIRE(project.addClip(mikserstudio::core::Clip{
            .id = u"clip-1"_s, .mediaId = u"media-v"_s, .trackId = u"V1"_s, .position = 0, .in = 0, .out = 50}));
    }
};

}  // namespace

TEST_CASE("AiPanel is disabled while the AI commands are missing", "[ai-panel]")
{
    AiFixture f;
    AiPanel panel(f.bus, f.project, f.selection);
    f.selection.select(u"clip-1"_s);

    CHECK_FALSE(panel.isAvailable());
    CHECK_FALSE(panel.upscaleButton()->isEnabled());
    CHECK_FALSE(panel.generateButton()->isEnabled());
    CHECK_FALSE(panel.promptEditor()->isEnabled());
    CHECK_FALSE(panel.scaleEditor()->isEnabled());

    // Showing the panel does not poll a service that is not there.
    panel.show();
    CHECK_FALSE(panel.isPolling());
    panel.upscaleSelected();
    CHECK(f.ai.calls.isEmpty());
    panel.hide();
}

TEST_CASE("AiPanel enables itself when the AI commands are registered and disables again without them", "[ai-panel]")
{
    AiFixture f;
    AiPanel panel(f.bus, f.project, f.selection);
    REQUIRE_FALSE(panel.isAvailable());

    f.ai.registerAll();
    CHECK(panel.isAvailable());
    CHECK(panel.promptEditor()->isEnabled());
    CHECK_FALSE(panel.upscaleButton()->isEnabled());  // Nothing selected.
    CHECK_FALSE(panel.generateButton()->isEnabled());  // No prompt.

    f.selection.select(u"clip-1"_s);
    CHECK(panel.upscaleButton()->isEnabled());
    panel.promptEditor()->setPlainText(u"Shot ideas"_s);
    CHECK(panel.generateButton()->isEnabled());

    panel.show();
    CHECK(panel.isPolling());

    f.ai.unregisterAll();
    CHECK_FALSE(panel.isAvailable());
    CHECK_FALSE(panel.isPolling());
    CHECK_FALSE(panel.upscaleButton()->isEnabled());
    CHECK_FALSE(panel.generateButton()->isEnabled());
    panel.hide();
}

TEST_CASE("AiPanel sends upscale and text jobs and lists them", "[ai-panel]")
{
    AiFixture f;
    f.ai.registerAll();
    AiPanel panel(f.bus, f.project, f.selection);
    REQUIRE(panel.isAvailable());
    CHECK(panel.jobCount() == 0);

    f.selection.select(u"clip-1"_s);
    panel.scaleEditor()->setCurrentIndex(panel.scaleEditor()->findData(3));
    panel.upscaleButton()->click();
    REQUIRE(f.ai.calls.size() == 1);
    CHECK(f.ai.calls.at(0).first == u"ai.upscale_clip"_s);
    CHECK(f.ai.calls.at(0).second == QJsonObject{{u"clipId"_s, u"clip-1"_s}, {u"scale"_s, 3}});
    CHECK(panel.jobCount() == 1);

    panel.promptEditor()->setPlainText(u"  Shot ideas for the opening  "_s);
    panel.generateButton()->click();
    REQUIRE(f.ai.calls.size() == 2);
    CHECK(f.ai.calls.at(1).second == QJsonObject{{u"prompt"_s, u"Shot ideas for the opening"_s}});
    CHECK(panel.promptEditor()->toPlainText().isEmpty());
    CHECK(panel.jobCount() == 2);
    CHECK(panel.errorText().isEmpty());

    // Jobs that disappear from ai.jobs leave the list.
    f.ai.jobs.removeFirst();
    panel.refreshJobs();
    CHECK(panel.jobCount() == 1);
}

TEST_CASE("AiPanel shows a failed request inline", "[ai-panel]")
{
    AiFixture f;
    f.ai.registerAll();
    f.bus.unregisterCommand(u"ai.generate_text"_s);
    REQUIRE(f.bus.registerCommand(CommandSpec{.id = u"ai.generate_text"_s,
                                              .title = u"Generate Text"_s,
                                              .description = u"Generates text."_s,
                                              .category = u"AI"_s,
                                              .paramsSchema = QJsonObject{{u"type"_s, u"object"_s}},
                                              .shortcut = {},
                                              .undoable = false},
                                  [](const QJsonObject&) -> CommandResult {
                                      return mikserstudio::core::commandFailure(
                                          mikserstudio::core::CommandErrorCode::NotAvailable,
                                          u"No language model is installed."_s);
                                  }));
    AiPanel panel(f.bus, f.project, f.selection);
    panel.promptEditor()->setPlainText(u"Hello"_s);
    panel.generateText();
    CHECK(panel.errorText() == u"No language model is installed."_s);
    CHECK(panel.promptEditor()->toPlainText() == u"Hello"_s);  // Kept for another try.
}
