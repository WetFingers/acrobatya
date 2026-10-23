// unit.presentation.* — переходы /Trans и время показа /Dur для полноэкранного режима (план 2.4; эталон 3.1
// «Режимы»; view-22): nav.pdf — стр. 1 без перехода, стр. 2 — растворение 0,5 с, стр. 3 — шторка сверху вниз
// 1 с и показ 3 с; значения совпадают с pdf_page_presentation MuPDF; не PDF — без переходов.
#include <gtest/gtest.h>

#include "core/doc/document.h"
#include "core/doc/presentation.h"
#include "core/util/features.h"
#include "support/test_pdf.h"

using ot::Document;
using ot::presentation::TransitionType;

namespace {

std::filesystem::path fixture(const char* rel) { return ot::test::sourceDir() / "tests" / "fixtures" / rel; }

}  // namespace

TEST(presentation, transitions_of_nav) {
    OT_FEATURE("view-22");
    auto doc = Document::openOrThrow(fixture("view/nav.pdf"));
    const auto all = ot::presentation::transitions(*doc);
    ASSERT_EQ(all.size(), 4u);
    EXPECT_EQ(all[0].type, TransitionType::None);
    EXPECT_EQ(all[0].pageDuration, 0);
    EXPECT_EQ(all[1].type, TransitionType::Dissolve);
    EXPECT_FLOAT_EQ(all[1].duration, 0.5f);
    EXPECT_EQ(all[2].type, TransitionType::Wipe);
    EXPECT_EQ(all[2].direction, 270);
    EXPECT_FLOAT_EQ(all[2].duration, 1.0f);
    EXPECT_FLOAT_EQ(all[2].pageDuration, 3.0f);
    EXPECT_EQ(ot::presentation::transitionName(all[2].type), "Wipe");
    const auto single = ot::presentation::pageTransition(*doc, 1);
    EXPECT_EQ(single.type, TransitionType::Dissolve);
}

TEST(presentation, none_for_images) {
    auto doc = Document::openOrThrow(fixture("images/chart.png"));
    const auto all = ot::presentation::transitions(*doc);
    ASSERT_EQ(all.size(), 1u);
    EXPECT_EQ(all[0].type, TransitionType::None);
}
