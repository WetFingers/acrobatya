// Сохранение в интерфейсе (план 2.5; эталон 4.4, 4.5, 3.1 «Открытие»; security-09): «Сохранить» (Ctrl+S) —
// Saver::Auto: сначала decide (services/save_ops.h) без записи —
//   новый документ без файла → «Сохранить как»; не PDF → «Сохранить как PDF» окна;
//   подписанный, а запись сломала бы подписи → предупреждение «Подписи станут недействительными» с «Сохранить
//   как копию…» (полная запись в другой файл с allowBreakingSignatures; документ остаётся на своём файле с
//   правками) или «Отмена»;
//   иначе — запись (восстановленный файл — полная: полоса уже предупредила).
// «Сохранить как…» (Ctrl+Shift+S) — диалог параметров (save_as_dialog.h) → DocumentSession::saveAs; документ
// переходит на новый файл. Ошибки — тостом (код SaveError → русская строка). «Операция с перезаписью»
// (объединение, уменьшение размера, OCR «Exact», экспорт, Office — этапы 7–9) создаёт новый документ «Без
// имени» без отмены: rewriteNotice — полоса об этом на его виде. Ответы диалогов тесты подставляют
// (setSignaturePrompt, setSaveAsPrompt) — сохранение синхронное (ждёт задачу с циклом событий).
#pragma once

#include <QObject>
#include <QString>

#include <functional>

#include "core/doc/saver.h"

namespace ot::ui {

class DocumentView;
class MainWindow;
class MessageBar;
class SaveAsDialog;

class SaveFlow : public QObject {
    Q_OBJECT
public:
    enum class SignatureAnswer { SaveCopy, Cancel };
    using SignaturePrompt = std::function<SignatureAnswer(DocumentView&)>;
    // Показать диалог «Сохранить как» (по умолчанию — exec()); true — «Сохранить».
    using SaveAsPrompt = std::function<bool(SaveAsDialog&)>;

    explicit SaveFlow(MainWindow& window);

    void setSignaturePrompt(SignaturePrompt prompt) { signature_ = std::move(prompt); }
    void setSaveAsPrompt(SaveAsPrompt prompt) { saveAs_ = std::move(prompt); }

    // Ctrl+S. true — записано (или нечего записывать).
    bool save(DocumentView& view);
    // Ctrl+Shift+S; copy — «Сохранить как копию» (документ остаётся на своём файле).
    bool saveAs(DocumentView& view, bool copy = false);
    // Что сделает «Сохранить» (без записи).
    SaveReport decide(DocumentView& view, const SaveOptions& options = {});

    // Код ошибки → строка для человека.
    static QString errorText(SaveError error);
    QString lastError() const { return error_; }

    // Полоса нового документа после операции с перезаписью: «Создан новый документ… Отменить операцию нельзя;
    // исходный файл не изменён» (objectName rewriteNoticeBar).
    static MessageBar* rewriteNotice(DocumentView& view, const QString& operation);

signals:
    void saved(ot::ui::DocumentView* view, const QString& path);

private:
    bool write(DocumentView& view, const QString& path, const SaveOptions& options, bool copy);
    void fail(DocumentView& view, const SaveReport& report, const QString& target);

    MainWindow& window_;
    SignaturePrompt signature_;
    SaveAsPrompt saveAs_;
    QString error_;
};

}  // namespace ot::ui
