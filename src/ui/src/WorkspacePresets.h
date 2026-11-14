// The built-in workspaces (Editing, Color, AI) as steps that place panels.
#pragma once

#include <QList>
#include <QString>

namespace mikserstudio::ui {

// How a built-in workspace places one panel. `relativeTo` names a panel placed by an earlier
// step; when that panel is missing the step is placed relative to the whole main window (or, for
// Tab, in the panel's default area).
enum class Placement { Left, Right, Top, Bottom, Tab };

struct WorkspaceStep {
    QString panelId;
    Placement placement = Placement::Tab;
    QString relativeTo;
    int size = 0;  // Preferred width (Left/Right) or height (Top/Bottom); 0 = automatic.
};

struct WorkspacePreset {
    QString name;
    QList<WorkspaceStep> steps;
};

// "Editing", "Color", "AI", in menu order.
[[nodiscard]] const QList<WorkspacePreset>& workspacePresets();
[[nodiscard]] const WorkspacePreset* findWorkspacePreset(const QString& name);

}  // namespace mikserstudio::ui
