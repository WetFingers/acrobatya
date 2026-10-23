// Поиск текста (эталон 3.2, план 1.4) — новый поиск MuPDF fz_new_search: регистр (ignore-case), диакритика
// (ignore-diacritics), регулярные выражения (regexp — синтаксис JavaScript; \w и \b — только ASCII). MuPDF
// приводит и текст, и образец к NFKD с обратной сборкой: лигатура «ﬁ» находится как «fi», «й» — как «й» с
// бревисом; дефис в конце строки убирается, строки склеиваются, пробелы схлопываются. «Целое слово» — свой
// фильтр: символы по краям совпадения внутри строки не должны быть буквами, цифрами или метками
// (TextPage::isWordChar).
//
// searchPage — одна страница (квады для подсветки). SearchSession — документ: страницы подаются по запросу
// MuPDF (FZ_SEARCH_MORE_INPUT), совпадение может переходить на следующую страницу; прогресс — не чаще раза в
// 100 мс и в конце, отмена — между страницами; найденное копится, счётчик «N из M» и переходы F3/Shift+F3 по
// кругу. Работает в потоке документа (TextPage из кеша сессии) или в любом потоке над готовыми TextPage; слои
// для поиска — SearchSession::textOptions() (без сегментации). Индексы firstChar/lastChar — того слоя, по
// которому шёл поиск; подсветка — по квадам.
#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "core/text/text_types.h"
#include "core/util/geometry.h"

namespace ot {

class TextPage;

struct SearchOptions {
    bool caseSensitive = false;
    bool wholeWord = false;
    bool regex = false;
    bool ignoreDiacritics = false;

    bool operator==(const SearchOptions&) const = default;
};

// Флаги fz_search_options для опций.
int searchFlags(const SearchOptions& options) noexcept;

struct SearchQuad {
    int page = 0;
    QuadF quad;  // пункты страницы page (y вниз)
};

struct SearchHit {
    int page = 0;                   // страница начала
    int firstChar = -1;             // первый символ — индекс TextPage::chars() страницы page
    int endPage = 0;                // страница конца (совпадение через границу страниц — следующая)
    int lastChar = -1;              // последний символ (включительно) на странице endPage
    std::vector<SearchQuad> quads;  // по кваду на строку (соседние символы строки слиты)
};

// Все совпадения на странице. Пустой образец — ничего; неверное регулярное выражение — MuError.
std::vector<SearchHit> searchPage(const TextPage& page, std::string_view needle,
                                  const SearchOptions& options = {});

struct SearchProgress {
    int pagesDone = 0;  // страниц подано в поиск
    int pageCount = 0;
    int hits = 0;
    bool finished = false;  // последний вызов: поиск закончен или отменён
};

class SearchSession {
public:
    // Текстовый слой страницы; nullptr — у страницы слоя нет (ошибка построения): страница пропускается.
    using PageSource = std::function<std::shared_ptr<const TextPage>(int page)>;
    using ProgressFn = std::function<void(const SearchProgress&)>;
    // true — отменить (проверяется после каждой страницы).
    using CancelFn = std::function<bool()>;

    static constexpr std::chrono::milliseconds kProgressInterval{100};

    SearchSession(std::string needle, SearchOptions options);

    // Опции слоёв для поиска: без SEGMENT и PARAGRAPH_BREAK (readingOrder = false) — в разы быстрее, а
    // совпадения те же; квады совпадений годятся для подсветки на любом слое страницы.
    static TextOptions textOptions() noexcept {
        TextOptions options;
        options.readingOrder = false;
        return options;
    }

    const std::string& needle() const noexcept { return needle_; }
    const SearchOptions& options() const noexcept { return options_; }

    // Искать по страницам [0, pageCount) по порядку; повторный вызов начинает заново. Отмена (cancel или
    // MuError Aborted от source) — aborted(), найденное остаётся. Неверное регулярное выражение и прочие
    // ошибки MuPDF — MuError; ошибка слоя одной страницы (кроме отмены) — страница пропускается.
    void run(int pageCount, const PageSource& source, const ProgressFn& progress = {},
             const CancelFn& cancel = {});

    const std::vector<SearchHit>& hits() const noexcept { return hits_; }
    int count() const noexcept { return static_cast<int>(hits_.size()); }
    bool finished() const noexcept { return finished_; }
    bool aborted() const noexcept { return aborted_; }
    int pagesDone() const noexcept { return pagesDone_; }
    // Совпадения, у которых есть квад на странице (подсветка всех совпадений оверлеем).
    std::vector<int> hitsOnPage(int page) const;

    // Текущее совпадение (F3 / Shift+F3): −1 — не выбрано или нет совпадений.
    int current() const noexcept { return current_; }
    const SearchHit* currentHit() const noexcept;
    int next() noexcept;      // следующее по кругу
    int previous() noexcept;  // предыдущее по кругу (из «не выбрано» — последнее)
    // Первое совпадение на странице page или после неё (по кругу) — начать с текущей страницы.
    int selectFrom(int page) noexcept;
    // Счётчик «3 из 17»: {номер текущего с 1 (0 — не выбрано), всего}.
    std::pair<int, int> counter() const noexcept { return {current_ + 1, count()}; }

private:
    std::string needle_;
    SearchOptions options_;
    std::vector<SearchHit> hits_;
    int current_ = -1;
    int pagesDone_ = 0;
    bool finished_ = false;
    bool aborted_ = false;
};

}  // namespace ot
