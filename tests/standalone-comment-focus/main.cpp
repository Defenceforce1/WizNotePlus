#include <QDebug>
#include <QStringList>
#include <QWebEnginePage>
#include <array>

// Recording collaborators only: no QApplication, clipboard, account, renderer
// or web-view factory is constructed. The command bodies are production code.
struct FakeComments {
    bool focused = false;
    int calls = 0;
    QWebEnginePage::WebAction last = QWebEnginePage::NoWebAction;
    bool hasFocus() const { return focused; }
    void triggerPageAction(QWebEnginePage::WebAction action) { ++calls; last = action; }
};
struct FakeDocument {
    FakeComments* comments = nullptr;
    int peeks = 0;
    FakeComments* existingCommentView() { ++peeks; return comments; }
    // Deliberately no allocating commentView(): adding a factory call to a
    // focus check must fail compilation rather than escape the regression.
};
struct FakeEditor {
    QStringList calls;
    void undo() { calls << "undo"; }
    void redo() { calls << "redo"; }
    void triggerPageAction(QWebEnginePage::WebAction action) { calls << QString::number(int(action)); }
    void editorCommandExecutePastePlainText() { calls << "plain"; }
};
struct FakeCodeEditor {
    static inline bool handled = false;
    static bool undo() { return handled; }
    static bool cut() { return handled; }
    static bool copy() { return handled; }
    static bool paste() { return handled; }
    static bool selectAll() { return handled; }
};
struct FocusHarness {
    FakeDocument document;
    FakeEditor editor;
    FakeDocument* current = &document;
    FakeDocument* currentDocumentView() { return current; }
    FakeEditor* getActiveEditor() { return &editor; }
    void on_actionEditingUndo_triggered();
    void on_actionEditingRedo_triggered();
    void on_actionEditingCut_triggered();
    void on_actionEditingCopy_triggered();
    void on_actionEditingPaste_triggered();
    void on_actionEditingPastePlain_triggered();
    void on_actionEditingSelectAll_triggered();
};
#include "actual-commands.inc"

int main()
{
    using Action = QWebEnginePage;
    struct Case {
        void (FocusHarness::*run)();
        Action::WebAction action;
        QString fallback;
        bool codeEditor;
    };
    const std::array<Case, 7> cases{{
        {&FocusHarness::on_actionEditingUndo_triggered, Action::Undo, "undo", true},
        {&FocusHarness::on_actionEditingRedo_triggered, Action::Redo, "redo", false},
        {&FocusHarness::on_actionEditingCut_triggered, Action::Cut, QString::number(int(Action::Cut)), true},
        {&FocusHarness::on_actionEditingCopy_triggered, Action::Copy, QString::number(int(Action::Copy)), true},
        {&FocusHarness::on_actionEditingPaste_triggered, Action::Paste, QString::number(int(Action::Paste)), true},
        {&FocusHarness::on_actionEditingPastePlain_triggered, Action::Paste, "plain", false},
        {&FocusHarness::on_actionEditingSelectAll_triggered, Action::SelectAll, QString::number(int(Action::SelectAll)), true}
    }};
    int checks = 0, failures = 0;
    const auto check = [&](bool ok, const char* label) {
        ++checks; failures += !ok; qInfo().noquote() << (ok ? "PASS" : "FAIL") << label;
    };
    for (const auto& test : cases) {
        for (int mode = 0; mode < 3; ++mode) {
            FocusHarness h; FakeComments comments; comments.focused = mode == 2;
            h.document.comments = mode == 0 ? nullptr : &comments;
            FakeCodeEditor::handled = false;
            (h.*test.run)();
            check(h.document.peeks == 1 && (mode == 2
                ? comments.calls == 1 && comments.last == test.action && h.editor.calls.isEmpty()
                : comments.calls == 0 && h.editor.calls == QStringList{test.fallback}),
                "actual command preserves editor/comment routing without creating a panel");
        }
        if (test.codeEditor) {
            FocusHarness h; FakeCodeEditor::handled = true; (h.*test.run)();
            check(h.document.peeks == 0 && h.editor.calls.isEmpty(),
                "existing code-editor handler returns before any comment focus check");
        }
    }
    FocusHarness missing; missing.current = nullptr; FakeCodeEditor::handled = false;
    missing.on_actionEditingPastePlain_triggered();
    check(missing.document.peeks == 0 && missing.editor.calls.isEmpty(),
        "plain paste retains its no-document early return");
    qInfo() << "comment-focus checks" << checks << "failures" << failures;
    return failures ? 1 : 0;
}
