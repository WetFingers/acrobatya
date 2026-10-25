#include "ui/tools/forms/form_script.h"

#include <QJsonArray>
#include <QJsonObject>

#include "services/document_session.h"
#include "services/forms_ops.h"
#include "services/script_runner.h"
#include "ui/app/script_actions.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"

namespace ot::ui {

void registerFormScriptActions(ScriptRunner& runner, MainWindow& window) {
    MainWindow* w = &window;
    runner.registerAction(
        QStringLiteral("form.setValue"),
        R"({"type": "object", "properties": {
            "name": {"type": "string", "minLength": 1},
            "value": {"type": ["string", "boolean", "array"], "items": {"type": "string"}}},
            "required": ["name", "value"], "additionalProperties": false})",
        [w](const QJsonObject& a, ScriptContext&) {
            DocumentView& doc = script::document(*w);
            const QString name = a.value(QLatin1String("name")).toString();
            const QJsonValue value = a.value(QLatin1String("value"));
            forms::SetResult r;
            if (value.isArray()) {
                QStringList values;
                for (const QJsonValue& v : value.toArray()) {
                    values << v.toString();
                }
                r = script::await(forms::setValues(doc.session(), name, values), "form.setValue");
            } else if (value.isBool()) {
                const std::optional<forms::Field> field = [&] {
                    const forms::Form form = script::await(forms::read(doc.session()), "form.setValue");
                    const forms::Field* f = form.find(name.toStdString());
                    return f != nullptr ? std::optional<forms::Field>(*f) : std::nullopt;
                }();
                QString state = QStringLiteral("Off");
                if (value.toBool() && field && !field->widgets.empty()) {
                    state = QString::fromStdString(field->widgets.front().onState);
                }
                r = script::await(forms::setValue(doc.session(), name, state), "form.setValue");
            } else {
                r = script::await(forms::setValue(doc.session(), name, value.toString()), "form.setValue");
            }
            if (!r.accepted) {
                throw ScriptError(QStringLiteral("field %1: %2").arg(name, QString::fromStdString(r.error)));
            }
        });
}

}  // namespace ot::ui
