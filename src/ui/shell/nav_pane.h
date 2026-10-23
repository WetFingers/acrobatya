// Правая панель навигации (эталон 6.1 «Правая панель»; план 2.4; view-30): полоса кнопок 44 px у правого края
// (Миниатюры, Закладки, Комментарии, Слои, Вложения, Подписи; «Поля» — только в «Подготовить форму»;
// «Назначения» — правым щелчком по полосе) и панель 280 px слева от неё (NavPanel — в сплиттере окна, ширина
// тянется 220–480). Щелчок по кнопке открывает её страницу, по нажатой — закрывает; F4 — открыть/закрыть
// последнюю; одна страница за раз. Состояние — settings.ini: navpane/open, navpane/current, navpane/width,
// navpane/destinations (кнопка «Назначения» в полосе). Страницы будущих этапов — пустые состояния «появится в
// версии 0.N» (Комментарии — 5, Подписи — 4, Поля — 6); их сессии заменяют страницу (replacePage).
#pragma once

#include <QHash>
#include <QPointer>
#include <QStringList>
#include <QWidget>

#include "services/settings_keys.h"

class QMenu;
class QStackedWidget;
class QVBoxLayout;

namespace ot::ui {

class DocumentView;
class NavPage;
class PanelHeader;
class ToolButton;

// Панель 280 px (место — сплиттер окна): заголовок с названием страницы и «Закрыть», под ним — страница.
class NavPanel : public QWidget {
    Q_OBJECT
public:
    explicit NavPanel(QWidget* parent = nullptr);
    PanelHeader* header() const { return header_; }
    QStackedWidget* pages() const { return pages_; }
    ToolButton* closeButton() const { return close_; }
    // Ширина по умолчанию для сплиттера окна (navpane/width).
    void setPreferredWidth(int width);
    QSize sizeHint() const override;

private:
    void applyMetrics();

    int preferred_ = 0;
    PanelHeader* header_ = nullptr;
    ToolButton* close_ = nullptr;
    QStackedWidget* pages_ = nullptr;
};

class NavPane : public QWidget {
    Q_OBJECT
public:
    static constexpr const char* kOpenKey = settings::kNavPaneOpen.name;
    static constexpr const char* kCurrentKey = settings::kNavPaneCurrent.name;
    static constexpr const char* kWidthKey = settings::kNavPaneWidth.name;
    static constexpr const char* kDestinationsKey = settings::kNavPaneDestinations.name;

    explicit NavPane(QWidget* parent = nullptr);
    ~NavPane() override;

    NavPanel* panel() const { return panel_; }
    // Порядок полосы: thumbnails, bookmarks, comments, layers, attachments, signatures, fields, destinations.
    static QStringList paneIds();
    NavPage* page(const QString& id) const { return pages_.value(id); }
    ToolButton* button(const QString& id) const { return buttons_.value(id); }
    // Сессия-владелец ставит настоящую страницу вместо заглушки (Комментарии 5.4, Подписи 4.6, Поля 6.3).
    void replacePage(NavPage* page);

    void setDocument(DocumentView* view);
    DocumentView* document() const { return view_; }

    bool isPanelOpen() const { return open_; }
    QString currentPane() const { return current_; }
    void showPane(const QString& id);
    void closePanel();
    void togglePanel();  // F4
    // Полоса и панель на экране: окно скрывает их на Главной, по F8 и в режиме чтения.
    void setShown(bool shown);
    bool isShown() const { return shown_; }
    // «Поля» — только в режиме «Подготовить форму» (6.3).
    void setFieldsVisible(bool visible);
    // Кнопка «Назначения» в полосе (правый щелчок → «Назначения»).
    void setDestinationsButton(bool on);
    bool hasDestinationsButton() const;
    // Ширина панели из settings.ini (по умолчанию 280, в пределах 220–480 токенов) и запись после
    // перетаскивания.
    int panelWidth() const;
    void setPanelWidth(int width);
    // Меню правого щелчка по полосе (тесты и сцены — без exec).
    QMenu* railMenu();

signals:
    void panelOpenChanged(bool open);
    void currentPaneChanged(const QString& id);
    void openPdfRequested(const QString& path, const QString& title);

protected:
    void contextMenuEvent(QContextMenuEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void addPage(NavPage* page);
    void applyState();
    void saveState() const;
    void retranslate();
    void applyMetrics();
    QString paneTitle(const QString& id) const;
    QString paneIcon(const QString& id) const;

    QPointer<NavPanel> panel_;  // в сплиттере окна: сплиттер может удалить её раньше полосы
    QVBoxLayout* rail_ = nullptr;
    QHash<QString, NavPage*> pages_;
    QHash<QString, ToolButton*> buttons_;
    QPointer<QMenu> menu_;
    QPointer<DocumentView> view_;
    QString current_ = QStringLiteral("thumbnails");
    bool open_ = false;
    bool shown_ = true;
    bool fields_ = false;
};

}  // namespace ot::ui
