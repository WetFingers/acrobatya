// unit.paths.* — UTF-8 ↔ UTF-16, запись и чтение файла по кириллическому пути, короткий путь.
#include <gtest/gtest.h>

#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace paths = ot::paths;

TEST(paths, utf8_roundtrip) {
    const std::string utf8 = "Оттиск · ПДФ — «проба» ü";
    EXPECT_EQ(paths::narrow(paths::widen(utf8)), utf8);
    EXPECT_EQ(paths::widen("abc"), L"abc");
    EXPECT_TRUE(paths::widen("").empty());
}

TEST(paths, file_io_with_cyrillic_name) {
    ot::test::TempDir dir;
    const auto file = dir.path() / L"подкаталог" / L"файл.bin";
    const std::string data("\x00\x01\xffданные", 9);
    paths::writeFile(file, data);
    EXPECT_EQ(paths::readFile(file), data);
    const std::wstring shortPath = paths::toShortPath(file.native());
    EXPECT_FALSE(shortPath.empty());
}
