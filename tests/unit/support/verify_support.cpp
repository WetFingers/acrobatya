#include "support/verify_support.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <cstdlib>
#include <regex>
#include <stdexcept>

#include "core/util/paths.h"
#include "support/sign_support.h"
#include "support/test_pdf.h"

namespace fs = std::filesystem;
using nlohmann::json;
using ot::sign::Bytes;
using ot::sign::SignatureInfo;

namespace ot::test {

namespace {

fs::path corpusDir() {
    if (const char* env = std::getenv("OTTISK_CORPUS"); env != nullptr && *env != '\0') {
        return fs::path(ot::paths::fromUtf8(env));
    }
    return fs::path(L"C:\\Tools\\ottisk\\corpus");
}

std::string kindName(ot::sign::SignatureKind k) { return ot::sign::signatureKindName(k); }

bool hasIssueNamed(const SignatureInfo& sig, const std::string& name) {
    return std::any_of(sig.findings.begin(), sig.findings.end(),
                       [&](const ot::sign::Finding& f) { return name == ot::sign::issueName(f.issue); });
}

std::string findingsText(const SignatureInfo& sig) {
    std::string out;
    for (const ot::sign::Finding& f : sig.findings) {
        out += std::string(ot::sign::issueName(f.issue)) + " (" + f.detail + "); ";
    }
    return out;
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

}  // namespace

std::optional<fs::path> dssFile(const std::string& rel) {
    const fs::path p = corpusDir() / L"dss" / L"dss-pades" / L"src" / L"test" / L"resources" / L"validation" /
                       ot::paths::fromUtf8(rel);
    std::error_code ec;
    if (!fs::exists(p, ec)) {
        return std::nullopt;
    }
    return p;
}

const json& verifierExpectations() {
    static const json j = json::parse(
        ot::paths::readFile(sourceDir() / L"tests" / L"fixtures" / L"sig" / L"verifier_expected.json"));
    return j;
}

const SignatureInfo* findSignature(const ot::sign::DocumentVerification& v, const std::string& field) {
    for (const SignatureInfo& s : v.signatures) {
        if (s.field == field) {
            return &s;
        }
    }
    return nullptr;
}

void expectSignature(const SignatureInfo& sig, const json& want, const std::string& what) {
    SCOPED_TRACE(what + " / " + sig.field + ": " + findingsText(sig));
    if (want.contains("structureOk")) {
        EXPECT_EQ(sig.structureOk, want["structureOk"].get<bool>());
    }
    if (want.contains("intact")) {
        EXPECT_EQ(sig.intact, want["intact"].get<bool>());
    }
    if (want.contains("coversWholeFile")) {
        EXPECT_EQ(sig.coversWholeFile, want["coversWholeFile"].get<bool>());
    }
    if (want.contains("kind")) {
        EXPECT_EQ(kindName(sig.kind), want["kind"].get<std::string>());
    }
    if (want.contains("subFilter")) {
        EXPECT_EQ(sig.subFilter, want["subFilter"].get<std::string>());
    }
    if (want.contains("signedAttributes")) {
        EXPECT_EQ(sig.signedAttributes, want["signedAttributes"].get<bool>());
    }
    if (want.contains("verdict")) {
        EXPECT_EQ(std::string(ot::sign::verdictName(sig.verdict)), want["verdict"].get<std::string>());
    }
    for (const auto& name : want.value("issues", json::array())) {
        EXPECT_TRUE(hasIssueNamed(sig, name.get<std::string>())) << "нет " << name.get<std::string>();
    }
    for (const auto& name : want.value("noIssues", json::array())) {
        EXPECT_FALSE(hasIssueNamed(sig, name.get<std::string>())) << "лишнее " << name.get<std::string>();
    }
}

void patchContents(const fs::path& file, int sigIndex, const std::function<void(Bytes&)>& edit) {
    std::string bytes = ot::paths::readFile(file);
    static const std::regex kBr(R"(/ByteRange\s*\[\s*(\d+)\s+(\d+)\s+(\d+)\s+(\d+)\s*\])");
    auto it = std::sregex_iterator(bytes.begin(), bytes.end(), kBr);
    for (int i = 0; i < sigIndex && it != std::sregex_iterator(); ++i) {
        ++it;
    }
    if (it == std::sregex_iterator()) {
        throw std::runtime_error("no /ByteRange #" + std::to_string(sigIndex));
    }
    const std::size_t holeBegin = std::stoull((*it)[2].str()) + 1;  // после «<»
    const std::size_t holeEnd = std::stoull((*it)[3].str()) - 1;    // до «>»
    Bytes der;
    for (std::size_t i = holeBegin; i + 1 < holeEnd; i += 2) {
        der.push_back(static_cast<std::uint8_t>(hexValue(bytes[i]) * 16 + hexValue(bytes[i + 1])));
    }
    const std::size_t used = std::min(der.size(), derLength(der));
    der.resize(used);
    edit(der);
    if (der.size() * 2 > holeEnd - holeBegin) {
        throw std::runtime_error("patched DER does not fit /Contents");
    }
    static const char* kHex = "0123456789abcdef";
    for (std::size_t i = 0; i < (holeEnd - holeBegin) / 2; ++i) {
        const std::uint8_t b = i < der.size() ? der[i] : 0;
        bytes[holeBegin + 2 * i] = kHex[b >> 4];
        bytes[holeBegin + 2 * i + 1] = kHex[b & 15];
    }
    ot::paths::writeFile(file, bytes);
}

void patchBytes(const fs::path& file, const std::string& from, const std::string& to) {
    if (from.size() != to.size()) {
        throw std::invalid_argument("patchBytes: lengths differ");
    }
    std::string bytes = ot::paths::readFile(file);
    const std::size_t at = bytes.find(from);
    if (at == std::string::npos) {
        throw std::runtime_error("patchBytes: not found: " + from);
    }
    bytes.replace(at, from.size(), to);
    ot::paths::writeFile(file, bytes);
}

}  // namespace ot::test
