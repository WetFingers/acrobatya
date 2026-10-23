// unit.mu_context.* — контекст MuPDF (эталон 4.1): один global() на процесс, клоны для потоков с общим
// store и замками, ошибки MuPDF как MuError с кодом и видом, контекст пригоден после ошибки, предупреждения
// и ошибки MuPDF — в spdlog (колбэки не бросают), RAII-обёртки mu::*Handle.
#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/sinks/base_sink.h>
#include <spdlog/sinks/ringbuffer_sink.h>
#include <spdlog/spdlog.h>

#include "core/mu/call.h"
#include "core/mu/context.h"
#include "core/mu/handles.h"
#include "core/util/features.h"

extern "C" {
#include <mupdf/pdf.h>
}

using ot::ErrorKind;
using ot::MuContext;
using ot::MuError;

namespace {

// Подменяет логгер spdlog по умолчанию на время теста и возвращает прежний.
class LogCapture {
public:
    LogCapture() : sink_(std::make_shared<spdlog::sinks::ringbuffer_sink_mt>(64)) {
        previous_ = spdlog::default_logger();
        auto logger = std::make_shared<spdlog::logger>("capture", sink_);
        logger->set_level(spdlog::level::trace);
        logger->set_pattern("%l|%v");
        spdlog::set_default_logger(logger);
    }
    ~LogCapture() { spdlog::set_default_logger(previous_); }
    LogCapture(const LogCapture&) = delete;
    LogCapture& operator=(const LogCapture&) = delete;

    std::string text() const {
        std::string all;
        for (const auto& line : sink_->last_formatted()) {
            all += line;
        }
        return all;
    }

private:
    std::shared_ptr<spdlog::sinks::ringbuffer_sink_mt> sink_;
    std::shared_ptr<spdlog::logger> previous_;
};

// Приёмник, который бросает на каждой записи: колбэки MuPDF не должны выпустить исключение.
class ThrowingSink : public spdlog::sinks::base_sink<std::mutex> {
protected:
    void sink_it_(const spdlog::details::log_msg&) override { throw std::runtime_error("sink failure"); }
    void flush_() override {}
};

int renderBlankPageInk(fz_context* ctx) {
    pdf_document* doc = nullptr;
    pdf_obj* page = nullptr;
    fz_page* loaded = nullptr;
    fz_pixmap* pix = nullptr;
    int white = 0;
    ot::mu::call(ctx, [&] {
        doc = pdf_create_document(ctx);
        page = pdf_add_page(ctx, doc, fz_make_rect(0, 0, 200, 100), 0, nullptr, nullptr);
        pdf_insert_page(ctx, doc, -1, page);
        loaded = fz_load_page(ctx, &doc->super, 0);
        pix = fz_new_pixmap_from_page(ctx, loaded, fz_identity, fz_device_rgb(ctx), 0);
        white = fz_pixmap_samples(ctx, pix)[0];
    });
    fz_drop_pixmap(ctx, pix);
    fz_drop_page(ctx, loaded);
    pdf_drop_obj(ctx, page);
    pdf_drop_document(ctx, doc);
    return white;
}

}  // namespace

TEST(mu_context, creates_with_document_handlers) {
    MuContext mu;
    ASSERT_NE(mu.get(), nullptr);
    EXPECT_EQ(MuContext::version(), "1.28.5");
}

TEST(mu_context, global_is_one_per_process) {
    OT_FEATURE("view-01");
    MuContext& a = MuContext::global();
    MuContext& b = MuContext::global();
    EXPECT_EQ(&a, &b);
    ASSERT_NE(a.get(), nullptr);
}

TEST(mu_context, clones_share_store_with_global) {
    MuContext& global = MuContext::global();
    auto clone = global.clone();
    ASSERT_NE(clone->get(), nullptr);
    EXPECT_NE(clone->get(), global.get());
    EXPECT_EQ(clone->get()->store, global.get()->store);  // общий кеш ресурсов
    auto second = clone->clone();                         // клон клона — тот же store
    EXPECT_EQ(second->get()->store, global.get()->store);
}

TEST(mu_context, clone_works_in_other_thread) {
    MuContext mu;
    auto clone = mu.clone();
    int pages = -1;
    std::thread t([&] {
        fz_context* ctx = clone->get();
        ot::mu::call(ctx, [&] {
            pdf_document* doc = pdf_create_document(ctx);
            pages = pdf_count_pages(ctx, doc);
            pdf_drop_document(ctx, doc);
        });
    });
    t.join();
    EXPECT_EQ(pages, 0);
}

TEST(mu_context, clones_render_concurrently) {
    // Замки из трёх полей на std::mutex: восемь потоков с клонами global() рисуют одновременно.
    std::atomic<int> ok{0};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i) {
        threads.emplace_back([&ok] {
            auto ctx = MuContext::global().clone();
            for (int k = 0; k < 10; ++k) {
                if (renderBlankPageInk(ctx->get()) == 255) {
                    ++ok;
                }
            }
        });
    }
    for (auto& t : threads) {
        t.join();
    }
    EXPECT_EQ(ok.load(), 80);
}

TEST(mu_context, error_becomes_exception_with_code) {
    MuContext mu;
    fz_context* ctx = mu.get();
    try {
        ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_ARGUMENT, "test error %d", 42); });
        FAIL() << "no exception";
    } catch (const MuError& e) {
        EXPECT_EQ(e.code(), FZ_ERROR_ARGUMENT);
        EXPECT_EQ(e.kind(), ErrorKind::Argument);
        EXPECT_STREQ(e.what(), "test error 42");
    }
    // контекст пригоден после ошибки
    int n = -1;
    ot::mu::call(ctx, [&] { n = 7; });
    EXPECT_EQ(n, 7);
}

TEST(mu_context, error_kinds_follow_codes) {
    MuContext mu;
    fz_context* ctx = mu.get();
    struct Case {
        int code;
        ErrorKind kind;
        const char* name;
    };
    const Case cases[] = {
        {FZ_ERROR_FORMAT, ErrorKind::Damaged, "format"},
        {FZ_ERROR_SYNTAX, ErrorKind::Damaged, "syntax"},
        {FZ_ERROR_UNSUPPORTED, ErrorKind::Unsupported, "unsupported"},
        {FZ_ERROR_LIMIT, ErrorKind::Limit, "limit"},
        {FZ_ERROR_SYSTEM, ErrorKind::System, "system"},
        {FZ_ERROR_ABORT, ErrorKind::Aborted, "abort"},
        {FZ_ERROR_GENERIC, ErrorKind::Other, "generic"},
        {FZ_ERROR_LIBRARY, ErrorKind::Other, "library"},
    };
    for (const Case& c : cases) {
        try {
            ot::mu::call(ctx, [&] { fz_throw(ctx, c.code, "проба кода %d", c.code); });
            ADD_FAILURE() << "no exception for " << c.name;
        } catch (const MuError& e) {
            EXPECT_EQ(e.code(), c.code);
            EXPECT_EQ(e.kind(), c.kind) << c.name;
            EXPECT_STREQ(MuError::codeName(e.code()), c.name);
            EXPECT_NE(std::string(e.what()).find("проба кода"), std::string::npos);
        }
    }
    EXPECT_EQ(ot::errorKind(0), ErrorKind::None);
}

TEST(mu_context, many_errors_do_not_exhaust_error_stack) {
    LogCapture quiet;  // тысяча строк «format error» — не в вывод теста
    MuContext mu;
    fz_context* ctx = mu.get();
    int caught = 0;
    for (int i = 0; i < 1000; ++i) {
        try {
            ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_FORMAT, "broken %d", i); });
        } catch (const MuError&) {
            ++caught;
        }
    }
    EXPECT_EQ(caught, 1000);
    int pages = -1;
    ot::mu::call(ctx, [&] {
        pdf_document* doc = pdf_create_document(ctx);
        pages = pdf_count_pages(ctx, doc);
        pdf_drop_document(ctx, doc);
    });
    EXPECT_EQ(pages, 0);
}

TEST(mu_context, long_message_is_truncated_not_overflowed) {
    LogCapture quiet;
    MuContext mu;
    fz_context* ctx = mu.get();
    const std::string longText(2000, 'x');
    try {
        ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_SYNTAX, "%s", longText.c_str()); });
        FAIL() << "no exception";
    } catch (const MuError& e) {
        EXPECT_GT(std::string(e.what()).size(), 100u);
        EXPECT_LT(std::string(e.what()).size(), 1024u);
    }
}

TEST(mu_context, warnings_and_errors_go_to_spdlog) {
    LogCapture capture;
    MuContext mu;
    fz_context* ctx = mu.get();
    fz_warn(ctx, "предупреждение %d", 5);
    fz_flush_warnings(ctx);
    EXPECT_THROW(ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_FORMAT, "плохой файл"); }), MuError);
    const std::string log = capture.text();
    EXPECT_NE(log.find("warning|mupdf: предупреждение 5"), std::string::npos) << log;
    EXPECT_NE(log.find("error|mupdf: format error: плохой файл"), std::string::npos) << log;
}

TEST(mu_context, clone_inherits_log_callbacks) {
    LogCapture capture;
    auto clone = MuContext::global().clone();
    fz_warn(clone->get(), "из клона");
    fz_flush_warnings(clone->get());
    EXPECT_NE(capture.text().find("mupdf: из клона"), std::string::npos) << capture.text();
}

TEST(mu_context, aborted_error_is_not_logged_as_error) {
    LogCapture capture;
    MuContext mu;
    fz_context* ctx = mu.get();
    EXPECT_THROW(ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_ABORT, "отменено"); }), MuError);
    EXPECT_EQ(capture.text().find("error|"), std::string::npos) << capture.text();
}

TEST(mu_context, log_callbacks_never_throw) {
    auto previous = spdlog::default_logger();
    spdlog::set_default_logger(
        std::make_shared<spdlog::logger>("throwing", std::make_shared<ThrowingSink>()));
    spdlog::default_logger()->set_error_handler(
        [](const std::string&) { throw std::runtime_error("handler"); });
    {
        MuContext mu;
        fz_context* ctx = mu.get();
        EXPECT_NO_THROW(fz_warn(ctx, "не должно бросить"));
        EXPECT_NO_THROW(fz_flush_warnings(ctx));
        EXPECT_THROW(ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_FORMAT, "x"); }), MuError);
        int n = 0;
        ot::mu::call(ctx, [&] { n = 1; });
        EXPECT_EQ(n, 1);
    }
    spdlog::set_default_logger(previous);
}

TEST(mu_handles, drop_on_scope_exit) {
    MuContext mu;
    fz_context* ctx = mu.get();
    fz_buffer* raw = nullptr;
    ot::mu::call(ctx, [&] { raw = fz_new_buffer(ctx, 16); });
    fz_keep_buffer(ctx, raw);
    ASSERT_EQ(raw->refs, 2);
    {
        ot::mu::BufferHandle owned(ctx, raw);
        EXPECT_EQ(owned.get(), raw);
        EXPECT_TRUE(owned);
    }
    EXPECT_EQ(raw->refs, 1);
    fz_drop_buffer(ctx, raw);
}

TEST(mu_handles, reset_release_and_move) {
    MuContext mu;
    fz_context* ctx = mu.get();
    ot::mu::BufferHandle a(ctx);
    EXPECT_FALSE(a);
    ot::mu::call(ctx, [&] { a.reset(fz_new_buffer(ctx, 8)); });
    ASSERT_TRUE(a);
    fz_buffer* keep = fz_keep_buffer(ctx, a.get());
    ot::mu::BufferHandle b(std::move(a));
    EXPECT_FALSE(a);  // NOLINT(bugprone-use-after-move): перемещённый пуст по контракту
    EXPECT_EQ(b.get(), keep);
    ot::mu::BufferHandle c(ctx);
    c = std::move(b);
    EXPECT_EQ(c.get(), keep);
    EXPECT_EQ(keep->refs, 2);
    c.reset();
    EXPECT_EQ(keep->refs, 1);
    ot::mu::BufferHandle d(ctx, fz_keep_buffer(ctx, keep));
    fz_buffer* released = d.release();
    EXPECT_FALSE(d);
    EXPECT_EQ(keep->refs, 2);
    fz_drop_buffer(ctx, released);
    fz_drop_buffer(ctx, keep);
}

TEST(mu_handles, page_and_pixmap_handles_with_mu_call) {
    MuContext mu;
    fz_context* ctx = mu.get();
    ot::mu::PdfDocumentHandle doc(ctx);
    ot::mu::PdfObjHandle pageObj(ctx);
    ot::mu::PageHandle page(ctx);
    ot::mu::PixmapHandle pix(ctx);
    ot::mu::call(ctx, [&] {
        doc.reset(pdf_create_document(ctx));
        pageObj.reset(pdf_add_page(ctx, doc, fz_make_rect(0, 0, 50, 40), 0, nullptr, nullptr));
        pdf_insert_page(ctx, doc, -1, pageObj);
        page.reset(fz_load_page(ctx, &doc.get()->super, 0));
        pix.reset(fz_new_pixmap_from_page(ctx, page, fz_identity, fz_device_rgb(ctx), 0));
    });
    EXPECT_EQ(fz_pixmap_width(ctx, pix), 50);
    EXPECT_EQ(fz_pixmap_height(ctx, pix), 40);
}
