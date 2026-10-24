// Действия --script редактора (план 3.4, п. 4; рецепт «Новое действие --script»; ими пользуется рубеж Р2):
//   edit.insertText{page, block, pos, text} — вставить текст в абзац: страница и блок — с 1 (блоки — в
//   порядке
//     чтения, как рамки режима «Редактировать»), pos — позиция символа в абзаце с 0 (нет — в конец); запись —
//     DocumentSession::editText (шаг «Edit text»);
//   edit.replaceAll{find, replace, matchCase, wholeWord} — «Заменить все» по документу одним шагом истории;
//     ничего не заменено — ошибка шага.
#include <QJsonObject>

#include "services/document_session.h"
#include "services/edit_ops.h"
#include "services/script_runner.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/tools/edit/edit_script.h"

namespace ot::ui {

void registerEditScriptActions(ScriptRunner& runner, MainWindow& w) {
    MainWindow* window = &w;
    runner.registerAction(
        QStringLiteral("edit.insertText"),
        R"({"type": "object", "properties": {"page": {"type": "integer", "minimum": 1},
            "block": {"type": "integer", "minimum": 1}, "pos": {"type": "integer", "minimum": 0},
            "text": {"type": "string", "minLength": 1}}, "required": ["page", "block", "text"],
            "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            const int page = a.value(QLatin1String("page")).toInt() - 1;
            const int block = a.value(QLatin1String("block")).toInt() - 1;
            if (page >= doc.session().pageCount()) {
                throw ScriptError(QStringLiteral("page %1 is out of range").arg(page + 1));
            }
            const editing::PageContent content =
                script::await(editing::pageContent(doc.session(), page), "edit.insertText");
            if (block >= static_cast<int>(content.blocks.size())) {
                throw ScriptError(QStringLiteral("block %1 is out of range (page has %2)")
                                      .arg(block + 1)
                                      .arg(content.blocks.size()));
            }
            const editing::Block& b = content.blocks[static_cast<std::size_t>(block)];
            if (!b.editable()) {
                throw ScriptError(QStringLiteral("block %1 is read-only: %2").arg(block + 1).arg(b.reason));
            }
            edit::ParagraphEditor editor = *b.editor;
            const int pos =
                a.contains(QLatin1String("pos")) ? a.value(QLatin1String("pos")).toInt() : editor.size();
            editor.insert(std::min(pos, editor.size()),
                          a.value(QLatin1String("text")).toString().toStdU32String());
            const EditTextResult result =
                script::await(doc.session().editText(editor, QString::fromLatin1(editing::kEditTextStep)),
                              "edit.insertText");
            if (!result.report.ok()) {
                throw ScriptError(QStringLiteral("text edit failed: %1 %2")
                                      .arg(QLatin1String(edit::rewriteErrorCode(result.report.error)),
                                           QString::fromStdString(result.report.message)));
            }
        });
    runner.registerAction(
        QStringLiteral("edit.replaceAll"),
        R"({"type": "object", "properties": {"find": {"type": "string", "minLength": 1},
            "replace": {"type": "string"}, "matchCase": {"type": "boolean"}, "wholeWord": {"type": "boolean"}},
            "required": ["find", "replace"], "additionalProperties": false})",
        [window](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*window);
            SearchOptions options;
            options.caseSensitive = a.value(QLatin1String("matchCase")).toBool(false);
            options.wholeWord = a.value(QLatin1String("wholeWord")).toBool(false);
            const QString find = a.value(QLatin1String("find")).toString();
            const edit::ReplaceReport report = script::await(
                doc.session().replaceAll(find, options, a.value(QLatin1String("replace")).toString()),
                "edit.replaceAll");
            if (report.replaced == 0) {
                throw ScriptError(QStringLiteral("nothing replaced: \"%1\" (found %2, skipped %3)")
                                      .arg(find)
                                      .arg(report.found)
                                      .arg(report.skipped.size()));
            }
        });
}

}  // namespace ot::ui
