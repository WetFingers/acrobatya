// Редактор текста через DocumentSession (план 3.4; эталон 3.9 «Инструмент»): задачи потока документа над
// core/edit — для ui, у которого нет MuPDF и модели контент-потока. Содержимое страницы для инструмента
// «Редактировать PDF» — блоки (абзацы) в порядке чтения: рамка, можно ли править и почему нет (код
// readOnlyCode), у редактируемого — ParagraphEditor, построенный по той же модели (правка — копией редактора,
// запись — DocumentSession::editText). Запрет правки документа — подписан или /Perms (edit::editLock:
// Rewriter откажет так же).
#pragma once

#include <QFuture>
#include <QString>

#include <optional>
#include <vector>

#include "core/edit/paragraph_editor.h"
#include "core/edit/rewriter.h"
#include "core/util/geometry.h"
#include "services/document_session.h"

namespace ot::editing {

// Коды шагов истории (подпись «Отменить: …» по-русски выбирает ui).
inline constexpr const char* kEditTextStep = "Edit text";
inline constexpr const char* kAddTextStep = "Add text";

struct Block {
    RectF bbox;      // координаты страницы MuPDF
    QString reason;  // пусто — можно править; иначе readOnlyCode ("rotated", "form", "type3"…)
    std::optional<edit::ParagraphEditor> editor;  // у редактируемого

    bool editable() const noexcept { return reason.isEmpty() && editor.has_value(); }
};

struct PageContent {
    int page = -1;
    std::vector<Block> blocks;  // порядок чтения (blocksForPage)
};

// Блоки страницы page по модели контент-потока (ContentModel::build → blocksForPage → fromBlock).
QFuture<PageContent> pageContent(DocumentSession& session, int page,
                                 TaskPriority priority = TaskPriority::High);
// Можно ли править документ: подписан, /Perms или нет.
QFuture<edit::EditLock> lock(DocumentSession& session, TaskPriority priority = TaskPriority::High);

}  // namespace ot::editing
