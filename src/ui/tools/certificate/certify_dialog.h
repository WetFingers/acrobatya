// «Сертифицировать» (план 4.6, п. 1; эталон 3.8 «Инструмент»; sign-16): диалог «Подписать как» в режиме
// Certify — сверху объяснение (сертифицирующая подпись подтверждает содержимое и задаёт, что можно менять
// после неё; ставится первой — документ без подписей), вместо «Заблокировать документ» — «Разрешённые
// изменения»: никакие (P=1) / заполнение форм и подпись (P=2, по умолчанию) / ещё и комментарии (P=3).
// Видимая (рамка на странице) и невидимая.
#pragma once

#include "ui/tools/certificate/sign_dialog.h"

namespace ot::ui {

class CertifyDialog : public SignDialog {
    Q_OBJECT
public:
    CertifyDialog(bool visible, QSizeF rect, const QString& documentTitle, QWidget* parent = nullptr);
};

}  // namespace ot::ui
