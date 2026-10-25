// Окно app.alert сценария документа (план 6.1, п. 2; эталон 3.6: app.alert → QMessageBox через
// pdf_set_doc_event_callback; сцена form-js-alert): текст, значок и кнопки сценария (ОК, ОК/Отмена, Да/Нет,
// Да/Нет/Отмена), флажок «Больше не показывать», если сценарий его просит. Заголовок — «Сообщение JavaScript»
// (заголовок сценария — после тире). Закрытие крестиком — ответ по умолчанию (как у Acrobat).
#pragma once

#include <QMessageBox>

#include "core/forms/js_bridge.h"

namespace ot::ui {

class JsAlertDialog : public QMessageBox {
    Q_OBJECT
public:
    JsAlertDialog(const forms::AlertRequest& request, QWidget* parent = nullptr);

    // Ответ по нажатой кнопке (до нажатия — ответ по умолчанию).
    forms::AlertReply reply() const;
    // Модально (exec) и ответ.
    static forms::AlertReply ask(QWidget* parent, const forms::AlertRequest& request);

private:
    forms::AlertRequest request_;
};

}  // namespace ot::ui
