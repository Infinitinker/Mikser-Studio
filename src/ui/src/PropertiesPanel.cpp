// The Properties panel: the clip's fields, and the move and trim commands that edits run.
#include "PropertiesPanel.h"

#include "CommandInvoker.h"
#include "PanelLayout.h"
#include "Timecode.h"

#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/Project.h"
#include "mikserstudio/ui/ClipSelection.h"

#include <QDir>
#include <QFormLayout>
#include <QJsonObject>
#include <QLabel>
#include <QSpinBox>
#include <QStackedLayout>
#include <QVBoxLayout>

#include <algorithm>

#include <limits>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QSpinBox* frameSpinBox(QWidget* parent)
{
    auto* spin = new QSpinBox(parent);
    spin->setRange(0, std::numeric_limits<int>::max());
    spin->setSuffix(u" f"_s);
    spin->setAccelerated(true);
    spin->setKeyboardTracking(false);
    return spin;
}

int toInt(core::FramePos frames)
{
    return static_cast<int>(std::clamp<core::FramePos>(frames, 0, std::numeric_limits<int>::max()));
}

// The frame after the last one a clip of this media may use (stills: any length).
core::FramePos mediaEnd(const core::MediaItem* media)
{
    if (!media || media->info.kind == core::MediaKind::Image)
        return core::kMaxFrame;
    return media->info.durationFrames;
}

}  // namespace

PropertiesPanel::PropertiesPanel(core::CommandBus& bus, const core::ProjectModel& project,
                                 const ClipSelection& selection, QWidget* parent)
    : QWidget(parent)
    , m_bus(bus)
    , m_project(project)
    , m_selection(selection)
{
    auto* layout = makePanelLayout(this);
    layout->addLayout(makePanelHeader(u"Clip properties"_s));

    auto* stackHost = new QWidget(this);
    m_stack = new QStackedLayout(stackHost);

    auto* empty = makeLabel(u"Select a clip on the timeline to see its properties."_s, u"muted"_s);
    empty->setAlignment(Qt::AlignCenter);
    empty->setWordWrap(true);
    m_stack->addWidget(empty);

    auto* details = new QWidget;
    auto* form = new QFormLayout(details);
    form->setContentsMargins(0, 4, 0, 0);
    form->setHorizontalSpacing(12);
    form->setVerticalSpacing(10);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);

    m_media = new QLabel(details);
    m_media->setWordWrap(true);
    m_media->setTextInteractionFlags(Qt::TextSelectableByMouse);
    form->addRow(u"Media"_s, m_media);

    m_track = new QLabel(details);
    form->addRow(u"Track"_s, m_track);

    m_position = frameSpinBox(details);
    m_position->setToolTip(u"Timeline frame where the clip starts (timeline.move_clip)."_s);
    m_positionTimecode = makeLabel({}, u"hint"_s, details);
    auto* positionCell = new QWidget(details);
    auto* positionLayout = new QVBoxLayout(positionCell);
    positionLayout->setContentsMargins(0, 0, 0, 0);
    positionLayout->setSpacing(2);
    positionLayout->addWidget(m_position);
    positionLayout->addWidget(m_positionTimecode);
    form->addRow(u"Position"_s, positionCell);

    m_in = frameSpinBox(details);
    m_in->setObjectName(u"in"_s);
    m_in->setToolTip(u"First source frame used (timeline.trim_clip)."_s);
    form->addRow(u"In"_s, m_in);
    m_out = frameSpinBox(details);
    m_out->setObjectName(u"out"_s);
    m_out->setToolTip(u"Source frame after the last one used (timeline.trim_clip)."_s);
    m_trimTimecode = makeLabel({}, u"hint"_s, details);
    auto* outCell = new QWidget(details);
    auto* outLayout = new QVBoxLayout(outCell);
    outLayout->setContentsMargins(0, 0, 0, 0);
    outLayout->setSpacing(2);
    outLayout->addWidget(m_out);
    outLayout->addWidget(m_trimTimecode);
    form->addRow(u"Out"_s, outCell);

    m_duration = new QLabel(details);
    form->addRow(u"Duration"_s, m_duration);

    m_error = makeLabel({}, u"error"_s, details);
    m_error->setObjectName(u"error"_s);
    m_error->setWordWrap(true);
    m_error->hide();
    form->addRow(m_error);
    m_stack->addWidget(details);

    layout->addWidget(stackHost);
    layout->addStretch();

    connect(m_position, &QSpinBox::editingFinished, this, &PropertiesPanel::commitPosition);
    connect(m_in, &QSpinBox::editingFinished, this, &PropertiesPanel::commitIn);
    connect(m_out, &QSpinBox::editingFinished, this, &PropertiesPanel::commitOut);
    connect(&selection, &ClipSelection::changed, this, [this] {
        m_error->hide();
        refresh();
    });
    connect(&project, &core::ProjectModel::clipChanged, this, [this](const QString& id) {
        if (id == m_clipId)
            refresh();
    });
    connect(&project, &core::ProjectModel::projectReset, this, &PropertiesPanel::refresh);
    refresh();
}

void PropertiesPanel::commitPosition()
{
    const core::Clip* clip = m_project.findClip(m_clipId);
    if (!clip || m_position->value() == clip->position)
        return;
    const auto run = executeInteractive(
        m_bus, u"timeline.move_clip"_s,
        QJsonObject{{u"clipId"_s, clip->id}, {u"trackId"_s, clip->trackId}, {u"position"_s, m_position->value()}},
        this);
    if (run && !*run)
        showError(run->error().message);
    else if (run)
        m_error->hide();
    refresh();
}

void PropertiesPanel::commitIn()
{
    const core::Clip* clip = m_project.findClip(m_clipId);
    if (!clip)
        return;
    const core::FramePos in = m_in->value();
    core::FramePos out = m_out->value();
    if (in >= out)
        out = std::min(out + (in - clip->in), mediaEnd(m_project.findMedia(clip->mediaId)));
    commitTrim(in, out);
}

void PropertiesPanel::commitOut()
{
    const core::Clip* clip = m_project.findClip(m_clipId);
    if (!clip)
        return;
    core::FramePos in = m_in->value();
    const core::FramePos out = m_out->value();
    if (out <= in)
        in = std::max<core::FramePos>(0, in + (out - clip->out));
    commitTrim(in, out);
}

void PropertiesPanel::commitTrim(core::FramePos in, core::FramePos out)
{
    const core::Clip* clip = m_project.findClip(m_clipId);
    if (!clip || (in == clip->in && out == clip->out))
        return;
    const auto run = executeInteractive(
        m_bus, u"timeline.trim_clip"_s,
        QJsonObject{{u"clipId"_s, clip->id}, {u"in"_s, static_cast<qint64>(in)}, {u"out"_s, static_cast<qint64>(out)}},
        this);
    if (run && !*run)
        showError(run->error().message);
    else if (run)
        m_error->hide();
    refresh();
}

void PropertiesPanel::refresh()
{
    m_clipId = m_selection.current();
    const core::Clip* clip = m_project.findClip(m_clipId);
    if (!clip) {
        m_clipId.clear();
        m_stack->setCurrentIndex(0);
        return;
    }
    const core::MediaItem* media = m_project.findMedia(clip->mediaId);
    const core::Track* track = m_project.findTrack(clip->trackId);
    const core::Rational rate = m_project.frameRate();

    m_media->setText(media ? media->name : clip->mediaId);
    m_media->setToolTip(media ? QDir::toNativeSeparators(media->path) : QString());
    m_track->setText(track && !track->name.isEmpty() && track->name != track->id
                         ? u"%1 (%2)"_s.arg(track->id, track->name)
                         : clip->trackId);

    const QSignalBlocker blockPosition(m_position);
    const QSignalBlocker blockIn(m_in);
    const QSignalBlocker blockOut(m_out);
    m_position->setValue(toInt(clip->position));
    // Video and audio cannot be trimmed past the media's length; stills can be any length.
    const int end = toInt(mediaEnd(media));
    m_in->setRange(0, std::max(0, end - 1));
    m_out->setRange(1, std::max(1, end));
    m_in->setValue(toInt(clip->in));
    m_out->setValue(toInt(clip->out));

    m_positionTimecode->setText(
        u"%1 → %2"_s.arg(formatTimecode(clip->position, rate), formatTimecode(clip->end(), rate)));
    m_trimTimecode->setText(u"Source %1 → %2"_s.arg(formatTimecode(clip->in, rate), formatTimecode(clip->out, rate)));
    m_duration->setText(u"%1  (%2 frames)"_s.arg(formatTimecode(clip->length(), rate)).arg(clip->length()));
    m_stack->setCurrentIndex(1);
}

void PropertiesPanel::showError(const QString& message)
{
    m_error->setText(message);
    m_error->show();
}

}  // namespace mikserstudio::ui
