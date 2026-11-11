#include "mikserstudio/ui/Theme.h"

#include <QApplication>
#include <QFont>
#include <QFontDatabase>
#include <QQuickStyle>
#include <QStringList>
#include <QStyleFactory>
#include <QStyleHints>

using namespace Qt::StringLiterals;

namespace mikserstudio::ui {

namespace {

QString firstInstalled(const QStringList& candidates, QFontDatabase::SystemFont fallback)
{
    const QStringList installed = QFontDatabase::families();
    for (const QString& family : candidates) {
        if (installed.contains(family, Qt::CaseInsensitive))
            return family;
    }
    return QFontDatabase::systemFont(fallback).family();
}

ThemeTokens makeDarkTheme()
{
    ThemeTokens t;
    t.window = QColor(0x12, 0x14, 0x17);
    t.panel = QColor(0x1a, 0x1d, 0x21);
    t.panelAlt = QColor(0x20, 0x23, 0x28);
    t.surface = QColor(0x26, 0x2a, 0x30);
    t.hover = QColor(0x2d, 0x32, 0x3a);
    t.selection = QColor(0x2b, 0x45, 0x66);
    t.border = QColor(0x2a, 0x2e, 0x35);
    t.borderStrong = QColor(0x3a, 0x40, 0x49);

    t.text = QColor(0xe4, 0xe7, 0xeb);
    t.textMuted = QColor(0x9a, 0xa2, 0xad);
    t.textDisabled = QColor(0x5f, 0x66, 0x70);

    t.accent = QColor(0x4f, 0x8c, 0xf7);
    t.accentHover = QColor(0x6a, 0x9f, 0xfa);
    t.accentPressed = QColor(0x3d, 0x77, 0xe0);
    t.accentText = QColor(0xff, 0xff, 0xff);
    t.success = QColor(0x3f, 0xb3, 0x80);
    t.warning = QColor(0xe0, 0xa4, 0x3a);
    t.danger = QColor(0xe5, 0x5b, 0x55);

    t.playhead = QColor(0xf2, 0x6d, 0x5b);
    t.ruler = QColor(0x16, 0x18, 0x1c);
    t.trackVideo = QColor(0x1c, 0x20, 0x26);
    t.trackAudio = QColor(0x1a, 0x21, 0x1f);
    t.clipVideo = QColor(0x3f, 0x6f, 0xb5);
    t.clipAudio = QColor(0x3a, 0x93, 0x70);
    t.clipImage = QColor(0x85, 0x62, 0xc2);

    t.fontFamily = firstInstalled({u"Segoe UI Variable Text"_s, u"Segoe UI"_s, u"Inter"_s, u"Noto Sans"_s,
                                   u"SF Pro Text"_s, u"Cantarell"_s},
                                  QFontDatabase::GeneralFont);
    t.monoFontFamily = firstInstalled({u"Cascadia Mono"_s, u"JetBrains Mono"_s, u"Consolas"_s,
                                       u"DejaVu Sans Mono"_s, u"Menlo"_s},
                                      QFontDatabase::FixedFont);
    return t;
}

}  // namespace

const ThemeTokens& darkTheme()
{
    static const ThemeTokens tokens = makeDarkTheme();
    return tokens;
}

QPalette palette(const ThemeTokens& t)
{
    QPalette p;
    p.setColor(QPalette::Window, t.panel);
    p.setColor(QPalette::WindowText, t.text);
    p.setColor(QPalette::Base, t.surface);
    p.setColor(QPalette::AlternateBase, t.panelAlt);
    p.setColor(QPalette::ToolTipBase, t.surface);
    p.setColor(QPalette::ToolTipText, t.text);
    p.setColor(QPalette::PlaceholderText, t.textDisabled);
    p.setColor(QPalette::Text, t.text);
    p.setColor(QPalette::Button, t.surface);
    p.setColor(QPalette::ButtonText, t.text);
    p.setColor(QPalette::BrightText, t.accentText);
    p.setColor(QPalette::Light, t.borderStrong);
    p.setColor(QPalette::Midlight, t.border);
    p.setColor(QPalette::Mid, t.border);
    p.setColor(QPalette::Dark, t.window);
    p.setColor(QPalette::Shadow, QColor(0, 0, 0));
    p.setColor(QPalette::Highlight, t.accent);
    p.setColor(QPalette::HighlightedText, t.accentText);
    p.setColor(QPalette::Link, t.accentHover);
    p.setColor(QPalette::LinkVisited, t.accent);
    p.setColor(QPalette::Accent, t.accent);
    for (const auto role : {QPalette::WindowText, QPalette::Text, QPalette::ButtonText}) {
        p.setColor(QPalette::Disabled, role, t.textDisabled);
    }
    p.setColor(QPalette::Disabled, QPalette::Highlight, t.hover);
    return p;
}

void prepareApplication()
{
    QGuiApplication::setHighDpiScaleFactorRoundingPolicy(Qt::HighDpiScaleFactorRoundingPolicy::PassThrough);
}

void applyTheme(QApplication& app, const ThemeTokens& tokens)
{
    // Dark native title bars and dialogs to match the dark theme.
    QGuiApplication::styleHints()->setColorScheme(Qt::ColorScheme::Dark);
    if (QStyle* fusion = QStyleFactory::create(u"Fusion"_s))
        QApplication::setStyle(fusion);
    QApplication::setPalette(palette(tokens));
    QFont font(tokens.fontFamily);
    font.setPixelSize(tokens.fontSize);
    QApplication::setFont(font);
    app.setStyleSheet(styleSheet(tokens));
    QQuickStyle::setStyle(u"Fusion"_s);
}

ThemeObject::ThemeObject(const ThemeTokens& tokens, QObject* parent)
    : QObject(parent)
    , m_tokens(tokens)
{
}

}  // namespace mikserstudio::ui
