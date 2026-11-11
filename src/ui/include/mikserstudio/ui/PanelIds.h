#pragma once

#include <QStringView>

// Ids of the panels the UI and the app register. Saved workspaces store these, so never rename
// them.
namespace mikserstudio::ui::panel_ids {

inline constexpr QStringView kMediaBin = u"mikserstudio.core/media-bin";
inline constexpr QStringView kTimeline = u"mikserstudio.core/timeline";
inline constexpr QStringView kProperties = u"mikserstudio.core/properties";
inline constexpr QStringView kAi = u"mikserstudio.core/ai";
inline constexpr QStringView kLog = u"mikserstudio.core/log";
inline constexpr QStringView kPlugins = u"mikserstudio.core/plugins";
// Registered by the app (engine monitors); the built-in workspaces place them.
inline constexpr QStringView kSourceMonitor = u"mikserstudio.core/monitor.source";
inline constexpr QStringView kProgramMonitor = u"mikserstudio.core/monitor.program";

}  // namespace mikserstudio::ui::panel_ids
