// The Settings window: the built-in plugins that name a section, with a form each, saved through
// plugins.set_settings.
#include "mikserstudio/core/CommandBus.h"
#include "mikserstudio/core/PluginCommands.h"
#include "mikserstudio/core/PluginManager.h"
#include "mikserstudio/core/UndoStack.h"
#include "mikserstudio/ui/SchemaForm.h"

#include "SettingsWindow.h"

#include <catch2/catch_test_macros.hpp>

#include <QDir>
#include <QFile>
#include <QGroupBox>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QPushButton>
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

QByteArray manifest(const QByteArray& id, const QByteArray& name, const QByteArray& section)
{
    const QByteArray sectionField = section.isEmpty() ? QByteArray() : R"("settingsSection": ")" + section + R"(",)";
    return R"({"id": ")" + id + R"(", "name": ")" + name + R"(", "version": "1.0.0", "apiVersion": 1,
        "entry": "main.lua", )" +
           sectionField + R"( "settingsSchema": {"type": "object", "properties": {
            "url": {"type": "string", "title": "Address", "default": "http://127.0.0.1:1"}}}})";
}

}  // namespace

TEST_CASE("The Settings window shows the built-in plugins of each section and saves them", "[plugins]")
{
    QTemporaryDir dir;
    writeManifest(dir.filePath(u"builtin/ollama"_s), manifest("mikserstudio.ollama", "Ollama", "AI tools"));
    writeManifest(dir.filePath(u"builtin/comfyui"_s), manifest("mikserstudio.comfyui", "ComfyUI", "AI tools"));
    writeManifest(dir.filePath(u"builtin/other"_s), manifest("mikserstudio.other", "Other", ""));
    writeManifest(dir.filePath(u"user/mine"_s), manifest("someone.mine", "Mine", "AI tools"));
    mikserstudio::core::UndoStack undo;
    mikserstudio::core::CommandBus bus(undo);
    mikserstudio::core::PluginManager plugins(bus, dir.filePath(u"settings"_s));
    mikserstudio::core::registerPluginCommands(bus, plugins);
    plugins.addSearchPath(dir.filePath(u"builtin"_s), mikserstudio::core::PluginSource::BuiltIn);
    plugins.addSearchPath(dir.filePath(u"user"_s), mikserstudio::core::PluginSource::User);
    plugins.discover();

    mikserstudio::ui::SettingsWindow window(bus, plugins);
    auto* sections = window.findChild<QListWidget*>();
    REQUIRE(sections);
    REQUIRE(sections->count() == 1);  // A plugin without a section, and the person's own, aren't shown.
    CHECK(sections->item(0)->text() == u"AI tools"_s);

    QStringList groups;
    for (const QGroupBox* group : window.findChildren<QGroupBox*>())
        groups.append(group->title());
    groups.sort();
    CHECK(groups == QStringList{u"ComfyUI"_s, u"Ollama"_s});

    QGroupBox* ollama = nullptr;
    for (QGroupBox* group : window.findChildren<QGroupBox*>()) {
        if (group->title() == u"Ollama"_s)
            ollama = group;
    }
    REQUIRE(ollama);
    auto* address = ollama->findChild<QLineEdit*>();
    REQUIRE(address);
    CHECK(address->text() == u"http://127.0.0.1:1"_s);
    address->setText(u"http://192.168.1.5:11434"_s);

    const auto buttons = window.findChildren<QPushButton*>();
    const auto save = std::ranges::find_if(buttons, [](const QPushButton* b) { return b->text() == u"Save"_s; });
    REQUIRE(save != buttons.end());
    (*save)->click();

    CHECK(plugins.settings(u"mikserstudio.ollama"_s).value(u"url").toString() == u"http://192.168.1.5:11434"_s);
    CHECK(plugins.settings(u"mikserstudio.comfyui"_s).value(u"url").toString() == u"http://127.0.0.1:1"_s);
    QStringList labels;
    for (const QLabel* label : window.findChildren<QLabel*>())
        labels.append(label->text());
    CHECK(labels.contains(u"Settings saved. Jobs asked for from now on use them."_s));
}
