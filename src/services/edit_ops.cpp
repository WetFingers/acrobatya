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
            out.images = edit::pageImages(tc.document(), model);
            return out;
        },
        priority, QStringLiteral("Edit blocks"));
}

namespace {

std::string_view view(const QByteArray& bytes) {
    return {bytes.constData(), static_cast<std::size_t>(bytes.size())};
}

template <class F>
QFuture<void> imageEdit(DocumentSession& session, const char* step, int page, F fn) {
    return session.edit(QString::fromLatin1(step), [page, step, fn = std::move(fn)](TaskContext& tc) {
        edit::ImageOps ops(tc.document());
        fn(ops, std::string(step));
        tc.touchPage(page);
        tc.markHeavyEdit();
    });
}

}  // namespace

QFuture<void> moveImage(DocumentSession& session, int page, int index, PointF delta) {
    return imageEdit(session, kMoveImageStep, page,
                     [=](edit::ImageOps& o, const std::string& op) { o.move(page, index, delta, op); });
}

QFuture<void> setImageRect(DocumentSession& session, int page, int index, RectF rect) {
    return imageEdit(session, kResizeImageStep, page,
                     [=](edit::ImageOps& o, const std::string& op) { o.setRect(page, index, rect, op); });
}

QFuture<void> rotateImage(DocumentSession& session, int page, int index, float degrees) {
    return imageEdit(session, kRotateImageStep, page,
                     [=](edit::ImageOps& o, const std::string& op) { o.rotate(page, index, degrees, op); });
}

QFuture<void> flipImage(DocumentSession& session, int page, int index, bool horizontal) {
    return imageEdit(session, kFlipImageStep, page,
                     [=](edit::ImageOps& o, const std::string& op) { o.flip(page, index, horizontal, op); });
}

QFuture<void> cropImage(DocumentSession& session, int page, int index, RectF rect) {
    return imageEdit(session, kCropImageStep, page,
                     [=](edit::ImageOps& o, const std::string& op) { o.crop(page, index, rect, op); });
}

QFuture<void> replaceImage(DocumentSession& session, int page, int index, QByteArray bytes) {
    return imageEdit(session, kReplaceImageStep, page, [=](edit::ImageOps& o, const std::string& op) {
        o.replace(page, index, view(bytes), op);
    });
}

QFuture<int> addImage(DocumentSession& session, int page, QByteArray bytes, RectF rect) {
    return session.edit(QString::fromLatin1(kAddImageStep), [=](TaskContext& tc) {
        edit::ImageOps ops(tc.document());
        const int index = ops.add(page, view(bytes), rect, kAddImageStep);
        tc.touchPage(page);
        tc.markHeavyEdit();
        return index;
    });
}

QFuture<void> removeImage(DocumentSession& session, int page, int index) {
    return imageEdit(session, kDeleteImageStep, page,
                     [=](edit::ImageOps& o, const std::string& op) { o.remove(page, index, op); });
}

QFuture<edit::ImageData> exportImage(DocumentSession& session, int page, int index) {
    return session.post(
        [=](TaskContext& tc) { return edit::ImageOps(tc.document()).exportImage(page, index); },
        TaskPriority::High, QStringLiteral("Export image"));
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
