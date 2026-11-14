// The widget style sheet of the theme, with its color tokens filled in.
#include "mikserstudio/ui/Theme.h"

#include <QList>
#include <QPair>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

// Widget stylesheet. "@token@" placeholders are replaced with the theme's values; the dynamic
// properties used here ("role", "state") are set by the panels.
constexpr auto kTemplate = uR"QSS(
* {
    outline: none;
}
QMainWindow, QDialog {
    background: @window@;
}
QWidget {
    color: @text@;
    font-family: "@font@";
    font-size: @fontSize@px;
}
QToolTip {
    background: @surface@;
    color: @text@;
    border: 1px solid @borderStrong@;
    border-radius: @radiusSm@px;
    padding: 4px 8px;
}

/* Menus */
QMenuBar {
    background: @window@;
    border-bottom: 1px solid @border@;
    padding: 2px 6px;
}
QMenuBar::item {
    background: transparent;
    padding: 5px 10px;
    border-radius: @radiusSm@px;
}
QMenuBar::item:selected, QMenuBar::item:pressed {
    background: @hover@;
}
QMenu {
    background: @panelAlt@;
    border: 1px solid @borderStrong@;
    border-radius: @radiusMd@px;
    padding: 6px;
}
QMenu::item {
    padding: 6px 28px 6px 24px;
    border-radius: @radiusSm@px;
}
QMenu::item:selected {
    background: @selection@;
}
QMenu::item:disabled {
    color: @textDisabled@;
}
QMenu::separator {
    height: 1px;
    background: @border@;
    margin: 5px 8px;
}
QMenu::indicator {
    width: 14px;
    height: 14px;
    left: 6px;
}

/* Status bar */
QStatusBar {
    background: @window@;
    border-top: 1px solid @border@;
    color: @textMuted@;
    min-height: 24px;
}
QStatusBar::item {
    border: none;
}
QLabel[role="status"] {
    padding: 0 8px;
    color: @textMuted@;
}
QLabel[role="status"][state="error"] {
    color: @danger@;
}
QLabel[role="status"][state="ok"] {
    color: @text@;
}

/* Panels */
QWidget[role="panel"] {
    background: @panel@;
}
QLabel[role="heading"] {
    font-size: @fontSizeLarge@px;
    font-weight: 600;
    color: @text@;
}
QLabel[role="muted"], QLabel[role="hint"] {
    color: @textMuted@;
}
QLabel[role="hint"] {
    font-size: @fontSizeSmall@px;
}
QLabel[role="error"] {
    color: @danger@;
    font-size: @fontSizeSmall@px;
}
QLabel[role="placeholder"] {
    color: @textMuted@;
    font-size: @fontSizeLarge@px;
}
QFrame[role="card"] {
    background: @panelAlt@;
    border: 1px solid @border@;
    border-radius: @radiusMd@px;
}
QFrame[role="separator"] {
    background: @border@;
    max-height: 1px;
    border: none;
}

/* Buttons */
QPushButton, QToolButton {
    background: @surface@;
    border: 1px solid @borderStrong@;
    border-radius: @radiusSm@px;
    padding: 4px 12px;
    min-height: 20px;
}
QPushButton:hover, QToolButton:hover {
    background: @hover@;
}
QPushButton:pressed, QToolButton:pressed {
    background: @selection@;
}
QPushButton:disabled, QToolButton:disabled {
    color: @textDisabled@;
    border-color: @border@;
    background: @panelAlt@;
}
QPushButton[role="primary"] {
    background: @accent@;
    border-color: @accent@;
    color: @accentText@;
    font-weight: 600;
}
QPushButton[role="primary"]:hover {
    background: @accentHover@;
}
QPushButton[role="primary"]:pressed {
    background: @accentPressed@;
}
QPushButton[role="primary"]:disabled {
    background: @hover@;
    border-color: @hover@;
    color: @textDisabled@;
}
QToolButton[role="flat"] {
    background: transparent;
    border: none;
    padding: 2px 6px;
}
QToolButton[role="flat"]:hover {
    background: @hover@;
}

/* Inputs */
QLineEdit, QPlainTextEdit, QTextEdit, QSpinBox, QDoubleSpinBox, QComboBox {
    background: @surface@;
    border: 1px solid @border@;
    border-radius: @radiusSm@px;
    padding: 3px 8px;
    min-height: 20px;
    selection-background-color: @accent@;
    selection-color: @accentText@;
}
QLineEdit:hover, QPlainTextEdit:hover, QTextEdit:hover, QSpinBox:hover, QDoubleSpinBox:hover, QComboBox:hover {
    border-color: @borderStrong@;
}
QLineEdit:focus, QPlainTextEdit:focus, QTextEdit:focus, QSpinBox:focus, QDoubleSpinBox:focus, QComboBox:focus {
    border-color: @accent@;
}
QLineEdit:disabled, QPlainTextEdit:disabled, QSpinBox:disabled, QDoubleSpinBox:disabled, QComboBox:disabled {
    color: @textDisabled@;
    background: @panelAlt@;
}
QLineEdit[state="invalid"], QSpinBox[state="invalid"], QDoubleSpinBox[state="invalid"], QComboBox[state="invalid"] {
    border-color: @danger@;
}
QComboBox::drop-down {
    border: none;
    width: 20px;
}
QComboBox QAbstractItemView {
    background: @panelAlt@;
    border: 1px solid @borderStrong@;
    selection-background-color: @selection@;
    padding: 4px;
}
QSpinBox::up-button, QSpinBox::down-button, QDoubleSpinBox::up-button, QDoubleSpinBox::down-button {
    width: 16px;
    border: none;
    background: transparent;
}
QCheckBox {
    spacing: 8px;
}
QGroupBox {
    border: 1px solid @border@;
    border-radius: @radiusMd@px;
    margin-top: 14px;
    padding: 10px 8px 8px 8px;
}
QGroupBox::title {
    subcontrol-origin: margin;
    left: 10px;
    padding: 0 4px;
    color: @textMuted@;
}

/* Item views */
QAbstractItemView {
    background: @panel@;
    alternate-background-color: @panelAlt@;
    border: none;
    selection-background-color: @selection@;
    selection-color: @text@;
}
QAbstractItemView::item {
    padding: 4px 6px;
    border: none;
}
QAbstractItemView::item:hover {
    background: @hover@;
}
QAbstractItemView::item:selected {
    background: @selection@;
}
QHeaderView {
    background: @panel@;
}
QHeaderView::section {
    background: @panel@;
    color: @textMuted@;
    border: none;
    border-bottom: 1px solid @border@;
    padding: 5px 8px;
    font-size: @fontSizeSmall@px;
    font-weight: 600;
}
QProgressBar {
    background: @surface@;
    border: none;
    border-radius: 3px;
    max-height: 6px;
    text-align: center;
    color: transparent;
}
QProgressBar::chunk {
    background: @accent@;
    border-radius: 3px;
}

/* Scroll bars */
QScrollBar:vertical {
    background: transparent;
    width: 10px;
    margin: 2px;
}
QScrollBar:horizontal {
    background: transparent;
    height: 10px;
    margin: 2px;
}
QScrollBar::handle {
    background: @borderStrong@;
    border-radius: 3px;
    min-height: 24px;
    min-width: 24px;
}
QScrollBar::handle:hover {
    background: @textDisabled@;
}
QScrollBar::add-line, QScrollBar::sub-line, QScrollBar::add-page, QScrollBar::sub-page {
    background: none;
    border: none;
    width: 0;
    height: 0;
}

/* Docking (KDDockWidgets) */
KDDockWidgets--QtWidgets--MainWindow {
    background: @window@;
}
KDDockWidgets--QtWidgets--Group {
    background: @panel@;
    border: 1px solid @border@;
    border-radius: @radiusMd@px;
}
KDDockWidgets--QtWidgets--TitleBar {
    background: @panelAlt@;
    color: @textMuted@;
    border: none;
    border-top-left-radius: @radiusMd@px;
    border-top-right-radius: @radiusMd@px;
}
KDDockWidgets--QtWidgets--TitleBar QAbstractButton,
KDDockWidgets--QtWidgets--Stack > QWidget > QAbstractButton {
    background: transparent;
    border: none;
    padding: 2px;
    border-radius: @radiusSm@px;
    min-height: 0;
    min-width: 0;
}
KDDockWidgets--QtWidgets--TitleBar QAbstractButton:hover,
KDDockWidgets--QtWidgets--Stack > QWidget > QAbstractButton:hover {
    background: @hover@;
}
KDDockWidgets--QtWidgets--Stack::pane {
    border: none;
}
KDDockWidgets--QtWidgets--TabBar {
    background: @panelAlt@;
}
KDDockWidgets--QtWidgets--TabBar::tab {
    background: transparent;
    color: @textMuted@;
    border: none;
    border-bottom: 2px solid transparent;
    padding: 6px 14px;
    margin: 0;
}
KDDockWidgets--QtWidgets--TabBar::tab:hover {
    color: @text@;
    background: @hover@;
}
KDDockWidgets--QtWidgets--TabBar::tab:selected {
    color: @text@;
    border-bottom: 2px solid @accent@;
}
KDDockWidgets--QtWidgets--Separator {
    background: @window@;
}
KDDockWidgets--QtWidgets--FloatingWindow {
    background: @window@;
    border: 1px solid @borderStrong@;
}
)QSS";

}  // namespace

QString styleSheet(const ThemeTokens& t)
{
    const QList<QPair<QString, QString>> values{
        {u"window"_s, t.window.name()},
        {u"panelAlt"_s, t.panelAlt.name()},
        {u"panel"_s, t.panel.name()},
        {u"surface"_s, t.surface.name()},
        {u"hover"_s, t.hover.name()},
        {u"selection"_s, t.selection.name()},
        {u"borderStrong"_s, t.borderStrong.name()},
        {u"border"_s, t.border.name()},
        {u"textMuted"_s, t.textMuted.name()},
        {u"textDisabled"_s, t.textDisabled.name()},
        {u"text"_s, t.text.name()},
        {u"accentHover"_s, t.accentHover.name()},
        {u"accentPressed"_s, t.accentPressed.name()},
        {u"accentText"_s, t.accentText.name()},
        {u"accent"_s, t.accent.name()},
        {u"danger"_s, t.danger.name()},
        {u"radiusSm"_s, QString::number(t.radiusSm)},
        {u"radiusMd"_s, QString::number(t.radiusMd)},
        {u"fontSizeSmall"_s, QString::number(t.fontSizeSmall)},
        {u"fontSizeLarge"_s, QString::number(t.fontSizeLarge)},
        {u"fontSize"_s, QString::number(t.fontSize)},
        {u"font"_s, t.fontFamily},
    };
    QString css = QString(kTemplate);
    for (const auto& [name, value] : values)
        css.replace(u'@' + name + u'@', value);
    return css;
}

}  // namespace mikserstudio::ui
