#include "ui/tools/tool_registry.h"

#include <algorithm>

#include "services/settings.h"
#include "ui/shell/document_view.h"

namespace ot::ui {

ToolRegistry& ToolRegistry::instance() {
    static ToolRegistry registry;
    return registry;
}

const QStringList& ToolRegistry::builtinOrder() {
    static const QStringList order = {
        QStringLiteral("edit"),       QStringLiteral("organize"),    QStringLiteral("comment"),
        QStringLiteral("fillsign"),   QStringLiteral("prepareform"), QStringLiteral("create"),
        QStringLiteral("combine"),    QStringLiteral("export"),      QStringLiteral("protect"),
        QStringLiteral("compress"),   QStringLiteral("certificate"), QStringLiteral("scan"),
        QStringLiteral("print"),      QStringLiteral("measure"),     QStringLiteral("accessibility"),
        QStringLiteral("javascript"),
    };
    return order;
}

ToolRegistry::ToolRegistry() = default;

ToolRegistry::~ToolRegistry() {
    if (active_ != nullptr) {
        active_->deactivate();
        active_ = nullptr;
    }
}

Tool* ToolRegistry::add(std::unique_ptr<Tool> tool) {
    Tool* raw = tool.get();
    const auto same =
        std::find_if(tools_.begin(), tools_.end(), [&](const auto& t) { return t->id() == raw->id(); });
    if (same != tools_.end()) {
        if (active_ == same->get()) {
            deactivate();
        }
        *same = std::move(tool);  // заглушку сменяет настоящий инструмент — место в порядке то же
    } else {
        tools_.push_back(std::move(tool));
    }
    emit toolsChanged();
    return raw;
}

bool ToolRegistry::remove(const QString& id) {
    const auto it = std::find_if(tools_.begin(), tools_.end(), [&](const auto& t) { return t->id() == id; });
    if (it == tools_.end()) {
        return false;
    }
    if (active_ == it->get()) {
        deactivate();
    }
    tools_.erase(it);
    emit toolsChanged();
    return true;
}

Tool* ToolRegistry::find(const QString& id) const {
    for (const auto& t : tools_) {
        if (t->id() == id) {
            return t.get();
        }
    }
    return nullptr;
}

std::vector<Tool*> ToolRegistry::tools() const {
    std::vector<Tool*> out;
    for (const QString& id : builtinOrder()) {
        if (Tool* t = find(id)) {
            out.push_back(t);
        }
    }
    for (const auto& t : tools_) {
        if (!builtinOrder().contains(t->id())) {
            out.push_back(t.get());
        }
    }
    return out;
}

std::vector<Tool*> ToolRegistry::listed() const {
    std::vector<Tool*> out;
    const QStringList pins = pinned();
    for (const QString& id : pins) {
        if (Tool* t = find(id)) {
            out.push_back(t);
        }
    }
    for (Tool* t : tools()) {
        if (!pins.contains(t->id())) {
            out.push_back(t);
        }
    }
    return out;
}

bool ToolRegistry::activate(const QString& id, DocumentView* view) {
    Tool* tool = find(id);
    if (tool == nullptr || tool->isStub()) {
        return false;
    }
    if (active_ != nullptr) {
        active_->deactivate();
    }
    active_ = tool;
    view_ = view;
    if (view != nullptr) {
        tool->activate(*view);
    }
    emit activated(tool);
    return true;
}

void ToolRegistry::deactivate() {
    if (active_ == nullptr) {
        return;
    }
    active_->deactivate();
    active_ = nullptr;
    emit deactivated();
}

void ToolRegistry::setView(DocumentView* view) {
    if (view_ == view) {
        return;
    }
    if (active_ != nullptr && view_ != nullptr) {
        active_->deactivate();
    }
    view_ = view;
    if (active_ != nullptr && view != nullptr) {
        active_->activate(*view);
    }
}

DocumentView* ToolRegistry::view() const { return view_; }

QStringList ToolRegistry::pinned() const { return Settings().get(settings::kPinnedTools); }

bool ToolRegistry::isPinned(const QString& id) const { return pinned().contains(id); }

void ToolRegistry::setPinned(const QString& id, bool on) {
    QStringList pins = pinned();
    if (pins.contains(id) == on) {
        return;
    }
    if (on) {
        pins << id;
    } else {
        pins.removeAll(id);
    }
    Settings().set(settings::kPinnedTools, pins);
    emit pinnedChanged();
}

}  // namespace ot::ui
