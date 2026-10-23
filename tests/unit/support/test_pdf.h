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
