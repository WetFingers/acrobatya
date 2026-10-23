// TextPage — текстовый слой страницы (эталон 3.2, план 1.4): обёртка fz_stext_page. Блоки, строки и символы с
// рамками и стилем копируются в значения (core/text/text_types.h) в порядке чтения: блоки идут в порядке,
// который дал SEGMENT (страницы с деревом структуры MuPDF не сегментирует), struct-блоки обходятся в глубину.
// Векторные блоки (collectVectors) — в vectors(), узлы структуры (collectStructure) — в structure().
// Поиск, выделение и экспорт (core/text/search.h, selection.h, create/text_export.h) работают по самому
// fz_stext_page — stext() — контекстом его семьи (contextForThisThread).
//
// Строится в потоке документа (TextPage::build) контекстом документа; готовый TextPage неизменяем: значения
// читаются из любого потока, fz_stext_page читается контекстом семьи документа в любом потоке. Владеет одной
// ссылкой на fz_stext_page и клоном контекста — им ссылка отдаётся там, где умирает последний shared_ptr.
// fz_stext_page бывает и многостраничным (fz_stext_page_details): страница блока — TextBlock::sourcePage.
//
// TextCache — LRU текстовых слоёв документа (16 страниц; эталон 3.2), ключ — {страница, опции}; правка
// страницы сбрасывает её слои. Живёт в DocumentSession, строится и читается только в потоке документа.
#pragma once

#include <cstddef>
#include <list>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "core/text/text_types.h"
#include "core/util/geometry.h"

extern "C" {
#include <mupdf/fitz.h>
}

namespace ot {

class Document;
class MuContext;

class TextPage {
public:
    using Options = TextOptions;

    // Флаги fz_stext_options для опций (база + флажки).
    static int flagsFor(const Options& options) noexcept;

    // Извлечь текст страницы page (содержимое страницы без пометок — как fz_new_stext_page_from_page) в
    // контексте документа (поток документа). cookie: abort = 1 прерывает (MuError, ErrorKind::Aborted).
    static std::shared_ptr<const TextPage> build(const Document& doc, int page, const Options& options = {},
                                                 fz_cookie* cookie = nullptr);

    ~TextPage();
    TextPage(const TextPage&) = delete;
    TextPage& operator=(const TextPage&) = delete;

    int page() const noexcept { return page_; }
    const Options& options() const noexcept { return options_; }
    const RectF& bounds() const noexcept { return bounds_; }  // рамка страницы (fz_bound_page)

    const std::vector<TextBlock>& blocks() const noexcept { return blocks_; }
    const std::vector<TextLine>& lines() const noexcept { return lines_; }
    const std::vector<TextChar>& chars() const noexcept { return chars_; }
    const std::vector<TextFont>& fonts() const noexcept { return fonts_; }
    const std::vector<TextVector>& vectors() const noexcept { return vectors_; }
    const std::vector<TextStructNode>& structure() const noexcept { return structure_; }
    bool hasText() const noexcept { return !chars_.empty(); }

    // Весь текст в UTF-8: строки — «\n», текстовые блоки разделены пустой строкой.
    std::string text() const;
    // Символы [first, last] (по индексам chars(); last включительно) в UTF-8; конец строки — «\n».
    std::string text(int first, int last) const;
    std::string lineText(int line) const;
    // Слова по пробельным символам внутри строк (знаки препинания — часть слова, как у извлекателей текста).
    std::vector<TextWord> words() const;

    // Символ под точкой (в его четырёхугольнике); нет — ближайший в пределах tolerance пт; −1 — ничего.
    int charIndexAt(PointF p, float tolerance = 0) const;
    // Строка под точкой (рамка строки, расширенная на tolerance); −1 — нет.
    int lineAt(PointF p, float tolerance = 0) const;
    // Слово из букв, цифр и меток под точкой (двойной щелчок): без знаков препинания и пробелов; нет —
    // nullopt.
    std::optional<TextWord> wordAt(PointF p, float tolerance = 0) const;
    // Границы слова (буквы, цифры, метки) вокруг символа index: [first, last]; не буква — {index, index}.
    std::pair<int, int> wordBounds(int index) const;

    // Буква, цифра, метка или соединитель (_) — часть слова для wordAt и поиска «целое слово».
    static bool isWordChar(char32_t c) noexcept;

    // fz_stext_page и его семья контекстов: читать только контекстом той же семьи.
    fz_stext_page* stext() const noexcept { return stext_; }
    const void* family() const noexcept { return family_; }
    // Контекст этой семьи для текущего потока: MuContext::forThisThread(), если семья совпадает, иначе
    // временный клон владельца — он кладётся в temporary и живёт, пока жив temporary.
    fz_context* contextForThisThread(std::unique_ptr<MuContext>& temporary) const;
    // Индекс символа chars() по указателю fz_stext_char этого слоя (результаты поиска и выделения MuPDF);
    // −1 — не из этого слоя.
    int charIndexOf(const fz_stext_char* ch) const;

private:
    TextPage(std::unique_ptr<MuContext> owner, fz_stext_page* stext, int page, const Options& options,
             const RectF& bounds);
    void collect(fz_context* ctx);
    void collectBlocks(fz_context* ctx, fz_stext_block* first, int structNode);
    int fontIndex(fz_context* ctx, fz_font* font);
    void indexChars(fz_stext_block* first, int& index) const;

    std::unique_ptr<MuContext> owner_;
    const void* family_;
    fz_stext_page* stext_;
    int page_;
    Options options_;
    RectF bounds_;

    std::vector<TextBlock> blocks_;
    std::vector<TextLine> lines_;
    std::vector<TextChar> chars_;
    std::vector<TextFont> fonts_;
    std::vector<TextVector> vectors_;
    std::vector<TextStructNode> structure_;
    mutable std::once_flag charIndexOnce_;
    mutable std::unordered_map<const fz_stext_char*, int> charIndex_;  // для charIndexOf, строится лениво
    std::unordered_map<const fz_font*, int> fontIndex_;
};

class TextCache {
public:
    static constexpr std::size_t kDefaultCapacity = 16;

    explicit TextCache(std::size_t capacity = kDefaultCapacity) : capacity_(capacity == 0 ? 1 : capacity) {}

    // Слой страницы: из кеша (становится самым свежим) или построенный (вытесняет самый старый). Прерванное
    // построение не кешируется (MuError Aborted).
    std::shared_ptr<const TextPage> get(const Document& doc, int page, const TextOptions& options = {},
                                        fz_cookie* cookie = nullptr);
    // Без построения и без смены порядка; nullptr — нет в кеше.
    std::shared_ptr<const TextPage> find(int page, const TextOptions& options = {}) const;

    // Правка страницы: сбросить её слои со всеми опциями.
    void invalidate(int page);
    void clear();

    std::size_t size() const noexcept { return order_.size(); }
    std::size_t capacity() const noexcept { return capacity_; }
    void setCapacity(std::size_t capacity);
    std::size_t hits() const noexcept { return hits_; }
    std::size_t misses() const noexcept { return misses_; }

private:
    using Key = std::pair<int, int>;  // {страница, флаги fz_stext_options}
    using Entry = std::pair<Key, std::shared_ptr<const TextPage>>;

    void trim();

    std::size_t capacity_;
    std::list<Entry> order_;  // спереди — самые свежие
    std::map<Key, std::list<Entry>::iterator> index_;
    std::size_t hits_ = 0;
    std::size_t misses_ = 0;
};

}  // namespace ot
