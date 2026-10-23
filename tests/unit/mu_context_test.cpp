// unit.mu_context.* — контекст MuPDF, клон для другого потока, ошибки как исключения.
#include <gtest/gtest.h>

#include <thread>

#include "core/mu/call.h"

extern "C" {
#include <mupdf/pdf.h>
}
#include "core/mu/context.h"

using ot::MuContext;
using ot::MuError;

TEST(mu_context, creates_with_document_handlers) {
    MuContext mu;
    ASSERT_NE(mu.get(), nullptr);
    EXPECT_EQ(MuContext::version(), "1.28.5");
}

TEST(mu_context, clone_works_in_other_thread) {
    MuContext mu;
    auto clone = mu.clone();
    ASSERT_NE(clone->get(), nullptr);
    ASSERT_NE(clone->get(), mu.get());
    int pages = 0;
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

TEST(mu_context, error_becomes_exception_with_code) {
    MuContext mu;
    fz_context* ctx = mu.get();
    try {
        ot::mu::call(ctx, [&] { fz_throw(ctx, FZ_ERROR_ARGUMENT, "test error %d", 42); });
        FAIL() << "no exception";
    } catch (const MuError& e) {
        EXPECT_EQ(e.code(), FZ_ERROR_ARGUMENT);
        EXPECT_STREQ(e.what(), "test error 42");
    }
    // контекст пригоден после ошибки
    int n = -1;
    ot::mu::call(ctx, [&] { n = 7; });
    EXPECT_EQ(n, 7);
}
