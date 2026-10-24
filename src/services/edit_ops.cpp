#include "services/edit_ops.h"

#include "core/edit/content_model.h"
#include "core/edit/text_blocks.h"
#include "services/task_context.h"

namespace ot::editing {

QFuture<PageContent> pageContent(DocumentSession& session, int page, TaskPriority priority) {
    return session.post(
        [page](TaskContext& tc) {
            PageContent out;
            out.page = page;
            const edit::ContentModel model = edit::ContentModel::build(tc.document(), page);
            const std::vector<edit::TextBlock> blocks = edit::blocksForPage(model);
            out.blocks.reserve(blocks.size());
            for (const edit::TextBlock& b : blocks) {
                Block block;
                block.bbox = b.bbox;
                block.reason = QString::fromLatin1(edit::readOnlyCode(b.reason));
                if (b.editable()) {
                    try {
                        block.editor = edit::ParagraphEditor::fromBlock(model, b, blocks);
                    } catch (const std::invalid_argument&) {
                        block.reason = QStringLiteral("syntax");
                    }
                }
                out.blocks.push_back(std::move(block));
            }
            return out;
        },
        priority, QStringLiteral("Edit blocks"));
}

QFuture<edit::ParagraphEditor> newParagraph(DocumentSession& session, int page, PointF topLeft, float width,
                                            edit::CharStyle style) {
    return session.post(
        [page, topLeft, width, style](TaskContext& tc) {
            const edit::ContentModel model = edit::ContentModel::build(tc.document(), page);
            const Matrix toPage = concat(Matrix{1, 0, 0, -1, 0, 0}, model.pageMatrix());
            const PointF origin = mapPoint(topLeft, invertMatrix(toPage));
            edit::ParagraphEditor e;
            edit::ParagraphGeometry g;
            g.left = origin.x;
            g.width = width;
            g.fontSize = style.size;
            g.firstBaseline = origin.y + style.size * 0.9f;  // верх строки — у щелчка
            g.toPage = toPage;
            e.setGeometry(g);
            edit::ParagraphSource source;
            source.page = page;
            source.bbox = RectF{topLeft.x, topLeft.y, topLeft.x + width, topLeft.y + style.size * 1.2f};
            e.markWritten(source);
            (void)style;  // стиль набора пустого абзаца держит EditMode (до первой буквы)
            return e;
        },
        TaskPriority::High, QStringLiteral("New paragraph"));
}

QFuture<edit::EditLock> lock(DocumentSession& session, TaskPriority priority) {
    return session.post([](TaskContext& tc) { return edit::editLock(tc.document()); }, priority,
                        QStringLiteral("Edit lock"));
}

}  // namespace ot::editing
