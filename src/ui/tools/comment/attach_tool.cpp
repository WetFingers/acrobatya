// Вложить файл (план 5.2, п. 4; эталон 3.4 «Инструмент»: Вложить файл (/FileAttachment с иконкой);
// comment-14): щелчок — выбор файла (диалог «Открыть»; тесты подменяют setFilePrompt), значок 20 × 20 пт
// центром в точке щелчка, файл встраивается в PDF (/FS /EF, имя — /UF), текст комментария — имя файла, значок
// — стиль инструмента «attach» (булавка по умолчанию; Скрепка, График, Ярлык — в свойствах). Больше
// kMaxAttachMb — отказ. Открытие двойным щелчком — с чёрным списком и подтверждением панели «Вложения»
// (эталон 7.3, 2.4).
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFuture>

#include <algorithm>

#include "services/annot_ops.h"
#include "services/document_session.h"
#include "ui/tools/comment/comment_mode.h"
#include "ui/view/page_view.h"
#include "ui/view/tool_controller.h"

namespace ot::ui {

namespace {

constexpr qint64 kMaxAttachMb = 100;

CommentMode::FilePrompt& filePrompt() {
    static CommentMode::FilePrompt prompt;
    return prompt;
}

}  // namespace

void CommentMode::setFilePrompt(FilePrompt prompt) { filePrompt() = std::move(prompt); }

void CommentMode::attachPress(const Locator& at) {
    DocumentSession* s = session();
    if (s == nullptr) {
        return;
    }
    const QString path =
        filePrompt() ? filePrompt()() : QFileDialog::getOpenFileName(view_.window(), tr("Вложить файл"));
    if (path.isEmpty()) {
        controller_.finished();
        return;
    }
    QFile file(path);
    if (file.size() > kMaxAttachMb * 1024 * 1024) {
        reportError(tr("файл больше %1 МБ").arg(kMaxAttachMb));
        controller_.finished();
        return;
    }
    if (!file.open(QIODevice::ReadOnly)) {
        reportError(tr("не удалось прочитать «%1»").arg(QFileInfo(path).fileName()));
        controller_.finished();
        return;
    }
    const QByteArray bytes = file.readAll();
    const QSizeF size = view_.pageSizePoints(at.page);
    const float half = kNoteIconSize / 2;
    const float x =
        std::clamp(static_cast<float>(at.point.x()), half, static_cast<float>(size.width()) - half);
    const float y =
        std::clamp(static_cast<float>(at.point.y()), half, static_cast<float>(size.height()) - half);
    annot::AnnotProps props = newProps(CommentAction::Attach);
    props.rect = {x - half, y - half, x + half, y + half};
    const std::string name = QFileInfo(path).fileName().toStdString();
    props.fileName = name;
    props.contents = name;
    props.fileData.assign(bytes.constData(), static_cast<std::size_t>(bytes.size()));
    done(annot::create(*s, at.page, std::move(props)), false);
}

}  // namespace ot::ui
