#include "ui/shell/save_flow.h"

#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFileInfo>
#include <QFuture>
#include <QMessageBox>
#include <QPushButton>

#include <exception>

#include "services/document_registry.h"
#include "services/document_session.h"
#include "services/save_ops.h"
#include "ui/dialogs/save_as_dialog.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/shell/message_bar.h"
#include "ui/widgets/message_bar.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

namespace {

// Ждать задачу документа, не замораживая окно (перерисовка, тайлы).
template <class T>
T awaitResult(QFuture<T> future) {
    QEventLoop loop;
    while (!future.isFinished()) {
        loop.processEvents(QEventLoop::ExcludeUserInputEvents | QEventLoop::WaitForMoreEvents, 50);
    }
    return future.result();
}

}  // namespace

SaveFlow::SaveFlow(MainWindow& window) : QObject(&window), window_(window) {}

QString SaveFlow::errorText(SaveError error) {
    switch (error) {
        case SaveError::None:
            return {};
        case SaveError::NotPdf:
            return tr("Этот файл открыт только для просмотра: сохраните его как PDF.");
        case SaveError::NoPath:
            return tr("У документа ещё нет файла: выберите, куда сохранить.");
        case SaveError::WouldBreakSignatures:
            return tr("Сохранение изменит подписанные данные, и подписи станут недействительными.");
        case SaveError::IncrementalNotPossible:
            return tr(
                "Дописать изменения нельзя: файл восстанавливался, новый или меняется защита. Выберите "
                "«Записать файл заново».");
        case SaveError::WriteFailed:
            return tr("Не удалось записать файл: проверьте место на диске и права на папку.");
        case SaveError::LinearizeFailed:
            return tr("Файл записан, но подготовить его для быстрого просмотра в Интернете не удалось.");
        case SaveError::ReplaceFailed:
            return tr(
                "Файл занят другой программой. Изменения сохранены во временный файл — закройте программу "
                "и сохраните ещё раз.");
        case SaveError::ReopenFailed:
            return tr("Файл записан, но открыть его заново не удалось.");
        case SaveError::SameAsDocument:
            return tr("Копию нельзя сохранить поверх самого документа: выберите другой файл.");
    }
    return {};
}

SaveReport SaveFlow::decide(DocumentView& view, const SaveOptions& options) {
    try {
        return awaitResult(saving::decide(view.session(), options));
    } catch (const std::exception& e) {
        SaveReport r;
        r.error = SaveError::WriteFailed;
        r.message = e.what();
        return r;
    }
}

bool SaveFlow::save(DocumentView& view) {
    error_.clear();
    if (view.isViewOnly()) {
        error_ = errorText(SaveError::NotPdf);
        return false;
    }
    const SaveReport plan = decide(view);
    switch (plan.error) {
        case SaveError::None:
            return write(view, view.path(), {}, false);
        case SaveError::NoPath:
            return saveAs(view);
        case SaveError::WouldBreakSignatures: {
            SignatureAnswer answer = SignatureAnswer::Cancel;
            if (signature_) {
                answer = signature_(view);
            } else {
                QMessageBox box(QMessageBox::Warning, tr("Документ подписан"), errorText(plan.error),
                                QMessageBox::Cancel, &window_);
                box.setObjectName(QStringLiteral("signatureWarning"));
                box.setInformativeText(
                    tr("Изменения можно сохранить в копию: подписанный файл останется "
                       "нетронутым."));
                QPushButton* copy = box.addButton(tr("Сохранить как &копию…"), QMessageBox::AcceptRole);
                box.button(QMessageBox::Cancel)->setText(tr("Отмена"));
                box.setDefaultButton(copy);
                box.exec();
                answer = box.clickedButton() == copy ? SignatureAnswer::SaveCopy : SignatureAnswer::Cancel;
            }
            if (answer == SignatureAnswer::SaveCopy) {
                return saveAs(view, true);
            }
            error_ = errorText(plan.error);
            return false;
        }
        default:
            fail(view, plan, view.path());
            return false;
    }
}

bool SaveFlow::saveAs(DocumentView& view, bool copy) {
    error_.clear();
    if (view.isViewOnly()) {
        error_ = errorText(SaveError::NotPdf);
        return false;
    }
    const QFileInfo current(view.path());
    QString suggested = view.path();
    if (copy) {
        suggested = current.absoluteDir().filePath(tr("%1 (копия).pdf").arg(current.completeBaseName()));
    } else if (suggested.isEmpty()) {
        suggested = QDir::home().filePath(tr("Без имени.pdf"));
    }
    SaveAsDialog dialog(suggested, view.session().summary().isSigned, &window_);
    if (copy) {
        dialog.setWindowTitle(tr("Сохранить как копию"));
        dialog.fullButton()->setChecked(true);  // копия подписанного — полная запись
    }
    const bool ok = saveAs_ ? saveAs_(dialog) : dialog.exec() == QDialog::Accepted;
    if (!ok || dialog.path().isEmpty()) {
        return false;
    }
    SaveOptions options = dialog.options();
    if (copy) {
        options.allowBreakingSignatures = true;
    }
    return write(view, dialog.path(), options, copy);
}

bool SaveFlow::write(DocumentView& view, const QString& path, const SaveOptions& options, bool copy) {
    SaveReport report;
    try {
        DocumentSession& session = view.session();
        const bool own = !copy && DocumentRegistry::pathKey(path) == DocumentRegistry::pathKey(view.path());
        report = awaitResult(copy ? session.saveCopy(path, options)
                                  : (own ? session.save(options) : session.saveAs(path, options)));
    } catch (const std::exception& e) {
        report.error = SaveError::WriteFailed;
        report.message = e.what();
    }
    if (!report.ok()) {
        fail(view, report, path);
        return false;
    }
    emit saved(&view, path);
    if (copy) {
        Toast::notify(&window_, tr("Копия сохранена: «%1».").arg(QFileInfo(path).fileName()), Tone::Ok);
    }
    return true;
}

void SaveFlow::fail(DocumentView& view, const SaveReport& report, const QString& target) {
    Q_UNUSED(view);
    error_ = errorText(report.error);
    Toast::notify(&window_, tr("Не удалось сохранить «%1». %2").arg(QFileInfo(target).fileName(), error_),
                  Tone::Danger);
}

MessageBar* SaveFlow::rewriteNotice(DocumentView& view, const QString& operation) {
    MessageBar* bar = view.messageStack().add(
        Tone::Info, tr("Создан новый документ: «%1». Отменить эту операцию нельзя; исходный файл не изменён.")
                        .arg(operation));
    bar->setObjectName(QStringLiteral("rewriteNoticeBar"));
    return bar;
}

}  // namespace ot::ui
