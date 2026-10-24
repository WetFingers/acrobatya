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

QFuture<edit::EditLock> lock(DocumentSession& session, TaskPriority priority) {
    return session.post([](TaskContext& tc) { return edit::editLock(tc.document()); }, priority,
                        QStringLiteral("Edit lock"));
}

}  // namespace ot::editing
