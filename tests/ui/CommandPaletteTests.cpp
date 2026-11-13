// The command palette: fuzzy scoring, the model that lists, filters and ranks commands, and the popup that
// shows the bus commands and runs the current one.
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/CommandPalette.h"

#include "CommandPaletteModel.h"
#include "FuzzyMatch.h"
#include "TestCommands.h"

#include <catch2/catch_test_macros.hpp>

#include <QWidget>

using namespace Qt::StringLiterals;
using mikserstudio::core::CommandSpec;
using mikserstudio::ui::CommandPaletteModel;
using mikserstudio::ui::fuzzyScore;

namespace {

CommandSpec spec(const QString& id, const QString& title, const QString& category, const QString& shortcut = {})
{
    return CommandSpec{.id = id,
                       .title = title,
                       .description = title + u"."_s,
                       .category = category,
                       .paramsSchema = {},
                       .shortcut = shortcut,
                       .undoable = false};
}

QList<CommandSpec> sampleSpecs()
{
    return {
        spec(u"timeline.add_clip"_s, u"Add clip to timeline"_s, u"Timeline"_s),
        spec(u"project.import_media"_s, u"Import media"_s, u"Project"_s, u"Ctrl+I"_s),
        spec(u"edit.undo"_s, u"Undo"_s, u"Edit"_s, u"Ctrl+Z"_s),
        spec(u"ai.run"_s, u"Run AI operation"_s, u"AI"_s),
        spec(u"timeline.remove_clip"_s, u"Remove clip"_s, u"Timeline"_s),
    };
}

}  // namespace

TEST_CASE("fuzzyScore matches case-insensitive subsequences", "[palette]")
{
    CHECK(fuzzyScore(u"", u"anything") == 0);
    CHECK(fuzzyScore(u"tlac", u"timeline.add_clip").has_value());
    CHECK(fuzzyScore(u"IMPORT", u"Import media").has_value());
    CHECK_FALSE(fuzzyScore(u"xyz", u"Import media").has_value());
    CHECK_FALSE(fuzzyScore(u"clipp", u"Add clip").has_value());
}

TEST_CASE("fuzzyScore prefers word starts and consecutive runs", "[palette]")
{
    // "im" at the start of a word beats "im" spread inside words.
    CHECK(*fuzzyScore(u"im", u"Import media") > *fuzzyScore(u"im", u"Trim clip"));
    // A run of consecutive characters beats the same characters with gaps.
    CHECK(*fuzzyScore(u"clip", u"Add clip") > *fuzzyScore(u"clip", u"Recycle bin trip"));
    // Matching from the first character beats matching later.
    CHECK(*fuzzyScore(u"up", u"Upscale clip") > *fuzzyScore(u"up", u"Clip Upscale"));
}

TEST_CASE("CommandPaletteModel lists everything by category and title for an empty query", "[palette]")
{
    CommandPaletteModel model;
    model.setSpecs(sampleSpecs());
    CHECK(model.ids() == QStringList{u"ai.run"_s, u"edit.undo"_s, u"project.import_media"_s,
                                     u"timeline.add_clip"_s, u"timeline.remove_clip"_s});
    const QModelIndex first = model.index(0);
    CHECK(model.data(first, Qt::DisplayRole) == u"Run AI operation"_s);
    CHECK(model.data(first, CommandPaletteModel::CategoryRole) == u"AI"_s);
    CHECK(model.data(model.index(1), CommandPaletteModel::ShortcutRole).toString().contains(u"Z"_s));
}

TEST_CASE("CommandPaletteModel filters and ranks by title, id and category", "[palette]")
{
    CommandPaletteModel model;
    model.setSpecs(sampleSpecs());

    model.setQuery(u"run"_s);
    CHECK(model.ids() == QStringList{u"ai.run"_s});

    model.setQuery(u"clip"_s);
    const QStringList clips = model.ids();
    CHECK(clips.size() == 2);
    CHECK_FALSE(clips.contains(u"project.import_media"_s));

    model.setQuery(u"import_media"_s);  // By id.
    CHECK(model.ids().value(0) == u"project.import_media"_s);

    model.setQuery(u"timeline remove"_s);  // By category and title.
    CHECK(model.ids().value(0) == u"timeline.remove_clip"_s);

    model.setQuery(u"zzz"_s);
    CHECK(model.rowCount() == 0);
    CHECK(model.idAt(0).isEmpty());

    model.setQuery(QString());
    CHECK(model.rowCount() == 5);
}

TEST_CASE("CommandPalette shows the bus commands and runs the current one", "[palette]")
{
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    int runs = 0;
    REQUIRE(mikserstudio::ui::test::registerTestCommand(
        bus, {.id = u"render.preview"_s, .title = u"Render Preview"_s, .category = u"Render"_s}, &runs));
    QWidget window;
    mikserstudio::ui::CommandPalette palette(bus, &window);

    palette.popup();
    CHECK(palette.visibleCommandIds().contains(u"edit.undo"_s));
    CHECK(palette.visibleCommandIds().contains(u"render.preview"_s));

    palette.setQuery(u"rndpr"_s);
    CHECK(palette.visibleCommandIds() == QStringList{u"render.preview"_s});
    CHECK(palette.currentCommandId() == u"render.preview"_s);
    palette.runCurrent();
    CHECK(runs == 1);
    CHECK_FALSE(palette.isVisible());

    // Commands registered later show up the next time the palette opens.
    REQUIRE(mikserstudio::ui::test::registerTestCommand(
        bus, {.id = u"render.final"_s, .title = u"Render Final"_s, .category = u"Render"_s}, &runs));
    palette.popup();
    palette.setQuery(u"render"_s);
    CHECK(palette.visibleCommandIds().size() == 2);
    palette.hide();
}
