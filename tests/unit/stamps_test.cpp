// unit.stamps.* — штампы и вложение-комментарий (план 5.2, п. 4; эталон 3.4 «Инструмент»: Штамп (K), Вложить
// файл; comment-11, comment-12, comment-13 частично, comment-14, comment-41): подстановки {name} {title} {org}
// {date} {time}; штамп из assets/stamps — векторный вид (контуры, ни шрифтов, ни картинок) с /Name Acrobat,
// переживает инкрементальную запись; из PNG — картинка в /AP (pdf_set_annot_stamp_image и одностраничный PDF
// пользовательского штампа); страница чужого PDF — отдельным файлом; динамический штамп рисует подставленную дату
// (другая дата — другой вид); поворот — /Matrix вида, /Rect по повёрнутой рамке, /Rotate, undo; вложение
// извлекается байт в байт.
#include <gtest/gtest.h>

#include <cmath>
#include <filesystem>
#include <string>

#include "core/annot/annots.h"
#include "core/annot/defaults.h"
#include "core/annot/stamps.h"
#include "core/doc/document.h"
#include "core/doc/journal.h"
#include "core/doc/saver.h"
#include "core/links/attachments.h"
#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/render/bitmap.h"
#include "core/util/features.h"
#include "core/util/paths.h"
#include "support/test_pdf.h"

extern "C" {
#include <mupdf/fitz.h>
#include <mupdf/pdf.h>
}

namespace fs = std::filesystem;
using ot::Document;
using ot::annot::AnnotProps;
using ot::annot::AnnotRef;
using ot::annot::AnnotType;
using ot::annot::StampSource;
using ot::annot::StampValues;

namespace {

std::string asset(const char* rel) {
    return ot::paths::readFile(ot::test::sourceDir() / "assets" / "stamps" / ot::paths::fromUtf8(rel));
}

// PNG w × h: левая половина красная, правая — синяя.
std::string pngBytes(int w, int h) {
    fz_context* ctx = ot::MuContext::global().get();
    std::string out;
    ot::mu::call(ctx, [&] {
        fz_pixmap* pix = fz_new_pixmap(ctx, fz_device_rgb(ctx), w, h, nullptr, 0);
        unsigned char* s = fz_pixmap_samples(ctx, pix);
        for (int y = 0; y < h; ++y) {
            for (int x = 0; x < w; ++x) {
                unsigned char* p = s + (static_cast<std::ptrdiff_t>(y) * w + x) * 3;
                p[0] = x < w / 2 ? 220 : 20;
                p[1] = 30;
                p[2] = x < w / 2 ? 30 : 200;
            }
        }
        fz_buffer* buf = fz_new_buffer_from_pixmap_as_png(ctx, pix, fz_default_color_params);
        unsigned char* data = nullptr;
        const size_t len = fz_buffer_storage(ctx, buf, &data);
        out.assign(reinterpret_cast<const char*>(data), len);
        fz_drop_buffer(ctx, buf);
        fz_drop_pixmap(ctx, pix);
    });
    return out;
}

// Вид /AP /N: поток, есть ли шрифты и картинки в ресурсах, /Matrix, /Name и /Rotate словаря.
struct Ap {
    std::string stream;
    bool fonts = false;
    bool images = false;
    fz_matrix matrix = fz_identity;
    std::string name;
    int rotate = 0;
    fz_rect rect{};
    std::string subtype;
};

bool hasImageXObject(fz_context* ctx, pdf_obj* res, int depth = 0) {
    pdf_obj* xobjs = pdf_dict_get(ctx, res, PDF_NAME(XObject));
    for (int i = 0; i < pdf_dict_len(ctx, xobjs); ++i) {
        pdf_obj* x = pdf_dict_get_val(ctx, xobjs, i);
        if (pdf_is_image_stream(ctx, x)) {
            return true;
        }
        if (depth < 4 && hasImageXObject(ctx, pdf_dict_get(ctx, x, PDF_NAME(Resources)), depth + 1)) {
            return true;
        }
    }
    return false;
}

bool hasFonts(fz_context* ctx, pdf_obj* res, int depth = 0) {
    if (pdf_dict_len(ctx, pdf_dict_get(ctx, res, PDF_NAME(Font))) > 0) {
        return true;
    }
    pdf_obj* xobjs = pdf_dict_get(ctx, res, PDF_NAME(XObject));
    for (int i = 0; i < pdf_dict_len(ctx, xobjs) && depth < 4; ++i) {
        if (hasFonts(ctx, pdf_dict_get(ctx, pdf_dict_get_val(ctx, xobjs, i), PDF_NAME(Resources)), depth + 1)) {
            return true;
        }
    }
    return false;
}

Ap apOf(Document& doc, int objnum) {
    Ap out;
    fz_context* ctx = doc.context().get();
    pdf_document* pdf = doc.pdf();
    ot::mu::call(ctx, [&] {
        pdf_obj* a = pdf_load_object(ctx, pdf, objnum);
        pdf_obj* n = pdf_dict_getp(ctx, a, "AP/N");
        if (pdf_is_stream(ctx, n)) {
            fz_buffer* buf = pdf_load_stream(ctx, n);
            unsigned char* data = nullptr;
            const size_t len = fz_buffer_storage(ctx, buf, &data);
            out.stream.assign(reinterpret_cast<const char*>(data), len);
            fz_drop_buffer(ctx, buf);
            pdf_obj* res = pdf_dict_get(ctx, n, PDF_NAME(Resources));
            out.fonts = hasFonts(ctx, res);
            out.images = hasImageXObject(ctx, res);
            out.matrix = pdf_dict_get_matrix(ctx, n, PDF_NAME(Matrix));
        }
        const char* name = pdf_dict_get_name(ctx, a, PDF_NAME(Name));
        out.name = name != nullptr ? name : "";
        const char* sub = pdf_dict_get_name(ctx, a, PDF_NAME(Subtype));
        out.subtype = sub != nullptr ? sub : "";
        out.rotate = pdf_dict_get_int(ctx, a, PDF_NAME(Rotate));
        out.rect = pdf_dict_get_rect(ctx, a, PDF_NAME(Rect));
        pdf_drop_obj(ctx, a);
    });
    return out;
}

int inkPixels(const ot::Bitmap& with, const ot::Bitmap& without, const ot::RectF& r) {
    int n = 0;
    for (int y = std::max(0, static_cast<int>(r.y0)); y < std::min(with.height, static_cast<int>(r.y1)); ++y) {
        for (int x = std::max(0, static_cast<int>(r.x0)); x < std::min(with.width, static_cast<int>(r.x1)); ++x) {
            for (int k = 0; k < with.components; ++k) {
                const auto i = static_cast<std::size_t>(x * with.components + k);
                if (with.row(y)[i] != without.row(y)[i]) {
                    ++n;
                    break;
                }
            }
        }
    }
    return n;
}

AnnotProps stampProps(ot::RectF rect) {
    AnnotProps p = ot::annot::toolDefaults(AnnotType::Stamp);
    p.rect = rect;
    p.author = "Tester";
    return p;
}

}  // namespace

TEST(stamps, expand_placeholders) {
    OT_FEATURE("comment-13");
    StampValues v;
    v.name = "Мария Иванова";
    v.title = "Юрист";
    v.org = "ООО «Ромашка»";
    v.date = "25.10.2026";
    v.time = "14:30";
    EXPECT_EQ(ot::annot::expandStampText("{name}, {date} {time}", v), "Мария Иванова, 25.10.2026 14:30");
    EXPECT_EQ(ot::annot::expandStampText("{title} — {org}", v), "Юрист — ООО «Ромашка»");
    EXPECT_EQ(ot::annot::expandStampText("{unknown} {date}", v), "{unknown} 25.10.2026");
    StampValues anon = v;
    anon.name.clear();
    EXPECT_EQ(ot::annot::expandStampText("{name}, {date}", anon), "25.10.2026");
    EXPECT_EQ(ot::annot::expandStampText("{name}", anon), "");
}


// Свой штамп: векторный вид без шрифтов и картинок, /Name Acrobat, переживает запись и переоткрытие.
TEST(stamps, vector_stamp_from_assets_survives_save) {
    OT_FEATURE("comment-11");
    ot::test::TempDir dir;
    const fs::path path = dir.path() / "stamp.pdf";
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), 1));
    StampSource src;
    src.pdf = asset("standard/approved.pdf");
    src.name = "#Approved";
    const ot::annot::StampSize size = ot::annot::stampPageSize(src.pdf);
    EXPECT_GT(size.width, 100);
    EXPECT_NEAR(size.height, 50, 0.5);
    const ot::RectF rect{100, 100, 100 + size.width, 100 + size.height};
    std::string nm;
    {
        auto doc = Document::openOrThrow(path);
        const AnnotRef ref = ot::annot::createStamp(*doc, 0, src, {}, stampProps(rect));
        nm = ref.nm;
        EXPECT_EQ(doc->undoStack().undoName(), "Add stamp");
        const Ap ap = apOf(*doc, ref.objnum);
        EXPECT_EQ(ap.subtype, "Stamp");
        EXPECT_EQ(ap.name, "#Approved");
        EXPECT_FALSE(ap.stream.empty());
        EXPECT_FALSE(ap.fonts);   // надпись — контурами
        EXPECT_FALSE(ap.images);  // векторный
        const ot::Bitmap with = doc->renderPage(0, {.dpi = 72, .annotations = true});
        const ot::Bitmap without = doc->renderPage(0, {.dpi = 72, .annotations = false});
        EXPECT_GE(inkPixels(with, without, rect), 500);
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    auto doc = Document::openOrThrow(path);
    const auto list = ot::annot::listAnnots(*doc, 0);
    ASSERT_EQ(list.size(), 1u);
    EXPECT_EQ(list.front().ref.nm, nm);
    const auto props = ot::annot::readProps(*doc, list.front().ref);
    ASSERT_TRUE(props);
    EXPECT_EQ(props->type, AnnotType::Stamp);
    EXPECT_EQ(props->icon, "#Approved");
    EXPECT_NEAR(props->rect.x0, 100, 0.5);
    EXPECT_NEAR(props->rect.x1 - props->rect.x0, size.width, 0.5);
    // Рендер не пересинтезирует вид (нестандартное имя с /AP): шрифтов в виде так и нет.
    doc->renderPage(0, {.dpi = 36, .annotations = true});
    EXPECT_FALSE(apOf(*doc, list.front().ref.objnum).fonts);
}

// Картинка: вид с картинкой — штамп-картинка MuPDF и одностраничный PDF пользовательского штампа.
TEST(stamps, image_stamps_put_image_into_appearance) {
    OT_FEATURE("comment-12");
    OT_FEATURE("comment-41");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    const std::string png = pngBytes(96, 48);
    const ot::annot::StampSize size = ot::annot::imageStampSize(png);
    EXPECT_NEAR(size.width, 72, 0.5);  // 96 px при 96 dpi — дюйм
    EXPECT_NEAR(size.height, 36, 0.5);
    const AnnotRef a = ot::annot::createImageStamp(*doc, 0, png, stampProps({100, 100, 244, 172}), "Logo");
    Ap ap = apOf(*doc, a.objnum);
    EXPECT_TRUE(ap.images);
    EXPECT_EQ(ap.name, "Logo");
    EXPECT_NE(ap.stream.find("Do"), std::string::npos);

    const std::string pagePdf = ot::annot::imageStampPdf(png);
    const ot::annot::StampSize pageSize = ot::annot::stampPageSize(pagePdf);
    EXPECT_NEAR(pageSize.width, 72, 0.5);
    StampSource src;
    src.pdf = pagePdf;
    src.name = "MyLogo";
    const AnnotRef b = ot::annot::createStamp(*doc, 0, src, {}, stampProps({300, 100, 444, 172}));
    ap = apOf(*doc, b.objnum);
    EXPECT_TRUE(ap.images);
    const ot::Bitmap with = doc->renderPage(0, {.dpi = 72, .annotations = true});
    const ot::Bitmap without = doc->renderPage(0, {.dpi = 72, .annotations = false});
    EXPECT_GE(inkPixels(with, without, {300, 100, 444, 172}), 1000);
    EXPECT_GE(inkPixels(with, without, {100, 100, 244, 172}), 1000);

    // Страница чужого PDF — отдельным файлом.
    const std::string two = ot::test::makePdf(ot::MuContext::global(), 2);
    const std::string one = ot::annot::pageStampPdf(two, 1);
    auto single = Document::openOrThrowMemory(one);
    EXPECT_EQ(single->pageCount(), 1);
}

// Динамический: строки шаблона — подставленным текстом (другая дата — другой вид).
TEST(stamps, dynamic_stamp_draws_expanded_lines) {
    OT_FEATURE("comment-13");
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    StampSource src;
    src.pdf = asset("dynamic/approved.pdf");
    src.name = "#DApproved";
    src.lines = {{"{name}", 100, 47, 9, 1, 0x2E7D32}, {"{date} {time}", 100, 58, 9, 1, 0x2E7D32}};
    StampValues v;
    v.name = "Мария Иванова";
    v.date = "25.10.2026";
    v.time = "14:30";
    const AnnotRef a = ot::annot::createStamp(*doc, 0, src, v, stampProps({100, 600, 300, 664}));
    v.date = "01.01.2027";
    const AnnotRef b = ot::annot::createStamp(*doc, 0, src, v, stampProps({320, 600, 520, 664}));
    StampSource plain = src;
    plain.lines.clear();
    const AnnotRef c = ot::annot::createStamp(*doc, 0, plain, v, stampProps({100, 700, 300, 764}));
    const Ap apA = apOf(*doc, a.objnum);
    const Ap apB = apOf(*doc, b.objnum);
    const Ap apC = apOf(*doc, c.objnum);
    EXPECT_NE(apA.stream, apB.stream);                // дата другая — контуры другие
    EXPECT_GT(apA.stream.size(), apC.stream.size());  // строки добавились
    EXPECT_FALSE(apA.fonts);
    // Строки — в нижней части штампа: там штамп со строками отличается от штампа без них, надпись сверху — нет.
    const ot::Bitmap page = doc->renderPage(0, {.dpi = 72, .annotations = true});
    auto differing = [&page](int y0, int y1) {
        int n = 0;
        for (int y = y0; y < y1; ++y) {
            for (int x = 110; x < 290; ++x) {
                const auto i = static_cast<std::size_t>(x * page.components);
                if (page.row(y)[i] != page.row(y + 100)[i] || page.row(y)[i + 1] != page.row(y + 100)[i + 1]) {
                    ++n;
                }
            }
        }
        return n;
    };
    EXPECT_GT(differing(640, 662), 50);  // строки шаблона
    EXPECT_EQ(differing(604, 630), 0);   // надпись и рамка
}

// Поворот: /Matrix вида, /Rect по повёрнутой рамке с тем же центром, /Rotate; undo — как было.
TEST(stamps, rotation_sets_matrix_and_rect) {
    auto doc = Document::openOrThrowMemory(ot::test::makePdf(ot::MuContext::global(), 1));
    StampSource src;
    src.pdf = asset("standard/draft.pdf");
    src.name = "#Draft";
    const ot::annot::StampSize size = ot::annot::stampPageSize(src.pdf);
    const ot::RectF rect{200, 200, 200 + size.width, 200 + size.height};
    const AnnotRef ref = ot::annot::createStamp(*doc, 0, src, {}, stampProps(rect));
    ot::annot::setStampRotation(*doc, ref, 90);
    EXPECT_EQ(doc->undoStack().undoName(), "Rotate stamp");
    EXPECT_NEAR(ot::annot::stampRotation(*doc, ref), 90, 0.01);
    const Ap ap = apOf(*doc, ref.objnum);
    EXPECT_EQ(ap.rotate, 90);
    EXPECT_NEAR(std::fabs(ap.matrix.b), 1, 0.001);  // поворот на 90°
    EXPECT_NEAR(ap.matrix.a, 0, 0.001);
    EXPECT_NEAR(ap.rect.x1 - ap.rect.x0, size.height, 0.5);  // рамка повернулась
    EXPECT_NEAR(ap.rect.y1 - ap.rect.y0, size.width, 0.5);
    const auto props = ot::annot::readProps(*doc, ref);
    ASSERT_TRUE(props);
    EXPECT_NEAR((props->rect.x0 + props->rect.x1) / 2, (rect.x0 + rect.x1) / 2, 0.5);  // центр на месте
    doc->undoStack().undo();
    EXPECT_NEAR(ot::annot::stampRotation(*doc, ref), 0, 0.01);
}

// Вложение-комментарий: файл извлекается байт в байт после записи и переоткрытия.
TEST(stamps, file_attachment_extracts_byte_for_byte) {
    OT_FEATURE("comment-14");
    ot::test::TempDir dir;
    const fs::path path = dir.path() / "attach.pdf";
    ot::paths::writeFile(path, ot::test::makePdf(ot::MuContext::global(), 1));
    std::string data;
    for (int i = 0; i < 5000; ++i) {
        data.push_back(static_cast<char>(i * 37 % 256));
    }
    {
        auto doc = Document::openOrThrow(path);
        AnnotProps p = ot::annot::toolDefaults(AnnotType::FileAttachment);
        p.rect = {100, 100, 120, 120};
        p.icon = "Paperclip";
        p.fileName = "отчёт.bin";
        p.fileData = data;
        p.contents = "отчёт.bin";
        ot::annot::create(*doc, 0, p);
        const ot::SaveReport report = ot::Saver::save(*doc, path, {.mode = ot::SaveMode::Incremental});
        ASSERT_TRUE(report.ok()) << report.message;
    }
    auto doc = Document::openOrThrow(path);
    const auto files = ot::attachments::list(*doc);
    ASSERT_EQ(files.size(), 1u);
    EXPECT_EQ(files.front().source, ot::attachments::Source::Annotation);
    EXPECT_EQ(files.front().fileName, "отчёт.bin");
    EXPECT_EQ(ot::attachments::contents(*doc, files.front()), data);
    const auto props = ot::annot::readProps(*doc, ot::annot::listAnnots(*doc, 0).front().ref);
    ASSERT_TRUE(props);
    EXPECT_EQ(props->icon, "Paperclip");
}
