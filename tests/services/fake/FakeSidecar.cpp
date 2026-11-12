// A stand-in for the Python mikserstudio-ai sidecar, used by the SidecarManager and AI command tests.
//
// It accepts the sidecar's command line (--host, --port, --cache-dir, --exit-with-stdin, ...) and
// serves the same /v1 API from memory. Jobs advance by half on every poll of /v1/jobs, so a job
// finishes after two polls. Extra options choose a misbehavior:
//   --mode serve               behave (default)
//   --mode exit                print an error on stderr and exit with code 3 at once
//   --mode silent              never print the READY line
//   --mode crash-after-ready   exit with code 1 shortly after READY (see --crash-times)
//   --mode crash-on-poll       exit with code 1 instead of answering the first job list
//                              (GET /v1/jobs), so a submitted job is running when it crashes
//   --mode unhealthy           answer /v1/health with HTTP 500
//   --mode ignore-stdin        keep running after stdin is closed
//   --crash-times N --counter F  with crash-after-ready/crash-on-poll: crash only on the first N
//                              launches, counted in file F; later launches behave.
//   --report-launch F          write the working directory and NoDefaultCurrentDirectoryInExePath
//                              (one per line) to file F at start-up.
// Special params: an upscale input containing "fail" fails, a prompt "hold" never finishes, a
// prompt "unavailable" is refused with 409.

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QHttpServer>
#include <QHttpServerRequest>
#include <QHttpServerResponse>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTcpServer>
#include <QTimer>
#include <QUuid>

#include <cstdio>
#include <map>
#include <thread>

using namespace Qt::StringLiterals;

namespace {

struct Options {
    QString mode = u"serve"_s;
    QString cacheDir;
    QString counterFile;
    QString reportFile;
    int crashTimes = 0;
    bool exitWithStdin = false;
};

struct Job {
    QString id;
    QString op;
    QJsonObject params;
    QString state = u"running"_s;
    double progress = 0.0;
    QJsonObject result;
    QString error;
};

Options parseOptions(const QStringList& arguments)
{
    Options options;
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments[i];
        const QString value = i + 1 < arguments.size() ? arguments[i + 1] : QString();
        if (argument == u"--mode")
            options.mode = value;
        else if (argument == u"--cache-dir")
            options.cacheDir = value;
        else if (argument == u"--counter")
            options.counterFile = value;
        else if (argument == u"--report-launch")
            options.reportFile = value;
        else if (argument == u"--crash-times")
            options.crashTimes = value.toInt();
        else if (argument == u"--exit-with-stdin")
            options.exitWithStdin = true;
    }
    return options;
}

// Counts this launch in the counter file and returns the launch number (1 for the first).
int countLaunch(const QString& path)
{
    QFile file(path);
    int launches = 0;
    if (file.open(QIODevice::ReadOnly))
        launches = file.readAll().trimmed().toInt();
    file.close();
    ++launches;
    if (file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        file.write(QByteArray::number(launches));
    return launches;
}

QHttpServerResponse error(QHttpServerResponse::StatusCode status, const QString& code, const QString& message)
{
    return QHttpServerResponse(QJsonObject{{u"error"_s, QJsonObject{{u"code"_s, code}, {u"message"_s, message}}}},
                               status);
}

QJsonObject toJson(const Job& job)
{
    return QJsonObject{
        {u"id"_s, job.id},
        {u"op"_s, job.op},
        {u"backend"_s, u"fake"_s},
        {u"params"_s, job.params},
        {u"state"_s, job.state},
        {u"progress"_s, job.progress},
        {u"message"_s, job.state == u"running" ? u"Working"_s : u"Done"_s},
        {u"result"_s, job.result.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(job.result)},
        {u"error"_s, job.error.isEmpty() ? QJsonValue(QJsonValue::Null) : QJsonValue(job.error)},
    };
}

[[noreturn]] void crashOnPurpose()
{
    std::fputs("fake sidecar: crashing on purpose\n", stderr);
    std::fflush(stderr);
    std::exit(1);
}

class FakeApi {
public:
    FakeApi(Options options, bool crashOnPoll)
        : m_options(std::move(options))
        , m_crashOnPoll(crashOnPoll)
    {
    }

    void install(QHttpServer& server)
    {
        server.route(u"/v1/health"_s, QHttpServerRequest::Method::Get, [this] {
            if (m_options.mode == u"unhealthy")
                return error(QHttpServerResponse::StatusCode::InternalServerError, u"internal"_s, u"unhealthy"_s);
            return QHttpServerResponse(QJsonObject{{u"status"_s, u"ok"_s}});
        });
        server.route(u"/v1/backends"_s, QHttpServerRequest::Method::Get, [this](const QHttpServerRequest& request) {
            return authorized(request, [] { return QHttpServerResponse(backends()); });
        });
        server.route(u"/v1/jobs"_s, QHttpServerRequest::Method::Get, [this](const QHttpServerRequest& request) {
            return authorized(request, [this] {
                if (m_crashOnPoll)
                    crashOnPurpose();
                QJsonArray list;
                for (auto& [id, job] : m_jobs) {
                    advance(job);
                    list.append(toJson(job));
                }
                return QHttpServerResponse(list);
            });
        });
        server.route(u"/v1/jobs"_s, QHttpServerRequest::Method::Post, [this](const QHttpServerRequest& request) {
            return authorized(request, [this, &request] { return submit(request); });
        });
        server.route(u"/v1/jobs/<arg>"_s, QHttpServerRequest::Method::Get,
                     [this](const QString& id, const QHttpServerRequest& request) {
                         return authorized(request, [this, id] {
                             const auto it = m_jobs.find(id);
                             if (it == m_jobs.end())
                                 return error(QHttpServerResponse::StatusCode::NotFound, u"not_found"_s, u"no job"_s);
                             return QHttpServerResponse(toJson(it->second));
                         });
                     });
        server.route(u"/v1/jobs/<arg>"_s, QHttpServerRequest::Method::Delete,
                     [this](const QString& id, const QHttpServerRequest& request) {
                         return authorized(request, [this, id] { return cancel(id); });
                     });
    }

private:
    template <typename Handler> QHttpServerResponse authorized(const QHttpServerRequest& request, Handler handler)
    {
        const QByteArray expected = "Bearer " + qgetenv("MIKSERSTUDIO_AI_TOKEN");
        if (request.headers().value(QHttpHeaders::WellKnownHeader::Authorization) != expected)
            return error(QHttpServerResponse::StatusCode::Unauthorized, u"unauthorized"_s,
                         u"Missing or wrong token."_s);
        return handler();
    }

    static QJsonArray backends()
    {
        const QJsonObject schema{{u"type"_s, u"object"_s}};
        return QJsonArray{QJsonObject{
            {u"id"_s, u"fake"_s},
            {u"title"_s, u"Fake"_s},
            {u"ops"_s,
             QJsonArray{
                 QJsonObject{{u"id"_s, u"video.upscale"_s}, {u"title"_s, u"Upscale"_s}, {u"paramsSchema"_s, schema}},
                 QJsonObject{{u"id"_s, u"text.generate"_s}, {u"title"_s, u"Text"_s}, {u"paramsSchema"_s, schema}},
             }},
            {u"available"_s, true},
            {u"reason"_s, u""_s},
        }};
    }

    QHttpServerResponse submit(const QHttpServerRequest& request)
    {
        const QJsonObject body = QJsonDocument::fromJson(request.body()).object();
        const QString op = body.value(u"op").toString();
        const QJsonObject params = body.value(u"params").toObject();
        if (op != u"video.upscale" && op != u"text.generate")
            return error(QHttpServerResponse::StatusCode::NotFound, u"unknown_op"_s, u"Unknown operation."_s);
        if (params.value(u"prompt").toString() == u"unavailable")
            return error(QHttpServerResponse::StatusCode::Conflict, u"unavailable"_s, u"Ollama is not running."_s);
        Job job{
            .id = u"job-"_s + QUuid::createUuid().toString(QUuid::Id128),
            .op = op,
            .params = params,
            .state = u"running"_s,
            .progress = 0.0,
            .result = {},
            .error = {},
        };
        const QString id = job.id;
        m_jobs.emplace(id, std::move(job));
        return QHttpServerResponse(QJsonObject{{u"jobId"_s, id}}, QHttpServerResponse::StatusCode::Accepted);
    }

    QHttpServerResponse cancel(const QString& id)
    {
        const auto it = m_jobs.find(id);
        if (it == m_jobs.end())
            return error(QHttpServerResponse::StatusCode::NotFound, u"not_found"_s, u"no job"_s);
        Job& job = it->second;
        if (job.state != u"running")
            return error(QHttpServerResponse::StatusCode::Conflict, u"already_finished"_s, u"finished"_s);
        job.state = u"canceled"_s;
        return QHttpServerResponse(toJson(job));
    }

    void advance(Job& job)
    {
        if (job.state != u"running" || job.params.value(u"prompt").toString() == u"hold")
            return;
        job.progress += 0.5;
        if (job.progress >= 1.0)
            complete(job);
    }

    void complete(Job& job)
    {
        const QString input = job.params.value(u"input").toString();
        if (input.contains(u"fail")) {
            job.state = u"failed"_s;
            job.error = u"fake upscale failure"_s;
            return;
        }
        const QDir folder(QDir(m_options.cacheDir).filePath(job.id));
        QDir().mkpath(folder.path());
        QString output;
        QString kind;
        if (job.op == u"video.upscale") {
            const int scale = job.params.value(u"scale").toInt(2);
            output = folder.filePath(u"%1_x%2.mkv"_s.arg(QFileInfo(input).completeBaseName()).arg(scale));
            if (!QFile::copy(input, output)) {
                QFile empty(output);
                static_cast<void>(empty.open(QIODevice::WriteOnly));
            }
            kind = u"video"_s;
        } else {
            output = folder.filePath(u"response.txt"_s);
            QFile text(output);
            if (text.open(QIODevice::WriteOnly))
                text.write("echo\n");
            kind = u"text"_s;
            job.result.insert(u"text"_s, u"echo: "_s + job.params.value(u"prompt").toString());
        }
        job.result.insert(u"outputs"_s, QJsonArray{QJsonObject{{u"path"_s, output}, {u"kind"_s, kind}}});
        job.result.insert(u"provenance"_s, QJsonObject{
                                               {u"backend"_s, u"fake"_s},
                                               {u"op"_s, job.op},
                                               {u"params"_s, job.params},
                                               {u"jobId"_s, job.id},
                                               {u"createdAt"_s, QDateTime::currentDateTimeUtc().toString(Qt::ISODate)},
                                               {u"tools"_s, QJsonObject{{u"fake"_s, u"1.0"_s}}},
                                           });
        job.state = u"succeeded"_s;
        job.progress = 1.0;
    }

    Options m_options;
    bool m_crashOnPoll = false;
    std::map<QString, Job> m_jobs;
};

void quitWhenStdinCloses()
{
    std::thread([] {
        char buffer[256];
        while (std::fread(buffer, 1, sizeof(buffer), stdin) > 0) {
        }
        QMetaObject::invokeMethod(QCoreApplication::instance(), &QCoreApplication::quit, Qt::QueuedConnection);
    }).detach();
}

}  // namespace

int main(int argc, char* argv[])
{
    QCoreApplication app(argc, argv);
    const Options options = parseOptions(QCoreApplication::arguments());

    if (options.mode == u"exit") {
        std::fputs("fake sidecar: refusing to start\n", stderr);
        return 3;
    }
    if (!options.reportFile.isEmpty()) {
        QFile report(options.reportFile);
        if (report.open(QIODevice::WriteOnly)) {
            report.write(QDir::currentPath().toUtf8() + '\n');
            report.write(qgetenv("NoDefaultCurrentDirectoryInExePath") + '\n');
        }
    }
    bool crash = options.mode == u"crash-after-ready" || options.mode == u"crash-on-poll";
    if (crash && !options.counterFile.isEmpty())
        crash = countLaunch(options.counterFile) <= options.crashTimes;

    QHttpServer server;
    FakeApi api(options, crash && options.mode == u"crash-on-poll");
    api.install(server);
    auto* tcp = new QTcpServer(&app);
    if (!tcp->listen(QHostAddress::LocalHost, 0) || !server.bind(tcp)) {
        std::fputs("fake sidecar: cannot listen\n", stderr);
        return 4;
    }
    if (options.exitWithStdin && options.mode != u"ignore-stdin")
        quitWhenStdinCloses();

    if (options.mode != u"silent") {
        std::printf("MIKSERSTUDIO_AI_READY port=%u\n", static_cast<unsigned>(tcp->serverPort()));
        std::fflush(stdout);
    }
    if (crash && options.mode == u"crash-after-ready")
        QTimer::singleShot(200, &app, [] { crashOnPurpose(); });
    return QCoreApplication::exec();
}
