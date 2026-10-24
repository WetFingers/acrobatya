// unit.system_fonts.* — системные шрифты для редактора (план 3.2, п. 2; эталон 3.9 «FontResolver», edit-49 —
// поиск шрифта): FakeSystemFonts отдаёт файлы C:\Windows\Fonts по семейству, весу и курсиву (грани нет —
// синтетика, как у DirectWrite) и по PostScript-имени; findSystemFont ищет тот же шрифт по имени из PDF
// («ABCDEF+Arial-BoldMT», «Arial,BoldItalic»), затем по семейству, затем метрическую замену base14 (Helvetica
// → Arial, Times → Times New Roman, Courier → Courier New); семейства, которого нет, — нет. Живой DirectWrite
// — ui.dwrite.
#include <gtest/gtest.h>

#include <algorithm>
#include <filesystem>
#include <optional>
#include <string>

#include "core/edit/system_fonts.h"
#include "core/util/features.h"

namespace fs = std::filesystem;
using ot::edit::FakeSystemFonts;
using ot::edit::FontQuery;
using ot::edit::SystemFace;

namespace {

std::string fileName(const std::optional<SystemFace>& f) {
    return f ? ot::edit::lowerAscii(f->file.filename().string()) : std::string("<нет>");
}

FontQuery query(const char* name, int weight = 400, bool italic = false) {
    FontQuery q;
    q.name = name;
    q.weight = weight;
    q.italic = italic;
    return q;
}

}  // namespace

TEST(system_fonts, fake_faces) {
    OT_FEATURE("edit-49");
    FakeSystemFonts fonts;
    if (!fonts.face("Arial", 400, false)) {
        GTEST_SKIP() << "нет C:\\Windows\\Fonts\\arial.ttf";
    }
    EXPECT_EQ(fileName(fonts.face("Arial", 400, false)), "arial.ttf");
    EXPECT_EQ(fileName(fonts.face("arial", 700, false)), "arialbd.ttf");
    EXPECT_EQ(fileName(fonts.face("Arial", 700, true)), "arialbi.ttf");
    EXPECT_EQ(fileName(fonts.face("Times New Roman", 400, true)), "timesi.ttf");
    EXPECT_EQ(fileName(fonts.face("Courier New", 400, false)), "cour.ttf");
    EXPECT_EQ(fileName(fonts.face("Segoe UI", 400, false)), "segoeui.ttf");
    EXPECT_EQ(fileName(fonts.face("Segoe UI", 300, false)), "segoeuil.ttf");

    const auto arial = fonts.face("Arial", 700, false);
    ASSERT_TRUE(arial);
    EXPECT_EQ(arial->family, "Arial");
    EXPECT_EQ(arial->postscript, "Arial-BoldMT");
    EXPECT_EQ(arial->weight, 700);
    EXPECT_FALSE(arial->italic);
    EXPECT_FALSE(arial->simulatedBold);
    EXPECT_TRUE(fs::exists(arial->file));

    // Грани нет — ближайшая и синтетика (как DWRITE_FONT_SIMULATIONS).
    const auto tahomaItalic = fonts.face("Tahoma", 400, true);
    ASSERT_TRUE(tahomaItalic);
    EXPECT_EQ(fileName(tahomaItalic), "tahoma.ttf");
    EXPECT_FALSE(tahomaItalic->italic);
    EXPECT_TRUE(tahomaItalic->simulatedItalic);
    EXPECT_FALSE(tahomaItalic->simulatedBold);
    const auto symbolBold = fonts.face("Segoe UI Symbol", 700, false);
    ASSERT_TRUE(symbolBold);
    EXPECT_EQ(fileName(symbolBold), "seguisym.ttf");
    EXPECT_TRUE(symbolBold->simulatedBold);
    const auto cambria = fonts.face("Cambria", 400, false);
    ASSERT_TRUE(cambria);
    EXPECT_EQ(fileName(cambria), "cambria.ttc");
    EXPECT_EQ(cambria->index, 0);

    EXPECT_FALSE(fonts.face("Fantasy Sans", 400, false));
    EXPECT_FALSE(fonts.face("", 400, false));
}

TEST(system_fonts, fake_by_name_and_families) {
    FakeSystemFonts fonts;
    if (!fonts.face("Arial", 400, false)) {
        GTEST_SKIP() << "нет C:\\Windows\\Fonts\\arial.ttf";
    }
    EXPECT_EQ(fileName(fonts.byName("ArialMT")), "arial.ttf");
    EXPECT_EQ(fileName(fonts.byName("Arial-BoldMT")), "arialbd.ttf");
    EXPECT_EQ(fileName(fonts.byName("TimesNewRomanPS-ItalicMT")), "timesi.ttf");
    EXPECT_EQ(fileName(fonts.byName("Arial Bold")), "arialbd.ttf");  // полное имя
    EXPECT_FALSE(fonts.byName("Helvetica"));

    const auto families = fonts.families();
    EXPECT_TRUE(std::is_sorted(families.begin(), families.end()));
    EXPECT_NE(std::find(families.begin(), families.end(), "Arial"), families.end());
    EXPECT_NE(std::find(families.begin(), families.end(), "Segoe UI Symbol"), families.end());

    fonts.remove("Arial");
    EXPECT_FALSE(fonts.face("Arial", 400, false));
    EXPECT_FALSE(fonts.byName("ArialMT"));
    EXPECT_TRUE(fonts.face("Times New Roman", 400, false));
}

TEST(system_fonts, find_for_pdf_names) {
    OT_FEATURE("edit-49");
    FakeSystemFonts fonts;
    if (!fonts.face("Arial", 400, false)) {
        GTEST_SKIP() << "нет C:\\Windows\\Fonts\\arial.ttf";
    }
    // Тот же шрифт: PostScript-имя подмножества, имя Word, CamelCase без пробелов.
    EXPECT_EQ(fileName(findSystemFont(fonts, query("ABCDEF+Arial-BoldMT", 700))), "arialbd.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("Arial,BoldItalic", 700, true))), "arialbi.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("TimesNewRomanPSMT"))), "times.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("CourierNewPS-BoldMT", 700))), "courbd.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("SegoeUI-Bold", 700))), "segoeuib.ttf");
    // Имя говорит одно, а нужен другой стиль (Ctrl+B): грань семейства, а не грань по имени.
    EXPECT_EQ(fileName(findSystemFont(fonts, query("ArialMT", 700))), "arialbd.ttf");
    // base14 и метрически совместимые замены (правило 4).
    EXPECT_EQ(fileName(findSystemFont(fonts, query("Helvetica"))), "arial.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("Helvetica-BoldOblique", 700, true))), "arialbi.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("Times-Roman"))), "times.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("Times-Bold", 700))), "timesbd.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("Courier"))), "cour.ttf");
    EXPECT_EQ(fileName(findSystemFont(fonts, query("LiberationSans"))), "arial.ttf");
    // Без замены — только тот же шрифт (правило 2: «найти тот же системный шрифт»).
    FontQuery same = query("Helvetica");
    same.metricSubstitute = false;
    EXPECT_FALSE(findSystemFont(fonts, same));
    // Отсутствующее семейство.
    EXPECT_FALSE(findSystemFont(fonts, query("ABCDEF+FantasySans")));
    EXPECT_FALSE(findSystemFont(fonts, query("FantasySerif-Bold", 700)));
    // Семейство задано явно (FontDetails::family).
    FontQuery byFamily = query("");
    byFamily.family = "Georgia";
    byFamily.italic = true;
    EXPECT_EQ(fileName(findSystemFont(fonts, byFamily)), "georgiai.ttf");
}

TEST(system_fonts, metric_substitutes) {
    EXPECT_EQ(ot::edit::metricSubstitute("Helvetica"), "Arial");
    EXPECT_EQ(ot::edit::metricSubstitute("helvetica"), "Arial");
    EXPECT_EQ(ot::edit::metricSubstitute("Helvetica Neue"), "Arial");
    EXPECT_EQ(ot::edit::metricSubstitute("Times"), "Times New Roman");
    EXPECT_EQ(ot::edit::metricSubstitute("Times Roman"), "Times New Roman");
    EXPECT_EQ(ot::edit::metricSubstitute("Courier"), "Courier New");
    EXPECT_EQ(ot::edit::metricSubstitute("Symbol"), "Symbol");
    EXPECT_EQ(ot::edit::metricSubstitute("ZapfDingbats"), "Segoe UI Symbol");
    EXPECT_EQ(ot::edit::metricSubstitute("Liberation Serif"), "Times New Roman");
    EXPECT_EQ(ot::edit::metricSubstitute("Nimbus Mono"), "Courier New");
    EXPECT_EQ(ot::edit::metricSubstitute("Arimo"), "Arial");
    EXPECT_EQ(ot::edit::metricSubstitute("Georgia"), "");
    EXPECT_EQ(ot::edit::metricSubstitute(""), "");
}

TEST(system_fonts, pick_face) {
    std::vector<SystemFace> faces(4);
    faces[0].weight = 300;
    faces[1].weight = 400;
    faces[2].weight = 700;
    faces[3].weight = 400;
    faces[3].italic = true;
    for (size_t i = 0; i < faces.size(); ++i) {
        faces[i].file = "f" + std::to_string(i);
    }
    EXPECT_EQ(ot::edit::pickFace(faces, 400, false).file, "f1");
    EXPECT_EQ(ot::edit::pickFace(faces, 600, false).file, "f2");
    EXPECT_EQ(ot::edit::pickFace(faces, 350, false).file, "f0");  // 350 и меньше — сначала легче
    EXPECT_EQ(ot::edit::pickFace(faces, 400, true).file, "f3");
    const SystemFace boldItalic = ot::edit::pickFace(faces, 700, true);
    EXPECT_EQ(boldItalic.file, "f3");  // курсив важнее веса
    EXPECT_TRUE(boldItalic.simulatedBold);
    EXPECT_FALSE(boldItalic.simulatedItalic);
}
