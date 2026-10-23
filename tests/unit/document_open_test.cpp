// unit.document_open.* — открытие документа (эталон 4.2): OpenResult, пароли (R2–R6, пустой и кириллический),
// ремонт, флаги PDF/A, подписи, форм и XFA, кириллические пути, не-PDF форматы (только просмотр), свойства и
// права /P. Ожидания — JSON рядом с фикстурами: tests/data/mini/mini.json (tools/gen/make_mini.py),
// tests/fixtures/enc/passwords.json, tests/fixtures/demo/demo.json.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <map>
#include <string>

#include <nlohmann/json.hpp>

#include "core/doc/document.h"
#include "core/mu/context.h"
#include "core/mu/error.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

using nlohmann::json;
using ot::Document;
using ot::ErrorKind;
using ot::MuContext;
using ot::OpenResult;
using ot::Perm;

namespace fs = std::filesystem;

namespace {

fs::path fixture(const wchar_t* rel) { return ot::test::sourceDir() / rel; }

json loadJson(const fs::path& p) { return json::parse(ot::paths::readFile(p)); }

std::unique_ptr<Document> openOk(const fs::path& p, const std::string& password = {}) {
    OpenResult r = Document::open(p, password);
    EXPECT_TRUE(r.ok) << ot::paths::toUtf8(p) << ": " << r.error.message;
    return std::move(r.document);
}

}  // namespace

TEST(document_open, from_memory_counts_pages_and_sizes) {
    MuContext mu;
    OpenResult r = Document::openMemory(ot::test::makePdf(mu, 3));
    ASSERT_TRUE(r.ok) << r.error.message;
    ASSERT_TRUE(r.document);
    EXPECT_FALSE(r.needsPassword);
    EXPECT_FALSE(r.repaired);
    EXPECT_TRUE(r.document->isPdf());
    EXPECT_EQ(r.document->pageCount(), 3);
    const auto box = r.document->pageBox(0);
    EXPECT_FLOAT_EQ(box.width(), 595.0f);
    EXPECT_FLOAT_EQ(box.height(), 842.0f);
    EXPECT_EQ(r.document->pageRotation(0), 0);
}

TEST(document_open, from_cyrillic_folder_and_file) {
    OT_FEATURE("view-01");
    MuContext mu;
    ot::test::TempDir dir;
    const auto folder = dir.path() / L"Папка «проба» ёж";
    const auto file = folder / L"проба — документ №1.pdf";
    ot::paths::writeFile(file, ot::test::makePdf(mu, 2));
    OpenResult r = Document::open(file);
    ASSERT_TRUE(r.ok) << r.error.message;
    EXPECT_EQ(r.document->pageCount(), 2);
    EXPECT_EQ(r.document->path(), file);
}

TEST(document_open, missing_file_is_not_found) {
    ot::test::TempDir dir;
    OpenResult r = Document::open(dir.path() / L"нет-такого.pdf");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.document);
    EXPECT_EQ(r.error.kind, ErrorKind::NotFound);
    EXPECT_THROW(Document::openOrThrow(dir.path() / L"нет-такого.pdf"), std::runtime_error);
}

TEST(document_open, garbage_is_damaged) {
    OpenResult r = Document::openMemory(std::string(64, 'x'), ".pdf");
    EXPECT_FALSE(r.ok);
    EXPECT_FALSE(r.document);
    EXPECT_EQ(r.error.kind, ErrorKind::Damaged) << r.error.code << " " << r.error.message;
    EXPECT_FALSE(r.error.message.empty());
    EXPECT_THROW(Document::openOrThrowMemory(std::string(64, 'x')), ot::MuError);
}

TEST(document_open, truncated_pdf_is_repaired) {
    MuContext mu;
    std::string bytes = ot::test::makePdf(mu, 2);
    bytes.resize(bytes.size() - 60);  // без хвоста xref/trailer — MuPDF чинит
    OpenResult r = Document::openMemory(bytes);
    ASSERT_TRUE(r.ok) << r.error.message;
    EXPECT_TRUE(r.repaired);
    EXPECT_TRUE(r.document->repaired());
    EXPECT_EQ(r.document->pageCount(), 2);
}

TEST(document_open, blank_document_saves_and_reopens) {
    auto blank = Document::createBlank(4);
    EXPECT_EQ(blank->pageCount(), 4);
    const std::string bytes = blank->savePdfToMemory();
    ASSERT_GT(bytes.size(), 100u);
    EXPECT_EQ(bytes.substr(0, 5), "%PDF-");
    auto again = Document::openOrThrowMemory(bytes);
    EXPECT_EQ(again->pageCount(), 4);
}

TEST(document_open, clone_of_base_context_inherits_settings) {
    MuContext base;
    fz_set_aa_level(base.get(), 2);
    OpenResult r = Document::openMemory(base, ot::test::makePdf(base, 1));
    ASSERT_TRUE(r.ok);
    EXPECT_NE(r.document->context().get(), base.get());
    EXPECT_EQ(fz_aa_level(r.document->context().get()), 2);
    EXPECT_EQ(r.document->context().get()->store, base.get()->store);
}

TEST(document_open, mini_fixtures_match_json) {
    const fs::path dir = fixture(L"tests/data/mini");
    const json mini = loadJson(dir / L"mini.json");
    for (const auto& [name, info] : mini["files"].items()) {
        SCOPED_TRACE(name);
        const fs::path file = dir / ot::paths::fromUtf8(name);
        const std::string password = info.value("user_password", std::string());
        OpenResult r = Document::open(file);
        ASSERT_TRUE(r.document) << r.error.message;
        if (!password.empty()) {
            EXPECT_FALSE(r.ok);
            EXPECT_TRUE(r.needsPassword);
            EXPECT_FALSE(r.document->authenticate("неверный"));
            EXPECT_TRUE(r.document->authenticate(password));
        } else {
            EXPECT_TRUE(r.ok) << r.error.message;
            EXPECT_FALSE(r.needsPassword);
        }
        EXPECT_EQ(r.document->pageCount(), info["pages"].get<int>());
        EXPECT_EQ(r.document->repaired(), info.value("repair", false));
        EXPECT_EQ(r.repaired, info.value("repair", false));
        if (info.contains("size_pt")) {
            const auto box = r.document->pageBox(0);
            EXPECT_NEAR(box.width(), info["size_pt"][0].get<double>(), 0.01);
            EXPECT_NEAR(box.height(), info["size_pt"][1].get<double>(), 0.01);
        }
    }
}

TEST(document_open, wrong_password_needs_password_again) {
    OT_FEATURE("security-09");
    const fs::path file = fixture(L"tests/data/mini/enc-r6.pdf");
    OpenResult wrong = Document::open(file, "неверный");
    EXPECT_FALSE(wrong.ok);
    EXPECT_TRUE(wrong.needsPassword);
    ASSERT_TRUE(wrong.document);  // документ остаётся для повторного authenticate()
    EXPECT_EQ(wrong.error.kind, ErrorKind::None);
    OpenResult right = Document::open(file, "user");
    EXPECT_TRUE(right.ok);
    EXPECT_FALSE(right.needsPassword);
    EXPECT_EQ(right.document->pageCount(), 1);
    EXPECT_THROW(Document::openOrThrow(file), std::runtime_error);
    EXPECT_EQ(Document::openOrThrow(file, "user")->pageCount(), 1);
}

TEST(document_open, encrypted_fixtures_passwords_and_permissions) {
    OT_FEATURE("security-09");
    OT_FEATURE("view-46");
    const fs::path dir = fixture(L"tests/fixtures/enc");
    const json passwords = loadJson(dir / L"passwords.json");
    const std::map<std::string, Perm> perms = {
        {"print low resolution", Perm::Print},        {"print high resolution", Perm::PrintHighRes},
        {"extract for any purpose", Perm::Copy},      {"extract for accessibility", Perm::Accessibility},
        {"modify document assembly", Perm::Assemble}, {"modify forms", Perm::FillForms},
        {"modify annotations", Perm::Annotate},       {"modify other", Perm::Edit},
    };
    for (const json& f : passwords["files"]) {
        const std::string name = f["file"].get<std::string>();
        SCOPED_TRACE(name);
        const fs::path file = dir / ot::paths::fromUtf8(name);
        OpenResult bare = Document::open(file);
        ASSERT_TRUE(bare.document) << bare.error.message;
        EXPECT_EQ(bare.ok, f["opens_without_password"].get<bool>());
        EXPECT_EQ(bare.needsPassword, !f["opens_without_password"].get<bool>());

        auto user = openOk(file, f["user_password"].get<std::string>());
        ASSERT_TRUE(user);
        const ot::DocProperties props = user->properties();
        EXPECT_TRUE(props.encrypted);
        EXPECT_EQ(props.encryptionRevision, f["R"].get<int>());
        EXPECT_EQ(props.permissionsP, f["P"].get<int>());
        EXPECT_FALSE(props.ownerAuthenticated);
        EXPECT_FALSE(props.encryption.empty());
        for (const auto& [key, value] : f["permissions"].items()) {
            const auto it = perms.find(key);
            if (it != perms.end()) {
                EXPECT_EQ(user->allowed(it->second), value.get<bool>()) << key;
            }
        }
        auto owner = openOk(file, f["owner_password"].get<std::string>());
        ASSERT_TRUE(owner);
        EXPECT_TRUE(owner->properties().ownerAuthenticated);
        for (const auto& [key, perm] : perms) {
            EXPECT_TRUE(owner->allowed(perm)) << "владелец: " << key;
        }
    }
}

TEST(document_open, plain_document_allows_everything) {
    auto doc = openOk(fixture(L"tests/fixtures/enc/plain.pdf"));
    ASSERT_TRUE(doc);
    const auto props = doc->properties();
    EXPECT_FALSE(props.encrypted);
    EXPECT_EQ(props.encryptionRevision, 0);
    for (Perm p : {Perm::Print, Perm::PrintHighRes, Perm::Copy, Perm::Edit, Perm::Annotate, Perm::FillForms,
                   Perm::Accessibility, Perm::Assemble}) {
        EXPECT_TRUE(doc->allowed(p));
    }
}

TEST(document_open, properties_read_info_version_linearization) {
    OT_FEATURE("view-45");
    auto report = openOk(fixture(L"tests/fixtures/demo/report.pdf"));
    ASSERT_TRUE(report);
    const auto props = report->properties();
    EXPECT_EQ(props.title, "Демонстрационный отчёт");
    EXPECT_EQ(props.pdfVersion, 17);
    EXPECT_EQ(props.format, "PDF 1.7");
    EXPECT_EQ(props.pageCount, 12);
    EXPECT_FALSE(props.linearized);
    EXPECT_FALSE(props.producer.empty());

    auto lin = openOk(fixture(L"tests/fixtures/pick/pdfium_resources/testing/resources/page_labels.pdf"));
    ASSERT_TRUE(lin);
    EXPECT_TRUE(lin->properties().linearized);
    EXPECT_EQ(lin->properties().pdfVersion, 16);
}

TEST(document_open, page_boxes_and_rotation_match_demo_json) {
    const json demo = loadJson(fixture(L"tests/fixtures/demo/demo.json"));
    auto doc = openOk(fixture(L"tests/fixtures/demo/mixed-sizes.pdf"));
    ASSERT_TRUE(doc);
    const json& info = demo["mixed-sizes.pdf"];
    ASSERT_EQ(doc->pageCount(), info["pages"].get<int>());
    for (const json& p : info["pages_info"]) {
        const int index = p["page"].get<int>() - 1;
        SCOPED_TRACE(p["name"].get<std::string>());
        const double inset = p["cropbox_inset"].get<double>();
        double w = p["mediabox"][2].get<double>() - p["mediabox"][0].get<double>() - 2 * inset;
        double h = p["mediabox"][3].get<double>() - p["mediabox"][1].get<double>() - 2 * inset;
        const int rotate = p["rotate"].get<int>();
        if (rotate == 90 || rotate == 270) {
            std::swap(w, h);
        }
        EXPECT_EQ(doc->pageRotation(index), rotate);
        const auto box = doc->pageBox(index);
        EXPECT_NEAR(box.width(), w, 0.01);
        EXPECT_NEAR(box.height(), h, 0.01);
        EXPECT_NEAR(doc->pageSize(index).width, w, 0.01);
    }
}

TEST(document_open, negative_rotation_is_normalized) {
    auto doc = openOk(fixture(L"tests/fixtures/pick/pdfium_resources/testing/resources/bug_713197.pdf"));
    ASSERT_TRUE(doc);
    EXPECT_EQ(doc->pageRotation(0), 270);
}

TEST(document_open, flags_signed_form_xfa_pdfa) {
    struct Case {
        const wchar_t* file;
        bool isSigned, hasForm, hasXfa, pdfa;
    };
    const Case cases[] = {
        {L"tests/data/mini/blank-a4.pdf", false, false, false, false},
        {L"tests/fixtures/sig/signed-bb.pdf", true, false, false, false},  // только поле подписи — не форма
        {L"tests/fixtures/sig/unsigned.pdf", false, false, false, false},
        {L"tests/fixtures/forms/forms-all.pdf", false, true, false, false},
        {L"tests/fixtures/forms/xfa-stub.pdf", false, true, true, false},
        {L"tests/fixtures/pick/bfo/pdfa2-6-1-5-bfo-t01-pass.pdf", false, false, false, true},
    };
    for (const Case& c : cases) {
        SCOPED_TRACE(ot::paths::narrow(c.file));
        OpenResult r = Document::open(fixture(c.file));
        ASSERT_TRUE(r.ok) << r.error.message;
        EXPECT_EQ(r.isSigned, c.isSigned);
        EXPECT_EQ(r.hasForm, c.hasForm);
        EXPECT_EQ(r.hasXfa, c.hasXfa);
        EXPECT_EQ(r.pdfa, c.pdfa);
    }
    OpenResult a = Document::open(fixture(L"tests/fixtures/pick/bfo/pdfa2-6-1-5-bfo-t01-pass.pdf"));
    EXPECT_EQ(a.document->properties().pdfa, "PDF/A-2B");
}

TEST(document_open, image_and_text_open_for_viewing_only) {
    OpenResult png = Document::open(fixture(L"tests/fixtures/images/logo.png"));
    ASSERT_TRUE(png.ok) << png.error.message;
    EXPECT_FALSE(png.document->isPdf());
    EXPECT_EQ(png.document->pageCount(), 1);
    EXPECT_THROW((void)png.document->savePdfToMemory(), ot::MuError);

    OpenResult tif = Document::open(fixture(L"tests/fixtures/images/multipage-g4.tif"));
    ASSERT_TRUE(tif.ok) << tif.error.message;
    EXPECT_FALSE(tif.document->isPdf());
    EXPECT_EQ(tif.document->pageCount(), 2);

    ot::test::TempDir dir;
    const auto txt = dir.path() / L"заметка.txt";
    ot::paths::writeFile(txt, "Привет, мир!\nВторая строка.\n");
    OpenResult t = Document::open(txt);
    ASSERT_TRUE(t.ok) << t.error.message;
    EXPECT_FALSE(t.document->isPdf());
    EXPECT_GE(t.document->pageCount(), 1);
    EXPECT_FALSE(t.document->properties().format.empty());
}
