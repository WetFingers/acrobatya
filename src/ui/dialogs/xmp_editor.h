// Редактор XMP «Дополнительные метаданные» (план 2.5; эталон 3.1 «Свойства документа», 3.13 «Метаданные XMP»,
// 6.3; view-46, edit-50): категории слева (как «Установки») —
//   Описание — Название, Автор, Тема, Ключевые слова (те же поля, что в «Свойствах»: /Info и XMP вместе);
//   Пользовательские — свои свойства (/Info и pdfx: в XMP): добавить, изменить, удалить;
//   Дополнительно — все свойства пакета по схемам (dc, xmp, pdf, pdfx, pdfaid, xmpMM…) — только чтение;
//   XMP — сам пакет (только чтение).
// «ОК» возвращает значения в диалог «Свойства» — записывает их «ОК» «Свойств» одним шагом журнала
// (сериализатор XMP 1.3 правит только свои свойства: pdfaid, чужие схемы и обёртка остаются).
#pragma once

#include <QDialog>
#include <QList>
#include <QString>

#include <string>
#include <utility>

class QLineEdit;
class QListWidget;
class QPlainTextEdit;
class QPushButton;
class QStackedWidget;
class QTableWidget;
class QTreeWidget;

namespace ot::ui {

class XmpEditor : public QDialog {
    Q_OBJECT
public:
    enum Category { Description = 0, Custom, Advanced, Raw };

    explicit XmpEditor(QWidget* parent = nullptr);

    // Заполнить: пакет XMP документа, поля описания и свои свойства (как в «Свойствах» сейчас).
    void load(const std::string& packet, const QString& title, const QString& author, const QString& subject,
              const QString& keywords, const QList<std::pair<QString, QString>>& custom, bool readOnly);
    void showCategory(Category category);

    QString title() const;
    QString author() const;
    QString subject() const;
    QString keywords() const;
    // Свои свойства по порядку таблицы (пустые имена пропускаются).
    QList<std::pair<QString, QString>> customProperties() const;
    void addCustom(const QString& name, const QString& value);

    QListWidget* categories() const { return nav_; }
    QTableWidget* customTable() const { return custom_; }
    QTreeWidget* advancedTree() const { return advanced_; }
    QPlainTextEdit* rawView() const { return raw_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();

    QListWidget* nav_ = nullptr;
    QStackedWidget* pages_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* author_ = nullptr;
    QLineEdit* subject_ = nullptr;
    QLineEdit* keywords_ = nullptr;
    QTableWidget* custom_ = nullptr;
    QPushButton* add_ = nullptr;
    QPushButton* remove_ = nullptr;
    QTreeWidget* advanced_ = nullptr;
    QPlainTextEdit* raw_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
};

}  // namespace ot::ui
