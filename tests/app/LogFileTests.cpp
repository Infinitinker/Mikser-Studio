// The log file: messages reach it with their level and category, and it rotates at start and when it is full.
#include "LogFile.h"

#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QTemporaryDir>

#include <catch2/catch_test_macros.hpp>

using namespace Qt::StringLiterals;
using mikserstudio::app::LogFile;

namespace {

Q_LOGGING_CATEGORY(lcLogTest, "mikserstudio.test.log", QtInfoMsg)

QString contents(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

}  // namespace

TEST_CASE("Messages go to the log file with their level and category", "[log]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString folder = QDir(dir.path()).filePath(u"logs"_s);
    QString path;
    {
        auto log = LogFile::open(folder);
        REQUIRE(log.has_value());
        path = (*log)->path();
        CHECK(path == QDir(folder).filePath(u"mikser-studio.log"_s));
        CHECK_FALSE(LogFile::open(folder).has_value());  // One at a time.
        qCInfo(lcLogTest) << "The AI service is ready";
        qCWarning(lcLogTest) << "Health check 1 of 3 failed";
    }
    qCInfo(lcLogTest) << "After the log was closed";

    const QString text = contents(path);
    CHECK(text.contains(u" info mikserstudio.test.log: The AI service is ready"_s));
    CHECK(text.contains(u" warning mikserstudio.test.log: Health check 1 of 3 failed"_s));
    CHECK_FALSE(text.contains(u"After the log was closed"_s));
}

TEST_CASE("The log rotates at start and when it grows past its size, keeping the last ones", "[log]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir folder(dir.path());
    for (int run = 0; run < LogFile::kKeptLogs + 1; ++run) {
        auto log = LogFile::open(folder.path());
        REQUIRE(log.has_value());
        qCInfo(lcLogTest) << "run" << run;
    }
    CHECK(contents(folder.filePath(u"mikser-studio.log"_s)).contains(u"run 5"_s));
    CHECK(contents(folder.filePath(u"mikser-studio.1.log"_s)).contains(u"run 4"_s));
    CHECK(contents(folder.filePath(u"mikser-studio.4.log"_s)).contains(u"run 1"_s));
    CHECK_FALSE(QFile::exists(folder.filePath(u"mikser-studio.5.log"_s)));

    {
        auto log = LogFile::open(folder.path(), LogFile::Start::NewLog, 200);
        REQUIRE(log.has_value());
        for (int line = 0; line < 10; ++line)
            qCInfo(lcLogTest) << "line" << line << "of a log that fills up quickly";
    }
    CHECK(contents(folder.filePath(u"mikser-studio.log"_s)).contains(u"line 9"_s));
    CHECK_FALSE(contents(folder.filePath(u"mikser-studio.log"_s)).contains(u"line 0"_s));
}

TEST_CASE("A start that stops early adds to the current log instead of rotating", "[log]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QDir folder(dir.path());
    {
        auto log = LogFile::open(folder.path());
        REQUIRE(log.has_value());
        qCInfo(lcLogTest) << "the last run";
    }
    {
        auto log = LogFile::open(folder.path(), LogFile::Start::ContinueLog);
        REQUIRE(log.has_value());
        qCCritical(lcLogTest) << "a wrong command line";
    }
    const QString text = contents(folder.filePath(u"mikser-studio.log"_s));
    CHECK(text.contains(u"the last run"_s));
    CHECK(text.contains(u"a wrong command line"_s));
    CHECK_FALSE(QFile::exists(folder.filePath(u"mikser-studio.1.log"_s)));
}

TEST_CASE("A logs folder that cannot be created is an error, and logging goes on as before", "[log]")
{
    QTemporaryDir dir;
    REQUIRE(dir.isValid());
    const QString blocker = QDir(dir.path()).filePath(u"file"_s);
    QFile file(blocker);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.close();

    const auto log = LogFile::open(QDir(blocker).filePath(u"logs"_s));
    REQUIRE_FALSE(log.has_value());
    CHECK(log.error().contains(u"keeps no log file"_s));
}
