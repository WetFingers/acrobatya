// Глобальная панель (эталон 6.1, 6.4; высота 48, фон surfaceAlt): «Меню ▾» (классическая строка меню
// выпадающим списком; F9 показывает обычный QMenuBar), «Главная», вкладки документов (DocumentTabs), справа
// поле поиска, «Все инструменты» (раскрывает левую панель), «Поделиться ▾» и «⚙» — Установки (Ctrl+K). Что
// делают кнопки, решает окно: панель только отдаёт сигналы и держит меню, которые ей поставили.
#pragma once

#include <QWidget>

class QMenu;

namespace ot::ui {

class DocumentTabs;
class SearchField;
class ToolButton;

class GlobalBar : public QWidget {
    Q_OBJECT
public:
    explicit GlobalBar(QWidget* parent = nullptr);

    DocumentTabs* tabs() const { return tabs_; }
    SearchField* search() const { return search_; }
    ToolButton* menuButton() const { return menu_; }
    ToolButton* homeButton() const { return home_; }
    ToolButton* allToolsButton() const { return allTools_; }
    ToolButton* shareButton() const { return share_; }
    ToolButton* settingsButton() const { return settings_; }

    void setMainMenu(QMenu* menu);
    void setShareMenu(QMenu* menu);
    // «Главная» нажата, пока показана Главная.
    void setHomeActive(bool active);

signals:
    void homeClicked();
    void allToolsClicked();
    void settingsClicked();
    void searchRequested(const QString& text);

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslate();
    void applyMetrics();

    ToolButton* menu_ = nullptr;
    ToolButton* home_ = nullptr;
    QWidget* separator_ = nullptr;
    DocumentTabs* tabs_ = nullptr;
    SearchField* search_ = nullptr;
    ToolButton* allTools_ = nullptr;
    ToolButton* share_ = nullptr;
    ToolButton* settings_ = nullptr;
};

}  // namespace ot::ui
