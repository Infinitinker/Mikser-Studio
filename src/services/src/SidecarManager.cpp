#include "mikserstudio/services/SidecarManager.h"

#include "Logging.h"
#include "SidecarLaunch.h"

#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QProcess>

#include <algorithm>
#include <utility>

using namespace Qt::StringLiterals;

namespace mikserstudio::services {

namespace {

constexpr qsizetype kStderrLinesKept = 10;
constexpr int kTerminateWaitMs = 1000;
constexpr int kKillWaitMs = 1000;

int toMs(std::chrono::milliseconds duration)
{
    return static_cast<int>(std::clamp<std::chrono::milliseconds::rep>(duration.count(), 0, 24 * 3600 * 1000));
}

}  // namespace

QString toString(SidecarState state)
{
    switch (state) {
    case SidecarState::Stopped:
        return u"stopped"_s;
    case SidecarState::Starting:
        return u"starting"_s;
    case SidecarState::Running:
        return u"running"_s;
    case SidecarState::Restarting:
        return u"restarting"_s;
    case SidecarState::Failed:
        return u"failed"_s;
    }
    return u"failed"_s;
}

SidecarManager::SidecarManager(SidecarOptions options, QObject* parent)
    : QObject(parent)
    , m_options(std::move(options))
    , m_network(new QNetworkAccessManager(this))
{
    m_network->setProxy(QNetworkProxy::NoProxy);
    for (QTimer* timer : {&m_readyTimer, &m_restartTimer, &m_stableTimer})
        timer->setSingleShot(true);
    connect(&m_readyTimer, &QTimer::timeout, this, &SidecarManager::onReadyTimeout);
    connect(&m_restartTimer, &QTimer::timeout, this, &SidecarManager::launch);
    connect(&m_healthTimer, &QTimer::timeout, this, &SidecarManager::checkHealth);
    connect(&m_stableTimer, &QTimer::timeout, this, [this] {
        if (m_restarts > 0)
            qCInfo(lcSidecar) << "The AI service has been stable; the restart budget is full again";
        m_restarts = 0;
    });
}

SidecarManager::~SidecarManager()
{
    stop();
}

void SidecarManager::start()
{
    if (m_state == SidecarState::Starting || m_state == SidecarState::Running || m_state == SidecarState::Restarting)
        return;
    m_restarts = 0;
    m_error.clear();
    launch();
}

void SidecarManager::stop()
{
    for (QTimer* timer : {&m_readyTimer, &m_healthTimer, &m_restartTimer, &m_stableTimer})
        timer->stop();
    if (m_process)
        qCInfo(lcSidecar) << "Stopping the AI service";
    discardProcess(true);
    m_baseUrl.clear();
    setState(SidecarState::Stopped);
}

void SidecarManager::launch()
{
    discardProcess(false);
    m_token = newSidecarToken();
    auto launch = resolveSidecarLaunch(m_options, m_token);
    if (!launch) {
        fail(launch.error());
        return;
    }

    m_stderrTail.clear();
    m_healthFailures = 0;
    m_baseUrl.clear();
    m_process = new QProcess(this);
    m_process->setProgram(launch->program);
    m_process->setArguments(launch->arguments);
    m_process->setProcessEnvironment(launch->environment);
    m_process->setWorkingDirectory(launch->workingDirectory);
    m_process->setProcessChannelMode(QProcess::SeparateChannels);
    connect(m_process, &QProcess::readyReadStandardOutput, this, &SidecarManager::readStdout);
    connect(m_process, &QProcess::readyReadStandardError, this, &SidecarManager::readStderr);
    connect(m_process, &QProcess::finished, this, [this](int exitCode) { onFinished(exitCode); });
    connect(m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart)
            onFailedToStart();
    });

    qCInfo(lcSidecar).noquote() << "Starting the AI service:" << launch->program << launch->arguments.join(u' ');
    setState(SidecarState::Starting);
    m_readyTimer.start(toMs(m_options.readyTimeout));
    // ReadWrite keeps a pipe on stdin; closing it later asks the sidecar to exit.
    m_process->start(QIODevice::ReadWrite);
}

void SidecarManager::readStdout()
{
    while (m_process && m_process->canReadLine()) {
        const QByteArray line = m_process->readLine().trimmed();
        const int port = parseReadyLine(line);
        if (port > 0 && m_state == SidecarState::Starting) {
            becomeRunning(port);
        } else if (!line.isEmpty()) {
            qCInfo(lcSidecar).noquote() << "[stdout]" << QString::fromUtf8(line);
        }
    }
}

void SidecarManager::readStderr()
{
    if (!m_process)
        return;
    m_process->setReadChannel(QProcess::StandardError);
    while (m_process->canReadLine()) {
        const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
        if (line.isEmpty())
            continue;
        qCInfo(lcSidecar).noquote() << line;
        m_stderrTail.append(line);
        if (m_stderrTail.size() > kStderrLinesKept)
            m_stderrTail.removeFirst();
    }
    m_process->setReadChannel(QProcess::StandardOutput);
}

void SidecarManager::becomeRunning(int port)
{
    m_readyTimer.stop();
    m_baseUrl = QUrl(u"http://127.0.0.1:%1"_s.arg(port));
    m_error.clear();
    qCInfo(lcSidecar).noquote() << "The AI service is ready at" << m_baseUrl.toString();
    m_healthTimer.start(toMs(m_options.healthInterval));
    m_stableTimer.start(toMs(m_options.stableAfter));
    setState(SidecarState::Running);
}

void SidecarManager::onFinished(int exitCode)
{
    if (m_process) {
        // Whatever it printed last usually explains the exit.
        readStdout();
        m_process->setReadChannel(QProcess::StandardError);
        const QStringList rest = QString::fromUtf8(m_process->readAll()).split(u'\n', Qt::SkipEmptyParts);
        for (const QString& line : rest) {
            if (!line.trimmed().isEmpty())
                m_stderrTail.append(line.trimmed());
        }
    }
    const bool wasRunning = m_state == SidecarState::Running;
    QString problem = wasRunning ? u"stopped unexpectedly (exit code %1)"_s.arg(exitCode)
                                 : u"exited before it was ready (exit code %1)"_s.arg(exitCode);
    if (const QString last = lastStderrLine(); !last.isEmpty())
        problem += u": "_s + last;
    restartAfter(problem);
}

void SidecarManager::onFailedToStart()
{
    const QString program = m_process ? m_process->program() : QString();
    const QString reason = m_process ? m_process->errorString() : QString();
    m_readyTimer.stop();
    discardProcess(false);
    fail(u"The AI service could not start: \"%1\" could not be run (%2)."_s.arg(program, reason));
}

void SidecarManager::onReadyTimeout()
{
    const QString seconds = QString::number(static_cast<double>(m_options.readyTimeout.count()) / 1000.0);
    QString problem = u"did not report that it was ready within %1 seconds"_s.arg(seconds);
    if (const QString last = lastStderrLine(); !last.isEmpty())
        problem += u" (last message: %1)"_s.arg(last);
    restartAfter(problem);
}

void SidecarManager::checkHealth()
{
    if (m_state != SidecarState::Running || m_healthInFlight)
        return;
    QUrl url = m_baseUrl;
    url.setPath(u"/v1/health"_s);
    QNetworkRequest request(url);
    request.setTransferTimeout(toMs(m_options.healthTimeout));
    m_healthInFlight = true;
    QNetworkReply* reply = m_network->get(request);
    const QUrl checkedUrl = m_baseUrl;
    connect(reply, &QNetworkReply::finished, this, [this, reply, checkedUrl] {
        reply->deleteLater();
        m_healthInFlight = false;
        if (m_state != SidecarState::Running || m_baseUrl != checkedUrl)
            return;
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 200) {
            m_healthFailures = 0;
            return;
        }
        ++m_healthFailures;
        qCWarning(lcSidecar).noquote() << "Health check" << m_healthFailures << "of"
                                       << m_options.healthFailuresBeforeRestart
                                       << "failed:" << (status ? u"HTTP %1"_s.arg(status) : reply->errorString());
        if (m_healthFailures >= m_options.healthFailuresBeforeRestart)
            restartAfter(u"stopped answering health checks"_s);
    });
}

void SidecarManager::restartAfter(const QString& problem)
{
    for (QTimer* timer : {&m_readyTimer, &m_healthTimer, &m_stableTimer})
        timer->stop();
    discardProcess(false);
    m_baseUrl.clear();
    if (m_restarts >= m_options.maxRestarts) {
        fail(
            u"The AI service %1. Mikser Studio gave up after %2 restarts; run an AI command to try again, and see the log (mikserstudio.services.sidecar) for details."_s
                .arg(problem)
                .arg(m_restarts));
        return;
    }
    const auto delay = m_options.restartDelay * (1LL << std::min(m_restarts, 20));
    ++m_restarts;
    const QString message = u"The AI service %1. Restarting it (attempt %2 of %3)."_s.arg(problem)
                                .arg(m_restarts)
                                .arg(m_options.maxRestarts);
    qCWarning(lcSidecar).noquote() << message;
    m_error = message;
    setState(SidecarState::Restarting);
    Q_EMIT errorOccurred(message);
    m_restartTimer.start(toMs(delay));
}

void SidecarManager::fail(const QString& message)
{
    qCWarning(lcSidecar).noquote() << message;
    m_error = message;
    setState(SidecarState::Failed);
    Q_EMIT errorOccurred(message);
}

void SidecarManager::discardProcess(bool graceful)
{
    if (!m_process)
        return;
    QProcess* process = std::exchange(m_process, nullptr);
    process->disconnect(this);
    if (process->state() != QProcess::NotRunning) {
        bool exited = false;
        if (graceful) {
            process->closeWriteChannel();
            exited = process->waitForFinished(toMs(m_options.stopTimeout));
            if (!exited) {
                qCWarning(lcSidecar) << "The AI service did not exit after its stdin was closed; terminating it";
                process->terminate();
                exited = process->waitForFinished(kTerminateWaitMs);
            }
        }
        if (!exited) {
            process->kill();
            if (!process->waitForFinished(kKillWaitMs))
                qCWarning(lcSidecar) << "The AI service process did not end after being killed";
        }
    }
    process->deleteLater();
}

void SidecarManager::setState(SidecarState state)
{
    if (m_state == state)
        return;
    m_state = state;
    qCDebug(lcSidecar).noquote() << "State:" << toString(state);
    Q_EMIT stateChanged(state);
}

QString SidecarManager::lastStderrLine() const
{
    return m_stderrTail.isEmpty() ? QString() : m_stderrTail.last();
}

}  // namespace mikserstudio::services
