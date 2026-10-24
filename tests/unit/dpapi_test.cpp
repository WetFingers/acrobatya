// unit.dpapi — пароли удостоверений через DPAPI текущего пользователя (план 4.2, п. 4; вопрос 12.5;
// эталон 3.8 — «.pfx с паролем через DPAPI по желанию»): CryptProtectData/CryptUnprotectData без окна — туда
// и обратно, испорченный или чужой (другая энтропия) блок — nullopt; DpapiProtector — защитник IdentityStore;
// Identities (services) — хранилище над state.sqlite и <данные>\ids\ с DPAPI: запомненный пароль
// переживает новый объект.
#include <gtest/gtest.h>

#include <QByteArray>
#include <QDir>
#include <QString>

#include <filesystem>
#include <string>

#include "core/sign/identity.h"
#include "core/util/paths.h"
#include "services/dpapi.h"
#include "services/identities.h"
#include "services/state_db.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;

TEST(dpapi, protect_unprotect_roundtrip) {
    const QByteArray secret = QByteArrayLiteral("\xD0\xBF\xD0\xB0\xD1\x80\xD0\xBE\xD0\xBB\xD1\x8C 123");
    const QByteArray blob = ot::dpapi::protect(secret);
    ASSERT_FALSE(blob.isEmpty());
    EXPECT_FALSE(blob.contains(secret)) << "в блоке нет открытого текста";
    const auto back = ot::dpapi::unprotect(blob);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(*back, secret);
    EXPECT_NE(ot::dpapi::protect(secret), blob) << "соль — каждый раз свой блок";
}

TEST(dpapi, tampered_or_foreign_blob_is_rejected) {
    QByteArray blob = ot::dpapi::protect(QByteArrayLiteral("secret"));
    ASSERT_GT(blob.size(), 40);
    QByteArray broken = blob;
    broken[broken.size() - 5] = static_cast<char>(broken[broken.size() - 5] ^ 0x55);
    EXPECT_FALSE(ot::dpapi::unprotect(broken).has_value());
    EXPECT_FALSE(ot::dpapi::unprotect(QByteArrayLiteral("not a dpapi blob")).has_value());
    const QByteArray other =
        ot::dpapi::protect(QByteArrayLiteral("secret"), QByteArrayLiteral("other entropy"));
    EXPECT_FALSE(ot::dpapi::unprotect(other).has_value()) << "энтропия программы — своя";
    EXPECT_TRUE(ot::dpapi::unprotect(other, QByteArrayLiteral("other entropy")).has_value());
}

TEST(dpapi, protector_for_identity_store) {
    ot::DpapiProtector protector;
    const std::string pw = "секрет";
    const ot::sign::Bytes blob =
        protector.protect(std::span(reinterpret_cast<const std::uint8_t*>(pw.data()), pw.size()));
    const auto back = protector.unprotect(blob);
    ASSERT_TRUE(back.has_value());
    EXPECT_EQ(std::string(back->begin(), back->end()), pw);
}

TEST(dpapi, identities_service_remembers_password) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const QString data = QString::fromStdWString(dir.path().wstring());
    const fs::path source = dir.path() / L"rsa.pfx";
    ot::paths::writeFile(source, ot::test::readPki("signer-rsa.pfx"));
    std::string id;
    {
        ot::StateDb db(QDir(data).filePath(QStringLiteral("state.sqlite")));
        ASSERT_TRUE(db.isOpen());
        ot::Identities identities(db, QDir(data).filePath(QStringLiteral("ids")));
        const ot::sign::IdentityRecord rec = identities.with(
            [&](ot::sign::IdentityStore& s) { return s.addPkcs12(source, "acrobatya", true); });
        id = rec.id;
        EXPECT_TRUE(fs::exists(dir.path() / L"ids" / rec.file.filename()));
    }
    ot::StateDb db(QDir(data).filePath(QStringLiteral("state.sqlite")));
    ot::Identities identities(db, QDir(data).filePath(QStringLiteral("ids")));
    const ot::sign::SigningKey key = identities.with([&](ot::sign::IdentityStore& s) { return s.open(id); });
    EXPECT_TRUE(key.key) << "пароль — из DPAPI";
}
