// The stand-in for a panel of a saved workspace whose owner, usually a plugin, is not installed.
#pragma once

#include <QWidget>

namespace mikserstudio::ui {

// Stands in for a panel of a saved workspace whose owner (usually a plugin) is not installed,
// so the workspace keeps its shape and the panel comes back when the owner does.
class PlaceholderPanel final : public QWidget {
    Q_OBJECT

public:
    explicit PlaceholderPanel(const QString& panelId, QWidget* parent = nullptr);

    [[nodiscard]] QString panelId() const { return m_panelId; }

    // "This panel comes from <owner>, which is not installed." where <owner> is the part of the
    // panel id before '/', or the whole id when it has none.
    [[nodiscard]] static QString message(const QString& panelId);
    // The title used for the dock tab: the panel part of the id.
    [[nodiscard]] static QString title(const QString& panelId);

private:
    QString m_panelId;
};

}  // namespace mikserstudio::ui
