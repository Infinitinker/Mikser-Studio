// The project file's JSON schema and the conversion of the project and its parts to and from JSON.
#include "ProjectJson.h"

#include "mikserstudio/core/JsonSchema.h"

#include <QJsonArray>
#include <QJsonDocument>

#include <limits>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

// Every integer has a "maximum", so reading it with toInt()/toInteger() never silently turns an
// out-of-range number into 0. MAX_INT and MAX_FRAME are replaced before parsing.
QJsonObject buildProjectSchema()
{
    QByteArray text = R"json({
        "type": "object",
        "required": ["version", "frameRate", "width", "height", "media", "tracks", "clips"],
        "properties": {
            "version": {"type": "integer", "minimum": 1, "maximum": MAX_INT},
            "frameRate": {
                "type": "object", "required": ["num", "den"],
                "properties": {
                    "num": {"type": "integer", "minimum": 1, "maximum": MAX_INT},
                    "den": {"type": "integer", "minimum": 1, "maximum": MAX_INT}
                }
            },
            "width": {"type": "integer", "minimum": 1, "maximum": MAX_INT},
            "height": {"type": "integer", "minimum": 1, "maximum": MAX_INT},
            "media": {
                "type": "array",
                "items": {
                    "type": "object", "required": ["id", "path", "info"],
                    "properties": {
                        "id": {"type": "string", "minLength": 1},
                        "path": {"type": "string", "minLength": 1},
                        "name": {"type": "string"},
                        "provenance": {"type": "object"},
                        "info": {
                            "type": "object", "required": ["kind", "durationFrames"],
                            "properties": {
                                "kind": {"enum": ["video", "audio", "image"]},
                                "durationFrames": {"type": "integer", "minimum": 0, "maximum": MAX_FRAME},
                                "width": {"type": "integer", "minimum": 0, "maximum": MAX_INT},
                                "height": {"type": "integer", "minimum": 0, "maximum": MAX_INT},
                                "frameRate": {
                                    "type": "object", "required": ["num", "den"],
                                    "properties": {
                                        "num": {"type": "integer", "minimum": 0, "maximum": MAX_INT},
                                        "den": {"type": "integer", "minimum": 0, "maximum": MAX_INT}
                                    }
                                },
                                "hasVideo": {"type": "boolean"},
                                "hasAudio": {"type": "boolean"},
                                "audioSampleRate": {"type": "integer", "minimum": 0, "maximum": MAX_INT},
                                "audioChannels": {"type": "integer", "minimum": 0, "maximum": MAX_INT}
                            }
                        }
                    }
                }
            },
            "tracks": {
                "type": "array",
                "items": {
                    "type": "object", "required": ["id", "kind"],
                    "properties": {
                        "id": {"type": "string", "minLength": 1},
                        "kind": {"enum": ["video", "audio"]},
                        "name": {"type": "string"},
                        "muted": {"type": "boolean"},
                        "hidden": {"type": "boolean"}
                    }
                }
            },
            "clips": {
                "type": "array",
                "items": {
                    "type": "object", "required": ["id", "mediaId", "trackId", "position", "in", "out"],
                    "properties": {
                        "id": {"type": "string", "minLength": 1},
                        "mediaId": {"type": "string"},
                        "trackId": {"type": "string"},
                        "position": {"type": "integer", "minimum": 0, "maximum": MAX_FRAME},
                        "in": {"type": "integer", "minimum": 0, "maximum": MAX_FRAME},
                        "out": {"type": "integer", "minimum": 0, "maximum": MAX_FRAME}
                    }
                }
            }
        }
    })json";
    text.replace("MAX_INT", QByteArray::number(std::numeric_limits<int>::max()));
    text.replace("MAX_FRAME", QByteArray::number(kMaxFrame));
    return QJsonDocument::fromJson(text).object();
}

const QJsonObject& projectSchema()
{
    static const QJsonObject schema = buildProjectSchema();
    return schema;
}

Rational rationalFromJson(const QJsonObject& json)
{
    return {json.value(u"num").toInt(), json.value(u"den").toInt()};
}

MediaKind mediaKindFromString(const QString& text)
{
    if (text == u"audio")
        return MediaKind::Audio;
    if (text == u"image")
        return MediaKind::Image;
    return MediaKind::Video;
}

MediaInfo mediaInfoFromJson(const QJsonObject& json)
{
    MediaInfo info;
    info.kind = mediaKindFromString(json.value(u"kind").toString());
    info.durationFrames = json.value(u"durationFrames").toInteger();
    info.width = json.value(u"width").toInt();
    info.height = json.value(u"height").toInt();
    if (json.contains(u"frameRate"))
        info.frameRate = rationalFromJson(json.value(u"frameRate").toObject());
    info.hasVideo = json.value(u"hasVideo").toBool();
    info.hasAudio = json.value(u"hasAudio").toBool();
    info.audioSampleRate = json.value(u"audioSampleRate").toInt();
    info.audioChannels = json.value(u"audioChannels").toInt();
    return info;
}

QJsonObject mediaInfoToJson(const MediaInfo& info)
{
    return QJsonObject{
        {u"kind"_s, toString(info.kind)},
        {u"durationFrames"_s, info.durationFrames},
        {u"width"_s, info.width},
        {u"height"_s, info.height},
        {u"frameRate"_s, rationalToJson(info.frameRate)},
        {u"hasVideo"_s, info.hasVideo},
        {u"hasAudio"_s, info.hasAudio},
        {u"audioSampleRate"_s, info.audioSampleRate},
        {u"audioChannels"_s, info.audioChannels},
    };
}

}  // namespace

QJsonObject rationalToJson(Rational rate)
{
    return QJsonObject{{u"num"_s, rate.num}, {u"den"_s, rate.den}};
}

QJsonObject mediaToJson(const MediaItem& item)
{
    return QJsonObject{
        {u"id"_s, item.id},
        {u"path"_s, item.path},
        {u"name"_s, item.name},
        {u"info"_s, mediaInfoToJson(item.info)},
        {u"provenance"_s, item.provenance},
    };
}

QJsonObject trackToJson(const Track& track)
{
    return QJsonObject{
        {u"id"_s, track.id},       {u"kind"_s, toString(track.kind)}, {u"name"_s, track.name},
        {u"muted"_s, track.muted}, {u"hidden"_s, track.hidden},
    };
}

QJsonObject clipToJson(const Clip& clip)
{
    return QJsonObject{
        {u"id"_s, clip.id}, {u"mediaId"_s, clip.mediaId}, {u"trackId"_s, clip.trackId}, {u"position"_s, clip.position},
        {u"in"_s, clip.in}, {u"out"_s, clip.out},
    };
}

std::expected<ProjectDocument, QString> projectFromJson(const QJsonObject& json)
{
    if (const QStringList problems = JsonSchema::validate(json, projectSchema()); !problems.isEmpty())
        return std::unexpected(u"The project file is damaged: %1"_s.arg(problems.join(u"; "_s)));

    ProjectDocument document;
    document.frameRate = rationalFromJson(json.value(u"frameRate").toObject());
    document.width = json.value(u"width").toInt();
    document.height = json.value(u"height").toInt();
    for (const QJsonValue value : json.value(u"media").toArray()) {
        const QJsonObject object = value.toObject();
        const QString path = object.value(u"path").toString();
        if (!isAllowedMediaPath(path)) {
            return std::unexpected(
                u"The project file is damaged: media \"%1\" has the path \"%2\", which is not an absolute path to a "
                "file on this computer. Mikser Studio only opens local media files from projects; import the file again."_s
                    .arg(object.value(u"id").toString(), path));
        }
        document.media.append(MediaItem{
            .id = object.value(u"id").toString(),
            .path = path,
            .name = object.value(u"name").toString(),
            .info = mediaInfoFromJson(object.value(u"info").toObject()),
            .provenance = object.value(u"provenance").toObject(),
        });
    }
    for (const QJsonValue value : json.value(u"tracks").toArray()) {
        const QJsonObject object = value.toObject();
        document.tracks.append(Track{
            .id = object.value(u"id").toString(),
            .kind = object.value(u"kind").toString() == u"audio" ? TrackKind::Audio : TrackKind::Video,
            .name = object.value(u"name").toString(),
            .muted = object.value(u"muted").toBool(),
            .hidden = object.value(u"hidden").toBool(),
        });
    }
    for (const QJsonValue value : json.value(u"clips").toArray()) {
        const QJsonObject object = value.toObject();
        document.clips.append(Clip{
            .id = object.value(u"id").toString(),
            .mediaId = object.value(u"mediaId").toString(),
            .trackId = object.value(u"trackId").toString(),
            .position = object.value(u"position").toInteger(),
            .in = object.value(u"in").toInteger(),
            .out = object.value(u"out").toInteger(),
        });
    }
    return document;
}

}  // namespace mikserstudio::core
