#include "AiJson.h"

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

std::expected<AiOperation, QString> parseOperation(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    const QString id = object.value(u"id").toString();
    if (id.isEmpty())
        return std::unexpected(u"an operation has no \"id\""_s);
    return AiOperation{
        .id = id,
        .title = object.value(u"title").toString(id),
        .description = object.value(u"description").toString(),
        .paramsSchema = object.value(u"paramsSchema").toObject(),
    };
}

std::expected<AiBackendInfo, QString> parseBackend(const QJsonValue& value)
{
    const QJsonObject object = value.toObject();
    AiBackendInfo backend{
        .id = object.value(u"id").toString(),
        .title = object.value(u"title").toString(),
        .operations = {},
        .available = object.value(u"available").toBool(),
        .reason = object.value(u"reason").toString(),
    };
    if (backend.id.isEmpty())
        return std::unexpected(u"a backend has no \"id\""_s);
    for (const QJsonValue op : object.value(u"ops").toArray()) {
        auto operation = parseOperation(op);
        if (!operation)
            return std::unexpected(u"backend \"%1\": %2"_s.arg(backend.id, operation.error()));
        backend.operations.append(*operation);
    }
    return backend;
}

}  // namespace

std::expected<QList<AiBackendInfo>, QString> parseBackends(const QJsonDocument& document)
{
    if (!document.isArray())
        return std::unexpected(u"the backend list is not a JSON array"_s);
    QList<AiBackendInfo> backends;
    for (const QJsonValue value : document.array()) {
        auto backend = parseBackend(value);
        if (!backend)
            return std::unexpected(backend.error());
        backends.append(*backend);
    }
    return backends;
}

std::expected<AiJobInfo, QString> parseJob(const QJsonObject& object)
{
    const QString id = object.value(u"id").toString();
    if (id.isEmpty())
        return std::unexpected(u"a job has no \"id\""_s);
    const QString stateText = object.value(u"state").toString();
    const auto state = aiJobStateFromString(stateText);
    if (!state)
        return std::unexpected(u"job %1 has the unknown state \"%2\""_s.arg(id, stateText));

    AiJobInfo job{
        .id = id,
        .op = object.value(u"op").toString(),
        .backend = object.value(u"backend").toString(),
        .state = *state,
        .progress = std::clamp(object.value(u"progress").toDouble(), 0.0, 1.0),
        .message = object.value(u"message").toString(),
        .result = object.value(u"result").toObject(),
        .outputs = {},
        .provenance = {},
        .error = object.value(u"error").toString(),
    };
    job.provenance = job.result.value(u"provenance").toObject();
    for (const QJsonValue value : job.result.value(u"outputs").toArray()) {
        const QJsonObject output = value.toObject();
        job.outputs.append(AiJobOutput{output.value(u"path").toString(), output.value(u"kind").toString()});
    }
    return job;
}

std::expected<QList<AiJobInfo>, QString> parseJobs(const QJsonDocument& document)
{
    if (!document.isArray())
        return std::unexpected(u"the job list is not a JSON array"_s);
    QList<AiJobInfo> jobs;
    for (const QJsonValue value : document.array()) {
        auto job = parseJob(value.toObject());
        if (!job)
            return std::unexpected(job.error());
        jobs.append(*job);
    }
    return jobs;
}

QJsonObject toJson(const AiBackendInfo& backend)
{
    QJsonArray ops;
    for (const AiOperation& operation : backend.operations) {
        ops.append(QJsonObject{
            {u"id"_s, operation.id},
            {u"title"_s, operation.title},
            {u"description"_s, operation.description},
            {u"paramsSchema"_s, operation.paramsSchema},
        });
    }
    return QJsonObject{
        {u"id"_s, backend.id},
        {u"title"_s, backend.title},
        {u"ops"_s, ops},
        {u"available"_s, backend.available},
        {u"reason"_s, backend.reason},
    };
}

}  // namespace mikserstudio::services
