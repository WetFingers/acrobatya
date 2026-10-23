// unit.text_export.* — текстовые форматы (эталон 3.12, план 1.4): TXT в UTF-8 (без BOM и с ним), UTF-16 (LE с
// BOM) и CP1251 (читается обратно; чего нет в 1251 — «?»), строки CRLF, страницы через \f, порядок чтения;
// HTML и XHTML через fz_print_stext_page_as_* с картинками base64; XML; RTF — свой писатель, проверяется
// структурно (у Qt нет RTF-ридера): {\rtf1, \par по абзацам, \b и \i, кегль \fs, кириллица \uN?, только
// ASCII, скобки сбалансированы); диапазон страниц, отмена, DocumentSession::exportText в файл с кириллицей в
// пути. text_export_compat.DISABLED_write_rtf пишет файл для compat.rtf (Word COM: Content.Text содержит
// «Привет»).
#include <gtest/gtest.h>

#include <windows.h>

#include <QFuture>
#include <QString>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>

#include "core/create/rtf_writer.h"
#include "core/create/text_export.h"
#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::TextEncoding;
using ot::TextExportOptions;
using ot::TextFormat;

namespace fs = std::filesystem;

namespace {

fs::path fixture(const char* rel) { return ot::test::sourceDir() / rel; }

std::string exportOf(const char* rel, TextExportOptions o) {
    auto doc = Document::openOrThrow(fixture(rel));
    return ot::exportText(*doc, o);
}

TextExportOptions as(TextFormat f, TextEncoding e = TextEncoding::Utf8) {
    TextExportOptions o;
    o.format = f;
    o.encoding = e;
    return o;
}

std::string fromCodePage(const std::string& bytes, UINT codePage) {
    const int n = MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), nullptr, 0);
    std::wstring w(static_cast<std::size_t>(n), L'\0');
    MultiByteToWideChar(codePage, 0, bytes.data(), static_cast<int>(bytes.size()), w.data(), n);
    return ot::paths::narrow(w);
}

int count(const std::string& s, const std::string& what) {
    int n = 0;
    for (std::size_t p = s.find(what); p != std::string::npos; p = s.find(what, p + what.size())) {
        ++n;
    }
    return n;
}

}  // namespace

TEST(text_export, txt_utf8_crlf_pages_reading_order) {
    OT_FEATURE("create-34");
    const std::string txt = exportOf("tests/fixtures/text/search.pdf", as(TextFormat::Txt));
    EXPECT_NE(txt.rfind("\xEF\xBB\xBF", 0), 0u);  // без BOM по умолчанию
    EXPECT_EQ(txt.rfind("Оттиск: поиск и выделение текста\r\n", 0), 0u) << txt.substr(0, 80);
    EXPECT_NE(txt.find("Первая строка для выделения.\r\nВторая строка для выделения.\r\n"),
              std::string::npos);
    EXPECT_EQ(count(txt, "\f"), 2);  // 3 страницы
    EXPECT_LT(txt.find("Красная строка"), txt.find("Вторая страница"));
    EXPECT_LT(txt.find("Вторая страница"), txt.find("Третья страница"));
    for (std::size_t p = txt.find('\n'); p != std::string::npos; p = txt.find('\n', p + 1)) {
        ASSERT_GT(p, 0u);
        ASSERT_EQ(txt[p - 1], '\r') << "LF without CR at " << p;
    }
}

TEST(text_export, txt_utf16_and_bom) {
    OT_FEATURE("create-34");
    const std::string utf8 = exportOf("tests/data/mini/cyr-cid.pdf", as(TextFormat::Txt));
    const std::string utf16 =
        exportOf("tests/data/mini/cyr-cid.pdf", as(TextFormat::Txt, TextEncoding::Utf16));
    ASSERT_GE(utf16.size(), 2u);
    EXPECT_EQ(utf16.substr(0, 2), std::string("\xFF\xFE"));
    ASSERT_EQ(utf16.size() % 2, 0u);
    const std::wstring wide(reinterpret_cast<const wchar_t*>(utf16.data() + 2), (utf16.size() - 2) / 2);
    EXPECT_EQ(ot::paths::narrow(wide), utf8);
    TextExportOptions bom = as(TextFormat::Txt);
    bom.bom = true;
    const std::string withBom = exportOf("tests/data/mini/cyr-cid.pdf", bom);
    EXPECT_EQ(withBom, "\xEF\xBB\xBF" + utf8);
}

TEST(text_export, txt_cp1251_reads_back) {
    OT_FEATURE("create-34");
    // Type1 с /Differences: кириллица, «—», «Ё», «€ ± § ©» — всё есть в 1251, обратно — то же, что UTF-8.
    const std::string utf8 = exportOf("tests/data/mini/type1-diff.pdf", as(TextFormat::Txt));
    const std::string ansi =
        exportOf("tests/data/mini/type1-diff.pdf", as(TextFormat::Txt, TextEncoding::Cp1251));
    EXPECT_EQ(fromCodePage(ansi, 1251), utf8);
    EXPECT_NE(utf8.find("Привет, мир — Ёлка, ёж, щука."), std::string::npos);
    // Лигатуры и метка ударения в 1251 не входят — «?» (без «похожих» замен).
    const std::string search =
        exportOf("tests/fixtures/text/search.pdf", as(TextFormat::Txt, TextEncoding::Cp1251));
    const std::string back = fromCodePage(search, 1251);
    EXPECT_NE(back.find("?нал, ?аг"), std::string::npos);
    EXPECT_NE(back.find("отти?ск"), std::string::npos);
}

TEST(text_export, html_xhtml_xml_with_images) {
    OT_FEATURE("create-33");
    ot::MuContext mu;
    auto doc = Document::openOrThrowMemory(ot::test::makeMixedPdf(mu, ot::test::MixedContent::Full));
    const std::string html = ot::exportText(*doc, as(TextFormat::Html));
    EXPECT_EQ(html.rfind("<!DOCTYPE html>", 0), 0u) << html.substr(0, 60);
    EXPECT_NE(html.find("<div id=\"page1\""), std::string::npos);
    EXPECT_NE(html.find("data:image/"), std::string::npos);
    EXPECT_NE(html.find(";base64,"), std::string::npos);
    EXPECT_NE(html.find("</html>"), std::string::npos);

    const std::string xhtml = ot::exportText(*doc, as(TextFormat::Xhtml));
    EXPECT_NE(xhtml.find("<html xmlns=\"http://www.w3.org/1999/xhtml\""), std::string::npos)
        << xhtml.substr(0, 200);
    EXPECT_NE(xhtml.find("data:image/"), std::string::npos);

    auto cyr = Document::openOrThrow(fixture("tests/data/mini/cyr-cid.pdf"));
    const std::string xml = ot::exportText(*cyr, as(TextFormat::Xml));
    EXPECT_EQ(xml.rfind("<?xml version=\"1.0\" encoding=\"UTF-8\"?>", 0), 0u);
    EXPECT_NE(xml.find("<document"), std::string::npos);
    EXPECT_NE(xml.find("<page id=\"page1\""), std::string::npos);
    EXPECT_NE(xml.find("</document>"), std::string::npos);
    const std::string cyrHtml = ot::exportText(*cyr, as(TextFormat::Html));
    // MuPDF пишет всё вне ASCII ссылками на символы: «Привет».
    EXPECT_NE(cyrHtml.find("&#x41f;&#x440;&#x438;&#x432;&#x435;&#x442;"), std::string::npos);
}

TEST(text_export, rtf_structure) {
    OT_FEATURE("create-34");
    const std::string rtf = exportOf("tests/fixtures/text/search.pdf", as(TextFormat::Rtf));
    EXPECT_EQ(rtf.rfind("{\\rtf1\\ansi", 0), 0u) << rtf.substr(0, 60);
    EXPECT_EQ(rtf.back(), '}');
    int depth = 0;
    for (std::size_t i = 0; i < rtf.size(); ++i) {
        const auto c = static_cast<unsigned char>(rtf[i]);
        ASSERT_LT(c, 0x80u) << "non-ASCII byte at " << i;  // всё вне ASCII — \uN?
        if (rtf[i] == '\\') {
            ++i;  // \{ \} \\ — не скобки
            continue;
        }
        depth += rtf[i] == '{' ? 1 : (rtf[i] == '}' ? -1 : 0);
        ASSERT_GE(depth, 0);
    }
    EXPECT_EQ(depth, 0);
    EXPECT_NE(rtf.find("{\\fonttbl"), std::string::npos);
    EXPECT_NE(rtf.find("Arial;"), std::string::npos);
    EXPECT_GE(count(rtf, "\\par"), 10);  // абзацы трёх страниц
    EXPECT_EQ(count(rtf, "\\page"), 2);  // между страницами
    EXPECT_NE(rtf.find("\\b "), std::string::npos);
    EXPECT_NE(rtf.find("\\i "), std::string::npos);
    EXPECT_NE(rtf.find("\\fs40"), std::string::npos);  // заголовок 20 pt
    EXPECT_NE(rtf.find("\\fs24"), std::string::npos);  // текст 12 pt
    // «Оттиск» = О т т и с к → ၔ?႐?႐?ႀ?ႉ?ႂ?
    EXPECT_NE(rtf.find("\\u1054?\\u1090?\\u1090?\\u1080?\\u1089?\\u1082?"), std::string::npos);
    EXPECT_NE(rtf.find("\\u769?"), std::string::npos);    // U+0301 ударение
    EXPECT_NE(rtf.find("\\u-1279?"), std::string::npos);  // U+FB01 (> 0x7FFF — отрицательное)
}

TEST(text_export, rtf_writer_escapes) {
    EXPECT_EQ(ot::rtfText(U"a{b}\\c\td"), "a\\{b\\}\\\\c\\tab d");
    EXPECT_EQ(ot::rtfText(U"Ё"), "\\u1025?");
    EXPECT_EQ(ot::rtfText(U"\U0001F600"), "\\u-10179?\\u-8704?");  // вне BMP — суррогатная пара
    EXPECT_EQ(ot::rtfFontFamily("ABCDEF+TimesNewRomanPS-BoldItalicMT"), "Times New Roman");
    EXPECT_EQ(ot::rtfFontFamily("ArialMT"), "Arial");
    EXPECT_EQ(ot::rtfFontFamily("Arial,Bold"), "Arial");
    EXPECT_EQ(ot::rtfFontFamily("SegoeUI-Semibold"), "Segoe UI");
    EXPECT_EQ(ot::rtfFontFamily(""), "Arial");
}

TEST(text_export, page_range_and_cancel) {
    auto doc = Document::openOrThrow(fixture("tests/fixtures/text/search.pdf"));
    TextExportOptions o = as(TextFormat::Txt);
    o.firstPage = 1;
    o.lastPage = 1;
    const std::string page2 = ot::exportText(*doc, o);
    EXPECT_EQ(page2.rfind("Вторая страница: оттиск, ОТТИСК и Оттиск.\r\n", 0), 0u) << page2;
    EXPECT_EQ(page2.find('\f'), std::string::npos);
    EXPECT_EQ(page2.find("Третья"), std::string::npos);
    try {
        (void)ot::exportText(*doc, as(TextFormat::Rtf), [] { return true; });  // отмена до первой страницы
        FAIL() << "no exception";
    } catch (const ot::MuError& e) {
        EXPECT_EQ(e.kind(), ot::ErrorKind::Aborted);
    }
}

TEST(text_export, session_writes_file) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(fixture("tests/data/mini/cyr-cid.pdf").wstring())));
    const fs::path out = dir.path() / L"экспорт.txt";
    session.exportText(QString::fromStdWString(out.wstring()), as(TextFormat::Txt, TextEncoding::Utf16))
        .waitForFinished();
    ASSERT_TRUE(fs::exists(out));
    const std::string bytes = ot::paths::readFile(out);
    EXPECT_EQ(bytes.substr(0, 2), std::string("\xFF\xFE"));
    session.close();
}

// Файл для compat.rtf (Word COM открывает RTF, Content.Text содержит «Привет»): запуск ctest
// compat.rtf.generate — unit_tests --gtest_also_run_disabled_tests с OTTISK_COMPAT_OUT.
TEST(text_export_compat, DISABLED_write_rtf) {
    const wchar_t* out = _wgetenv(L"OTTISK_COMPAT_OUT");
    ASSERT_NE(out, nullptr) << "OTTISK_COMPAT_OUT is not set";
    const fs::path dir = fs::path(out) / L"rtf";
    fs::remove_all(dir);
    fs::create_directories(dir);
    auto doc = Document::openOrThrow(fixture("tests/data/mini/cyr-cid.pdf"));
    ot::paths::writeFile(dir / L"привет.rtf", ot::exportText(*doc, as(TextFormat::Rtf)));
    auto search = Document::openOrThrow(fixture("tests/fixtures/text/search.pdf"));
    ot::paths::writeFile(dir / L"поиск.rtf", ot::exportText(*search, as(TextFormat::Rtf)));
}
