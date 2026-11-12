// The Log panel's table over the command log model.
#include "LogPanel.h"

#include "CommandLogModel.h"
#include "PanelLayout.h"

#include <QHeaderView>
#include <QLabel>
#include <QPushButton>
#include <QScrollBar>
#include <QTreeView>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

LogPanel::LogPanel(CommandLogModel& model, QWidget* parent)
    : QWidget(parent)
    , m_view(new QTreeView(this))
{
    auto* layout = makePanelLayout(this);

    auto* clear = new QPushButton(u"Clear"_s, this);
    clear->setToolTip(u"Removes every entry from the log."_s);
    connect(clear, &QPushButton::clicked, &model, &CommandLogModel::clear);
    layout->addLayout(makePanelHeader(u"Command log"_s, {clear}));

    m_view->setModel(&model);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(true);
    m_view->setSelectionMode(QAbstractItemView::ExtendedSelection);
    m_view->setTextElideMode(Qt::ElideRight);
    m_view->header()->setStretchLastSection(true);
    m_view->header()->resizeSection(CommandLogModel::TimeColumn, 72);
    m_view->header()->resizeSection(CommandLogModel::CommandColumn, 190);
    m_view->header()->resizeSection(CommandLogModel::ParamsColumn, 220);
    layout->addWidget(m_view, 1);

    // Follow new entries while the view is scrolled to the bottom.
    connect(&model, &CommandLogModel::rowsInserted, this, [this] {
        QScrollBar* bar = m_view->verticalScrollBar();
        if (bar->value() >= bar->maximum() - 2)
            m_view->scrollToBottom();
    });
}

}  // namespace mikserstudio::ui
