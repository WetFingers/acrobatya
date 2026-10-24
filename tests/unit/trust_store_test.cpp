// unit.trust_store — хранилище доверия (план 4.4, п. 2; эталон 3.8 «Доверие», 12.4; sign-30, sign-32): якоря
// с флагами «подписи / сертификация / динамический контент / JS», отпечатки SHA-1 и SHA-256 в любом
// написании, импорт .cer (DER и PEM), .p7c, .fdf Acrobat, запись и чтение anchors.json (испорченный — в .bad,
// пусто), X509_STORE по назначению, корни Windows — выключены по умолчанию, по опции — в хранилище.
#include <gtest/gtest.h>

#include <filesystem>
#include <string>

#include "core/sign/identity.h"
#include "core/sign/trust_store.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/pki.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using ot::sign::Bytes;
using ot::sign::TrustSource;
using ot::sign::TrustStore;

namespace {

Bytes pki(const char* name) {
    const std::string s = ot::test::readPki(name);
    return Bytes(s.begin(), s.end());
}

int storeCount(X509_STORE* store) {
    ot::sign::Ossl<STACK_OF(X509)> all(X509_STORE_get1_all_certs(store));
    return all ? sk_X509_num(all.get()) : 0;
}

}  // namespace

TEST(trust_store, add_find_flags_remove) {
    OT_FEATURE("sign-30");
    TrustStore store;
    EXPECT_TRUE(store.anchors().empty());
    EXPECT_FALSE(store.useWindowsRoots()) << "корни Windows по умолчанию не доверены (вопрос 12.4)";
    ASSERT_TRUE(store.add(pki("root.cer")));
    EXPECT_FALSE(store.add(pki("root.pem"))) << "тот же сертификат в PEM — уже есть";
    ASSERT_EQ(store.anchors().size(), 1u);
    const ot::sign::TrustAnchor& a = store.anchors()[0];
    EXPECT_EQ(a.cert.commonName, "АкроБатя тест — корневой ЦС");
    EXPECT_EQ(a.sha1.size(), 40u);
    EXPECT_EQ(a.sha256.size(), 64u);
    EXPECT_EQ(a.flags, ot::sign::kTrustSignatures);
    EXPECT_EQ(a.source, TrustSource::User);

    // Отпечаток в любом написании: нижний регистр, пробелы, двоеточия.
    std::string spaced;
    for (std::size_t i = 0; i < a.sha1.size(); i += 2) {
        spaced += a.sha1.substr(i, 2) + (i % 4 == 0 ? " " : ":");
    }
    for (char& c : spaced) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    EXPECT_NE(store.find(spaced), nullptr);
    EXPECT_NE(store.find(a.sha256), nullptr);
    EXPECT_EQ(store.find("00"), nullptr);
    EXPECT_EQ(ot::sign::normalizeThumbprint(" ab:cd\xe2\x80\x8e ef "), "ABCDEF");

    EXPECT_TRUE(store.setFlags(a.sha1, ot::sign::kTrustSignatures | ot::sign::kTrustCertification));
    EXPECT_EQ(store.find(a.sha1)->flags, ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    EXPECT_EQ(store.count(ot::sign::kTrustCertification), 1);
    EXPECT_EQ(store.count(ot::sign::kTrustJavaScript), 0);
    EXPECT_TRUE(store.remove(a.sha256));
    EXPECT_FALSE(store.remove(a.sha256));
    EXPECT_TRUE(store.anchors().empty());
    EXPECT_THROW(store.add(Bytes{1, 2, 3}), ot::sign::OsslError);
}

TEST(trust_store, import_cer_pem_p7c_fdf) {
    OT_FEATURE("sign-32");
    TrustStore store;
    EXPECT_EQ(store.import(pki("chain.pem")), 2) << "выпускающий и корневой ЦС";
    EXPECT_EQ(store.import(pki("root.cer")), 0) << "уже есть";
    const auto signer = ot::test::pkiCert("signer-rsa.pem");
    EXPECT_EQ(store.import(ot::sign::exportCertificate(signer.get(), ot::sign::CertFormat::P7c),
                           ot::sign::kTrustSignatures | ot::sign::kTrustCertification),
              1);
    EXPECT_EQ(store.anchors().size(), 3u);
    TrustStore fdf;
    const Bytes fdfBytes = ot::sign::exportCertificate(signer.get(), ot::sign::CertFormat::Fdf);
    EXPECT_EQ(fdf.import(fdfBytes), 1);
    EXPECT_EQ(fdf.anchors()[0].cert.serial, "2000");
    EXPECT_EQ(fdf.import(Bytes{'x', 'y'}), 0);

    ot::test::TempDir dir;
    const fs::path cer = dir.path() / L"корень.cer";
    ot::paths::writeFile(cer, ot::test::readPki("root.cer"));
    TrustStore fromFile;
    EXPECT_EQ(fromFile.importFile(cer), 1);
    EXPECT_THROW(fromFile.importFile(dir.path() / L"нет.cer"), std::runtime_error);
}

TEST(trust_store, save_load_roundtrip) {
    ot::test::TempDir dir;
    const fs::path trustDir = dir.path() / L"trust";
    {
        TrustStore store;
        store.add(pki("root.cer"), ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
        store.add(pki("inter.cer"), ot::sign::kTrustDynamicContent, TrustSource::Eutl);
        store.setUseWindowsRoots(true);
        store.save(trustDir);
    }
    EXPECT_TRUE(fs::exists(trustDir / L"anchors.json"));
    const TrustStore loaded = TrustStore::load(trustDir);
    ASSERT_EQ(loaded.anchors().size(), 2u);
    EXPECT_TRUE(loaded.useWindowsRoots());
    EXPECT_EQ(loaded.anchors()[0].flags, ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    EXPECT_EQ(loaded.anchors()[1].flags, ot::sign::kTrustDynamicContent);
    EXPECT_EQ(loaded.anchors()[1].source, TrustSource::Eutl);
    EXPECT_EQ(loaded.anchors()[0].cert.der, pki("root.cer"));

    EXPECT_TRUE(TrustStore::load(dir.path() / L"нет").anchors().empty());
    ot::paths::writeFile(trustDir / L"anchors.json", "{ испорчен");
    EXPECT_TRUE(TrustStore::load(trustDir).anchors().empty());
    EXPECT_TRUE(fs::exists(trustDir / L"anchors.json.bad"));
}

TEST(trust_store, x509_store_by_purpose_and_windows_roots) {
    TrustStore store;
    store.add(pki("root.cer"), ot::sign::kTrustSignatures);
    store.add(pki("inter.cer"), ot::sign::kTrustCertification);
    EXPECT_EQ(storeCount(store.x509Store(ot::sign::kTrustSignatures).get()), 1);
    EXPECT_EQ(storeCount(store.x509Store(ot::sign::kTrustCertification).get()), 1);
    EXPECT_EQ(storeCount(store.x509Store(ot::sign::kTrustSignatures | ot::sign::kTrustCertification).get()),
              2);
    EXPECT_EQ(storeCount(store.x509Store(ot::sign::kTrustJavaScript).get()), 0);

    const std::vector<Bytes> roots = ot::sign::windowsRootCertificates();
    ASSERT_FALSE(roots.empty()) << "в Windows всегда есть корни";
    store.setUseWindowsRoots(true);
    EXPECT_GE(storeCount(store.x509Store(ot::sign::kTrustSignatures).get()), 2);
    EXPECT_EQ(store.anchors().size(), 2u) << "корни Windows не копируются в якоря";
}
