#include "services/forms_ops.h"

#include <set>

#include "services/task_context.h"

namespace ot::forms {

namespace forms_ops_impl {

void touch(TaskContext& tc, const std::vector<int>& pages) {
    for (const int p : pages) {
        tc.touchPage(p);
    }
}

std::vector<std::string> toStd(const QStringList& list) {
    std::vector<std::string> out;
    out.reserve(static_cast<std::size_t>(list.size()));
    for (const QString& s : list) {
        out.push_back(s.toStdString());
    }
    return out;
}

}  // namespace forms_ops_impl

using forms_ops_impl::toStd;
using forms_ops_impl::touch;

QFuture<Form> read(DocumentSession& session, TaskPriority priority) {
    return session.post([](TaskContext& tc) { return readForm(tc.document()); }, priority,
                        QStringLiteral("read form"));
}

QFuture<FormSnapshot> snapshot(DocumentSession& session) {
    return session.post(
        [](TaskContext& tc) {
            FormSnapshot s;
            if (tc.document().pdf() == nullptr) {
                return s;
            }
            s.form = readForm(tc.document());
            std::set<int> pages;
            for (const Field& f : s.form.fields) {
                for (const Widget& w : f.widgets) {
                    if (w.page >= 0) {
                        pages.insert(w.page);
                    }
                }
            }
            for (const int p : pages) {
                s.tabs[p] =
                    forms::tabSequence(s.form, p, tabOrder(tc.document(), p), annotOrder(tc.document(), p));
            }
            return s;
        },
        TaskPriority::High, QStringLiteral("form snapshot"));
}

QFuture<std::vector<PageWidget>> widgets(DocumentSession& session, int page, TaskPriority priority) {
    return session.post([page](TaskContext& tc) { return pageWidgets(tc.document(), page); }, priority,
                        QStringLiteral("form widgets"));
}

QFuture<std::vector<int>> tabSequence(DocumentSession& session, int page) {
    return session.post([page](TaskContext& tc) { return forms::tabSequence(tc.document(), page); },
                        TaskPriority::High, QStringLiteral("tab order"));
}

QFuture<QString> formatted(DocumentSession& session, QString name) {
    return session.post(
        [name = name.toStdString()](TaskContext& tc) {
            return QString::fromStdString(formattedValue(tc.document(), name));
        },
        TaskPriority::High, QStringLiteral("format field"));
}

QFuture<SetResult> setValue(DocumentSession& session, QString name, QString value, bool recalculate) {
    return session.edit(
        QString::fromLatin1(kFillStep),
        [name = name.toStdString(), value = value.toStdString(), recalculate](TaskContext& tc) {
            SetOptions options;
            options.recalculate = recalculate;
            SetResult r = forms::setValue(tc.document(), name, value, options);
            touch(tc, r.pages);
            return r;
        });
}

QFuture<SetResult> setValues(DocumentSession& session, QString name, QStringList values, bool recalculate) {
    return session.edit(QString::fromLatin1(kFillStep),
                        [name = name.toStdString(), values = toStd(values), recalculate](TaskContext& tc) {
                            SetOptions options;
                            options.recalculate = recalculate;
                            SetResult r = forms::setValues(tc.document(), name, values, options);
                            touch(tc, r.pages);
                            return r;
                        });
}

QFuture<SetResult> toggle(DocumentSession& session, int page, int objnum, bool recalculate) {
    return session.edit(QString::fromLatin1(kFillStep), [page, objnum, recalculate](TaskContext& tc) {
        SetOptions options;
        options.recalculate = recalculate;
        SetResult r = forms::toggle(tc.document(), page, objnum, options);
        touch(tc, r.pages);
        return r;
    });
}

QFuture<std::vector<int>> reset(DocumentSession& session, QStringList names, bool exclude) {
    return session.edit(QString::fromLatin1(kResetStep), [names = toStd(names), exclude](TaskContext& tc) {
        std::vector<int> pages = resetForm(tc.document(), names, exclude);
        touch(tc, pages);
        return pages;
    });
}

QFuture<ClickResult> click(DocumentSession& session, int page, int objnum) {
    return session.edit(QString::fromLatin1(kClickStep), [page, objnum](TaskContext& tc) {
        ClickResult r = forms::click(tc.document(), page, objnum);
        touch(tc, r.pages);
        return r;
    });
}

QFuture<std::vector<int>> focus(DocumentSession& session, int page, int objnum, bool in) {
    return session.edit(QString::fromLatin1(kFillStep), [page, objnum, in](TaskContext& tc) {
        std::vector<int> pages = forms::focus(tc.document(), page, objnum, in);
        touch(tc, pages);
        return pages;
    });
}

QFuture<std::vector<int>> recalculate(DocumentSession& session) {
    return session.edit(QString::fromLatin1(kCalculateStep), [](TaskContext& tc) {
        std::vector<int> pages = forms::recalculate(tc.document());
        touch(tc, pages);
        return pages;
    });
}

}  // namespace ot::forms
