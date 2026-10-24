// unit.pki — тестовая PKI и локальный TSA (план 4.1, п. 2; эталон 9.2): все файлы tests/fixtures/pki на месте
// и разбираются, сроки — больше 5 лет вперёд, ключи подходят к сертификатам, цепочки строятся до корня, у
// службы времени единственный EKU id-kp-timeStamping критический, CRL и OCSP-ответы — как в pki.json;
// tools/pki/tsa_serve.py отвечает на RFC 3161-запрос токеном, который проверяется по корню PKI (nonce и
// imprint сверены).
#include <gtest/gtest.h>

#include <openssl/err.h>
#include <openssl/pem.h>
#include <openssl/rand.h>

#include <QByteArray>
#include <QNetworkAccessManager>
#include <QNetworkProxy>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QUrl>

#include <nlohmann/json.hpp>

#include "core/sign/ossl.h"
#include "support/pki.h"
#include "support/qt_app.h"
#include "support/tsa_server.h"

namespace {

using ot::sign::Bytes;
using ot::sign::Ossl;

nlohmann::json pkiJson() { return nlohmann::json::parse(ot::test::readPki("pki.json")); }

Ossl<X509> loadPem(const std::string& name) {
    const std::string pem = ot::test::readPki(name.c_str());
    Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    return Ossl<X509>(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

Ossl<EVP_PKEY> loadKey(const std::string& name) {
    const std::string pem = ot::test::readPki(name.c_str());
    Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    return Ossl<EVP_PKEY>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

std::string commonName(X509* cert) {
    X509_NAME* name = X509_get_subject_name(cert);
    const int idx = X509_NAME_get_index_by_NID(name, NID_commonName, -1);
    if (idx < 0) {
        return {};
    }
    unsigned char* utf8 = nullptr;
    const int len = ASN1_STRING_to_UTF8(&utf8, X509_NAME_ENTRY_get_data(X509_NAME_get_entry(name, idx)));
    std::string out(reinterpret_cast<const char*>(utf8), len > 0 ? static_cast<std::size_t>(len) : 0);
    OPENSSL_free(utf8);
    return out;
}

std::string serialHex(const ASN1_INTEGER* serial) {
    Ossl<BIGNUM> bn(ASN1_INTEGER_to_BN(serial, nullptr));
    char* hex = BN_bn2hex(bn.get());
    std::string out(hex);
    OPENSSL_free(hex);
    return out;
}

// Дней от сейчас до t (отрицательное — в прошлом).
int daysFromNow(const ASN1_TIME* t) {
    int days = 0;
    int secs = 0;
    EXPECT_EQ(ASN1_TIME_diff(&days, &secs, nullptr, t), 1);
    return days;
}

bool extensionCritical(X509* cert, int nid) {
    const int idx = X509_get_ext_by_NID(cert, nid, -1);
    return idx >= 0 && X509_EXTENSION_get_critical(X509_get_ext(cert, idx)) == 1;
}

}  // namespace

TEST(pki, fixtures) {
    const nlohmann::json j = pkiJson();
    ASSERT_TRUE(j.contains("certs"));
    const std::vector<std::string> required = {"root",    "inter", "signer-rsa", "signer-ec",
                                               "revoked", "tsa",   "ocsp"};
    for (const auto& name : required) {
        ASSERT_TRUE(j["certs"].contains(name)) << name;
    }

    Ossl<X509> root = loadPem("root.pem");
    Ossl<X509> inter = loadPem("inter.pem");
    ASSERT_TRUE(root && inter);
    Ossl<X509_STORE> store(X509_STORE_new());
    ASSERT_EQ(X509_STORE_add_cert(store.get(), root.get()), 1);

    for (const auto& [name, c] : j["certs"].items()) {
        SCOPED_TRACE(name);
        Ossl<X509> cert = loadPem(c["file"].get<std::string>());
        ASSERT_TRUE(cert) << "PEM " << c["file"];
        EXPECT_EQ(commonName(cert.get()), c["subject_cn"].get<std::string>());
        EXPECT_EQ(serialHex(X509_get0_serialNumber(cert.get())), c["serial"].get<std::string>());
        // Сроки: 10 лет (2026-01-01 … 2036-01-01), до конца — больше 5 лет.
        EXPECT_GT(daysFromNow(X509_get0_notAfter(cert.get())), 5 * 365);
        int days = 0;
        int secs = 0;
        ASSERT_EQ(
            ASN1_TIME_diff(&days, &secs, X509_get0_notBefore(cert.get()), X509_get0_notAfter(cert.get())), 1);
        EXPECT_GE(days, 3650);
        Ossl<EVP_PKEY> key = loadKey(c["key"].get<std::string>());
        ASSERT_TRUE(key);
        EXPECT_EQ(X509_check_private_key(cert.get(), key.get()), 1);
        const std::string type = c["key_type"].get<std::string>();
        if (type.rfind("rsa-", 0) == 0) {
            EXPECT_EQ(EVP_PKEY_get_base_id(key.get()), EVP_PKEY_RSA);
            EXPECT_EQ(EVP_PKEY_get_bits(key.get()), std::stoi(type.substr(4)));
        } else {
            EXPECT_EQ(type, "ec-p256");
            EXPECT_EQ(EVP_PKEY_get_base_id(key.get()), EVP_PKEY_EC);
            EXPECT_EQ(EVP_PKEY_get_bits(key.get()), 256);
        }

        // Цепочка до корня строится (без отзыва).
        Ossl<STACK_OF(X509)> untrusted(sk_X509_new_null());
        sk_X509_push(untrusted.get(), X509_dup(inter.get()));
        Ossl<X509_STORE_CTX> ctx(X509_STORE_CTX_new());
        ASSERT_EQ(X509_STORE_CTX_init(ctx.get(), store.get(), cert.get(), untrusted.get()), 1);
        EXPECT_EQ(X509_verify_cert(ctx.get()), 1)
            << X509_verify_cert_error_string(X509_STORE_CTX_get_error(ctx.get()));
    }

    // Роли.
    Ossl<X509> tsa = loadPem("tsa.pem");
    EXPECT_EQ(X509_get_extended_key_usage(tsa.get()), static_cast<uint32_t>(XKU_TIMESTAMP));
    EXPECT_TRUE(extensionCritical(tsa.get(), NID_ext_key_usage));  // RFC 3161 2.3
    EXPECT_EQ(X509_check_purpose(tsa.get(), X509_PURPOSE_TIMESTAMP_SIGN, 0), 1);
    Ossl<X509> ocsp = loadPem("ocsp.pem");
    EXPECT_NE(X509_get_extended_key_usage(ocsp.get()) & XKU_OCSP_SIGN, 0U);
    EXPECT_GE(X509_get_ext_by_NID(ocsp.get(), NID_id_pkix_OCSP_noCheck, -1), 0);
    for (const char* signer : {"signer-rsa.pem", "signer-ec.pem"}) {
        Ossl<X509> s = loadPem(signer);
        EXPECT_NE(X509_get_extended_key_usage(s.get()) & XKU_SMIME, 0U) << signer;
        EXPECT_EQ(X509_check_purpose(s.get(), X509_PURPOSE_SMIME_SIGN, 0), 1) << signer;
        EXPECT_NE(X509_get_key_usage(s.get()) & (KU_DIGITAL_SIGNATURE | KU_NON_REPUDIATION), 0U) << signer;
    }
    EXPECT_EQ(X509_check_ca(root.get()), 1);
    EXPECT_EQ(X509_check_ca(inter.get()), 1);

    // DER-копии корня и выпускающего ЦС — те же сертификаты.
    for (const auto& [name, file] : j["der"].items()) {
        const std::string der = ot::test::readPki(file.get<std::string>().c_str());
        const auto* p = reinterpret_cast<const unsigned char*>(der.data());
        Ossl<X509> x(d2i_X509(nullptr, &p, static_cast<long>(der.size())));
        ASSERT_TRUE(x) << file;
        EXPECT_EQ(X509_cmp(x.get(), name == "root" ? root.get() : inter.get()), 0) << file;
    }

    // chain.pem — выпускающий ЦС и корень.
    {
        const std::string pem = ot::test::readPki("chain.pem");
        Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
        Ossl<X509> first(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
        Ossl<X509> second(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
        ASSERT_TRUE(first && second);
        EXPECT_EQ(X509_cmp(first.get(), inter.get()), 0);
        EXPECT_EQ(X509_cmp(second.get(), root.get()), 0);
        ERR_clear_error();
    }

    // PKCS#12 — на месте (разбор — unit.pkcs12).
    for (const auto& [file, p] : j["pfx"].items()) {
        EXPECT_GT(ot::test::readPki(file.c_str()).size(), 1000U) << file;
        EXPECT_FALSE(p["password"].get<std::string>().empty());
    }

    // CRL: издатель, подпись, отозванные.
    const std::string revokedSerial = j["certs"]["revoked"]["serial"].get<std::string>();
    for (const auto& [file, c] : j["crl"].items()) {
        SCOPED_TRACE(file);
        const std::string der = ot::test::readPki(file.c_str());
        const auto* p = reinterpret_cast<const unsigned char*>(der.data());
        Ossl<X509_CRL> crl(d2i_X509_CRL(nullptr, &p, static_cast<long>(der.size())));
        ASSERT_TRUE(crl);
        X509* issuer = c["issuer"] == "root" ? root.get() : inter.get();
        EXPECT_EQ(X509_NAME_cmp(X509_CRL_get_issuer(crl.get()), X509_get_subject_name(issuer)), 0);
        EXPECT_EQ(X509_CRL_verify(crl.get(), X509_get0_pubkey(issuer)), 1);
        EXPECT_GT(daysFromNow(X509_CRL_get0_nextUpdate(crl.get())), 5 * 365);
        STACK_OF(X509_REVOKED)* revoked = X509_CRL_get_REVOKED(crl.get());
        const int count = revoked != nullptr ? sk_X509_REVOKED_num(revoked) : 0;
        ASSERT_EQ(static_cast<std::size_t>(count), c["revoked"].size());
        if (count == 1) {
            EXPECT_EQ(serialHex(X509_REVOKED_get0_serialNumber(sk_X509_REVOKED_value(revoked, 0))),
                      revokedSerial);
        }
    }

    // OCSP-ответы: успешные, подписаны респондером.
    for (const auto& [file, o] : j["ocsp"].items()) {
        SCOPED_TRACE(file);
        const std::string der = ot::test::readPki(file.c_str());
        const auto* p = reinterpret_cast<const unsigned char*>(der.data());
        Ossl<OCSP_RESPONSE> resp(d2i_OCSP_RESPONSE(nullptr, &p, static_cast<long>(der.size())));
        ASSERT_TRUE(resp);
        EXPECT_EQ(OCSP_response_status(resp.get()), OCSP_RESPONSE_STATUS_SUCCESSFUL);
        Ossl<OCSP_BASICRESP> basic(OCSP_response_get1_basic(resp.get()));
        ASSERT_TRUE(basic);
        EXPECT_EQ(OCSP_resp_count(basic.get()), 1);
    }
}

TEST(pki, tsa_server_replies) {
    std::string reason;
    auto server = ot::test::TsaServer::start(&reason);
    if (!server) {
        GTEST_SKIP() << reason;
    }
    EXPECT_GT(server->port(), 0);

    // Запрос RFC 3161: SHA-256 данных, nonce, certReq.
    const Bytes digest = ot::sign::ossl::sha256(std::string_view("АкроБатя: данные для метки времени"));
    Ossl<TS_REQ> req(TS_REQ_new());
    ASSERT_EQ(TS_REQ_set_version(req.get(), 1), 1);
    Ossl<TS_MSG_IMPRINT> imprint(TS_MSG_IMPRINT_new());
    Ossl<X509_ALGOR> algo(X509_ALGOR_new());
    ASSERT_EQ(X509_ALGOR_set0(algo.get(), OBJ_nid2obj(NID_sha256), V_ASN1_NULL, nullptr), 1);
    ASSERT_EQ(TS_MSG_IMPRINT_set_algo(imprint.get(), algo.get()), 1);
    ASSERT_EQ(TS_MSG_IMPRINT_set_msg(imprint.get(), const_cast<unsigned char*>(digest.data()),
                                     static_cast<int>(digest.size())),
              1);
    ASSERT_EQ(TS_REQ_set_msg_imprint(req.get(), imprint.get()), 1);
    unsigned char nonceBytes[8];
    ASSERT_EQ(RAND_bytes(nonceBytes, sizeof nonceBytes), 1);
    nonceBytes[0] &= 0x7f;
    Ossl<BIGNUM> nonceBn(BN_bin2bn(nonceBytes, sizeof nonceBytes, nullptr));
    Ossl<ASN1_INTEGER> nonce(BN_to_ASN1_INTEGER(nonceBn.get(), nullptr));
    ASSERT_EQ(TS_REQ_set_nonce(req.get(), nonce.get()), 1);
    ASSERT_EQ(TS_REQ_set_cert_req(req.get(), 1), 1);
    const Bytes query = ot::sign::ossl::toDer(req.get(), i2d_TS_REQ, "i2d_TS_REQ");

    ot::test::qtApp();
    QNetworkAccessManager nam;
    nam.setProxy(QNetworkProxy::NoProxy);
    QNetworkRequest request(QUrl(QString::fromStdString(server->url("/tsr"))));
    request.setHeader(QNetworkRequest::ContentTypeHeader, QByteArrayLiteral("application/timestamp-query"));
    QNetworkReply* reply = nam.post(
        request, QByteArray(reinterpret_cast<const char*>(query.data()), static_cast<int>(query.size())));
    ASSERT_TRUE(ot::test::waitUntil([&] { return reply->isFinished(); }, 30000));
    EXPECT_EQ(reply->error(), QNetworkReply::NoError) << reply->errorString().toStdString();
    EXPECT_EQ(reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt(), 200);
    EXPECT_EQ(reply->header(QNetworkRequest::ContentTypeHeader).toString().toStdString(),
              "application/timestamp-reply");
    const QByteArray body = reply->readAll();
    reply->deleteLater();

    const auto* p = reinterpret_cast<const unsigned char*>(body.constData());
    Ossl<TS_RESP> resp(d2i_TS_RESP(nullptr, &p, body.size()));
    ASSERT_TRUE(resp);
    EXPECT_EQ(ASN1_INTEGER_get(TS_STATUS_INFO_get0_status(TS_RESP_get_status_info(resp.get()))),
              0);  // granted

    // Проверка по корню PKI: подпись, версия, imprint, nonce, подписант (EKU timeStamping).
    Ossl<TS_VERIFY_CTX> vctx(TS_REQ_to_TS_VERIFY_CTX(req.get(), nullptr));
    ASSERT_TRUE(vctx);
    Ossl<X509_STORE> store(X509_STORE_new());
    Ossl<X509> root = loadPem("root.pem");
    ASSERT_EQ(X509_STORE_add_cert(store.get(), root.get()), 1);
    TS_VERIFY_CTX_set0_store(vctx.get(), store.release());
    TS_VERIFY_CTX_add_flags(vctx.get(), TS_VFY_SIGNATURE | TS_VFY_SIGNER);
    EXPECT_EQ(TS_RESP_verify_response(vctx.get(), resp.get()), 1)
        << ot::sign::OsslError::fromQueue("verify").what();
}
