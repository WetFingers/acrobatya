#include "ui/tools/print/print_runner.h"

#include <QMetaObject>
#include <QPrinter>
#include <QtConcurrent/QtConcurrentRun>

#include "services/background_tasks.h"
#include "services/document_session.h"
#include "ui/widgets/toast.h"

namespace ot::ui {

PrintRunner::PrintRunner(std::shared_ptr<DocumentSession> session, PrintSettings settings,
                         std::unique_ptr<QPrinter> printer, std::unique_ptr<PrintDevice> device,
                         QWidget* host, QObject* parent)
    : QObject(parent),
      session_(std::move(session)),
      printer_(std::move(printer)),
      device_(std::move(device)),
      job_(std::make_unique<PrintJob>(*session_, std::move(settings))),
      host_(host) {}

PrintRunner::~PrintRunner() {
    if (!finished_) {
        job_->cancel();
        future_.waitForFinished();
        if (task_ != 0) {
            BackgroundTasks::shared().finish(task_);
        }
    }
}

void PrintRunner::start() {
    task_ = BackgroundTasks::shared().start(tr("Печать"), [this] { cancel(); });
    QPointer<PrintRunner> self(this);
    future_ = QtConcurrent::run([this, self] {
        return job_->run(*device_, [self](int done, int total) {
            QMetaObject::invokeMethod(
                self,
                [self, done, total] {
                    if (self != nullptr) {
                        BackgroundTasks::shared().setProgress(self->task_,
                                                              total > 0 ? done * 100 / total : 0);
                        emit self->progress(done, total);
                    }
                },
                Qt::QueuedConnection);
        });
    });
    future_.then(this, [this](PrintStatus status) { done(status); });
}

void PrintRunner::cancel() { job_->cancel(); }

void PrintRunner::done(PrintStatus status) {
    finished_ = true;
    status_ = status;
    error_ = job_->errorString();
    sheets_ = job_->sheetsPrinted();
    BackgroundTasks::shared().finish(task_);
    task_ = 0;
    if (notify_ && host_ != nullptr) {
        switch (status) {
            case PrintStatus::Done:
                Toast::notify(host_, tr("Отправлено на печать: %n лист(ов)", nullptr, sheets_), Tone::Ok);
                break;
            case PrintStatus::Cancelled:
                Toast::notify(host_, tr("Печать отменена."), Tone::Info);
                break;
            case PrintStatus::Denied:
                Toast::notify(host_, tr("Документ защищён: печать запрещена."), Tone::Danger);
                break;
            case PrintStatus::NothingToPrint:
            case PrintStatus::RangeError:
                Toast::notify(host_, tr("Нет страниц для печати."), Tone::Warn);
                break;
            case PrintStatus::Failed:
                Toast::notify(host_, tr("Не удалось напечатать: %1").arg(error_), Tone::Danger);
                break;
        }
    }
    emit finished(status);
    deleteLater();
}

}  // namespace ot::ui
