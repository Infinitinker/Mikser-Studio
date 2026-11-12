#include "TestSupport.h"

#include "mikserstudio/core/UndoStack.h"

#include <catch2/catch_test_macros.hpp>

#include <stdexcept>

using namespace Qt::StringLiterals;
using mikserstudio::core::UndoableEdit;
using mikserstudio::core::UndoStack;

namespace {

// Adds `delta` to a counter; the edit is pushed after the change was applied.
class AddEdit final : public UndoableEdit {
public:
    AddEdit(int& value, int delta)
        : m_value(value)
        , m_delta(delta)
    {
    }
    void undo() override { m_value -= m_delta; }
    void redo() override { m_value += m_delta; }
    [[nodiscard]] QString text() const override { return u"Add %1"_s.arg(m_delta); }

private:
    int& m_value;
    int m_delta;
};

void apply(UndoStack& stack, int& value, int delta)
{
    value += delta;
    stack.push(std::make_unique<AddEdit>(value, delta));
}

}  // namespace

TEST_CASE("UndoStack starts empty", "[undo]")
{
    UndoStack stack;
    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.canRedo());
    CHECK(stack.undoText().isEmpty());
    CHECK(stack.redoText().isEmpty());
    CHECK_FALSE(stack.undo());
    CHECK_FALSE(stack.redo());
}

TEST_CASE("UndoStack undoes and redoes in order", "[undo]")
{
    UndoStack stack;
    int value = 0;
    apply(stack, value, 1);
    apply(stack, value, 10);
    REQUIRE(value == 11);
    CHECK(stack.undoText() == u"Add 10"_s);

    REQUIRE(stack.undo());
    CHECK(value == 1);
    CHECK(stack.undoText() == u"Add 1"_s);
    CHECK(stack.redoText() == u"Add 10"_s);

    REQUIRE(stack.undo());
    CHECK(value == 0);
    CHECK_FALSE(stack.canUndo());

    REQUIRE(stack.redo());
    REQUIRE(stack.redo());
    CHECK(value == 11);
    CHECK_FALSE(stack.canRedo());
}

TEST_CASE("UndoStack discards the redo branch on push", "[undo]")
{
    UndoStack stack;
    int value = 0;
    apply(stack, value, 1);
    apply(stack, value, 2);
    REQUIRE(stack.undo());
    apply(stack, value, 5);
    CHECK(value == 6);
    CHECK_FALSE(stack.canRedo());
    REQUIRE(stack.undo());
    REQUIRE(stack.undo());
    CHECK(value == 0);
    CHECK_FALSE(stack.canUndo());
}

TEST_CASE("UndoStack emits changed and can be cleared", "[undo]")
{
    UndoStack stack;
    int changes = 0;
    QObject::connect(&stack, &UndoStack::changed, [&changes] { ++changes; });
    int value = 0;
    apply(stack, value, 3);
    REQUIRE(stack.undo());
    REQUIRE(stack.redo());
    CHECK(changes == 3);

    stack.push(nullptr);
    CHECK(changes == 3);

    stack.clear();
    CHECK(changes == 4);
    CHECK_FALSE(stack.canUndo());
    CHECK(value == 3);
    stack.clear();
    CHECK(changes == 4);
}

TEST_CASE("UndoStack groups edits into one compound edit", "[undo]")
{
    UndoStack stack;
    int changes = 0;
    QObject::connect(&stack, &UndoStack::changed, [&changes] { ++changes; });
    int value = 0;

    stack.beginGroup(u"Outer"_s);
    apply(stack, value, 1);
    stack.beginGroup(u"Inner"_s);
    apply(stack, value, 10);
    CHECK(stack.isGrouping());
    CHECK_FALSE(stack.canUndo());
    CHECK_FALSE(stack.undo());  // Not while recording.
    stack.endGroup();
    apply(stack, value, 100);
    CHECK(changes == 0);
    stack.endGroup();

    CHECK_FALSE(stack.isGrouping());
    CHECK(changes == 1);
    CHECK(value == 111);
    CHECK(stack.undoText() == u"Outer"_s);
    REQUIRE(stack.undo());
    CHECK(value == 0);
    CHECK_FALSE(stack.canUndo());
    CHECK(stack.redoText() == u"Outer"_s);
    REQUIRE(stack.redo());
    CHECK(value == 111);
}

TEST_CASE("UndoStack reverts grouped edits in reverse order", "[undo]")
{
    // Each edit records its name when undone or redone.
    class LogEdit final : public UndoableEdit {
    public:
        LogEdit(QStringList& log, QString name)
            : m_log(log)
            , m_name(std::move(name))
        {
        }
        void undo() override { m_log.append(u"undo "_s + m_name); }
        void redo() override { m_log.append(u"redo "_s + m_name); }
        [[nodiscard]] QString text() const override { return m_name; }

    private:
        QStringList& m_log;
        QString m_name;
    };

    UndoStack stack;
    QStringList log;
    {
        mikserstudio::core::UndoGroup group(stack, u"Both"_s);
        stack.push(std::make_unique<LogEdit>(log, u"a"_s));
        stack.push(std::make_unique<LogEdit>(log, u"b"_s));
    }
    REQUIRE(stack.undo());
    REQUIRE(stack.redo());
    CHECK(log == QStringList{u"undo b"_s, u"undo a"_s, u"redo a"_s, u"redo b"_s});
}

TEST_CASE("UndoStack handles empty, unbalanced and interrupted groups", "[undo]")
{
    UndoStack stack;
    int changes = 0;
    QObject::connect(&stack, &UndoStack::changed, [&changes] { ++changes; });
    int value = 0;

    SECTION("an empty group pushes nothing and does not keep its text")
    {
        stack.beginGroup(u"Nothing"_s);
        stack.endGroup();
        CHECK(changes == 0);
        CHECK_FALSE(stack.canUndo());
        stack.beginGroup(QString());
        apply(stack, value, 1);
        stack.endGroup();
        CHECK(stack.undoText().isEmpty());
    }
    SECTION("endGroup without beginGroup is ignored")
    {
        stack.endGroup();
        CHECK_FALSE(stack.isGrouping());
        apply(stack, value, 1);
        CHECK(stack.undoText() == u"Add 1"_s);
    }
    SECTION("the RAII group closes when an exception leaves the scope")
    {
        CHECK_THROWS_AS(
            [&] {
                const mikserstudio::core::UndoGroup group(stack, u"Failing"_s);
                apply(stack, value, 5);
                throw std::runtime_error("stop");
            }(),
            std::runtime_error);
        CHECK_FALSE(stack.isGrouping());
        CHECK(stack.undoText() == u"Failing"_s);
    }
    SECTION("clear drops the edits collected so far but keeps the group open")
    {
        stack.beginGroup(u"Group"_s);
        apply(stack, value, 1);
        stack.clear();
        CHECK(stack.isGrouping());
        apply(stack, value, 2);
        stack.endGroup();
        REQUIRE(stack.undo());
        CHECK(value == 1);
        CHECK_FALSE(stack.canUndo());
    }
}
