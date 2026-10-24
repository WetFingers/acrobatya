#include "ui/tools/comment/annot_menu.h"

#include "core/annot/freetext.h"
#include "ui/dialogs/annot_props_dialog.h"

#include <QCoreApplication>
#include <QFuture>
#include <QMenu>
#include <QPointer>

#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/panes/attachments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/comment_tool.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

// Ключ стиля инструмента по типу (как CommentMode::actionKey; прочие — имя подтипа).
QString styleKey(const annot::AnnotProps& p) {
    switch (p.type) {
        case annot::AnnotType::Text:
            return CommentMode::actionKey(CommentAction::Note);
        case annot::AnnotType::Highlight:
            return CommentMode::actionKey(CommentAction::Highlight);
        case annot::AnnotType::Underline:
            return CommentMode::actionKey(CommentAction::Underline);
        case annot::AnnotType::StrikeOut:
            return CommentMode::actionKey(CommentAction::StrikeOut);
        case annot::AnnotType::Squiggly:
            return CommentMode::actionKey(CommentAction::Squiggly);
        case annot::AnnotType::Caret:
            return CommentMode::actionKey(CommentAction::InsertText);
        case annot::AnnotType::Ink:
            return CommentMode::actionKey(CommentAction::Ink);
        case annot::AnnotType::FreeText:
            return CommentMode::actionKey(p.intent == "FreeTextCallout" ? CommentAction::Callout
                                                                        : CommentAction::TextBox);
        case annot::AnnotType::Line:
            return CommentMode::actionKey(p.intent == "LineArrow" ? CommentAction::Arrow
                                                                  : CommentAction::Line);
        case annot::AnnotType::Square:
            return CommentMode::actionKey(CommentAction::Rectangle);
        case annot::AnnotType::Circle:
            return CommentMode::actionKey(CommentAction::Oval);
        case annot::AnnotType::Polygon:
            return CommentMode::actionKey(p.cloudy ? CommentAction::Cloud : CommentAction::Polygon);
        case annot::AnnotType::PolyLine:
            return CommentMode::actionKey(CommentAction::PolyLine);
        case annot::AnnotType::Stamp:
            return QStringLiteral("stamp");
        case annot::AnnotType::FileAttachment:
            return QStringLiteral("attach");
        default: {
            const std::string_view name = annot::annotTypeName(p.type);
            return QString::fromLatin1(name.data(), static_cast<qsizetype>(name.size()));
        }
    }
}

void makeDefault(DocumentSession& session, const annot::AnnotRef& ref) {
    annot::props(session, ref).then(&session, [](QFuture<std::optional<annot::AnnotProps>> f) {
        std::optional<annot::AnnotProps> p;
        try {
            p = f.result();
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): нет свойств — нечего запоминать
        }
        if (p) {
            saveAsDefault(*p);
        }
    });
}

// Вложение комментария (/FileAttachment, comment-14): панель «Вложения» окна открывает или сохраняет файл с
// чёрным списком и подтверждением (эталон 7.3, 2.4).
void openAttachment(PageView& view, const annot::AnnotRef& ref, bool save) {
    auto* w = qobject_cast<MainWindow*>(view.window());
    if (w == nullptr) {
        return;
    }
    DocumentView* document = nullptr;
    for (QWidget* p = view.parentWidget(); p != nullptr && document == nullptr; p = p->parentWidget()) {
        document = qobject_cast<DocumentView*>(p);
    }
    if (document == nullptr) {
        return;
    }
    w->navPane().showPane(QStringLiteral("attachments"));
    if (auto* pane = qobject_cast<AttachmentsPane*>(w->navPane().page(QStringLiteral("attachments")))) {
        pane->openAnnotation(document, ref.page, ref.objnum, save);
    }
}

void fill(PageView& view, QMenu* menu) {
    const auto& selection = view.tools().annotSelect().selection();
    if (selection.empty()) {
        return;
    }
    const bool single = selection.size() == 1 && annot::isComment(selection.front().type);
    const annot::AnnotRef ref = selection.front().ref;
    menu->addSeparator();
    QPointer<PageView> guard(&view);
    QAction* open = menu->addAction(
        QCoreApplication::translate("ot::ui::CommentMenu", "Открыть окно записки"), [guard, ref] {
            if (guard != nullptr) {
                PopupLayer::of(*guard).open(ref, true);
            }
        });
    open->setObjectName(QStringLiteral("commentOpenPopup"));
    open->setEnabled(single);
    const bool freeText = single && selection.front().type == annot::AnnotType::FreeText;
    open->setVisible(!freeText);
    QAction* editText =
        menu->addAction(QCoreApplication::translate("ot::ui::CommentMenu", "Изменить текст"), [guard, ref] {
            if (guard != nullptr) {
                CommentTool::modeOf(*guard).editTextBox(ref);
            }
        });
    editText->setObjectName(QStringLiteral("commentEditText"));
    editText->setVisible(freeText);
    editText->setEnabled(freeText && view.tools().editable() && !selection.front().locked());
    const bool attachment = single && selection.front().type == annot::AnnotType::FileAttachment;
    QAction* openFile =
        menu->addAction(QCoreApplication::translate("ot::ui::CommentMenu", "Открыть вложение"), [guard, ref] {
            if (guard != nullptr) {
                openAttachment(*guard, ref, false);
            }
        });
    openFile->setObjectName(QStringLiteral("commentOpenAttachment"));
    openFile->setVisible(attachment);
    QAction* saveFile = menu->addAction(
        QCoreApplication::translate("ot::ui::CommentMenu", "Сохранить вложение…"), [guard, ref] {
            if (guard != nullptr) {
                openAttachment(*guard, ref, true);
            }
        });
    saveFile->setObjectName(QStringLiteral("commentSaveAttachment"));
    saveFile->setVisible(attachment);
    for (const char* later : {QT_TRANSLATE_NOOP("ot::ui::CommentMenu", "Ответить"),
                              QT_TRANSLATE_NOOP("ot::ui::CommentMenu", "Статус")}) {
        QAction* a = menu->addAction(QCoreApplication::translate("ot::ui::CommentMenu", later));
        a->setEnabled(false);
        a->setToolTip(QCoreApplication::translate("ot::ui::CommentMenu", "Появится в версии 0.5"));
    }
    QAction* props =
        menu->addAction(QCoreApplication::translate("ot::ui::CommentMenu", "Свойства…"), [guard, ref] {
            if (guard != nullptr) {
                AnnotPropsDialog::openFor(*guard, ref);
            }
        });
    props->setObjectName(QStringLiteral("commentProperties"));
    props->setEnabled(single);
    DocumentSession* session = view.document();
    QAction* def =
        menu->addAction(QCoreApplication::translate("ot::ui::CommentMenu", "Сделать свойствами по умолчанию"),
                        [session, ref] {
                            if (session != nullptr) {
                                makeDefault(*session, ref);
                            }
                        });
    def->setObjectName(QStringLiteral("commentMakeDefault"));
    def->setEnabled(single && session != nullptr);
}

}  // namespace

QString styleKeyFor(const annot::AnnotProps& props) { return styleKey(props); }

void saveAsDefault(const annot::AnnotProps& p) {
    comments::ToolStyle style;
    if (!p.color.empty()) {
        style.color = p.color;
    }
    style.opacity = p.opacity;
    style.width = p.borderWidth;
    if (!p.icon.empty() && (p.type == annot::AnnotType::Text || p.type == annot::AnnotType::FileAttachment)) {
        style.icon = p.icon;
    }
    const bool closed = p.type == annot::AnnotType::Square || p.type == annot::AnnotType::Circle ||
                        p.type == annot::AnnotType::Polygon || p.type == annot::AnnotType::Line;
    if (closed && !p.interiorColor.empty()) {
        style.fill = p.interiorColor;
    }
    if (p.type == annot::AnnotType::Line || p.type == annot::AnnotType::PolyLine) {
        style.lineStart = static_cast<int>(p.lineStartStyle);
        style.lineEnd = static_cast<int>(p.lineEndStyle);
    }
    if (p.type == annot::AnnotType::FreeText) {
        style.font = annot::textFontFamily(p.fontName);
        style.fontSize = p.fontSize;
        if (!p.textColor.empty()) {
            style.textColor = p.textColor;
        }
        style.align = p.align;
    }
    Settings settings;
    comments::setToolStyle(settings, styleKey(p), style);
}

void installCommentMenu(PageView& view) {
    QObject::connect(&view.tools(), &ToolController::annotContextMenu, &view,
                     [v = &view](QMenu* menu) { fill(*v, menu); });
    // Двойной щелчок по выбранному: поле FreeText — правка на странице, вложение — открыть файл (и в только
    // чтении), записка и прочие — окно записки.
    QObject::connect(&view.tools(), &ToolController::annotDoubleClicked, &view,
                     [v = &view](const annot::AnnotInfo& info) {
                         if (info.type == annot::AnnotType::FileAttachment) {
                             openAttachment(*v, info.ref, false);
                             return;
                         }
                         if (!v->tools().editable() || info.locked()) {
                             return;
                         }
                         if (info.type == annot::AnnotType::FreeText) {
                             CommentTool::modeOf(*v).editTextBox(info.ref);
                         } else if (annot::isComment(info.type)) {
                             PopupLayer::of(*v).open(info.ref, true);
                         }
                     });
}

}  // namespace ot::ui
