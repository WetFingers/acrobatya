// Строка состояния 32 px внизу окна (эталон 6.1 «Строка состояния», 6.6; план 2.4; view-08):
// «Стр. [ iii ] (3 из 12) ◀ ▶ │ − [125 % ▾] + │ Одна · Непрерывно · Разворот │ ⟲ ⟳ │ … Подписано ✓ · PDF/A ·
// Только чтение │ фоновая задача ▭▭▭ ×». Поле страницы принимает логический номер /PageLabels («iv») или
// число («3»: логическая «3», иначе третья; «(3)» — третья), Ctrl+Shift+N — фокус в поле; Esc — назад в
// документ. Масштаб — ZoomBox (предустановки и подгонки), режимы раскладки и поворот вида — у PageView
// текущей вкладки. Индикаторы — из сводки открытия (подписан, PDF/A) и вида (не-PDF — только чтение). Фоновые
// задачи — services/background_tasks.h: первая с прогрессом и «×» (отмена), остальные — числом. На Главной
// видна только задача; по F8 и в режиме чтения строка скрыта.
#pragma once

#include <QPointer>
#include <QStringList>
#include <QWidget>

#include <string>
#include <vector>

class QHBoxLayout;
class QLabel;
class QLineEdit;
class QProgressBar;

namespace ot::ui {

class Chip;
class DocumentView;
class SegmentedBar;
class ToolButton;
class ZoomBox;

class StatusBar : public QWidget {
    Q_OBJECT
public:
    // «Использовать логические номера страниц» (Установки → Просмотр страницы, 2.5b; вкл. по умолчанию, как в
    // Acrobat).
    static constexpr char kLogicalKey[] = "pageDisplay/useLogicalPageNumbers";

    explicit StatusBar(QWidget* parent = nullptr);

    void setDocument(DocumentView* view);
    DocumentView* document() const { return view_; }

    QLineEdit* pageField() const { return page_; }
    QLabel* pageTotal() const { return total_; }
    ToolButton* previousButton() const { return prev_; }
    ToolButton* nextButton() const { return next_; }
    ZoomBox* zoomBox() const { return zoom_; }
    SegmentedBar* layoutBar() const { return layout_; }
    ToolButton* rotateCcwButton() const { return rotateCcw_; }
    ToolButton* rotateCwButton() const { return rotateCw_; }
    Chip* signedChip() const { return signed_; }
    Chip* pdfaChip() const { return pdfa_; }
    Chip* readOnlyChip() const { return readOnly_; }
    QWidget* tasksIndicator() const { return tasks_; }
    QLabel* taskLabel() const { return taskLabel_; }
    QProgressBar* taskProgress() const { return taskProgress_; }
    ToolButton* taskCancel() const { return taskCancel_; }

    // Ctrl+Shift+N: фокус в поле страницы, текст выделен.
    void focusPageField();
    // Перейти по вводу поля; false — такой страницы нет (поле вернуло текущую).
    bool goToInput(const QString& text);
    static bool useLogicalNumbers();

signals:
    void pageInputRejected(const QString& text);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void refreshAll();
    void refreshPage();
    void refreshZoom();
    void refreshLayout();
    void refreshIndicators();
    void refreshTasks();
    void retranslate();
    void applyMetrics();
    std::vector<std::string> labels() const;

    QHBoxLayout* row_ = nullptr;
    QWidget* docGroup_ = nullptr;
    QLabel* pageCaption_ = nullptr;
    QLineEdit* page_ = nullptr;
    QLabel* total_ = nullptr;
    ToolButton* prev_ = nullptr;
    ToolButton* next_ = nullptr;
    ZoomBox* zoom_ = nullptr;
    SegmentedBar* layout_ = nullptr;
    ToolButton* rotateCcw_ = nullptr;
    ToolButton* rotateCw_ = nullptr;
    Chip* signed_ = nullptr;
    Chip* pdfa_ = nullptr;
    Chip* readOnly_ = nullptr;
    QWidget* tasks_ = nullptr;
    QLabel* taskLabel_ = nullptr;
    QProgressBar* taskProgress_ = nullptr;
    ToolButton* taskCancel_ = nullptr;
    QList<QWidget*> separators_;
    QPointer<DocumentView> view_;
    QStringList labels_;
};

}  // namespace ot::ui
