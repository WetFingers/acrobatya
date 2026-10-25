// Сохранённые подпись и инициалы Fill & Sign (план 5.3, п. 1; эталон 3.5 «сохранённые подпись и инициалы —
// %LOCALAPPDATA%\AcroBatya\signatures\{signature,initials}.png», 4.7; sign-02 — локально, без облака):
// по одной картинке PNG с альфой на вид (подпись, инициалы) в <каталог данных>/signatures (тесты —
// OTTISK_DATA_DIR); запись — через QSaveFile (оборванная запись не портит прежнюю), удалить — крестик в меню
// «Подпись ▾»/«Инициалы ▾». Подпись Fill & Sign — не криптографическая: картинка штампа, не удостоверение.
// Только Qt Core/Gui (без Widgets).
#pragma once

#include <QByteArray>
#include <QImage>
#include <QString>

namespace ot::fillsign {

enum class SignatureKind { Signature, Initials };

// Имя файла без расширения и ключ: "signature", "initials".
QString kindName(SignatureKind kind);

class SavedSignatures {
public:
    // <каталог данных>/signatures; root — другой корень (тесты).
    SavedSignatures();
    explicit SavedSignatures(QString root);

    const QString& root() const noexcept { return root_; }
    QString path(SignatureKind kind) const;
    bool has(SignatureKind kind) const;
    // Картинка (ARGB32); нет или не читается — пустая.
    QImage load(SignatureKind kind) const;
    // Байты PNG как на диске; нет — пусто.
    QByteArray png(SignatureKind kind) const;
    // Записать PNG с альфой (каталог создаётся); пустая картинка или сбой записи — false и английский текст.
    bool save(SignatureKind kind, const QImage& image, QString* error = nullptr);
    // Удалить; не было — false.
    bool remove(SignatureKind kind);

private:
    QString root_;
};

}  // namespace ot::fillsign
