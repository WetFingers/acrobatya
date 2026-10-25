#include "ui/panes/comment_actions.h"

#include <QAction>
#include <QActionGroup>
#include <QColor>
#include <QCoreApplication>
#include <QDateTime>
#include <QMenu>
#include <QPointer>

#include <functional>

#include "services/comment_settings.h"
#include "services/comments_ops.h"
#include "services/document_session.h"
#include "services/settings.h"
#include "ui/dialogs/annot_props_dialog.h"

namespace ot::ui::comment_actions {

namespace {

QString tr(const char* text) { return QCoreApplication::translate("ot::ui::CommentActions", text); }

}  // namespace

QString typeTitle(const annot::Comment& c) {
    if (c.depth > 0) {
        return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Ответ"));
    }
    if (c.type == annot::AnnotType::Redact) {
        return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Пометка для скрытия"));
    }
    annot::AnnotProps p;
    p.type = c.type;
    p.intent = c.intent;
    return AnnotPropsDialog::typeTitle(p);
}

QString typeIcon(const annot::Comment& c) {
    using annot::AnnotType;
    if (c.depth > 0) {
        return QStringLiteral("reply");
    }
    switch (c.type) {
        case AnnotType::Text:
            return QStringLiteral("sticky-note");
        case AnnotType::FreeText:
            return c.intent == "FreeTextCallout" ? QStringLiteral("message-square-quote")
                                                 : QStringLiteral("type");
        case AnnotType::Line:
            return c.intent == "LineArrow" ? QStringLiteral("move-up-right") : QStringLiteral("slash");
        case AnnotType::Square:
            return QStringLiteral("square");
        case AnnotType::Circle:
            return QStringLiteral("circle");
        case AnnotType::Polygon:
            return QStringLiteral("pentagon");
        case AnnotType::PolyLine:
            return QStringLiteral("waypoints");
        case AnnotType::Highlight:
            return QStringLiteral("highlighter");
        case AnnotType::Underline:
        case AnnotType::Squiggly:
            return QStringLiteral("underline");
        case AnnotType::StrikeOut:
            return QStringLiteral("strikethrough");
        case AnnotType::Caret:
            return QStringLiteral("text-cursor");
        case AnnotType::Ink:
            return QStringLiteral("pencil");
        case AnnotType::Stamp:
            return QStringLiteral("stamp");
        case AnnotType::FileAttachment:
            return QStringLiteral("paperclip");
        default:
            return QStringLiteral("message-square");
    }
}

QString stateTitle(annot::ReviewState state) {
    switch (state) {
        case annot::ReviewState::Accepted:
            return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Принято"));
        case annot::ReviewState::Rejected:
            return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Отклонено"));
        case annot::ReviewState::Cancelled:
            return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Отменено"));
        case annot::ReviewState::Completed:
            return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Завершено"));
        case annot::ReviewState::None:
            break;
    }
    return tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Нет"));
}

QString dateText(std::int64_t seconds) {
    if (seconds <= 0) {
        return {};
    }
    return QDateTime::fromSecsSinceEpoch(seconds).toString(QStringLiteral("dd.MM.yyyy HH:mm"));
}

QString richHtml(const std::string& richContents, const std::string& plain) {
    const std::vector<annot::RichSpan> spans = richContents.empty()
                                                   ? std::vector<annot::RichSpan>{{.text = plain}}
                                                   : annot::parseRichContents(richContents);
    QString html;
    for (const annot::RichSpan& s : spans) {
        QString text = QString::fromStdString(s.text).toHtmlEscaped();
        text.replace(QLatin1Char('\n'), QStringLiteral("<br>"));
        if (s.bold) {
            text = QStringLiteral("<b>%1</b>").arg(text);
        }
        if (s.italic) {
            text = QStringLiteral("<i>%1</i>").arg(text);
        }
        if (s.underline) {
            text = QStringLiteral("<u>%1</u>").arg(text);
        }
        if (s.color != annot::kNoRichColor) {
            text = QStringLiteral("<span style=\"color:%1\">%2</span>")
                       .arg(QColor::fromRgb(s.color).name(QColor::HexRgb), text);
        }
        html += text;
    }
    return html;
}

QString threadHtml(const annot::Comment& c) {
    QString html;
    std::function<void(const annot::Comment&)> walk = [&](const annot::Comment& r) {
        html += QStringLiteral("<p style=\"margin-left:%1px\"><b>%2</b> %3<br>%4</p>")
                    .arg((r.depth - 1) * 12)
                    .arg(QString::fromStdString(r.author).toHtmlEscaped(), dateText(r.date()),
                         richHtml(r.richContents, r.contents));
        for (const annot::Comment& child : r.replies) {
            walk(child);
        }
    };
    for (const annot::Comment& r : c.replies) {
        walk(r);
    }
    if (c.review() != annot::ReviewState::None) {
        html += QStringLiteral("<p>%1</p>")
                    .arg(tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Статус: %1 (%2)"))
                             .arg(stateTitle(c.review()),
                                  QString::fromStdString(c.reviewAuthor()).toHtmlEscaped()));
    }
    return html;
}

QMenu* addStatusMenu(QMenu& menu, DocumentSession& session, const annot::AnnotRef& ref,
                     annot::ReviewState current, bool enabled) {
    QMenu* sub = menu.addMenu(tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "&Статус")));
    sub->setObjectName(QStringLiteral("commentStatusMenu"));
    sub->setEnabled(enabled);
    auto* group = new QActionGroup(sub);
    const QPointer<DocumentSession> guard(&session);
    for (int i = 0; i < annot::kReviewStateCount; ++i) {
        const auto state = static_cast<annot::ReviewState>(i);
        QAction* a = sub->addAction(stateTitle(state));
        a->setObjectName(
            QStringLiteral("commentStatus%1").arg(QLatin1String(annot::reviewStateName(state).data())));
        a->setCheckable(true);
        a->setChecked(state == current);
        group->addAction(a);
        QObject::connect(a, &QAction::triggered, sub, [guard, ref, state, current] {
            if (guard == nullptr || state == current) {
                return;
            }
            comments::setStatus(*guard, ref, std::string(annot::kReviewModel),
                                std::string(annot::reviewStateName(state)), comments::author(Settings()));
        });
    }
    return sub;
}

QAction* addCheckAction(QMenu& menu, DocumentSession& session, const annot::AnnotRef& ref, bool marked,
                        bool enabled) {
    QAction* a = menu.addAction(tr(QT_TRANSLATE_NOOP("ot::ui::CommentActions", "Отметить &галочкой")));
    a->setObjectName(QStringLiteral("commentCheckMark"));
    a->setCheckable(true);
    a->setChecked(marked);
    a->setEnabled(enabled);
    const QPointer<DocumentSession> guard(&session);
    QObject::connect(a, &QAction::triggered, &menu, [guard, ref](bool on) {
        if (guard != nullptr) {
            comments::setStatus(*guard, ref, std::string(annot::kMarkedModel), on ? "Marked" : "Unmarked",
                                comments::author(Settings()));
        }
    });
    return a;
}

}  // namespace ot::ui::comment_actions
