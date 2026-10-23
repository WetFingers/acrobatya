// RtfWriter — минимальный писатель RTF из текстового слоя (эталон 3.12, план 1.4; create-34): текстовый блок
// — абзац (\pard … \par, строки блока — через пробел), шрифт (\fN из таблицы \fonttbl, семейство по имени
// шрифта PDF), кегль (\fsN — полупункты), жирный и курсив (\b, \i — по имени и флагам шрифта), страницы —
// \page. Всё вне ASCII — \uN? (N — знаковое 16-битное, вне BMP — суррогатная пара; \uc1 — один запасной «?»):
// файл чистый ASCII, кириллица не зависит от кодовой страницы читателя. Таблица шрифтов — \fcharset204
// (кириллица) для Word.
#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace ot {

class TextPage;

// Текст RTF: \ { } экранируются, табуляция — \tab, всё вне ASCII — \uN?, управляющие символы пропускаются.
std::string rtfText(std::u32string_view text);
// Семейство для \fonttbl по имени шрифта PDF: без префикса подмножества, стиля после «-» или «,», хвостов
// MT/PS/PSMT; CamelCase — словами («TimesNewRomanPS-BoldMT» → «Times New Roman»); пусто — «Arial».
std::string rtfFontFamily(std::string_view pdfFontName);

class RtfWriter {
public:
    void addPage(const TextPage& page);
    // Весь документ: {\rtf1\ansi\ansicpg1251\deff0\uc1{\fonttbl…}… }.
    std::string finish() const;

private:
    int fontIndex(const std::string& family);

    std::vector<std::string> fonts_;
    std::string body_;
    int pages_ = 0;
};

}  // namespace ot
