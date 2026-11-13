// mikser-studio-mcp: the MCP bridge on stdin and stdout, one JSON-RPC message a line. It offers the
// commands of a running Mikser Studio to an MCP client, such as Claude Code, as tools (docs/api.md).
#include "Bridge.h"
#include "Discovery.h"
#include "Logging.h"
#include "StandardStreams.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QDir>

#include <csignal>
#include <cstdio>

using namespace Qt::StringLiterals;

namespace {

// stdout carries the protocol, so the log goes to stderr, where MCP clients collect it.
void logToStderr(QtMsgType type, const QMessageLogContext& context, const QString& message)
{
    const QByteArray line = qFormatLogMessage(type, context, message).toUtf8() + '\n';
    std::fwrite(line.constData(), 1, static_cast<std::size_t>(line.size()), stderr);
    std::fflush(stderr);
}

}  // namespace

int main(int argc, char* argv[])
{
    using namespace mikserstudio::mcp;

#ifdef SIGPIPE
    // Answers to a client that has gone fail to write; they must not end the program.
    (void)std::signal(SIGPIPE, SIG_IGN);
#endif
    useBinaryStdio();
    qSetMessagePattern(u"%{if-debug}DEBUG%{endif}%{if-info}INFO%{endif}%{if-warning}WARNING%{endif}"
                       "%{if-critical}ERROR%{endif}%{if-fatal}FATAL%{endif} %{category}: %{message}"_s);
    qInstallMessageHandler(logToStderr);

    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName(u"mikser-studio-mcp"_s);
    QCoreApplication::setApplicationVersion(QStringLiteral(MIKSERSTUDIO_VERSION_STRING));

    QCommandLineParser parser;
    parser.setApplicationDescription(
        u"Offers the commands of a running Mikser Studio to an MCP client, such as Claude Code, as tools. It "
        "speaks MCP on stdin and stdout and finds the app through its discovery file."_s);
    parser.addHelpOption();
    parser.addVersionOption();
    const QCommandLineOption instance(
        u"instance"_s,
        u"Which Mikser Studio to drive: installed, the installed app; dev, Mikser Studio Dev, built from "
        "source; or e2e, Mikser Studio E2E. The default is MIKSERSTUDIO_INSTANCE when it names one, otherwise "
        "the installed app."_s,
        u"name"_s, defaultInstance());
    const QCommandLineOption discoveryFileOption(u"discovery-file"_s,
                                                 u"The app's api.json, when it is somewhere else."_s, u"path"_s);
    parser.addOption(instance);
    parser.addOption(discoveryFileOption);
    parser.process(app);

    const QString instanceName = parser.value(instance);
    if (!instanceNames().contains(instanceName)) {
        std::fputs(qUtf8Printable(u"mikser-studio-mcp: --instance is “%1”; choose one of %2.\n"_s.arg(
                       instanceName, instanceNames().join(u", "_s))),
                   stderr);
        return 2;
    }
    const QString path = parser.isSet(discoveryFileOption) ? parser.value(discoveryFileOption)
                                                           : discoveryFile(instanceName);
    qCInfo(lcMcp).noquote() << u"Looking for Mikser Studio through %1."_s.arg(QDir::toNativeSeparators(path));

    Bridge bridge(path, writeMessage, Bridge::Options{});
    StdinReader reader;
    QObject::connect(&reader, &StdinReader::lineRead, &bridge, &Bridge::receive);
    QObject::connect(&reader, &StdinReader::finished, &bridge, &Bridge::close);
    QObject::connect(&bridge, &Bridge::closed, &app, &QCoreApplication::quit, Qt::QueuedConnection);
    bridge.start();
    reader.start();
    return QCoreApplication::exec();
}
