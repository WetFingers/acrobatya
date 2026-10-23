// Сценарии --script (эталон 3.18, 9.6; план 2.4b): AcroBatya.exe --script файл.json выполняет шаги
// {"cmd": "id", "args": {…}, "name": "…"} по порядку. Действие регистрируют сессии-владельцы:
// ScriptRunner::registerAction(id, jsonSchema, fn) — базовые doc.* / view.* / shot
// (ui/app/script_actions.cpp, 2.4b), инструменты — 3.4 edit.insertText, 4.6 sign.sign, 5.1 annot.create, 6.1
// form.setValue, 6.2 form.createField, 8.1 ocr.recognize; рубежи (tools/verify_r<N>.py) ничего своего не
// изобретают. Перед выполнением проверяются все шаги: неизвестный cmd или аргументы не по схеме — ошибка с
// номером и именем шага, ни одно действие не выполнено; дальше — по порядку до первой ошибки (действие
// бросает ScriptError или любое исключение). Файл — массив шагов или {"steps": [...]}. Строки аргументов
// раскрывают ${VAR} (окружение,
// ${SCRIPT_DIR}, ${OUT_DIR}); относительный путь — от каталога скрипта (ScriptContext::path).
// Схема — подмножество JSON Schema: type (строка или список), enum, minimum/maximum, minLength, properties,
// required, additionalProperties: false, items, minItems/maxItems; прочие ключи (description, default)
// пропускаются. Поток: GUI (действия трогают окно).
#pragma once

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QMap>
#include <QString>
#include <QStringList>

#include <functional>
#include <stdexcept>

namespace ot {

// Ошибка действия: текст — по-английски (журнал и вывод для рубежей).
class ScriptError : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
    explicit ScriptError(const QString& message) : std::runtime_error(message.toStdString()) {}
};

struct ScriptContext {
    QString scriptDir;  // каталог файла скрипта (runFile ставит сам)
    QString outDir;     // --out: снимки shot; пусто — текущий каталог
    int step = 0;       // выполняемый шаг, с 1
    QString cmd;        // его cmd

    // ${VAR} → окружение (и SCRIPT_DIR, OUT_DIR); неизвестная переменная — ScriptError.
    QString expand(const QString& text) const;
    // expand + относительный путь — от scriptDir (иначе — от текущего каталога), чистый абсолютный путь.
    QString path(const QString& text) const;
    // Каталог снимков (создаётся).
    QString shotsDir() const;
};

struct ScriptResult {
    bool ok = false;
    int steps = 0;       // шагов в скрипте
    int done = 0;        // выполнено успешно
    int failedStep = 0;  // номер упавшего шага, с 1 (0 — ошибка файла или нет ошибки)
    QString cmd;         // его cmd
    QString name;        // его "name"
    QString message;
    // «step 3 doc.open "открыть отчёт": no such file …» / «script: …» / «ok: 5 steps».
    QString describe() const;
};

class ScriptRunner {
public:
    using Action = std::function<void(const QJsonObject& args, ScriptContext& context)>;

    // Тот же id — замена. schema — JSON Schema аргументов (объект); пустая — аргументов нет вовсе.
    void registerAction(const QString& id, const QJsonObject& schema, Action action);
    // То же со схемой текстом JSON (для краткости в местах регистрации); испорченная схема — qFatal в Debug,
    // иначе действие без проверки аргументов.
    void registerAction(const QString& id, const char* schemaJson, Action action);
    bool hasAction(const QString& id) const { return actions_.contains(id); }
    QStringList actionIds() const { return actions_.keys(); }
    QJsonObject schema(const QString& id) const;

    ScriptResult run(const QJsonDocument& script, ScriptContext& context) const;
    ScriptResult runFile(const QString& file, ScriptContext& context) const;

    // Пусто — значение подходит; иначе «args.page: expected integer».
    static QString validate(const QJsonValue& value, const QJsonObject& schema,
                            const QString& where = QStringLiteral("args"));
    static QJsonObject parseSchema(const char* json, QString* error = nullptr);

private:
    struct Entry {
        QJsonObject schema;
        Action action;
    };
    QMap<QString, Entry> actions_;
};

}  // namespace ot
