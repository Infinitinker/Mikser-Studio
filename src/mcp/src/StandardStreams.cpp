// Reading stdin line by line on a thread, and writing messages to stdout.
#include "StandardStreams.h"

#include <QJsonDocument>

#include <cstdio>
#include <iostream>
#include <string>

#ifdef Q_OS_WIN
#include <fcntl.h>
#include <io.h>
#endif

namespace mikserstudio::mcp {

StdinReader::StdinReader(QObject* parent)
    : QObject(parent)
{
}

StdinReader::~StdinReader()
{
    // The thread ends with stdin, and the program runs until then.
    if (m_thread.joinable())
        m_thread.join();
}

void StdinReader::start()
{
    // Signals emitted on the thread reach receivers on the thread that created this reader, in order.
    m_thread = std::thread([this] {
        std::string line;
        while (std::getline(std::cin, line)) {
            QByteArray bytes = QByteArray::fromStdString(line).trimmed();
            if (!bytes.isEmpty())
                Q_EMIT lineRead(bytes);
        }
        Q_EMIT finished();
    });
}

void useBinaryStdio()
{
#ifdef Q_OS_WIN
    (void)_setmode(_fileno(stdin), _O_BINARY);
    (void)_setmode(_fileno(stdout), _O_BINARY);
#endif
}

void writeMessage(const QJsonObject& message)
{
    const QByteArray line = QJsonDocument(message).toJson(QJsonDocument::Compact) + '\n';
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stdout);
    std::fflush(stdout);
}

}  // namespace mikserstudio::mcp
