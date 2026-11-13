// The media bin panel: importing, removing and placing media items, always through commands.
#include "MediaBinPanel.h"

#include "CommandInvoker.h"
#include "MediaBinModel.h"
#include "PanelLayout.h"
#include "TimelineRules.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"

#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFileDialog>
#include <QHeaderView>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QPushButton>
#include <QTreeView>
#include <QUrl>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

const auto kMediaFilter =
    u"Media files (*.mp4 *.mov *.mkv *.webm *.avi *.mxf *.m4v *.mts *.wav *.mp3 *.flac *.aac *.ogg *.m4a *.opus "
    u"*.png *.jpg *.jpeg *.tif *.tiff *.bmp *.webp *.exr);;All files (*)"_s;

// A typical clip name, which the name column keeps room for, and the room the theme's item padding and the kind
// swatch's spacing take beside it.
const auto kTypicalName = u"Interview_CamA_take03.mp4"_s;
constexpr int kNameMargins = 24;

QStringList showFileDialog(QWidget* parent)
{
    return QFileDialog::getOpenFileNames(parent, u"Import media"_s, QString(), kMediaFilter);
}

}  // namespace

MediaBinPanel::MediaBinPanel(core::CommandBus& bus, const core::ProjectModel& project, QWidget* parent)
    : QWidget(parent)
    , m_bus(bus)
    , m_project(project)
    , m_model(new MediaBinModel(project, this))
    , m_view(new QTreeView(this))
    , m_empty(makeLabel(u"Import media or drop files here."_s, u"muted"_s, this))
    , m_pickFiles(&showFileDialog)
{
    auto* layout = makePanelLayout(this);
    auto* import = new QPushButton(u"Import…"_s, this);
    import->setProperty("role", u"primary"_s);
    import->setToolTip(u"Adds media files to the project (project.import_media)."_s);
    connect(import, &QPushButton::clicked, this, [this] { importFiles(m_pickFiles(this)); });
    layout->addLayout(makePanelHeader(u"Media bin"_s, {import}));

    m_view->setModel(m_model);
    m_view->setRootIsDecorated(false);
    m_view->setUniformRowHeights(true);
    m_view->setAlternatingRowColors(false);
    m_view->setSelectionMode(QAbstractItemView::SingleSelection);
    m_view->setDragEnabled(true);
    m_view->setDragDropMode(QAbstractItemView::DragOnly);
    m_view->setIconSize(QSize(10, 10));
    m_view->header()->setStretchLastSection(false);
    m_view->header()->setSectionResizeMode(MediaBinModel::NameColumn, QHeaderView::Stretch);
    m_view->header()->setSectionResizeMode(MediaBinModel::DurationColumn, QHeaderView::ResizeToContents);
    m_view->header()->setSectionResizeMode(MediaBinModel::KindColumn, QHeaderView::ResizeToContents);
    m_view->installEventFilter(this);
    m_view->viewport()->installEventFilter(this);
    connect(m_view, &QTreeView::doubleClicked, this,
            [this](const QModelIndex& index) { appendToTimeline(m_model->mediaIdAt(index.row())); });
    layout->addWidget(m_view, 1);

    m_empty->setAlignment(Qt::AlignCenter);
    m_empty->setWordWrap(true);
    layout->addWidget(m_empty, 1);

    connect(m_model, &MediaBinModel::modelReset, this, &MediaBinPanel::updateEmptyState);
    connect(m_model, &MediaBinModel::modelReset, this, &MediaBinPanel::updateColumns);
    updateEmptyState();
    setAcceptDrops(true);
}

void MediaBinPanel::importFiles(const QStringList& paths)
{
    for (const QString& path : paths)
        executeReported(m_bus, u"project.import_media"_s, QJsonObject{{u"path"_s, QDir::fromNativeSeparators(path)}},
                        this);
}

void MediaBinPanel::appendToTimeline(const QString& mediaId)
{
    const core::MediaItem* media = m_project.findMedia(mediaId);
    if (!media)
        return;
    const QString trackId = firstTrackFor(m_project, *media);
    if (trackId.isEmpty())
        return;
    executeReported(
        m_bus, u"timeline.add_clip"_s,
        QJsonObject{{u"mediaId"_s, mediaId}, {u"trackId"_s, trackId}, {u"position"_s, trackEnd(m_project, trackId)}},
        this);
}

void MediaBinPanel::removeSelected()
{
    const QModelIndex current = m_view->currentIndex();
    if (!current.isValid())
        return;
    executeReported(m_bus, u"project.remove_media"_s, QJsonObject{{u"mediaId"_s, m_model->mediaIdAt(current.row())}},
                    this);
}

void MediaBinPanel::dragEnterEvent(QDragEnterEvent* event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
}

void MediaBinPanel::dropEvent(QDropEvent* event)
{
    QStringList paths;
    for (const QUrl& url : event->mimeData()->urls()) {
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    }
    importFiles(paths);
    event->acceptProposedAction();
}

bool MediaBinPanel::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_view->viewport() && event->type() == QEvent::Resize)
        updateColumns();
    if (watched == m_view && event->type() == QEvent::KeyPress) {
        const int key = static_cast<QKeyEvent*>(event)->key();
        if (key == Qt::Key_Delete || key == Qt::Key_Backspace) {
            removeSelected();
            return true;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void MediaBinPanel::updateEmptyState()
{
    const bool empty = m_model->rowCount() == 0;
    m_view->setVisible(!empty);
    m_empty->setVisible(empty);
}

void MediaBinPanel::updateColumns()
{
    const QHeaderView* header = m_view->header();
    const QAbstractItemView* items = m_view;  // QTreeView hides sizeHintForColumn().
    const auto contentWidth = [items, header](int column) {
        return std::max(header->sectionSizeHint(column), items->sizeHintForColumn(column));
    };
    const int nameRoom = m_view->viewport()->width() - contentWidth(MediaBinModel::DurationColumn) -
                         contentWidth(MediaBinModel::KindColumn);
    const int typicalName = m_view->fontMetrics().horizontalAdvance(kTypicalName) + m_view->iconSize().width() +
                            kNameMargins;
    m_view->setColumnHidden(MediaBinModel::KindColumn, nameRoom < typicalName);
}

}  // namespace mikserstudio::ui
