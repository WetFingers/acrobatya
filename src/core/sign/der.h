// Свой разбор DER (и BER с неопределённой длиной у составных — встречается в чужих CMS) без OpenSSL:
// структура CMS SignedData, атрибуты CAdES, ESS, TSTInfo (план 4.1, п. 3: подписанные атрибуты проверяются
// независимым от OpenSSL CMS разбором). Ошибка кодирования — DerError.
#pragma once

#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>

namespace ot::sign::der {

class DerError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

// Теги, которые нужны разбору (однооктетные: класс | составной | номер).
inline constexpr std::uint8_t kInteger = 0x02;
inline constexpr std::uint8_t kBitString = 0x03;
inline constexpr std::uint8_t kOctetString = 0x04;
inline constexpr std::uint8_t kNull = 0x05;
inline constexpr std::uint8_t kOid = 0x06;
inline constexpr std::uint8_t kUtf8String = 0x0c;
inline constexpr std::uint8_t kUtcTime = 0x17;
inline constexpr std::uint8_t kGeneralizedTime = 0x18;
inline constexpr std::uint8_t kSequence = 0x30;
inline constexpr std::uint8_t kSet = 0x31;
// [n] составной контекстный (IMPLICIT/EXPLICIT SEQUENCE, SET) и простой.
constexpr std::uint8_t context(int n, bool constructed = true) {
    return static_cast<std::uint8_t>(0x80 | (constructed ? 0x20 : 0) | n);
}

struct Node {
    std::uint8_t tag = 0;                   // первый октет; многооктетные номера тегов — DerError
    std::span<const std::uint8_t> value;    // содержимое (у неопределённой длины — без 00 00)
    std::span<const std::uint8_t> encoded;  // TLV целиком (как в файле)
    bool constructed() const { return (tag & 0x20) != 0; }
};

class Reader {
public:
    explicit Reader(std::span<const std::uint8_t> data) : data_(data) {}

    bool atEnd() const { return pos_ >= data_.size(); }
    // Следующий элемент; конец данных или ошибка кодирования — DerError.
    Node next();
    // Следующий элемент с тегом tag, иначе DerError «what: expected …».
    Node expect(std::uint8_t tag, const char* what);
    // Если следующий элемент с тегом tag — прочитать его в out и вернуть true.
    bool nextIf(std::uint8_t tag, Node& out);
    // Тег следующего элемента без чтения (0 — конец).
    std::uint8_t peekTag() const { return atEnd() ? 0 : data_[pos_]; }

private:
    std::span<const std::uint8_t> data_;
    std::size_t pos_ = 0;
};

// OID «1.2.840.113549.1.9.4» из содержимого.
std::string toOid(std::span<const std::uint8_t> value);
// Малое целое (до 63 бит) — версии, статусы; большое — DerError.
std::int64_t toInt(const Node& node);
// UTCTime (0x17) / GeneralizedTime (0x18) → «2026-06-01T12:30:00Z» (доли секунд отбрасываются).
std::string toTime(std::uint8_t tag, std::span<const std::uint8_t> value);

}  // namespace ot::sign::der
