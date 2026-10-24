// unit.sign_levels — уровни PAdES при подписании (план 4.5, п. 2; эталон 3.8 «LTV и B-LTA»; sign-21 — уровни,
// sign-20 — метка времени документа и продление): B — без метки и при заданной службе, T — метка в подписи,
// LT — B-T и /DSS следующей секцией (две секции), LTA — ещё метка времени документа третьей секцией; проверка
// без сети — уровни LT/LTA, «LTV включено»; LT/LTA без службы времени — отказ до записи; продление архивной
// метки — /DSS (если есть новое) и новая метка документа, прежние подписи целы.
#include <gtest/gtest.h>

#include <QFuture>
#include <QString>

#include <filesystem>
#include <memory>
#include <string>

#include "core/doc/document.h"
#include "core/doc/revisions.h"
#include "core/sign/doc_timestamp.h"
#include "core/sign/ltv.h"
#include "core/sign/signer.h"
#include "core/sign/trust_store.h"
#include "core/sign/verifier.h"
#include "core/sign/x509_info.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "services/document_session.h"
#include "services/http_fetcher.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"
#include "support/verify_support.h"

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::SignError;
using ot::sign::SignLevel;
using ot::sign::SignRequest;
using ot::sign::SignResult;
using ot::sign::SignStatus;
using ot::sign::TrustStore;

namespace {

TrustStore rootTrust() {
    TrustStore t;
    const std::string s = ot::test::readPki("root.cer");
    t.add(ot::sign::Bytes(s.begin(), s.end()), ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
    return t;
}

struct Env {
    ot::test::LocalTsa tsa = ot::test::localTsa();
    std::shared_ptr<ot::net::FakeHttpFetcher> net = ot::test::pkiResponder();
    TrustStore trust = rootTrust();

    SignRequest request(SignLevel level) const {
        SignRequest r;
        r.identity = ot::test::pkiIdentity();
        r.timestamp = tsa.client;
        r.level = level;
        r.trust = &trust;
        r.revocationFetcher = net.get();
        return r;
    }
};

int versions(const fs::path& file) {
    auto doc = Document::openOrThrow(file);
    return ot::revisions::versionCount(*doc);
}

ot::sign::DocumentVerification verifyOffline(const fs::path& file, const TrustStore& trust) {
    const auto offline = ot::test::offlineFetcher();
    ot::sign::VerifyOptions vo;
    vo.trust = &trust;
    vo.fetcher = offline.get();
    vo.revocation = ot::sign::RevocationPolicy::Required;
    auto doc = Document::openOrThrow(file);
    return ot::sign::verifyDocument(*doc, vo);
}

SignResult signFile(const fs::path& file, const SignRequest& r) {
    auto doc = Document::openOrThrow(file);
    return ot::sign::signDocument(*doc, r);
}

}  // namespace

// B — без метки, даже если служба задана; T — метка в подписи (одна секция).
TEST(sign_levels, b_and_t) {
    OT_FEATURE("sign-21");
    ot::test::TempDir dir;
    const Env env;
    const fs::path b = ot::test::unsignedCopy(dir.path(), L"b.pdf");
    const int base = versions(b);
    const SignResult rb = signFile(b, env.request(SignLevel::B));
    EXPECT_FALSE(rb.timestamped);
    EXPECT_EQ(rb.level, SignLevel::B);
    EXPECT_EQ(versions(b), base + 1);

    const fs::path t = ot::test::unsignedCopy(dir.path(), L"t.pdf");
    const SignResult rt = signFile(t, env.request(SignLevel::T));
    EXPECT_TRUE(rt.timestamped);
    EXPECT_EQ(rt.level, SignLevel::T);
    EXPECT_FALSE(rt.dssWritten);
    EXPECT_FALSE(rt.docTimestamped);
    EXPECT_EQ(versions(t), base + 1);
    const ot::sign::DocumentVerification v = verifyOffline(t, env.trust);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].level, ot::sign::PadesLevel::T);
}

// LT: подпись B-T и /DSS следующей секцией; без сети — действительна, «LTV включено», уровень LT.
TEST(sign_levels, lt_adds_dss_increment) {
    OT_FEATURE("sign-21");
    ot::test::TempDir dir;
    const Env env;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"lt.pdf");
    const int base = versions(file);
    const SignResult r = signFile(file, env.request(SignLevel::LT));
    EXPECT_EQ(r.level, SignLevel::LT);
    EXPECT_TRUE(r.dssWritten);
    EXPECT_TRUE(r.ltvComplete) << ::testing::PrintToString(r.ltvMissing);
    EXPECT_TRUE(r.ltvError.empty()) << r.ltvError;
    EXPECT_EQ(r.dssCertificates, 5);
    EXPECT_EQ(versions(file), base + 2);
    const ot::sign::DocumentVerification v = verifyOffline(file, env.trust);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].verdict, ot::sign::Verdict::Valid);
    EXPECT_EQ(v.signatures[0].level, ot::sign::PadesLevel::LT);
    EXPECT_TRUE(v.signatures[0].ltv);
}

// LTA: подпись → /DSS → метка времени документа (три секции); уровень LTA, метка документа доверена.
TEST(sign_levels, lta_adds_dss_then_document_timestamp) {
    OT_FEATURE("sign-21");
    OT_FEATURE("sign-20");
    ot::test::TempDir dir;
    const Env env;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"lta.pdf");
    const int base = versions(file);
    const SignResult r = signFile(file, env.request(SignLevel::LTA));
    EXPECT_EQ(r.level, SignLevel::LTA);
    EXPECT_TRUE(r.dssWritten);
    EXPECT_TRUE(r.docTimestamped);
    EXPECT_EQ(r.docTimestampField, "Timestamp1");
    EXPECT_FALSE(r.docTimestamp.genTime.empty());
    EXPECT_TRUE(r.archiveError.empty()) << r.archiveError;
    EXPECT_EQ(versions(file), base + 3);
    const ot::sign::DocumentVerification v = verifyOffline(file, env.trust);
    ASSERT_EQ(v.signatures.size(), 2u);
    const ot::sign::SignatureInfo* sig = ot::test::findSignature(v, "Signature1");
    const ot::sign::SignatureInfo* dts = ot::test::findSignature(v, "Timestamp1");
    ASSERT_NE(sig, nullptr);
    ASSERT_NE(dts, nullptr);
    EXPECT_EQ(sig->verdict, ot::sign::Verdict::Valid);
    EXPECT_EQ(sig->level, ot::sign::PadesLevel::LTA);
    EXPECT_TRUE(sig->ltv);
    EXPECT_EQ(dts->kind, ot::sign::SignatureKind::DocTimestamp);
    EXPECT_TRUE(dts->intact);
    EXPECT_TRUE(dts->timestampTrusted);
}

// LT и LTA без службы времени — отказ до записи, файл не тронут.
TEST(sign_levels, lt_without_tsa_refused_before_writing) {
    ot::test::TempDir dir;
    const Env env;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"без службы.pdf");
    const std::string before = ot::paths::readFile(file);
    for (const SignLevel level : {SignLevel::LT, SignLevel::LTA}) {
        SignRequest r = env.request(level);
        r.timestamp = nullptr;
        try {
            (void)signFile(file, r);
            ADD_FAILURE() << "нужна служба времени";
        } catch (const SignError& e) {
            EXPECT_EQ(e.status(), SignStatus::TimestampFailed);
        }
    }
    EXPECT_EQ(ot::paths::readFile(file), before);
}

// Продление (sign-20): к B-LTA — /DSS (новое, если есть) и вторая метка документа; подпись остаётся LTA,
// обе метки документа целы и доверены.
TEST(sign_levels, extend_archive_timestamp) {
    OT_FEATURE("sign-20");
    ot::test::TempDir dir;
    const Env env;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"продление.pdf");
    (void)signFile(file, env.request(SignLevel::LTA));
    const int before = versions(file);
    ot::sign::ArchiveResult a;
    {
        ot::sign::LtvOptions o;
        o.trust = &env.trust;
        o.fetcher = env.net.get();
        auto doc = Document::openOrThrow(file);
        a = ot::sign::extendArchiveTimestamp(*doc, env.tsa.client, o);
    }
    EXPECT_EQ(a.timestamp.field, "Timestamp2");
    EXPECT_TRUE(a.timestamp.save.ok());
    EXPECT_EQ(versions(file), before + (a.ltv.dss.written ? 2 : 1));
    const ot::sign::DocumentVerification v = verifyOffline(file, env.trust);
    ASSERT_EQ(v.signatures.size(), 3u);
    const ot::sign::SignatureInfo* sig = ot::test::findSignature(v, "Signature1");
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(sig->verdict, ot::sign::Verdict::Valid);
    EXPECT_EQ(sig->level, ot::sign::PadesLevel::LTA);
    for (const char* f : {"Timestamp1", "Timestamp2"}) {
        const ot::sign::SignatureInfo* d = ot::test::findSignature(v, f);
        ASSERT_NE(d, nullptr) << f;
        EXPECT_TRUE(d->intact) << f;
        EXPECT_TRUE(d->timestampTrusted) << f;
    }
}

// DocumentSession::extendTimestamp — задача потока документа с записью и переоткрытием.
TEST(sign_levels, session_extends_timestamp) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const Env env;
    const fs::path file = ot::test::unsignedCopy(dir.path(), L"сессия.pdf");
    (void)signFile(file, env.request(SignLevel::T));
    const ot::ScopedHttpFetcher scoped(*env.net);
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(file.wstring()))) << session.errorString().toStdString();
    const ot::sign::ArchiveResult a =
        session.extendTimestamp(env.tsa.client, std::make_shared<TrustStore>(env.trust)).result();
    EXPECT_TRUE(a.ltv.dss.written);
    EXPECT_EQ(a.timestamp.field, "Timestamp1");
    EXPECT_TRUE(session.isOpen());
    const ot::sign::DocumentVerification v = verifyOffline(file, env.trust);
    const ot::sign::SignatureInfo* sig = ot::test::findSignature(v, "Signature1");
    ASSERT_NE(sig, nullptr);
    EXPECT_EQ(sig->level, ot::sign::PadesLevel::LTA);
}

// Файлы compat.sign.lta: B-LTA и продлённый (pyHanko: подпись цела, доверена, LTV по /DSS; метки документа
// целы и доверены; уровень LTA).
TEST(sign_compat, DISABLED_write_lta) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path out = fs::path(ot::paths::fromUtf8(outEnv)) / L"sign" / L"lta";
    fs::remove_all(out);
    fs::create_directories(out);
    const Env env;
    (void)signFile(ot::test::unsignedCopy(out, L"rsa-lta.pdf"), env.request(SignLevel::LTA));
    SignRequest ec = env.request(SignLevel::LTA);
    ec.identity = ot::test::pkiIdentity("signer-ec.pfx");
    (void)signFile(ot::test::unsignedCopy(out, L"ec-lta.pdf"), ec);
    const fs::path extended = ot::test::unsignedCopy(out, L"rsa-lta-extended.pdf");
    (void)signFile(extended, env.request(SignLevel::LTA));
    ot::sign::LtvOptions o;
    o.trust = &env.trust;
    o.fetcher = env.net.get();
    auto doc = Document::openOrThrow(extended);
    (void)ot::sign::extendArchiveTimestamp(*doc, env.tsa.client, o);
}
