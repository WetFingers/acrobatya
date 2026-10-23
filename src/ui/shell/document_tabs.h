// Вкладки документов глобальной панели (эталон 6.1, 6.4; view-27): высота 36, ширина ≤ 220, имя файла с
// многоточием посередине, путь — подсказкой; перетаскивание меняет порядок, средняя кнопка и крестик
// закрывают (tabCloseRequested), при переполнении — ◀ ▶; точка accent вместо значка файла — «несохранено».
// Ctrl+Tab / Ctrl+Shift+Tab — selectNext/selectPrevious по кругу (команды окна). Вкладка знает id документа
// (DocumentRegistry); вид вкладки — DocumentView в окне.
#pragma once

#include <QTabBar>

namespace ot::ui {

class DocumentTabs : public QTabBar {
    Q_OBJECT
public:
    explicit DocumentTabs(QWidget* parent = nullptr);

    int addDocument(quint64 id, const QString& title, const QString& path);
    int indexOf(quint64 id) const;
    quint64 idAt(int index) const;
    void setModified(int index, bool modified);
    bool isModified(int index) const;
    void selectNext();
    void selectPrevious();

protected:
    QSize tabSizeHint(int index) const override;
    QSize minimumTabSizeHint(int index) const override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void tabInserted(int index) override;

private:
    void applyMetrics();
    void refreshIcons();
    QIcon dotIcon() const;
};

}  // namespace ot::ui
