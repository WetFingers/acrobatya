// Предпросмотр печати справа в диалоге «Печать» (план 2.6; эталон 3.3 «предпросмотр… первая страница
// результата, листание»; view-37): лист плана рисует тот же конвейер, что и печать (PrintJob::preview → растр
// полосами в QImage 96 dpi), в пуле потоков; смена параметров — новый растр через короткую паузу (поколение
// отбрасывает устаревшие). Лист — белая бумага с рамкой и тенью на холсте, пунктир — область печати; под ним
// — «◀ Лист N из M ▶» и размер бумаги.
#pragma once

#include <QImage>
#include <QWidget>

#include <memory>
#include <vector>

#include "services/print_job.h"

class QLabel;
class QTimer;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class ToolButton;

class PrintPreview : public QWidget {
    Q_OBJECT
public:
    static constexpr int kDpi = 96;

    explicit PrintPreview(QWidget* parent = nullptr);
    ~PrintPreview() override;

    // Что показывать: сессия документа, параметры, бумага и план (листы и их ориентация; пусто — печатать
    // нечего).
    void setSource(std::shared_ptr<DocumentSession> session, const PrintSettings& settings,
                   const print::Paper& paper, const PrintPlan& plan);

    int sheetCount() const { return count_; }
    int currentSheet() const { return current_; }
    void setCurrentSheet(int sheet);
    void next() { setCurrentSheet(current_ + 1); }
    void previous() { setCurrentSheet(current_ - 1); }
    // Растр текущего листа готов (сцены и тесты ждут его).
    bool isReady() const { return ready_; }
    QImage image() const { return image_; }
    ToolButton* previousButton() const { return prev_; }
    ToolButton* nextButton() const { return next_; }
    QLabel* counterLabel() const { return counter_; }

signals:
    void ready();

protected:
    void changeEvent(QEvent* event) override;

private:
    class Canvas;

    void schedule();
    void render();
    void updateLabels();
    bool landscape() const;

    Canvas* canvas_ = nullptr;
    ToolButton* prev_ = nullptr;
    ToolButton* next_ = nullptr;
    QLabel* counter_ = nullptr;
    QLabel* paperLabel_ = nullptr;
    QTimer* timer_ = nullptr;
    std::shared_ptr<DocumentSession> session_;
    PrintSettings settings_;
    print::Paper paper_;
    int count_ = 0;
    std::vector<bool> landscape_;          // ориентация листов плана
    std::vector<print::SheetSide> sides_;  // лицо и оборот буклета
    int current_ = 0;
    quint64 generation_ = 0;
    bool ready_ = false;
    QImage image_;
};

}  // namespace ot::ui
