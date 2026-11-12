#include "mikserstudio/ui/CommandPalette.h"

#include "CommandInvoker.h"
#include "CommandPaletteModel.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/ui/Theme.h"

#include <QKeyEvent>
#include <QLineEdit>
#include <QListView>
#include <QPainter>
#include <QStyledItemDelegate>
#include <QVBoxLayout>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

constexpr int kWidth = 620;
constexpr int kRowHeight = 46;
constexpr int kVisibleRows = 9;

// Title with "Category · id" under it and the shortcut as a key cap on the right.
class PaletteDelegate final : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem& option, const QModelIndex&) const override
    {
        return {option.rect.width(), kRowHeight};
    }

    void paint(QPainter* painter, const QStyleOptionViewItem& option, const QModelIndex& index) const override
    {
        const ThemeTokens& theme = darkTheme();
        painter->save();
        painter->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(4, 2, -4, -2);
        if (option.state & (QStyle::State_Selected | QStyle::State_MouseOver)) {
            painter->setPen(Qt::NoPen);
            painter->setBrush((option.state & QStyle::State_Selected) ? theme.selection : theme.hover);
            painter->drawRoundedRect(row, theme.radiusMd, theme.radiusMd);
        }

        const QRectF content = row.adjusted(12, 5, -12, -5);
        QRectF textArea = content;
        const QString shortcut = index.data(CommandPaletteModel::ShortcutRole).toString();
        if (!shortcut.isEmpty()) {
            QFont capFont = option.font;
            capFont.setPixelSize(theme.fontSizeSmall);
            const qreal capWidth = QFontMetricsF(capFont).horizontalAdvance(shortcut) + 14;
            const QRectF cap(content.right() - capWidth, content.center().y() - 10, capWidth, 20);
            painter->setPen(theme.borderStrong);
            painter->setBrush(theme.panel);
            painter->drawRoundedRect(cap, theme.radiusSm, theme.radiusSm);
            painter->setFont(capFont);
            painter->setPen(theme.textMuted);
            painter->drawText(cap, Qt::AlignCenter, shortcut);
            textArea.setRight(cap.left() - 12);
        }

        QFont titleFont = option.font;
        titleFont.setPixelSize(theme.fontSize + 1);
        painter->setFont(titleFont);
        painter->setPen(theme.text);
        const QRectF titleRect(textArea.left(), textArea.top(), textArea.width(), textArea.height() * 0.55);
        painter->drawText(
            titleRect, Qt::AlignLeft | Qt::AlignVCenter,
            QFontMetricsF(titleFont).elidedText(index.data().toString(), Qt::ElideRight, titleRect.width()));

        QFont detailFont = option.font;
        detailFont.setPixelSize(theme.fontSizeSmall);
        painter->setFont(detailFont);
        painter->setPen(theme.textMuted);
        const QString detail = index.data(CommandPaletteModel::CategoryRole).toString() + u"  ·  "_s +
                               index.data(CommandPaletteModel::IdRole).toString();
        const QRectF detailRect(textArea.left(), titleRect.bottom(), textArea.width(),
                                textArea.height() - titleRect.height());
        painter->drawText(detailRect, Qt::AlignLeft | Qt::AlignVCenter,
                          QFontMetricsF(detailFont).elidedText(detail, Qt::ElideRight, detailRect.width()));
        painter->restore();
    }
};

}  // namespace

CommandPalette::CommandPalette(core::CommandBus& bus, QWidget* window)
    : QFrame(window, Qt::Popup | Qt::FramelessWindowHint)
    , m_bus(bus)
    , m_window(window)
    , m_model(new CommandPaletteModel(this))
    , m_search(new QLineEdit(this))
    , m_list(new QListView(this))
{
    const ThemeTokens& theme = darkTheme();
    setObjectName(u"commandPalette"_s);
    setStyleSheet(u"#commandPalette { background: %1; border: 1px solid %2; border-radius: %3px; }"
                  u" #commandPalette QLineEdit { font-size: %4px; padding: 8px 12px; min-height: 24px; }"
                  u" #commandPalette QListView { background: transparent; }"_s
                      .arg(theme.panelAlt.name(), theme.borderStrong.name())
                      .arg(theme.radiusLg)
                      .arg(theme.fontSizeLarge));

    m_search->setPlaceholderText(u"Type a command…"_s);
    m_search->setClearButtonEnabled(true);
    m_search->installEventFilter(this);

    m_list->setModel(m_model);
    m_list->setItemDelegate(new PaletteDelegate(m_list));
    m_list->setUniformItemSizes(true);
    m_list->setMouseTracking(true);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setFixedHeight(kRowHeight * kVisibleRows + 8);
    m_list->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(10, 10, 10, 10);
    layout->setSpacing(8);
    layout->addWidget(m_search);
    layout->addWidget(m_list);
    setFixedWidth(kWidth);

    connect(m_search, &QLineEdit::textChanged, this, &CommandPalette::setQuery);
    connect(m_list, &QListView::clicked, this, [this](const QModelIndex& index) {
        m_list->setCurrentIndex(index);
        runCurrent();
    });
}

CommandPalette::~CommandPalette() = default;

void CommandPalette::popup()
{
    m_model->setSpecs(m_bus.specs());
    setQuery(QString());
    adjustSize();
    const QWidget* anchor = m_window->window();
    const QPoint topCenter = anchor->mapToGlobal(QPoint(anchor->width() / 2, 0));
    move(topCenter.x() - width() / 2, topCenter.y() + 60);
    show();
    raise();
    activateWindow();
    m_search->setFocus();
}

void CommandPalette::setQuery(const QString& query)
{
    if (m_search->text() != query) {
        const QSignalBlocker blocker(m_search);
        m_search->setText(query);
    }
    m_model->setQuery(query);
    if (m_model->rowCount() > 0)
        m_list->setCurrentIndex(m_model->index(0));
}

QStringList CommandPalette::visibleCommandIds() const
{
    return m_model->ids();
}

QString CommandPalette::currentCommandId() const
{
    return m_model->idAt(m_list->currentIndex().row());
}

void CommandPalette::runCurrent()
{
    const QString id = currentCommandId();
    hide();
    if (!id.isEmpty())
        invokeCommand(m_bus, id, m_window);
}

bool CommandPalette::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_search && event->type() == QEvent::KeyPress) {
        const auto* key = static_cast<QKeyEvent*>(event);
        switch (key->key()) {
        case Qt::Key_Down:
            moveSelection(1);
            return true;
        case Qt::Key_Up:
            moveSelection(-1);
            return true;
        case Qt::Key_PageDown:
            moveSelection(kVisibleRows);
            return true;
        case Qt::Key_PageUp:
            moveSelection(-kVisibleRows);
            return true;
        case Qt::Key_Return:
        case Qt::Key_Enter:
            runCurrent();
            return true;
        case Qt::Key_Escape:
            hide();
            return true;
        default:
            break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommandPalette::moveSelection(int delta)
{
    const int rows = m_model->rowCount();
    if (rows == 0)
        return;
    const int row = std::clamp(m_list->currentIndex().row() + delta, 0, rows - 1);
    m_list->setCurrentIndex(m_model->index(row));
}

}  // namespace mikserstudio::ui
