// Document — открытый документ MuPDF (эталон 4.2): открыть (путь UTF-16 → fz_open_file_w, пароль),
// флаги открытия (ремонт, PDF/A, подписи, форма, XFA), страницы (рамка с учётом /Rotate, поворот),
// свойства (/Info, версия, линеаризация, шифрование, права /P), растр страницы.
// Документ живёт в своём контексте — клоне базового (по умолчанию MuContext::global()): общий store и
// замки, свой стек ошибок. Поток: один документ — один поток одновременно (правило MuPDF); синхронный.
// PDF правится и сохраняется; XPS/EPUB/CBZ/FB2/изображения/TXT/HTML — только просмотр (isPdf() == false).
// У PDF журнал правок MuPDF включается сразу при открытии (эталон 4.3): правка — только внутри
// Journal::Operation (core/doc/journal.h), история — undoStack().
#pragma once

#include <filesystem>
#include <memory>
#include <stdexcept>
#include <string>

#include "core/mu/error.h"
#include "core/render/bitmap.h"
#include "core/util/geometry.h"

struct fz_document;
struct pdf_document;

namespace ot {

class MuContext;
class Document;
class Saver;
class UndoStack;

struct PageSize {
    float width = 0;  // в пунктах (1/72 дюйма), с учётом /Rotate
    float height = 0;
};

struct RenderOptions {
    float dpi = 72.0f;
    bool annotations = true;  // false — только содержимое страницы (fz_run_page_contents)
};

// Права из /P (эталон 7.1); владелец (пароль владельца) может всё.
enum class Perm { Print, PrintHighRes, Copy, Edit, Annotate, FillForms, Accessibility, Assemble };

struct DocError {
    ErrorKind kind = ErrorKind::None;
    int code = 0;         // FZ_ERROR_* (0 — ошибка не из MuPDF, например NotFound)
    std::string message;  // английский текст MuPDF или наш — для журнала, не для интерфейса
};

struct OpenResult {
    std::unique_ptr<Document> document;  // nullptr — не открылся (error); при needsPassword — есть
    bool ok = false;                     // открыт и читается (пароль не нужен или подошёл)
    bool needsPassword = false;          // зашифрован, пароль не задан или не подошёл → authenticate()
    bool repaired = false;               // MuPDF чинил xref: инкрементальное сохранение запрещено
    bool pdfa = false;                   // заявлен PDF/A (XMP pdfaid:part)
    bool isSigned = false;               // есть подписанное поле подписи
    bool hasForm = false;                // есть поля AcroForm, кроме подписей
    bool hasXfa = false;                 // /AcroForm /XFA — «XFA не поддерживается»
    DocError error;
};

// Журнал автосохранения не подходит к файлу (другой исходник: MuPDF сверяет отпечаток xref).
class JournalMismatch : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

struct DocProperties {
    // /Info в UTF-8; даты — как в файле ("D:20260101120000+03'00'")
    std::string title, author, subject, keywords, creator, producer, creationDate, modDate;
    std::string format;  // "PDF 1.7", "XPS", "CBZ", "Image"… (FZ_META_FORMAT)
    int pdfVersion = 0;  // 17 для PDF 1.7, 20 для PDF 2.0; 0 — не PDF
    int pageCount = 0;
    bool linearized = false;  // «Быстрый просмотр в Интернете»
    bool encrypted = false;
    std::string encryption;      // "Standard V5 R6 256-bit AES" (FZ_META_ENCRYPTION) или пусто
    int encryptionRevision = 0;  // /R
    int permissionsP = -4;       // /P как в файле (без шифрования — −4: всё разрешено)
    bool ownerAuthenticated = false;
    std::string pdfa;  // "PDF/A-2B" или пусто
};

class Document {
public:
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Открыть файл: fz_open_document_with_stream(fz_open_file_w(path)). Формат — по расширению
    // (".pdf", ".xps", ".png"…; неизвестное — как PDF) и по содержимому. Пароль (UTF-8) пробуется, если
    // документ зашифрован. Не бросает: ошибка — в OpenResult::error.
    static OpenResult open(const std::filesystem::path& path, const std::string& password = {});
    static OpenResult open(MuContext& base, const std::filesystem::path& path,
                           const std::string& password = {});
    // Открыть из памяти (magic — расширение или MIME).
    static OpenResult openMemory(std::string bytes, const std::string& magic = ".pdf",
                                 const std::string& password = {});
    static OpenResult openMemory(MuContext& base, std::string bytes, const std::string& magic = ".pdf",
                                 const std::string& password = {});
    // То же для инструментов и тестов: бросает MuError (MuPDF) или std::runtime_error (нет файла, нужен
    // пароль).
    static std::unique_ptr<Document> openOrThrow(const std::filesystem::path& path,
                                                 const std::string& password = {});
    static std::unique_ptr<Document> openOrThrow(MuContext& base, const std::filesystem::path& path,
                                                 const std::string& password = {});
    static std::unique_ptr<Document> openOrThrowMemory(std::string bytes, const std::string& magic = ".pdf");
    // Восстановить несохранённые правки после падения (эталон 4.5; Autosave::recover): открыть source
    // (исходный файл или его копию из автосохранения) без журнала, загрузить журнал MuPDF (байты
    // pdf_write_journal с позицией 0) и вернуть position шагов — правки на месте, история undo/redo та же,
    // файл не сохранён. Путь документа — documentPath. Журнал от другого файла — JournalMismatch; прочее —
    // MuError / std::runtime_error.
    static std::unique_ptr<Document> recoverFrom(const std::filesystem::path& source,
                                                 const std::string& journal, int position,
                                                 const std::filesystem::path& documentPath,
                                                 const std::string& password = {});

    // Новый PDF в памяти: pages пустых страниц A4 (595 × 842 pt).
    static std::unique_ptr<Document> createBlank(int pages = 1);
    static std::unique_ptr<Document> createBlank(MuContext& base, int pages = 1);

    bool needsPassword() const;
    // true — пароль подошёл (пользователя или владельца); пароль в UTF-8.
    bool authenticate(const std::string& password);

    bool isPdf() const noexcept;
    // Журнал правок включён (PDF); история undo/redo — undoStack() (не PDF — std::logic_error).
    bool journaled() const noexcept { return undo_ != nullptr; }
    UndoStack& undoStack();
    bool repaired() const;  // pdf_was_repaired: файл чинили при открытии или позже
    bool isPdfA() const;
    bool isSigned() const;
    bool hasForm() const;
    bool hasXfa() const;
    // Действующее право (core/doc/permissions.h: правила /P, пароль владельца — всё).
    bool allowed(Perm perm) const;
    DocProperties properties() const;
    // Записать поля описания (Title, Author, Subject, Keywords, Creator, Producer, CreationDate; ModDate —
    // время записи) в /Info и XMP (core/doc/props.h); пользовательские ключи остаются. Операция журнала
    // «Document properties». Не PDF — std::logic_error.
    void setProperties(const DocProperties& props);

    int pageCount() const;
    // Рамка страницы в пунктах с учётом /CropBox и /Rotate (fz_bound_page).
    RectF pageBox(int index) const;
    // /Rotate, приведённый к 0, 90, 180, 270 (у не-PDF — 0).
    int pageRotation(int index) const;
    PageSize pageSize(int index) const;
    Bitmap renderPage(int index, const RenderOptions& options = {}) const;

    // Полная запись PDF в память (только PDF) — для тестов. Запись меняет xref документа (сплошная таблица,
    // инкрементальные секции считаются сохранёнными), и прежний журнал отменять уже нельзя: история undo
    // сбрасывается. Сохранение в файл — Saver (с переоткрытием).
    std::string savePdfToMemory();

    // Путь документа (пусто — новый или из памяти). Файл, открытый на самом деле, может быть временным файлом
    // неудачной замены (Saver: файл цели был занят) — backingFile(); его удаляет следующее сохранение или
    // закрытие документа.
    const std::filesystem::path& path() const noexcept { return path_; }
    const std::filesystem::path& backingFile() const noexcept { return backing_; }
    // Файл, открытый на самом деле: путь документа, временный файл неудачной замены или копия исходника из
    // автосохранения (восстановление); пусто — документ из памяти или новый.
    const std::filesystem::path& filePath() const noexcept { return file_; }
    // Пароль, которым документ открыт (UTF-8) — для переоткрытия после сохранения.
    const std::string& password() const noexcept { return password_; }
    fz_document* handle() const noexcept { return doc_; }
    // pdf_document того же документа; nullptr — не PDF.
    pdf_document* pdf() const noexcept;
    MuContext& context() const noexcept { return *mu_; }

private:
    friend class Saver;

    Document(std::unique_ptr<MuContext> mu, fz_document* doc, std::string bytes, std::filesystem::path path);
    static OpenResult finishOpen(std::unique_ptr<Document> doc, const std::string& password);

    // Saver: отпустить файл (fz_document и журнал) перед заменой и открыть сохранённый в том же объекте.
    void release() noexcept;
    // Открыть file (PDF) с паролем; путь документа — documentPath, временный файл — backing (или пусто).
    // Ошибка — исключение, документ тогда закрыт.
    void reopen(const std::filesystem::path& file, const std::string& password,
                const std::filesystem::path& documentPath, const std::filesystem::path& backing = {});

    std::unique_ptr<MuContext> mu_;  // клон базового контекста — только этого документа
    fz_document* doc_ = nullptr;
    std::string memory_;  // данные openMemory живут, пока жив документ
    std::filesystem::path path_;
    std::filesystem::path backing_;  // временный файл неудачной замены, удаляется деструктором
    std::filesystem::path file_;
    std::string password_;
    bool ownerAuthenticated_ = false;
    std::unique_ptr<UndoStack> undo_;  // журнал PDF (pdf_enable_journal)
};

}  // namespace ot
