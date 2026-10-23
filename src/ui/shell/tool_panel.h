// Левая панель (эталон 6.1 «Левая панель», 6.2; view-30): 320 px, свёрнутая — 48 (Shift+F4). Без активного
// инструмента — «Все инструменты»: строки из ToolRegistry (иконка Lucide 20 + подпись, звёздочка «закрепить
// наверху»; закреплённые — первыми), инструменты будущих этапов — серые с подсказкой «появится в версии 0.N».
// С активным — «◀ Все инструменты», название и панель инструмента (Tool::panel(); виджетом владеет
// инструмент, панель только показывает его и отдаёт назад). Свёрнутая — столбец иконок тех же инструментов.
// Щелчок по строке — сигнал toolRequested (включает окно: инструменту нужна вкладка).
#pragma once

#include <QPointer>
#include <QWidget>

class QScrollArea;
class QStackedWidget;
class QVBoxLayout;

namespace ot::ui {

class ListRow;
class PanelHeader;
class Tool;
class ToolButton;
class ToolRegistry;

class ToolPanel : public QWidget {
    Q_OBJECT
public:
    explicit ToolPanel(ToolRegistry& registry, QWidget* parent = nullptr);
    ~ToolPanel() override;

    bool isCollapsed() const { return collapsed_; }
    void setCollapsed(bool collapsed);
    void toggleCollapsed() { setCollapsed(!collapsed_); }
    // «◀ Все инструменты»: активный инструмент выключается, виден список.
    void showList();
    bool showsList() const;
    QWidget* currentPanel() const { return hosted_; }

    QList<ListRow*> rows() const;
    ListRow* row(const QString& id) const;
    QList<ToolButton*> railButtons() const;

signals:
    void toolRequested(const QString& id);
    void collapsedChanged(bool collapsed);

protected:
    void changeEvent(QEvent* event) override;

private:
    void rebuild();
    void onActivated(Tool* tool);
    void unhost();
    void retranslate();
    void applyMetrics();
    QString hint(const Tool& tool) const;

    ToolRegistry& registry_;
    QStackedWidget* pages_ = nullptr;
    QWidget* listPage_ = nullptr;
    PanelHeader* listHeader_ = nullptr;
    QScrollArea* scroll_ = nullptr;
    QWidget* rowsHost_ = nullptr;
    QVBoxLayout* rowsLayout_ = nullptr;
    QWidget* toolPage_ = nullptr;
    PanelHeader* toolHeader_ = nullptr;
    QVBoxLayout* toolLayout_ = nullptr;
    QWidget* rail_ = nullptr;
    QVBoxLayout* railLayout_ = nullptr;
    ToolButton* expand_ = nullptr;
    ToolButton* collapseList_ = nullptr;
    ToolButton* collapseTool_ = nullptr;
    QPointer<QWidget> hosted_;
    bool collapsed_ = false;
};

}  // namespace ot::ui
