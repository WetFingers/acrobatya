// unit.ossl — OpenSSL в ядре подписей (план 4.1, п. 1; эталон 3.8, 5.4): версия ≥ 3.5 (CMS_NO_SIGNING_TIME —
// только с 3.5.0), ошибки OsslError с очередью ERR, RAII Ossl<T>, BIO по диапазонам файла (/ByteRange) и в
// памяти.
#include <gtest/gtest.h>

#include <openssl/err.h>
#include <openssl/evp.h>
#include <openssl/opensslv.h>
#include <openssl/x509.h>

#include <string>

#include "core/sign/ossl.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

namespace {

using ot::sign::ByteRange;
using ot::sign::Bytes;

std::string readAll(BIO* bio, int chunk = 3) {
    std::string out;
    std::string buf(static_cast<std::size_t>(chunk), '\0');
    for (;;) {
        const int n = BIO_read(bio, buf.data(), chunk);
        if (n <= 0) {
            break;
        }
        out.append(buf.data(), static_cast<std::size_t>(n));
    }
    return out;
}

std::string sha256(std::string_view data) {
    unsigned char md[EVP_MAX_MD_SIZE];
    unsigned int len = 0;
    EVP_Digest(data.data(), data.size(), md, &len, EVP_sha256(), nullptr);
    return {reinterpret_cast<const char*>(md), len};
}

}  // namespace

TEST(ossl, version) {
    static_assert(OPENSSL_VERSION_NUMBER >= 0x30500000L, "OpenSSL 3.5+");
    EXPECT_GE(OPENSSL_VERSION_NUMBER, 0x30500000L);
    EXPECT_GE(ot::sign::ossl::versionNumber(), 0x30500000UL);
    EXPECT_NE(ot::sign::ossl::versionText().find("OpenSSL 3."), std::string::npos)
        << ot::sign::ossl::versionText();
    EXPECT_TRUE(ot::sign::ossl::hasSigningTimeFlag());
}

TEST(ossl, error_carries_queue_and_clears_it) {
    ERR_clear_error();
    const unsigned char garbage[] = {0x30, 0x03, 0x02, 0x01};
    const unsigned char* p = garbage;
    X509* x = d2i_X509(nullptr, &p, sizeof garbage);
    ASSERT_EQ(x, nullptr);
    const ot::sign::OsslError e = ot::sign::OsslError::fromQueue("d2i_X509");
    EXPECT_NE(e.code(), 0UL);
    const std::string what = e.what();
    EXPECT_EQ(what.rfind("d2i_X509: ", 0), 0U) << what;
    EXPECT_GT(what.size(), std::string("d2i_X509: ").size());
    EXPECT_EQ(ERR_peek_error(), 0UL);  // очередь разобрана
    // Пустая очередь — код 0 и пометка.
    const ot::sign::OsslError empty = ot::sign::OsslError::fromQueue("nothing");
    EXPECT_EQ(empty.code(), 0UL);
    EXPECT_STREQ(empty.what(), "nothing: unknown OpenSSL error");
}

TEST(ossl, check_throws_on_failure) {
    EXPECT_NO_THROW(ot::sign::ossl::check(1, "ok"));
    EXPECT_THROW(ot::sign::ossl::check(0, "zero"), ot::sign::OsslError);
    EXPECT_THROW(ot::sign::ossl::check(-1, "negative"), ot::sign::OsslError);
    int value = 5;
    EXPECT_EQ(ot::sign::ossl::checkPtr(&value, "ptr"), &value);
    EXPECT_THROW(ot::sign::ossl::checkPtr(static_cast<int*>(nullptr), "null"), ot::sign::OsslError);
}

TEST(ossl, raii_owns_objects) {
    ot::sign::Ossl<EVP_MD_CTX> ctx(EVP_MD_CTX_new());
    ASSERT_TRUE(ctx);
    ot::sign::Ossl<X509> cert(X509_new());
    ASSERT_TRUE(cert);
    ot::sign::Ossl<STACK_OF(X509)> stack(sk_X509_new_null());
    ASSERT_TRUE(stack);
    ASSERT_GT(sk_X509_push(stack.get(), X509_new()), 0);  // pop_free освобождает и элементы
    ot::sign::Ossl<BIO> bio(BIO_new(BIO_s_mem()));
    ASSERT_TRUE(bio);
}

TEST(ossl, memory_bio_roundtrip) {
    const Bytes data = {1, 2, 3, 250, 0, 7};
    auto in = ot::sign::ossl::memBio(data);
    EXPECT_EQ(readAll(in.get(), 4), std::string(data.begin(), data.end()));
    auto out = ot::sign::ossl::newMemBio();
    ASSERT_EQ(BIO_write(out.get(), "abc", 3), 3);
    EXPECT_EQ(ot::sign::ossl::bioBytes(out.get()), (Bytes{'a', 'b', 'c'}));
    // Пустой буфер — пустой BIO, не ошибка.
    auto none = ot::sign::ossl::memBio({});
    EXPECT_EQ(readAll(none.get()), "");
}

TEST(ossl, range_bio_reads_byte_ranges_of_file) {
    ot::test::TempDir dir;
    const std::filesystem::path file = dir.path() / L"подпись.bin";
    std::string content;
    for (int i = 0; i < 70000; ++i) {
        content.push_back(static_cast<char>('a' + i % 26));
    }
    ot::paths::writeFile(file, content);

    // Как /ByteRange [0 a b c]: две части вокруг «дыры» /Contents.
    const std::vector<ByteRange> ranges = {{0, 100}, {60000, 10000}};
    auto bio = ot::sign::ossl::openRangeBio(file, ranges);
    const std::string got = readAll(bio.get(), 4096);
    const std::string expected = content.substr(0, 100) + content.substr(60000, 10000);
    ASSERT_EQ(got.size(), expected.size());
    EXPECT_EQ(got, expected);
    EXPECT_EQ(ot::sign::ossl::rangeLength(ranges), 10100);

    // Мелкими кусками — тот же результат, конец — 0 (EOF), повторное чтение — снова 0.
    auto bio2 = ot::sign::ossl::openRangeBio(file, ranges);
    EXPECT_EQ(sha256(readAll(bio2.get(), 7)), sha256(expected));
    char c = 0;
    EXPECT_LE(BIO_read(bio2.get(), &c, 1), 0);
    EXPECT_TRUE(BIO_eof(bio2.get()));

    // Хэш диапазонов одной функцией — как будет считать подписант и валидатор.
    const std::string md = sha256(expected);
    EXPECT_EQ(ot::sign::ossl::sha256Ranges(file, ranges), Bytes(md.begin(), md.end()));
}

TEST(ossl, range_bio_rejects_bad_ranges) {
    ot::test::TempDir dir;
    const std::filesystem::path file = dir.path() / "small.bin";
    ot::paths::writeFile(file, "0123456789");
    // За концом файла, отрицательные, пересекающиеся и нет файла — ошибка при открытии, а не короткое чтение.
    EXPECT_THROW(ot::sign::ossl::openRangeBio(file, std::vector<ByteRange>{{0, 11}}), std::runtime_error);
    EXPECT_THROW(ot::sign::ossl::openRangeBio(file, std::vector<ByteRange>{{-1, 2}}), std::runtime_error);
    EXPECT_THROW(ot::sign::ossl::openRangeBio(file, std::vector<ByteRange>{{0, 5}, {3, 2}}),
                 std::runtime_error);
    EXPECT_THROW(ot::sign::ossl::openRangeBio(dir.path() / "none.bin", std::vector<ByteRange>{{0, 1}}),
                 std::runtime_error);
    // Пустой список — пустой поток.
    auto empty = ot::sign::ossl::openRangeBio(file, {});
    EXPECT_EQ(readAll(empty.get()), "");
}

TEST(ossl, hex_and_digest_helpers) {
    EXPECT_EQ(ot::sign::ossl::toHex(Bytes{0x00, 0xAB, 0x10}), "00AB10");
    const Bytes d = ot::sign::ossl::sha256(std::string_view("abc"));
    EXPECT_EQ(ot::sign::ossl::toHex(d), "BA7816BF8F01CFEA414140DE5DAE2223B00361A396177A9CB410FF61F20015AD");
}
