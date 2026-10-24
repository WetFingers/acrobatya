// Saver — запись PDF по правилам эталона 4.4 (решение человека 12.6):
//  1. подписан (есть подписанное поле — у неподписанного поля подписи ломать нечего) → только инкремент;
//  тяжёлая
//     правка, смена шифрования, полная запись или файл, который нельзя дописать (ремонт), на подписанном →
//     SaveError::WouldBreakSignatures (интерфейс объясняет и предлагает «Сохранить как копию» —
//     allowBreakingSignatures);
//  2. не подписан, правки лёгкие (аннотации, формы, метаданные, закладки, OCR-слой, оформление) → инкремент;
//  3. тяжёлые (страницы, картинки, редактор текста, уменьшение размера — UndoStack::hasHeavyChanges) →
//     FullOptimized: garbage=deduplicate, compress, compress-fonts, compress-images, objstms;
//  4. смена шифрования, восстановленный файл (pdf_can_be_saved_incrementally = 0), новый документ → Full
//     (garbage=compact, compress).
// do_incremental несовместим с garbage; do_linear — никогда (линеаризация — QpdfTool поверх записи).
// Запись — во временный файл в папке цели (_wfopen → fz_output), затем MoveFileExW(REPLACE_EXISTING |
// WRITE_THROUGH). Запись меняет xref документа в памяти, поэтому после неё документ переоткрывается из
// сохранённого файла в том же объекте Document (журнал и история — с нуля): «Сохранить» в свой путь — файл
// документа отпускается перед заменой; если заменить не удалось (файл занят), документ открывается из
// временного файла с прежним путём — правки не теряются, следующее сохранение уберёт временный.
#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>

namespace ot {

class Document;

enum class SaveMode { Auto, Incremental, Full, FullOptimized };

enum class SaveError {
    None,
    NotPdf,                  // XPS/EPUB/картинка — только «Экспорт»
    NoPath,                  // новый документ: нужен «Сохранить как»
    WouldBreakSignatures,    // подписанный документ: запись заденет подпись
    IncrementalNotPossible,  // инкремент запрошен, а файл восстановлен, новый или меняется шифрование
    WriteFailed,             // MuPDF или диск при записи временного файла
    LinearizeFailed,         // qpdf --linearize
    ReplaceFailed,           // MoveFileExW: файл цели занят или нет прав — документ открыт из временного
    ReopenFailed,            // сохранённый файл не открылся (документ закрыт)
    SameAsDocument,          // «Сохранить как копию» поверх файла документа — это «Сохранить»
};

// Новое шифрование («Сохранить как» с защитой паролем, 7.6): без него шифрование файла сохраняется как есть.
struct SaveEncryption {
    enum class Method { None, Aes128, Aes256 };
    Method method = Method::Aes256;
    std::string userPassword;   // UTF-8; пусто — открывается без пароля
    std::string ownerPassword;  // UTF-8
    int permissions = -4;       // /P (−4 — всё разрешено)
};

struct SaveOptions {
    SaveMode mode = SaveMode::Auto;
    std::optional<SaveEncryption> encryption;
    bool allowBreakingSignatures = false;  // «Сохранить как копию» подписанного с полной записью
    bool linearize = false;                // «Быстрый просмотр в Интернете» (qpdf; подписанный — нельзя)
    // «Сохранить как → Параметры» (2.5): уточнения полной записи (Full, FullOptimized; инкремент их не
    // применяет); nullopt — как решил режим.
    std::optional<bool> objectStreams;   // потоки объектов (do_use_objstms) — меньше файл
    std::optional<bool> compressImages;  // сжать несжатые картинки (do_compress_images)
    bool subsetFonts = false;  // встроенные шрифты — подмножествами (pdf_subset_fonts) перед записью
    // Подготовка документа перед полной записью (после решения о режиме, до subsetFonts): редактор урезает
    // шрифты, которые встроил сам (core/edit/font_embed.h). Исключение — как ошибка записи.
    std::function<void(Document&)> prepare;
};

struct SaveReport {
    SaveError error = SaveError::None;
    SaveMode mode = SaveMode::Auto;  // режим, которым записано (или выбранный, если отказ)
    std::uintmax_t bytes = 0;        // размер файла
    std::string message;             // английский текст для журнала
    bool ok() const noexcept { return error == SaveError::None; }
};

class Saver {
public:
    // Режим по правилам выше (без записи): что сделает save(). Отказ — в report.error.
    static SaveReport decide(Document& doc, const SaveOptions& options = {});

    // Записать документ в target (свой путь — «Сохранить», другой — «Сохранить как»: документ переходит на
    // новый файл) и переоткрыть. Ошибки — в отчёте, исключений нет (кроме нехватки памяти).
    static SaveReport save(Document& doc, const std::filesystem::path& target,
                           const SaveOptions& options = {});
    // «Сохранить» в путь документа.
    static SaveReport save(Document& doc, const SaveOptions& options = {});
    // «Сохранить как копию» (2.4b; --script doc.saveCopy): записать документ с несохранёнными правками в
    // target, а сам документ оставить на своём файле — с правками и историей undo. Запись открытого документа
    // перестроила бы его xref (и сбросила журнал), поэтому пишется второй документ: снимок журнала (Autosave
    // во временной папке, без копии исходника) переигрывается на исходном файле, без правок — исходный файл
    // открывается заново; режим — как решил бы decide для самого документа. Нет файла (новый, из памяти) —
    // NoPath, target — файл документа — SameAsDocument.
    static SaveReport saveCopy(Document& doc, const std::filesystem::path& target,
                               const SaveOptions& options = {});

    static const char* modeName(SaveMode mode) noexcept;
    static const char* errorName(SaveError error) noexcept;
};

}  // namespace ot
