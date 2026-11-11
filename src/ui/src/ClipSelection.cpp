#include "mikserstudio/ui/ClipSelection.h"

#include "mikserstudio/core/Project.h"

namespace mikserstudio::ui {

ClipSelection::ClipSelection(const core::ProjectModel& project, QObject* parent)
    : QObject(parent)
    , m_project(project)
{
    connect(&project, &core::ProjectModel::clipRemoved, this, [this](const QString& id) {
        QStringList ids = m_clipIds;
        if (ids.removeAll(id) > 0)
            setClipIds(ids);
    });
    connect(&project, &core::ProjectModel::projectReset, this, &ClipSelection::dropMissing);
}

void ClipSelection::select(const QString& clipId)
{
    setClipIds(m_project.findClip(clipId) ? QStringList{clipId} : QStringList{});
}

void ClipSelection::toggle(const QString& clipId)
{
    QStringList ids = m_clipIds;
    if (ids.contains(clipId))
        ids.removeAll(clipId);
    else if (m_project.findClip(clipId))
        ids.append(clipId);
    setClipIds(ids);
}

void ClipSelection::clear()
{
    setClipIds({});
}

void ClipSelection::setClipIds(QStringList ids)
{
    if (ids == m_clipIds)
        return;
    m_clipIds = std::move(ids);
    Q_EMIT changed();
}

void ClipSelection::dropMissing()
{
    QStringList ids;
    for (const QString& id : std::as_const(m_clipIds)) {
        if (m_project.findClip(id))
            ids.append(id);
    }
    setClipIds(ids);
}

}  // namespace mikserstudio::ui
