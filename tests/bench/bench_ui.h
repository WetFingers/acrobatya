// bench ui-paging — листание в виде (план 2.2b, эталон 5.8): tests/bench/bench_ui.cpp.
#pragma once

#include <filesystem>

namespace ot::bench {

// PageView offscreen 1440 × 900 при 100 %: открытие → первый полный кадр, листание страниц 1…pages с пустым
// кешем (QBENCHMARK) и второй раз из кеша. JSON — как у bench (tools/bench_compare.py). Код выхода: 0 — замер
// записан (числа не проверяются — цели 5.8 проверяют рубежи на Release), 1 — ошибка.
int runUiPaging(const std::filesystem::path& file, int runs, int pages, const std::filesystem::path& jsonOut);

}  // namespace ot::bench
