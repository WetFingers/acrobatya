// Сцены комментариев (план 5.1; эталон 3.4, 6.2 п. 3, 6.10) на абзацах Word (word-paragraphs.pdf) с
// включённым «Добавить комментарии»: annot-note-open — записка с открытым окном и текстом (и тёмная);
// annot-markup — выделение, подчёркивание, зачёркивание и волнистая на строках первого абзаца; annot-caret —
// вставка текста и «Заменить текст» (зачёркнутое слово и каретка) с окном нового текста; annot-quick-actions
// — выделенный «Выделением» текст и панель быстрых действий над ним. Комментарии создаются в открытом
// документе (на закрытии — «Не сохранять»); кадр — когда последний шаг сделан и тайлы перерисованы.
#include <QApplication>
#include <QCoreApplication>
#include <QFuture>
#include <QMouseEvent>
#include <QTimer>

#include <cmath>
#include <memory>
#include <vector>

#include "core/text/selection.h"
#include "core/text/stext.h"
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/comment/quick_actions_bar.h"
#include "ui/tools/tool_registry.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

constexpr const char* kDoneProperty = "commentSceneDone";

using TextPtr = std::shared_ptr<const TextPage>;
using SceneStep = std::function<void(MainWindow&, DocumentView&, const TextPtr&)>;

CommentTool& sceneCommentTool() {
    return *static_cast<CommentTool*>(ToolRegistry::instance().find(QStringLiteral("comment")));
}

void markDone(MainWindow& w) { w.setProperty(kDoneProperty, true); }

// Текст демонстрационных комментариев (по-русски — через перевод, как строки интерфейса).
std::string sceneText(const char* text) {
    return QCoreApplication::translate("ot::ui::CommentScenes", text).toStdString();
}

// Окно с документом rel и «Добавить комментарии»; then — когда текстовый слой первой страницы готов.
std::function<QWidget*()> commentScene(const QString& rel, SceneStep then) {
    return withMainWindow([rel, then](MainWindow& w) {
        w.setProperty(kDoneProperty, false);
        w.openFile(sceneFixture(rel));
        QTimer::singleShot(0, &w, [&w, then] {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            ToolRegistry::instance().activate(QStringLiteral("comment"), doc);
            doc->session().textPage(0).then(&w, [&w, then](TextPtr text) {
                DocumentView* d = w.currentDocument();
                if (d != nullptr && text) {
                    then(w, *d, text);
                }
            });
        });
    });
}

bool commentReady(QWidget* w) { return w->property(kDoneProperty).toBool() && sceneDocumentReady(w); }

// Квады строки line (все символы).
std::vector<QuadF> lineQuads(const TextPage& text, int line, int skip = 0, int count = -1) {
    if (line < 0 || line >= static_cast<int>(text.lines().size())) {
        return {};
    }
    const TextLine& l = text.lines()[static_cast<std::size_t>(line)];
    const int first = l.firstChar + std::min(skip, l.charCount - 1);
    const int last = count < 0 ? l.firstChar + l.charCount - 1
                               : std::min(first + count - 1, l.firstChar + l.charCount - 1);
    return selectRange(text, first, last).quads;
}

annot::AnnotProps sceneProps(CommentAction action) {
    annot::AnnotProps p =
        comments::newProps(Settings(), CommentMode::typeOf(action), CommentMode::actionKey(action));
    p.author = sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Мария Иванова"));
    return p;
}

void openPopupThenDone(MainWindow& w, DocumentView& doc, QFuture<annot::AnnotRef> future) {
    PageView* view = &doc.view();
    future.then(&w, [&w, view](annot::AnnotRef ref) {
        PopupLayer& popups = PopupLayer::of(*view);
        QObject::connect(&popups, &PopupLayer::opened, &w, [&w] { markDone(w); }, Qt::SingleShotConnection);
        popups.open(ref, false);
    });
}

void noteScene(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    sceneCommentTool().start(CommentAction::Note);
    const QSizeF size = doc.view().pageSizePoints(0);
    float y = 120;
    if (!text->lines().empty()) {
        y = text->lines().front().bbox.y0 + 10;
    }
    const auto x = static_cast<float>(size.width() - 70);
    annot::AnnotProps note = sceneProps(CommentAction::Note);
    note.rect = {x - 10, y - 10, x + 10, y + 10};
    note.popupRect = {x - 250, y + 20, x - 10, y + 140};
    note.contents = sceneText(QT_TRANSLATE_NOOP(
        "ot::ui::CommentScenes", "Проверить формулировку первого абзаца и уточнить сроки с отделом продаж."));
    openPopupThenDone(w, doc, annot::create(doc.session(), 0, std::move(note)));
}

void markupScene(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    sceneCommentTool().start(CommentAction::Highlight);
    const CommentAction actions[] = {CommentAction::Highlight, CommentAction::Underline,
                                     CommentAction::StrikeOut, CommentAction::Squiggly};
    QFuture<annot::AnnotRef> last;
    int line = 0;
    for (const CommentAction a : actions) {
        annot::AnnotProps p = sceneProps(a);
        p.quads = lineQuads(*text, line, 0, line % 2 == 0 ? -1 : 24);
        line += 1;
        if (p.quads.empty()) {
            continue;
        }
        last = annot::create(doc.session(), 0, std::move(p));
    }
    last.then(&w, [&w](annot::AnnotRef) { markDone(w); });
}

void caretScene(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    sceneCommentTool().start(CommentAction::ReplaceText);
    if (text->lines().size() < 3) {
        markDone(w);
        return;
    }
    const TextLine& l0 = text->lines()[0];
    const int at = l0.firstChar + std::min(12, l0.charCount - 1);
    const TextChar& c = text->chars()[static_cast<std::size_t>(at)];
    annot::AnnotProps insert = sceneProps(CommentAction::InsertText);
    insert.type = annot::AnnotType::Caret;
    const RectF box = c.quad.bounds();
    insert.rect = {box.x0 - c.size * 0.3f, c.origin.y - c.size * 0.5f, box.x0 + c.size * 0.3f,
                   c.origin.y + c.size * 0.1f};
    insert.contents = sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "ежеквартально "));
    insert.subject = "Inserted Text";
    annot::create(doc.session(), 0, std::move(insert));

    annot::AnnotProps strike = sceneProps(CommentAction::ReplaceText);
    strike.type = annot::AnnotType::StrikeOut;
    strike.quads = lineQuads(*text, 2, 4, 9);
    strike.intent = "StrikeOutTextEdit";
    const TextLine& l2 = text->lines()[2];
    const int end = l2.firstChar + std::min(4 + 9, l2.charCount - 1);
    const TextChar& e = text->chars()[static_cast<std::size_t>(end)];
    annot::AnnotProps caret = sceneProps(CommentAction::InsertText);
    caret.type = annot::AnnotType::Caret;
    const RectF eb = e.quad.bounds();
    caret.rect = {eb.x0 - e.size * 0.3f, e.origin.y - e.size * 0.5f, eb.x0 + e.size * 0.3f,
                  e.origin.y + e.size * 0.1f};
    caret.contents = sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "новые условия"));
    caret.subject = "Replace Text";
    std::vector<annot::AnnotProps> group{std::move(caret), std::move(strike)};
    openPopupThenDone(
        w, doc,
        annot::createGroup(doc.session(), 0, std::move(group), QString::fromLatin1(annot::kReplaceTextStep))
            .then([](const std::vector<annot::AnnotRef>& refs) {
                return refs.empty() ? annot::AnnotRef{} : refs.front();
            }));
}

void sendMouse(QWidget* target, QEvent::Type type, QPointF pos, Qt::MouseButtons buttons) {
    QMouseEvent event(type, pos, target->mapToGlobal(pos), Qt::LeftButton, buttons, Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void quickActionsScene(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    PageView& v = doc.view();
    v.tools().setTool(ViewToolId::Select);
    v.tools().select().textPage(0);
    if (text->lines().size() < 2) {
        markDone(w);
        return;
    }
    const RectF line = text->lines()[1].bbox;
    auto* poll = new QTimer(&w);
    poll->start(50);
    QObject::connect(poll, &QTimer::timeout, &w, [&w, &v, poll, line] {
        if (!v.tools().select().hasTextPage(0)) {
            return;
        }
        poll->deleteLater();
        const QTransform toView = v.pageTransform(0);
        const QPointF a = toView.map(QPointF(line.x0 + 1, (line.y0 + line.y1) / 2));
        const QPointF b = toView.map(QPointF(line.x0 + (line.x1 - line.x0) * 0.6, (line.y0 + line.y1) / 2));
        QWidget* vp = v.viewport();
        sendMouse(vp, QEvent::MouseButtonPress, a, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, (a + b) / 2, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseMove, b, Qt::LeftButton);
        sendMouse(vp, QEvent::MouseButtonRelease, b, Qt::NoButton);
        QTimer::singleShot(100, &w, [&w] { markDone(w); });
    });
}

// Волна от (x0, y) шириной width с размахом amp.
annot::Stroke wave(float x0, float y, float width, float amp) {
    annot::Stroke s;
    for (int i = 0; i <= 24; ++i) {
        const float t = static_cast<float>(i) / 24.0f;
        s.push_back({x0 + width * t, y + amp * std::sin(t * 6.2832f * 2)});
    }
    return s;
}

void inkScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    sceneCommentTool().start(CommentAction::Ink);
    annot::AnnotProps a = sceneProps(CommentAction::Ink);
    a.ink = {wave(140, 420, 220, 14), wave(140, 450, 220, 8)};
    a.borderWidth = 2;
    annot::create(doc.session(), 0, std::move(a));
    annot::AnnotProps b = sceneProps(CommentAction::Ink);
    b.color = annot::AnnotColor::fromRgb(0xF44336);
    b.borderWidth = 3;
    b.ink = {{{400, 400}, {430, 380}, {470, 395}, {480, 440}, {450, 470}, {410, 460}, {400, 420}}};
    annot::create(doc.session(), 0, std::move(b)).then(&w, [&w](annot::AnnotRef) { markDone(w); });
}

void selectedScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    annot::AnnotProps note = sceneProps(CommentAction::Note);
    note.rect = {450, 360, 470, 380};
    note.contents = sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Перенести подпись ниже"));
    annot::create(doc.session(), 0, std::move(note));
    annot::AnnotProps ink = sceneProps(CommentAction::Ink);
    ink.ink = {wave(140, 420, 220, 14)};
    ink.borderWidth = 2;
    PageView* view = &doc.view();
    annot::create(doc.session(), 0, std::move(ink)).then(&w, [&w, view, &doc](annot::AnnotRef ref) {
        ToolRegistry::instance().deactivate();
        view->tools().setTool(ViewToolId::AnnotSelect);
        annot::list(doc.session(), 0).then(&w, [&w, view, ref](const std::vector<annot::AnnotInfo>& list) {
            for (const annot::AnnotInfo& a : list) {
                if (a.ref == ref) {
                    view->tools().annotSelect().setSelection({a});
                }
            }
            markDone(w);
        });
    });
}

}  // namespace

std::vector<Scene> commentScenes() {
    std::vector<Scene> scenes;
    const QString doc = QStringLiteral("edit/word-paragraphs.pdf");

    Scene note;
    note.name = QStringLiteral("annot-note-open");
    note.make = commentScene(doc, noteScene);
    note.ready = commentReady;
    scenes.push_back(note);

    Scene noteDark = note;
    noteDark.name = QStringLiteral("annot-note-open-dark");
    noteDark.theme = ThemeChoice::Dark;
    scenes.push_back(noteDark);

    Scene markup;
    markup.name = QStringLiteral("annot-markup");
    markup.make = commentScene(doc, markupScene);
    markup.ready = commentReady;
    scenes.push_back(markup);

    Scene caret;
    caret.name = QStringLiteral("annot-caret");
    caret.make = commentScene(doc, caretScene);
    caret.ready = commentReady;
    scenes.push_back(caret);

    Scene quick;
    quick.name = QStringLiteral("annot-quick-actions");
    quick.make = commentScene(doc, quickActionsScene);
    quick.ready = [](QWidget* w) {
        auto* window = static_cast<MainWindow*>(w);
        DocumentView* d = window->currentDocument();
        return commentReady(w) && d != nullptr && d->quickActions().isVisible();
    };
    scenes.push_back(quick);

    Scene ink;
    ink.name = QStringLiteral("annot-ink");
    ink.make = commentScene(doc, inkScene);
    ink.ready = commentReady;
    scenes.push_back(ink);

    Scene selected;
    selected.name = QStringLiteral("annot-selected");
    selected.make = commentScene(doc, selectedScene);
    selected.ready = commentReady;
    scenes.push_back(selected);
    return scenes;
}

}  // namespace ot::ui
