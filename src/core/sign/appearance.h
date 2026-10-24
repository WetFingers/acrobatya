// Внешний вид подписи (эталон 3.8 «Диалог подписания» — внешний вид: имя, дата, причина, логотип PNG,
// рукописная картинка из Fill & Sign, «только изображение», язык; план 4.2, п. 3; sign-13). Готовые режимы
// MuPDF (флаги PDF_SIGNATURE_SHOW_*: ярлыки, DN, дата, имя текстом, имя графикой) — fromFlags/flags, их
// логотип MuPDF не рисуется (свой PNG — logo). Конструктор: графика слева — нет / имя крупно / картинка
// PNG/JPEG с альфой по пути (в 4.6/5.3 сюда подставляется сохранённая подпись Fill & Sign
// signatures\signature.png); текст справа — имя, DN, причина, место, дата «2026.10.27 11:00:00 +03'00'» (как
// в Acrobat), с ярлыками или без; ярлыки по-русски даёт интерфейс (tr), здесь — английские Acrobat EN;
// логотип — PNG фоном по центру, полупрозрачно; «только изображение» — картинка на всю рамку. Без графики и
// текста — пусто.
//
// Шрифт — Segoe UI (%WINDIR%\Fonts\segoeui.ttf; нет — Helvetica MuPDF с запасными шрифтами); раскладка своя:
// наибольший кегль, при котором слова не рвутся (pdf_layout_fit_text рвёт имя посреди слова), строки влево,
// блок по центру по высоте; глифы ложатся в display list контурами
// (fz_outline_glyph): pdf-device MuPDF встроил бы Segoe UI целиком (~1 МБ и полная /ToUnicode на каждую
// подпись), а подмножество в инкременте подписи задело бы чужие шрифты (pdf_subset_fonts). Display list — в
// координатах страницы MuPDF (y вниз) по рамке виджета; pdf_sign_signature_with_appearance делает из него
// /AP. Невидимая подпись — пустая рамка, вида нет (signer.h). Предпросмотр — тот же display list на белом.
#pragma once

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

#include "core/render/bitmap.h"
#include "core/util/geometry.h"

struct fz_context;
struct fz_display_list;

namespace ot::sign {

enum class AppearanceGraphic {
    None,   // только текст на всю рамку
    Name,   // имя крупно слева
    Image,  // картинка слева (image)
};

struct AppearanceText {
    bool name = true;
    bool dn = false;
    bool reason = true;
    bool location = true;
    bool date = true;
    bool labels = true;
};

// Ярлыки строк (UTF-8); интерфейс подставляет переводы.
struct AppearanceLabels {
    std::string signedBy = "Digitally signed by";
    std::string dn = "DN:";
    std::string reason = "Reason:";
    std::string location = "Location:";
    std::string date = "Date:";
};

struct SignatureAppearance {
    AppearanceGraphic graphic = AppearanceGraphic::Name;
    std::filesystem::path image;  // PNG/JPEG для Image и «только изображение»
    bool imageOnly = false;
    AppearanceText text;
    std::filesystem::path logo;   // PNG фоном, пусто — без логотипа
    std::string language = "ru";  // язык текста (BCP 47) — переносы и запасные шрифты MuPDF
    AppearanceLabels labels;

    // Готовые режимы MuPDF: LABELS, DN, DATE, TEXT_NAME, GRAPHIC_NAME (LOGO не переносится).
    static SignatureAppearance fromFlags(int flags);
    int flags() const;
};

// Что показывать.
struct AppearanceData {
    std::string name;  // имя подписанта (CN или /Name)
    std::string dn;    // RFC 2253
    std::string reason;
    std::string location;
    std::int64_t time = 0;  // секунды Unix; 0 — без даты
};

// «2026.10.27 11:00:00 +03'00'» — местное время.
std::string formatSignatureDate(std::int64_t time);
// Строки текста (пустые значения пропускаются; «только изображение» — без текста).
std::vector<std::string> appearanceLines(const SignatureAppearance& look, const AppearanceData& data);

// Новый display list внешнего вида по рамке rect (координаты страницы MuPDF; освобождает вызывающий —
// fz_drop_display_list). Картинка или логотип не читаются — std::runtime_error; MuPDF — MuError.
fz_display_list* buildAppearance(fz_context* ctx, const RectF& rect, const SignatureAppearance& look,
                                 const AppearanceData& data);
// Предпросмотр для диалога: RGB-растр рамки widthPt × heightPt при dpi на белом.
Bitmap previewAppearance(const SignatureAppearance& look, const AppearanceData& data, float widthPt,
                         float heightPt, float dpi = 96.0f);

}  // namespace ot::sign
