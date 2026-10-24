// unit.font_resolver.* — каким шрифтом набрать символ U+xxxx в пробеге со шрифтом F (план 3.2, п. 4;
// эталон 3.9 «FontResolver», edit-03 — начертания, edit-49 — запасной шрифт): (1) F встроен и знает глиф — F;
// (2) встроенное подмножество без глифа — тот же системный шрифт новым CID-шрифтом («ё» в латинском
// подмножестве Arial → Arial из FakeSystemFonts, «подмножество расширено»); (3) системного нет — запасной из
// настроек (Segoe UI / Times New Roman по засечкам, Segoe UI Symbol для символов) с честным «шрифт подменён»;
// глифа нет нигде — «нет глифа»; (4) F не встроен — метрически совместимый системный (Helvetica → Arial;
// латиница — простой TrueType WinAnsi, остальное — CID). Начертание по запросу (Ctrl+B) — грань семейства;
// грани нет — синтетика (Tr 2, обводка кегль · 0,03; наклон [1 0 0.2 1]). Пробеги «Привет, world! — 2026 €» в
// латинском подмножестве — три, два шрифта.
#include <gtest/gtest.h>

#include <filesystem>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/edit/font_embed.h"
#include "core/edit/font_info.h"
#include "core/edit/font_resolver.h"
#include "core/edit/system_fonts.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::edit::EmbedKind;
using ot::edit::FakeSystemFonts;
using ot::edit::FontChoice;
using ot::edit::FontLibrary;
using ot::edit::FontNoticeKind;
using ot::edit::FontProbe;
using ot::edit::FontResolver;
using ot::edit::FontRule;
using ot::edit::StyleRequest;

namespace {

fs::path edit(const std::string& name) {
    return ot::test::sourceDir() / "tests" / "fixtures" / "edit" / name;
}

std::string file(const FontChoice& c) { return ot::edit::lowerAscii(c.face.file.filename().string()); }

struct Env {
    std::unique_ptr<Document> doc;
    FakeSystemFonts system;
    std::unique_ptr<FontLibrary> lib;
    std::unique_ptr<FontResolver> resolver;
    bool ok = false;

    explicit Env(const std::string& fixture) {
        doc = Document::openOrThrow(edit(fixture));
        lib = std::make_unique<FontLibrary>(doc->context().get());
        resolver = std::make_unique<FontResolver>(*lib, system);
        ok = system.face("Arial", 400, false).has_value() && system.face("Segoe UI", 400, false).has_value();
    }
    std::unique_ptr<FontProbe> font(const std::string& resource) {
        return FontProbe::forPage(*doc, 0, resource);
    }
};

bool hasNotice(const FontResolver& r, FontNoticeKind kind, const std::string& replacement = {}) {
    for (const auto& n : r.notices()) {
        if (n.kind == kind && (replacement.empty() || n.replacement == replacement)) {
            return true;
        }
    }
    return false;
}

}  // namespace

// Правила 1 и 2: латинское подмножество Arial без «ё».
TEST(font_resolver, extend_subset) {
    Env env("latin.pdf");
    if (!env.ok) {
        GTEST_SKIP() << "нет шрифтов Windows";
    }
    auto f = env.font("F1");
    ASSERT_NE(f, nullptr);
    const FontChoice e = env.resolver->resolve(*f, U'e');
    EXPECT_EQ(e.rule, FontRule::Original);
    EXPECT_FALSE(e.newFont);
    EXPECT_TRUE(env.resolver->notices().empty());

    const FontChoice yo = env.resolver->resolve(*f, U'ё');
    EXPECT_EQ(yo.rule, FontRule::Extend);
    EXPECT_TRUE(yo.newFont);
    EXPECT_EQ(yo.embed, EmbedKind::Cid);
    EXPECT_EQ(file(yo), "arial.ttf");  // файл найден через FakeSystemFonts
    EXPECT_TRUE(fs::exists(yo.face.file));
    EXPECT_FALSE(yo.syntheticBold);
    EXPECT_FALSE(yo.syntheticItalic);
    EXPECT_FALSE(yo.missing);
    EXPECT_TRUE(hasNotice(*env.resolver, FontNoticeKind::SubsetExtended, "Arial"));
    // Тот же шрифт для другого недостающего символа — тот же выбор.
    EXPECT_TRUE(env.resolver->resolve(*f, U'Ж').sameFont(yo));
}

// Правило 4: base14 без встраивания → метрически совместимый системный.
TEST(font_resolver, replace_base14) {
    Env env("type1-diff.pdf");
    if (!env.ok) {
        GTEST_SKIP() << "нет шрифтов Windows";
    }
    auto f = env.font("F1");  // /Helvetica без встраивания
    ASSERT_NE(f, nullptr);
    const FontChoice a = env.resolver->resolve(*f, U'A');
    EXPECT_EQ(a.rule, FontRule::Replace);
    EXPECT_EQ(file(a), "arial.ttf");
    EXPECT_EQ(a.embed, EmbedKind::SimpleWinAnsi);  // латиница — простой TrueType WinAnsi
    const FontChoice zh = env.resolver->resolve(*f, U'Ж');
    EXPECT_EQ(zh.rule, FontRule::Replace);
    EXPECT_EQ(file(zh), "arial.ttf");
    EXPECT_EQ(zh.embed, EmbedKind::Cid);  // вне WinAnsi — CID
    EXPECT_FALSE(a.sameFont(zh));
    EXPECT_TRUE(hasNotice(*env.resolver, FontNoticeKind::Substituted, "Arial"));  // Helvetica → Arial

    Env times("base14.pdf");
    auto t = times.font("F1");  // /Times-Roman
    ASSERT_NE(t, nullptr);
    const FontChoice x = times.resolver->resolve(*t, U'x');
    EXPECT_EQ(x.rule, FontRule::Replace);
    EXPECT_EQ(file(x), "times.ttf");
}

// Правило 3: семейства нет в системе — запасной из настроек, с флагом и сообщением.
TEST(font_resolver, fallback_when_family_missing) {
    OT_FEATURE("edit-49");
    Env env("fonts/fonts.pdf");
    if (!env.ok) {
        GTEST_SKIP() << "нет шрифтов Windows";
    }
    auto sans = env.font("F7");  // встроенное подмножество «FantasySans»
    ASSERT_NE(sans, nullptr);
    EXPECT_EQ(env.resolver->resolve(*sans, U'F').rule, FontRule::Original);
    const FontChoice yo = env.resolver->resolve(*sans, U'ё');
    EXPECT_EQ(yo.rule, FontRule::Fallback);
    EXPECT_TRUE(yo.newFont);
    EXPECT_EQ(yo.embed, EmbedKind::Cid);
    EXPECT_EQ(file(yo), "segoeui.ttf");  // без засечек — Segoe UI
    EXPECT_TRUE(hasNotice(*env.resolver, FontNoticeKind::Substituted, "Segoe UI"));

    auto serif = env.font("F8");  // «FantasySerif-Bold» без встраивания: засечки, жирный
    ASSERT_NE(serif, nullptr);
    const FontChoice a = env.resolver->resolve(*serif, U'A');
    EXPECT_EQ(a.rule, FontRule::Fallback);
    EXPECT_EQ(file(a), "timesbd.ttf");  // засечки — Times New Roman, жирный — своя грань
    EXPECT_FALSE(a.syntheticBold);
    EXPECT_TRUE(hasNotice(*env.resolver, FontNoticeKind::Substituted, "Times New Roman"));

    // Символ, которого нет в запасном, — Segoe UI Symbol; иероглифа нет нигде — «нет глифа».
    const FontChoice snow = env.resolver->resolve(*sans, U'\x2603');
    EXPECT_EQ(file(snow), "seguisym.ttf");
    EXPECT_FALSE(snow.missing);
    const FontChoice han = env.resolver->resolve(*sans, U'\x4E00');
    EXPECT_TRUE(han.missing);
    EXPECT_TRUE(hasNotice(*env.resolver, FontNoticeKind::MissingGlyph));

    // Настройка запасного шрифта (edit-49).
    ot::edit::FallbackFonts fb;
    fb.family = "Georgia";
    env.resolver->setFallback(fb);
    EXPECT_EQ(file(env.resolver->resolve(*sans, U'ё')), "georgia.ttf");
    fb.family = "No Such Family";  // настройка указывает на шрифт, которого нет, — по засечкам
    env.resolver->setFallback(fb);
    EXPECT_EQ(file(env.resolver->resolve(*sans, U'ё')), "segoeui.ttf");
}

// Начертания (edit-03): грань семейства по запросу, недостающая — синтетика.
TEST(font_resolver, styles_and_synthetic) {
    OT_FEATURE("edit-03");
    Env env("fonts/fonts.pdf");
    if (!env.ok) {
        GTEST_SKIP() << "нет шрифтов Windows";
    }
    auto tahoma = env.font("F9");  // «Tahoma-Italic» без встраивания, курсива у Tahoma нет
    ASSERT_NE(tahoma, nullptr);
    const FontChoice a = env.resolver->resolve(*tahoma, U'A');
    EXPECT_EQ(a.rule, FontRule::Replace);
    EXPECT_EQ(file(a), "tahoma.ttf");
    EXPECT_TRUE(a.syntheticItalic);
    EXPECT_FALSE(a.syntheticBold);
    EXPECT_TRUE(hasNotice(*env.resolver, FontNoticeKind::Synthesized));
    StyleRequest bold;
    bold.weight = 700;
    const FontChoice ab = env.resolver->resolve(*tahoma, U'A', bold);
    EXPECT_EQ(file(ab), "tahomabd.ttf");
    EXPECT_TRUE(ab.syntheticItalic);
    EXPECT_FALSE(ab.syntheticBold);

    // Встроенный Arial Bold: начертание как у шрифта — сам шрифт; «обычный» по запросу — грань Arial.
    auto arialBold = env.font("F1");
    ASSERT_NE(arialBold, nullptr);
    EXPECT_EQ(env.resolver->resolve(*arialBold, U'A').rule, FontRule::Original);
    StyleRequest regular;
    regular.weight = 400;
    const FontChoice r = env.resolver->resolve(*arialBold, U'A', regular);
    EXPECT_EQ(r.rule, FontRule::Extend);
    EXPECT_EQ(file(r), "arial.ttf");
    StyleRequest italic;
    italic.italic = 1;
    EXPECT_EQ(file(env.resolver->resolve(*arialBold, U'A', italic)), "arialbi.ttf");

    // Synthetic: Tr 2, обводка кегль · 0,03, наклон 0,2.
    FontChoice synth;
    synth.syntheticBold = true;
    synth.syntheticItalic = true;
    const ot::edit::SyntheticStyle st = ot::edit::syntheticStyle(synth, 12);
    EXPECT_TRUE(st.active());
    EXPECT_EQ(st.render, 2);
    EXPECT_NEAR(st.lineWidth, 0.36, 1e-6);
    EXPECT_NEAR(st.skew, 0.2, 1e-6);
    const ot::Matrix tm = ot::edit::syntheticTextMatrix(ot::Matrix::translate(100, 200), st);
    EXPECT_NEAR(tm.c, 0.2, 1e-6);  // наклон в пространстве текста: x' = x + 0,2 · y
    EXPECT_NEAR(tm.e, 100, 1e-6);
    EXPECT_NEAR(tm.f, 200, 1e-6);
    EXPECT_EQ(ot::edit::syntheticOps(st), std::string("2 Tr 0.36 w"));
    EXPECT_FALSE(ot::edit::syntheticStyle(FontChoice{}, 12).active());
    EXPECT_EQ(ot::edit::syntheticOps(ot::edit::syntheticStyle(FontChoice{}, 12)), std::string());
}

// Пробеги: набор «Привет, world! — 2026 €» в абзац с латинским подмножеством — три пробега, два шрифта.
TEST(font_resolver, runs_mixed_text) {
    OT_FEATURE("edit-01");
    Env env("fonts/mixed-latin.pdf");
    if (!env.ok) {
        GTEST_SKIP() << "нет шрифтов Windows";
    }
    auto f = env.font("F1");
    ASSERT_NE(f, nullptr);
    const auto runs = env.resolver->runs(*f, U"Привет, world! — 2026 €");
    ASSERT_EQ(runs.size(), 3u);
    EXPECT_EQ(runs[0].text, U"Привет");
    EXPECT_EQ(runs[0].choice.rule, FontRule::Extend);
    EXPECT_EQ(runs[1].text, U", world! ");
    EXPECT_EQ(runs[1].choice.rule, FontRule::Original);
    EXPECT_EQ(runs[2].text, U"— 2026 €");
    EXPECT_EQ(runs[2].choice.rule, FontRule::Extend);
    EXPECT_TRUE(runs[0].choice.sameFont(runs[2].choice));  // два шрифта: исходный и новый Arial
    EXPECT_EQ(file(runs[0].choice), "arial.ttf");
    EXPECT_EQ(env.resolver->runs(*f, U"").size(), 0u);
    const auto latin = env.resolver->runs(*f, U"hello world");
    ASSERT_EQ(latin.size(), 1u);
    EXPECT_EQ(latin[0].choice.rule, FontRule::Original);
}
