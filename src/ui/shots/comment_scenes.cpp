// Сцены комментариев (план 5.1; эталон 3.4, 6.2 п. 3, 6.10) на абзацах Word (word-paragraphs.pdf) с
// включённым «Добавить комментарии»: annot-note-open — записка с открытым окном и текстом (и тёмная);
// annot-markup — выделение, подчёркивание, зачёркивание и волнистая на строках первого абзаца; annot-caret —
// вставка текста и «Заменить текст» (зачёркнутое слово и каретка) с окном нового текста; annot-quick-actions
// — выделенный «Выделением» текст и панель быстрых действий над ним. Комментарии создаются в открытом
// документе (на закрытии — «Не сохранять»); кадр — когда последний шаг сделан и тайлы перерисованы.
// 5.2: annot-freetext — текстовые поля (Arial с рамкой и заливкой, Segoe UI, Times New Roman по центру) и
// «Формат текста» панели; annot-callout — выноска с открытой стрелкой к строке; annot-text-format — открытый
// редактор нового поля с набранным текстом и форматом панели (Times New Roman 16, синий, по центру);
// annot-shapes — прямоугольник с заливкой, овал, линия, стрелка, многоугольник и ломаная с концами (панель —
// «Прямоугольник»); annot-cloud — облако вокруг абзаца и облачный прямоугольник; annot-vertex-edit —
// выбранный «Выбором объектов» многоугольник с маркерами вершин; annot-props-appearance — «Свойства
// комментария» облачного прямоугольника (вкладка «Вид»), annot-props-general — текстового поля (вкладка
// «Общие»).
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
#include "ui/dialogs/annot_props_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shots/scenes.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/comment/inline_text_editor.h"
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

annot::AnnotProps sceneTextBox(const char* text, RectF rect, const char* font, float size) {
    annot::AnnotProps p = sceneProps(CommentAction::TextBox);
    p.contents = sceneText(text);
    p.rect = rect;
    p.fontName = font;
    p.fontSize = size;
    p.align = 0;  // стиль инструмента из прошлых сцен не в счёт
    p.borderWidth = 0;
    p.color = {};
    p.textColor = annot::AnnotColor::gray(0);
    return p;
}

void freeTextScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    sceneCommentTool().start(CommentAction::TextBox);
    annot::AnnotProps a =
        sceneTextBox(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Согласовано с юристом, правок нет."),
                     {320, 380, 540, 420}, "Arial", 13);
    a.borderWidth = 1;
    a.color = annot::AnnotColor::fromRgb(0xFFF8C4);
    a.textColor = annot::AnnotColor::fromRgb(0xC62828);
    annot::create(doc.session(), 0, std::move(a));
    annot::AnnotProps b = sceneTextBox(
        QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Пометка на полях: проверить цифры во втором квартале"),
        {320, 440, 540, 490}, "Segoe UI", 12);
    annot::create(doc.session(), 0, std::move(b));
    annot::AnnotProps c = sceneTextBox(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Итоговая редакция"),
                                       {320, 510, 540, 540}, "Times New Roman", 16);
    c.align = 1;
    c.textColor = annot::AnnotColor::fromRgb(0x1565C0);
    annot::create(doc.session(), 0, std::move(c)).then(&w, [&w](annot::AnnotRef) { markDone(w); });
}

void calloutScene(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    sceneCommentTool().start(CommentAction::Callout);
    PointF tip{200, 300};
    if (text->lines().size() > 2) {
        const RectF line = text->lines()[2].bbox;
        tip = {line.x0 + (line.x1 - line.x0) * 0.4f, line.y1};
    }
    annot::AnnotProps p = sceneTextBox(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Уточнить срок поставки"),
                                       {tip.x + 120, tip.y + 160, tip.x + 300, tip.y + 190}, "Arial", 13);
    p.intent = "FreeTextCallout";
    p.borderWidth = 1;
    p.textColor = annot::AnnotColor::fromRgb(0xC62828);
    const float mid = tip.y + 175;
    p.callout = {tip, {tip.x + 96, mid}, {tip.x + 120, mid}};
    p.lineStartStyle = annot::LineEnding::OpenArrow;
    annot::create(doc.session(), 0, std::move(p)).then(&w, [&w](annot::AnnotRef) { markDone(w); });
}

void textFormatScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    CommentTool& tool = sceneCommentTool();
    tool.start(CommentAction::TextBox);
    tool.fontBox()->setCurrentIndex(tool.fontBox()->findData(QStringLiteral("Times New Roman")));
    tool.fontSizeField()->setValue(16);
    tool.alignBar()->setCurrentIndex(1);
    annot::AnnotProps p = CommentTool::modeOf(doc.view()).newProps(CommentAction::TextBox);
    p.rect = {300, 420, 520, 470};
    p.textColor = annot::AnnotColor::fromRgb(0x1565C0);
    InlineTextEditor* editor = InlineTextEditor::startNew(doc.view(), 0, std::move(p));
    editor->setPlainText(
        QCoreApplication::translate("ot::ui::CommentScenes", "Новый текст поля прямо на странице"));
    QTimer::singleShot(200, &w, [&w] { markDone(w); });
}

annot::AnnotProps sceneShape(CommentAction action, std::uint32_t color, float width) {
    annot::AnnotProps p = sceneProps(action);
    p.color = annot::AnnotColor::fromRgb(color);
    p.borderWidth = width;
    p.interiorColor = {};
    p.opacity = 1;
    p.lineStartStyle = annot::LineEnding::None;
    p.lineEndStyle = annot::LineEnding::None;
    return p;
}

void shapesScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    sceneCommentTool().start(CommentAction::Rectangle);
    annot::AnnotProps rect = sceneShape(CommentAction::Rectangle, 0xE53935, 2);
    rect.rect = {90, 360, 250, 450};
    rect.interiorColor = annot::AnnotColor::fromRgb(0xFFF59D);
    rect.opacity = 0.8f;
    annot::create(doc.session(), 0, std::move(rect));
    annot::AnnotProps oval = sceneShape(CommentAction::Oval, 0x1E88E5, 2);
    oval.rect = {290, 360, 450, 450};
    annot::create(doc.session(), 0, std::move(oval));
    annot::AnnotProps line = sceneShape(CommentAction::Line, 0x43A047, 2);
    line.lineStart = {90, 500};
    line.lineEnd = {250, 500};
    annot::create(doc.session(), 0, std::move(line));
    annot::AnnotProps arrow = sceneShape(CommentAction::Arrow, 0xE53935, 2);
    arrow.lineStart = {290, 540};
    arrow.lineEnd = {450, 490};
    arrow.lineEndStyle = annot::LineEnding::OpenArrow;
    arrow.intent = "LineArrow";
    annot::create(doc.session(), 0, std::move(arrow));
    annot::AnnotProps poly = sceneShape(CommentAction::Polygon, 0x8E24AA, 1.5f);
    poly.vertices = {{100, 600}, {190, 570}, {250, 640}, {170, 700}, {90, 670}};
    poly.interiorColor = annot::AnnotColor::fromRgb(0xE1BEE7);
    annot::create(doc.session(), 0, std::move(poly));
    annot::AnnotProps polyline = sceneShape(CommentAction::PolyLine, 0x00897B, 2);
    polyline.vertices = {{290, 690}, {340, 600}, {390, 670}, {450, 590}};
    polyline.lineEndStyle = annot::LineEnding::ClosedArrow;
    polyline.lineStartStyle = annot::LineEnding::Circle;
    annot::create(doc.session(), 0, std::move(polyline)).then(&w, [&w](annot::AnnotRef) { markDone(w); });
}

void cloudScene(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    sceneCommentTool().start(CommentAction::Cloud);
    RectF around{80, 170, 520, 230};
    if (text->lines().size() > 3) {
        const RectF a = text->lines()[2].bbox;
        const RectF b = text->lines()[3].bbox;
        around = {std::min(a.x0, b.x0) - 10, a.y0 - 8, std::max(a.x1, b.x1) + 10, b.y1 + 8};
    }
    annot::AnnotProps cloud = sceneShape(CommentAction::Cloud, 0xE53935, 1.5f);
    cloud.vertices = {
        {around.x0, around.y0}, {around.x1, around.y0}, {around.x1, around.y1}, {around.x0, around.y1}};
    cloud.cloudy = true;
    cloud.cloudIntensity = 1;
    cloud.intent = "PolygonCloud";
    annot::create(doc.session(), 0, std::move(cloud));
    annot::AnnotProps box = sceneShape(CommentAction::Rectangle, 0x1E88E5, 1.5f);
    box.rect = {120, 420, 380, 520};
    box.cloudy = true;
    box.cloudIntensity = 2;
    box.interiorColor = annot::AnnotColor::fromRgb(0xE3F2FD);
    annot::create(doc.session(), 0, std::move(box)).then(&w, [&w](annot::AnnotRef) { markDone(w); });
}

void vertexScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    annot::AnnotProps poly = sceneShape(CommentAction::Polygon, 0xE53935, 2);
    poly.vertices = {{180, 400}, {330, 380}, {400, 480}, {290, 560}, {170, 520}};
    poly.interiorColor = annot::AnnotColor::fromRgb(0xFFCDD2);
    poly.opacity = 0.9f;
    PageView* view = &doc.view();
    annot::create(doc.session(), 0, std::move(poly)).then(&w, [&w, view, &doc](annot::AnnotRef ref) {
        ToolRegistry::instance().deactivate();
        view->tools().setTool(ViewToolId::AnnotSelect);
        annot::list(doc.session(), 0).then(&w, [&w, view, ref](const std::vector<annot::AnnotInfo>& list) {
            for (const annot::AnnotInfo& a : list) {
                if (a.ref == ref) {
                    view->tools().annotSelect().setSelection({a});
                }
            }
            // Маркеры вершин читаются задачей — кадр, когда они пришли.
            auto* poll = new QTimer(&w);
            poll->start(50);
            QObject::connect(poll, &QTimer::timeout, &w, [&w, view, poll] {
                if (!view->tools().annotSelect().vertexPoints().empty()) {
                    poll->deleteLater();
                    markDone(w);
                }
            });
        });
    });
}

// Диалог «Свойства комментария» над созданной аннотацией props на вкладке tab; кадр — когда диалог загружен.
void propsScene(MainWindow& w, DocumentView& doc, annot::AnnotProps props, AnnotPropsDialog::Tab tab) {
    PageView* view = &doc.view();
    annot::create(doc.session(), 0, std::move(props)).then(&w, [&w, view, tab](annot::AnnotRef ref) {
        ToolRegistry::instance().deactivate();
        AnnotPropsDialog* d = AnnotPropsDialog::openFor(*view, ref);
        d->showTab(tab);
        QObject::connect(d, &AnnotPropsDialog::loadedChanged, &w, [&w, d] {
            embedCentered(w, d);
            markDone(w);
        });
    });
}

void propsAppearanceScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    annot::AnnotProps p = sceneShape(CommentAction::Rectangle, 0xE53935, 2);
    p.rect = {120, 400, 380, 520};
    p.cloudy = true;
    p.cloudIntensity = 1;
    p.interiorColor = annot::AnnotColor::fromRgb(0xFFF59D);
    p.opacity = 0.8f;
    propsScene(w, doc, std::move(p), AnnotPropsDialog::Appearance);
}

void propsGeneralScene(MainWindow& w, DocumentView& doc, const TextPtr& /*text*/) {
    annot::AnnotProps p =
        sceneTextBox(QT_TRANSLATE_NOOP("ot::ui::CommentScenes", "Согласовано с юристом, правок нет."),
                     {320, 400, 540, 440}, "Arial", 13);
    p.subject = "Text Box";
    propsScene(w, doc, std::move(p), AnnotPropsDialog::General);
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

    Scene panel;
    panel.name = QStringLiteral("tool-panel-comments");
    panel.make = commentScene(doc, [](MainWindow& w, DocumentView&, const TextPtr&) {
        sceneCommentTool().start(CommentAction::Ink);
        markDone(w);
    });
    panel.ready = commentReady;
    scenes.push_back(panel);

    Scene selected;
    selected.name = QStringLiteral("annot-selected");
    selected.make = commentScene(doc, selectedScene);
    selected.ready = commentReady;
    scenes.push_back(selected);

    Scene freeText;
    freeText.name = QStringLiteral("annot-freetext");
    freeText.make = commentScene(doc, freeTextScene);
    freeText.ready = commentReady;
    scenes.push_back(freeText);

    Scene freeTextDark = freeText;
    freeTextDark.name = QStringLiteral("annot-freetext-dark");
    freeTextDark.theme = ThemeChoice::Dark;
    scenes.push_back(freeTextDark);

    Scene callout;
    callout.name = QStringLiteral("annot-callout");
    callout.make = commentScene(doc, calloutScene);
    callout.ready = commentReady;
    scenes.push_back(callout);

    Scene format;
    format.name = QStringLiteral("annot-text-format");
    format.make = commentScene(doc, textFormatScene);
    format.ready = commentReady;
    scenes.push_back(format);

    Scene shapes;
    shapes.name = QStringLiteral("annot-shapes");
    shapes.make = commentScene(doc, shapesScene);
    shapes.ready = commentReady;
    scenes.push_back(shapes);

    Scene shapesDark = shapes;
    shapesDark.name = QStringLiteral("annot-shapes-dark");
    shapesDark.theme = ThemeChoice::Dark;
    scenes.push_back(shapesDark);

    Scene cloud;
    cloud.name = QStringLiteral("annot-cloud");
    cloud.make = commentScene(doc, cloudScene);
    cloud.ready = commentReady;
    scenes.push_back(cloud);

    Scene vertex;
    vertex.name = QStringLiteral("annot-vertex-edit");
    vertex.make = commentScene(doc, vertexScene);
    vertex.ready = commentReady;
    scenes.push_back(vertex);

    Scene propsAppearance;
    propsAppearance.name = QStringLiteral("annot-props-appearance");
    propsAppearance.make = commentScene(doc, propsAppearanceScene);
    propsAppearance.ready = commentReady;
    scenes.push_back(propsAppearance);

    Scene propsAppearanceDark = propsAppearance;
    propsAppearanceDark.name = QStringLiteral("annot-props-appearance-dark");
    propsAppearanceDark.theme = ThemeChoice::Dark;
    scenes.push_back(propsAppearanceDark);

    Scene propsGeneral;
    propsGeneral.name = QStringLiteral("annot-props-general");
    propsGeneral.make = commentScene(doc, propsGeneralScene);
    propsGeneral.ready = commentReady;
    scenes.push_back(propsGeneral);
    return scenes;
}

}  // namespace ot::ui
