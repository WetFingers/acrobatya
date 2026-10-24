// unit.permissions.* — права /P как их видит программа (план 2.5b; эталон 7.1; security-08): биты таблицы 22
// ISO 32000-1 с правилами «заполнять формы можно и при бите комментариев», «собирать страницы — и при бите
// правки», печать высокого качества — только при печати; у R2 доступность и сборка идут за копированием и
// правкой. Пароль владельца и файл без шифрования — всё разрешено. Фикстуры — tests/fixtures/enc
// (passwords.json) и PDF с /P без печати, записанный нашим Saver.
#include <gtest/gtest.h>

#include <filesystem>

#include "core/doc/document.h"
#include "core/doc/permissions.h"
#include "core/doc/saver.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::Perm;
using ot::Permissions;

namespace fs = std::filesystem;

namespace {

constexpr int kAll = -4;  // все биты, кроме 1–2

int without(int p, int bit) { return p & ~(1 << (bit - 1)); }

}  // namespace

TEST(permissions, bits_of_revision_3_and_later) {
    OT_FEATURE("security-08");
    const Permissions all = Permissions::fromP(kAll, 4);
    for (Perm p : ot::kAllPerms) {
        EXPECT_TRUE(all.allows(p)) << ot::permName(p);
    }
    EXPECT_FALSE(all.restricted());

    const Permissions noPrint = Permissions::fromP(without(kAll, 3), 4);
    EXPECT_FALSE(noPrint.print);
    EXPECT_FALSE(noPrint.printHighRes);  // высокое качество — только вместе с печатью
    EXPECT_TRUE(noPrint.copy);
    EXPECT_TRUE(noPrint.restricted());

    const Permissions lowRes = Permissions::fromP(without(kAll, 12), 4);
    EXPECT_TRUE(lowRes.print);
    EXPECT_FALSE(lowRes.printHighRes);

    // Без бита комментариев формы заполнять можно по биту 9; без бита правки сборка — по биту 11.
    const Permissions forms = Permissions::fromP(without(without(kAll, 6), 4), 4);
    EXPECT_FALSE(forms.annotate);
    EXPECT_FALSE(forms.edit);
    EXPECT_TRUE(forms.fillForms);
    EXPECT_TRUE(forms.assemble);
    const Permissions noForms = Permissions::fromP(without(without(kAll, 6), 9), 4);
    EXPECT_FALSE(noForms.fillForms);
    const Permissions formsByAnnotate = Permissions::fromP(without(kAll, 9), 4);
    EXPECT_TRUE(formsByAnnotate.fillForms);  // бит 6 разрешает и заполнение форм

    const Permissions noCopy = Permissions::fromP(without(kAll, 5), 4);
    EXPECT_FALSE(noCopy.copy);
    EXPECT_TRUE(noCopy.accessibility);
    EXPECT_TRUE(noCopy.screenReader());  // «копирование для доступности» остаётся
    const Permissions noCopyNoA11y = Permissions::fromP(without(without(kAll, 5), 10), 4);
    EXPECT_FALSE(noCopyNoA11y.screenReader());
}

TEST(permissions, revision_2_follows_copy_and_edit) {
    // R2: битов 9–12 нет — доступность как копирование, сборка как правка, высокое качество как печать.
    const Permissions r2 = Permissions::fromP(-64, 2);  // биты 3–6 сняты, старшие выставлены
    EXPECT_FALSE(r2.print);
    EXPECT_FALSE(r2.printHighRes);
    EXPECT_FALSE(r2.copy);
    EXPECT_FALSE(r2.accessibility);
    EXPECT_FALSE(r2.edit);
    EXPECT_FALSE(r2.assemble);
    EXPECT_FALSE(r2.annotate);
    EXPECT_FALSE(r2.fillForms);
    const Permissions r2all = Permissions::fromP(kAll, 2);
    for (Perm p : ot::kAllPerms) {
        EXPECT_TRUE(r2all.allows(p)) << ot::permName(p);
    }
}

TEST(permissions, fixture_rc4_128_user_and_owner) {
    const fs::path file = ot::test::sourceDir() / L"tests/fixtures/enc/enc-rc4-128.pdf";
    auto user = Document::openOrThrow(file, "user");
    const Permissions u = Permissions::of(*user);
    EXPECT_TRUE(u.print);
    EXPECT_FALSE(u.printHighRes);
    EXPECT_FALSE(u.copy);
    EXPECT_TRUE(u.accessibility);
    EXPECT_FALSE(u.edit);
    EXPECT_FALSE(u.annotate);
    EXPECT_FALSE(u.fillForms);
    EXPECT_FALSE(u.assemble);
    EXPECT_EQ(user->allowed(Perm::Copy), u.copy);

    auto owner = Document::openOrThrow(file, "owner");
    const Permissions o = Permissions::of(*owner);
    EXPECT_TRUE(o.copy);
    EXPECT_FALSE(o.restricted());
}

TEST(permissions, saved_without_print_opens_without_password_and_denies_print) {
    ot::test::TempDir dir;
    const fs::path file = dir.path() / L"без печати.pdf";
    auto blank = Document::createBlank(1);
    ot::SaveOptions options;
    options.mode = ot::SaveMode::Full;
    options.encryption =
        ot::SaveEncryption{ot::SaveEncryption::Method::Aes256, "", "owner", without(kAll, 3)};
    ASSERT_TRUE(ot::Saver::save(*blank, file, options).ok());

    auto doc = Document::openOrThrow(file);  // пустой пароль пользователя
    const Permissions p = Permissions::of(*doc);
    EXPECT_FALSE(p.print);
    EXPECT_TRUE(p.copy);
    EXPECT_TRUE(p.edit);
    EXPECT_FALSE(doc->allowed(Perm::Print));
    EXPECT_FALSE(doc->allowed(Perm::PrintHighRes));

    // Не PDF и PDF без шифрования — всё можно.
    auto plain = Document::openOrThrow(ot::test::sourceDir() / L"tests/fixtures/enc/plain.pdf");
    EXPECT_TRUE(Permissions::of(*plain).print);
    EXPECT_FALSE(Permissions::of(*plain).restricted());
}
