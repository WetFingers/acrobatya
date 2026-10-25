// «Сводка комментариев» (план 5.4; эталон 3.4 «Сводка комментариев», 6.3; comment-25): макет (документ и
// комментарии на одной странице с выносками / только комментарии / на отдельных страницах), сортировка и
// «по убыванию», «Только видимые в панели «Комментарии»» (фильтр и поиск панели), страницы (все или с — по),
// кегль. «Создать PDF» — новая вкладка «Без имени» (операция с перезаписью: без истории); «Печать…» — та же
// вкладка и сразу диалог «Печать». Сводку строит ядро (core/annot/summary.h) задачей документа.
#pragma once

#include <QDialog>
#include <QPointer>

#include "core/annot/summary.h"

class QCheckBox;
class QComboBox;
class QPushButton;
class QRadioButton;
class QSpinBox;

namespace ot::ui {

class CommentsPane;
class DocumentView;
class MainWindow;

class SummaryDialog : public QDialog {
    Q_OBJECT
public:
    SummaryDialog(MainWindow& window, DocumentView& view, CommentsPane* pane, QWidget* parent = nullptr);
    // Открыть над окном (окно-модально, удаляется при закрытии).
    static SummaryDialog* openFor(MainWindow& window, DocumentView& view, CommentsPane* pane);

    QRadioButton* layoutButton(annot::SummaryLayout layout) const;
    QComboBox* sortBox() const { return sort_; }
    QCheckBox* descendingBox() const { return descending_; }
    QCheckBox* visibleOnlyBox() const { return visibleOnly_; }
    QRadioButton* allPagesButton() const { return allPages_; }
    QRadioButton* rangeButton() const { return range_; }
    QSpinBox* fromBox() const { return from_; }
    QSpinBox* toBox() const { return to_; }
    QSpinBox* fontSizeBox() const { return fontSize_; }
    QPushButton* createButton() const { return create_; }
    QPushButton* printButton() const { return print_; }

    // Параметры сводки по полям диалога (подписи — на языке интерфейса).
    annot::SummaryOptions options() const;
    // Построить и открыть вкладку (print — затем «Печать»).
    void build(bool print);

signals:
    // Сводка открыта вкладкой «Без имени» (nullptr — не построилась).
    void created(ot::ui::DocumentView* result);

private:
    MainWindow& window_;
    QPointer<DocumentView> view_;
    QPointer<CommentsPane> pane_;
    QRadioButton* layouts_[3] = {};
    QComboBox* sort_ = nullptr;
    QCheckBox* descending_ = nullptr;
    QCheckBox* visibleOnly_ = nullptr;
    QRadioButton* allPages_ = nullptr;
    QRadioButton* range_ = nullptr;
    QSpinBox* from_ = nullptr;
    QSpinBox* to_ = nullptr;
    QSpinBox* fontSize_ = nullptr;
    QPushButton* create_ = nullptr;
    QPushButton* print_ = nullptr;
    bool busy_ = false;
};

}  // namespace ot::ui
