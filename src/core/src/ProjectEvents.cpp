// Publishing the project's changes on the event bus, each with the id of what changed.
#include "ProjectCommandsPrivate.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

// Publishes `topic` with {<key>: id} whenever `signal` reports a change to the item with that id.
template <typename Signal>
void publishIds(CommandBus& bus, const ProjectModel& project, Signal signal, const QString& topic, const QString& key)
{
    QObject::connect(&project, signal, &bus,
                     [&bus, topic, key](const QString& id) { bus.publish(topic, QJsonObject{{key, id}}); });
}

}  // namespace

void publishProjectEvents(CommandBus& bus, const ProjectModel& project)
{
    publishIds(bus, project, &ProjectModel::mediaAdded, u"media.added"_s, u"mediaId"_s);
    publishIds(bus, project, &ProjectModel::mediaRemoved, u"media.removed"_s, u"mediaId"_s);
    publishIds(bus, project, &ProjectModel::clipAdded, u"clip.added"_s, u"clipId"_s);
    publishIds(bus, project, &ProjectModel::clipRemoved, u"clip.removed"_s, u"clipId"_s);
    publishIds(bus, project, &ProjectModel::clipChanged, u"clip.changed"_s, u"clipId"_s);
    publishIds(bus, project, &ProjectModel::trackAdded, u"track.added"_s, u"trackId"_s);
    publishIds(bus, project, &ProjectModel::trackRemoved, u"track.removed"_s, u"trackId"_s);
    publishIds(bus, project, &ProjectModel::trackChanged, u"track.changed"_s, u"trackId"_s);
    QObject::connect(&project, &ProjectModel::projectReset, &bus, [&bus] { bus.publish(u"project.reset"_s, {}); });
    QObject::connect(&project, &ProjectModel::filePathChanged, &bus, [&bus](const QString& path) {
        bus.publish(u"project.file_changed"_s, QJsonObject{{u"path"_s, path}});
    });
}

}  // namespace mikserstudio::core
