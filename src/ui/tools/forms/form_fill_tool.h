// Заполнение формы на вкладке (план 6.1, п. 2; эталон 3.6, 6.1, 7.2; forms-01, forms-02, forms-03, forms-05,
// forms-15): FormFillTool живёт у каждой вкладки PDF (DocumentView::forms()) и включается сам, если в
// документе есть поля — отдельного инструмента выбирать не нужно, поля заполняются «Выделением», «Рукой» и
// «Панорамированием» (ui/view/field_interaction.h).
//
//   * Поля — снимок формы задачей (services/forms_ops.h snapshot: поля, виджеты, порядок Tab страниц); после
//     правки, отмены и записи — снимок заново (по поколению).
//   * Щелчок: текст, дата, combo, список — редактор поверх поля (ui/tools/forms/field_editors.h); флажок и
//     переключатель — переключение (шаг «Fill field»); кнопка — нажатие: ResetForm и JavaScript выполняет
//     ядро, URI — подтверждение ссылки (как /Link, эталон 7.4), GoTo/Named — переход, SubmitForm и ImportData
//     — сигналы окну (6.1, п. 3–4); пустое поле подписи — сигнал signatureFieldClicked (окно открывает диалог
//     подписи 4.6). Поле только для чтения, закрытое подписью или подписанное — не правится (серое при
//     подсветке).
//   * Tab/Shift+Tab — по порядку /Tabs страницы (R — ряды, C — столбцы, иначе /Annots), со страницы на
//     страницу; в группе переключателей — стрелки; пробел — флажок и кнопка.
//   * Слой (Overlay, z 8): подсветка полей цветом formField (forms-02; настройка forms/highlightFields и
//   кнопка
//     полосы), обязательные — рамка danger, только для чтения — серые, фокус — рамка accent
//     (forms/focusRect), поле, значение которого не принято, — рамка danger до следующей правки.
//   * Полосы: фиолетовая «Документ содержит поля [Подсветить поля]» (forms/showBar), жёлтая «XFA не
//     поддерживается», жёлтая «JavaScript отключён [Включить для этого файла]» — по политике js_policy
//     (services/js_prompter.h); включённый JavaScript — DocumentSession::setJavaScript с JsPrompter вкладки
//     (app.alert — окно JsAlertDialog, app.launchURL — подтверждение ссылки).
#pragma once

#include <QObject>
#include <QPointer>
#include <QString>

#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <vector>

#include "core/forms/data_io.h"
#include "core/forms/fields.h"
#include "core/forms/submit.h"
#include "core/forms/js_bridge.h"
#include "services/js_prompter.h"
#include "ui/view/field_interaction.h"
#include "ui/view/overlay.h"

namespace ot::ui {

class DocumentView;
class FieldEditor;
class MessageBar;
class PageView;

class FormFillTool : public QObject, public Overlay, public FieldInteraction {
    Q_OBJECT
public:
    explicit FormFillTool(DocumentView& view);
    ~FormFillTool() override;

    // Снимок формы пришёл (после открытия, правки, записи).
    bool isLoaded() const noexcept { return loaded_; }
    bool hasForm() const noexcept { return !form_.fields.empty(); }
    const forms::Form& form() const noexcept { return form_; }
    // Перечитать снимок (поколение растёт — устаревшие ответы отбрасываются).
    void reload();

    // Подсветка полей (forms-02): настройка forms/highlightFields.
    bool highlight() const noexcept { return highlight_; }
    void setHighlight(bool on);

    // JavaScript документа (эталон 7.2): решение политики и включение.
    forms::JsState jsState() const noexcept { return jsState_; }
    // «Включить для этого файла» (security-40): запомнить и включить.
    void enableJavaScriptForFile();
    JsPrompter& prompter() noexcept { return *prompter_; }
    // Ответчик app.alert (тесты, --script): пусто — окно JsAlertDialog.
    void setAlertPrompt(JsPrompter::Prompt prompt);

    // Поле в фокусе: страница и номер объекта виджета; нет — nullopt.
    struct Focus {
        int page = -1;
        int objnum = 0;
        bool operator==(const Focus&) const = default;
    };
    std::optional<Focus> focused() const noexcept { return focus_; }
    QString focusedField() const;
    FieldEditor* editor() const noexcept;
    // Перейти к полю (по имени; первый виджет): фокус, редактор у текстовых.
    bool focusField(const QString& name);
    // Как щелчок по виджету (тесты, панель «Поля» 6.3).
    bool activate(int page, int objnum);
    // Записать открытый редактор (Enter); cancel — бросить.
    void commitEditor(int move = 0);
    void cancelEditor();
    // «Очистить форму» (forms-06) — шаг «Reset form».
    void resetForm();

    // Данные формы (6.1, п. 3; forms-35, forms-36): «Экспорт данных…» — диалог FormDataExportDialog, запись —
    // exportDataTo; «Импорт данных…» — выбор файла, importDataFrom: разбор, подтверждение с именем файла и
    // числом значений (security-36), шаг «Import form data», отчёт (окно, если что-то не принято). Действие
    // кнопки ImportData — тот же импорт (путь — от папки документа).
    void exportData();
    void exportDataTo(const QString& path, forms::DataFormat format);
    void importData();
    void importDataFrom(const QString& path);
    using ImportConfirm = std::function<bool(const QString& file, int values)>;
    void setImportConfirm(ImportConfirm confirm) { importConfirm_ = std::move(confirm); }
    using FileChooser = std::function<QString()>;
    void setOpenChooser(FileChooser chooser) { openChooser_ = std::move(chooser); }
    // Окно отчёта импорта (тесты — выкл.: только сигнал dataImported).
    void setReportWindow(bool on) noexcept { reportWindow_ = on; }

    // Отправка формы (6.1, п. 4; forms-30): кнопка с /SubmitForm — план (адрес, формат, поля), подтверждение
    // SubmitDialog (без него ничего не уходит), тело (PDF — копия документа), mailto: — письмо (services/mail.h
    // mailer()), http(s) — HttpFetcher в рабочем потоке; ответ сервера с FDF — импорт в форму. В защищённом
    // просмотре — отказ (эталон 7.5).
    void submit(const forms::FieldAction& action);
    using SubmitConfirm = std::function<bool(const forms::SubmitPlan& plan)>;
    void setSubmitConfirm(SubmitConfirm confirm) { submitConfirm_ = std::move(confirm); }
    // Поля с непринятым значением (рамка danger).
    const std::set<QString>& rejected() const noexcept { return rejected_; }
    // Настройки «Формы» сменились (Установки, ОК).
    void settingsChanged();

    // FieldInteraction
    bool fieldPress(QMouseEvent* event) override;
    bool fieldRelease(QMouseEvent* event) override;
    std::optional<QCursor> fieldCursor(QPointF point) const override;
    bool fieldKey(QKeyEvent* event) override;
    bool wantsKey(const QKeyEvent* event) const override;
    bool focusNextField(bool next) override;

    // Overlay
    void paintPage(QPainter& painter, const PageView& view, int page) override;

signals:
    void loaded();
    // Значение поля записано (accepted — принято) или отклонено.
    void committed(const QString& field, bool accepted);
    void focusChanged();
    void submitRequested(const ot::forms::FieldAction& action);
    void importRequested(const ot::forms::FieldAction& action);
    void signatureFieldClicked(const QString& field);
    void dataExported(const QString& path, bool ok);
    void dataImported(const QString& file, const ot::forms::DataImportReport& report);
    void dataImportFailed(const QString& file, const QString& reason);
    // Отправка закончилась: ok — письмо создано или сервер принял; message — строка для человека.
    void submitted(bool ok, const QString& message);
    void javaScriptChanged(ot::forms::JsState state);

private:
    struct Hit {
        const forms::Field* field = nullptr;
        const forms::Widget* widget = nullptr;
    };
    Hit hitAt(QPointF viewportPoint) const;
    Hit find(int page, int objnum) const;
    bool interactive(const forms::Field& f) const;
    void applySnapshot(forms::Form form, std::map<int, std::vector<int>> tabs);
    void updateBars();
    void decideJavaScript();
    void setFocus(std::optional<Focus> focus);
    void openEditor(const forms::Field& field, const forms::Widget& widget);
    void closeEditor();
    void press(const forms::Field& field, const forms::Widget& widget);
    void handleAction(const forms::FieldAction& action);
    void afterWrite(const QString& field, const forms::SetResult& result);
    // Следующая остановка Tab после from (или первая); nullopt — нет.
    std::optional<Focus> nextStop(std::optional<Focus> from, bool forward) const;
    void moveFocus(bool forward);
    bool editable() const;
    void repaint();

    DocumentView& view_;
    PageView& page_;
    std::shared_ptr<JsPrompter> prompter_;  // мост сессии держит его (JsHost)
    forms::Form form_;
    std::map<int, std::vector<int>> tabs_;
    bool loaded_ = false;
    bool jsDecided_ = false;
    quint64 generation_ = 0;
    bool highlight_ = false;
    bool focusRect_ = true;
    forms::JsState jsState_ = forms::JsState::NoScripts;
    std::optional<Focus> focus_;
    std::optional<Focus> pressed_;
    bool popupOnRelease_ = false;
    QPointer<FieldEditor> editor_;
    std::set<QString> rejected_;
    ImportConfirm importConfirm_;
    FileChooser openChooser_;
    bool reportWindow_ = true;
    SubmitConfirm submitConfirm_;
};

}  // namespace ot::ui
