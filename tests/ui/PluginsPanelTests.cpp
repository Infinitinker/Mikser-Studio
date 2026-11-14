// The Plugins panel: each plugin listed by its name and version, and a plugin whose manifest is
// broken by its id, with the reason it failed.
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"

#include "PluginsPanel.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QLabel>
#include <QListWidget>
#include <QTemporaryDir>

#include <algorithm>

using namespace Qt::StringLiterals;

namespace {

void writeManifest(const QString& folder, const QByteArray& content)
{
    REQUIRE(QDir().mkpath(folder));
    QFile file(folder + u"/mikserstudio-plugin.json"_s);
    REQUIRE(file.open(QIODevice::WriteOnly));
    file.write(content);
}

}  // namespace

TEST_CASE("The Plugins panel names a plugin with a broken manifest by its id", "[plugins]")
{
    QTemporaryDir dir;
    writeManifest(dir.filePath(u"plugins/good"_s), R"({"id": "mikserstudio.tests.good", "name": "Good",
        "version": "1.2.0", "apiVersion": 1, "entry": "main.lua"})");
    writeManifest(dir.filePath(u"plugins/broken"_s), R"({"id": "x.broken", "name": "Broken", "kind": "native",
        "version": "1.0.0", "apiVersion": 1, "entry": "main.lua"})");
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::core::PluginManager plugins(bus, dir.filePath(u"settings"_s));
    plugins.addSearchPath(dir.filePath(u"plugins"_s));
    plugins.discover();

    mikserstudio::ui::PluginsPanel panel(bus, plugins);
    auto* list = panel.findChild<QListWidget*>();
    REQUIRE(list);
    REQUIRE(list->count() == 2);
    CHECK(list->item(0)->text() == u"broken"_s);  // The folder's name: a broken manifest's id isn't trusted.
    CHECK(list->item(1)->text() == u"Good  1.2.0"_s);

    list->setCurrentRow(0);
    QStringList labels;
    for (const QLabel* label : panel.findChildren<QLabel*>())
        labels.append(label->text());
    CHECK(labels.contains(u"broken"_s));
    CHECK(labels.contains(u"broken  ·  No permissions"_s));
    CHECK(std::ranges::any_of(labels, [](const QString& text) { return text.contains(u"Lua plugins only"_s); }));
}
