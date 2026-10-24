#include "core/sign/ossl.h"

#include <openssl/crypto.h>
#include <openssl/err.h>
#include <openssl/opensslv.h>
#include <openssl/provider.h>

#include <cstdio>
#include <mutex>

#include "core/util/paths.h"

static_assert(OPENSSL_VERSION_NUMBER >= 0x30500000L,
              "OpenSSL 3.5+ required: CMS_NO_SIGNING_TIME (design 3.8)");

namespace ot::sign {

OsslError OsslError::fromQueue(std::string_view what) {
    std::string message(what);
    unsigned long first = 0;
    bool any = false;
    const char* file = nullptr;
    int line = 0;
    const char* func = nullptr;
    const char* data = nullptr;
    int flags = 0;
    for (unsigned long e = ERR_get_error_all(&file, &line, &func, &data, &flags); e != 0;
         e = ERR_get_error_all(&file, &line, &func, &data, &flags)) {
        if (first == 0) {
            first = e;
        }
        char buf[256];
        ERR_error_string_n(e, buf, sizeof buf);
        message += any ? "; " : ": ";
        message += buf;
        if (data != nullptr && (flags & ERR_TXT_STRING) != 0 && *data != '\0') {
            message += " (";
            message += data;
            message += ')';
        }
        any = true;
    }
    if (!any) {
        message += ": unknown OpenSSL error";
    }
    return {first, message};
}

int OsslError::reason() const noexcept { return ERR_GET_REASON(code_); }

int OsslError::library() const noexcept { return ERR_GET_LIB(code_); }

namespace ossl {

namespace {

// BIO по частям файла: своё устройство (BIO_meth_*) поверх FILE*.
struct RangeSource {
    RangeSource() = default;
    RangeSource(const RangeSource&) = delete;
    RangeSource& operator=(const RangeSource&) = delete;
    ~RangeSource() {
        if (file != nullptr) {
            std::fclose(file);
        }
    }

    std::FILE* file = nullptr;
    std::vector<ByteRange> ranges;
    std::size_t index = 0;    // текущая часть
    std::int64_t done = 0;    // прочитано из текущей части
    bool positioned = false;  // файл стоит на ranges[index].offset + done
};

int rangeRead(BIO* bio, char* out, std::size_t size, std::size_t* readBytes) {
    *readBytes = 0;
    auto* src = static_cast<RangeSource*>(BIO_get_data(bio));
    if (src == nullptr || src->file == nullptr) {
        return 0;
    }
    while (*readBytes < size && src->index < src->ranges.size()) {
        const ByteRange& r = src->ranges[src->index];
        const std::int64_t left = r.length - src->done;
        if (left <= 0) {
            ++src->index;
            src->done = 0;
            src->positioned = false;
            continue;
        }
        if (!src->positioned) {
            if (_fseeki64(src->file, r.offset + src->done, SEEK_SET) != 0) {
                ERR_raise_data(ERR_LIB_BIO, ERR_R_SYS_LIB, "seek failed");
                return *readBytes > 0 ? 1 : -1;
            }
            src->positioned = true;
        }
        const std::size_t want = std::min<std::size_t>(size - *readBytes, static_cast<std::size_t>(left));
        const std::size_t got = std::fread(out + *readBytes, 1, want, src->file);
        if (got == 0) {
            // Файл стал короче, чем при открытии.
            ERR_raise_data(ERR_LIB_BIO, ERR_R_SYS_LIB, "unexpected end of file");
            return *readBytes > 0 ? 1 : -1;
        }
        *readBytes += got;
        src->done += static_cast<std::int64_t>(got);
    }
    return *readBytes > 0 ? 1 : 0;
}

long rangeCtrl(BIO* bio, int cmd, long /*num*/, void* /*ptr*/) {
    auto* src = static_cast<RangeSource*>(BIO_get_data(bio));
    switch (cmd) {
        case BIO_CTRL_EOF:
            if (src == nullptr) {
                return 1;
            }
            for (std::size_t i = src->index; i < src->ranges.size(); ++i) {
                if (src->ranges[i].length - (i == src->index ? src->done : 0) > 0) {
                    return 0;
                }
            }
            return 1;
        case BIO_CTRL_FLUSH:
            return 1;
        case BIO_CTRL_PENDING:
        case BIO_CTRL_WPENDING:
            return 0;
        default:
            return 0;
    }
}

int rangeCreate(BIO* bio) {
    BIO_set_data(bio, nullptr);
    BIO_set_init(bio, 1);
    return 1;
}

int rangeDestroy(BIO* bio) {
    delete static_cast<RangeSource*>(BIO_get_data(bio));
    BIO_set_data(bio, nullptr);
    return 1;
}

BIO_METHOD* rangeMethod() {
    static BIO_METHOD* method = [] {
        BIO_METHOD* m = BIO_meth_new(BIO_get_new_index() | BIO_TYPE_SOURCE_SINK, "ot byte ranges");
        if (m != nullptr) {
            BIO_meth_set_read_ex(m, rangeRead);
            BIO_meth_set_ctrl(m, rangeCtrl);
            BIO_meth_set_create(m, rangeCreate);
            BIO_meth_set_destroy(m, rangeDestroy);
        }
        return m;
    }();
    return method;
}

}  // namespace

unsigned long versionNumber() noexcept { return OpenSSL_version_num(); }

std::string versionText() { return OpenSSL_version(OPENSSL_VERSION); }

bool hasSigningTimeFlag() noexcept {
#ifdef CMS_NO_SIGNING_TIME
    return versionNumber() >= 0x30500000UL;
#else
    return false;
#endif
}

void check(int rc, std::string_view what) {
    if (rc != 1) {
        throw OsslError::fromQueue(what);
    }
}

bool ensureLegacyProvider() {
    static std::once_flag once;
    static bool loaded = false;
    std::call_once(once, [] {
        // retain_fallbacks = 1: явная загрузка не отключает default.
        loaded = OSSL_PROVIDER_try_load(nullptr, "legacy", 1) != nullptr;
        if (!loaded) {
            ERR_clear_error();
        }
    });
    return loaded;
}

Ossl<BIO> memBio(std::span<const std::uint8_t> data) {
    Ossl<BIO> bio(checkPtr(BIO_new(BIO_s_mem()), "BIO_new"));
    if (!data.empty() &&
        BIO_write(bio.get(), data.data(), static_cast<int>(data.size())) != static_cast<int>(data.size())) {
        throw OsslError::fromQueue("BIO_write");
    }
    BIO_set_mem_eof_return(bio.get(), 0);
    return bio;
}

Ossl<BIO> newMemBio() {
    Ossl<BIO> bio(checkPtr(BIO_new(BIO_s_mem()), "BIO_new"));
    BIO_set_mem_eof_return(bio.get(), 0);
    return bio;
}

Bytes bioBytes(BIO* bio) {
    char* data = nullptr;
    const long len = BIO_get_mem_data(bio, &data);
    if (len <= 0 || data == nullptr) {
        return {};
    }
    return {reinterpret_cast<const std::uint8_t*>(data), reinterpret_cast<const std::uint8_t*>(data) + len};
}

std::int64_t rangeLength(std::span<const ByteRange> ranges) {
    std::int64_t total = 0;
    for (const ByteRange& r : ranges) {
        total += r.length;
    }
    return total;
}

Ossl<BIO> openRangeBio(const std::filesystem::path& file, std::span<const ByteRange> ranges) {
    auto src = std::make_unique<RangeSource>();
    if (_wfopen_s(&src->file, file.c_str(), L"rb") != 0 || src->file == nullptr) {
        src->file = nullptr;
        throw std::runtime_error("cannot open " + paths::toUtf8(file));
    }
    if (_fseeki64(src->file, 0, SEEK_END) != 0) {
        throw std::runtime_error("cannot seek " + paths::toUtf8(file));
    }
    const std::int64_t size = _ftelli64(src->file);
    std::int64_t end = 0;
    for (const ByteRange& r : ranges) {
        if (r.offset < 0 || r.length < 0 || r.offset < end || r.offset + r.length > size) {
            throw std::runtime_error("bad byte range [" + std::to_string(r.offset) + ", " +
                                     std::to_string(r.length) + "] for file of " + std::to_string(size) +
                                     " bytes");
        }
        end = r.offset + r.length;
    }
    src->ranges.assign(ranges.begin(), ranges.end());
    BIO_METHOD* method = checkPtr(rangeMethod(), "BIO_meth_new");
    Ossl<BIO> bio(checkPtr(BIO_new(method), "BIO_new"));
    BIO_set_data(bio.get(), src.release());
    return bio;
}

Bytes sha256(std::span<const std::uint8_t> data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    check(EVP_Digest(data.data(), data.size(), md, &len, EVP_sha256(), nullptr), "EVP_Digest");
    return {md, md + len};
}

Bytes sha256(std::string_view data) {
    return sha256(
        std::span<const std::uint8_t>(reinterpret_cast<const std::uint8_t*>(data.data()), data.size()));
}

Bytes digestRanges(const EVP_MD* md, const std::filesystem::path& file, std::span<const ByteRange> ranges) {
    Ossl<BIO> in = openRangeBio(file, ranges);
    Ossl<EVP_MD_CTX> ctx(checkPtr(EVP_MD_CTX_new(), "EVP_MD_CTX_new"));
    check(EVP_DigestInit_ex(ctx.get(), md, nullptr), "EVP_DigestInit_ex");
    std::vector<char> buf(1 << 16);
    for (;;) {
        const int n = BIO_read(in.get(), buf.data(), static_cast<int>(buf.size()));
        if (n < 0) {
            throw OsslError::fromQueue("read " + paths::toUtf8(file));
        }
        if (n == 0) {
            break;
        }
        check(EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<std::size_t>(n)), "EVP_DigestUpdate");
    }
    unsigned char out[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    check(EVP_DigestFinal_ex(ctx.get(), out, &len), "EVP_DigestFinal_ex");
    return {out, out + len};
}

Bytes sha256Ranges(const std::filesystem::path& file, std::span<const ByteRange> ranges) {
    return digestRanges(EVP_sha256(), file, ranges);
}

std::string toHex(std::span<const std::uint8_t> data) {
    static constexpr char kDigits[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(data.size() * 2);
    for (const std::uint8_t b : data) {
        out.push_back(kDigits[b >> 4]);
        out.push_back(kDigits[b & 0x0f]);
    }
    return out;
}

}  // namespace ossl
}  // namespace ot::sign
