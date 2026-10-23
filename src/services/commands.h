// Реестр команд и клавиш (эталон 6.8; view-32 — ядро реестра, view-51 — клавиши Acrobat): у команды id
// `группа.действие`, название (строку tr() даёт ui), клавиши по умолчанию — из таблицы эталона 6.8, иконка,
// действие, контекст when и место в меню. Меню, QAction/клавиши окна, палитра команд (2.4b), «Горячие
// клавиши» (9.2) и --script (2.4b) берут команды отсюда; страж unit.guard.hotkeys сверяет реестр с
// таблицей 6.8 (AcroBatya.exe --commands-list → JSON).
//
// Контекст when: пусто — окно целиком; "view" — фокус в документе (клавиши, которые иначе мешали бы полям
// ввода); прочие ("readAloud.active", "prepareForm"…) — режимы: команда доступна, пока контекст включён
// (setContextActive), и её клавиша вытесняет такую же клавишу команды окна (Ctrl+Shift+E — «Извлечь» и «стоп
// чтения вслух»). Конфликт клавиш допустим только у команд с разными when (conflicts()).
// Однобуквенные ускорители (H, V, Z…, singleKey) действуют, только если включена настройка
// hotkeys/singleKeyAccelerators (выкл. по умолчанию); переназначенные клавиши — hotkeys/<id> в settings.ini.
// Заглушка будущего этапа (stage > 0) видна в меню серой с подсказкой «появится в версии 0.N».
// Поток: GUI.
#pragma once

#include <QJsonArray>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QSet>
#include <QString>
#include <QStringList>

#include <functional>
#include <vector>

namespace ot {

struct Command {
    QString id;
    QString title;
    QList<QKeySequence> shortcuts;  // по умолчанию (таблица 6.8)
    QString icon;
    std::function<void()> run;
    QString when;
    std::function<bool()> enabled;  // пусто — всегда
    QString menu;                   // "file", "view/zoom"…; пусто — не в меню
    bool separatorBefore = false;
    bool singleKey = false;
    bool checkable = false;
    std::function<bool()> checked;
    int stage = 0;  // > 0 — появится в версии 0.stage
};

class CommandRegistry : public QObject {
    Q_OBJECT
public:
    static constexpr char kSingleKeyKey[] = "hotkeys/singleKeyAccelerators";
    static constexpr char kOverrideGroup[] = "hotkeys";
    static constexpr char kViewContext[] = "view";

    struct Conflict {
        QKeySequence key;
        QStringList ids;
    };

    explicit CommandRegistry(QObject* parent = nullptr);

    // Тот же id — замена (место в порядке то же).
    void add(Command command);
    bool remove(const QString& id);
    const Command* find(const QString& id) const;
    std::vector<const Command*> all() const;
    std::vector<const Command*> inMenu(const QString& menu) const;
    int count() const { return static_cast<int>(commands_.size()); }

    bool isEnabled(const QString& id) const;
    bool isChecked(const QString& id) const;
    // Выполнить, если доступна; false — нет такой, заглушка или недоступна.
    bool trigger(const QString& id);

    // Клавиши, которые действуют сейчас: переназначение из ini, однобуквенные — по настройке, клавиша команды
    // окна вытеснена командой включённого режима с той же клавишей.
    QList<QKeySequence> effectiveShortcuts(const QString& id) const;
    QList<QKeySequence> shortcuts(const QString& id) const;  // без учёта настройки и режимов
    bool singleKeysEnabled() const { return singleKeys_; }
    void setSingleKeysEnabled(bool on);
    void loadSettings();  // однобуквенные и переназначения из settings.ini
    void setContextActive(const QString& when, bool active);
    bool isContextActive(const QString& when) const;

    // Одинаковые клавиши у команд с одинаковым when (по клавишам по умолчанию и переназначениям).
    QList<Conflict> conflicts() const;
    // Для стража и диалога клавиш: id, title, keys (PortableText), when, menu, singleKey, stage.
    QJsonArray toJson() const;

signals:
    void changed();
    void shortcutsChanged();

private:
    std::vector<Command> commands_;
    QHash<QString, QList<QKeySequence>> overrides_;
    QSet<QString> contexts_;
    bool singleKeys_ = false;
};

}  // namespace ot
