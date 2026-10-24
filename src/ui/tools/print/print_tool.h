// Инструмент «Печать» из «Все инструменты» (план 2.6; эталон 6.2 п. 13, 3.3; view-37): выбор инструмента
// открывает диалог «Печать» текущей вкладки (как Ctrl+P), панель слева — «Печать…», «Печать в PDF…»
// (принтер «Microsoft Print to PDF», create-15 — замена виртуального принтера Adobe PDF) и «Установки
// печати…». Диалог открывается по сигналу ToolRegistry::activated (выбор), а не в activate(): смена вкладки
// при включённом инструменте его не открывает.
#pragma once

#include <QObject>
#include <QPointer>

#include "ui/tools/tool.h"

class QLabel;
class QPushButton;

namespace ot::ui {

class ToolRegistry;

class PrintTool : public Tool {
public:
    explicit PrintTool(ToolRegistry& registry);
    ~PrintTool() override;

    QString id() const override { return QStringLiteral("print"); }
    QString title() const override;
    QString icon() const override;
    int stage() const override { return 2; }
    void activate(DocumentView& view) override { view_ = &view; }
    void deactivate() override {}
    QWidget* panel() override;

    // Открыть диалог текущей вкладки; printer — сразу выбрать принтер (пусто — по умолчанию).
    void openDialog(const QString& printer = {});

private:
    QObject context_;  // соединения с реестром
    QPointer<DocumentView> view_;
    QPointer<QWidget> panel_;
    QPointer<QLabel> note_;
    QPointer<QPushButton> print_;
    QPointer<QPushButton> pdf_;
    QPointer<QPushButton> prefs_;
};

}  // namespace ot::ui
