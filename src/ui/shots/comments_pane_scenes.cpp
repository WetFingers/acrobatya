// Сцены панели «Комментарии» (план 5.4; эталон 3.4 «Панель «Комментарии»», 6.1, 6.10) на абзацах Word
// (word-paragraphs.pdf): рецензия из пяти комментариев трёх авторов — выделение, записка с двумя ответами и
// статусом «Принято», подчёркивание «Отклонено», зачёркивание с галочкой, прямоугольник «Завершено».
// pane-comments — панель с карточками, выбрана записка (ответы деревом под ней, выделена на странице);
// pane-comments-filtered — фильтр по автору «Мария Иванова» (сводка «Показано 2 из 5», на странице только её
// пометки); pane-comments-search — поиск «приложени» (находит записку по тексту ответа). Комментарии
// создаются в открытом документе (на закрытии — «Не сохранять»).
// Ответы и статусы: comment-reply — поле ответа панели на записку (текст с жирным словом) и открытое окно
// записки с веткой ответов, статусом и полем «Ответить…»; comment-status — «Свойства комментария» записки на
// вкладке «История проверок» («Принято»).
// FDF/XFDF и сводка: comments-export — меню «…» панели (развернуть/свернуть, экспорт всех и выбранного в
// FDF/XFDF, импорт, сводка); summary-dialog — «Сводка комментариев» (макет «с выносками», сортировка,
// страницы, кегль); summary-result — вкладка «Без имени» со сводкой «с выносками»: страница слева, карточки
// справа, линии-связки и номера у пометок.
#include <QCoreApplication>
#include <QDate>
#include <QDateTime>
#include <QFuture>
#include <QLabel>
#include <QMenu>
#include <QTime>
#include <QTimer>
#include <QTreeView>
#include <QTreeWidget>

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "core/annot/comments.h"
#include "core/annot/defaults.h"
#include "core/text/selection.h"
#include "core/text/stext.h"
#include "services/annot_ops.h"
#include "services/comment_settings.h"
#include "services/comments_ops.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/dialogs/annot_props_dialog.h"
#include "ui/dialogs/summary_dialog.h"
#include "ui/panes/comments_pane.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/nav_pane.h"
#include "ui/shots/scenes.h"
#include "ui/tools/comment/popup_widget.h"
#include "ui/tools/comment/reply_editor.h"
#include "ui/view/page_view.h"
#include "ui/widgets/tool_button.h"

namespace ot::ui {

namespace comments_scene {

constexpr const char* kDone = "commentsSceneDone";
constexpr const char* kApplied = "commentsSceneApplied";

using TextPtr = std::shared_ptr<const TextPage>;
using Variant = std::function<void(CommentsPane&)>;

std::string sceneText(const char* text) {
    return QCoreApplication::translate("ot::ui::CommentsScenes", text).toStdString();
}

std::int64_t when(int day, int hour, int minute) {
    return QDateTime(QDate(2026, 10, day), QTime(hour, minute)).toSecsSinceEpoch();
}

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

annot::AnnotProps props(annot::AnnotType type, const char* author, const char* contents, std::int64_t at) {
    annot::AnnotProps p = annot::toolDefaults(type);
    p.author = sceneText(author);
    p.contents = sceneText(contents);
    p.created = at;
    p.modified = at;
    return p;
}

CommentsPane* paneOf(QWidget* w) {
    return qobject_cast<CommentsPane*>(
        static_cast<MainWindow*>(w)->navPane().page(QStringLiteral("comments")));
}

// Рецензия на первой странице; по готовности — свойство kDone.
void buildReview(MainWindow& w, DocumentView& doc, const TextPtr& text) {
    DocumentSession& s = doc.session();
    const QSizeF size = doc.view().pageSizePoints(0);
    const int lines = static_cast<int>(text->lines().size());
    const auto line = [&](int i) { return std::min(i, std::max(0, lines - 1)); };

    annot::AnnotProps hl =
        props(annot::AnnotType::Highlight, QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Мария Иванова"),
              QT_TRANSLATE_NOOP("ot::ui::CommentsScenes",
                                "Уточнить формулировку: «в разумный срок» — сколько дней?"),
              when(20, 9, 40));
    hl.quads = lineQuads(*text, line(0));
    annot::AnnotProps note =
        props(annot::AnnotType::Text, QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Борис Петров"),
              QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Нужна ссылка на приложение 2 с расчётом."),
              when(20, 10, 5));
    float y = 200;
    if (lines > 3) {
        y = text->lines()[3].bbox.y0;
    }
    const auto x = static_cast<float>(size.width() - 60);
    note.rect = {x - 10, y - 2, x + 10, y + 18};
    annot::AnnotProps ul = props(
        annot::AnnotType::Underline, QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Анна Смирнова"),
        QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Проверить цифры по второму кварталу."), when(21, 9, 30));
    ul.quads = lineQuads(*text, line(5), 6, 28);
    annot::AnnotProps so =
        props(annot::AnnotType::StrikeOut, QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Мария Иванова"),
              QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Лишнее предложение — убрать."), when(21, 14, 10));
    so.quads = lineQuads(*text, line(7), 0, 40);
    annot::AnnotProps box =
        props(annot::AnnotType::Square, QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Борис Петров"),
              QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Согласовать абзац с юристом."), when(22, 16, 45));
    if (lines > 11) {
        const RectF a = text->lines()[9].bbox;
        const RectF b = text->lines()[11].bbox;
        box.rect = {a.x0 - 6, a.y0 - 4, std::max(a.x1, b.x1) + 6, b.y1 + 4};
    } else {
        box.rect = {60, 420, 400, 480};
    }

    annot::create(s, 0, std::move(hl)).then(&w, [&s](annot::AnnotRef ref) {
        comments::setStatus(s, ref, std::string(annot::kMarkedModel), "Marked", comments::author(Settings()),
                            when(20, 17, 0));
    });
    QFuture<annot::AnnotRef> noteRef = annot::create(s, 0, std::move(note));
    QFuture<annot::AnnotRef> ulRef = annot::create(s, 0, std::move(ul));
    annot::create(s, 0, std::move(so));
    QFuture<annot::AnnotRef> boxRef = annot::create(s, 0, std::move(box));
    noteRef.then(&w, [&w, &s, ulRef, boxRef](annot::AnnotRef ref) mutable {
        comments::reply(s, ref,
                        {.author = sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Мария Иванова")),
                         .contents = sceneText(
                             QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Добавлю в следующей версии.")),
                         .created = when(20, 11, 20)})
            .then(&w, [&w, &s, ref, ulRef, boxRef](annot::AnnotRef first) mutable {
                comments::reply(
                    s, first,
                    {.author = sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Анна Смирнова")),
                     .contents = sceneText(
                         QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Согласна, приложение уже готово.")),
                     .created = when(21, 8, 55)});
                comments::setStatus(s, ref, std::string(annot::kReviewModel), "Accepted",
                                    sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Мария Иванова")),
                                    when(21, 12, 15));
                comments::setStatus(s, ulRef.result(), std::string(annot::kReviewModel), "Rejected",
                                    sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Борис Петров")),
                                    when(21, 15, 40));
                comments::setStatus(s, boxRef.result(), std::string(annot::kReviewModel), "Completed",
                                    sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Анна Смирнова")),
                                    when(23, 9, 10))
                    .then(&w, [&w](annot::AnnotRef) { w.setProperty(kDone, true); });
            });
    });
}

std::function<QWidget*()> reviewScene() {
    return withMainWindow([](MainWindow& w) {
        w.setProperty(kDone, false);
        w.setProperty(kApplied, false);
        w.openFile(sceneFixture(QStringLiteral("edit/word-paragraphs.pdf")));
        w.navPane().showPane(QStringLiteral("comments"));
        QTimer::singleShot(0, &w, [&w] {
            DocumentView* doc = w.currentDocument();
            if (doc == nullptr) {
                return;
            }
            doc->session().textPage(0).then(&w, [&w](TextPtr text) {
                DocumentView* d = w.currentDocument();
                if (d != nullptr && text) {
                    buildReview(w, *d, text);
                }
            });
        });
    });
}

// Готово: рецензия создана, панель прочитала её (пять карточек, у записки два ответа), вариант применён,
// extra (если есть) согласен и тайлы перерисованы.
std::function<bool(QWidget*)> reviewReady(Variant variant, std::function<bool(QWidget*)> extra = {}) {
    return [variant = std::move(variant), extra = std::move(extra)](QWidget* w) {
        CommentsPane* pane = paneOf(w);
        if (!w->property(kApplied).toBool()) {
            if (!w->property(kDone).toBool() || pane == nullptr || !pane->isLoaded() ||
                pane->tree().roots.size() != 5 || pane->tree().count() != 7) {
                return false;
            }
            w->setProperty(kApplied, true);
            variant(*pane);
            return false;
        }
        // После варианта панель может показывать другую вкладку (сводка «Без имени») — дерево не сверяется.
        if (extra && !extra(w)) {
            return false;
        }
        if (pane != nullptr) {
            pane->list()->viewport()->repaint();
        }
        return sceneDocumentReady(w);
    };
}

const annot::Comment* noteOf(const CommentsPane& pane) {
    for (const annot::Comment& c : pane.tree().roots) {
        if (c.type == annot::AnnotType::Text) {
            return &c;
        }
    }
    return nullptr;
}

}  // namespace comments_scene

std::vector<Scene> commentsPaneScenes() {
    std::vector<Scene> scenes;
    Scene all;
    all.name = QStringLiteral("pane-comments");
    all.make = comments_scene::reviewScene();
    all.ready = comments_scene::reviewReady([](CommentsPane& pane) {
        if (const annot::Comment* note = comments_scene::noteOf(pane)) {
            pane.selectComment(note->ref, true);
        }
    });
    scenes.push_back(all);

    Scene filtered;
    filtered.name = QStringLiteral("pane-comments-filtered");
    filtered.make = comments_scene::reviewScene();
    filtered.ready = comments_scene::reviewReady([](CommentsPane& pane) {
        annot::CommentFilter f;
        f.authors = {comments_scene::sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Мария Иванова"))};
        pane.setFilter(f);
    });
    scenes.push_back(filtered);

    Scene search;
    search.name = QStringLiteral("pane-comments-search");
    search.make = comments_scene::reviewScene();
    search.ready = comments_scene::reviewReady([](CommentsPane& pane) {
        pane.setSearchText(QCoreApplication::translate("ot::ui::CommentsScenes", "приложени"));
    });
    scenes.push_back(search);

    Scene reply;
    reply.name = QStringLiteral("comment-reply");
    reply.make = comments_scene::reviewScene();
    reply.ready = comments_scene::reviewReady(
        [](CommentsPane& pane) {
            const annot::Comment* note = comments_scene::noteOf(pane);
            if (note == nullptr || pane.document() == nullptr) {
                return;
            }
            pane.selectComment(note->ref, true);
            pane.startReply(note->ref);
            pane.replyEditor()->setSpans(
                {{.text =
                      comments_scene::sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "Расчёт — в "))},
                 {.text =
                      comments_scene::sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", "приложении 2")),
                  .bold = true},
                 {.text =
                      comments_scene::sceneText(QT_TRANSLATE_NOOP("ot::ui::CommentsScenes", ", стр. 14."))}});
            PopupLayer::of(pane.document()->view()).open(note->ref, false);
        },
        [](QWidget* w) {
            CommentsPane* pane = comments_scene::paneOf(w);
            const annot::Comment* note = comments_scene::noteOf(*pane);
            PopupWidget* popup =
                note != nullptr ? PopupLayer::of(pane->document()->view()).popup(note->ref) : nullptr;
            return popup != nullptr && !popup->repliesLabel()->text().isEmpty() && popup->isVisible();
        });
    scenes.push_back(reply);

    Scene status;
    status.name = QStringLiteral("comment-status");
    status.make = comments_scene::reviewScene();
    status.ready = comments_scene::reviewReady(
        [](CommentsPane& pane) {
            const annot::Comment* note = comments_scene::noteOf(pane);
            if (note == nullptr || pane.document() == nullptr) {
                return;
            }
            pane.selectComment(note->ref, true);
            auto* w = qobject_cast<MainWindow*>(pane.window());
            AnnotPropsDialog* d = AnnotPropsDialog::openFor(pane.document()->view(), note->ref);
            d->showTab(AnnotPropsDialog::History);
            QObject::connect(d, &AnnotPropsDialog::loadedChanged, d, [w, d] {
                if (w != nullptr) {
                    embedCentered(*w, d);
                }
            });
        },
        [](QWidget* w) {
            auto* d = w->findChild<AnnotPropsDialog*>();
            return d != nullptr && d->loaded() && d->historyList()->topLevelItemCount() > 0;
        });
    scenes.push_back(status);

    Scene exportMenu;
    exportMenu.name = QStringLiteral("comments-export");
    exportMenu.make = comments_scene::reviewScene();
    exportMenu.ready = comments_scene::reviewReady(
        [](CommentsPane& pane) {
            if (const annot::Comment* note = comments_scene::noteOf(pane)) {
                pane.selectComment(note->ref, true);
            }
            auto* w = qobject_cast<MainWindow*>(pane.window());
            pane.fillMoreMenu();
            QMenu* menu = pane.moreMenu();
            if (w == nullptr) {
                return;
            }
            embedWindow(*w, menu);
            menu->adjustSize();
            const QPoint anchor =
                pane.moreButton()->mapTo(w, QPoint(pane.moreButton()->width(), pane.moreButton()->height()));
            menu->move(anchor.x() - menu->width(), anchor.y());
            menu->show();
            menu->raise();
            // Подменю «Экспорт всех комментариев» — рядом, слева от меню.
            if (auto* sub = menu->findChild<QMenu*>(QStringLiteral("commentsExportAll"))) {
                menu->setActiveAction(sub->menuAction());
                embedWindow(*w, sub);
                sub->adjustSize();
                const QRect item = menu->actionGeometry(sub->menuAction());
                sub->move(menu->x() - sub->width(), menu->y() + item.y());
                sub->show();
                sub->raise();
            }
        },
        [](QWidget* w) {
            auto* pane = comments_scene::paneOf(w);
            return pane != nullptr && pane->moreMenu()->isVisible();
        });
    scenes.push_back(exportMenu);

    Scene dialog;
    dialog.name = QStringLiteral("summary-dialog");
    dialog.make = comments_scene::reviewScene();
    dialog.ready = comments_scene::reviewReady(
        [](CommentsPane& pane) {
            auto* w = qobject_cast<MainWindow*>(pane.window());
            if (w == nullptr || pane.document() == nullptr) {
                return;
            }
            auto* d = new SummaryDialog(*w, *pane.document(), &pane, w);
            d->setAttribute(Qt::WA_DeleteOnClose);
            d->adjustSize();
            embedCentered(*w, d);
        },
        [](QWidget* w) { return w->findChild<SummaryDialog*>() != nullptr; });
    scenes.push_back(dialog);

    Scene result;
    result.name = QStringLiteral("summary-result");
    result.make = comments_scene::reviewScene();
    result.ready = comments_scene::reviewReady(
        [](CommentsPane& pane) {
            auto* w = qobject_cast<MainWindow*>(pane.window());
            if (w == nullptr || pane.document() == nullptr) {
                return;
            }
            auto* d = new SummaryDialog(*w, *pane.document(), &pane, w);
            d->setAttribute(Qt::WA_DeleteOnClose);
            QObject::connect(d, &SummaryDialog::created, w, [w](DocumentView* view) {
                if (view != nullptr) {
                    view->view().setZoomPercent(50);
                    w->navPane().closePanel();
                }
                w->setProperty("summaryCreated", true);
            });
            d->build(false);
        },
        [](QWidget* w) {
            auto* window = static_cast<MainWindow*>(w);
            return w->property("summaryCreated").toBool() && window->currentDocument() != nullptr &&
                   window->currentDocument()->path().isEmpty();
        });
    scenes.push_back(result);
    return scenes;
}

}  // namespace ot::ui
