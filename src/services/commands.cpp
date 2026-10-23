#include "services/commands.h"

#include <QHash>
#include <QJsonObject>
#include <QSettings>

#include <algorithm>

#include "services/app_info.h"

namespace ot {

namespace {

// Контекст-режим: команда живёт, только пока он включён (окно и "view" — всегда).
bool isMode(const QString& when) {
    return !when.isEmpty() && when != QLatin1String(CommandRegistry::kViewContext);
}

}  // namespace

CommandRegistry::CommandRegistry(QObject* parent) : QObject(parent) {}

void CommandRegistry::add(Command command) {
    const auto it = std::find_if(commands_.begin(), commands_.end(),
                                 [&](const Command& c) { return c.id == command.id; });
    if (it != commands_.end()) {
        *it = std::move(command);
    } else {
        commands_.push_back(std::move(command));
    }
    emit changed();
}

bool CommandRegistry::remove(const QString& id) {
    const auto it =
        std::find_if(commands_.begin(), commands_.end(), [&](const Command& c) { return c.id == id; });
    if (it == commands_.end()) {
        return false;
    }
    commands_.erase(it);
    emit changed();
    return true;
}

const Command* CommandRegistry::find(const QString& id) const {
    for (const Command& c : commands_) {
        if (c.id == id) {
            return &c;
        }
    }
    return nullptr;
}

std::vector<const Command*> CommandRegistry::all() const {
    std::vector<const Command*> out;
    out.reserve(commands_.size());
    for (const Command& c : commands_) {
        out.push_back(&c);
    }
    return out;
}

std::vector<const Command*> CommandRegistry::inMenu(const QString& menu) const {
    std::vector<const Command*> out;
    for (const Command& c : commands_) {
        if (c.menu == menu) {
            out.push_back(&c);
        }
    }
    return out;
}

bool CommandRegistry::isEnabled(const QString& id) const {
    const Command* c = find(id);
    if (c == nullptr || c->stage > 0 || !c->run) {
        return false;
    }
    if (isMode(c->when) && !contexts_.contains(c->when)) {
        return false;
    }
    return !c->enabled || c->enabled();
}

bool CommandRegistry::isChecked(const QString& id) const {
    const Command* c = find(id);
    return c != nullptr && c->checkable && c->checked && c->checked();
}

bool CommandRegistry::trigger(const QString& id) {
    if (!isEnabled(id)) {
        return false;
    }
    find(id)->run();
    return true;
}

QList<QKeySequence> CommandRegistry::shortcuts(const QString& id) const {
    const Command* c = find(id);
    if (c == nullptr) {
        return {};
    }
    return overrides_.value(id, c->shortcuts);
}

QList<QKeySequence> CommandRegistry::effectiveShortcuts(const QString& id) const {
    const Command* c = find(id);
    if (c == nullptr || (c->singleKey && !singleKeys_) || (isMode(c->when) && !contexts_.contains(c->when))) {
        return {};
    }
    QList<QKeySequence> keys = shortcuts(id);
    if (!isMode(c->when)) {
        // Клавишу забирает включённый режим (в режиме чтения вслух Ctrl+Shift+E — «стоп», не «Извлечь»).
        for (const Command& other : commands_) {
            if (isMode(other.when) && contexts_.contains(other.when)) {
                for (const QKeySequence& k : shortcuts(other.id)) {
                    keys.removeAll(k);
                }
            }
        }
    }
    return keys;
}

void CommandRegistry::setSingleKeysEnabled(bool on) {
    if (singleKeys_ != on) {
        singleKeys_ = on;
        emit shortcutsChanged();
    }
}

void CommandRegistry::loadSettings() {
    QSettings ini(app::settingsPath(), QSettings::IniFormat);
    singleKeys_ = ini.value(QLatin1String(kSingleKeyKey), false).toBool();
    overrides_.clear();
    ini.beginGroup(QLatin1String(kOverrideGroup));
    for (const QString& key : ini.childKeys()) {
        if (QStringLiteral("hotkeys/") + key == QLatin1String(kSingleKeyKey)) {
            continue;
        }
        QList<QKeySequence> keys;
        for (const QString& k : ini.value(key).toStringList()) {
            const QKeySequence seq(k, QKeySequence::PortableText);
            if (!seq.isEmpty()) {
                keys << seq;
            }
        }
        overrides_.insert(key, keys);
    }
    emit shortcutsChanged();
}

void CommandRegistry::setContextActive(const QString& when, bool active) {
    if (contexts_.contains(when) == active) {
        return;
    }
    if (active) {
        contexts_.insert(when);
    } else {
        contexts_.remove(when);
    }
    emit shortcutsChanged();
}

bool CommandRegistry::isContextActive(const QString& when) const { return contexts_.contains(when); }

QList<CommandRegistry::Conflict> CommandRegistry::conflicts() const {
    QHash<QString, QStringList> byKey;  // «клавиша|when» → id
    QStringList order;
    for (const Command& c : commands_) {
        for (const QKeySequence& k : shortcuts(c.id)) {
            const QString slot = k.toString(QKeySequence::PortableText) + QLatin1Char('|') + c.when;
            if (!byKey.contains(slot)) {
                order << slot;
            }
            byKey[slot] << c.id;
        }
    }
    QList<Conflict> out;
    for (const QString& slot : order) {
        const QStringList& ids = byKey.value(slot);
        if (ids.size() > 1) {
            out << Conflict{QKeySequence(slot.section(QLatin1Char('|'), 0, 0), QKeySequence::PortableText),
                            ids};
        }
    }
    return out;
}

QJsonArray CommandRegistry::toJson() const {
    QJsonArray out;
    for (const Command& c : commands_) {
        QJsonArray keys;
        for (const QKeySequence& k : shortcuts(c.id)) {
            keys.append(k.toString(QKeySequence::PortableText));
        }
        QJsonObject o;
        o.insert(QStringLiteral("id"), c.id);
        o.insert(QStringLiteral("title"), c.title);
        o.insert(QStringLiteral("keys"), keys);
        o.insert(QStringLiteral("when"), c.when);
        o.insert(QStringLiteral("menu"), c.menu);
        o.insert(QStringLiteral("singleKey"), c.singleKey);
        o.insert(QStringLiteral("stage"), c.stage);
        out.append(o);
    }
    return out;
}

}  // namespace ot
