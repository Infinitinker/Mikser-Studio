// The media bin commands: project.import_media and project.remove_media, with their undo steps.
#include "Logging.h"
#include "ProjectCommandsPrivate.h"
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"

#include <QDir>
#include <QFileInfo>

#include <algorithm>
#include <cmath>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

constexpr double kImageSeconds = 5.0;

FramePos imageDuration(Rational frameRate)
{
    return std::max<FramePos>(1, std::llround(kImageSeconds * frameRate.toDouble()));
}

qsizetype mediaIndex(const ProjectModel& project, const QString& id)
{
    const auto& media = project.media();
    return std::ranges::find(media, id, &MediaItem::id) - media.begin();
}

CommandResult importMedia(ProjectModel& project, MediaProbe& probe, const QJsonObject& params)
{
    const QFileInfo file(params.value(u"path").toString());
    const QString path = QDir::fromNativeSeparators(file.absoluteFilePath());
    if (!isAllowedMediaPath(path)) {  // Checked before touching the file, so no share is contacted.
        return commandFailure(CommandErrorCode::Failed,
                              u"\"%1\" is on a network share. Copy it to a folder on this computer (or map the share "
                              "to a drive letter) and import it from there."_s.arg(file.filePath()));
    }
    if (!file.exists())
        return commandFailure(CommandErrorCode::Failed,
                              u"The file “%1” does not exist. Check the path and try again."_s.arg(file.filePath()));
    if (!file.isFile())
        return commandFailure(CommandErrorCode::Failed,
                              u"“%1” is a folder; import the media files inside it instead."_s.arg(file.filePath()));

    auto info = probe.probe(path, project.frameRate());
    if (!info)
        return commandFailure(CommandErrorCode::Failed, u"Cannot import “%1”: %2"_s.arg(file.fileName(), info.error()));
    if (info->kind == MediaKind::Image)
        info->durationFrames = imageDuration(project.frameRate());

    MediaItem item{
        .id = ProjectModel::newId(u"media"_s),
        .path = path,
        .name = params.value(u"name").toString(file.fileName()),
        .info = *info,
        .provenance = params.value(u"provenance").toObject(),
    };
    const qsizetype index = project.media().size();
    if (auto added = project.addMedia(item, index); !added)
        return commandFailure(CommandErrorCode::Failed, added.error());

    const QString id = item.id;
    qCInfo(lcProject).noquote() << "Imported" << path << "as" << id;
    return CommandOutcome{
        QJsonObject{{u"mediaId"_s, id}},
        makeEdit(
            u"Import “%1”"_s.arg(item.name),
            [&project, id] -> std::expected<void, QString> {
                auto removed = project.removeMedia(id);
                return removed ? std::expected<void, QString>{} : std::unexpected(removed.error());
            },
            [&project, item, index] { return project.addMedia(item, index); }),
    };
}

CommandResult removeMedia(ProjectModel& project, const QJsonObject& params)
{
    const QString id = params.value(u"mediaId").toString();
    const qsizetype index = mediaIndex(project, id);
    auto removed = project.removeMedia(id);
    if (!removed)
        return commandFailure(CommandErrorCode::Failed, removed.error());

    const MediaItem& item = *removed;
    return CommandOutcome{
        QJsonObject{},
        makeEdit(
            u"Remove “%1”"_s.arg(item.name), [&project, item, index] { return project.addMedia(item, index); },
            [&project, id] -> std::expected<void, QString> {
                auto again = project.removeMedia(id);
                return again ? std::expected<void, QString>{} : std::unexpected(again.error());
            }),
    };
}

}  // namespace

void registerMediaCommands(CommandBus& bus, ProjectModel& project, MediaProbe& probe)
{
    bus.registerCommand(
        CommandSpec{
            .id = u"project.import_media"_s,
            .title = u"Import media"_s,
            .description = u"Adds a video, audio or image file to the media bin."_s,
            .category = u"Project"_s,
            .paramsSchema = objectSchema(
                QJsonObject{
                    {u"path"_s, idParam(u"Path of the file to import."_s)},
                    {u"name"_s, QJsonObject{{u"type"_s, u"string"_s},
                                            {u"description"_s, u"Display name; defaults to the file name."_s}}},
                    {u"provenance"_s,
                     QJsonObject{
                         {u"type"_s, u"object"_s},
                         {u"description"_s,
                          u"How generated media was made (workflow, seed, model hash); stored with the item."_s}}},
                },
                {u"path"_s}),
            .shortcut = u"Ctrl+I"_s,
            .undoable = true,
        },
        [&project, &probe](const QJsonObject& params) { return importMedia(project, probe, params); });

    bus.registerCommand(
        CommandSpec{
            .id = u"project.remove_media"_s,
            .title = u"Remove media"_s,
            .description = u"Removes an unused item from the media bin."_s,
            .category = u"Project"_s,
            .paramsSchema = objectSchema(QJsonObject{{u"mediaId"_s, idParam(u"Id of the media item."_s)}},
                                         {u"mediaId"_s}),
            .shortcut = {},
            .undoable = true,
        },
        [&project](const QJsonObject& params) { return removeMedia(project, params); });

    bus.registerCommand(
        CommandSpec{
            .id = u"project.list"_s,
            .title = u"Describe project"_s,
            .description = u"Returns the whole project (media, tracks and clips) as JSON."_s,
            .category = u"Project"_s,
            .paramsSchema = {},
            .shortcut = {},
            .undoable = false,
            .readOnly = true,
            .confirmation = {},
        },
        [&project](const QJsonObject&) -> CommandResult { return CommandOutcome{project.toJson(), nullptr}; });
}

}  // namespace mikserstudio::core
