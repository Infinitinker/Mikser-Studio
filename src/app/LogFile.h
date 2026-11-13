// The log file: every message of Mikser Studio, in the per-user logs folder.
#pragma once

#include <QString>

#include <expected>
#include <memory>

namespace mikserstudio::app {

// While it exists, every Qt log message goes to <folder>/mikser-studio.log as well as where it went
// before (the console, or the debugger's output in a build without a console).
//
// The log rotates: at the start of a run, and whenever it grows past maxBytes, the log becomes
// mikser-studio.1.log, the one before mikser-studio.2.log, and so on; the oldest of kKeptLogs is
// deleted. One at a time.
class LogFile final {
public:
    static constexpr qint64 kDefaultMaxBytes = 10LL * 1024 * 1024;
    static constexpr int kKeptLogs = 5;  // The current log and four before it.

    // How the log starts: a run gets a new log; a start that stops before it runs (a wrong command
    // line) adds its reason to the current log, so it pushes no earlier run's log out.
    enum class Start { NewLog, ContinueLog };

    // Opens the log in `folder`, creating it, and installs the message handler.
    [[nodiscard]] static std::expected<std::unique_ptr<LogFile>, QString> open(const QString& folder,
                                                                               Start start = Start::NewLog,
                                                                               qint64 maxBytes = kDefaultMaxBytes);
    ~LogFile();
    LogFile(const LogFile&) = delete;
    LogFile& operator=(const LogFile&) = delete;

    // The current log: <folder>/mikser-studio.log.
    [[nodiscard]] QString path() const;
    [[nodiscard]] static QString pathIn(const QString& folder);

    struct State;

private:
    explicit LogFile(std::unique_ptr<State> state);
    std::unique_ptr<State> m_state;
};

}  // namespace mikserstudio::app
