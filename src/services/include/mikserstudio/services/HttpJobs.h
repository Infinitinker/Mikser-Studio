// The job kinds that let plugins reach the network: http.request and http.stream_ndjson.
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>

class QNetworkAccessManager;
class QNetworkReply;

namespace mikserstudio::core {
class JobManager;
}

namespace mikserstudio::services {

struct HttpJobOptions {
    // The port of the app's own API, which the jobs never reach; nullopt while it isn't serving.
    std::function<std::optional<quint16>()> apiPort;
};

// Registers the kinds in `jobs` and runs their jobs; unregisters them when it goes. Both need the
// "network" permission and connect only to the addresses the owner's network entries allow
// (core::NetworkEntry), never to the app's own API.
//
//   http.request {url, method = "GET", body?, timeout = 30, title?}
//     -> {status, body, json?}: the answer, also one that isn't 2xx; `json` when the body is JSON.
//     Fails when the address can't be reached or nothing comes back for `timeout` seconds.
//   http.stream_ndjson {url, method = "POST", body?, fields = ["response"], messages?, timeout = 600,
//                       title?}
//     -> {fields: {name: text}, last, lines}: reads an answer of JSON lines, such as Ollama's, and
//     joins the text of each of `fields` across them; `last` is the last line. `messages` maps a
//     field to the progress shown while it grows ("Writing ({count})", with the lines so far).
//     Fails with the tool's message for an answer that isn't 2xx or a line with an "error", and
//     when nothing comes for `timeout` seconds.
//
// The body, when there is one, is sent as JSON. Answers larger than kMaxAnswerBytes fail.
class HttpJobs final : public QObject {
    Q_OBJECT

public:
    static constexpr qint64 kMaxAnswerBytes = 16LL * 1024 * 1024;

    HttpJobs(core::JobManager& jobs, HttpJobOptions options, QObject* parent = nullptr);
    ~HttpJobs() override;
    HttpJobs(const HttpJobs&) = delete;
    HttpJobs& operator=(const HttpJobs&) = delete;

    struct Call;  // What a job asks for, read from its input.

private:
    void send(const QString& id, const std::shared_ptr<const Call>& call, bool stream);
    void readAnswer(const QString& id, const std::shared_ptr<const Call>& call, QNetworkReply* reply);
    void readStream(const QString& id, const std::shared_ptr<const Call>& call, QNetworkReply* reply);

    core::JobManager& m_jobs;
    HttpJobOptions m_options;
    QNetworkAccessManager* m_network;
    std::map<QString, QPointer<QNetworkReply>> m_replies;  // The running jobs' requests.
};

}  // namespace mikserstudio::services
