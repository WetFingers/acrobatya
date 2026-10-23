// Вспомогательное для unit-тестов: PDF в памяти через MuPDF (страницы с чёрным квадратом),
// временный каталог с кириллицей в имени, путь к исходникам.
#pragma once

#include <filesystem>
#include <string>

#include "core/mu/context.h"

namespace ot::test {

// PDF из pages страниц A4; на каждой — чёрный квадрат 100×100 pt в точке (100, 100) от левого
// верхнего угла и (на второй и далее) номер страницы текстом Helvetica.
std::string makePdf(MuContext& mu, int pages = 1);

// «Тяжёлая» страница A4: curves длинных кривых Безье через всю страницу (толщина 3 pt, детерминированно).
// Каждая кривая — отдельный узел display list: рендер на 600 dpi идёт секунды, отмена через fz_cookie
// проверяется между узлами.
std::string makeHeavyPdf(MuContext& mu, int curves = 4000);

// Смешанная страница A4: текст Helvetica и Times, прямоугольники; Curves — ещё кривая и треугольник с
// заливкой и обводкой; Full — ещё картинка 100 × 80 px, растянутая на 300 × 240 pt, и картинка 1200 × 900 px,
// сжатая в 200 × 150 pt (градиенты с полосами, детерминированно).
enum class MixedContent { TextAndRects, Curves, Full };
std::string makeMixedPdf(MuContext& mu, MixedContent content = MixedContent::Full);

// pages страниц A4, на каждой своя картинка w × h px (градиент с полосами, узор зависит от номера страницы),
// растянутая на всю страницу, и номер страницы текстом — «файл с картинками» для проверок памяти.
std::string makeImagePagesPdf(MuContext& mu, int pages = 200, int w = 128, int h = 96);

// Уникальный временный каталог с кириллицей в имени (удаляется деструктором).
class TempDir {
public:
    TempDir();
    ~TempDir();
    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;
    const std::filesystem::path& path() const { return path_; }

private:
    std::filesystem::path path_;
};

std::filesystem::path sourceDir();  // OTTISK_SOURCE_DIR

}  // namespace ot::test
