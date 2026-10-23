// unit.revisions.* — ревизии и подписанная версия (эталон 4.5, sign-28): число версий файла, проверка истории
// после подписей, конец /ByteRange и байты подписанной ревизии [0, конец) во временный файл — на фикстуре B-B
// pyHanko (tests/fixtures/sig/signed-bb.pdf), на ней же после инкрементальной записи (записка после подписи)
// и на DSS modified_after_signature.pdf из корпуса (нет корпуса — пропуск). changeHistoryValid — по правам
// DocMDP и /Lock, changedSince — строгая проверка MuPDF на подпись (pdf_validate_signature).
#include <gtest/gtest.h>

#include <cstdlib>
#include <filesystem>
#include <stdexcept>
#include <string>

#include "core/doc/document.h"
#include "core/doc/revisions.h"
#include "core/doc/saver.h"
#include "core/mu/context.h"
#include "core/util/features.h"
#include "core/util/hash.h"
#include "core/util/paths.h"
#include "support/test_edits.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::Document;
namespace rev = ot::revisions;

namespace {

fs::path signedFixture() { return ot::test::sourceDir() / L"tests/fixtures/sig/signed-bb.pdf"; }

fs::path corpusFile(const std::wstring& rel) {
    const wchar_t* env = _wgetenv(L"OTTISK_CORPUS");
    const fs::path root =
        env != nullptr && *env != L'\0' ? fs::path(env) : fs::path(L"C:\\Tools\\ottisk\\corpus");
    return root / rel;
}

}  // namespace

TEST(revisions, pyhanko_bb_signature_covers_the_whole_file) {
    OT_FEATURE("sign-28");
    const std::string bytes = ot::paths::readFile(signedFixture());
    auto doc = Document::openOrThrow(signedFixture());
    EXPECT_EQ(rev::versionCount(*doc), 2);  // исходная запись + инкремент подписи
    EXPECT_TRUE(rev::changeHistoryValid(*doc));
    const auto sigs = rev::signatures(*doc);
    ASSERT_EQ(sigs.size(), 1u);
    EXPECT_EQ(sigs[0].index, 0);
    EXPECT_EQ(sigs[0].field, "Signature1");
    EXPECT_EQ(sigs[0].rangeEnd, static_cast<std::int64_t>(bytes.size()));
    EXPECT_TRUE(sigs[0].coversWholeFile);
    EXPECT_EQ(sigs[0].version, 2);
    EXPECT_EQ(sigs[0].changedSince, 0);
    EXPECT_EQ(rev::signedRevision(*doc, 0), bytes);
}

TEST(revisions, signed_version_after_incremental_note_is_the_original_signed_file) {
    OT_FEATURE("sign-28");
    ot::test::TempDir dir;
    const std::string original = ot::paths::readFile(signedFixture());
    const fs::path path = dir.path() / L"подписан и дополнен.pdf";
    ot::paths::writeFile(path, original);
    auto doc = Document::openOrThrow(path);
    ot::test::addNote(*doc, 0, "после подписи");
    ASSERT_TRUE(ot::Saver::save(*doc).ok());
    EXPECT_EQ(rev::versionCount(*doc), 3);
    const auto sigs = rev::signatures(*doc);
    ASSERT_EQ(sigs.size(), 1u);
    EXPECT_FALSE(sigs[0].coversWholeFile);
    EXPECT_EQ(sigs[0].rangeEnd, static_cast<std::int64_t>(original.size()));
    // MuPDF строг, как pyHanko: правка страницы с виджетом подписи (её /Annots) — изменение после подписи
    // («стало недействительным одно сохранение назад»); Acrobat назвал бы это разрешённым комментарием —
    // различие разбирает валидатор (4.4). Прав DocMDP и /Lock у подписи нет — история изменений допустима.
    EXPECT_EQ(sigs[0].changedSince, 1);
    EXPECT_TRUE(rev::changeHistoryValid(*doc));
    // «Просмотреть подписанную версию»: байты подписанной ревизии — исходный файл бит в бит.
    const fs::path signedCopy = rev::signedRevisionBytes(*doc, 0, dir.path() / L"подписанные");
    EXPECT_TRUE(fs::exists(signedCopy));
    EXPECT_EQ(ot::hash::sha256File(signedCopy), ot::hash::sha256Hex(original));
    EXPECT_NE(signedCopy.filename().wstring().find(L".signed-1.pdf"), std::wstring::npos);
    auto signedDoc = Document::openOrThrow(signedCopy);
    EXPECT_TRUE(signedDoc->isSigned());
    EXPECT_EQ(rev::versionCount(*signedDoc), 2);
    EXPECT_THROW((void)rev::signedRevision(*doc, 1), std::out_of_range);
}

TEST(revisions, unsigned_document_has_one_version_and_no_signatures) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 2));
    EXPECT_EQ(rev::versionCount(*doc), 1);
    EXPECT_TRUE(rev::changeHistoryValid(*doc));
    EXPECT_TRUE(rev::signatures(*doc).empty());
    EXPECT_THROW((void)rev::signedRevision(*doc, 0), std::out_of_range);
    auto png =
        Document::openOrThrowMemory(std::string("\x89PNG\r\n\x1a\n", 8) + std::string(64, '\0'), ".png");
    EXPECT_EQ(rev::versionCount(*png), 0);
}

TEST(revisions, dss_modified_after_signature_reports_changed_history) {
    OT_FEATURE("sign-28");
    const fs::path file =
        corpusFile(L"dss/dss-pades/src/test/resources/validation/modified_after_signature.pdf");
    if (!fs::exists(file)) {
        GTEST_SKIP() << "corpus dss is not on disk: " << ot::paths::toUtf8(file);
    }
    const std::string bytes = ot::paths::readFile(file);
    auto doc = Document::openOrThrow(file);
    EXPECT_GE(rev::versionCount(*doc), 3);
    const auto sigs = rev::signatures(*doc);
    ASSERT_EQ(sigs.size(), 1u);
    EXPECT_EQ(sigs[0].rangeEnd, 642689);  // /ByteRange [0 575649 632483 10206]
    EXPECT_FALSE(sigs[0].coversWholeFile);
    const std::string signedBytes = rev::signedRevision(*doc, 0);
    ASSERT_EQ(signedBytes.size(), 642689u);
    EXPECT_EQ(signedBytes, bytes.substr(0, signedBytes.size()));
    // После подписи подписанное содержимое изменили: проверка подписи это видит; прав DocMDP и /Lock у этой
    // утверждающей подписи нет, поэтому история изменений по правам формально допустима.
    EXPECT_GT(sigs[0].changedSince, 0);
    EXPECT_TRUE(rev::changeHistoryValid(*doc));
}
