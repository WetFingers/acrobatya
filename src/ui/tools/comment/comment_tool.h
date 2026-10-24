// Инструмент «Добавить комментарии» из «Все инструменты» (план 5.1; эталон 3.4 «Инструмент», 6.2 п. 3;
// comment-01, comment-02, comment-03, comment-42): включение — панель слева 320 с действиями группы
// (Записка, Выделить, Подчеркнуть, Зачеркнуть, Волнистая, Вставить текст, Заменить текст); выбор действия —
// режим CommentMode в слоте ViewToolId::Comment вида вкладки. Клавиши — S и Ctrl+6 (записка), U (выделить
// текст). Добавлять комментарии нельзя (защищённый просмотр, только чтение, не PDF, право /P) — кнопки
// серые, причина — строкой панели и подсказкой.
#pragma once

#include <QLabel>
#include <QObject>
#include <QPointer>
#include <QString>

#include <map>
#include <vector>

#include "ui/tools/comment/comment_mode.h"
#include "ui/tools/tool.h"

class QVBoxLayout;

namespace ot::ui {

class PageView;
class ToolButton;
class ToolRegistry;

class CommentTool : public QObject, public Tool {
    Q_OBJECT
public:
    explicit CommentTool(ToolRegistry& registry);
    ~CommentTool() override;

    QString id() const override { return QStringLiteral("comment"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 5; }
    void activate(DocumentView& view) override;
    void deactivate() override;
    QWidget* panel() override;
    std::vector<ot::Command> commands() const override;
    void settingsChanged() override;

    // Режим комментариев вида (ставится в слот при первом обращении).
    static CommentMode& modeOf(PageView& view);
    CommentMode* mode() const;
    DocumentView* view() const { return view_; }
    QString lockReason() const { return lock_; }

    // Включить действие на вкладке инструмента: режим вида + кнопка панели.
    void start(CommentAction action);
    ToolButton* actionButton(CommentAction action) const;
    QLabel* lockLabel() const { return lockLabel_; }

signals:
    void lockChanged(const QString& reason);

private:
    void buildPanel();
    void updateLock();
    void syncButtons();
    ToolButton* addRow(QVBoxLayout* column, const QString& icon, const QString& label);
    void addCaption(QVBoxLayout* column, const QString& text);

    ToolRegistry& registry_;
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    std::map<CommentAction, QPointer<ToolButton>> buttons_;
    QPointer<QLabel> lockLabel_;
    QString lock_;
    std::vector<QMetaObject::Connection> connections_;
};

}  // namespace ot::ui
