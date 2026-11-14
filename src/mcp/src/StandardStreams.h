// The client's side of the program: the lines it writes to stdin, and the messages the bridge writes to
// stdout, one JSON object a line.
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>

#include <thread>

namespace mikserstudio::mcp {

// Reads stdin on a thread of its own, since a blocking read works on every system, for pipes and consoles
// alike, and hands each line that is not blank to the thread that created it.
class StdinReader : public QObject {
    Q_OBJECT

public:
    explicit StdinReader(QObject* parent = nullptr);
    ~StdinReader() override;

    void start();

Q_SIGNALS:
    void lineRead(const QByteArray& line);
    // stdin has closed: the client has gone.
    void finished();

private:
    std::thread m_thread;
};

// Puts stdin and stdout in binary mode where the system has another, so lines end in \n alone.
void useBinaryStdio();

// Writes `message` to stdout as one line.
void writeMessage(const QJsonObject& message);

}  // namespace mikserstudio::mcp
