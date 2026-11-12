#include "MltResource.h"

#include "mikserstudio/core/Project.h"

#include <Mlt.h>

#include <QDir>
#include <QFileInfo>
#include <QStringList>

#include <limits>
#include <string_view>

using namespace Qt::StringLiterals;

namespace mikserstudio::engine {

namespace {

// avformat marks single pictures as endless (length INT_MAX). It only learns that the source is a
// still once it decoded it, so until then a seek far from frame 0 yields an empty frame.
bool isAvformatStill(Mlt::Producer& producer)
{
    const char* service = producer.get("mlt_service");
    return service && std::string_view(service).starts_with("avformat")
           && producer.get_length() == std::numeric_limits<int>::max();
}

void primeStill(Mlt::Producer& producer)
{
    producer.seek(0);
    const std::unique_ptr<Mlt::Frame> frame(producer.get_frame());
    if (!frame)
        return;
    mlt_image_format format = mlt_image_rgba;
    int width = 0;
    int height = 0;
    frame->get_image(format, width, height);
    producer.seek(0);
}

// Extensions MLT's loader (share/mlt-7/core/loader.dict) hands to document producers (xml,
// melt_file, glaxnimate, webvfx, kdenlivetitle). Those documents name further resources, URLs and
// devices included, which MLT then opens; they are projects, not media.
bool isMltDocument(const QFileInfo& file)
{
    static const QStringList documents{
        u"mlt"_s,  u"westley"_s, u"kdenlive"_s, u"graphics"_s, u"jfx"_s, u"jef"_s,  u"kino"_s,
        u"story"_s, u"xml"_s,    u"melt"_s,     u"inigo"_s,    u"htm"_s, u"html"_s, u"qml"_s,
        u"aep"_s,  u"avd"_s,     u"json"_s,     u"lot"_s,      u"lottie"_s, u"rawr"_s, u"riv"_s,
        u"tgs"_s,  u"kdenlivetitle"_s,
    };
    return documents.contains(file.suffix(), Qt::CaseInsensitive);
}

std::expected<QString, QString> localFileResource(const QString& path)
{
    const QString shown = QDir::toNativeSeparators(path);
    // Checked before QFileInfo touches the path: a network share would make Windows contact it.
    if (!core::isAllowedMediaPath(path))
        return std::unexpected(
            u"\"%1\" is not a file on this computer. Mikser Studio plays only local media files (no web addresses, "
            u"network shares or MLT services); copy the file to a local or mapped drive and import it again."_s
                .arg(shown));
    const QFileInfo file(path);
    if (!file.exists())
        return std::unexpected(
            u"The file \"%1\" does not exist. It may have been moved, renamed or deleted."_s.arg(shown));
    if (!file.isFile())
        return std::unexpected(u"\"%1\" is a folder, not a media file."_s.arg(shown));
    if (isMltDocument(file))
        return std::unexpected(
            u"\"%1\" is a project or document file, not a media file. Import its video, audio or image "
            u"files instead."_s.arg(shown));
    return QDir::fromNativeSeparators(file.absoluteFilePath());
}

}  // namespace

bool isColorResource(const QString& path)
{
    return path.startsWith(u"color:"_s) && core::isAllowedMediaPath(path);
}

std::expected<std::unique_ptr<Mlt::Producer>, QString> openProducer(Mlt::Profile& profile, const QString& path)
{
    const auto resource = isColorResource(path) ? std::expected<QString, QString>(path) : localFileResource(path);
    if (!resource)
        return std::unexpected(resource.error());

    auto producer = std::make_unique<Mlt::Producer>(profile, resource->toUtf8().constData());
    if (!producer->is_valid())
        return std::unexpected(
            u"\"%1\" could not be opened. The file may be damaged or in a format Mikser Studio cannot read."_s.arg(
                QDir::toNativeSeparators(path)));
    if (isAvformatStill(*producer))
        primeStill(*producer);
    return producer;
}

}  // namespace mikserstudio::engine
