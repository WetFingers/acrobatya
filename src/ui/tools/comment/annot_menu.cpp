#include "ui/tools/comment/annot_menu.h"

#include <QCoreApplication>
#include <QFuture>
#include <QMenu>
#include <QPointer>

#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

// Ключ стиля инструмента по типу (как CommentMode::actionKey; прочие — имя подтипа).
QString styleKey(annot::AnnotType type) {
    switch (type) {
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
        default: {
            const std::string_view name = annot::annotTypeName(type);
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
        if (!p) {
            return;
        }
        comments::ToolStyle style;
        if (!p->color.empty()) {
            style.color = p->color;
        }
        style.opacity = p->opacity;
        style.width = p->borderWidth;
        if (!p->icon.empty()) {
            style.icon = p->icon;
        }
        Settings settings;
        comments::setToolStyle(settings, styleKey(p->type), style);
    });
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
    for (const char* later : {QT_TRANSLATE_NOOP("ot::ui::CommentMenu", "Ответить"),
                              QT_TRANSLATE_NOOP("ot::ui::CommentMenu", "Статус"),
                              QT_TRANSLATE_NOOP("ot::ui::CommentMenu", "Свойства…")}) {
        QAction* a = menu->addAction(QCoreApplication::translate("ot::ui::CommentMenu", later));
        a->setEnabled(false);
        a->setToolTip(QCoreApplication::translate("ot::ui::CommentMenu", "Появится в версии 0.5"));
    }
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

void installCommentMenu(PageView& view) {
    QObject::connect(&view.tools(), &ToolController::annotContextMenu, &view,
                     [v = &view](QMenu* menu) { fill(*v, menu); });
}

}  // namespace ot::ui
