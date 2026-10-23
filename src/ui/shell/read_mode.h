// Режим чтения (эталон 3.1 «Режимы», 6.1; план 2.4; view-21): Ctrl+H прячет всё, кроме холста документа, —
// глобальную панель, левую и правую панели, строку состояния, полосы сообщений, плавающую панель — и
// показывает тонкую полупрозрачную полосу внизу (ReadModeBar: ◀ [стр.] из N ▶ │ − 100 % + │ «Выйти»);
// Esc или Ctrl+H — назад: панели — ровно как были (F8, Shift+F4, F4, строка меню). Вкладки переключаются и в
// режиме. Само переключение — MainWindow::setReadMode (read_mode.cpp).
#pragma once

#include <QPointer>
#include <QStringList>

#include "ui/widgets/floating_bar.h"

class QLabel;
class QLineEdit;

namespace ot::ui {

class DocumentView;

class ReadModeBar : public FloatingBar {
    Q_OBJECT
public:
    explicit ReadModeBar(QWidget* parent = nullptr);

    void attach(DocumentView* view);
    DocumentView* document() const { return view_; }
    QLineEdit* pageField() const { return page_; }
    QLabel* pageTotal() const { return total_; }
    QLabel* zoomLabel() const { return zoom_; }
    // Перейти по вводу поля (логический или физический номер, как в строке состояния).
    bool goToInput(const QString& text);

signals:
    void exitRequested();

protected:
    void changeEvent(QEvent* event) override;

private:
    void refresh();
    void retranslate();

    QLineEdit* page_ = nullptr;
    QLabel* total_ = nullptr;
    QLabel* zoom_ = nullptr;
    QPointer<DocumentView> view_;
    QStringList labels_;
};

}  // namespace ot::ui
