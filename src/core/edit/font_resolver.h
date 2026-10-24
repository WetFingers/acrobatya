// FontResolver — «каким шрифтом набрать символ U+xxxx в пробеге со шрифтом F» (план 3.2, п. 4; эталон 3.9
// «FontResolver», четыре правила; edit-03 — начертания, edit-49 — запасной шрифт):
//   1) F встроен и содержит глиф (код в кодировке F, непустой контур — FontProbe::encode) — F;
//   2) F встроен, глифа нет (подмножество) — тот же системный шрифт (findSystemFont по /BaseFont без
//   префикса,
//      семейство и стиль из дескриптора) новым CID-шрифтом — «подмножество расширено»;
//   3) системного шрифта нет (или в нём нет глифа) — запасной из настроек: семейство edit-49, «автоматически»
//   —
//      Segoe UI / Times New Roman по засечкам F (Courier New — моноширинный), символы — Segoe UI Symbol;
//      честное «шрифт подменён на …»; глифа нет нигде — «нет глифа» (покажется .notdef запасного);
//   4) F не встроен (base14 или имя системного шрифта) — метрически совместимый системный (Helvetica → Arial,
//      Times → Times New Roman, Courier → Courier New): латиница WinAnsi — простым TrueType, остальное — CID.
// Начертание по запросу (Ctrl+B / Ctrl+I) — грань того же семейства; грани нет — синтетика: жирный — Tr 2 с
// обводкой кегль · 0,03, курсив — наклон [1 0 0.2 1] к матрице текста (только когда начертания нет).
//
// Значения выбора — без MuPDF; сам резолвер живёт в потоке документа (FontProbe, FontLibrary).
#pragma once

#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "core/edit/font_embed.h"
#include "core/edit/system_fonts.h"
#include "core/util/geometry.h"

namespace ot::edit {

class FontProbe;
struct FontDetails;

enum class FontRule : std::uint8_t { Original = 1, Extend = 2, Fallback = 3, Replace = 4 };
const char* fontRuleCode(FontRule rule) noexcept;  // "original", "extend", "fallback", "replace"

// Запасные шрифты (edit-49: «Шрифт подстановки» в Установках → Редактирование).
struct FallbackFonts {
    std::string family;  // выбран человеком; пусто — «автоматически» (по засечкам шрифта пробега)
    std::string sans = "Segoe UI";
    std::string serif = "Times New Roman";
    std::string mono = "Courier New";
    std::string symbol = "Segoe UI Symbol";  // символы, которых нет в запасном
};

// Начертание, которое просят (панель FORMAT TEXT): 0 / −1 — как у шрифта пробега.
struct StyleRequest {
    int weight = 0;
    int italic = -1;
};

struct FontChoice {
    FontRule rule = FontRule::Original;
    bool newFont = false;  // набирать встроенным системным шрифтом face (FontEmbedder::add)
    EmbedKind embed = EmbedKind::Cid;
    SystemFace face;
    bool syntheticBold = false;
    bool syntheticItalic = false;
    bool missing = false;  // глифа нет ни в одном шрифте

    // Один и тот же шрифт набора (для склейки пробегов).
    bool sameFont(const FontChoice& o) const;
};

enum class FontNoticeKind : std::uint8_t {
    SubsetExtended,  // правило 2: подмножество дополнено системным шрифтом
    Substituted,     // правило 3 или 4 с другим семейством: «шрифт подменён на …»
    Synthesized,     // начертания нет — синтетический жирный/курсив
    MissingGlyph,    // глифа нет ни в одном шрифте
};
const char* fontNoticeCode(FontNoticeKind kind) noexcept;

// Сообщение для человека (ui переводит по коду): одно на вид, шрифт и замену, символы копятся.
struct FontNotice {
    FontNoticeKind kind = FontNoticeKind::Substituted;
    std::string font;         // шрифт пробега (/BaseFont без префикса)
    std::string replacement;  // семейство, которым набрано
    std::u32string chars;     // символы, из-за которых
};

struct ResolvedRun {
    FontChoice choice;
    std::u32string text;
};

class FontResolver {
public:
    FontResolver(FontLibrary& library, SystemFonts& system, FallbackFonts fallback = {});

    void setFallback(FallbackFonts fallback);
    const FallbackFonts& fallback() const noexcept { return fallback_; }

    // Шрифт для символа ch в пробеге со шрифтом run.
    FontChoice resolve(FontProbe& run, char32_t ch, const StyleRequest& style = {});
    // Текст по пробегам одного шрифта: пробел остаётся в текущем пробеге, если его шрифт знает пробел.
    std::vector<ResolvedRun> runs(FontProbe& run, std::u32string_view text, const StyleRequest& style = {});
    // Можно ли набрать ch выбором choice (у Original — шрифтом run).
    bool canEncode(const FontChoice& choice, FontProbe& run, char32_t ch);

    const std::vector<FontNotice>& notices() const noexcept { return notices_; }
    void clearNotices() { notices_.clear(); }

private:
    std::optional<SystemFace> find(const FontDetails& d, int weight, bool italic);
    std::optional<SystemFace> family(const std::string& family, int weight, bool italic);
    bool usable(const SystemFace& face, char32_t ch, EmbedKind kind);
    FontChoice fallbackFor(const FontDetails& d, char32_t ch, int weight, bool italic);
    void note(FontNoticeKind kind, const FontDetails& d, const std::string& replacement, char32_t ch);
    void synthetic(FontChoice& c, const FontDetails& d, char32_t ch);

    FontLibrary& lib_;
    SystemFonts& system_;
    FallbackFonts fallback_;
    std::map<std::string, std::optional<SystemFace>> cache_;  // запросы к SystemFonts (DirectWrite)
    std::vector<FontNotice> notices_;
};

// Синтетическое начертание пробега: жирный — Tr 2 (заливка и обводка) с w = кегль · 0,03 в пространстве
// пользователя, курсив — наклон skew к матрице текста.
struct SyntheticStyle {
    int render = 0;  // 0 — как есть; 2 — заливка и обводка
    float lineWidth = 0;
    float skew = 0;
    bool active() const noexcept { return render != 0 || skew != 0; }
};
SyntheticStyle syntheticStyle(const FontChoice& choice, float fontSize);
// Матрица текста с наклоном: [1 0 skew 1 0 0] × tm (x' = x + skew · y в пространстве текста).
Matrix syntheticTextMatrix(const Matrix& tm, const SyntheticStyle& style);
// Операторы состояния перед показом пробега («2 Tr 0.36 w»); ничего — пусто.
std::string syntheticOps(const SyntheticStyle& style);

}  // namespace ot::edit
