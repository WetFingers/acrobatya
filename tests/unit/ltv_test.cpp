// unit.ltv — сведения для проверки (план 4.5, п. 1; эталон 3.8 «LTV и B-LTA»; вопрос 12.3; sign-21): после
// B-T «Добавить сведения для проверки» пишет /DSS отдельной инкрементальной секцией — вся цепочка подписанта
// с корнем, сертификат службы времени и её цепочка, сертификат респондера OCSP, ответы OCSP (OCSPResponse, не
// Basic) и CRL (разбор qpdf --json и байтов потоков); повторное добавление не дублирует и файл не трогает;
// документ с /DSS проверяется без сети — «LTV включено», уровень LT; /VRI — по опции, ключ — SHA-1 DER
// подписи; сертификация P=1 после /DSS остаётся действительной; решение «Автоматически добавлять при
// сохранении».
#include <gtest/gtest.h>

#include <QFuture>
#include <QObject>
#include <QString>
#include <QStringList>

#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <memory>
#include <set>
#include <string>
#include <vector>

#include "core/doc/document.h"
#include "core/mu/call.h"
#include "core/mu/handles.h"
#include "core/optimize/qpdf_tool.h"
#include "core/sign/ltv.h"
#include "core/sign/ossl.h"
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

extern "C" {
#include <mupdf/pdf.h>
}

#include <openssl/ocsp.h>
#include <openssl/sha.h>

namespace fs = std::filesystem;
using ot::Document;
using ot::sign::Bytes;
using ot::sign::LtvOptions;
using ot::sign::SignRequest;
using ot::sign::TrustStore;

namespace {

Bytes pkiDer(const char* name) {
    const std::string s = ot::test::readPki(name);
    const ot::sign::Ossl<X509> x = ot::sign::loadCertificate(Bytes(s.begin(), s.end()));
    return ot::sign::ossl::toDer(x.get(), i2d_X509, "i2d_X509");
}

TrustStore rootTrust() {
    TrustStore t;
    t.add(pkiDer("root.pem"));
    return t;
}

fs::path signBt(const fs::path& dir, const std::wstring& name, const ot::test::LocalTsa& tsa) {
    const fs::path path = ot::test::unsignedCopy(dir, name);
    SignRequest r;
    r.identity = ot::test::pkiIdentity();
    r.timestamp = tsa.client;
    auto doc = Document::openOrThrow(path);
    (void)ot::sign::signDocument(*doc, r);
    return path;
}

struct Dss {
    std::vector<Bytes> certs, ocsps, crls;
    std::vector<std::string> vriKeys;
    nlohmann::json vri;  // ключ → {Cert, OCSP, CRL: число}
};

std::vector<Bytes> streams(fz_context* ctx, pdf_obj* arr) {
    std::vector<Bytes> out;
    int n = 0;
    ot::mu::call(ctx, [&] { n = pdf_array_len(ctx, arr); });
    for (int i = 0; i < n; ++i) {
        ot::mu::BufferHandle buf(ctx);
        ot::mu::call(ctx, [&] { buf.reset(pdf_load_stream(ctx, pdf_array_get(ctx, arr, i))); });
        unsigned char* data = nullptr;
        const std::size_t len = fz_buffer_storage(ctx, buf, &data);
        out.emplace_back(data, data + len);
    }
    return out;
}

Dss readDss(const fs::path& file) {
    auto doc = Document::openOrThrow(file);
    fz_context* ctx = doc->context().get();
    pdf_document* pdf = doc->pdf();
    Dss d;
    pdf_obj* dss = nullptr;
    ot::mu::call(ctx, [&] { dss = pdf_dict_getp(ctx, pdf_trailer(ctx, pdf), "Root/DSS"); });
    if (dss == nullptr) {
        return d;
    }
    pdf_obj* certs = nullptr;
    pdf_obj* ocsps = nullptr;
    pdf_obj* crls = nullptr;
    pdf_obj* vri = nullptr;
    ot::mu::call(ctx, [&] {
        certs = pdf_dict_gets(ctx, dss, "Certs");
        ocsps = pdf_dict_gets(ctx, dss, "OCSPs");
        crls = pdf_dict_gets(ctx, dss, "CRLs");
        vri = pdf_dict_gets(ctx, dss, "VRI");
    });
    d.certs = streams(ctx, certs);
    d.ocsps = streams(ctx, ocsps);
    d.crls = streams(ctx, crls);
    int n = 0;
    ot::mu::call(ctx, [&] { n = pdf_dict_len(ctx, vri); });
    for (int i = 0; i < n; ++i) {
        std::string key;
        int nc = 0;
        int no = 0;
        int nl = 0;
        ot::mu::call(ctx, [&] {
            key = pdf_to_name(ctx, pdf_dict_get_key(ctx, vri, i));
            pdf_obj* e = pdf_dict_get_val(ctx, vri, i);
            nc = pdf_array_len(ctx, pdf_dict_gets(ctx, e, "Cert"));
            no = pdf_array_len(ctx, pdf_dict_gets(ctx, e, "OCSP"));
            nl = pdf_array_len(ctx, pdf_dict_gets(ctx, e, "CRL"));
        });
        d.vriKeys.push_back(key);
        d.vri[key] = {{"Cert", nc}, {"OCSP", no}, {"CRL", nl}};
    }
    return d;
}

bool contains(const std::vector<Bytes>& set, const Bytes& der) {
    return std::find(set.begin(), set.end(), der) != set.end();
}

// DER первой подписи файла (без дополнения нулями).
Bytes firstSignatureDer(const fs::path& file) {
    const ot::test::DocView v = ot::test::readSignatures(file);
    return v.sigs.at(0).contents;
}

std::string sha1Hex(const Bytes& data) {
    unsigned char md[SHA_DIGEST_LENGTH];
    SHA1(data.data(), data.size(), md);
    return ot::sign::ossl::toHex(std::span<const std::uint8_t>(md, SHA_DIGEST_LENGTH));
}

}  // namespace

// /DSS после B-T: цепочка подписанта с корнем, служба времени, респондер OCSP; OCSPs — OCSPResponse; CRL.
TEST(ltv, dss_after_bt_has_chains_tsa_and_revocation) {
    OT_FEATURE("sign-21");
    ot::test::TempDir dir;
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    const fs::path path = signBt(dir.path(), L"bt-ltv.pdf", tsa);
    const auto net = ot::test::pkiResponder();
    const TrustStore trust = rootTrust();
    LtvOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    const auto before = fs::file_size(path);
    ot::sign::LtvResult r;
    {
        auto doc = Document::openOrThrow(path);
        r = ot::sign::addValidationInfo(*doc, options);
    }
    ASSERT_TRUE(r.dss.written);
    ASSERT_TRUE(r.save.ok()) << r.save.message;
    EXPECT_EQ(r.save.mode, ot::SaveMode::Incremental);
    EXPECT_GT(fs::file_size(path), before);
    ASSERT_EQ(r.collected.signatures.size(), 1u);
    EXPECT_TRUE(r.collected.signatures[0].complete())
        << ::testing::PrintToString(r.collected.signatures[0].missing);

    const Dss d = readDss(path);
    for (const char* cert : {"signer-rsa.pem", "inter.pem", "root.pem", "tsa.pem", "ocsp.pem"}) {
        EXPECT_TRUE(contains(d.certs, pkiDer(cert))) << cert;
    }
    EXPECT_EQ(d.certs.size(), 5u);
    ASSERT_EQ(d.ocsps.size(), 1u);
    for (const Bytes& der : d.ocsps) {
        const unsigned char* p = der.data();
        ot::sign::Ossl<OCSP_RESPONSE> resp(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(der.size())));
        ASSERT_TRUE(resp) << "OCSPs: не OCSPResponse";
        EXPECT_EQ(OCSP_response_status(resp.get()), OCSP_RESPONSE_STATUS_SUCCESSFUL);
    }
    // CRL выпускающего ЦС (служба времени, респондер) и корневого (выпускающий ЦС).
    EXPECT_EQ(d.crls.size(), 2u);
    EXPECT_TRUE(d.vriKeys.empty()) << "/VRI — только по опции";

    // qpdf --json: /DSS в каталоге последней ревизии, в массивах — ссылки на разные потоки.
    const ot::QpdfTool qpdf;
    if (qpdf.available()) {
        const ot::ToolResult json = qpdf.json(path, "qpdf");
        ASSERT_EQ(json.exitCode, 0) << json.output;
        const nlohmann::json o = nlohmann::json::parse(json.output);
        const nlohmann::json& all = o.at("qpdf").at(1);
        const std::string rootRef = all.at("trailer").at("value").at("/Root").get<std::string>();
        const nlohmann::json& root = all.at("obj:" + rootRef).at("value");
        nlohmann::json dss = root.at("/DSS");
        if (dss.is_string()) {
            dss = all.at("obj:" + dss.get<std::string>()).at("value");
        }
        for (const auto& [key, count] :
             {std::pair{"/Certs", 5u}, std::pair{"/OCSPs", 1u}, std::pair{"/CRLs", 2u}}) {
            const nlohmann::json& arr = dss.at(key);
            ASSERT_EQ(arr.size(), count) << key;
            std::set<std::string> refs;
            for (const auto& ref : arr) {
                refs.insert(ref.get<std::string>());
                EXPECT_TRUE(all.at("obj:" + ref.get<std::string>()).contains("stream")) << key;
            }
            EXPECT_EQ(refs.size(), count) << key << ": повторы";
        }
    }
}

// Повторное «Добавить сведения» — нового нет: секция не пишется, повторов нет.
TEST(ltv, repeat_does_not_duplicate) {
    OT_FEATURE("sign-21");
    ot::test::TempDir dir;
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    const fs::path path = signBt(dir.path(), L"повтор.pdf", tsa);
    const auto net = ot::test::pkiResponder();
    const TrustStore trust = rootTrust();
    LtvOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    {
        auto doc = Document::openOrThrow(path);
        ASSERT_TRUE(ot::sign::addValidationInfo(*doc, options).dss.written);
    }
    const Dss first = readDss(path);
    const auto size = fs::file_size(path);
    {
        auto doc = Document::openOrThrow(path);
        const ot::sign::LtvResult again = ot::sign::addValidationInfo(*doc, options);
        EXPECT_FALSE(again.dss.written);
        EXPECT_EQ(again.dss.certs + again.dss.ocsps + again.dss.crls, 0);
    }
    EXPECT_EQ(fs::file_size(path), size);
    const Dss second = readDss(path);
    EXPECT_EQ(second.certs.size(), first.certs.size());
    EXPECT_EQ(second.ocsps.size(), first.ocsps.size());
    EXPECT_EQ(second.crls.size(), first.crls.size());
}

// После /DSS: проверка без сети — действительна, «LTV включено», уровень LT; до /DSS «LTV» нет.
TEST(ltv, document_with_dss_verifies_offline) {
    OT_FEATURE("sign-21");
    OT_FEATURE("sign-33");
    ot::test::TempDir dir;
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    const fs::path path = signBt(dir.path(), L"без сети.pdf", tsa);
    const TrustStore trust = rootTrust();
    const auto offline = ot::test::offlineFetcher();
    ot::sign::VerifyOptions vo;
    vo.trust = &trust;
    vo.fetcher = offline.get();
    vo.revocation = ot::sign::RevocationPolicy::Required;
    {
        auto doc = Document::openOrThrow(path);
        const ot::sign::DocumentVerification v = ot::sign::verifyDocument(*doc, vo);
        ASSERT_EQ(v.signatures.size(), 1u);
        EXPECT_FALSE(v.signatures[0].ltv);
        LtvOptions lo;
        lo.trust = &trust;
        EXPECT_FALSE(ot::sign::ltvEnabled(*doc, v.signatures[0], lo));
    }
    const auto net = ot::test::pkiResponder();
    LtvOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    {
        auto doc = Document::openOrThrow(path);
        ASSERT_TRUE(ot::sign::addValidationInfo(*doc, options).save.ok());
    }
    const std::size_t requests = offline->requests().size();
    auto doc = Document::openOrThrow(path);
    const ot::sign::DocumentVerification v = ot::sign::verifyDocument(*doc, vo);
    ASSERT_EQ(v.signatures.size(), 1u);
    const ot::sign::SignatureInfo& s = v.signatures[0];
    EXPECT_EQ(s.verdict, ot::sign::Verdict::Valid)
        << ot::sign::issueName(s.findings.empty() ? ot::sign::Issue::Untrusted : s.findings[0].issue);
    EXPECT_TRUE(s.ltv);
    EXPECT_EQ(s.level, ot::sign::PadesLevel::LT);
    EXPECT_EQ(s.revocation, ot::sign::RevocationStatus::Good);
    EXPECT_EQ(offline->requests().size(), requests) << "проверка с /DSS не ходит в сеть";
    LtvOptions lo;
    lo.trust = &trust;
    EXPECT_TRUE(ot::sign::ltvEnabled(*doc, s, lo));
    const std::vector<ot::sign::LtvSignature> status = ot::sign::ltvStatus(*doc, lo);
    ASSERT_EQ(status.size(), 1u);
    EXPECT_TRUE(status[0].complete());
}

// /VRI — по опции: ключ — SHA-1 HEX DER подписи (как pyHanko), в записи — сертификаты, OCSP и CRL подписи.
TEST(ltv, vri_by_option_keyed_by_signature_sha1) {
    OT_FEATURE("sign-21");
    ot::test::TempDir dir;
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    const fs::path path = signBt(dir.path(), L"vri.pdf", tsa);
    const auto net = ot::test::pkiResponder();
    const TrustStore trust = rootTrust();
    LtvOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    options.vri = true;
    {
        auto doc = Document::openOrThrow(path);
        ASSERT_TRUE(ot::sign::addValidationInfo(*doc, options).save.ok());
    }
    const Dss d = readDss(path);
    ASSERT_EQ(d.vriKeys.size(), 1u);
    EXPECT_EQ(d.vriKeys[0], sha1Hex(firstSignatureDer(path)));
    EXPECT_EQ(d.vri[d.vriKeys[0]]["Cert"], 5);
    EXPECT_EQ(d.vri[d.vriKeys[0]]["OCSP"], 1);
    EXPECT_EQ(d.vri[d.vriKeys[0]]["CRL"], 2);
}

// Basic-ответ OCSP оборачивается в OCSPResponse (successful), OCSPResponse остаётся как есть.
TEST(ltv, basic_ocsp_wrapped_into_response) {
    const std::string s = ot::test::readPki("ocsp-good.der");
    const Bytes full(s.begin(), s.end());
    EXPECT_EQ(ot::sign::ocspResponseDer(full), full);
    const unsigned char* p = full.data();
    ot::sign::Ossl<OCSP_RESPONSE> resp(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(full.size())));
    ASSERT_TRUE(resp);
    ot::sign::Ossl<OCSP_BASICRESP> basic(OCSP_response_get1_basic(resp.get()));
    const Bytes basicDer = ot::sign::ossl::toDer(basic.get(), i2d_OCSP_BASICRESP, "i2d_OCSP_BASICRESP");
    const Bytes wrapped = ot::sign::ocspResponseDer(basicDer);
    p = wrapped.data();
    ot::sign::Ossl<OCSP_RESPONSE> again(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(wrapped.size())));
    ASSERT_TRUE(again);
    EXPECT_EQ(OCSP_response_status(again.get()), OCSP_RESPONSE_STATUS_SUCCESSFUL);
    EXPECT_EQ(wrapped, full);
    EXPECT_TRUE(ot::sign::ocspResponseDer(Bytes{0x01, 0x02}).empty());
}

// Сертификация P=1: /DSS после неё — разрешённое изменение, подпись действительна (DocMDP не нарушен).
TEST(ltv, certified_p1_stays_valid_after_dss) {
    OT_FEATURE("sign-21");
    ot::test::TempDir dir;
    const fs::path path = ot::test::unsignedCopy(dir.path(), L"p1.pdf");
    ot::test::certify(path, 1);
    const auto net = ot::test::pkiResponder();
    const TrustStore trust = [] {
        TrustStore t;
        t.add(pkiDer("root.pem"), ot::sign::kTrustSignatures | ot::sign::kTrustCertification);
        return t;
    }();
    LtvOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    {
        auto doc = Document::openOrThrow(path);
        ASSERT_TRUE(ot::sign::addValidationInfo(*doc, options).dss.written);
    }
    ot::sign::VerifyOptions vo;
    vo.trust = &trust;
    const auto offline = ot::test::offlineFetcher();
    vo.fetcher = offline.get();
    auto doc = Document::openOrThrow(path);
    const ot::sign::DocumentVerification v = ot::sign::verifyDocument(*doc, vo);
    ASSERT_EQ(v.signatures.size(), 1u);
    EXPECT_EQ(v.signatures[0].kind, ot::sign::SignatureKind::Certification);
    EXPECT_EQ(v.signatures[0].verdict, ot::sign::Verdict::Valid);
    EXPECT_TRUE(v.signatures[0].ltv);
    EXPECT_EQ(v.status, ot::sign::DocumentStatus::Certified);
}

// «Автоматически добавлять при сохранении» (вопрос 12.3): никогда / спросить, если > 10 % размера + 10 КБ /
// всегда.
TEST(ltv, auto_add_decision) {
    using ot::sign::LtvAutoAdd;
    using ot::sign::LtvDecision;
    EXPECT_EQ(ot::sign::decideAutoLtv(LtvAutoAdd::Never, 100000, 10), LtvDecision::Skip);
    EXPECT_EQ(ot::sign::decideAutoLtv(LtvAutoAdd::Always, 100000, 1000000), LtvDecision::Add);
    EXPECT_EQ(ot::sign::decideAutoLtv(LtvAutoAdd::Ask, 100000, 20000),
              LtvDecision::Add);  // 20 000 ≤ 10 000 + 10 240
    EXPECT_EQ(ot::sign::decideAutoLtv(LtvAutoAdd::Ask, 100000, 20241), LtvDecision::Ask);
    EXPECT_EQ(ot::sign::decideAutoLtv(LtvAutoAdd::Ask, 0, 10240), LtvDecision::Add);
    EXPECT_EQ(ot::sign::decideAutoLtv(LtvAutoAdd::Ask, 100000, 0), LtvDecision::Skip);
    EXPECT_EQ(ot::sign::ltvAutoAddFromName("never"), LtvAutoAdd::Never);
    EXPECT_EQ(ot::sign::ltvAutoAddFromName("always"), LtvAutoAdd::Always);
    EXPECT_EQ(ot::sign::ltvAutoAddFromName("что-то"), LtvAutoAdd::Ask);
}

// DocumentSession::addValidationInfo: задача потока документа, сеть — httpFetcher() процесса, запись и
// переоткрытие (сигнал saved).
TEST(ltv, session_adds_validation_info) {
    ot::test::qtApp();
    ot::test::TempDir dir;
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    const fs::path path = signBt(dir.path(), L"сессия.pdf", tsa);
    const auto net = ot::test::pkiResponder();
    const ot::ScopedHttpFetcher scoped(*net);
    auto trust = std::make_shared<TrustStore>(rootTrust());
    ot::DocumentSession session;
    ASSERT_TRUE(session.open(QString::fromStdWString(path.wstring()))) << session.errorString().toStdString();
    QStringList saved;
    QObject::connect(
        &session, &ot::DocumentSession::saved, &session, [&](const QString& p) { saved << p; },
        Qt::DirectConnection);
    const ot::sign::LtvResult r = session.addValidationInfo(trust, false).result();
    EXPECT_TRUE(r.dss.written);
    EXPECT_TRUE(r.save.ok());
    EXPECT_EQ(saved.size(), 1);
    EXPECT_TRUE(session.isOpen());
    EXPECT_EQ(readDss(path).certs.size(), 5u);
}

// Сведения для проверки в файлах compat.sign.lt (pyHanko: подпись цела и доверена по данным /DSS без сети).
TEST(sign_compat, DISABLED_write_lt) {
    const char* outEnv = std::getenv("OTTISK_COMPAT_OUT");  // NOLINT(concurrency-mt-unsafe)
    ASSERT_NE(outEnv, nullptr) << "нужен OTTISK_COMPAT_OUT";
    const fs::path out = fs::path(ot::paths::fromUtf8(outEnv)) / L"sign" / L"lt";
    fs::remove_all(out);
    fs::create_directories(out);
    const ot::test::LocalTsa tsa = ot::test::localTsa();
    const auto net = ot::test::pkiResponder();
    const TrustStore trust = rootTrust();
    LtvOptions options;
    options.trust = &trust;
    options.fetcher = net.get();
    const auto add = [&](const fs::path& file, bool vri) {
        LtvOptions o = options;
        o.vri = vri;
        auto doc = Document::openOrThrow(file);
        ASSERT_TRUE(ot::sign::addValidationInfo(*doc, o).save.ok());
    };
    add(signBt(out, L"rsa-lt.pdf", tsa), false);
    add(signBt(out, L"rsa-lt-vri.pdf", tsa), true);
    const fs::path ec = ot::test::unsignedCopy(out, L"ec-lt.pdf");
    {
        SignRequest r;
        r.identity = ot::test::pkiIdentity("signer-ec.pfx");
        r.timestamp = tsa.client;
        auto doc = Document::openOrThrow(ec);
        (void)ot::sign::signDocument(*doc, r);
    }
    add(ec, false);
}
