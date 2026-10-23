#include "core/util/hash.h"

#include <openssl/evp.h>

#include <fstream>
#include <memory>
#include <stdexcept>
#include <vector>

#include "core/util/paths.h"

namespace ot::hash {

namespace {

std::string toHex(const unsigned char* data, unsigned int len) {
    static constexpr char kDigits[] = "0123456789abcdef";
    std::string out;
    out.reserve(static_cast<std::size_t>(len) * 2);
    for (unsigned int i = 0; i < len; ++i) {
        out.push_back(kDigits[data[i] >> 4]);
        out.push_back(kDigits[data[i] & 0x0f]);
    }
    return out;
}

}  // namespace

std::string sha256Hex(std::string_view bytes) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), md, &len, EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("sha256 failed");
    }
    return toHex(md, len);
}

std::string sha256File(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error("cannot read " + paths::toUtf8(path));
    }
    const std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> ctx(EVP_MD_CTX_new(), &EVP_MD_CTX_free);
    if (!ctx || EVP_DigestInit_ex(ctx.get(), EVP_sha256(), nullptr) != 1) {
        throw std::runtime_error("sha256 init failed");
    }
    std::vector<char> buf(1 << 20);
    while (in) {
        in.read(buf.data(), static_cast<std::streamsize>(buf.size()));
        const std::streamsize got = in.gcount();
        if (got > 0 && EVP_DigestUpdate(ctx.get(), buf.data(), static_cast<std::size_t>(got)) != 1) {
            throw std::runtime_error("sha256 update failed");
        }
    }
    if (in.bad()) {
        throw std::runtime_error("read error: " + paths::toUtf8(path));
    }
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    if (EVP_DigestFinal_ex(ctx.get(), md, &len) != 1) {
        throw std::runtime_error("sha256 final failed");
    }
    return toHex(md, len);
}

}  // namespace ot::hash
