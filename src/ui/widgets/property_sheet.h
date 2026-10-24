// Лист свойств (правая часть «Свойства комментария», FORMAT TEXT, «Свойства поля»): разделы с заголовком и
// строки «подпись — редактор»; подпись — buddy редактора (мнемоника &).
#pragma once

// <prsht.h> из <windows.h> делает PropertySheet макросом PropertySheetW (unity-сборка: windows.h соседа).
#ifdef PropertySheet
#undef PropertySheet
#endif

#include <QWidget>

#include <vector>

class QGridLayout;
class QLabel;

namespace ot::ui {

class PropertySheet : public QWidget {
    Q_OBJECT
public:
    explicit PropertySheet(QWidget* parent = nullptr);

    QLabel* addSection(const QString& title);
    QLabel* addRow(const QString& label, QWidget* editor);
    void addWidget(QWidget* widget);  // во всю ширину
    int rowCount() const { return rows_; }
    QWidget* editorAt(int row) const;

private:
    void applyMetrics();

    QGridLayout* grid_ = nullptr;
    int nextLine_ = 0;
    int rows_ = 0;
    std::vector<QLabel*> sections_;
    std::vector<QWidget*> editors_;
};

}  // namespace ot::ui
