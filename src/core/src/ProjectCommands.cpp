// Registering the project and timeline commands, and the undo steps they share.
#include "mikserstudio/core/ProjectCommands.h"

#include "Logging.h"
#include "ProjectCommandsPrivate.h"
#include "mikserstudio/core/Project.h"

#include <QJsonArray>

using namespace Qt::StringLiterals;

namespace mikserstudio::core {

namespace {

class StepEdit final : public UndoableEdit {
public:
    StepEdit(QString text, EditStep undo, EditStep redo)
        : m_text(std::move(text))
        , m_undo(std::move(undo))
        , m_redo(std::move(redo))
    {
    }

    void undo() override { run(m_undo, "undo"); }
    void redo() override { run(m_redo, "redo"); }
    [[nodiscard]] QString text() const override { return m_text; }

private:
    void run(const EditStep& step, const char* what) const
    {
        if (auto done = step(); !done)
            qCWarning(lcProject).noquote() << "Could not" << what << m_text << "-" << done.error();
    }

    QString m_text;
    EditStep m_undo;
    EditStep m_redo;
};

}  // namespace

std::unique_ptr<UndoableEdit> makeEdit(QString text, EditStep undo, EditStep redo)
{
    return std::make_unique<StepEdit>(std::move(text), std::move(undo), std::move(redo));
}

QJsonObject idParam(const QString& description)
{
    return QJsonObject{{u"type"_s, u"string"_s}, {u"minLength"_s, 1}, {u"description"_s, description}};
}

QJsonObject frameParam(const QString& description)
{
    return QJsonObject{
        {u"type"_s, u"integer"_s},
        {u"minimum"_s, 0},
        {u"maximum"_s, qint64{kMaxFrame}},
        {u"description"_s, description},
    };
}

QJsonObject objectSchema(const QJsonObject& properties, const QStringList& required)
{
    return QJsonObject{
        {u"type"_s, u"object"_s},
        {u"properties"_s, properties},
        {u"required"_s, QJsonArray::fromStringList(required)},
        {u"additionalProperties"_s, false},
    };
}

void registerProjectCommands(CommandBus& bus, ProjectModel& project, MediaProbe& probe)
{
    registerMediaCommands(bus, project, probe);
    registerTimelineCommands(bus, project);
}

}  // namespace mikserstudio::core
