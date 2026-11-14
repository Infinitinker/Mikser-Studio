// The encoding of workspace names as file names, and the check for usable names.
#include "WorkspaceFiles.h"

#include <QByteArray>
#include <QUrl>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr auto kSuffix = u".json";
constexpr qsizetype kMaxNameLength = 64;

}  // namespace

QString workspaceFileName(const QString& name)
{
    // Letters, digits, '-' and '_' stay readable; everything else (spaces, dots, slashes,
    // non-ASCII) is encoded.
    return QString::fromLatin1(QUrl::toPercentEncoding(name, "-_")) + QString(kSuffix);
}

QString workspaceNameFromFileName(const QString& fileName)
{
    if (!fileName.endsWith(QString(kSuffix)))
        return {};
    const QString encoded = fileName.chopped(QString(kSuffix).size());
    if (encoded.isEmpty())
        return {};
    return QUrl::fromPercentEncoding(encoded.toLatin1());
}

QString checkWorkspaceName(const QString& name)
{
    if (name.trimmed().isEmpty())
        return u"Enter a name for the workspace."_s;
    if (name != name.trimmed())
        return u"The workspace name cannot start or end with spaces."_s;
    if (name.size() > kMaxNameLength)
        return u"Use a workspace name of at most %1 characters."_s.arg(kMaxNameLength);
    return {};
}

}  // namespace mikserstudio::ui
