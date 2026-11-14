// Importing a finished AI job's media outputs with their provenance, as one undo step.
#include "AiResultImport.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/UndoStack.h"

#include <QDir>
#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

std::expected<QJsonObject, QString> importAiOutputs(core::CommandBus& bus, const QJsonObject& job, const QString& name)
{
    const QJsonObject result = job.value(u"result").toObject();
    const QJsonObject provenance = result.value(u"provenance").toObject();
    QStringList paths;
    for (const QJsonValue value : result.value(u"outputs").toArray()) {
        const QJsonObject output = value.toObject();
        const QString kind = output.value(u"kind").toString();
        if (kind == u"image" || kind == u"video" || kind == u"audio")
            paths.append(output.value(u"path").toString());
    }

    QJsonArray mediaIds;
    if (paths.isEmpty())
        return QJsonObject{{u"mediaIds"_s, mediaIds}};
    // Everything below is one undo step, also when it stops half-way.
    const core::UndoGroup group(bus.undoStack(), u"Add AI result"_s);
    for (qsizetype i = 0; i < paths.size(); ++i) {
        const QString mediaName = paths.size() == 1 ? name : u"%1 (%2)"_s.arg(name).arg(i + 1);
        auto imported = bus.execute(
            u"project.import_media"_s,
            QJsonObject{{u"path"_s, paths.at(i)}, {u"name"_s, mediaName}, {u"provenance"_s, provenance}});
        if (!imported) {
            return std::unexpected(u"The AI result was saved to “%1” but could not be imported: %2"_s.arg(
                QDir::toNativeSeparators(paths.at(i)), imported.error().message));
        }
        mediaIds.append(imported->value(u"mediaId"));
    }
    return QJsonObject{{u"mediaIds"_s, mediaIds}};
}

}  // namespace mikserstudio::services
