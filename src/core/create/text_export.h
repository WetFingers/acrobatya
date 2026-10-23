// Экспорт текста (эталон 3.12 «Текстовые форматы», план 1.4; create-33 HTML, create-34 RTF/TXT/XML).
// TXT — текстовый слой в порядке чтения (TextPage: SEGMENT, абзацы — пустой строкой), строки CRLF,
// страницы через символ перевода формата (0x0C); кодировка UTF-8 (BOM по флажку), UTF-16 LE с BOM или CP1251
// («ANSI» русской Windows: чего нет в 1251 — «?», без «похожих» замен). HTML и XHTML —
// fz_print_stext_page_as_html / _xhtml (слои с картинками — data:…;base64), в HTML добавлен
// <meta charset="utf-8">. XML — fz_print_stext_page_as_xml внутри <document>.
// RTF — свой писатель (core/create/rtf_writer.h): абзацы, шрифт, кегль, жирный и курсив.
// Документ читается в его потоке (задача DocumentSession::exportText); cancelled() == true между страницами —
// MuError Aborted. Заголовок без MuPDF (его включает document_session.h).
#pragma once

#include <functional>
#include <string>
#include <string_view>

namespace ot {

class Document;

enum class TextFormat { Txt, Html, Xhtml, Xml, Rtf };
enum class TextEncoding { Utf8, Utf16, Cp1251 };

struct TextExportOptions {
    TextFormat format = TextFormat::Txt;
    TextEncoding encoding = TextEncoding::Utf8;  // только TXT
    bool bom = false;                            // UTF-8 с BOM (TXT)
    int firstPage = 0;
    int lastPage = -1;  // включительно; −1 — до конца
};

// Байты файла в выбранном формате.
std::string exportText(const Document& doc, const TextExportOptions& options = {},
                       const std::function<bool()>& cancelled = {});

// UTF-8 со «\n» → байты TXT: CRLF и кодировка (UTF-16 — с BOM всегда, UTF-8 — по bom).
std::string encodeText(std::string_view utf8, TextEncoding encoding, bool bom = false);

}  // namespace ot
