// Полноэкранный режим (эталон 3.1 «Режимы», 6.1; план 2.4; view-22): Ctrl+L — окно без рамки на мониторе
// главного окна, одна страница целиком на цвете фона, ничего больше. Листание: щелчок — вперёд, правый щелчок
// — назад (настройка), колесо, → ↓ PgDn Пробел Enter — вперёд, ← ↑ PgUp Backspace — назад, Home/End; Esc
// (настройка) и Ctrl+L — выход, вид документа встаёт на показанную страницу. Переходы /Trans страниц
// (простые: растворение и угасание, шторка в четыре стороны, сдвиги — как шторка, «разрезание», «коробка»,
// «жалюзи») поверх живого вида: прежний кадр уходит по ходу перехода, новая страница рисуется под ним;
// «Игнорировать все переходы» и переход по умолчанию — настройки. Автопродвижение — через N секунд
// (настройка) или по /Dur страницы; «По кругу» — после последней снова первая. Настройки — группа fullScreen/
// settings.ini (страница «Полноэкранный режим» в Установках — 2.4b/9.2).
#pragma once

#include <QColor>
#include <QString>
#include <QTimer>
#include <QWidget>

#include <memory>
#include <vector>

#include "core/doc/presentation.h"

class QScreen;

namespace ot {
class DocumentSession;
class RenderPool;
class TileCache;
}  // namespace ot

namespace ot::ui {

class PageView;
class TransitionOverlay;

struct FullScreenSettings {
    int advanceSeconds = 0;         // fullScreen/advanceSeconds: листать само через N с; 0 — нет
    bool useDocDuration = true;     // fullScreen/useDocDuration: время показа /Dur страницы
    bool loop = false;              // fullScreen/loop: после последней — первая
    bool escapeExits = true;        // fullScreen/escapeExits
    bool clickAdvances = true;      // fullScreen/clickAdvances: щелчок — вперёд, правый — назад
    bool transitions = true;        // fullScreen/transitions: false — «Игнорировать все переходы»
    QString defaultTransition;      // fullScreen/defaultTransition: "", "Fade", "Wipe"… — без /Trans
    QColor background = Qt::black;  // fullScreen/background

    static FullScreenSettings load();
    void save() const;
};

class FullScreenWindow : public QWidget {
    Q_OBJECT
public:
    FullScreenWindow(std::shared_ptr<DocumentSession> session, RenderPool* pool, TileCache* cache, int page,
                     QWidget* parent = nullptr);
    ~FullScreenWindow() override;

    // Показать без рамки во весь монитор (screen — монитор главного окна; nullptr — основной).
    void start(QScreen* screen);

    PageView& view() const { return *view_; }
    int currentPage() const;
    void next();
    void previous();
    void goTo(int page);

    const FullScreenSettings& settings() const { return settings_; }
    void setSettings(const FullScreenSettings& settings);
    bool isTransitionRunning() const;
    QString lastTransition() const { return lastTransition_; }  // имя последнего перехода (None — без)
    bool hasTransitions() const { return !transitions_.empty(); }
    bool isAdvanceScheduled() const { return advance_.isActive(); }

signals:
    void closed(int page);
    void transitionsLoaded();

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;
    void keyPressEvent(QKeyEvent* event) override;
    void closeEvent(QCloseEvent* event) override;
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    bool handleKey(QKeyEvent* event);
    void showPage(int page, bool animate);
    void scheduleAdvance();
    presentation::PageTransition transitionFor(int page) const;

    std::shared_ptr<DocumentSession> session_;
    PageView* view_ = nullptr;
    TransitionOverlay* overlay_ = nullptr;
    std::vector<presentation::PageTransition> transitions_;
    FullScreenSettings settings_;
    QTimer advance_;
    QString lastTransition_ = QStringLiteral("None");
    int startPage_ = 0;
    bool started_ = false;
};

}  // namespace ot::ui
