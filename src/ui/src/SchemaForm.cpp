#include "mikserstudio/ui/SchemaForm.h"

#include "SchemaEditors.h"

#include "mikserstudio/core/JsonSchema.h"

#include <QFormLayout>
#include <QGroupBox>
#include <QJsonArray>
#include <QLabel>
#include <QStyle>
#include <QVBoxLayout>

#include <vector>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString escapePointerToken(QString token)
{
    token.replace(u'~', u"~0"_s);
    token.replace(u'/', u"~1"_s);
    return token;
}

// "clipId" -> "Clip id", "max_tokens" -> "Max tokens".
QString humanize(const QString& key)
{
    QString words;
    for (qsizetype i = 0; i < key.size(); ++i) {
        const QChar c = key.at(i);
        if (c == u'_' || c == u'-') {
            words += u' ';
        } else if (c.isUpper() && i > 0 && key.at(i - 1).isLower()) {
            words += u' ';
            words += c.toLower();
        } else {
            words += c;
        }
    }
    if (!words.isEmpty())
        words[0] = words[0].toUpper();
    return words;
}

void setInvalid(QWidget* widget, bool invalid)
{
    widget->setProperty("state", invalid ? u"invalid"_s : QString());
    widget->style()->unpolish(widget);
    widget->style()->polish(widget);
}

// Required properties first, in the order of "required", then the others by name.
QStringList propertyOrder(const QJsonObject& schema)
{
    const QJsonObject properties = schema.value(u"properties").toObject();
    QStringList order;
    for (const QJsonValue name : schema.value(u"required").toArray()) {
        if (properties.contains(name.toString()) && !order.contains(name.toString()))
            order.append(name.toString());
    }
    for (const QString& key : properties.keys()) {
        if (!order.contains(key))
            order.append(key);
    }
    return order;
}

struct Node {
    QString key;
    QString pointer;
    QJsonObject schema;
    bool required = false;
    std::unique_ptr<FieldEditor> editor;  // Null for nested objects.
    QWidget* focusWidget = nullptr;
    QLabel* error = nullptr;
    std::vector<Node> children;
};

}  // namespace

struct SchemaForm::Impl {
    SchemaForm* q = nullptr;
    QJsonObject schema;
    QVBoxLayout* layout = nullptr;
    QWidget* body = nullptr;
    QLabel* formError = nullptr;
    std::vector<Node> nodes;

    void build(const QJsonObject& objectSchema, const QString& prefix, QFormLayout* form, std::vector<Node>& out)
    {
        const QJsonObject properties = objectSchema.value(u"properties").toObject();
        const QJsonArray required = objectSchema.value(u"required").toArray();
        for (const QString& key : propertyOrder(objectSchema)) {
            Node node;
            node.key = key;
            node.pointer = prefix + u'/' + escapePointerToken(key);
            node.schema = properties.value(key).toObject();
            node.required = required.contains(QJsonValue(key));
            const QString title = node.schema.value(u"title").toString(humanize(key));
            const QString description = node.schema.value(u"description").toString();

            node.error = new QLabel;
            node.error->setProperty("role", u"error"_s);
            node.error->setWordWrap(true);
            node.error->hide();

            if (singleSchemaType(node.schema) == u"object" && !node.schema.contains(u"enum")) {
                auto* group = new QGroupBox(title);
                group->setToolTip(description);
                auto* inner = new QFormLayout(group);
                configure(inner);
                build(node.schema, node.pointer, inner, node.children);
                inner->addRow(node.error);
                form->addRow(group);
                node.focusWidget = group;
            } else {
                const bool optional = !node.required && !node.schema.contains(u"default");
                node.editor = createFieldEditor(node.schema, optional, [this] { Q_EMIT q->valueChanged(); });
                node.focusWidget = node.editor->widget();
                node.focusWidget->setToolTip(description);
                auto* label = new QLabel(node.required ? title + u" *"_s : title);
                label->setToolTip(description);
                label->setBuddy(node.focusWidget);
                auto* cell = new QWidget;
                auto* cellLayout = new QVBoxLayout(cell);
                cellLayout->setContentsMargins(0, 0, 0, 0);
                cellLayout->setSpacing(2);
                cellLayout->addWidget(node.focusWidget);
                cellLayout->addWidget(node.error);
                form->addRow(label, cell);
            }
            out.push_back(std::move(node));
        }
    }

    static void configure(QFormLayout* form)
    {
        form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
        form->setLabelAlignment(Qt::AlignLeft | Qt::AlignVCenter);
        form->setHorizontalSpacing(12);
        form->setVerticalSpacing(8);
    }

    static void assign(std::vector<Node>& level, const QJsonObject& value)
    {
        for (Node& node : level) {
            if (!node.editor) {
                const QJsonValue nested = value.contains(node.key) ? value.value(node.key)
                                                                   : node.schema.value(u"default");
                assign(node.children, nested.toObject());
                continue;
            }
            if (value.contains(node.key))
                node.editor->setValue(value.value(node.key));
            else if (node.schema.contains(u"default"))
                node.editor->setValue(node.schema.value(u"default"));
            else
                node.editor->setValue(std::nullopt);
        }
    }

    static QJsonObject collect(const std::vector<Node>& level)
    {
        QJsonObject object;
        for (const Node& node : level) {
            if (!node.editor) {
                const QJsonObject nested = collect(node.children);
                if (node.required || !nested.isEmpty())
                    object.insert(node.key, nested);
            } else if (const auto value = node.editor->value()) {
                object.insert(node.key, *value);
            }
        }
        return object;
    }

    template <typename Visitor> static void visit(std::vector<Node>& level, const Visitor& visitor)
    {
        for (Node& node : level) {
            visitor(node);
            visit(node.children, visitor);
        }
    }

    template <typename Visitor> static void visit(const std::vector<Node>& level, const Visitor& visitor)
    {
        for (const Node& node : level) {
            visitor(node);
            visit(node.children, visitor);
        }
    }

    const Node* find(const QString& pointer) const
    {
        const Node* found = nullptr;
        visit(nodes, [&](const Node& node) {
            if (node.pointer == pointer)
                found = &node;
        });
        return found;
    }

    // The field that shows a problem at `pointer`: the field itself or its closest ancestor.
    Node* owner(const QString& pointer)
    {
        Node* best = nullptr;
        visit(nodes, [&](Node& node) {
            const bool matches = pointer == node.pointer || pointer.startsWith(node.pointer + u'/');
            if (matches && (!best || node.pointer.size() > best->pointer.size()))
                best = &node;
        });
        return best;
    }

    static void showError(QLabel* label, const QString& text)
    {
        label->setText(label->text().isEmpty() ? text : label->text() + u'\n' + text);
        label->show();
    }
};

SchemaForm::SchemaForm(QWidget* parent)
    : SchemaForm({}, {}, parent)
{
}

SchemaForm::SchemaForm(const QJsonObject& schema, const QJsonObject& value, QWidget* parent)
    : QWidget(parent)
    , d(std::make_unique<Impl>())
{
    d->q = this;
    d->layout = new QVBoxLayout(this);
    d->layout->setContentsMargins(0, 0, 0, 0);
    d->layout->setSpacing(8);
    d->formError = new QLabel(this);
    d->formError->setProperty("role", u"error"_s);
    d->formError->setWordWrap(true);
    d->formError->hide();
    d->layout->addWidget(d->formError);
    setSchema(schema, value);
}

SchemaForm::~SchemaForm() = default;

void SchemaForm::setSchema(const QJsonObject& schema, const QJsonObject& value)
{
    d->nodes.clear();
    delete d->body;
    d->schema = schema;
    d->body = new QWidget(this);
    auto* form = new QFormLayout(d->body);
    form->setContentsMargins(0, 0, 0, 0);
    Impl::configure(form);
    d->build(schema, QString(), form, d->nodes);
    d->layout->addWidget(d->body);
    clearErrors();
    setValue(value);
}

QJsonObject SchemaForm::schema() const
{
    return d->schema;
}

QJsonObject SchemaForm::value() const
{
    return Impl::collect(d->nodes);
}

void SchemaForm::setValue(const QJsonObject& value)
{
    {
        const QSignalBlocker blocker(this);
        Impl::assign(d->nodes, value);
    }
    Q_EMIT valueChanged();
}

QStringList SchemaForm::validate()
{
    clearErrors();
    QStringList problems;
    Impl::visit(d->nodes, [&](Node& node) {
        if (node.editor) {
            if (const QString error = node.editor->inputError(); !error.isEmpty()) {
                problems.append(node.pointer + u": "_s + error);
                Impl::showError(node.error, error);
                setInvalid(node.focusWidget, true);
            }
        }
    });
    if (!problems.isEmpty())
        return problems;

    problems = core::JsonSchema::validate(value(), d->schema);
    for (const QString& problem : std::as_const(problems)) {
        const qsizetype colon = problem.indexOf(u": "_s);
        const QString pointer = colon > 0 ? problem.left(colon) : QString();
        const QString message = colon > 0 ? problem.mid(colon + 2) : problem;
        Node* node = pointer.startsWith(u'/') ? d->owner(pointer) : nullptr;
        if (!node) {
            Impl::showError(d->formError, message);
            continue;
        }
        // A problem deeper than the field (e.g. inside a JSON array) keeps its pointer.
        Impl::showError(node->error, pointer == node->pointer ? message : pointer + u": "_s + message);
        setInvalid(node->focusWidget, true);
    }
    return problems;
}

void SchemaForm::clearErrors()
{
    d->formError->clear();
    d->formError->hide();
    Impl::visit(d->nodes, [](Node& node) {
        node.error->clear();
        node.error->hide();
        setInvalid(node.focusWidget, false);
    });
}

bool SchemaForm::hasFields() const
{
    return !d->nodes.empty();
}

QWidget* SchemaForm::editor(const QString& pointer) const
{
    const Node* node = d->find(pointer);
    return node ? node->focusWidget : nullptr;
}

QString SchemaForm::errorText(const QString& pointer) const
{
    if (pointer.isEmpty())
        return d->formError->text();
    const Node* node = d->find(pointer);
    return node ? node->error->text() : QString();
}

}  // namespace mikserstudio::ui
