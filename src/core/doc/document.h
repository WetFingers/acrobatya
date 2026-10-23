// Document — открытый документ MuPDF (эталон 4.2). Этап 0: открыть (путь UTF-16 → fz_open_file_w,
// пароль), число страниц, размер страницы, растр страницы; создать пустой PDF в памяти.
// Поток: один документ — один поток одновременно (правило MuPDF); синхронный.
#pragma once

#include <filesystem>
#include <memory>
#include <string>

#include "core/render/bitmap.h"

struct fz_document;

namespace ot {

class MuContext;

struct PageSize {
    float width = 0;  // в пунктах (1/72 дюйма), с учётом /Rotate
    float height = 0;
};

struct RenderOptions {
    float dpi = 72.0f;
    bool annotations = true;  // false — только содержимое страницы (fz_run_page_contents)
};

class Document {
public:
    ~Document();
    Document(const Document&) = delete;
    Document& operator=(const Document&) = delete;

    // Открыть файл. Формат — по расширению (".pdf", ".xps", ".png"…), неизвестное — как PDF.
    // Бросает MuError (повреждён, не поддерживается) и std::runtime_error (нет файла).
    static std::unique_ptr<Document> open(MuContext& mu, const std::filesystem::path& path);
    // Открыть из памяти (magic — расширение или MIME).
    static std::unique_ptr<Document> openMemory(MuContext& mu, std::string bytes,
                                                const std::string& magic = ".pdf");
    // Новый PDF в памяти: pages пустых страниц A4 (595 × 842 pt).
    static std::unique_ptr<Document> createBlank(MuContext& mu, int pages = 1);

    bool needsPassword() const;
    // true — пароль подошёл (пользователя или владельца).
    bool authenticate(const std::string& password);

    int pageCount() const;
    PageSize pageSize(int index) const;
    Bitmap renderPage(int index, const RenderOptions& options = {}) const;

    // Сериализация PDF (только для документов PDF) — для тестов и «Сохранить как» этапа 0.
    std::string savePdfToMemory() const;

    fz_document* handle() const noexcept { return doc_; }
    MuContext& context() const noexcept { return mu_; }

private:
    Document(MuContext& mu, fz_document* doc, std::string bytes);
    MuContext& mu_;
    fz_document* doc_ = nullptr;
    std::string memory_;  // данные openMemory живут, пока жив документ
};

}  // namespace ot
