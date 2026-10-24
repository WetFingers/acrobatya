// Каталог штампов (план 5.2, п. 4; эталон 3.4 «Инструмент»: Штамп — палитра категорий
// Стандартные/Динамические/Подписи/Пользовательские, из буфера, из файла; 4.7:
// %LOCALAPPDATA%\AcroBatya\stamps\ — пользовательские штампы PDF; comment-11, comment-12, comment-13,
// comment-41):
//   * свои — ресурс :/stamps (assets/stamps: stamps.json — id, файл, /Name Acrobat, надпись, цвет, строки
//     динамического; PDF по категориям «standard», «sign-here», «dynamic»);
//   * пользовательские — <каталог данных>/stamps/<категория>/<имя>.pdf (категория и имя — имена папки и
//   файла;
//     id — «user/<категория>/<имя>»): создать из PNG/JPEG (одностраничный PDF с картинкой), из страницы PDF,
//     из картинки буфера обмена (comment-41); переименовать, удалить; удаление последнего штампа категории
//     удаляет папку категории;
//   * избранное — state.sqlite stamp_favorites (миграция 6);
//   * значения динамических штампов — Identity Установок (имя или имя входа, должность, организация), дата
//     dd.MM.yyyy и время HH:mm.
// Только Qt Core/Gui (без Widgets); растр миниатюры — ядро (Document::renderPage).
#pragma once

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QString>
#include <QStringList>

#include <optional>
#include <string>
#include <vector>

#include "core/annot/stamps.h"
#include "services/settings.h"

namespace ot::stamps {

inline constexpr const char* kStandard = "standard";
inline constexpr const char* kSignHere = "sign-here";
inline constexpr const char* kDynamic = "dynamic";
inline constexpr const char* kUserPrefix = "user/";

struct StampEntry {
    QString id;        // «standard/approved», «user/Мои/Логотип»
    QString category;  // «standard», «sign-here», «dynamic» или имя пользовательской категории
    QString caption;   // надпись своего штампа (данные) или имя файла пользовательского
    QString path;      // «:/stamps/standard/approved.pdf» или путь файла
    bool builtIn = true;
    bool dynamic = false;
    std::string name;  // /Name аннотации: «#Approved»… или «#» + имя пользовательского
    std::vector<annot::StampLine> lines;

    bool valid() const noexcept { return !id.isEmpty(); }
};

class StampCatalog {
public:
    // Пользовательские — в <каталог данных>/stamps (тесты — OTTISK_DATA_DIR); root — другой корень.
    StampCatalog();
    explicit StampCatalog(QString userRoot);

    const QString& userRoot() const noexcept { return root_; }
    // Свои (по порядку stamps.json), пользовательские (категории и имена по алфавиту), все вместе.
    std::vector<StampEntry> builtIn() const;
    std::vector<StampEntry> user() const;
    std::vector<StampEntry> all() const;
    QStringList userCategories() const;
    std::optional<StampEntry> find(const QString& id) const;
    // Байты PDF штампа; нет файла — пусто.
    QByteArray pdf(const StampEntry& entry) const;
    // Источник для ядра (байты, /Name, строки шаблона).
    annot::StampSource source(const StampEntry& entry) const;
    // Размер штампа (страница PDF, пункты); не читается — 0 × 0.
    annot::StampSize size(const StampEntry& entry) const;

    // Пользовательские. Имя и категория — без \ / : * ? " < > | (заменяются на «_»), пустые — ошибка. Ошибка
    // — nullopt и текст (английский журнал) в error.
    std::optional<StampEntry> createFromFile(const QString& file, int page, const QString& category,
                                             const QString& name, QString* error = nullptr);
    std::optional<StampEntry> createFromImage(const QImage& image, const QString& category,
                                              const QString& name, QString* error = nullptr);
    std::optional<StampEntry> createFromPdf(const QByteArray& pdf, int page, const QString& category,
                                            const QString& name, QString* error = nullptr);
    std::optional<StampEntry> rename(const QString& id, const QString& name, const QString& category = {});
    bool remove(const QString& id);

    // Избранное (state.sqlite).
    QStringList favorites() const;
    bool isFavorite(const QString& id) const;
    void setFavorite(const QString& id, bool favorite);

private:
    QString root_;
};

// Безопасное имя файла и папки (без запрещённых в Windows символов); пусто — пусто.
QString safeName(const QString& name);
// Значения подстановок динамических штампов: Identity, дата dd.MM.yyyy и время HH:mm момента when.
annot::StampValues stampValues(const Settings& settings,
                               const QDateTime& when = QDateTime::currentDateTime());
// Миниатюра страницы штампа в рамке box (с полями, на прозрачном); не PDF — пустая.
QImage thumbnail(const QByteArray& pdf, int page, QSize box);

}  // namespace ot::stamps
