// История видов (эталон 3.1 «Навигация», 6.8: Alt+← / Alt+→): стек из 50 видов {страница, масштаб,
// прокрутка, режим}. Вид кладётся перед «скачком» (переход на страницу, Home/End, смена масштаба или режима,
// ссылка), а не на каждую прокрутку; повтор того же вида не кладётся. «Назад» отдаёт прежний вид и
// запоминает текущий для «Вперёд»; новый скачок после «Назад» обрезает ветку «Вперёд» (как в браузере).
#pragma once

#include <QPointF>

#include <cmath>
#include <deque>
#include <optional>

#include "ui/view/layout.h"

namespace ot::ui {

struct ViewState {
    int page = 0;
    float zoom = 1.0f;
    QPointF scroll;  // позиция полос прокрутки (логические px содержимого при этом масштабе и режиме)
    PageMode mode = PageMode::Continuous;
    bool coverPage = false;
    int rotation = 0;

    bool sameAs(const ViewState& o) const noexcept {
        return page == o.page && std::fabs(zoom - o.zoom) < 1e-4f && mode == o.mode &&
               coverPage == o.coverPage && rotation == o.rotation &&
               std::fabs(scroll.x() - o.scroll.x()) < 0.5 && std::fabs(scroll.y() - o.scroll.y()) < 0.5;
    }
};

class ViewHistory {
public:
    static constexpr int kMaxStates = 50;

    // Запомнить вид перед скачком; ветка «Вперёд» обрезается.
    void push(const ViewState& state) {
        forward_.clear();
        if (!back_.empty() && back_.back().sameAs(state)) {
            return;
        }
        back_.push_back(state);
        while (static_cast<int>(back_.size()) > kMaxStates) {
            back_.pop_front();
        }
    }
    // Прежний вид (current — текущий, уходит во «Вперёд»); нет — nullopt.
    std::optional<ViewState> back(const ViewState& current) {
        if (back_.empty()) {
            return std::nullopt;
        }
        ViewState s = back_.back();
        back_.pop_back();
        forward_.push_back(current);
        return s;
    }
    std::optional<ViewState> forward(const ViewState& current) {
        if (forward_.empty()) {
            return std::nullopt;
        }
        ViewState s = forward_.back();
        forward_.pop_back();
        back_.push_back(current);
        while (static_cast<int>(back_.size()) > kMaxStates) {
            back_.pop_front();
        }
        return s;
    }
    bool canBack() const noexcept { return !back_.empty(); }
    bool canForward() const noexcept { return !forward_.empty(); }
    int size() const noexcept { return static_cast<int>(back_.size()); }
    void clear() {
        back_.clear();
        forward_.clear();
    }

private:
    std::deque<ViewState> back_;
    std::deque<ViewState> forward_;
};

}  // namespace ot::ui
