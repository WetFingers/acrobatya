// Найти и заменить в ядре (эталон 3.9 «Найти и заменить», план 3.3, п. 5; edit-05 — ядро, view-18 — замена):
// поиск — тем же searchPage, что у панели «Найти» (core/text/search.h, слой SearchSession::textOptions()),
// совпадение сопоставляется с глифами модели страницы по точке пера (символы, которых в модели нет, —
// пробелы, добавленные fz_stext, — пропускаются), глиф → абзац (blocksForPage) → позиции в ParagraphEditor →
// replace → перевёрстка и запись Rewriter. Совпадение через границу абзацев (или страниц), в абзаце «только
// чтение» и не сопоставленное с глифами — пропуск с причиной (отчёт и предпросмотр).
//
// «Заменить все» — одна операция журнала на страницу: абзацы страницы по одному, каждый находится заново по
// своему первому глифу (запись абзаца глифы соседей не двигает), замены в абзаце — с конца. Замена, в которой
// снова есть образец, не зацикливается: совпадения считаются один раз до правки.
//
// Всё — в потоке документа.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "core/edit/edit_types.h"
#include "core/edit/paragraph_editor.h"
#include "core/text/search.h"
#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::edit {

class Rewriter;

enum class ReplaceSkip : std::uint8_t {
    None,
    CrossBlock,  // совпадение через границу абзацев или страниц
    ReadOnly,    // абзац «только чтение» (причина — readOnly)
    NoGlyphs,    // символы совпадения не нашлись среди глифов модели (форма, скрытый слой…)
    Failed,      // запись не удалась (ошибка Rewriter)
};
const char* replaceSkipCode(ReplaceSkip skip) noexcept;  // "", "crossBlock", "readOnly", "noGlyphs", "failed"

// Совпадение для предпросмотра и замены.
struct ReplaceMatch {
    SearchHit hit;        // страница, символы слоя поиска, квады подсветки
    int block = -1;       // абзац — индекс в blocksForPage(модель страницы)
    TextRange range;      // позиции в тексте ParagraphEditor абзаца
    PointF anchor;        // точка пера первого глифа абзаца (координаты страницы) — найти абзац заново
    std::string found;    // найденный текст абзаца (UTF-8)
    std::string context;  // фрагмент абзаца: совпадение между \x02 и \x03 (как searchSnippet)
    std::string result;   // тот же фрагмент после замены (замена между \x02 и \x03)
    ReplaceSkip skip = ReplaceSkip::None;
    std::string readOnly;  // код причины «только чтение» (readOnlyCode)

    bool replaceable() const noexcept { return skip == ReplaceSkip::None; }
};

struct ReplaceReport {
    int found = 0;                      // совпадений
    int replaced = 0;                   // заменено
    std::vector<int> pages;             // страницы, где была замена
    std::vector<ReplaceMatch> skipped;  // пропущенные с причиной
    std::vector<FontNotice> notices;    // сообщения о шрифтах набора
    std::vector<RectF> dirty;  // перерисовать (по рамке на абзац; координаты страницы своей страницы)
    std::string message;       // английский журнал первой ошибки
};

struct ReplaceProgress {
    int pagesDone = 0;
    int pageCount = 0;
    int replaced = 0;
};

class Replacer {
public:
    using ProgressFn = std::function<void(const ReplaceProgress&)>;
    using CancelFn = std::function<bool()>;

    // rewriter — над тем же документом (шрифты набора — его).
    Replacer(Document& doc, Rewriter& rewriter);

    // Совпадения страницы page с предпросмотром замены на replacement.
    std::vector<ReplaceMatch> find(int page, std::string_view needle, const SearchOptions& options,
                                   std::string_view replacement = {});
    // Совпадения документа (предпросмотр «Заменить все»); cancel — между страницами.
    std::vector<ReplaceMatch> findAll(std::string_view needle, const SearchOptions& options,
                                      std::string_view replacement = {}, const CancelFn& cancel = {});

    // Заменить одно совпадение (из find на той же странице без правок между ними) — правка operation.
    ReplaceReport replaceOne(const ReplaceMatch& match, std::string_view replacement,
                             const std::string& operation = "Replace text");
    // Заменить все совпадения страницы — одна операция журнала operation.
    ReplaceReport replacePage(int page, std::string_view needle, const SearchOptions& options,
                              std::string_view replacement, const std::string& operation = "Replace all");
    // Заменить все в документе: страница за страницей (своя операция на каждую), прогресс и отмена между
    // страницами.
    ReplaceReport replaceAll(std::string_view needle, const SearchOptions& options,
                             std::string_view replacement, const ProgressFn& progress = {},
                             const CancelFn& cancel = {});

private:
    Document& doc_;
    Rewriter& rewriter_;
};

}  // namespace ot::edit
