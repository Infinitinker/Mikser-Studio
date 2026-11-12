#pragma once

#include <QByteArray>
#include <QObject>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>
#include <QTimer>
#include <QUrl>

#include <chrono>

class QNetworkAccessManager;
class QProcess;

namespace mikserstudio::services {

enum class SidecarState {
    Stopped,     // Not started, or stopped on purpose.
    Starting,    // Process launched, waiting for "MIKSERSTUDIO_AI_READY port=<port>".
    Running,     // Ready and answering health checks; baseUrl() and token() are valid.
    Restarting,  // Crashed or hung; waiting for the backoff delay before the next launch.
    Failed,      // Could not start or crashed too often; errorString() says why. start() retries.
};

// "stopped", "starting", "running", "restarting", "failed".
[[nodiscard]] QString toString(SidecarState state);

struct SidecarOptions {
    // The program to run. Empty: mikserstudio-ai from the virtual environment in <aiDirectory>/.venv
    // (Scripts\mikserstudio-ai.exe on Windows, bin/mikserstudio-ai elsewhere), made with venv and pip.
    QString program;
    // Arguments for a custom `program`; ignored when `program` is empty.
    QStringList arguments;
    // The ai/ folder (the Python package with pyproject.toml and its .venv), needed by the default command.
    QString aiDirectory;
    // Where jobs write their outputs (the project/app cache). Created when missing. Required.
    QString cacheDirectory;
    // Optional path of ffmpeg for the builtin backend; empty lets the sidecar find it on PATH.
    QString ffmpegPath;
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();

    // A cold first start, importing Python and the backends, can take a while.
    std::chrono::milliseconds readyTimeout{120'000};
    std::chrono::milliseconds healthInterval{5'000};
    std::chrono::milliseconds healthTimeout{3'000};
    int healthFailuresBeforeRestart = 3;
    // Restarts after crashes/hangs before giving up (state Failed). The delay doubles each time.
    int maxRestarts = 3;
    std::chrono::milliseconds restartDelay{1'000};
    // Running this long without trouble resets the restart budget.
    std::chrono::milliseconds stableAfter{60'000};
    // How long stop() waits for a clean exit after closing the sidecar's stdin.
    std::chrono::milliseconds stopTimeout{3'000};
};

// Owns the mikserstudio-ai sidecar process (docs/ai.md): starts it with a fresh session token, waits
// for its READY line, checks its health, restarts it with backoff after crashes or hangs and
// stops it cleanly (stdin closed, then terminate, then kill) on stop() and destruction.
//
// The process gets these arguments after the command: --host 127.0.0.1 --port 0
// --cache-dir <dir> --exit-with-stdin [--ffmpeg <path>], and the token in the environment
// variable MIKSERSTUDIO_AI_TOKEN (not on the command line, where other processes could read it). It runs
// in the cache folder with NoDefaultCurrentDirectoryInExePath=1, so a program planted in the
// folder Mikser Studio was started from is never run in place of ffmpeg.
// Main thread only.
class SidecarManager final : public QObject {
    Q_OBJECT

public:
    explicit SidecarManager(SidecarOptions options, QObject* parent = nullptr);
    ~SidecarManager() override;  // Calls stop().

    // Launches the sidecar unless it is already starting, running or restarting. From Failed it
    // starts over with the full restart budget.
    void start();
    // Stops the sidecar and waits for it to exit (up to stopTimeout + 2 s).
    void stop();

    [[nodiscard]] SidecarState state() const { return m_state; }
    // The last user-facing error ("The AI service could not start: ..."); empty if none.
    [[nodiscard]] QString errorString() const { return m_error; }
    // http://127.0.0.1:<port> while Running, otherwise empty.
    [[nodiscard]] QUrl baseUrl() const { return m_baseUrl; }
    [[nodiscard]] QByteArray token() const { return m_token; }
    // Restarts since the last start() or stable period.
    [[nodiscard]] int restartCount() const { return m_restarts; }
    [[nodiscard]] const SidecarOptions& options() const { return m_options; }

Q_SIGNALS:
    void stateChanged(mikserstudio::services::SidecarState state);
    // A user-facing message when the sidecar fails or has to be restarted.
    void errorOccurred(const QString& message);

private:
    void launch();
    void readStdout();
    void readStderr();
    void onFinished(int exitCode);
    void onFailedToStart();
    void onReadyTimeout();
    void checkHealth();
    void becomeRunning(int port);
    void restartAfter(const QString& problem);
    void fail(const QString& message);
    void discardProcess(bool graceful);
    void setState(SidecarState state);
    [[nodiscard]] QString lastStderrLine() const;

    SidecarOptions m_options;
    SidecarState m_state = SidecarState::Stopped;
    QString m_error;
    QUrl m_baseUrl;
    QByteArray m_token;
    int m_restarts = 0;
    int m_healthFailures = 0;
    bool m_healthInFlight = false;
    QProcess* m_process = nullptr;
    QNetworkAccessManager* m_network;
    QStringList m_stderrTail;
    QTimer m_readyTimer;
    QTimer m_healthTimer;
    QTimer m_restartTimer;
    QTimer m_stableTimer;
};

}  // namespace mikserstudio::services
