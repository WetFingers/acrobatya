// unit.page_set.* — какие страницы печатать (план 2.6; эталон 3.3; view-37): диапазон «1, 6-8, iii» с
// логическими номерами (сначала подпись, затем физический номер, «(N)» — физический), «12-» и «-3»,
// убывающий «5-3», подписи с дефисом «A-1», ошибки; «Текущая», «Выделенные», чётные/нечётные, обратный
// порядок; обратное преобразование выделенных миниатюр в строку «1-3, 7».
#include <gtest/gtest.h>

#include <string>
#include <vector>

#include "core/print/page_set.h"
#include "core/util/features.h"

using ot::print::formatPageRange;
using ot::print::PageParity;
using ot::print::PageRange;
using ot::print::PageSetOptions;
using ot::print::parsePageRange;
using ot::print::resolvePages;

namespace {

using Pages = std::vector<int>;

// Подписи i–iv, затем 1–8 (12 страниц).
std::vector<std::string> romanThenDecimal() {
    return {"i", "ii", "iii", "iv", "1", "2", "3", "4", "5", "6", "7", "8"};
}

}  // namespace

TEST(page_set, range_with_labels) {
    OT_FEATURE("view-37");
    const auto labels = romanThenDecimal();
    // «1» — логическая 1 (пятая), «6-8» — логические 6…8, «iii» — третья.
    EXPECT_EQ(parsePageRange(labels, "1, 6-8, iii", 12), (Pages{4, 9, 10, 11, 2}));
    // Без логических номеров — физические; «iii» тогда не страница.
    EXPECT_EQ(parsePageRange(labels, "1, 6-8", 12, false), (Pages{0, 5, 6, 7}));
    EXPECT_EQ(parsePageRange(labels, "1, iii", 12, false), std::nullopt);
    // «(3)» — всегда физическая.
    EXPECT_EQ(parsePageRange(labels, "(3)", 12), (Pages{2}));
    // Регистр римских не важен, пробелы и «;» — тоже.
    EXPECT_EQ(parsePageRange(labels, " II ; iv ", 12), (Pages{1, 3}));
}

TEST(page_set, open_ends_descending_duplicates) {
    const std::vector<std::string> none;
    EXPECT_EQ(parsePageRange(none, "1-3, 7, 10-", 12), (Pages{0, 1, 2, 6, 9, 10, 11}));
    EXPECT_EQ(parsePageRange(none, "-3", 12), (Pages{0, 1, 2}));
    EXPECT_EQ(parsePageRange(none, "5-3", 12), (Pages{4, 3, 2}));
    EXPECT_EQ(parsePageRange(none, "3, 1, 1", 12), (Pages{2, 0, 0}));
    EXPECT_EQ(parsePageRange(none, "2,", 12), (Pages{1}));
    EXPECT_EQ(parsePageRange(none, "", 12), std::nullopt);
    EXPECT_EQ(parsePageRange(none, " , ", 12), std::nullopt);
    EXPECT_EQ(parsePageRange(none, "13", 12), std::nullopt);
    EXPECT_EQ(parsePageRange(none, "0", 12), std::nullopt);
    EXPECT_EQ(parsePageRange(none, "1-x", 12), std::nullopt);
    EXPECT_EQ(parsePageRange(none, "abc", 12), std::nullopt);
    EXPECT_EQ(parsePageRange(none, "1--3", 12), std::nullopt);
}

TEST(page_set, labels_with_hyphen) {
    const std::vector<std::string> labels = {"A-1", "A-2", "A-3", "B-1"};
    EXPECT_EQ(parsePageRange(labels, "A-2", 4), (Pages{1}));
    EXPECT_EQ(parsePageRange(labels, "A-1-A-3", 4), (Pages{0, 1, 2}));
    EXPECT_EQ(parsePageRange(labels, "A-3-", 4), (Pages{2, 3}));
    EXPECT_EQ(parsePageRange(labels, "2-3", 4), (Pages{1, 2}));
}

TEST(page_set, resolve_kinds_parity_reverse) {
    OT_FEATURE("view-37");
    PageSetOptions o;
    EXPECT_EQ(resolvePages(o, 5), (Pages{0, 1, 2, 3, 4}));
    o.parity = PageParity::Odd;
    EXPECT_EQ(resolvePages(o, 5), (Pages{0, 2, 4}));
    o.parity = PageParity::Even;
    o.reverse = true;
    EXPECT_EQ(resolvePages(o, 5), (Pages{3, 1}));

    o = {};
    o.range = PageRange::Current;
    o.current = 3;
    EXPECT_EQ(resolvePages(o, 5), (Pages{3}));
    o.current = 9;
    EXPECT_EQ(resolvePages(o, 5), (Pages{}));

    o = {};
    o.range = PageRange::Selected;
    o.selected = {4, 1, 2, 7};
    EXPECT_EQ(resolvePages(o, 5), (Pages{4, 1, 2}));

    o = {};
    o.range = PageRange::Custom;
    o.custom = "1, 6-8, iii";
    EXPECT_EQ(resolvePages(o, 12, romanThenDecimal()), (Pages{4, 9, 10, 11, 2}));
    o.parity = PageParity::Even;  // физические чётные: 10-я и 12-я
    EXPECT_EQ(resolvePages(o, 12, romanThenDecimal()), (Pages{9, 11}));
    o.custom = "1-";
    o.parity = PageParity::Both;
    o.useLabels = false;
    o.reverse = true;
    EXPECT_EQ(resolvePages(o, 3), (Pages{2, 1, 0}));
    o.custom = "x";
    EXPECT_EQ(resolvePages(o, 3), std::nullopt);
    // Одна страница и «Чётные» — печатать нечего.
    o = {};
    o.parity = PageParity::Even;
    EXPECT_EQ(resolvePages(o, 1), (Pages{}));
}

TEST(page_set, format_selected_thumbnails) {
    OT_FEATURE("view-10");
    EXPECT_EQ(formatPageRange({0, 1, 2, 6}), "1-3, 7");
    EXPECT_EQ(formatPageRange({4}), "5");
    EXPECT_EQ(formatPageRange({}), "");
    EXPECT_EQ(formatPageRange({3, 4, 9, 10, 11}, romanThenDecimal(), true), "iv-1, 6-8");
    // Строка снова разбирается в те же страницы.
    const auto labels = romanThenDecimal();
    EXPECT_EQ(parsePageRange(labels, formatPageRange({3, 4, 9, 10, 11}, labels, true), 12),
              (Pages{3, 4, 9, 10, 11}));
}
