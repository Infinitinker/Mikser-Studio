// The log file: every message of Mikser Studio, and the AI service's output with them, in the per-user
// logs folder.
#pragma once

#include <QString>

#include <expected>
#include <memory>

namespace mikserstudio::app {

// While it exists, every Qt log message goes to <folder>/mikser-studio.log as well as where it went
// before (the console, or the debugger's output in a build without a console). The AI service's
// output arrives as messages of mikserstudio.services.sidecar, so it is in the log too.
//
// The log rotates: at start, and whenever it grows past maxBytes, the log becomes mikser-studio.1.log,
// the one before mikser-studio.2.log, and so on; the oldest of kKeptLogs is deleted. One at a time.
class LogFile final {
public:
    static constexpr qint64 kDefaultMaxBytes = 10LL * 1024 * 1024;
    static constexpr int kKeptLogs = 5;  // The current log and four before it.

    // Opens the log in `folder`, creating it, and installs the message handler.
    [[nodiscard]] static std::expected<std::unique_ptr<LogFile>, QString> open(const QString& folder,
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
