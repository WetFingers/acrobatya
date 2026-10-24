// Сцены редактора (план 3.4; эталон 3.9 «Инструмент», 6.2 п. 1, 6.10): edit-mode — «Редактировать PDF» на
// абзацах Word (word-paragraphs.pdf): панель инструмента слева, рамки всех блоков; edit-caret — каретка в
// абзаце и выделенное слово; edit-readonly-block — rotated.pdf: два блока «только чтение» с замком. Кадр —
// когда блоки страницы пришли и видимые тайлы готовы. 3.4 п. 2: edit-format-panel — выделенное слово
// полужирным, панель FORMAT TEXT показывает его стиль; edit-add-text — новый блок «Добавить текст» с
// набранной строкой (локальная копия до записи) и маркерами ширины.
#include <QCoreApplication>
#include <QTimer>

#include <vector>

#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/edit/edit_mode.h"
#include "ui/tools/edit/edit_tool.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

// Окно с документом rel и включённым «Редактировать PDF»; then — после прихода блоков первой страницы.
std::function<QWidget*()> editScene(const QString& rel, std::function<void(EditMode&)> then = {}) {
    return withMainWindow([rel, then](MainWindow& w) {
        w.openFile(sceneFixture(rel));
        QTimer::singleShot(0, &w, [&w, then] {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            ToolRegistry::instance().activate(QStringLiteral("edit"), doc);
            EditMode& mode = EditTool::modeOf(doc->view());
            if (then) {
                QObject::connect(
                    &mode, &EditMode::contentLoaded, &mode,
                    [&mode, then](int page) {
                        if (page == 0) {
                            then(mode);
                        }
                    },
                    Qt::SingleShotConnection);
            }
        });
    });
}

bool editReady(QWidget* w, bool caret) {
    auto* window = static_cast<MainWindow*>(w);
    DocumentView* doc = window->currentDocument();
    if (doc == nullptr || doc->view().tools().tool() != ViewToolId::Edit) {
        return false;
    }
    EditMode& mode = EditTool::modeOf(doc->view());
    return mode.hasContent(0) && (!caret || mode.editing()) && sceneDocumentReady(w);
}

}  // namespace

std::vector<Scene> editScenes() {
    std::vector<Scene> scenes;

    Scene mode;
    mode.name = QStringLiteral("edit-mode");
    mode.make = editScene(QStringLiteral("edit/word-paragraphs.pdf"));
    mode.ready = [](QWidget* w) { return editReady(w, false); };
    scenes.push_back(mode);

    Scene caret;
    caret.name = QStringLiteral("edit-caret");
    caret.make = editScene(QStringLiteral("edit/word-paragraphs.pdf"), [](EditMode& m) {
        const std::vector<EditMode::Frame> frames = m.frames();
        if (frames.size() < 2) {
            return;
        }
        // Второй абзац: выделено третье слово.
        if (m.enterBlock(frames[1].page, frames[1].block, 0)) {
            const edit::ParagraphEditor* e = m.editor();
            int words = 0;
            for (int i = 0; i < e->size(); ++i) {
                const edit::TextRange w = e->wordAt(i);
                if (!w.empty() && w.begin == i && ++words == 3) {
                    m.setSelection(w.begin, w.end);
                    break;
                }
            }
        }
    });
    caret.ready = [](QWidget* w) { return editReady(w, true); };
    scenes.push_back(caret);

    Scene format;
    format.name = QStringLiteral("edit-format-panel");
    format.make = editScene(QStringLiteral("edit/word-paragraphs.pdf"), [](EditMode& m) {
        const std::vector<EditMode::Frame> frames = m.frames();
        if (frames.size() < 2 || !m.enterBlock(frames[1].page, frames[1].block, 0)) {
            return;
        }
        const edit::TextRange w = m.editor()->wordAt(4);  // «editor»
        m.setSelection(w.begin, w.end);
        m.toggleBold();
    });
    format.ready = [](QWidget* w) { return editReady(w, true); };
    scenes.push_back(format);

    Scene add;
    add.name = QStringLiteral("edit-add-text");
    add.make = editScene(QStringLiteral("edit/word-paragraphs.pdf"), [](EditMode& m) {
        QObject::connect(
            &m, &EditMode::editingChanged, &m,
            [&m](bool editing) {
                if (editing) {
                    m.insertText(
                        QCoreApplication::translate("ot::ui::EditScenes", "Новый блок текста — Arial 12"));
                }
            },
            Qt::SingleShotConnection);
        m.addTextAt(0, QPointF(72, 420));
    });
    add.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        DocumentView* doc = window->currentDocument();
        if (doc == nullptr || !editReady(w, true)) {
            return false;
        }
        const EditMode& m = EditTool::modeOf(doc->view());
        return m.editor() != nullptr && !m.editor()->empty() && m.editor()->layoutValid();
    };
    scenes.push_back(add);

    Scene readOnly;
    readOnly.name = QStringLiteral("edit-readonly-block");
    readOnly.make = editScene(QStringLiteral("edit/rotated.pdf"));
    readOnly.ready = [](QWidget* w) { return editReady(w, false); };
    scenes.push_back(readOnly);
    return scenes;
}

}  // namespace ot::ui
