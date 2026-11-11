#include "PanelLayout.h"

#include "mikserstudio/ui/Theme.h"

#include <QHBoxLayout>
#include <QLabel>
#include <QVBoxLayout>
#include <QWidget>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

QVBoxLayout* makePanelLayout(QWidget* panel)
{
    const ThemeTokens& theme = darkTheme();
    panel->setProperty("role", u"panel"_s);
    panel->setAttribute(Qt::WA_StyledBackground);
    auto* layout = new QVBoxLayout(panel);
    layout->setContentsMargins(theme.spacingMd, theme.spacingSm, theme.spacingMd, theme.spacingMd);
    layout->setSpacing(theme.spacingSm);
    return layout;
}

QHBoxLayout* makePanelHeader(const QString& heading, const QList<QWidget*>& trailing)
{
    auto* row = new QHBoxLayout;
    row->setContentsMargins(0, 0, 0, 0);
    row->setSpacing(darkTheme().spacingSm);
    row->addWidget(makeLabel(heading, u"heading"_s));
    row->addStretch();
    for (QWidget* widget : trailing)
        row->addWidget(widget);
    return row;
}

QLabel* makeLabel(const QString& text, const QString& role, QWidget* parent)
{
    auto* label = new QLabel(text, parent);
    label->setProperty("role", role);
    return label;
}

}  // namespace mikserstudio::ui
