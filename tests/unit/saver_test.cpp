// unit.saver.* — запись по правилам Saver (эталон 4.4): лёгкая правка — инкремент (исходные байты — префикс
// нового файла), тяжёлая — полная запись с оптимизацией (меньше исходника, qpdf --check = 0), подписанный —
// только инкремент (байты подписанной ревизии не меняются), тяжёлая правка и полная запись подписанного —
// WouldBreakSignatures, восстановленный файл (pdf_can_be_saved_incrementally = 0) и смена шифрования —
// полная, новый документ — только «Сохранить как»; временный файл в папке цели и замена, переоткрытие в том
// же Document, занятый файл — правки живут во временном; линеаризация qpdf; 10 правок → undo ×10 → полная
// запись = исходник по qpdf --json (объекты). saver_compat.DISABLED_write_artifacts пишет файлы для
// compat.save (OTTISK_COMPAT_OUT).
#include <gtest/gtest.h>

#include <windows.h>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/props.h"
#include "core/doc/saver.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/optimize/qpdf_tool.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::Journal;
using ot::QpdfTool;
using ot::SaveError;
using ot::SaveMode;
using ot::SaveOptions;
using ot::Saver;
using ot::SaveReport;

namespace {

fs::path fixture(const std::wstring& rel) { return ot::test::sourceDir() / L"tests" / rel; }

fs::path put(const fs::path& dir, const std::wstring& name, const std::string& bytes) {
    const fs::path p = dir / name;
    ot::paths::writeFile(p, bytes);
    return p;
}

fs::path copyFixture(const fs::path& dir, const std::wstring& rel, const std::wstring& name) {
    return put(dir, name, ot::paths::readFile(fixture(rel)));
}

std::vector<std::wstring> filesIn(const fs::path& dir) {
    std::vector<std::wstring> names;
    for (const auto& e : fs::directory_iterator(dir)) {
        names.push_back(e.path().filename().wstring());
    }
    return names;
}

void expectQpdfClean(const fs::path& file, const std::string& password = {}) {
    const QpdfTool qpdf;
    if (!qpdf.available()) {
        return;  // проверка совместимости — в compat.save; здесь только когда qpdf есть
    }
    const ot::ToolResult r = qpdf.check(file, password);
    EXPECT_EQ(r.exitCode, 0) << ot::paths::toUtf8(file) << "\n" << r.output;
}

void lightEdit(Document& doc) {
    Journal::Operation op(doc, "Page mode");
    ot::test::putRootName(doc, "PageMode", "UseOutlines");
    op.commit();
}

int canSaveIncrementally(const Document& doc) {
    fz_context* ctx = doc.context().get();
    int can = -1;
    ot::mu::call(ctx, [&] { can = pdf_can_be_saved_incrementally(ctx, doc.pdf()); });
    return can;
}

}  // namespace

TEST(saver, light_edit_is_incremental_and_document_reopens_in_place) {
    ot::test::TempDir dir;
    const std::string original = ot::test::makePdf(ot::MuContext::global(), 3);
    const fs::path path = put(dir.path(), L"заметки.pdf", original);
    auto doc = Document::openOrThrow(path);
    lightEdit(*doc);
    EXPECT_EQ(Saver::decide(*doc).mode, SaveMode::Incremental);
    const SaveReport r = Saver::save(*doc);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::Incremental);
    const std::string saved = ot::paths::readFile(path);
    EXPECT_EQ(r.bytes, saved.size());
    ASSERT_GT(saved.size(), original.size());
    EXPECT_EQ(saved.substr(0, original.size()), original);  // инкремент дописывает, не трогая прежние байты
    // Тот же объект Document, открыт заново из сохранённого: журнал есть, история пуста.
    EXPECT_EQ(doc->path(), path);
    EXPECT_TRUE(doc->journaled());
    EXPECT_EQ(doc->undoStack().size(), 0);
    EXPECT_EQ(ot::test::rootName(*doc, "PageMode"), "UseOutlines");
    EXPECT_EQ(doc->pageCount(), 3);
    EXPECT_EQ(filesIn(dir.path()), std::vector<std::wstring>{L"заметки.pdf"});  // временных не осталось
    expectQpdfClean(path);
    auto fresh = Document::openOrThrow(path);
    EXPECT_EQ(ot::test::rootName(*fresh, "PageMode"), "UseOutlines");
    // Документ после сохранения правится дальше.
    lightEdit(*doc);
    Journal::Operation op(*doc, "Rotate");
    ot::test::setRotate(*doc, 0, 90);
    op.commit();
    EXPECT_TRUE(doc->undoStack().canUndo());
}

TEST(saver, deleting_page_writes_full_optimized_file_smaller_than_source) {
    ot::test::TempDir dir;
    const std::string original = ot::test::makeImagePagesPdf(ot::MuContext::global(), 4, 256, 192);
    const fs::path path = put(dir.path(), L"картинки.pdf", original);
    auto doc = Document::openOrThrow(path);
    ot::test::deletePage(*doc, 1);
    EXPECT_TRUE(doc->undoStack().hasHeavyChanges());
    EXPECT_EQ(Saver::decide(*doc).mode, SaveMode::FullOptimized);
    const SaveReport r = Saver::save(*doc);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::FullOptimized);
    EXPECT_LT(fs::file_size(path), original.size());
    EXPECT_EQ(doc->pageCount(), 3);
    expectQpdfClean(path);
    // Отменённая тяжёлая правка тяжёлой не считается.
    ot::test::deletePage(*doc, 0);
    (void)doc->undoStack().undo();
    EXPECT_FALSE(doc->undoStack().hasHeavyChanges());
}

TEST(saver, signed_document_light_edit_is_incremental_and_signed_bytes_stay) {
    OT_FEATURE("sign-28");
    ot::test::TempDir dir;
    const fs::path path = copyFixture(dir.path(), L"fixtures/sig/signed-bb.pdf", L"подписан.pdf");
    const std::string original = ot::paths::readFile(path);
    auto doc = Document::openOrThrow(path);
    ASSERT_TRUE(doc->isSigned());
    ot::test::addNote(*doc, 0, "after signing");
    EXPECT_EQ(Saver::decide(*doc).mode, SaveMode::Incremental);
    const SaveReport r = Saver::save(*doc);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::Incremental);
    const std::string saved = ot::paths::readFile(path);
    ASSERT_GT(saved.size(), original.size());
    EXPECT_EQ(saved.substr(0, original.size()), original);
    EXPECT_TRUE(doc->isSigned());
    expectQpdfClean(path);
}

TEST(saver, heavy_edit_or_full_write_of_signed_document_would_break_signatures) {
    ot::test::TempDir dir;
    const fs::path path = copyFixture(dir.path(), L"fixtures/sig/signed-bb.pdf", L"подписан.pdf");
    const std::string original = ot::paths::readFile(path);
    auto doc = Document::openOrThrow(path);
    {
        Journal::Operation op(*doc, "Edit text");
        op.markHeavy();
        ot::test::setRotate(*doc, 0, 90);
        op.commit();
    }
    SaveReport r = Saver::save(*doc);
    EXPECT_EQ(r.error, SaveError::WouldBreakSignatures);
    EXPECT_EQ(ot::paths::readFile(path), original);  // файл не тронут
    EXPECT_EQ(doc->pageRotation(0), 90);             // правка в документе осталась
    EXPECT_TRUE(doc->undoStack().canUndo());
    (void)doc->undoStack().undo();
    lightEdit(*doc);
    SaveOptions full;
    full.mode = SaveMode::Full;
    EXPECT_EQ(Saver::decide(*doc, full).error, SaveError::WouldBreakSignatures);
    SaveOptions encrypt;
    encrypt.encryption = ot::SaveEncryption{};
    encrypt.encryption->ownerPassword = "owner";
    EXPECT_EQ(Saver::decide(*doc, encrypt).error, SaveError::WouldBreakSignatures);
    // «Сохранить как копию» с полной записью — по явному согласию.
    full.allowBreakingSignatures = true;
    const fs::path copy = dir.path() / L"копия.pdf";
    r = Saver::save(*doc, copy, full);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::Full);
    EXPECT_EQ(ot::paths::readFile(path), original);
    EXPECT_EQ(doc->path(), copy);
    expectQpdfClean(copy);
}

TEST(saver, repaired_file_cannot_be_saved_incrementally_and_is_written_full) {
    ot::test::TempDir dir;
    const fs::path path = copyFixture(dir.path(), L"data/mini/truncated.pdf", L"обрезан.pdf");
    auto doc = Document::openOrThrow(path);
    ASSERT_TRUE(doc->repaired());
    EXPECT_EQ(canSaveIncrementally(*doc), 0);
    EXPECT_EQ(Saver::decide(*doc).mode, SaveMode::Full);
    SaveOptions incremental;
    incremental.mode = SaveMode::Incremental;
    EXPECT_EQ(Saver::decide(*doc, incremental).error, SaveError::IncrementalNotPossible);
    EXPECT_EQ(Saver::save(*doc, incremental).error, SaveError::IncrementalNotPossible);
    const SaveReport r = Saver::save(*doc);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::Full);
    EXPECT_FALSE(doc->repaired());
    EXPECT_EQ(canSaveIncrementally(*doc), 1);
    EXPECT_EQ(doc->pageCount(), 3);
    expectQpdfClean(path);
}

TEST(saver, new_document_needs_save_as_and_is_written_full) {
    ot::test::TempDir dir;
    auto doc = Document::createBlank(2);
    EXPECT_EQ(Saver::save(*doc).error, SaveError::NoPath);
    const fs::path path = dir.path() / L"новый.pdf";
    const SaveReport r = Saver::save(*doc, path);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::Full);
    EXPECT_EQ(doc->path(), path);
    EXPECT_EQ(doc->pageCount(), 2);
    expectQpdfClean(path);
    // Документ из памяти — так же: «Сохранить как», затем живёт в файле.
    auto mem = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    EXPECT_EQ(Saver::save(*mem).error, SaveError::NoPath);
    const fs::path memPath = dir.path() / L"из памяти.pdf";
    ASSERT_TRUE(Saver::save(*mem, memPath).ok());
    EXPECT_EQ(mem->path(), memPath);
}

TEST(saver, save_as_moves_document_to_new_file_and_keeps_original) {
    ot::test::TempDir dir;
    const std::string original = ot::test::makePdf(ot::MuContext::global(), 2);
    const fs::path path = put(dir.path(), L"исходный.pdf", original);
    auto doc = Document::openOrThrow(path);
    lightEdit(*doc);
    const fs::path other = dir.path() / L"другой.pdf";
    const SaveReport r = Saver::save(*doc, other);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(ot::paths::readFile(path), original);
    EXPECT_EQ(doc->path(), other);
    EXPECT_EQ(ot::test::rootName(*doc, "PageMode"), "UseOutlines");
    // Цель существует — заменяется.
    lightEdit(*doc);
    ASSERT_TRUE(Saver::save(*doc, path).ok());
    EXPECT_EQ(doc->path(), path);
    expectQpdfClean(path);
}

TEST(saver, locked_target_keeps_edits_in_temp_file_and_next_save_replaces) {
    ot::test::TempDir dir;
    const std::string original = ot::test::makePdf(ot::MuContext::global(), 2);
    const fs::path path = put(dir.path(), L"занят.pdf", original);
    auto doc = Document::openOrThrow(path);
    lightEdit(*doc);
    // Чужая программа держит файл без права удаления — заменить нельзя.
    HANDLE lock =
        CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    ASSERT_NE(lock, INVALID_HANDLE_VALUE);
    SaveReport r = Saver::save(*doc);
    EXPECT_EQ(r.error, SaveError::ReplaceFailed) << r.message;
    CloseHandle(lock);
    EXPECT_EQ(ot::paths::readFile(path), original);
    EXPECT_EQ(doc->path(), path);
    EXPECT_EQ(ot::test::rootName(*doc, "PageMode"), "UseOutlines");  // правка жива (документ — из временного)
    r = Saver::save(*doc);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(filesIn(dir.path()), std::vector<std::wstring>{L"занят.pdf"});
    auto fresh = Document::openOrThrow(path);
    EXPECT_EQ(ot::test::rootName(*fresh, "PageMode"), "UseOutlines");
}

TEST(saver, encryption_change_writes_full_and_reopens_with_password) {
    ot::test::TempDir dir;
    const fs::path path = put(dir.path(), L"секрет.pdf", ot::test::makePdf(ot::MuContext::global(), 1));
    auto doc = Document::openOrThrow(path);
    SaveOptions options;
    options.encryption = ot::SaveEncryption{};
    options.encryption->userPassword = "чтение";
    options.encryption->ownerPassword = "владелец";
    EXPECT_EQ(Saver::decide(*doc, options).mode, SaveMode::Full);
    options.mode = SaveMode::Incremental;
    EXPECT_EQ(Saver::decide(*doc, options).error, SaveError::IncrementalNotPossible);
    options.mode = SaveMode::Auto;
    const SaveReport r = Saver::save(*doc, options);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_EQ(r.mode, SaveMode::Full);
    const ot::DocProperties props = doc->properties();
    EXPECT_TRUE(props.encrypted);
    EXPECT_EQ(props.encryptionRevision, 6);
    EXPECT_TRUE(props.ownerAuthenticated);
    EXPECT_TRUE(Document::open(path).needsPassword);  // OpenResult держит файл — только временным
    EXPECT_TRUE(Document::open(path, "чтение").ok);
    expectQpdfClean(path, "владелец");
    // Дальше шифрование сохраняется как есть: лёгкая правка — инкремент.
    lightEdit(*doc);
    const SaveReport again = Saver::save(*doc);
    ASSERT_TRUE(again.ok()) << again.message;
    EXPECT_EQ(again.mode, SaveMode::Incremental);
    EXPECT_TRUE(Document::open(path).needsPassword);
}

TEST(saver, linearize_through_qpdf_for_fast_web_view) {
    const QpdfTool qpdf;
    if (!qpdf.available()) {
        GTEST_SKIP() << "qpdf.exe not found";
    }
    ot::test::TempDir dir;
    const fs::path path = put(dir.path(), L"веб.pdf", ot::test::makePdf(ot::MuContext::global(), 5));
    auto doc = Document::openOrThrow(path);
    SaveOptions options;
    options.linearize = true;
    const SaveReport r = Saver::save(*doc, options);
    ASSERT_TRUE(r.ok()) << r.message;
    EXPECT_TRUE(qpdf.checkLinearization(path).ok()) << qpdf.checkLinearization(path).output;
    EXPECT_TRUE(doc->properties().linearized);
    EXPECT_EQ(filesIn(dir.path()), std::vector<std::wstring>{L"веб.pdf"});
    // Подписанный — не линеаризуется (запись qpdf заденет подпись).
    const fs::path sig = copyFixture(dir.path(), L"fixtures/sig/signed-bb.pdf", L"подписан.pdf");
    auto signedDoc = Document::openOrThrow(sig);
    EXPECT_EQ(Saver::decide(*signedDoc, options).error, SaveError::WouldBreakSignatures);
}

TEST(saver, ten_edits_undone_then_full_write_equals_source_by_qpdf_json) {
    OT_FEATURE("edit-13");
    const QpdfTool qpdf;
    if (!qpdf.available()) {
        GTEST_SKIP() << "qpdf.exe not found";
    }
    ot::test::TempDir dir;
    const std::string bytes = ot::test::makePdf(ot::MuContext::global(), 3);
    SaveOptions full;
    full.mode = SaveMode::Full;
    auto source = Document::openOrThrowMemory(bytes);
    const fs::path a = dir.path() / L"исходник.pdf";
    ASSERT_TRUE(Saver::save(*source, a, full).ok());
    auto doc = Document::openOrThrowMemory(bytes);
    doc->undoStack().setGroupWindow(std::chrono::milliseconds(0));
    ot::test::tenEdits(*doc);
    for (int i = 0; i < 10; ++i) {
        (void)doc->undoStack().undo();
    }
    const fs::path b = dir.path() / L"после отмены.pdf";
    ASSERT_TRUE(Saver::save(*doc, b, full).ok());
    auto objects = [&](const fs::path& p) {
        const ot::ToolResult r = qpdf.json(p, "qpdf");
        EXPECT_EQ(r.exitCode, 0) << r.output;
        nlohmann::json j = nlohmann::json::parse(r.output);
        nlohmann::json objs = j.at("qpdf").at(1);
        objs.at("trailer").at("value").erase("/ID");  // /ID — время записи
        return objs;
    };
    const nlohmann::json ja = objects(a);
    const nlohmann::json jb = objects(b);
    EXPECT_EQ(ja, jb) << nlohmann::json::diff(ja, jb).dump(1).substr(0, 2000);
}

// Файлы для compat.save (qpdf --check, pdfcpu validate -m strict, mutool clean, pypdfium2, pyHanko): запуск
// ctest compat.save.generate — unit_tests --gtest_also_run_disabled_tests с OTTISK_COMPAT_OUT.
TEST(saver_compat, DISABLED_write_artifacts) {
    const char* out = std::getenv("OTTISK_COMPAT_OUT");
    ASSERT_NE(out, nullptr) << "OTTISK_COMPAT_OUT is not set";
    const fs::path root = ot::paths::fromUtf8(out);
    const fs::path plain = root / L"plain";
    const fs::path signedDir = root / L"signed";
    const fs::path encrypted = root / L"encrypted";
    for (const fs::path& d : {plain, signedDir, encrypted}) {
        fs::remove_all(d);
        fs::create_directories(d);
    }
    auto check = [](const SaveReport& r, SaveMode mode) {
        ASSERT_TRUE(r.ok()) << r.message;
        EXPECT_EQ(r.mode, mode);
    };
    {
        auto doc = Document::openOrThrow(
            put(plain, L"incremental.pdf", ot::test::makePdf(ot::MuContext::global(), 3)));
        lightEdit(*doc);
        ot::test::addNote(*doc, 1, "Заметка");
        check(Saver::save(*doc), SaveMode::Incremental);
    }
    {
        auto doc = Document::openOrThrow(put(
            plain, L"full-optimized.pdf", ot::test::makeImagePagesPdf(ot::MuContext::global(), 4, 128, 96)));
        ot::test::deletePage(*doc, 2);
        check(Saver::save(*doc), SaveMode::FullOptimized);
    }
    {
        auto doc =
            Document::openOrThrow(copyFixture(plain, L"data/mini/truncated.pdf", L"repaired-full.pdf"));
        check(Saver::save(*doc), SaveMode::Full);
    }
    {
        auto doc = Document::createBlank(2);
        check(Saver::save(*doc, plain / L"new-full.pdf"), SaveMode::Full);
    }
    {
        // Свойства: XMP фикстуры обновлён, у файла без XMP — создан (pdfcpu strict проверяет и метаданные).
        auto doc =
            Document::openOrThrow(copyFixture(plain, L"data/mini/blank-a4.pdf", L"properties-xmp.pdf"));
        ot::DocProperties p = doc->properties();
        p.title = "Свойства & <XMP>";
        p.keywords = "раз; два";
        doc->setProperties(p);
        check(Saver::save(*doc), SaveMode::Incremental);
        auto fresh = Document::openOrThrow(
            put(plain, L"properties-new-xmp.pdf", ot::test::makePdf(ot::MuContext::global(), 1)));
        ot::props::InfoFields info = ot::props::readInfo(*fresh);
        info.title = "Новый XMP";
        info.custom["Department"] = "ОТК";
        ot::props::writeInfo(*fresh, info);
        ot::props::setLang(*fresh, "ru-RU");
        check(Saver::save(*fresh), SaveMode::Incremental);
    }
    {
        auto doc = Document::openOrThrow(
            put(plain, L"linearized.pdf", ot::test::makePdf(ot::MuContext::global(), 4)));
        SaveOptions options;
        options.linearize = true;
        options.mode = SaveMode::FullOptimized;
        check(Saver::save(*doc, options), SaveMode::FullOptimized);
    }
    {
        auto doc = Document::openOrThrow(
            copyFixture(signedDir, L"fixtures/sig/signed-bb.pdf", L"signed-incremental.pdf"));
        ot::test::addNote(*doc, 0, "after signing");
        check(Saver::save(*doc), SaveMode::Incremental);
    }
    {
        auto doc = Document::openOrThrow(
            copyFixture(signedDir, L"fixtures/sig/signed-bb.pdf", L"signed-metadata.pdf"));
        fz_context* ctx = doc->context().get();
        Journal::Operation op(*doc, "Set title");
        ot::mu::call(ctx, [&] { fz_set_metadata(ctx, doc->handle(), FZ_META_INFO_TITLE, "Подписанный"); });
        op.commit();
        check(Saver::save(*doc), SaveMode::Incremental);
    }
    {
        auto doc = Document::openOrThrow(
            put(encrypted, L"aes256.pdf", ot::test::makePdf(ot::MuContext::global(), 2)));
        SaveOptions options;
        options.encryption = ot::SaveEncryption{};
        options.encryption->userPassword = "user";
        options.encryption->ownerPassword = "owner";
        check(Saver::save(*doc, options), SaveMode::Full);
        lightEdit(*doc);
        check(Saver::save(*doc), SaveMode::Incremental);
    }
}
