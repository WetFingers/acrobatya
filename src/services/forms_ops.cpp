#include "services/forms_ops.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTemporaryDir>

#include <set>
#include <stdexcept>

#include <spdlog/spdlog.h>

#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/util/paths.h"
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

QFuture<QByteArray> exportData(DocumentSession& session, DataFormat format, QString fileName) {
    return session.post(
        [format, name = fileName.toStdString()](TaskContext& tc) {
            const std::string bytes = writeData(collectData(tc.document(), name), format);
            return QByteArray(bytes.data(), static_cast<qsizetype>(bytes.size()));
        },
        TaskPriority::Normal, QStringLiteral("export form data"));
}

QFuture<DataImportReport> importData(DocumentSession& session, FormData data, QString documentFile) {
    return session.edit(QString::fromLatin1(kImportDataStep),
                        [data = std::move(data), file = documentFile.toStdString()](TaskContext& tc) {
                            DataImportReport r = forms::importData(tc.document(), data, file);
                            touch(tc, r.pages);
                            return r;
                        });
}

QFuture<SubmitPlan> planSubmit(DocumentSession& session, FieldAction action) {
    return session.post(
        [action = std::move(action)](TaskContext& tc) {
            const std::string file = paths::toUtf8(tc.document().path().filename());
            return forms::planSubmit(tc.document(), action, file);
        },
        TaskPriority::High, QStringLiteral("plan submit"));
}

QFuture<QByteArray> submitBody(DocumentSession& session, SubmitPlan plan) {
    if (plan.format != SubmitFormat::Pdf) {
        return QtFuture::makeReadyValueFuture(QByteArray(plan.body.data(), static_cast<qsizetype>(plan.body.size())));
    }
    const QString dir = QDir::temp().filePath(QStringLiteral("AcroBatya"));
    QDir().mkpath(dir);
    const QString file = QDir(dir).filePath(QStringLiteral("submit-%1-%2.pdf")
                                                .arg(QCoreApplication::applicationPid())
                                                .arg(session.id()));
    SaveOptions options;
    options.allowBreakingSignatures = true;  // копия для отправки; документ на диске не меняется
    return session.saveCopy(file, options).then([file](SaveReport report) {
        QByteArray bytes;
        if (report.ok()) {
            QFile f(file);
            if (f.open(QIODevice::ReadOnly)) {
                bytes = f.readAll();
            }
        }
        QFile::remove(file);
        if (bytes.isEmpty()) {
            throw std::runtime_error("submit: cannot write a copy of the document: " + report.message);
        }
        return bytes;
    });
}

MailResult sendMail(const SubmitPlan& plan, const QByteArray& pdf, Mailer& mailer) {
    MailMessage m;
    if (plan.mail) {
        for (const std::string& a : plan.mail->to) {
            m.to << QString::fromStdString(a);
        }
        for (const std::string& a : plan.mail->cc) {
            m.cc << QString::fromStdString(a);
        }
        for (const std::string& a : plan.mail->bcc) {
            m.bcc << QString::fromStdString(a);
        }
        m.subject = QString::fromStdString(plan.mail->subject);
        m.body = QString::fromStdString(plan.mail->body);
    }
    const QByteArray bytes = plan.format == SubmitFormat::Pdf
                                 ? pdf
                                 : QByteArray(plan.body.data(), static_cast<qsizetype>(plan.body.size()));
    QTemporaryDir dir(QDir::temp().filePath(QStringLiteral("AcroBatya-mail-XXXXXX")));
    MailResult failed;
    if (!dir.isValid()) {
        failed.message = QStringLiteral("cannot create a temporary folder");
        return failed;
    }
    const QString name = QString::fromStdString(plan.attachmentName);
    const QString path = dir.filePath(name);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly) || f.write(bytes) != bytes.size()) {
        failed.message = QStringLiteral("cannot write the attachment");
        return failed;
    }
    f.close();
    m.attachments.push_back({path, name});
    const MailResult r = mailer.send(m);
    spdlog::info("forms: submit by mail ({}) -> {}", submitFormatName(plan.format), mailStatusName(r.status));
    return r;
}

net::Response sendHttp(const SubmitPlan& plan, const QByteArray& pdf, net::HttpFetcher& fetcher) {
    net::Request r;
    r.method = plan.get ? "GET" : "POST";
    r.url = plan.url;
    r.purpose = net::Purpose::General;
    r.sameHostRedirectsOnly = true;
    if (!plan.get) {
        r.contentType = plan.contentType;
        const QByteArray bytes = plan.format == SubmitFormat::Pdf
                                     ? pdf
                                     : QByteArray(plan.body.data(), static_cast<qsizetype>(plan.body.size()));
        r.body.assign(bytes.begin(), bytes.end());
    }
    r.headers.emplace_back("Accept", "application/vnd.fdf, text/html, */*");
    return fetcher.fetch(r);
}

}  // namespace ot::forms
