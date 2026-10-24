// Файлы для compat.freetext (план 5.2, п. 1; эталон 3.4 «Ядро», «Acrobat»; критерий этапа 5 — FreeText с
// кириллицей: встроенный TrueType в /DR и «Привет» в pypdfium2): freetext_compat.DISABLED_write пишет в
// $OTTISK_COMPAT_OUT/freetext — boxes.pdf (четыре семейства с кириллицей, выравнивание, рамка и заливка,
// непрозрачность), callout.pdf (выноски с открытой и закрытой стрелкой), rotated.pdf (страница /Rotate 90) —
// инкрементом поверх пустого файла — и expected.json (ожидаемый текст каждого поля); tools/compat_freetext.py
// проверяет pikepdf (шрифт /DA в /AcroForm /DR: Type0, Identity-H, /FontFile2, /ToUnicode с кириллицей),
// pypdfium2 (непустой растр в /Rect, текст после сплющивания) и compat_check.py. Отключён: запускает ctest
// compat.freetext.generate.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <functional>
#include <string>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::annot::AnnotColor;
using ot::annot::AnnotProps;
using ot::annot::AnnotType;

namespace {

AnnotProps box(const char* text, ot::RectF rect, const char* font, float size) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::FreeText);
    p.author = "compat";
    p.contents = text;
    p.rect = rect;
    p.fontName = font;
    p.fontSize = size;
    return p;
}

// Пустой файл, правка fill, инкрементальная запись; ожидания — в expected.
void write(const fs::path& dir, const char* name, nlohmann::json& expected, int rotate,
           const std::function<void(ot::Document&, nlohmann::json&)>& fill) {
    const fs::path path = dir / name;
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), 1));
    auto doc = ot::Document::openOrThrow(path);
    if (rotate != 0) {
        fz_context* ctx = doc->context().get();
        pdf_document* pdf = doc->pdf();
        ot::Journal::Operation op(*doc, "Rotate");
        ot::mu::call(
            ctx, [&] { pdf_dict_put_int(ctx, pdf_lookup_page_obj(ctx, pdf, 0), PDF_NAME(Rotate), rotate); });
        op.invalidate(0);
        op.commit();
    }
    nlohmann::json texts = nlohmann::json::array();
    fill(*doc, texts);
    const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
    ASSERT_TRUE(report.ok()) << report.message;
    expected[name] = texts;
}

}  // namespace

TEST(freetext_compat, DISABLED_write) {
    const char* out = std::getenv("OTTISK_COMPAT_OUT");
    if (out == nullptr || *out == '\0') {
        GTEST_SKIP() << "OTTISK_COMPAT_OUT не задан";
    }
    const fs::path dir = fs::path(ot::paths::fromUtf8(out)) / "freetext";
    fs::remove_all(dir);
    fs::create_directories(dir);
    nlohmann::json expected = nlohmann::json::object();

    write(dir, "boxes.pdf", expected, 0, [](ot::Document& doc, nlohmann::json& texts) {
        const char* fonts[] = {"Segoe UI", "Arial", "Times New Roman", "Courier New"};
        float y = 60;
        int i = 0;
        for (const char* font : fonts) {
            AnnotProps p = box("Привет, мир — Hello", {60, y, 330, y + 50}, font, 16);
            p.align = i % 3;
            if (i == 1) {
                p.borderWidth = 1.5f;
                p.color = AnnotColor::rgb(1, 1, 0.8f);
                p.textColor = AnnotColor::rgb(0.1f, 0.2f, 0.7f);
            }
            if (i == 3) {
                p.opacity = 0.6f;
                p.dash = {3, 2};
                p.borderWidth = 1;
            }
            ot::annot::create(doc, 0, p);
            texts.push_back("Привет, мир");
            y += 80;
            ++i;
        }
        AnnotProps wrap = box("Длинный текст переносится по ширине поля на несколько строк",
                              {360, 60, 520, 200}, "Arial", 12);
        wrap.borderWidth = 1;
        ot::annot::create(doc, 0, wrap);
        texts.push_back("Длинный текст");
    });

    write(dir, "callout.pdf", expected, 0, [](ot::Document& doc, nlohmann::json& texts) {
        AnnotProps a = box("Выноска", {260, 100, 420, 140}, "Arial", 14);
        a.intent = "FreeTextCallout";
        a.callout = {{90, 260}, {200, 120}, {260, 120}};
        a.lineStartStyle = ot::annot::LineEnding::OpenArrow;
        a.borderWidth = 1;
        ot::annot::create(doc, 0, a);
        texts.push_back("Выноска");
        AnnotProps b = box("Закрытая стрелка", {260, 400, 460, 440}, "Segoe UI", 14);
        b.intent = "FreeTextCallout";
        b.callout = {{90, 520}, {200, 420}, {260, 420}};
        b.lineStartStyle = ot::annot::LineEnding::ClosedArrow;
        b.borderWidth = 2;
        b.color = AnnotColor::rgb(0.9f, 0.95f, 1);
        ot::annot::create(doc, 0, b);
        texts.push_back("Закрытая стрелка");
    });

    write(dir, "rotated.pdf", expected, 90, [](ot::Document& doc, nlohmann::json& texts) {
        AnnotProps p = box("Повёрнутая страница", {60, 60, 300, 100}, "Arial", 14);
        p.borderWidth = 1;
        ot::annot::create(doc, 0, p);
        texts.push_back("Повёрнутая страница");
    });

    ot::paths::writeFile(dir / "expected.json", expected.dump(2));
}
