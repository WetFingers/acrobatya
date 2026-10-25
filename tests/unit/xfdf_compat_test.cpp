// compat.xfdf (план 5.4, п. 4; критерий этапа 5 «XFDF нашего экспорта импортируется обратно без потерь»):
// xfdf_compat.DISABLED_write пишет <OTTISK_COMPAT_OUT>/xfdf — matrix.xfdf и matrix.fdf (все комментарии
// матрицы annots-all.pdf — 16 подтипов — с ответом, статусом и /RC), imported.pdf (пустая копия матрицы с
// импортом XFDF, полная запись) и expected.json (что было в исходнике: /NM, подтип, /Rect в пунктах PDF,
// автор, текст); tools/xfdf_check.py сверяет XML (пространство имён Adobe, атрибуты по типам), imported.pdf
// (pikepdf) с expected.json и гонит compat_check.py по imported.pdf.
#include <gtest/gtest.h>

#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <string>
#include <vector>

#include "core/annot/annots.h"
#include "core/annot/comments.h"
#include "core/annot/fdf.h"
#include "core/annot/xfdf.h"
#include "core/doc/document.h"
#include "core/doc/saver.h"
#include "core/util/paths.h"
#include "services/xfdf_xml.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::annot::AnnotInfo;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;

TEST(xfdf_compat, DISABLED_write) {
    const char* out = std::getenv("OTTISK_COMPAT_OUT");
    if (out == nullptr || *out == '\0') {
        GTEST_SKIP() << "OTTISK_COMPAT_OUT не задан";
    }
    const fs::path dir = fs::path(ot::paths::fromUtf8(out)) / "xfdf";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const fs::path matrix = ot::test::sourceDir() / "tests" / "fixtures" / "annots" / "annots-all.pdf";
    auto src = Document::openOrThrow(matrix);
    AnnotRef text;
    for (const AnnotInfo& a : ot::annot::listAnnots(*src, 0)) {
        if (a.ref.nm == "ottisk-01-text") {
            text = a.ref;
        }
    }
    ASSERT_TRUE(text.valid());
    ot::annot::addReply(*src, text,
                        {.author = "Анна",
                         .contents = "Ответ с оформлением",
                         .richContents = ot::annot::richContentsFrom(
                             {{.text = "Ответ "}, {.text = "с оформлением", .italic = true}}),
                         .nm = "compat-reply"});
    ot::annot::setStatus(*src, text, ot::annot::kReviewModel, "Accepted", "Анна");

    // Набор — как «Экспорт всех комментариев» панели: комментарии матрицы с ветками. Служебные подтипы
    // (Sound, Movie, Screen, 3D…) XFDF переносит без своих данных — строгий pdfcpu их без /Sound, /Movie… не
    // примет.
    std::vector<AnnotRef> roots;
    for (const ot::annot::Comment& c : ot::annot::buildComments(*src).roots) {
        roots.push_back(c.ref);
    }
    const ot::annot::XfdfDocument model = ot::annot::exportXfdf(*src, roots, "annots-all.pdf");
    ot::paths::writeFile(dir / "matrix.xfdf", ot::xfdf::write(model).toStdString());
    ot::paths::writeFile(dir / "matrix.fdf", ot::annot::exportFdf(*src, roots, "annots-all.pdf"));

    nlohmann::json expected = nlohmann::json::array();
    for (const ot::annot::XfdfAnnot& a : model.annots) {
        expected.push_back(
            {{"element", a.element},
             {"name", a.attribute("name")},
             {"subtype", std::string(ot::annot::annotTypeName(ot::annot::xfdfType(a.element)))},
             {"rect", ot::annot::parseNumbers(a.attribute("rect"))},
             {"title", a.attribute("title")},
             {"contents", a.contents},
             {"inreplyto", a.attribute("inreplyto")}});
    }
    ot::paths::writeFile(dir / "expected.json", expected.dump(2));

    // Пустая копия матрицы + импорт XFDF → полная запись.
    auto dst = Document::openOrThrow(matrix);
    std::vector<AnnotRef> all;
    for (const AnnotInfo& a : ot::annot::listAnnots(*dst, 0)) {
        if (a.type != AnnotType::Popup) {
            all.push_back(a.ref);
        }
    }
    ot::annot::remove(*dst, all);
    const ot::annot::ImportReport report =
        ot::annot::importXfdf(*dst, ot::xfdf::read(ot::xfdf::write(model)), "annots-all.pdf");
    EXPECT_EQ(report.created, static_cast<int>(model.annots.size()));
    const ot::SaveReport saved = ot::Saver::saveCopy(*dst, dir / "imported.pdf");
    ASSERT_TRUE(saved.ok()) << saved.message;
}
