#include "core/sign/identity.h"

#include <openssl/bn.h>
#include <openssl/evp.h>
#include <openssl/pkcs12.h>
#include <openssl/pkcs7.h>
#include <openssl/x509v3.h>
#include <spdlog/spdlog.h>
#include <sqlite3.h>

#include <algorithm>
#include <ctime>

#include "core/sign/cng_store.h"
#include "core/sign/x509_info.h"
#include "core/util/paths.h"

namespace ot::sign {

namespace fs = std::filesystem;

namespace {

// ---------------------------------------------------------------- сертификат

void addNameEntry(X509_NAME* name, int nid, const std::string& value) {
    if (value.empty()) {
        return;
    }
    ossl::check(X509_NAME_add_entry_by_NID(name, nid, MBSTRING_UTF8,
                                           reinterpret_cast<const unsigned char*>(value.data()),
                                           static_cast<int>(value.size()), -1, 0),
                "X509_NAME_add_entry_by_NID");
}

void addExtension(X509* cert, int nid, const std::string& value) {
    X509V3_CTX ctx;
    X509V3_set_ctx_nodb(&ctx);
    X509V3_set_ctx(&ctx, cert, cert, nullptr, nullptr, 0);
    X509_EXTENSION* ext = X509V3_EXT_conf_nid(nullptr, &ctx, nid, value.c_str());
    if (ext == nullptr) {
        throw OsslError::fromQueue("X509V3_EXT_conf_nid");
    }
    const int rc = X509_add_ext(cert, ext, -1);
    X509_EXTENSION_free(ext);
    ossl::check(rc, "X509_add_ext");
}

std::string digestHex(X509* cert, const EVP_MD* md) {
    unsigned char buf[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    ossl::check(X509_digest(cert, md, buf, &len), "X509_digest");
    return ossl::toHex(std::span<const std::uint8_t>(buf, len));
}

std::string toStdString(std::string_view s) { return std::string(s.data(), s.size()); }

// ---------------------------------------------------------------- FDF

void skipSpace(std::string_view s, std::size_t& i) {
    while (i < s.size() &&
           (s[i] == ' ' || s[i] == '\t' || s[i] == '\r' || s[i] == '\n' || s[i] == '\f' || s[i] == '\0')) {
        ++i;
    }
}

int hexValue(char c) {
    if (c >= '0' && c <= '9') {
        return c - '0';
    }
    if (c >= 'a' && c <= 'f') {
        return c - 'a' + 10;
    }
    if (c >= 'A' && c <= 'F') {
        return c - 'A' + 10;
    }
    return -1;
}

// <hex> с позиции после «<»; false — нет закрывающей «>».
bool readHexString(std::string_view s, std::size_t& i, Bytes& out) {
    int high = -1;
    while (i < s.size() && s[i] != '>') {
        const int v = hexValue(s[i++]);
        if (v < 0) {
            continue;  // пробелы внутри
        }
        if (high < 0) {
            high = v;
        } else {
            out.push_back(static_cast<std::uint8_t>(high * 16 + v));
            high = -1;
        }
    }
    if (i >= s.size()) {
        return false;
    }
    if (high >= 0) {
        out.push_back(static_cast<std::uint8_t>(high * 16));  // нечётная длина — последняя цифра и 0
    }
    ++i;
    return true;
}

// (литерал) с позиции после «(»: вложенные скобки, экранирование \n \r \t \b \f \( \) \\ \ddd.
bool readLiteralString(std::string_view s, std::size_t& i, Bytes& out) {
    int depth = 1;
    while (i < s.size()) {
        const char c = s[i++];
        if (c == '\\' && i < s.size()) {
            const char e = s[i++];
            switch (e) {
                case 'n':
                    out.push_back('\n');
                    break;
                case 'r':
                    out.push_back('\r');
                    break;
                case 't':
                    out.push_back('\t');
                    break;
                case 'b':
                    out.push_back('\b');
                    break;
                case 'f':
                    out.push_back('\f');
                    break;
                case '\r':
                    if (i < s.size() && s[i] == '\n') {
                        ++i;
                    }
                    break;
                case '\n':
                    break;
                default:
                    if (e >= '0' && e <= '7') {
                        int v = e - '0';
                        for (int k = 0; k < 2 && i < s.size() && s[i] >= '0' && s[i] <= '7'; ++k) {
                            v = v * 8 + (s[i++] - '0');
                        }
                        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
                    } else {
                        out.push_back(static_cast<std::uint8_t>(e));
                    }
            }
            continue;
        }
        if (c == '(') {
            ++depth;
        } else if (c == ')' && --depth == 0) {
            return true;
        }
        out.push_back(static_cast<std::uint8_t>(c));
    }
    return false;
}

// ---------------------------------------------------------------- sqlite

class Stmt {
public:
    Stmt(sqlite3* db, const char* sql) {
        if (db == nullptr || sqlite3_prepare_v2(db, sql, -1, &st_, nullptr) != SQLITE_OK) {
            const std::string why = db != nullptr ? sqlite3_errmsg(db) : "no database";
            st_ = nullptr;
            throw IdentityError(IdentityStatus::IoError, std::string("identities: ") + why);
        }
    }
    ~Stmt() { sqlite3_finalize(st_); }
    Stmt(const Stmt&) = delete;
    Stmt& operator=(const Stmt&) = delete;

    Stmt& text(int i, std::string_view v) {
        sqlite3_bind_text(st_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& integer(int i, std::int64_t v) {
        sqlite3_bind_int64(st_, i, v);
        return *this;
    }
    Stmt& blob(int i, std::span<const std::uint8_t> v) {
        sqlite3_bind_blob(st_, i, v.data(), static_cast<int>(v.size()), SQLITE_TRANSIENT);
        return *this;
    }
    Stmt& null(int i) {
        sqlite3_bind_null(st_, i);
        return *this;
    }
    bool row() { return sqlite3_step(st_) == SQLITE_ROW; }
    void run() {
        const int rc = sqlite3_step(st_);
        if (rc != SQLITE_DONE && rc != SQLITE_ROW) {
            throw IdentityError(IdentityStatus::IoError,
                                std::string("identities: ") + sqlite3_errmsg(sqlite3_db_handle(st_)));
        }
    }
    std::string columnText(int i) const {
        const auto* p = reinterpret_cast<const char*>(sqlite3_column_text(st_, i));
        return p != nullptr ? std::string(p, static_cast<std::size_t>(sqlite3_column_bytes(st_, i)))
                            : std::string();
    }
    Bytes columnBlob(int i) const {
        const auto* p = static_cast<const std::uint8_t*>(sqlite3_column_blob(st_, i));
        return p != nullptr ? Bytes(p, p + sqlite3_column_bytes(st_, i)) : Bytes();
    }
    std::int64_t columnInt(int i) const { return sqlite3_column_int64(st_, i); }
    bool columnNull(int i) const { return sqlite3_column_type(st_, i) == SQLITE_NULL; }

private:
    sqlite3_stmt* st_ = nullptr;
};

constexpr const char* kSelect =
    "SELECT id, kind, file, thumbprint, name, cert, is_default, secret IS NOT NULL, timeout FROM identities";

IdentityRecord readRecord(const Stmt& st) {
    IdentityRecord r;
    r.id = st.columnText(0);
    r.kind = st.columnText(1) == "windows" ? IdentityKind::WindowsStore : IdentityKind::Pkcs12File;
    const std::string file = st.columnText(2);
    if (!file.empty()) {
        r.file = paths::fromUtf8(file);
    }
    r.thumbprint = st.columnText(3);
    r.name = st.columnText(4);
    const Bytes der = st.columnBlob(5);
    r.isDefault = st.columnInt(6) != 0;
    r.rememberPassword = st.columnInt(7) != 0;
    r.passwordTimeout = static_cast<int>(st.columnInt(8));
    if (!der.empty()) {
        try {
            const Ossl<X509> cert = loadCertificate(der);
            const CertSummary s = summarize(cert.get());
            r.subject = s.subject;
            r.issuer = s.issuer;
            r.notAfter = s.notAfter;
        } catch (const std::exception&) {  // NOLINT(bugprone-empty-catch): запись без сведений
        }
    }
    return r;
}

IdentityError fromPkcs12(const Pkcs12Error& e) {
    switch (e.status()) {
        case Pkcs12Status::BadPassword:
            return {IdentityStatus::BadPassword, e.what()};
        default:
            return {IdentityStatus::Damaged, e.what()};
    }
}

}  // namespace

// ---------------------------------------------------------------- самоподписанное и PKCS#12

SigningKey createSelfSigned(const SelfSignedOptions& options) {
    if (options.commonName.empty()) {
        throw std::invalid_argument("self-signed identity needs a name");
    }
    Ossl<EVP_PKEY> key(options.algorithm == KeyAlgorithm::Rsa3072
                           ? EVP_PKEY_Q_keygen(nullptr, nullptr, "RSA", static_cast<size_t>(3072))
                           : EVP_PKEY_Q_keygen(nullptr, nullptr, "EC", "P-256"));
    if (!key) {
        throw OsslError::fromQueue("EVP_PKEY_Q_keygen");
    }
    Ossl<X509> cert(ossl::checkPtr(X509_new(), "X509_new"));
    ossl::check(X509_set_version(cert.get(), X509_VERSION_3), "X509_set_version");
    Ossl<BIGNUM> serial(ossl::checkPtr(BN_new(), "BN_new"));
    ossl::check(BN_rand(serial.get(), 127, BN_RAND_TOP_ANY, BN_RAND_BOTTOM_ANY), "BN_rand");
    ossl::checkPtr(BN_to_ASN1_INTEGER(serial.get(), X509_get_serialNumber(cert.get())), "BN_to_ASN1_INTEGER");
    std::time_t now = std::time(nullptr);
    ossl::checkPtr(X509_time_adj_ex(X509_getm_notBefore(cert.get()), 0, 0, &now), "X509_time_adj_ex");
    const int days = options.years * 365 + options.years / 4;
    ossl::checkPtr(X509_time_adj_ex(X509_getm_notAfter(cert.get()), days, 0, &now), "X509_time_adj_ex");

    Ossl<X509_NAME> name(ossl::checkPtr(X509_NAME_new(), "X509_NAME_new"));
    addNameEntry(name.get(), NID_countryName, options.country);
    addNameEntry(name.get(), NID_organizationName, options.organization);
    addNameEntry(name.get(), NID_organizationalUnitName, options.organizationalUnit);
    addNameEntry(name.get(), NID_commonName, options.commonName);
    addNameEntry(name.get(), NID_pkcs9_emailAddress, options.email);
    ossl::check(X509_set_subject_name(cert.get(), name.get()), "X509_set_subject_name");
    ossl::check(X509_set_issuer_name(cert.get(), name.get()), "X509_set_issuer_name");
    ossl::check(X509_set_pubkey(cert.get(), key.get()), "X509_set_pubkey");

    addExtension(cert.get(), NID_basic_constraints, "critical,CA:FALSE");
    addExtension(cert.get(), NID_key_usage, "critical,digitalSignature,nonRepudiation");
    // emailProtection, documentSigning (RFC 9336), Adobe Authentic Documents Trust.
    addExtension(cert.get(), NID_ext_key_usage, "emailProtection,1.3.6.1.5.5.7.3.36,1.2.840.113583.1.1.5");
    addExtension(cert.get(), NID_subject_key_identifier, "hash");
    addExtension(cert.get(), NID_authority_key_identifier, "keyid:always");
    if (!options.email.empty()) {
        addExtension(cert.get(), NID_subject_alt_name, "email:" + options.email);
    }
    if (X509_sign(cert.get(), key.get(), EVP_sha256()) <= 0) {
        throw OsslError::fromQueue("X509_sign");
    }

    SigningKey out;
    out.key = std::move(key);
    out.cert = std::move(cert);
    out.chain.reset(ossl::checkPtr(sk_X509_new_null(), "sk_X509_new_null"));
    out.friendlyName = options.commonName;
    return out;
}

Bytes exportPkcs12(const SigningKey& key, std::string_view passwordUtf8) {
    const std::string password = toStdString(passwordUtf8);
    const std::string friendly =
        !key.friendlyName.empty() ? key.friendlyName : commonName(X509_get_subject_name(key.cert.get()));
    // Без MAC при создании — MAC SHA-256 отдельно (PKCS12_set_mac).
    Ossl<PKCS12> p12(PKCS12_create(password.c_str(), friendly.empty() ? nullptr : friendly.c_str(),
                                   key.key.get(), key.cert.get(), key.chain.get(), NID_aes_256_cbc,
                                   NID_aes_256_cbc, PKCS12_DEFAULT_ITER, -1, 0));
    if (!p12) {
        throw OsslError::fromQueue("PKCS12_create");
    }
    ossl::check(
        PKCS12_set_mac(p12.get(), password.c_str(), -1, nullptr, 0, PKCS12_DEFAULT_ITER, EVP_sha256()),
        "PKCS12_set_mac");
    return ossl::toDer(p12.get(), i2d_PKCS12, "i2d_PKCS12");
}

std::string certificateThumbprint(X509* cert) { return digestHex(cert, EVP_sha1()); }

std::string certificateId(X509* cert) { return digestHex(cert, EVP_sha256()); }

Bytes toDerCertificate(X509* cert) { return ossl::toDer(cert, i2d_X509, "i2d_X509"); }

WindowsImport importToWindows(std::span<const std::uint8_t> pfx, std::string_view passwordUtf8,
                              bool persist) {
    WindowsImport result;
    try {
        const cng::CertStore imported = cng::CertStore::importPfx(
            pfx, passwordUtf8, persist ? cng::KeyStorage::User : cng::KeyStorage::Memory);
        cng::CertContext found;
        for (const cng::CertEntry& e : imported.list()) {
            if (e.hasPrivateKey) {
                found = e.context;
                result.thumbprint = e.thumbprint;
                break;
            }
        }
        if (!found) {
            result.error = "no certificate with a private key in the PKCS#12";
            return result;
        }
        result.hasKey = true;
        if (persist) {
            cng::CertStore my = cng::CertStore::openPersonal(true);
            (void)my.add(found);
        }
        result.ok = true;
    } catch (const cng::StoreError& e) {
        result.error = e.what();
    }
    return result;
}

// ---------------------------------------------------------------- экспорт сертификата

Bytes exportCertificate(X509* cert, CertFormat format) {
    switch (format) {
        case CertFormat::Cer:
            return toDerCertificate(cert);
        case CertFormat::P7c: {
            Ossl<PKCS7> p7(ossl::checkPtr(PKCS7_new(), "PKCS7_new"));
            ossl::check(PKCS7_set_type(p7.get(), NID_pkcs7_signed), "PKCS7_set_type");
            ASN1_OBJECT_free(p7->d.sign->contents->type);
            p7->d.sign->contents->type = OBJ_nid2obj(NID_pkcs7_data);
            ossl::check(PKCS7_add_certificate(p7.get(), cert), "PKCS7_add_certificate");
            return ossl::toDer(p7.get(), i2d_PKCS7, "i2d_PKCS7");
        }
        case CertFormat::Fdf: {
            const Bytes der = toDerCertificate(cert);
            const std::string text = "%FDF-1.2\n%\xE2\xE3\xCF\xD3\n1 0 obj\n<</FDF<</Certs[<" +
                                     ossl::toHex(der) + ">]>>>>\nendobj\ntrailer\n<</Root 1 0 R>>\n%%EOF\n";
            return Bytes(text.begin(), text.end());
        }
    }
    return {};
}

std::vector<Bytes> readFdfCertificates(std::string_view fdf) {
    std::vector<Bytes> out;
    if (fdf.rfind("%FDF-", 0) != 0) {
        return out;
    }
    std::size_t i = fdf.find("/Certs");
    if (i == std::string_view::npos) {
        return out;
    }
    i += 6;
    skipSpace(fdf, i);
    if (i >= fdf.size() || fdf[i] != '[') {
        return out;
    }
    ++i;
    for (;;) {
        skipSpace(fdf, i);
        if (i >= fdf.size() || fdf[i] == ']') {
            break;
        }
        Bytes item;
        bool ok = false;
        if (fdf[i] == '<') {
            ++i;
            ok = readHexString(fdf, i, item);
        } else if (fdf[i] == '(') {
            ++i;
            ok = readLiteralString(fdf, i, item);
        }
        if (!ok) {
            break;
        }
        out.push_back(std::move(item));
    }
    return out;
}

// ---------------------------------------------------------------- IdentityStore

IdentityStore::IdentityStore(sqlite3* db, fs::path idsDir, SecretProtector* protector)
    : db_(db), idsDir_(std::move(idsDir)), protector_(protector) {}

void IdentityStore::createSchema(sqlite3* db) {
    char* message = nullptr;
    const int rc = sqlite3_exec(
        db,
        "CREATE TABLE IF NOT EXISTS identities(id TEXT PRIMARY KEY, kind TEXT NOT NULL, file TEXT NOT NULL "
        "DEFAULT '', thumbprint TEXT NOT NULL DEFAULT '', name TEXT NOT NULL DEFAULT '', cert BLOB, "
        "is_default "
        "INTEGER NOT NULL DEFAULT 0, secret BLOB, timeout INTEGER NOT NULL DEFAULT -1, added_at INTEGER NOT "
        "NULL DEFAULT 0)",
        nullptr, nullptr, &message);
    const std::string why = message != nullptr ? message : "";
    sqlite3_free(message);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("identities schema: " + why);
    }
}

sqlite3* IdentityStore::db() const {
    if (db_ == nullptr) {
        throw IdentityError(IdentityStatus::IoError, "identities: state.sqlite is not open");
    }
    return db_;
}

std::vector<IdentityRecord> IdentityStore::list() const {
    std::vector<IdentityRecord> out;
    if (db_ == nullptr) {
        return out;
    }
    Stmt st(db_, (std::string(kSelect) + " ORDER BY added_at, rowid").c_str());
    while (st.row()) {
        out.push_back(readRecord(st));
    }
    return out;
}

std::optional<IdentityRecord> IdentityStore::find(std::string_view id) const {
    if (db_ == nullptr) {
        return std::nullopt;
    }
    Stmt st(db_, (std::string(kSelect) + " WHERE id = ?1").c_str());
    st.text(1, id);
    if (st.row()) {
        return readRecord(st);
    }
    return std::nullopt;
}

std::optional<IdentityRecord> IdentityStore::defaultIdentity() const {
    for (IdentityRecord& r : list()) {
        if (r.isDefault) {
            return std::move(r);
        }
    }
    return std::nullopt;
}

bool IdentityStore::setDefault(std::string_view id) {
    if (!find(id)) {
        return false;
    }
    Stmt(db(), "UPDATE identities SET is_default = CASE WHEN id = ?1 THEN 1 ELSE 0 END").text(1, id).run();
    return true;
}

IdentityRecord IdentityStore::insert(const SigningKey& key, IdentityKind kind, const Bytes& pfx,
                                     std::string_view password, bool rememberPassword) {
    sqlite3* database = db();
    const std::string id = certificateId(key.cert.get());
    if (find(id)) {
        throw IdentityError(IdentityStatus::Exists, "identity already added");
    }
    fs::path file;
    if (kind == IdentityKind::Pkcs12File) {
        std::error_code ec;
        fs::create_directories(idsDir_, ec);
        file = idsDir_ / (id.substr(0, 16) + ".pfx");
        try {
            paths::writeFile(file, std::string_view(reinterpret_cast<const char*>(pfx.data()), pfx.size()));
        } catch (const std::exception& e) {
            throw IdentityError(IdentityStatus::IoError, e.what());
        }
    }
    const bool first = list().empty();
    const Bytes der = toDerCertificate(key.cert.get());
    std::string name = commonName(X509_get_subject_name(key.cert.get()));
    if (name.empty()) {
        name = nameToString(X509_get_subject_name(key.cert.get()));
    }
    Stmt(database,
         "INSERT INTO identities(id, kind, file, thumbprint, name, cert, is_default, timeout, added_at) "
         "VALUES(?1, ?2, ?3, ?4, ?5, ?6, ?7, ?8, ?9)")
        .text(1, id)
        .text(2, kind == IdentityKind::WindowsStore ? "windows" : "pfx")
        .text(3, file.empty() ? std::string() : paths::toUtf8(file))
        .text(4, certificateThumbprint(key.cert.get()))
        .text(5, name)
        .blob(6, der)
        .integer(7, first ? 1 : 0)
        .integer(8, kUntilExit)
        .integer(9, static_cast<std::int64_t>(std::time(nullptr)))
        .run();
    if (rememberPassword && kind == IdentityKind::Pkcs12File) {
        storeSecret(id, password);
    }
    spdlog::info("identities: added {} ({})", id.substr(0, 16),
                 kind == IdentityKind::WindowsStore ? "windows" : "pfx");
    return *find(id);
}

IdentityRecord IdentityStore::addPkcs12(const fs::path& file, std::string_view password,
                                        bool rememberPassword) {
    std::string bytes;
    try {
        bytes = paths::readFile(file);
    } catch (const std::exception& e) {
        throw IdentityError(IdentityStatus::FileMissing, e.what());
    }
    const Bytes pfx(bytes.begin(), bytes.end());
    SigningKey key;
    try {
        key = loadPkcs12(pfx, password);
    } catch (const Pkcs12Error& e) {
        throw fromPkcs12(e);
    }
    return insert(key, IdentityKind::Pkcs12File, pfx, password, rememberPassword);
}

IdentityRecord IdentityStore::createSelfSigned(const SelfSignedOptions& options, std::string_view password,
                                               bool rememberPassword) {
    const SigningKey key = sign::createSelfSigned(options);
    return insert(key, IdentityKind::Pkcs12File, exportPkcs12(key, password), password, rememberPassword);
}

IdentityRecord IdentityStore::addWindows(X509* cert) {
    SigningKey key;
    X509_up_ref(cert);
    key.cert.reset(cert);
    return insert(key, IdentityKind::WindowsStore, {}, {}, false);
}

bool IdentityStore::remove(std::string_view id, bool deleteFile) {
    const std::optional<IdentityRecord> rec = find(id);
    if (!rec) {
        return false;
    }
    Stmt(db(), "DELETE FROM identities WHERE id = ?1").text(1, id).run();
    cache_.erase(rec->id);
    if (deleteFile && rec->kind == IdentityKind::Pkcs12File && rec->file.parent_path() == idsDir_) {
        std::error_code ec;
        fs::remove(rec->file, ec);
    }
    if (rec->isDefault) {
        const std::vector<IdentityRecord> rest = list();
        if (!rest.empty()) {
            (void)setDefault(rest.front().id);
        }
    }
    return true;
}

SigningKey IdentityStore::load(const IdentityRecord& rec, std::string_view password) const {
    std::error_code ec;
    if (!fs::exists(rec.file, ec)) {
        throw IdentityError(IdentityStatus::FileMissing,
                            "identity file is missing: " + paths::toUtf8(rec.file));
    }
    const std::string bytes = paths::readFile(rec.file);
    try {
        return loadPkcs12(Bytes(bytes.begin(), bytes.end()), password);
    } catch (const Pkcs12Error& e) {
        throw fromPkcs12(e);
    }
}

void IdentityStore::cache(const IdentityRecord& rec, std::string_view password) {
    if (rec.passwordTimeout == 0) {
        cache_.erase(rec.id);
        return;
    }
    Cached c;
    c.password = toStdString(password);
    if (rec.passwordTimeout > 0) {
        c.expires = now() + std::chrono::seconds(rec.passwordTimeout);
    }
    cache_[rec.id] = std::move(c);
}

void IdentityStore::storeSecret(std::string_view id, std::optional<std::string_view> password) {
    if (!password) {
        Stmt(db(), "UPDATE identities SET secret = NULL WHERE id = ?1").text(1, id).run();
        return;
    }
    if (protector_ == nullptr) {
        throw IdentityError(IdentityStatus::IoError, "no password protector");
    }
    const Bytes blob = protector_->protect(std::span<const std::uint8_t>(
        reinterpret_cast<const std::uint8_t*>(password->data()), password->size()));
    Stmt(db(), "UPDATE identities SET secret = ?2 WHERE id = ?1").text(1, id).blob(2, blob).run();
}

std::optional<std::string> IdentityStore::secret(std::string_view id) const {
    if (protector_ == nullptr || db_ == nullptr) {
        return std::nullopt;
    }
    Stmt st(db_, "SELECT secret FROM identities WHERE id = ?1");
    st.text(1, id);
    if (!st.row() || st.columnNull(0)) {
        return std::nullopt;
    }
    const std::optional<Bytes> plain = protector_->unprotect(st.columnBlob(0));
    if (!plain) {
        return std::nullopt;
    }
    return std::string(plain->begin(), plain->end());
}

void IdentityStore::changePassword(std::string_view id, std::string_view oldPassword,
                                   std::string_view newPassword) {
    const std::optional<IdentityRecord> rec = find(id);
    if (!rec) {
        throw IdentityError(IdentityStatus::NotFound, "no such identity");
    }
    if (rec->kind == IdentityKind::WindowsStore) {
        throw IdentityError(IdentityStatus::WindowsStore, "Windows store identities have no file password");
    }
    const SigningKey key = load(*rec, oldPassword);
    const Bytes pfx = exportPkcs12(key, newPassword);
    const fs::path temp = rec->file.parent_path() / (rec->file.filename().wstring() + L".new");
    try {
        paths::writeFile(temp, std::string_view(reinterpret_cast<const char*>(pfx.data()), pfx.size()));
        fs::rename(temp, rec->file);
    } catch (const std::exception& e) {
        std::error_code ec;
        fs::remove(temp, ec);
        throw IdentityError(IdentityStatus::IoError, e.what());
    }
    if (rec->rememberPassword) {
        storeSecret(id, newPassword);
    }
    if (cache_.contains(rec->id)) {
        cache(*rec, newPassword);
    }
}

void IdentityStore::setRememberedPassword(std::string_view id, std::optional<std::string> password) {
    const std::optional<IdentityRecord> rec = find(id);
    if (!rec) {
        throw IdentityError(IdentityStatus::NotFound, "no such identity");
    }
    if (password) {
        (void)load(*rec, *password);  // неверный — BadPassword
        storeSecret(id, std::string_view(*password));
    } else {
        storeSecret(id, std::nullopt);
    }
}

void IdentityStore::setPasswordTimeout(std::string_view id, int seconds) {
    Stmt(db(), "UPDATE identities SET timeout = ?2 WHERE id = ?1").text(1, id).integer(2, seconds).run();
    if (seconds == 0) {
        const auto it = cache_.find(id);
        if (it != cache_.end()) {
            cache_.erase(it);
        }
    }
}

void IdentityStore::forgetCachedPasswords() { cache_.clear(); }

SigningKey IdentityStore::open(std::string_view id, std::optional<std::string> password) {
    const std::optional<IdentityRecord> rec = find(id);
    if (!rec) {
        throw IdentityError(IdentityStatus::NotFound, "no such identity");
    }
    if (rec->kind == IdentityKind::WindowsStore) {
        throw IdentityError(IdentityStatus::WindowsStore, "Windows store identities sign through CNG");
    }
    if (password) {
        SigningKey key = load(*rec, *password);
        cache(*rec, *password);
        return key;
    }
    const auto it = cache_.find(id);
    if (it != cache_.end()) {
        if (rec->passwordTimeout != 0 && (!it->second.expires || now() < *it->second.expires)) {
            try {
                return load(*rec, it->second.password);
            } catch (const IdentityError& e) {
                if (e.status() != IdentityStatus::BadPassword) {
                    throw;
                }
            }
        }
        cache_.erase(rec->id);
    }
    if (rec->rememberPassword) {
        if (const std::optional<std::string> remembered = secret(id)) {
            try {
                return load(*rec, *remembered);
            } catch (const IdentityError& e) {
                if (e.status() != IdentityStatus::BadPassword) {
                    throw;
                }
            }
        }
    }
    throw IdentityError(IdentityStatus::NeedPassword, "identity password is required");
}

Bytes IdentityStore::certificateDer(std::string_view id) const {
    Stmt st(db(), "SELECT cert FROM identities WHERE id = ?1");
    st.text(1, id);
    if (!st.row()) {
        throw IdentityError(IdentityStatus::NotFound, "no such identity");
    }
    return st.columnBlob(0);
}

void IdentityStore::exportCertificate(std::string_view id, const fs::path& target, CertFormat format) const {
    const Bytes der = certificateDer(id);
    const Ossl<X509> cert = loadCertificate(der);
    const Bytes out = sign::exportCertificate(cert.get(), format);
    try {
        paths::writeFile(target, std::string_view(reinterpret_cast<const char*>(out.data()), out.size()));
    } catch (const std::exception& e) {
        throw IdentityError(IdentityStatus::IoError, e.what());
    }
}

}  // namespace ot::sign
