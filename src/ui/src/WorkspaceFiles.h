#pragma once

#include <QString>

namespace mikserstudio::ui {

// Workspace names are free text; their files are "<percent-encoded name>.json" so any name is a
// valid, unique file name on every OS.
[[nodiscard]] QString workspaceFileName(const QString& name);
// Empty when `fileName` is not a workspace file.
[[nodiscard]] QString workspaceNameFromFileName(const QString& fileName);
// Empty when the name is usable, otherwise the reason in user terms.
[[nodiscard]] QString checkWorkspaceName(const QString& name);

}  // namespace mikserstudio::ui
