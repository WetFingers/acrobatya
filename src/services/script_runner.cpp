#include "services/script_runner.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonParseError>
#include <QRegularExpression>

#include <spdlog/spdlog.h>

namespace ot {

namespace {

QString typeOf(const QJsonValue& v) {
    switch (v.type()) {
        case QJsonValue::Null:
            return QStringLiteral("null");
        case QJsonValue::Bool:
            return QStringLiteral("boolean");
        case QJsonValue::Double: {
            const double d = v.toDouble();
            return d == static_cast<double>(static_cast<qint64>(d)) ? QStringLiteral("integer")
                                                                    : QStringLiteral("number");
        }
        case QJsonValue::String:
            return QStringLiteral("string");
        case QJsonValue::Array:
            return QStringLiteral("array");
        case QJsonValue::Object:
            return QStringLiteral("object");
        case QJsonValue::Undefined:
            break;
    }
    return QStringLiteral("undefined");
}

bool typeMatches(const QString& actual, const QString& wanted) {
    return actual == wanted || (wanted == QLatin1String("number") && actual == QLatin1String("integer"));
}

QString shortJson(const QJsonValue& v) {
    QJsonArray wrap{v};
    QString s = QString::fromUtf8(QJsonDocument(wrap).toJson(QJsonDocument::Compact));
    s = s.mid(1, s.size() - 2);
    return s.size() > 60 ? s.left(57) + QStringLiteral("...") : s;
}

const QStringList kStepKeys = {QStringLiteral("cmd"), QStringLiteral("args"), QStringLiteral("name"),
                               QStringLiteral("comment")};

}  // namespace

// ---------------------------------------------------------------- ScriptContext

QString ScriptContext::expand(const QString& text) const {
    static const QRegularExpression var(QStringLiteral(R"(\$\{([A-Za-z_][A-Za-z0-9_]*)\})"));
    QString out;
    qsizetype last = 0;
    for (auto it = var.globalMatch(text); it.hasNext();) {
        const QRegularExpressionMatch m = it.next();
        out += text.mid(last, m.capturedStart() - last);
        const QString name = m.captured(1);
        QString value;
        if (name == QLatin1String("SCRIPT_DIR")) {
            value = scriptDir;
        } else if (name == QLatin1String("OUT_DIR")) {
            value = shotsDir();
        } else if (qEnvironmentVariableIsSet(name.toLatin1().constData())) {
            value = qEnvironmentVariable(name.toLatin1().constData());
        } else {
            throw ScriptError(QStringLiteral("unknown variable ${") + name + QLatin1Char('}'));
        }
        out += value;
        last = m.capturedEnd();
    }
    out += text.mid(last);
    return out;
}

QString ScriptContext::path(const QString& text) const {
    const QString expanded = expand(text);
    if (expanded.isEmpty()) {
        return expanded;
    }
    const QString base = scriptDir.isEmpty() ? QDir::currentPath() : scriptDir;
    return QDir::cleanPath(QDir(base).absoluteFilePath(expanded));
}

QString ScriptContext::shotsDir() const {
    const QString dir = outDir.isEmpty() ? QDir::currentPath() : QDir(outDir).absolutePath();
    QDir().mkpath(dir);
    return QDir::cleanPath(dir);
}

// ---------------------------------------------------------------- ScriptResult

QString ScriptResult::describe() const {
    if (ok) {
        return QStringLiteral("ok: %1 steps").arg(done);
    }
    if (failedStep == 0) {
        return QStringLiteral("script: ") + message;
    }
    QString where =
        QStringLiteral("step %1 %2").arg(failedStep).arg(cmd.isEmpty() ? QStringLiteral("?") : cmd);
    if (!name.isEmpty()) {
        where += QStringLiteral(" \"") + name + QLatin1Char('"');
    }
    return where + QStringLiteral(": ") + message;
}

// ---------------------------------------------------------------- ScriptRunner

void ScriptRunner::registerAction(const QString& id, const QJsonObject& schema, Action action) {
    actions_.insert(id, Entry{schema, std::move(action)});
}

void ScriptRunner::registerAction(const QString& id, const char* schemaJson, Action action) {
    QString error;
    const QJsonObject schema = parseSchema(schemaJson, &error);
    if (!error.isEmpty()) {
        spdlog::error("script: schema of {} is broken: {}", id.toStdString(), error.toStdString());
        Q_ASSERT_X(false, "ScriptRunner::registerAction", "broken schema");
    }
    registerAction(id, schema, std::move(action));
}

QJsonObject ScriptRunner::schema(const QString& id) const {
    const auto it = actions_.constFind(id);
    return it == actions_.constEnd() ? QJsonObject() : it->schema;
}

QJsonObject ScriptRunner::parseSchema(const char* json, QString* error) {
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray(json), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        if (error != nullptr) {
            *error =
                pe.error != QJsonParseError::NoError ? pe.errorString() : QStringLiteral("not an object");
        }
        return {};
    }
    return doc.object();
}

QString ScriptRunner::validate(const QJsonValue& value, const QJsonObject& schema, const QString& where) {
    const QJsonValue type = schema.value(QLatin1String("type"));
    const QString actual = typeOf(value);
    if (type.isString() || type.isArray()) {
        QStringList wanted;
        if (type.isString()) {
            wanted << type.toString();
        } else {
            for (const QJsonValue& t : type.toArray()) {
                wanted << t.toString();
            }
        }
        bool any = false;
        for (const QString& w : wanted) {
            any = any || typeMatches(actual, w);
        }
        if (!any) {
            return QStringLiteral("%1: expected %2, got %3")
                .arg(where, wanted.join(QLatin1Char('|')), actual);
        }
    }
    if (schema.contains(QLatin1String("enum"))) {
        const QJsonArray options = schema.value(QLatin1String("enum")).toArray();
        if (!options.contains(value)) {
            QStringList names;
            for (const QJsonValue& o : options) {
                names << shortJson(o);
            }
            return QStringLiteral("%1: %2 is not one of %3")
                .arg(where, shortJson(value), names.join(QLatin1Char('|')));
        }
    }
    if (value.isDouble()) {
        const double d = value.toDouble();
        if (schema.contains(QLatin1String("minimum")) &&
            d < schema.value(QLatin1String("minimum")).toDouble()) {
            return QStringLiteral("%1: %2 is less than %3")
                .arg(where, shortJson(value), shortJson(schema.value(QLatin1String("minimum"))));
        }
        if (schema.contains(QLatin1String("maximum")) &&
            d > schema.value(QLatin1String("maximum")).toDouble()) {
            return QStringLiteral("%1: %2 is more than %3")
                .arg(where, shortJson(value), shortJson(schema.value(QLatin1String("maximum"))));
        }
    }
    if (value.isString() && schema.contains(QLatin1String("minLength")) &&
        value.toString().size() < schema.value(QLatin1String("minLength")).toInt()) {
        return QStringLiteral("%1: string is shorter than %2")
            .arg(where)
            .arg(schema.value(QLatin1String("minLength")).toInt());
    }
    if (value.isObject()) {
        const QJsonObject obj = value.toObject();
        const QJsonObject props = schema.value(QLatin1String("properties")).toObject();
        for (const QJsonValue& r : schema.value(QLatin1String("required")).toArray()) {
            if (!obj.contains(r.toString())) {
                return QStringLiteral("%1.%2: required").arg(where, r.toString());
            }
        }
        for (auto it = obj.constBegin(); it != obj.constEnd(); ++it) {
            const QString sub = where + QLatin1Char('.') + it.key();
            if (props.contains(it.key())) {
                const QString e = validate(it.value(), props.value(it.key()).toObject(), sub);
                if (!e.isEmpty()) {
                    return e;
                }
            } else if (schema.value(QLatin1String("additionalProperties")) == QJsonValue(false)) {
                return QStringLiteral("%1: unknown argument").arg(sub);
            }
        }
    }
    if (value.isArray()) {
        const QJsonArray arr = value.toArray();
        if (schema.contains(QLatin1String("minItems")) &&
            arr.size() < schema.value(QLatin1String("minItems")).toInt()) {
            return QStringLiteral("%1: fewer than %2 items")
                .arg(where)
                .arg(schema.value(QLatin1String("minItems")).toInt());
        }
        if (schema.contains(QLatin1String("maxItems")) &&
            arr.size() > schema.value(QLatin1String("maxItems")).toInt()) {
            return QStringLiteral("%1: more than %2 items")
                .arg(where)
                .arg(schema.value(QLatin1String("maxItems")).toInt());
        }
        if (schema.contains(QLatin1String("items"))) {
            const QJsonObject items = schema.value(QLatin1String("items")).toObject();
            for (qsizetype i = 0; i < arr.size(); ++i) {
                const QString e = validate(arr.at(i), items, QStringLiteral("%1[%2]").arg(where).arg(i));
                if (!e.isEmpty()) {
                    return e;
                }
            }
        }
    }
    return {};
}

ScriptResult ScriptRunner::run(const QJsonDocument& script, ScriptContext& context) const {
    ScriptResult result;
    QJsonArray steps;
    if (script.isArray()) {
        steps = script.array();
    } else if (script.isObject() && script.object().value(QLatin1String("steps")).isArray()) {
        steps = script.object().value(QLatin1String("steps")).toArray();
    } else {
        result.message = QStringLiteral("expected an array of steps or {\"steps\": [...]}");
        return result;
    }
    result.steps = static_cast<int>(steps.size());

    // Сначала — все шаги: имя действия и аргументы по схеме (ничего не выполнено, если хоть один неверен).
    auto fail = [&](int index, const QJsonObject& step, const QString& message) {
        result.failedStep = index + 1;
        result.cmd = step.value(QLatin1String("cmd")).toString();
        result.name = step.value(QLatin1String("name")).toString();
        result.message = message;
        spdlog::error("script: {}", result.describe().toStdString());
        return result;
    };
    for (int i = 0; i < steps.size(); ++i) {
        if (!steps.at(i).isObject()) {
            return fail(i, {}, QStringLiteral("a step is an object {\"cmd\": \"id\", \"args\": {...}}"));
        }
        const QJsonObject step = steps.at(i).toObject();
        for (auto it = step.constBegin(); it != step.constEnd(); ++it) {
            if (!kStepKeys.contains(it.key())) {
                return fail(i, step, QStringLiteral("unknown step key \"") + it.key() + QLatin1Char('"'));
            }
        }
        const QJsonValue cmd = step.value(QLatin1String("cmd"));
        if (!cmd.isString() || cmd.toString().isEmpty()) {
            return fail(i, step, QStringLiteral("step has no \"cmd\""));
        }
        const auto entry = actions_.constFind(cmd.toString());
        if (entry == actions_.constEnd()) {
            return fail(i, step, QStringLiteral("unknown command"));
        }
        const QJsonValue args = step.value(QLatin1String("args"));
        if (!args.isUndefined() && !args.isObject()) {
            return fail(i, step, QStringLiteral("args: expected object, got ") + typeOf(args));
        }
        const QJsonObject argsObj = args.toObject();
        QString error;
        if (entry->schema.isEmpty()) {
            if (!argsObj.isEmpty()) {
                error = QStringLiteral("args: %1 takes no arguments").arg(cmd.toString());
            }
        } else {
            error = validate(argsObj, entry->schema);
        }
        if (!error.isEmpty()) {
            return fail(i, step, error);
        }
    }

    for (int i = 0; i < steps.size(); ++i) {
        const QJsonObject step = steps.at(i).toObject();
        context.step = i + 1;
        context.cmd = step.value(QLatin1String("cmd")).toString();
        const Entry& entry = actions_.value(context.cmd);
        spdlog::info("script: step {} {}", context.step, context.cmd.toStdString());
        try {
            entry.action(step.value(QLatin1String("args")).toObject(), context);
        } catch (const std::exception& e) {
            return fail(i, step, QString::fromUtf8(e.what()));
        }
        result.done = i + 1;
    }
    result.ok = true;
    return result;
}

ScriptResult ScriptRunner::runFile(const QString& file, ScriptContext& context) const {
    ScriptResult result;
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly)) {
        result.message = QStringLiteral("cannot read ") + QDir::toNativeSeparators(file) +
                         QStringLiteral(": ") + f.errorString();
        return result;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError) {
        result.message = QStringLiteral("%1: JSON error at offset %2: %3")
                             .arg(QDir::toNativeSeparators(file))
                             .arg(pe.offset)
                             .arg(pe.errorString());
        return result;
    }
    if (context.scriptDir.isEmpty()) {
        context.scriptDir = QFileInfo(file).absolutePath();
    }
    return run(doc, context);
}

}  // namespace ot
