// Палитра штампов (план 5.2, п. 4; эталон 3.4 «Инструмент»: Штамп — палитра категорий Стандартные/Динамические/
// Подписи/Пользовательские, из буфера, из файла; 6.3 «Палитра штампов (Создать/Управлять, Identity)»;
// comment-11, comment-12, comment-13, comment-41): слева — категории («Избранное», «Стандартные», «Подписать
// здесь», «Динамические» и пользовательские — папки stamps\), справа — миниатюры штампов категории; щелчок по
// штампу — выбрать его для инструмента «Штамп» (stampChosen) и закрыть палитру. Внизу: «Создать…» (PNG, JPEG или
// первая страница PDF → категория и имя → stamps\<категория>\<имя>.pdf), «Из буфера обмена» (картинка буфера —
// штампом на одну постановку, comment-41), «В избранное», «Переименовать…», «Удалить» (только пользовательские),
// «Identity…» (Установки → Комментирование: имя, должность, организация для динамических штампов). Каталог —
// services/stamps_catalog.h; файловый диалог, имя и подтверждение удаления тесты подменяют.
#pragma once

#include <QDialog>
#include <QImage>
#include <QString>
#include <QStringList>

#include <functional>
#include <optional>
#include <utility>

#include "services/stamps_catalog.h"

class QLabel;
class QListWidget;
class QListWidgetItem;
class QPushButton;

namespace ot::ui {

class StampsPalette : public QDialog {
    Q_OBJECT
public:
    // Категория «Избранное».
    static constexpr const char* kFavorites = "favorites";
    // Создание: файл; категория и имя ({категория, имя}; nullopt — отмена); подтверждение удаления.
    using FilePrompt = std::function<QString()>;
    using NamePrompt =
        std::function<std::optional<std::pair<QString, QString>>(const QString& name, const QStringList& categories)>;
    using Confirm = std::function<bool(const QString& title)>;

    explicit StampsPalette(QWidget* parent = nullptr);

    // Заголовок категории и штампа по-русски (пользовательские — как названы).
    static QString categoryTitle(const QString& category);
    static QString stampTitle(const stamps::StampEntry& entry);

    QListWidget* categoryList() const { return categories_; }
    QListWidget* stampList() const { return stamps_; }
    QPushButton* createButton() const { return create_; }
    QPushButton* clipboardButton() const { return clipboard_; }
    QPushButton* favoriteButton() const { return favorite_; }
    QPushButton* renameButton() const { return rename_; }
    QPushButton* deleteButton() const { return delete_; }
    QPushButton* identityButton() const { return identity_; }
    QLabel* noteLabel() const { return note_; }

    // Категория: kFavorites, «standard», «sign-here», «dynamic» или «user:<имя>».
    void showCategory(const QString& category);
    QString currentCategory() const;
    // Штамп списка (id); выбранный.
    void selectStamp(const QString& id);
    QString selectedStamp() const;
    // Перечитать каталог (после создания, переименования, удаления).
    void reload();

    void setFilePrompt(FilePrompt prompt) { filePrompt_ = std::move(prompt); }
    void setNamePrompt(NamePrompt prompt) { namePrompt_ = std::move(prompt); }
    void setConfirm(Confirm confirm) { confirm_ = std::move(confirm); }
    stamps::StampCatalog& catalog() { return catalog_; }

    // Действия кнопок (тесты зовут напрямую).
    void createStamp();
    void useClipboard();
    void toggleFavorite();
    void renameStamp();
    void deleteStamp();

signals:
    void stampChosen(const QString& id);
    void clipboardStampChosen(const QImage& image);
    void identityRequested();

private:
    void fillCategories();
    void fillStamps();
    void updateButtons();
    std::optional<stamps::StampEntry> selectedEntry() const;
    std::optional<std::pair<QString, QString>> askName(const QString& name);

    stamps::StampCatalog catalog_;
    QListWidget* categories_ = nullptr;
    QListWidget* stamps_ = nullptr;
    QPushButton* create_ = nullptr;
    QPushButton* clipboard_ = nullptr;
    QPushButton* favorite_ = nullptr;
    QPushButton* rename_ = nullptr;
    QPushButton* delete_ = nullptr;
    QPushButton* identity_ = nullptr;
    QLabel* note_ = nullptr;
    FilePrompt filePrompt_;
    NamePrompt namePrompt_;
    Confirm confirm_;
};

}  // namespace ot::ui
