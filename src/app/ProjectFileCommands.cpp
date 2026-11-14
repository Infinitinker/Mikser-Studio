// The project file commands: starting, opening and saving .msproj files, and asking before
// unsaved changes or another file are lost.
#include "ProjectFileCommands.h"

#include "Logging.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

#include <optional>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

using core::CommandErrorCode;
using core::commandFailure;

const QString kExtension = u".msproj"_s;

QJsonObject agreementProperty(const QString& description)
{
    return QJsonObject{{u"type"_s, u"boolean"_s}, {u"default"_s, false}, {u"description"_s, description}};
}

QJsonObject pathProperty(const QString& description)
{
    return QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}, {u"description"_s, description}};
}

QJsonObject objectSchema(const QJsonObject& properties, const QJsonArray& required)
{
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, properties},
        {u"required"_s, required},
        {u"additionalProperties"_s, false},
    };
}

bool namesFolder(const QString& path)
{
    return path.endsWith(u'/') || path.endsWith(u'\\') || QFileInfo(path).isDir();
}

// True when `path` and the project's own file are the same file.
bool isOwnFile(const core::ProjectModel& project, const QString& path)
{
    return !project.filePath().isEmpty() && QFileInfo(path) == QFileInfo(project.filePath());
}

core::CommandResult saveProject(core::ProjectModel& project, core::UndoStack& undo, const QString& requested,
                                bool overwrite)
{
    QString path = project.filePath();
    if (!requested.isEmpty()) {
        if (namesFolder(requested)) {
            const QString example = QDir(requested).filePath(u"My project"_s + kExtension);
            return commandFailure(
                CommandErrorCode::Failed,
                u"“%1” is a folder. Give a file name for the project, such as “%2”."_s.arg(requested, example));
        }
        path = projectFilePath(requested);
    } else if (path.isEmpty()) {
        return commandFailure(CommandErrorCode::InvalidParams,
                              u"This project has no file yet. Give “path”, the file to save it to."_s);
    }
    if (QFileInfo(path).isDir())
        return commandFailure(CommandErrorCode::Failed,
                              u"“%1” is a folder. Give another file name for the project."_s.arg(path));
    if (!overwrite && QFileInfo::exists(path) && !isOwnFile(project, path))
        return commandFailure(CommandErrorCode::NeedsConfirmation,
                              u"“%1” already exists. Replace it with this project?"_s.arg(path));

    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return commandFailure(CommandErrorCode::Failed,
                              u"Could not write “%1”: %2. Choose another folder."_s.arg(path, file.errorString()));
    file.write(QJsonDocument(project.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return commandFailure(
            CommandErrorCode::Failed,
            u"Could not save “%1”: %2. Check the free space and permissions."_s.arg(path, file.errorString()));
    const QString saved = QFileInfo(path).absoluteFilePath();
    project.setFilePath(saved);
    undo.setClean();
    return core::CommandOutcome{QJsonObject{{u"path"_s, saved}}, nullptr};
}

// The file project.open reads for `requested`, or the error that says why there is none.
std::expected<QString, core::CommandError> fileToOpen(const QString& requested)
{
    if (namesFolder(requested))
        return std::unexpected(core::CommandError{
            CommandErrorCode::Failed, u"“%1” is a folder. Choose a .msproj project file in it."_s.arg(requested)});
    if (QFileInfo(requested).isFile())
        return requested;
    QString withExtension = projectFilePath(requested);
    if (QFileInfo(withExtension).isFile())
        return withExtension;
    return std::unexpected(core::CommandError{
        CommandErrorCode::Failed,
        u"There is no project file “%1”. Check the path, or choose a .msproj file that Mikser Studio saved."_s.arg(
            requested)});
}

core::CommandResult openProject(core::ProjectModel& project, core::UndoStack& undo, const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return commandFailure(CommandErrorCode::Failed,
                              u"Could not open “%1”: %2. Check that the file exists and that you can read it."_s.arg(
                                  path, file.errorString()));
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        qCWarning(lcApp).noquote() << "Not a project file:" << path << "-" << parseError.errorString() << "at offset"
                                   << parseError.offset;
        return commandFailure(
            CommandErrorCode::Failed,
            u"“%1” is not a Mikser Studio project, or it is damaged. Open a .msproj file that Mikser Studio saved."_s
                .arg(path));
    }
    if (const auto loaded = project.loadJson(document.object()); !loaded)
        return commandFailure(CommandErrorCode::Failed, loaded.error());
    const QString opened = QFileInfo(path).absoluteFilePath();
    project.setFilePath(opened);
    undo.clear();  // Old edits refer to the previous project.
    undo.setClean();
    return core::CommandOutcome{QJsonObject{{u"path"_s, opened}}, nullptr};
}

std::optional<core::CommandError> unsavedChanges(const core::UndoStack& undo, bool discard, const QString& question)
{
    if (discard || undo.isClean())
        return std::nullopt;
    return core::CommandError{CommandErrorCode::NeedsConfirmation, question};
}

}  // namespace

QString projectFilePath(const QString& path)
{
    return path.endsWith(kExtension, Qt::CaseInsensitive) ? path : path + kExtension;
}

void registerProjectFileCommands(core::CommandBus& bus, core::ProjectModel& project)
{
    core::UndoStack& undo = bus.undoStack();
    const QJsonObject discard = agreementProperty(u"Discard the unsaved changes. Set only after the person agreed."_s);
    const QJsonObject overwrite = agreementProperty(
        u"Replace the file when it exists. Set only after the person agreed."_s);

    bus.registerCommand(
        {.id = u"project.new"_s,
         .title = u"New project"_s,
         .description = u"Starts an empty project. The person is asked first when there are "
                        u"unsaved changes."_s,
         .category = u"File"_s,
         .paramsSchema = objectSchema(QJsonObject{{u"discard"_s, discard}}, {}),
         .shortcut = u"Ctrl+N"_s,
         .undoable = false,
         .readOnly = false,
         .confirmation = {},
         .confirmationParam = u"discard"_s},
        [&project, &undo](const QJsonObject& params) -> core::CommandResult {
            if (const auto asked = unsavedChanges(undo, params.value(u"discard"_s).toBool(),
                                                  u"Start a new project? Unsaved changes to this one will be lost."_s))
                return std::unexpected(*asked);
            project.reset(project.frameRate(), project.width(), project.height());
            undo.clear();
            undo.setClean();
            return core::CommandOutcome{};
        });

    bus.registerCommand(
        {.id = u"project.open"_s,
         .title = u"Open project…"_s,
         .description = u"Opens a .msproj project file and clears the undo history. The person is asked first "
                        u"when there are unsaved changes."_s,
         .category = u"File"_s,
         .paramsSchema = objectSchema(
             QJsonObject{{u"path"_s, pathProperty(u"The .msproj project file to open. Without an extension, .msproj is "
                                                  u"tried too."_s)},
                         {u"discard"_s, discard}},
             QJsonArray{u"path"_s}),
         .shortcut = u"Ctrl+O"_s,
         .undoable = false,
         .readOnly = false,
         .confirmation = {},
         .confirmationParam = u"discard"_s},
        [&project, &undo](const QJsonObject& params) -> core::CommandResult {
            const auto path = fileToOpen(params.value(u"path"_s).toString());
            if (!path)
                return std::unexpected(path.error());
            if (const auto asked = unsavedChanges(
                    undo, params.value(u"discard"_s).toBool(),
                    u"Open “%1”? Unsaved changes to this project will be lost."_s.arg(*path)))
                return std::unexpected(*asked);
            return openProject(project, undo, *path);
        });

    const auto save = [&project, &undo](const QJsonObject& params) {
        return saveProject(project, undo, params.value(u"path"_s).toString(), params.value(u"overwrite"_s).toBool());
    };
    bus.registerCommand(
        {.id = u"project.save"_s,
         .title = u"Save project"_s,
         .description = u"Saves the project to its own file, or to “path” when it is given. The person is "
                        u"asked before another existing file is replaced."_s,
         .category = u"File"_s,
         .paramsSchema = objectSchema(
             QJsonObject{{u"path"_s, pathProperty(u"Where to save the project instead of its own file. A path "
                                                  u"that doesn't end with .msproj gets it."_s)},
                         {u"overwrite"_s, overwrite}},
             {}),
         .shortcut = u"Ctrl+S"_s,
         .undoable = false,
         .readOnly = false,
         .confirmation = {},
         .confirmationParam = u"overwrite"_s},
        save);
    bus.registerCommand(
        {.id = u"project.save_as"_s,
         .title = u"Save project as…"_s,
         .description = u"Saves the project to a .msproj file, which becomes its own file. The person is asked "
                        u"before another existing file is replaced."_s,
         .category = u"File"_s,
         .paramsSchema = objectSchema(
             QJsonObject{{u"path"_s, pathProperty(u"Where to save the project. A path that doesn't end with "
                                                  u".msproj gets it."_s)},
                         {u"overwrite"_s, overwrite}},
             QJsonArray{u"path"_s}),
         .shortcut = u"Ctrl+Shift+S"_s,
         .undoable = false,
         .readOnly = false,
         .confirmation = {},
         .confirmationParam = u"overwrite"_s},
        save);
}

}  // namespace mikserstudio::app
