#pragma once

#include <QColor>
#include <QObject>
#include <QPalette>
#include <QString>

class QApplication;

namespace mikserstudio::ui {

// The single set of design tokens of the UI. Widgets get them through the QSS stylesheet and
// palette built from them, QML panels through ThemeObject (context property "theme").
// Sizes are device-independent pixels, so they scale with the screen's DPI.
struct ThemeTokens {
    // Surfaces, from the window background up to raised controls.
    QColor window;
    QColor panel;
    QColor panelAlt;
    QColor surface;
    QColor hover;
    QColor selection;
    QColor border;
    QColor borderStrong;

    // Text.
    QColor text;
    QColor textMuted;
    QColor textDisabled;

    // Accent and states.
    QColor accent;
    QColor accentHover;
    QColor accentPressed;
    QColor accentText;
    QColor success;
    QColor warning;
    QColor danger;

    // Timeline.
    QColor playhead;
    QColor ruler;
    QColor trackVideo;
    QColor trackAudio;
    QColor clipVideo;
    QColor clipAudio;
    QColor clipImage;

    int spacingXs = 4;
    int spacingSm = 8;
    int spacingMd = 12;
    int spacingLg = 16;
    int spacingXl = 24;

    int radiusSm = 4;
    int radiusMd = 6;
    int radiusLg = 10;

    QString fontFamily;
    QString monoFontFamily;
    int fontSizeSmall = 11;
    int fontSize = 12;
    int fontSizeLarge = 14;
    int fontSizeTitle = 16;
    int controlHeight = 28;
};

// The dark theme. Font families are resolved against the installed fonts on first use, so this
// needs a QGuiApplication.
[[nodiscard]] const ThemeTokens& darkTheme();

[[nodiscard]] QString styleSheet(const ThemeTokens& tokens);
[[nodiscard]] QPalette palette(const ThemeTokens& tokens);

// Call before constructing the QApplication: high-DPI rounding that keeps fractional scale
// factors (125 %, 150 %) crisp.
void prepareApplication();

// Applies the Fusion style, palette, font and stylesheet to `app`, and the matching Fusion
// style to Qt Quick Controls (which must happen before the first QML engine is created).
void applyTheme(QApplication& app, const ThemeTokens& tokens = darkTheme());

// The tokens for QML. Every property is constant.
class ThemeObject final : public QObject {
    Q_OBJECT
    Q_PROPERTY(QColor window READ window CONSTANT)
    Q_PROPERTY(QColor panel READ panel CONSTANT)
    Q_PROPERTY(QColor panelAlt READ panelAlt CONSTANT)
    Q_PROPERTY(QColor surface READ surface CONSTANT)
    Q_PROPERTY(QColor hover READ hover CONSTANT)
    Q_PROPERTY(QColor selection READ selection CONSTANT)
    Q_PROPERTY(QColor border READ border CONSTANT)
    Q_PROPERTY(QColor borderStrong READ borderStrong CONSTANT)
    Q_PROPERTY(QColor text READ text CONSTANT)
    Q_PROPERTY(QColor textMuted READ textMuted CONSTANT)
    Q_PROPERTY(QColor textDisabled READ textDisabled CONSTANT)
    Q_PROPERTY(QColor accent READ accent CONSTANT)
    Q_PROPERTY(QColor accentHover READ accentHover CONSTANT)
    Q_PROPERTY(QColor accentText READ accentText CONSTANT)
    Q_PROPERTY(QColor success READ success CONSTANT)
    Q_PROPERTY(QColor warning READ warning CONSTANT)
    Q_PROPERTY(QColor danger READ danger CONSTANT)
    Q_PROPERTY(QColor playhead READ playhead CONSTANT)
    Q_PROPERTY(QColor ruler READ ruler CONSTANT)
    Q_PROPERTY(QColor trackVideo READ trackVideo CONSTANT)
    Q_PROPERTY(QColor trackAudio READ trackAudio CONSTANT)
    Q_PROPERTY(QColor clipVideo READ clipVideo CONSTANT)
    Q_PROPERTY(QColor clipAudio READ clipAudio CONSTANT)
    Q_PROPERTY(QColor clipImage READ clipImage CONSTANT)
    Q_PROPERTY(int spacingXs READ spacingXs CONSTANT)
    Q_PROPERTY(int spacingSm READ spacingSm CONSTANT)
    Q_PROPERTY(int spacingMd READ spacingMd CONSTANT)
    Q_PROPERTY(int spacingLg READ spacingLg CONSTANT)
    Q_PROPERTY(int radiusSm READ radiusSm CONSTANT)
    Q_PROPERTY(int radiusMd READ radiusMd CONSTANT)
    Q_PROPERTY(int radiusLg READ radiusLg CONSTANT)
    Q_PROPERTY(QString fontFamily READ fontFamily CONSTANT)
    Q_PROPERTY(QString monoFontFamily READ monoFontFamily CONSTANT)
    Q_PROPERTY(int fontSizeSmall READ fontSizeSmall CONSTANT)
    Q_PROPERTY(int fontSize READ fontSize CONSTANT)
    Q_PROPERTY(int fontSizeLarge READ fontSizeLarge CONSTANT)
    Q_PROPERTY(int fontSizeTitle READ fontSizeTitle CONSTANT)

public:
    explicit ThemeObject(ThemeTokens tokens = darkTheme(), QObject* parent = nullptr);

    [[nodiscard]] const ThemeTokens& tokens() const { return m_tokens; }

    [[nodiscard]] QColor window() const { return m_tokens.window; }
    [[nodiscard]] QColor panel() const { return m_tokens.panel; }
    [[nodiscard]] QColor panelAlt() const { return m_tokens.panelAlt; }
    [[nodiscard]] QColor surface() const { return m_tokens.surface; }
    [[nodiscard]] QColor hover() const { return m_tokens.hover; }
    [[nodiscard]] QColor selection() const { return m_tokens.selection; }
    [[nodiscard]] QColor border() const { return m_tokens.border; }
    [[nodiscard]] QColor borderStrong() const { return m_tokens.borderStrong; }
    [[nodiscard]] QColor text() const { return m_tokens.text; }
    [[nodiscard]] QColor textMuted() const { return m_tokens.textMuted; }
    [[nodiscard]] QColor textDisabled() const { return m_tokens.textDisabled; }
    [[nodiscard]] QColor accent() const { return m_tokens.accent; }
    [[nodiscard]] QColor accentHover() const { return m_tokens.accentHover; }
    [[nodiscard]] QColor accentText() const { return m_tokens.accentText; }
    [[nodiscard]] QColor success() const { return m_tokens.success; }
    [[nodiscard]] QColor warning() const { return m_tokens.warning; }
    [[nodiscard]] QColor danger() const { return m_tokens.danger; }
    [[nodiscard]] QColor playhead() const { return m_tokens.playhead; }
    [[nodiscard]] QColor ruler() const { return m_tokens.ruler; }
    [[nodiscard]] QColor trackVideo() const { return m_tokens.trackVideo; }
    [[nodiscard]] QColor trackAudio() const { return m_tokens.trackAudio; }
    [[nodiscard]] QColor clipVideo() const { return m_tokens.clipVideo; }
    [[nodiscard]] QColor clipAudio() const { return m_tokens.clipAudio; }
    [[nodiscard]] QColor clipImage() const { return m_tokens.clipImage; }
    [[nodiscard]] int spacingXs() const { return m_tokens.spacingXs; }
    [[nodiscard]] int spacingSm() const { return m_tokens.spacingSm; }
    [[nodiscard]] int spacingMd() const { return m_tokens.spacingMd; }
    [[nodiscard]] int spacingLg() const { return m_tokens.spacingLg; }
    [[nodiscard]] int radiusSm() const { return m_tokens.radiusSm; }
    [[nodiscard]] int radiusMd() const { return m_tokens.radiusMd; }
    [[nodiscard]] int radiusLg() const { return m_tokens.radiusLg; }
    [[nodiscard]] QString fontFamily() const { return m_tokens.fontFamily; }
    [[nodiscard]] QString monoFontFamily() const { return m_tokens.monoFontFamily; }
    [[nodiscard]] int fontSizeSmall() const { return m_tokens.fontSizeSmall; }
    [[nodiscard]] int fontSize() const { return m_tokens.fontSize; }
    [[nodiscard]] int fontSizeLarge() const { return m_tokens.fontSizeLarge; }
    [[nodiscard]] int fontSizeTitle() const { return m_tokens.fontSizeTitle; }

private:
    ThemeTokens m_tokens;
};

}  // namespace mikserstudio::ui
