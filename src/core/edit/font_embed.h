// Встраивание системных шрифтов и подмножества (план 3.2, п. 3; эталон 3.9 «FontResolver», правила 2–4;
// edit-01, edit-02 — шрифты нового текста). Новый шрифт — системный файл (core/edit/system_fonts.h),
// загруженный FontLibrary и записанный в документ:
//   * Cid — Type0 / Identity-H с /ToUnicode (pdf_add_cid_font; CID = GID, CIDToGIDMap Identity) — для любых
//     символов (правила 2 и 3, правило 4 вне WinAnsi);
//   * SimpleWinAnsi — простой TrueType /WinAnsiEncoding (pdf_add_simple_font) — латиница правила 4 (замена
//     невстроенного base14 метрически совместимым системным шрифтом).
// MuPDF пишет программу шрифта целиком; дескриптор (/Flags, /ItalicAngle, /StemV, /CapHeight) FontEmbedder
// доводит по таблицам post/OS/2, а /ToUnicode переписывает: у MuPDF глиф, общий для нескольких символов,
// получает последний (пробел — U+00A0, дефис — мягкий перенос U+00AD), у нас — наименьший код.
//
// Один объект шрифта на документ на грань и вид, пока он не урезан (кеш FontEmbedder; кеш ресурсов MuPDF
// pdf_find_font_resource журналом не откатывается и после подмножества указывает на урезанный файл — его
// запись перед добавлением забывается); на странице — свой ресурс «OtFn» в /Resources/Font. Правка — операция
// журнала «Add font» (внутри внешней операции — её часть).
//
// Подмножество при сохранении — subsetFonts: только шрифты, встроенные FontEmbedder (pdf_subset_fonts MuPDF
// пересобрал бы все встроенные шрифты документа, включая шрифты форм /DR, и с pages считает глифы только этих
// страниц), той же fz_subset_ttf_for_gids; глифы — по модели содержимого всех страниц, где шрифт в ресурсах;
// /W и /ToUnicode — только использованные глифы, имя — с префиксом «ABCDEF+». Шрифт, который не урезался,
// остаётся полным (spdlog); весь шаг — операция журнала «Subset fonts», ошибка откатывает её целиком.
//
// Всё — в потоке документа.
#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "core/edit/system_fonts.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {
class Document;
}

namespace ot::edit {

enum class EmbedKind : std::uint8_t { Cid, SimpleWinAnsi };
const char* embedKindCode(EmbedKind kind) noexcept;  // "cid", "winansi"

// Загруженные файлы системных шрифтов (fz_font) по файлу и грани — один раз на процесс редактирования.
class FontLibrary {
public:
    explicit FontLibrary(fz_context* ctx);
    ~FontLibrary();
    FontLibrary(const FontLibrary&) = delete;
    FontLibrary& operator=(const FontLibrary&) = delete;

    fz_context* context() const noexcept { return ctx_; }
    // Шрифт грани; nullptr — файл не прочитался или не шрифт (в журнал).
    fz_font* font(const SystemFace& face);
    // GID символа (fz_encode_character), 0 — глифа нет.
    int gid(const SystemFace& face, char32_t u);
    bool hasGlyph(const SystemFace& face, char32_t u) { return gid(face, u) > 0; }
    // Ширина глифа в долях кегля — fz_advance_glyph.
    float advance(const SystemFace& face, int gid);
    // Шрифт можно встроить (pdf_font_writing_supported: TrueType/OpenType, fsType разрешает).
    bool embeddable(const SystemFace& face);

private:
    fz_context* ctx_;
    std::map<std::pair<std::filesystem::path, int>, fz_font*> fonts_;
};

// Встроенный шрифт на странице.
struct EmbeddedFont {
    std::string resource;  // имя в /Resources/Font страницы
    int objnum = 0;        // словарь шрифта
    EmbedKind kind = EmbedKind::Cid;
    SystemFace face;
    bool valid() const noexcept { return objnum > 0 && !resource.empty(); }
};

struct SubsetReport {
    int fonts = 0;                                // шрифтов к урезанию
    int subset = 0;                               // урезано
    int unused = 0;                               // глифов нет ни на одной странице — ресурсы страниц сняты
    int failed = 0;                               // остались полными
    std::size_t bytesBefore = 0, bytesAfter = 0;  // программы шрифтов
    std::vector<std::string> errors;              // английский журнал
};

class FontEmbedder {
public:
    FontEmbedder(Document& doc, FontLibrary& library);

    // Шрифт грани face вида kind в ресурсах страницы page (0 — первая). Ошибка MuPDF — MuError; грань нельзя
    // встроить или файл не читается — std::runtime_error.
    EmbeddedFont add(int page, const SystemFace& face, EmbedKind kind);
    // Только словарь шрифта (номер объекта) — без ресурса страницы: шрифт /DA текстового поля комментария
    // (core/annot/freetext.h). Ошибки — как у add.
    int fontObject(const SystemFace& face, EmbedKind kind);
    // Встраивание — «тяжёлая» правка (полная запись файла, 3.2); шрифт поля комментария и Fill & Sign (5.3) —
    // лёгкая: дописывается инкрементом и не задевает подписи.
    void setHeavy(bool heavy) noexcept { heavy_ = heavy; }
    // Код символа для строки этим шрифтом (Cid — GID двумя байтами, SimpleWinAnsi — байт cp1252); пусто —
    // глифа нет (или символа нет в WinAnsi).
    std::string encode(const EmbeddedFont& font, char32_t u);
    // Ширина символа в долях кегля — как её запишет /W или /Widths (trunc(1000 · advance) / 1000).
    float width(const EmbeddedFont& font, char32_t u);
    // Шрифты, встроенные и ещё не урезанные (номера словарей).
    std::vector<int> pending() const;
    // Подмножества pending() (subsetFonts); урезанные больше не переиспользуются — следующий add встроит
    // новый.
    SubsetReport subset();

private:
    struct Entry {
        SystemFace face;
        EmbedKind kind = EmbedKind::Cid;
        int objnum = 0;
        std::string baseFont;
        bool done = false;  // урезан
    };
    bool alive(const Entry& e) const;
    std::size_t entryFor(const SystemFace& face, EmbedKind kind);

    Document& doc_;
    FontLibrary& lib_;
    std::vector<Entry> entries_;
    bool heavy_ = true;
    std::map<std::pair<int, std::size_t>, std::string> resources_;  // (страница, запись) → ресурс
};

// Подмножества шрифтов fontObjnums (встроенных FontEmbedder) по глифам всех страниц документа — см. начало
// файла.
SubsetReport subsetFonts(Document& doc, const std::vector<int>& fontObjnums);
// Один шрифт objnum (встроенный FontEmbedder) — до глифов gids (0 — всегда), независимо от страниц:
// программа, /W и /ToUnicode — только они, имя — с префиксом подмножества. Операция «Subset fonts» (внутри
// внешней — её часть). Не вышло — false, шрифт полный (spdlog).
bool subsetFont(Document& doc, int objnum, const std::set<int>& gids, bool heavy = true);

}  // namespace ot::edit
