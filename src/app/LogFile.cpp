// Writing Qt's log messages to a rotating file next to their usual output.
#include "LogFile.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QMutexLocker>

using namespace Qt::StringLiterals;

namespace mikserstudio::app {

struct LogFile::State {
    QDir folder;
    qint64 maxBytes = 0;
    QFile file;
    QtMessageHandler previous = nullptr;
};

namespace {

// The open log, for the message handler. Messages come from every thread, MLT's included, so g_mutex
// guards it, and the log is closed only while no message is being written.
QMutex g_mutex;
LogFile::State* g_state = nullptr;

QString logName(int index)
{
    return index == 0 ? u"mikser-studio.log"_s : u"mikser-studio.%1.log"_s.arg(index);
}

// mikser-studio.log becomes mikser-studio.1.log, and so on; the oldest kept log is deleted.
void rotate(const QDir& folder)
{
    QFile::remove(folder.filePath(logName(LogFile::kKeptLogs - 1)));
    for (int index = LogFile::kKeptLogs - 2; index >= 0; --index)
        QFile::rename(folder.filePath(logName(index)), folder.filePath(logName(index + 1)));
}

const char* levelName(QtMsgType type)
{
    switch (type) {
    case QtDebugMsg:
        return "debug";
    case QtInfoMsg:
        return "info";
    case QtWarningMsg:
        return "warning";
    case QtCriticalMsg:
        return "critical";
    case QtFatalMsg:
        return "fatal";
    }
    return "?";
}

void handleMessage(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QMutexLocker lock(&g_mutex);
    if (LogFile::State* state = g_state) {
        if (state->file.isOpen() && state->file.size() >= state->maxBytes) {
            state->file.close();
            rotate(state->folder);
            // If the new log cannot be opened, the file stays closed and the messages go only where they
            // went before.
            [[maybe_unused]] const bool reopened = state->file.open(QIODevice::WriteOnly | QIODevice::Append |
                                                                    QIODevice::Text);
        }
        if (state->file.isOpen()) {
            const QString line = u"%1 %2 %3: %4\n"_s.arg(
                QDateTime::currentDateTime().toString(Qt::ISODateWithMs), QLatin1StringView(levelName(type)),
                QLatin1StringView(context.category ? context.category : "default"), message);
            state->file.write(line.toUtf8());
            state->file.flush();  // A crash must not lose the lines that explain it.
        }
        if (state->previous)
            state->previous(type, context, message);
    }
}

}  // namespace

std::expected<std::unique_ptr<LogFile>, QString> LogFile::open(const QString& folder, qint64 maxBytes)
{
    if (const QMutexLocker lock(&g_mutex); g_state)
        return std::unexpected(u"A log file is already open."_s);
    auto state = std::make_unique<State>();
    state->folder = QDir(folder);
    state->maxBytes = maxBytes;
    if (folder.isEmpty() || !QDir().mkpath(folder)) {
        return std::unexpected(u"The logs folder “%1” cannot be created, so this run keeps no log file."_s.arg(
            QDir::toNativeSeparators(folder)));
    }
    rotate(state->folder);
    state->file.setFileName(state->folder.filePath(logName(0)));
    if (!state->file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
        return std::unexpected(u"The log file “%1” cannot be written (%2), so this run keeps no log file."_s.arg(
            QDir::toNativeSeparators(state->file.fileName()), state->file.errorString()));
    }
    const QMutexLocker lock(&g_mutex);
    g_state = state.get();
    state->previous = qInstallMessageHandler(handleMessage);
    return std::unique_ptr<LogFile>(new LogFile(std::move(state)));
}

LogFile::LogFile(std::unique_ptr<State> state)
    : m_state(std::move(state))
{
}

LogFile::~LogFile()
{
    const QMutexLocker lock(&g_mutex);
    qInstallMessageHandler(m_state->previous);
    g_state = nullptr;
    m_state->file.close();
}

QString LogFile::path() const
{
    return m_state->file.fileName();
}

QString LogFile::pathIn(const QString& folder)
{
    return QDir(folder).filePath(logName(0));
}

}  // namespace mikserstudio::app
