// The project.new, project.save, project.open and source.open_media commands.
#include "AppCommands.h"

#include "Logging.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/engine/ClipPlayer.h"

#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

namespace {

using core::CommandErrorCode;
using core::commandFailure;

QJsonObject pathSchema(const QString& description)
{
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s,
         QJsonObject{
             {u"path"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}, {u"description"_s, description}}}}},
        {u"required"_s, QJsonArray{u"path"_s}},
        {u"additionalProperties"_s, false},
    };
}

// The file a project is saved to: `path`, with the project extension added when it has none.
QString projectFilePath(const QString& path)
{
    return QFileInfo(path).suffix().isEmpty() ? path + u".msproj"_s : path;
}

core::CommandResult saveProject(const core::ProjectModel& project, const QString& requested)
{
    const QString path = projectFilePath(requested);
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return commandFailure(CommandErrorCode::Failed,
                              u"Could not write “%1”: %2. Choose another folder."_s.arg(path, file.errorString()));
    file.write(QJsonDocument(project.toJson()).toJson(QJsonDocument::Indented));
    if (!file.commit())
        return commandFailure(
            CommandErrorCode::Failed,
            u"Could not save “%1”: %2. Check the free space and permissions."_s.arg(path, file.errorString()));
    return core::CommandOutcome{QJsonObject{{u"path"_s, path}}, nullptr};
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
    undo.clear();  // Old edits refer to the previous project.
    return core::CommandOutcome{QJsonObject{{u"path"_s, path}}, nullptr};
}

}  // namespace

void registerAppCommands(core::CommandBus& bus, core::ProjectModel& project, engine::ClipPlayer* source)
{
    core::UndoStack& undo = bus.undoStack();

    bus.registerCommand({.id = u"project.new"_s,
                         .title = u"New project"_s,
                         .description = u"Starts an empty project. This cannot be undone."_s,
                         .category = u"File"_s,
                         .paramsSchema = {},
                         .shortcut = u"Ctrl+N"_s,
                         .undoable = false,
                         .readOnly = false,
                         .confirmation = u"Start a new project? Unsaved changes will be lost."_s},
                        [&project, &undo](const QJsonObject&) -> core::CommandResult {
                            project.reset(project.frameRate(), project.width(), project.height());
                            undo.clear();
                            return core::CommandOutcome{};
                        });
    bus.registerCommand(
        {u"project.save"_s, u"Save project as…"_s, u"Saves the project to a .msproj file."_s, u"File"_s,
         pathSchema(u"Where to save the project. A path without an extension gets .msproj."_s), u"Ctrl+Shift+S"_s,
         false},
        [&project](const QJsonObject& params) { return saveProject(project, params.value(u"path"_s).toString()); });
    bus.registerCommand({.id = u"project.open"_s,
                         .title = u"Open project…"_s,
                         .description = u"Opens a .msproj project file. The undo history is cleared."_s,
                         .category = u"File"_s,
                         .paramsSchema = pathSchema(u"The .msproj project file to open."_s),
                         .shortcut = u"Ctrl+O"_s,
                         .undoable = false,
                         .readOnly = false,
                         .confirmation = u"Open another project? Unsaved changes to this one will be lost."_s},
                        [&project, &undo](const QJsonObject& params) {
                            return openProject(project, undo, params.value(u"path"_s).toString());
                        });

    if (!source)
        return;
    const QJsonObject mediaSchema{
        {u"type"_s, u"object"_s},
        {u"properties"_s, QJsonObject{{u"mediaId"_s, QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}}}}},
        {u"required"_s, QJsonArray{u"mediaId"_s}},
        {u"additionalProperties"_s, false},
    };
    bus.registerCommand({u"source.open_media"_s,
                         u"Show in source monitor"_s,
                         u"Loads a media item from the bin into the source monitor."_s,
                         u"Source monitor"_s,
                         mediaSchema,
                         {},
                         false},
                        [&project, source](const QJsonObject& params) -> core::CommandResult {
                            const QString id = params.value(u"mediaId"_s).toString();
                            const core::MediaItem* item = project.findMedia(id);
                            if (!item)
                                return commandFailure(CommandErrorCode::Failed,
                                                      u"There is no media item “%1” in the project."_s.arg(id));
                            if (const auto opened = source->openFile(item->path); !opened)
                                return commandFailure(CommandErrorCode::Failed, opened.error());
                            return core::CommandOutcome{QJsonObject{{u"mediaId"_s, id}}, nullptr};
                        });
}

}  // namespace mikserstudio::app
