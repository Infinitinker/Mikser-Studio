#include "PlaceholderPanel.h"

#include <QLabel>
#include <QVBoxLayout>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

PlaceholderPanel::PlaceholderPanel(const QString& panelId, QWidget* parent)
    : QWidget(parent)
    , m_panelId(panelId)
{
    setProperty("role", u"panel"_s);
    setAttribute(Qt::WA_StyledBackground);

    auto* text = new QLabel(message(panelId), this);
    text->setProperty("role", u"placeholder"_s);
    text->setWordWrap(true);
    text->setAlignment(Qt::AlignCenter);

    auto* hint = new QLabel(u"Install or enable it to bring the panel back. Panel id: %1"_s.arg(panelId), this);
    hint->setProperty("role", u"hint"_s);
    hint->setWordWrap(true);
    hint->setAlignment(Qt::AlignCenter);
    hint->setTextInteractionFlags(Qt::TextSelectableByMouse);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 24, 24, 24);
    layout->setSpacing(8);
    layout->addStretch();
    layout->addWidget(text);
    layout->addWidget(hint);
    layout->addStretch();
}

QString PlaceholderPanel::message(const QString& panelId)
{
    const qsizetype slash = panelId.indexOf(u'/');
    const QString owner = slash > 0 ? panelId.left(slash) : panelId;
    return u"This panel comes from %1, which is not installed."_s.arg(owner);
}

QString PlaceholderPanel::title(const QString& panelId)
{
    const qsizetype slash = panelId.lastIndexOf(u'/');
    return slash >= 0 ? panelId.mid(slash + 1) : panelId;
}

}  // namespace mikserstudio::ui
