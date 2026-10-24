#include "ui/tools/certificate/sign_texts.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QLabel>
#include <QLocale>

#include "ui/shell/save_flow.h"

namespace ot::ui {

QString SignTexts::tsaStatus(sign::TsaStatus status) {
    switch (status) {
        case sign::TsaStatus::Ok:
            return {};
        case sign::TsaStatus::NetworkDisabled:
            return tr("сеть программы выключена (Установки → Диспетчер доверия)");
        case sign::TsaStatus::NetworkError:
            return tr("служба времени недоступна: нет соединения или неверный адрес");
        case sign::TsaStatus::Timeout:
            return tr("служба времени не ответила вовремя");
        case sign::TsaStatus::HttpError:
            return tr("служба времени ответила ошибкой");
        case sign::TsaStatus::BadResponse:
            return tr("ответ службы времени не похож на метку времени");
        case sign::TsaStatus::Rejected:
            return tr("служба времени отказала в метке");
        case sign::TsaStatus::VerifyFailed:
            return tr("метка времени не прошла проверку");
    }
    return {};
}

QString SignTexts::signError(const sign::SignError& error) {
    using sign::SignStatus;
    switch (error.status()) {
        case SignStatus::NotPdf:
            return tr("Подписывать можно только PDF.");
        case SignStatus::NotAllowed:
            return tr("Права документа запрещают заполнять формы и подписывать.");
        case SignStatus::NoIdentity:
            return tr("Удостоверение не найдено: сертификата нет в хранилище или файл удалён.");
        case SignStatus::NoPrivateKey:
            return tr("У сертификата нет закрытого ключа — подписать им нельзя.");
        case SignStatus::Cancelled:
            return tr("Подпись отменена: ввод PIN-кода прерван.");
        case SignStatus::KeyFailed:
            return tr("Ключ не подписал данные: проверьте смарт-карту или токен и PIN-код.");
        case SignStatus::NotSignatureField:
            return tr("Поле с таким именем есть, но это не поле подписи.");
        case SignStatus::FieldSigned:
            return tr("Это поле уже подписано.");
        case SignStatus::FieldReadOnly:
            return tr("Поле заблокировано прежней подписью.");
        case SignStatus::BadPlacement:
            return tr("Рамка подписи вне страницы.");
        case SignStatus::NoTarget:
            return tr("Выберите, куда сохранить подписанный документ.");
        case SignStatus::CmsFailed:
            return tr("Не удалось собрать подпись (CMS).");
        case SignStatus::TimestampFailed:
            return tr("Метка времени не получена: %1.").arg(tsaStatus(error.tsaStatus()));
        case SignStatus::TooLarge:
            return tr("Подпись не помещается в отведённое место: слишком длинная цепочка сертификатов.");
        case SignStatus::CannotCertify:
            return tr("Сертифицировать можно только документ без подписей.");
        case SignStatus::BadAppearance:
            return tr("Картинка или логотип внешнего вида не читаются.");
        case SignStatus::SaveFailed:
            return SaveFlow::errorText(error.saveError());
    }
    return QString::fromUtf8(error.what());
}

QString SignTexts::identityError(sign::IdentityStatus status) {
    using sign::IdentityStatus;
    switch (status) {
        case IdentityStatus::NotFound:
            return tr("Удостоверение не найдено.");
        case IdentityStatus::Exists:
            return tr("Это удостоверение уже есть в списке.");
        case IdentityStatus::NeedPassword:
            return tr("Введите пароль удостоверения.");
        case IdentityStatus::BadPassword:
            return tr("Неверный пароль удостоверения.");
        case IdentityStatus::Damaged:
            return tr("Файл удостоверения повреждён или в нём нет закрытого ключа.");
        case IdentityStatus::FileMissing:
            return tr("Файл удостоверения удалён с диска.");
        case IdentityStatus::WindowsStore:
            return tr("Это сертификат хранилища Windows: пароль не нужен.");
        case IdentityStatus::IoError:
            return tr("Не удалось записать удостоверение: проверьте место на диске.");
    }
    return {};
}

QString SignTexts::warningKind(sign::WarningKind kind) {
    using sign::WarningKind;
    switch (kind) {
        case WarningKind::JavaScript:
            return tr("JavaScript может изменить вид и содержимое документа");
        case WarningKind::Launch:
            return tr("Действие запускает программу");
        case WarningKind::ExternalLink:
            return tr("Ссылка на сайт");
        case WarningKind::ExternalFile:
            return tr("Ссылка на внешний файл или отправка данных");
        case WarningKind::Multimedia:
            return tr("Мультимедиа или 3D-содержимое");
        case WarningKind::EmbeddedFile:
            return tr("Вложенный файл");
        case WarningKind::Xfa:
            return tr("Форма XFA: её вид строится при открытии");
        case WarningKind::NeedAppearances:
            return tr("Вид полей формы перестраивается при открытии");
    }
    return {};
}

QString SignTexts::warningPlace(const sign::DocumentWarning& w) {
    QString place = w.page >= 0 ? tr("стр. %1").arg(w.page + 1) : tr("документ");
    if (!w.where.empty()) {
        place += QStringLiteral(" · ") + QString::fromStdString(w.where);
    }
    return place;
}

QString SignTexts::permission(sign::MdpPermission p) {
    using sign::MdpPermission;
    switch (p) {
        case MdpPermission::NoChanges:
            return tr("Изменения не разрешены");
        case MdpPermission::FormFilling:
            return tr("Заполнение форм и подпись");
        case MdpPermission::Annotations:
            return tr("Комментарии, заполнение форм и подпись");
        case MdpPermission::None:
            break;
    }
    return tr("Не ограничены (утверждающая подпись)");
}

QString SignTexts::verdict(sign::Verdict v) {
    using sign::Verdict;
    switch (v) {
        case Verdict::Valid:
            return tr("действительна");
        case Verdict::ValidWithChanges:
            return tr("действительна, внесены разрешённые изменения");
        case Verdict::ValidUntrusted:
            return tr("подпись цела, подписант не доверен");
        case Verdict::Unknown:
            return tr("состояние неизвестно");
        case Verdict::Invalid:
            return tr("недействительна");
    }
    return {};
}

Tone SignTexts::verdictTone(sign::Verdict v) {
    using sign::Verdict;
    switch (v) {
        case Verdict::Valid:
        case Verdict::ValidWithChanges:
            return Tone::Ok;
        case Verdict::ValidUntrusted:
        case Verdict::Unknown:
            return Tone::Warn;
        case Verdict::Invalid:
            return Tone::Danger;
    }
    return Tone::Warn;
}

QString SignTexts::verdictIcon(sign::Verdict v, bool certification) {
    using sign::Verdict;
    switch (v) {
        case Verdict::Valid:
        case Verdict::ValidWithChanges:
            return certification ? QStringLiteral("badge-check") : QStringLiteral("shield-check");
        case Verdict::ValidUntrusted:
            return QStringLiteral("shield-alert");
        case Verdict::Unknown:
            return QStringLiteral("shield-question");
        case Verdict::Invalid:
            return QStringLiteral("shield-x");
    }
    return QStringLiteral("shield-question");
}

QString SignTexts::issue(sign::Issue i) {
    using sign::Issue;
    switch (i) {
        case Issue::NoByteRange:
            return tr("Нет диапазона подписанных байтов (/ByteRange).");
        case Issue::BadByteRange:
            return tr("Диапазон подписанных байтов неверен: подпись повреждена или подделана.");
        case Issue::NoContents:
            return tr("Значение подписи пусто.");
        case Issue::CmsDamaged:
            return tr("Подпись повреждена: данные CMS не читаются.");
        case Issue::UnsupportedSubFilter:
            return tr("Формат подписи не поддерживается.");
        case Issue::UnsupportedAlgorithm:
            return tr("Алгоритм подписи или хэша не поддерживается.");
        case Issue::NoSignerCertificate:
            return tr("В подписи нет сертификата подписанта.");
        case Issue::NoMessageDigest:
            return tr("В подписи нет хэша документа.");
        case Issue::DigestMismatch:
            return tr("Документ изменён или повреждён после подписи.");
        case Issue::SignatureMismatch:
            return tr("Подпись не соответствует сертификату подписанта.");
        case Issue::EssMismatch:
            return tr("Подпись ссылается на другой сертификат подписанта.");
        case Issue::ForbiddenAlgorithm:
            return tr("Алгоритм подписи небезопасен (MD5) — подпись не принимается.");
        case Issue::WeakAlgorithm:
            return tr("Устаревший алгоритм (SHA-1).");
        case Issue::ModifiedAfterSigning:
            return tr("После подписи в документ вносились изменения.");
        case Issue::Untrusted:
            return tr("Удостоверение подписанта не доверено: его нет среди доверенных сертификатов.");
        case Issue::CertExpired:
            return tr("Сертификат в цепочке истёк к моменту проверки.");
        case Issue::CertNotYetValid:
            return tr("Сертификат в цепочке ещё не действовал к моменту проверки.");
        case Issue::BadKeyUsage:
            return tr("Сертификат не предназначен для подписи документов.");
        case Issue::ChainBroken:
            return tr("Цепочка сертификатов нарушена.");
        case Issue::Revoked:
            return tr("Сертификат отозван до момента подписи.");
        case Issue::RevocationUnknown:
            return tr("Не удалось проверить отзыв сертификата.");
        case Issue::RevocationUnchecked:
            return tr("Отзыв сертификата не проверялся.");
        case Issue::TimestampInvalid:
            return tr("Метка времени подписи повреждена.");
        case Issue::TimestampUntrusted:
            return tr("Служба времени не доверена: время подписи взято с компьютера подписавшего.");
        case Issue::TimestampExpired:
            return tr("Сертификат службы времени истёк.");
        case Issue::ClaimedTimeAfterTimestamp:
            return tr("Время подписи позже времени метки.");
        case Issue::AllowedChanges:
            return tr("После подписи внесены разрешённые изменения.");
        case Issue::DisallowedChanges:
            return tr("После подписи внесены изменения, которые не разрешены.");
        case Issue::LockedFieldChanged:
            return tr("Изменено поле, заблокированное подписью.");
    }
    return {};
}

QString SignTexts::changeKind(sign::ChangeKind kind) {
    using sign::ChangeKind;
    switch (kind) {
        case ChangeKind::Dss:
            return tr("Добавлены сведения для проверки (LTV)");
        case ChangeKind::DocTimestamp:
            return tr("Добавлена метка времени документа");
        case ChangeKind::Metadata:
            return tr("Изменены свойства документа");
        case ChangeKind::Signature:
            return tr("Подписано поле подписи");
        case ChangeKind::SignatureField:
            return tr("Добавлено поле подписи");
        case ChangeKind::FormField:
            return tr("Заполнено поле формы");
        case ChangeKind::LockedField:
            return tr("Изменено заблокированное поле");
        case ChangeKind::Annotation:
            return tr("Комментарий добавлен, изменён или удалён");
        case ChangeKind::Page:
            return tr("Изменена страница");
        case ChangeKind::FieldStructure:
            return tr("Поле формы добавлено, удалено или переименовано");
        case ChangeKind::Other:
            return tr("Прочие изменения документа");
    }
    return {};
}

QString SignTexts::timeSource(sign::TimeSource source) {
    using sign::TimeSource;
    switch (source) {
        case TimeSource::Timestamp:
            return tr("Подпись содержит встроенную метку времени");
        case TimeSource::Claimed:
            return tr("Время подписи взято с часов компьютера подписавшего");
        case TimeSource::Now:
            break;
    }
    return tr("Проверено на текущее время");
}

QString SignTexts::revocation(sign::RevocationStatus status) {
    using sign::RevocationStatus;
    switch (status) {
        case RevocationStatus::Good:
            return tr("не отозван");
        case RevocationStatus::Revoked:
            return tr("отозван");
        case RevocationStatus::Unknown:
            break;
    }
    return tr("не проверен");
}

QString SignTexts::padesLevel(sign::PadesLevel level) {
    using sign::PadesLevel;
    switch (level) {
        case PadesLevel::B:
            return QStringLiteral("PAdES B-B");
        case PadesLevel::T:
            return QStringLiteral("PAdES B-T");
        case PadesLevel::LT:
            return QStringLiteral("PAdES B-LT");
        case PadesLevel::LTA:
            return QStringLiteral("PAdES B-LTA");
        case PadesLevel::None:
            break;
    }
    return {};
}

QString SignTexts::kind(sign::SignatureKind k) {
    using sign::SignatureKind;
    switch (k) {
        case SignatureKind::Certification:
            return tr("Сертифицирующая подпись");
        case SignatureKind::DocTimestamp:
            return tr("Метка времени документа");
        case SignatureKind::Approval:
            break;
    }
    return tr("Подпись");
}

QString SignTexts::documentStatus(sign::DocumentStatus status) {
    using sign::DocumentStatus;
    switch (status) {
        case DocumentStatus::Valid:
            return tr("Подписано, все подписи действительны.");
        case DocumentStatus::Certified:
            return tr("Сертифицировано: подписи действительны, документ подтверждён автором.");
        case DocumentStatus::Problems:
            return tr("Проблемы с подписью: хотя бы одна подпись требует внимания.");
        case DocumentStatus::Invalid:
            return tr("Хотя бы одна подпись недействительна.");
        case DocumentStatus::NotSigned:
            break;
    }
    return {};
}

sign::AppearanceLabels SignTexts::appearanceLabels() {
    sign::AppearanceLabels l;
    l.signedBy = tr("Подписано:").toStdString();
    l.dn = tr("DN:").toStdString();
    l.reason = tr("Причина:").toStdString();
    l.location = tr("Место:").toStdString();
    l.date = tr("Дата:").toStdString();
    return l;
}

QStringList SignTexts::standardReasons() {
    return {tr("Я являюсь автором этого документа"),
            tr("Я просмотрел этот документ"),
            tr("Я утверждаю этот документ"),
            tr("Я подтверждаю точность и целостность этого документа"),
            tr("Я соглашаюсь с условиями, определяемыми размещением моей подписи в этом документе"),
            tr("Я соглашаюсь с указанными частями этого документа")};
}

QString SignTexts::dateTime(const std::string& iso) {
    if (iso.empty()) {
        return {};
    }
    QDateTime t = QDateTime::fromString(QString::fromStdString(iso), Qt::ISODate);
    if (!t.isValid()) {
        return QString::fromStdString(iso);
    }
    return QLocale().toString(t.toLocalTime(), QStringLiteral("dd.MM.yyyy HH:mm:ss"));
}

void SignTexts::setNote(QLabel* label, Tone tone) {
    label->setProperty("note", toneName(tone));
    repolish(label);
}

void SignTexts::clearNote(QLabel* label) {
    label->setProperty("note", QVariant());
    repolish(label);
}

}  // namespace ot::ui
