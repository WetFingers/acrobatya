// Диалог «Свойства документа» (план 2.5; эталон 3.1 «Свойства документа», 3.13, 6.3; view-45, view-46,
// view-47, edit-50, ocr-32): Ctrl+D, окно-модальный (open()), вкладки как в Acrobat —
//   Описание — файл, Название, Автор, Тема, Ключевые слова (правятся: /Info и XMP вместе), даты, приложение,
//              производитель, версия, размер, страницы, теги, быстрый просмотр; «Дополнительные метаданные…»
//              — редактор XMP (xmp_editor.h);
//   Защита — метод и шифрование, права /P; «Изменить…» — серая до 0.7;
//   Шрифты — обход ресурсов всех страниц (по первому показу вкладки задачей документа);
//   Начальный вид — панель навигации (/PageMode), макет (/PageLayout), увеличение и страница (/OpenAction),
//              окно и интерфейс (/ViewerPreferences);
//   Пользовательские — свои ключи /Info (и pdfx: в XMP);
//   Дополнительно — версия, линеаризация, теги, PDF/A, размер, объекты, Base URL (/URI /Base), язык (/Lang),
//              переплёт (/Direction), «Проверить» — предупреждения pdf_check_document.
// «ОК» пишет только изменённое — одним шагом журнала «Document properties» (services/props_ops.h: undo
// возвращает всё сразу; сохранение — инкрементальное, лёгкая правка). Документ «только для просмотра» (не
// PDF, PDF/A без «Включить редактирование») — поля только для чтения.
#pragma once

#include <QDialog>
#include <QFuture>

#include <memory>
#include <optional>

#include "services/props_ops.h"

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QSpinBox;
class QTabWidget;
class QTableWidget;
class QTreeWidget;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class DocumentView;
class XmpEditor;

class PropertiesDialog : public QDialog {
    Q_OBJECT
public:
    enum Tab { Description = 0, Security, Fonts, InitialViewTab, Custom, Advanced };

    // readOnly — поля только для чтения (не PDF, PDF/A в режиме чтения).
    PropertiesDialog(DocumentView& view, bool readOnly, QWidget* parent = nullptr);
    ~PropertiesDialog() override;

    void showTab(Tab tab);
    QTabWidget* tabs() const { return tabs_; }
    bool isLoaded() const { return loaded_; }
    bool fontsLoaded() const { return fontsLoaded_; }
    bool isReadOnly() const { return readOnly_; }
    const props::Sheet& sheet() const { return sheet_; }

    // Описание
    QLineEdit* titleEdit() const { return title_; }
    QLineEdit* authorEdit() const { return author_; }
    QLineEdit* subjectEdit() const { return subject_; }
    QLineEdit* keywordsEdit() const { return keywords_; }
    // Защита
    QLabel* securityMethodLabel() const { return method_; }
    QPushButton* changeSecurityButton() const { return changeSecurity_; }
    // Шрифты
    QTreeWidget* fontsTree() const { return fonts_; }
    // Начальный вид
    QComboBox* navigationBox() const { return navigation_; }
    QComboBox* layoutBox() const { return layout_; }
    QComboBox* magnificationBox() const { return magnification_; }
    QSpinBox* openPageBox() const { return openPage_; }
    QCheckBox* fitWindowBox() const { return fitWindow_; }
    QCheckBox* centerWindowBox() const { return centerWindow_; }
    QCheckBox* fullScreenBox() const { return fullScreen_; }
    QComboBox* showTitleBox() const { return showTitle_; }
    QCheckBox* hideMenubarBox() const { return hideMenubar_; }
    QCheckBox* hideToolbarBox() const { return hideToolbar_; }
    QCheckBox* hideWindowUiBox() const { return hideWindowUi_; }
    // Пользовательские
    QTableWidget* customTable() const { return custom_; }
    void addCustom(const QString& name, const QString& value);
    // Дополнительно
    QLineEdit* baseUrlEdit() const { return baseUrl_; }
    QComboBox* languageBox() const { return language_; }
    QComboBox* bindingBox() const { return binding_; }
    QPushButton* checkButton() const { return check_; }
    QPlainTextEdit* warningsView() const { return warnings_; }

    // Что запишет «ОК» (пусто — ничего не менялось).
    props::Changes changes() const;
    // Записать изменения (как «ОК», без закрытия); готовый QFuture — если менять нечего.
    QFuture<void> applyChanges();
    // «Дополнительные метаданные…»: редактор XMP поверх диалога (open()).
    XmpEditor* showXmpEditor();
    XmpEditor* xmpEditor() const;

signals:
    void loaded();
    void fontsReady();
    void checked();
    void applied();

public slots:
    void accept() override;

protected:
    void changeEvent(QEvent* event) override;

private:
    QWidget* buildDescription();
    QWidget* buildSecurity();
    QWidget* buildFonts();
    QWidget* buildInitialView();
    QWidget* buildCustom();
    QWidget* buildAdvanced();
    void fill(const props::Sheet& sheet);
    void loadFonts();
    void runCheck();
    InitialView collectInitialView() const;
    props::InfoFields collectInfo() const;
    QString collectLanguage() const;
    QString collectDirection() const;
    void setReadOnlyFields();
    void retranslate();

    std::shared_ptr<DocumentSession> session_;
    int pageCount_ = 0;
    bool readOnly_ = false;
    bool loaded_ = false;
    bool fontsLoaded_ = false;
    bool fontsRequested_ = false;
    props::Sheet sheet_;
    // Значения полей сразу после загрузки — «ОК» пишет только то, что отличается.
    props::InfoFields baseInfo_;
    InitialView baseInitial_;
    QString baseLang_;
    QString baseUrlValue_;
    QString baseDirection_;

    QTabWidget* tabs_ = nullptr;
    QLabel* file_ = nullptr;
    QLineEdit* title_ = nullptr;
    QLineEdit* author_ = nullptr;
    QLineEdit* subject_ = nullptr;
    QLineEdit* keywords_ = nullptr;
    QLabel* created_ = nullptr;
    QLabel* modified_ = nullptr;
    QLabel* creator_ = nullptr;
    QLabel* producer_ = nullptr;
    QLabel* version_ = nullptr;
    QLabel* location_ = nullptr;
    QLabel* fileSize_ = nullptr;
    QLabel* pageSize_ = nullptr;
    QLabel* pages_ = nullptr;
    QLabel* tagged_ = nullptr;
    QLabel* fastWeb_ = nullptr;
    QPushButton* moreMetadata_ = nullptr;
    QLabel* method_ = nullptr;
    QLabel* encryption_ = nullptr;
    QLabel* rights_[9] = {};
    QPushButton* changeSecurity_ = nullptr;
    QTreeWidget* fonts_ = nullptr;
    QComboBox* navigation_ = nullptr;
    QComboBox* layout_ = nullptr;
    QComboBox* magnification_ = nullptr;
    QSpinBox* openPage_ = nullptr;
    QCheckBox* fitWindow_ = nullptr;
    QCheckBox* centerWindow_ = nullptr;
    QCheckBox* fullScreen_ = nullptr;
    QComboBox* showTitle_ = nullptr;
    QCheckBox* hideMenubar_ = nullptr;
    QCheckBox* hideToolbar_ = nullptr;
    QCheckBox* hideWindowUi_ = nullptr;
    QTableWidget* custom_ = nullptr;
    QPushButton* addCustom_ = nullptr;
    QPushButton* removeCustom_ = nullptr;
    QLabel* advVersion_ = nullptr;
    QLabel* advLinearized_ = nullptr;
    QLabel* advTagged_ = nullptr;
    QLabel* advPdfa_ = nullptr;
    QLabel* advSize_ = nullptr;
    QLabel* advObjects_ = nullptr;
    QLabel* advRepaired_ = nullptr;
    QLineEdit* baseUrl_ = nullptr;
    QComboBox* language_ = nullptr;
    QComboBox* binding_ = nullptr;
    QPushButton* check_ = nullptr;
    QPlainTextEdit* warnings_ = nullptr;
    QPushButton* ok_ = nullptr;
    QPushButton* cancel_ = nullptr;
    XmpEditor* xmp_ = nullptr;
};

}  // namespace ot::ui
