// The project file commands: the .msproj extension, refused folders, asking before another file is
// replaced or unsaved changes are lost, and the project's own file and clean state after a save.
#include "ProjectFileCommands.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTemporaryDir>

using namespace Qt::StringLiterals;
using mikserstudio::app::projectFilePath;
using mikserstudio::core::CommandErrorCode;

namespace {

// A change the person made, as far as the undo history can tell.
class SomeEdit final : public mikserstudio::core::UndoableEdit {
public:
    void undo() override { }
    void redo() override { }
    [[nodiscard]] QString text() const override { return u"Some edit"_s; }
};

struct Fixture {
    QTemporaryDir folder;
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus{undo};
    mikserstudio::core::ProjectModel project;

    Fixture() { mikserstudio::app::registerProjectFileCommands(bus, project); }

    [[nodiscard]] QString path(const QString& name) const { return folder.filePath(name); }
    void change() { undo.push(std::make_unique<SomeEdit>()); }
    auto run(const QString& id, const QJsonObject& params = {}) { return bus.execute(id, params); }
};

CommandErrorCode codeOf(const std::expected<QJsonObject, mikserstudio::core::CommandError>& result)
{
    REQUIRE_FALSE(result.has_value());
    return result.error().code;
}

}  // namespace

TEST_CASE("A project file path gets .msproj unless it already ends with it", "[project-file]")
{
    CHECK(projectFilePath(u"D:/p/first"_s) == u"D:/p/first.msproj"_s);
    CHECK(projectFilePath(u"D:/p/Cut v1.2"_s) == u"D:/p/Cut v1.2.msproj"_s);
    CHECK(projectFilePath(u"D:/p/notes.txt"_s) == u"D:/p/notes.txt.msproj"_s);
    CHECK(projectFilePath(u"D:/p/first.msproj"_s) == u"D:/p/first.msproj"_s);
    CHECK(projectFilePath(u"D:/p/FIRST.MSPROJ"_s) == u"D:/p/FIRST.MSPROJ"_s);
}

TEST_CASE("Saving a dotted name keeps the extension, and the file becomes the project's own", "[project-file]")
{
    Fixture f;
    REQUIRE(f.folder.isValid());
    const auto saved = f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"Cut v1.2"_s)}});
    REQUIRE(saved.has_value());
    const QString file = f.path(u"Cut v1.2.msproj"_s);
    CHECK(QFile::exists(file));
    CHECK(QFileInfo(saved->value(u"path"_s).toString()) == QFileInfo(file));
    CHECK(QFileInfo(f.project.filePath()) == QFileInfo(file));
    CHECK(f.undo.isClean());
}

TEST_CASE("Saving refuses a folder and a path that ends with a slash", "[project-file]")
{
    Fixture f;
    REQUIRE(QDir(f.folder.path()).mkdir(u"cuts"_s));
    const auto folder = f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"cuts"_s)}});
    CHECK(codeOf(folder) == CommandErrorCode::Failed);
    CHECK(folder.error().message.contains(u"is a folder"_s));
    const auto slash = f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"new/"_s)}});
    CHECK(codeOf(slash) == CommandErrorCode::Failed);
    CHECK_FALSE(QFile::exists(f.path(u"cuts.msproj"_s)));
    CHECK_FALSE(QFile::exists(f.path(u"cuts/.msproj"_s)));
    CHECK_FALSE(QFile::exists(f.path(u"new/.msproj"_s)));
    CHECK(f.project.filePath().isEmpty());
}

TEST_CASE("Saving asks before replacing another file, never before saving to its own", "[project-file]")
{
    Fixture f;
    REQUIRE(f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"first"_s)}}).has_value());
    REQUIRE(f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"second"_s)}}).has_value());

    f.change();
    CHECK(f.run(u"project.save"_s).has_value());  // Its own file, now second.msproj.
    CHECK(f.undo.isClean());
    CHECK(f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"second.msproj"_s)}}).has_value());

    const auto asked = f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"first"_s)}});
    CHECK(codeOf(asked) == CommandErrorCode::NeedsConfirmation);
    CHECK(asked.error().message.contains(u"already exists"_s));
    CHECK(f.run(u"project.save"_s, {{u"path"_s, f.path(u"first"_s)}, {u"overwrite"_s, true}}).has_value());
    CHECK(QFileInfo(f.project.filePath()) == QFileInfo(f.path(u"first.msproj"_s)));
}

TEST_CASE("Save without a file needs a path", "[project-file]")
{
    Fixture f;
    const auto saved = f.run(u"project.save"_s);
    CHECK(codeOf(saved) == CommandErrorCode::InvalidParams);
    CHECK(saved.error().message.contains(u"no file yet"_s));
}

TEST_CASE("Opening finds the file without its extension, as saving names it", "[project-file]")
{
    Fixture f;
    REQUIRE(f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"first"_s)}}).has_value());
    REQUIRE(f.run(u"project.new"_s).has_value());
    CHECK(f.project.filePath().isEmpty());

    REQUIRE(f.run(u"project.open"_s, {{u"path"_s, f.path(u"first"_s)}}).has_value());
    CHECK(QFileInfo(f.project.filePath()) == QFileInfo(f.path(u"first.msproj"_s)));
    CHECK(f.undo.isClean());

    CHECK(codeOf(f.run(u"project.open"_s, {{u"path"_s, f.path(u"missing"_s)}})) == CommandErrorCode::Failed);
    CHECK(codeOf(f.run(u"project.open"_s, {{u"path"_s, f.folder.path()}})) == CommandErrorCode::Failed);
}

TEST_CASE("New and Open ask only when there are unsaved changes", "[project-file]")
{
    Fixture f;
    REQUIRE(f.run(u"project.save_as"_s, {{u"path"_s, f.path(u"first"_s)}}).has_value());
    CHECK(f.run(u"project.new"_s).has_value());
    CHECK(f.run(u"project.open"_s, {{u"path"_s, f.path(u"first"_s)}}).has_value());

    f.change();
    CHECK(codeOf(f.run(u"project.new"_s)) == CommandErrorCode::NeedsConfirmation);
    CHECK(codeOf(f.run(u"project.open"_s, {{u"path"_s, f.path(u"first"_s)}})) == CommandErrorCode::NeedsConfirmation);
    CHECK_FALSE(f.undo.isClean());

    REQUIRE(f.run(u"project.new"_s, {{u"discard"_s, true}}).has_value());
    CHECK(f.undo.isClean());
    CHECK_FALSE(f.undo.canUndo());
}
