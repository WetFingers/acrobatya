// Печать в фоне (план 2.6; эталон 3.3; view-37): задание services/print_job.h идёт в пуле потоков
// (QtConcurrent) — окно не замирает, ход и «Отмена» — в строке состояния (BackgroundTasks, без модальных
// «подождите»), итог — тост на окне: «Отправлено на печать: N листов», «Печать отменена», «Не удалось
// напечатать: …». Владеет QPrinter (настроенным «Свойствами принтера…») и устройством до конца задания;
// сессию держит shared_ptr — закрытая вкладка печать не обрывает. Удаление (закрытие окна) отменяет задание и
// ждёт его.
#pragma once

#include <QFuture>
#include <QObject>
#include <QPointer>

#include <memory>

#include "services/print_job.h"

class QPrinter;
class QWidget;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class PrintRunner : public QObject {
    Q_OBJECT
public:
    // host — окно для тоста (может исчезнуть раньше задания).
    PrintRunner(std::shared_ptr<DocumentSession> session, PrintSettings settings,
                std::unique_ptr<QPrinter> printer, std::unique_ptr<PrintDevice> device, QWidget* host,
                QObject* parent = nullptr);
    ~PrintRunner() override;

    void start();
    void cancel();
    bool isFinished() const { return finished_; }
    PrintStatus status() const { return status_; }
    QString errorString() const { return error_; }
    int sheetsPrinted() const { return sheets_; }
    PrintDevice& device() const { return *device_; }

    // Тост итога (тесты и сцены выключают, чтобы не ждать его исчезновения).
    void setNotify(bool on) { notify_ = on; }

signals:
    void progress(int done, int total);
    void finished(ot::PrintStatus status);

private:
    void done(PrintStatus status);

    std::shared_ptr<DocumentSession> session_;
    std::unique_ptr<QPrinter> printer_;
    std::unique_ptr<PrintDevice> device_;
    std::unique_ptr<PrintJob> job_;
    QPointer<QWidget> host_;
    QFuture<PrintStatus> future_;
    quint64 task_ = 0;
    PrintStatus status_ = PrintStatus::Done;
    QString error_;
    int sheets_ = 0;
    bool finished_ = false;
    bool notify_ = true;
};

}  // namespace ot::ui
