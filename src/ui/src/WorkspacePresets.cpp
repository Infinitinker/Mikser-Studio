#include "WorkspacePresets.h"

#include "mikserstudio/ui/PanelIds.h"

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString id(QStringView panelId)
{
    return panelId.toString();
}

QList<WorkspacePreset> makePresets()
{
    using namespace panel_ids;
    // Editing: bin | source | program | properties on top, the timeline across the bottom.
    WorkspacePreset editing{u"Editing"_s,
                            {
                                {id(kSourceMonitor), Placement::Top, {}, 0},
                                {id(kProgramMonitor), Placement::Right, id(kSourceMonitor), 0},
                                {id(kMediaBin), Placement::Left, id(kSourceMonitor), 340},
                                {id(kProperties), Placement::Right, id(kProgramMonitor), 300},
                                {id(kAi), Placement::Tab, id(kProperties), 0},
                                {id(kPlugins), Placement::Tab, id(kMediaBin), 0},
                                {id(kTimeline), Placement::Bottom, {}, 340},
                                {id(kLog), Placement::Tab, id(kProperties), 0},
                            }};
    // Color: a large program monitor with the clip properties beside it.
    WorkspacePreset color{u"Color"_s,
                          {
                              {id(kProgramMonitor), Placement::Top, {}, 0},
                              {id(kProperties), Placement::Right, id(kProgramMonitor), 340},
                              {id(kTimeline), Placement::Bottom, {}, 260},
                              {id(kMediaBin), Placement::Tab, id(kProperties), 0},
                          }};
    // AI: the AI panel next to the program monitor, the log under it.
    WorkspacePreset ai{u"AI"_s,
                       {
                           {id(kProgramMonitor), Placement::Top, {}, 0},
                           {id(kAi), Placement::Right, id(kProgramMonitor), 420},
                           {id(kMediaBin), Placement::Left, id(kProgramMonitor), 300},
                           {id(kTimeline), Placement::Bottom, {}, 300},
                           {id(kLog), Placement::Bottom, id(kAi), 220},
                           {id(kProperties), Placement::Tab, id(kMediaBin), 0},
                       }};
    return {editing, color, ai};
}

}  // namespace

const QList<WorkspacePreset>& workspacePresets()
{
    static const QList<WorkspacePreset> presets = makePresets();
    return presets;
}

const WorkspacePreset* findWorkspacePreset(const QString& name)
{
    for (const WorkspacePreset& preset : workspacePresets()) {
        if (preset.name.compare(name, Qt::CaseInsensitive) == 0)
            return &preset;
    }
    return nullptr;
}

}  // namespace mikserstudio::ui
