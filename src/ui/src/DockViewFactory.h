// A KDDockWidgets view factory that draws the title bar and tab bar buttons in the theme's colors.
#pragma once

#include <kddockwidgets/qtwidgets/ViewFactory.h>

namespace mikserstudio::ui {

// KDDockWidgets views in the Mikser Studio theme: title bar and tab bar buttons get icons drawn in the
// theme's text color (KDDockWidgets' own icons are dark and vanish on a dark theme). The buttons
// themselves stay KDDockWidgets' own: it relies on their type internally.
class DockViewFactory final : public KDDockWidgets::QtWidgets::ViewFactory {
    Q_OBJECT

public:
    QIcon iconForButtonType(KDDockWidgets::TitleBarButtonType type, qreal dpr) const override;
};

}  // namespace mikserstudio::ui
