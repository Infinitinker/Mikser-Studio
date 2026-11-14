// Reading a project snapshot: finding items by id, the clips in a range or of a media item, and its
// JSON.
#include "mikserstudio/core/ProjectSnapshot.h"

#include "ProjectJson.h"

#include <QJsonValue>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

template <typename Item> const Item* findById(const QList<Item>& items, const QString& id)
{
    const auto it = std::ranges::find(items, id, &Item::id);
    return it == items.end() ? nullptr : &*it;
}

}  // namespace

const MediaItem* ProjectSnapshot::findMedia(const QString& id) const
{
    return findById(media, id);
}

const Track* ProjectSnapshot::findTrack(const QString& id) const
{
    return findById(tracks, id);
}

const Clip* ProjectSnapshot::findClip(const QString& id) const
{
    return findById(clips, id);
}

QList<Clip> ProjectSnapshot::clipsInRange(FramePos from, FramePos to, const QString& trackId) const
{
    QList<Clip> found;
    for (const Clip& clip : clips) {
        if ((trackId.isEmpty() || clip.trackId == trackId) && clip.position < to && clip.end() > from)
            found.append(clip);
    }
    return found;
}

QList<Clip> ProjectSnapshot::clipsOfMedia(const QString& mediaId) const
{
    QList<Clip> found;
    std::ranges::copy_if(clips, std::back_inserter(found),
                         [&mediaId](const Clip& clip) { return clip.mediaId == mediaId; });
    return found;
}

QJsonObject ProjectSnapshot::toJson() const
{
    QJsonObject json = projectToJson(frameRate, width, height, media, tracks, clips);
    json.insert(u"session"_s, session);
    json.insert(u"revision"_s, static_cast<qint64>(revision));
    return json;
}

}  // namespace mikserstudio::core
