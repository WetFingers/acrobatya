// Панель «Слои» (эталон 3.1 «Навигация», 6.1; план 2.4; view-15): строки конфигурации /D — вложенность
// /Order, группы-подписи, «глаз» (включён / выключен), замок у заблокированных (не переключаются). Щелчок по
// строке — переключить (services/nav_ops.h toggleLayer: состояние просмотра, не правка — вид перерисовывается
// по layersChanged); «Сбросить к исходному» — снова /D. Нет слоёв — пустое состояние.
#pragma once

#include <QPointer>

#include <vector>

#include "core/links/layers.h"
#include "ui/panes/nav_page.h"

class QPushButton;
class QStackedWidget;
class QTreeWidget;
class QTreeWidgetItem;

namespace ot::ui {

class EmptyState;

class LayersPane : public NavPage {
    Q_OBJECT
public:
    explicit LayersPane(QWidget* parent = nullptr);

    QString paneId() const override { return QStringLiteral("layers"); }
    QString title() const override;
    void setDocument(DocumentView* view) override;
    void activated() override;

    QTreeWidget* tree() const { return tree_; }
    QPushButton* resetButton() const { return reset_; }
    bool isEmptyShown() const;
    const std::vector<layers::LayerUi>& rows() const { return rows_; }
    QTreeWidgetItem* item(int index) const;  // строка pdf_layer_config_ui
    // Переключить строку index (то же, что щелчок) и сбросить к /D.
    void toggle(int index);
    void resetToDefault();

signals:
    void rowsChanged();

protected:
    void changeEvent(QEvent* event) override;

private:
    void reload();
    void fill(const std::vector<layers::LayerUi>& rows);
    void retranslate();

    QPushButton* reset_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    EmptyState* empty_ = nullptr;
    QPointer<DocumentView> view_;
    std::vector<layers::LayerUi> rows_;
    quint64 generation_ = 0;
    bool dirty_ = true;
};

}  // namespace ot::ui
