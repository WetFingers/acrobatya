// Строки интерфейса подписей (план 4.6; эталон 3.8, 6.3 «Подписи»): коды core/sign и services → русские
// тексты по терминам Acrobat (tr в контексте ot::ui::SignTexts). Общие для инструмента «Использовать
// сертификат», его диалогов, панели «Подписи», свойств подписи, просмотра сертификата и Установок. Класс —
// QObject только ради tr() с контекстом ot::ui::SignTexts (lupdate); экземпляров нет.
#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

#include "core/sign/appearance.h"
#include "core/sign/changes.h"
#include "core/sign/docmdp.h"
#include "core/sign/identity.h"
#include "core/sign/revocation.h"
#include "core/sign/signer.h"
#include "core/sign/tsa.h"
#include "core/sign/verifier.h"
#include "core/sign/warnings_scan.h"
#include "ui/widgets/tone.h"

class QLabel;

namespace ot::ui {

class SignTexts : public QObject {
    Q_OBJECT
public:
    // Ошибка подписания (SignError: статус, запись, служба времени).
    static QString signError(const sign::SignError& error);
    static QString identityError(sign::IdentityStatus status);
    static QString tsaStatus(sign::TsaStatus status);

    // «Проверить документ» (sign-17): что найдено.
    static QString warningKind(sign::WarningKind kind);
    static QString warningPlace(const sign::DocumentWarning& warning);

    // Права после сертификации (DocMDP P).
    static QString permission(sign::MdpPermission p);

    // Статус подписи — как в панели «Подписи» Acrobat: «действительна», «подписант не доверен»…
    static QString verdict(sign::Verdict verdict);
    // Тон и иконка статуса (Lucide): Valid — ok/shield-check, Untrusted — warn/shield-alert, Unknown — warn/
    // shield-question, Invalid — danger/shield-x; сертифицирующая цела — info/badge-check.
    static Tone verdictTone(sign::Verdict verdict);
    static QString verdictIcon(sign::Verdict verdict, bool certification = false);
    // Причина (Issue) для свойств и «Подробнее».
    static QString issue(sign::Issue issue);
    static QString changeKind(sign::ChangeKind kind);
    static QString timeSource(sign::TimeSource source);
    static QString revocation(sign::RevocationStatus status);
    static QString padesLevel(sign::PadesLevel level);
    static QString kind(sign::SignatureKind kind);
    // Полоса над документом.
    static QString documentStatus(sign::DocumentStatus status);

    // Ярлыки строк внешнего вида подписи по-русски («Подписано:», «Причина:»…).
    static sign::AppearanceLabels appearanceLabels();
    // Шесть стандартных причин подписи (Acrobat).
    static QStringList standardReasons();

    // Дата ISO 8601 (UTC) → местное время «24.10.2026 17:05:12».
    static QString dateTime(const std::string& iso);

    // Строка-заметка диалога (QLabel[note=…]) тона tone; clearNote — снова обычный текст.
    static void setNote(QLabel* label, Tone tone);
    static void clearNote(QLabel* label);
};

}  // namespace ot::ui
