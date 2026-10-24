// Rewriter — запись абзаца в контент-поток (эталон 3.9 «Модель»: правка → Reflow → Rewriter пишет только
// изменённые текстовые объекты, остальной поток — байт в байт; план 3.3, п. 3; edit-01, edit-02 — ядро,
// edit-03).
//
//   1. Модель страницы заново; операторы абзаца (ParagraphSource) должны быть те же — иначе Stale.
//   2. Шрифты: нетронутые символы — их кодами в исходном шрифте; набранные и с другим стилем — FontResolver
//      (четыре правила эталона, семейство из панели), новые шрифты — FontEmbedder (ресурс OtFn).
//   3. Перевёрстка (core/edit/reflow.h) по ширинам шрифтов набора; наложение на соседние блоки — в отчёт.
//   4. Новый текст — на месте первого по потоку оператора абзаца, внутри его текстового объекта (так MCID и
//      порядок рисования те же): Tf, Tm каждой строки (масштаб 1 — кегль в Tf, пункты пространства
//      пользователя), Tc, Tw (выравнивание по ширине при однобайтовом пробеле) или сдвиги TJ, Tz, Ts, Tr,
//      цвет исходными операторами (цветовое пространство сохраняется; новый цвет — rg), синтетика (Tr 2 и w,
//      наклон Tm); строки — TJ с кодами шрифта (литеральные строки, \( \) \\ и восьмеричные < 32). Затем
//      состояние после исходного оператора возвращается теми же байтами операций, что его задали (StateRefs):
//      Tf, Tc, Tw, Tz, Ts, Tr, цвет, w; Tm/Tlm — если дальше в объекте от них что-то зависит.
//   5. Остальные операторы абзаца — «призраки»: Tj/TJ — ничего (или сдвиг пера, если следующий чужой спан на
//      той же строке без позиционирования), ' — T*, " — Tw Tc T*. Текстовый объект, в котором не осталось
//      своего текста, убирается целиком (BT, ET, позиционирование; Tf/Tc/цвет и пометки остаются — вне BT они
//      законны), кроме объекта с операцией, разрезанной границей потоков.
//   6. Подчёркивание — прямоугольники «re f» после ET объекта, в «/OttU BMC … EMC» (п. 4).
//   7. Запись — одна операция журнала (решение 1.3: журнал покрывает pdf_update_stream): изменённые потоки —
//      FlateDecode, pdf_update_stream; поток, общий с другой страницей, — копия только для этой (элемент
//      /Contents); абзац, у которого не осталось операторов (стёрт), пишется заново новым потоком в конце
//      /Contents («q» новым первым потоком — исходные потоки не трогаются). Undo возвращает потоки байт в
//      байт.
//
// Подписанный документ и документ с /Perms — отказ (эталон 3.9 «Граничные случаи»). Всё — в потоке документа.
#pragma once

#include <cstdint>
#include <memory>
#include <string>
#include <vector>

#include "core/edit/paragraph_editor.h"
#include "core/edit/reflow.h"
#include "core/util/geometry.h"

namespace ot {
class Document;
}

namespace ot::edit {

class FontLibrary;
class FontEmbedder;
class SystemFonts;
struct SubsetReport;

enum class RewriteError : std::uint8_t {
    None,
    Stale,     // модель страницы другая (документ изменился) — абзац построить заново
    ReadOnly,  // спан абзаца нельзя править
    Signed,    // документ подписан или с /Perms
    Failed,    // ошибка MuPDF при записи (операция откатана)
};
const char* rewriteErrorCode(RewriteError error) noexcept;  // "", "stale", "readOnly", "signed", "failed"

struct RewriteOptions {
    std::string operation = "Edit text";  // имя операции журнала (ui подписывает «Отменить: …»)
};

struct RewriteReport {
    RewriteError error = RewriteError::None;
    std::string message;  // английский журнал
    int streams = 0;      // записано потоков
    int lines = 0;
    int textObjectsRemoved = 0;
    bool appended = false;        // абзац записан новым потоком (у прежнего не осталось операторов)
    RectF bbox = RectF::none();   // абзац после записи (координаты страницы)
    RectF dirty = RectF::none();  // прежняя и новая рамки вместе — перерисовать
    std::vector<RectF> overlaps;  // блоки, на которые наехал абзац (координаты страницы)
    std::vector<FontNotice> notices;

    bool ok() const noexcept { return error == RewriteError::None; }
};

class Rewriter {
public:
    // library — одна на сеанс редактирования документа; system — системные шрифты (DirectWrite или
    // FakeSystemFonts), fallback — запасные из настроек (edit-49).
    Rewriter(Document& doc, FontLibrary& library, SystemFonts& system, const FallbackFonts& fallback);
    ~Rewriter();
    Rewriter(const Rewriter&) = delete;
    Rewriter& operator=(const Rewriter&) = delete;

    // Записать абзац editor (страница editor.source().page). Успех — editor указывает на новые операторы,
    // раскладка — новая, modified() сброшен. Ошибка — документ как был, editor тоже.
    RewriteReport rewrite(ParagraphEditor& editor, const RewriteOptions& options = {});
    // Раскладка без записи (курсор во время набора): ширины шрифтов набора, перевёрстка; editor получает её.
    // Шрифты не встраиваются (ширины новых — по файлам системных шрифтов).
    ReflowResult layout(ParagraphEditor& editor);

    // Встроенные редактором шрифты — подмножества перед полной записью (SaveOptions::prepare).
    FontEmbedder& embedder() noexcept;
    SubsetReport subsetFonts();
    void setFallback(const FallbackFonts& fallback);

private:
    struct Impl;
    std::unique_ptr<Impl> d_;
};

}  // namespace ot::edit
