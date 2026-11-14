// The project: the media bin and timeline model, the media probe interface and the rules that keep them
// consistent.
#pragma once

#include <QJsonObject>
#include <QList>
#include <QObject>
#include <QString>

#include <cstdint>
#include <expected>
#include <optional>

namespace mikserstudio::core {

// Frames are the unit of time on the timeline, at the project's frame rate.
using FramePos = std::int64_t;

// Largest frame value anywhere in a project (positions, in/out points, durations, clip ends):
// about 1160 years at 30 fps. Every frame-valued schema uses it as "maximum" (and step sizes as
// -kMaxFrame..kMaxFrame), so frame arithmetic such as Clip::end() can never overflow.
inline constexpr FramePos kMaxFrame = FramePos{1} << 40;

struct Rational {
    int num = 30;
    int den = 1;
    [[nodiscard]] double toDouble() const { return den == 0 ? 0.0 : static_cast<double>(num) / den; }
    friend bool operator==(const Rational&, const Rational&) = default;
};

enum class MediaKind { Video, Audio, Image };

// What a media probe found out about a file.
struct MediaInfo {
    MediaKind kind = MediaKind::Video;
    FramePos durationFrames = 0;  // In the project's frame rate.
    int width = 0;
    int height = 0;
    Rational frameRate;
    bool hasVideo = false;
    bool hasAudio = false;
    int audioSampleRate = 0;
    int audioChannels = 0;
};

// Implemented by the engine (MLT); injected into the core so the core does not depend on it.
class MediaProbe {
public:
    virtual ~MediaProbe() = default;
    // `projectFrameRate` is the rate durationFrames must be expressed in.
    [[nodiscard]] virtual std::expected<MediaInfo, QString> probe(const QString& path, Rational projectFrameRate) = 0;
};

struct MediaItem {
    QString id;  // Stable unique id ("media-<uuid>").
    QString path;  // Absolute path, '/' separators (see isAllowedMediaPath()).
    QString name;  // Display name, defaults to the file name.
    MediaInfo info;
    // Provenance of generated media (AI jobs): workflow, seed, model hash. Empty for imports.
    QJsonObject provenance;
};

enum class TrackKind { Video, Audio };

struct Track {
    QString id;  // "V1", "V2", "A1", ...
    TrackKind kind = TrackKind::Video;
    QString name;
    bool muted = false;
    bool hidden = false;
};

struct Clip {
    QString id;  // "clip-<uuid>".
    QString mediaId;
    QString trackId;
    FramePos position = 0;  // Timeline frame where the clip starts.
    FramePos in = 0;  // First source frame used (inclusive).
    FramePos out = 0;  // Source frame after the last one used (exclusive).
    [[nodiscard]] FramePos length() const { return out - in; }
    [[nodiscard]] FramePos end() const { return position + length(); }
};

// The document: media bin plus the timeline. Owned by the main thread.
//
// Placement rules: a clip needs existing media and track, position >= 0 and 0 <= in < out; out
// may not exceed the media duration (except for still images, which can be any length); out and
// end() may not exceed kMaxFrame; video tracks take video and images, audio tracks take media
// with audio; clips on one track never overlap ([position, end) ranges are disjoint).
//
// Track order: video tracks first, bottom-up (V1 under V2, so a later video track is drawn on
// top), then audio tracks.
//
// Mutating methods validate their input and return an English error message on failure; they
// never leave the model half-changed. They do not record undo history: commands do that
// (see ProjectCommands.h), so scripts, the API and the UI all get undo through the CommandBus.
class ProjectModel final : public QObject {
    Q_OBJECT

public:
    static constexpr int kFormatVersion = 1;

    explicit ProjectModel(QObject* parent = nullptr);
    ~ProjectModel() override;

    [[nodiscard]] Rational frameRate() const { return m_frameRate; }
    [[nodiscard]] int width() const { return m_width; }
    [[nodiscard]] int height() const { return m_height; }

    [[nodiscard]] const QList<MediaItem>& media() const { return m_media; }
    [[nodiscard]] const QList<Track>& tracks() const { return m_tracks; }
    [[nodiscard]] const QList<Clip>& clips() const { return m_clips; }  // Sorted by track, position.

    [[nodiscard]] const MediaItem* findMedia(const QString& id) const;
    [[nodiscard]] const Track* findTrack(const QString& id) const;
    [[nodiscard]] const Clip* findClip(const QString& id) const;
    [[nodiscard]] FramePos duration() const;  // End of the last clip.

    // Item id must be set and unique; an empty name defaults to the file name. `index` is the
    // position in the media bin (-1 appends), so undo can restore a removed item where it was.
    std::expected<void, QString> addMedia(MediaItem item, qsizetype index = -1);
    std::expected<MediaItem, QString> removeMedia(const QString& id);  // Fails if clips use it.

    std::expected<void, QString> addClip(Clip clip);  // Fails on overlap or bad references.
    std::expected<Clip, QString> removeClip(const QString& id);
    std::expected<void, QString> moveClip(const QString& id, const QString& trackId, FramePos position);
    std::expected<void, QString> trimClip(const QString& id, FramePos in, FramePos out);  // Keeps position.

    // Adds an empty track with the next free number ("V3", "A3") and returns its id. A video
    // track goes after the last video track (it becomes the top one), an audio track at the end.
    std::expected<QString, QString> addTrack(TrackKind kind);
    // Removes a track that holds no clips (used to undo addTrack).
    std::expected<Track, QString> removeTrack(const QString& id);
    std::expected<void, QString> setTrackFlags(const QString& id, bool muted, bool hidden);

    // The project's own file: the one it was opened from or last saved to, as an absolute path;
    // empty for a project that has never been saved. reset() empties it; loadJson() leaves it to
    // the caller, which knows where the project came from.
    [[nodiscard]] const QString& filePath() const { return m_filePath; }
    void setFilePath(const QString& path);

    // Creates a new empty project with tracks V1, V2, A1, A2, and no file.
    void reset(Rational frameRate = {30, 1}, int width = 1920, int height = 1080);

    [[nodiscard]] QJsonObject toJson() const;
    // Migrates older versions (see "version"), validates everything and only then replaces the
    // current project; on failure the current project is unchanged.
    std::expected<void, QString> loadJson(const QJsonObject& json);

    [[nodiscard]] static QString newId(const QString& prefix);  // "<prefix>-<uuid>"

Q_SIGNALS:
    void mediaAdded(const QString& id);
    void mediaRemoved(const QString& id);
    void clipAdded(const QString& id);
    void clipRemoved(const QString& id);
    void clipChanged(const QString& id);
    void trackAdded(const QString& id);
    void trackRemoved(const QString& id);
    void trackChanged(const QString& id);  // Muted or hidden changed.
    void projectReset();  // Everything changed (new project or load).
    void timelineChanged();  // Any clip/track change; coalesce rebuilds on this.
    void filePathChanged(const QString& path);

private:
    [[nodiscard]] std::optional<QString> checkPlacement(const Clip& clip, const QString& ignoreClipId) const;
    void sortClips();

    Rational m_frameRate{30, 1};
    int m_width = 1920;
    int m_height = 1080;
    QList<MediaItem> m_media;
    QList<Track> m_tracks;
    QList<Clip> m_clips;
    QString m_filePath;
};

// True for an absolute path to a file on this computer (not a URL or a network share such as
// "//server/share") and for MLT color resources ("color:#ff0000", "color:red"). Project files
// and imports accept only these, so a shared project cannot make Mikser Studio open other resources.
[[nodiscard]] bool isAllowedMediaPath(const QString& path);

[[nodiscard]] QString toString(MediaKind kind);
[[nodiscard]] QString toString(TrackKind kind);

}  // namespace mikserstudio::core
