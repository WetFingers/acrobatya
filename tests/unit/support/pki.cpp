#include "support/pki.h"

#include <QProcess>
#include <QProcessEnvironment>
#include <QString>
#include <QStringList>

#include <openssl/pem.h>
#include <openssl/ts.h>

#include <stdexcept>

#include "core/util/paths.h"
#include "support/qt_app.h"
#include "support/test_pdf.h"

namespace ot::test {

std::filesystem::path pkiDir() { return sourceDir() / "tests" / "fixtures" / "pki"; }

std::string readPki(const char* name) { return paths::readFile(pkiDir() / name); }

ot::sign::Ossl<X509> pkiCert(const char* name) {
    const std::string pem = readPki(name);
    ot::sign::Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    return ot::sign::Ossl<X509>(PEM_read_bio_X509(bio.get(), nullptr, nullptr, nullptr));
}

ot::sign::Ossl<EVP_PKEY> pkiKey(const char* name) {
    const std::string pem = readPki(name);
    ot::sign::Ossl<BIO> bio(BIO_new_mem_buf(pem.data(), static_cast<int>(pem.size())));
    return ot::sign::Ossl<EVP_PKEY>(PEM_read_bio_PrivateKey(bio.get(), nullptr, nullptr, nullptr));
}

ot::sign::Ossl<X509_STORE> pkiRootStore() {
    ot::sign::Ossl<X509_STORE> store(X509_STORE_new());
    const auto root = pkiCert("root.pem");
    if (!store || !root || X509_STORE_add_cert(store.get(), root.get()) != 1) {
        throw std::runtime_error("cannot build PKI root store");
    }
    return store;
}

std::vector<std::uint8_t> localTsaReply(std::span<const std::uint8_t> query) {
    ot::sign::Ossl<TS_RESP_CTX> ctx(TS_RESP_CTX_new());
    const auto cert = pkiCert("tsa.pem");
    const auto key = pkiKey("tsa.key");
    ot::sign::Ossl<STACK_OF(X509)> certs(sk_X509_new_null());
    sk_X509_push(certs.get(), pkiCert("inter.pem").release());
    sk_X509_push(certs.get(), pkiCert("root.pem").release());
    ot::sign::Ossl<ASN1_OBJECT> policy(OBJ_txt2obj("1.2.3.4.1", 1));
    if (!ctx || TS_RESP_CTX_set_signer_cert(ctx.get(), cert.get()) != 1 ||
        TS_RESP_CTX_set_signer_key(ctx.get(), key.get()) != 1 ||
        TS_RESP_CTX_set_certs(ctx.get(), certs.get()) != 1 ||
        TS_RESP_CTX_set_def_policy(ctx.get(), policy.get()) != 1 ||
        TS_RESP_CTX_set_signer_digest(ctx.get(), EVP_sha256()) != 1 ||
        TS_RESP_CTX_set_ess_cert_id_digest(ctx.get(), EVP_sha256()) != 1 ||
        TS_RESP_CTX_set_accuracy(ctx.get(), 1, 0, 0) != 1) {
        throw ot::sign::OsslError::fromQueue("TS_RESP_CTX");
    }
    for (const EVP_MD* md : {EVP_sha1(), EVP_sha256(), EVP_sha384(), EVP_sha512()}) {
        TS_RESP_CTX_add_md(ctx.get(), md);
    }
    TS_RESP_CTX_add_flags(ctx.get(), TS_TSA_NAME | TS_ORDERING);
    auto in = ot::sign::ossl::memBio(query);
    ot::sign::Ossl<TS_RESP> resp(TS_RESP_create_response(ctx.get(), in.get()));
    if (!resp) {
        throw ot::sign::OsslError::fromQueue("TS_RESP_create_response");
    }
    return ot::sign::ossl::toDer(resp.get(), i2d_TS_RESP, "i2d_TS_RESP");
}

std::optional<std::filesystem::path> opensslExe() {
    std::vector<std::filesystem::path> candidates;
    const QString env = qEnvironmentVariable("OTTISK_OPENSSL");
    if (!env.isEmpty()) {
        candidates.emplace_back(env.toStdWString());
    }
    candidates.emplace_back(L"C:\\Program Files\\Git\\mingw64\\bin\\openssl.exe");
    candidates.emplace_back(L"C:\\Program Files\\Git\\usr\\bin\\openssl.exe");
    for (const auto& c : candidates) {
        std::error_code ec;
        if (std::filesystem::is_regular_file(c, ec)) {
            return c;
        }
    }
    return std::nullopt;
}

ProcessResult runProcess(const std::filesystem::path& exe,
                         const std::vector<std::filesystem::path::string_type>& args, int timeoutMs) {
    qtApp();
    QProcess p;
    QStringList list;
    for (const auto& a : args) {
        list << QString::fromStdWString(a);
    }
    p.setProgram(QString::fromStdWString(exe.wstring()));
    p.setArguments(list);
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.setStandardInputFile(QProcess::nullDevice());
    // openssl.exe на Windows берёт аргументы (и пути с кириллицей) как UTF-8 только с OPENSSL_WIN32_UTF8.
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("OPENSSL_WIN32_UTF8"), QStringLiteral("1"));
    p.setProcessEnvironment(env);
    ProcessResult r;
    p.start();
    if (!p.waitForStarted(timeoutMs)) {
        r.output = p.errorString().toStdString();
        return r;
    }
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(3000);
        r.output = "timeout: " + p.readAll().toStdString();
        return r;
    }
    r.output = p.readAll().toStdString();
    r.exitCode = p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1;
    return r;
}

}  // namespace ot::test
