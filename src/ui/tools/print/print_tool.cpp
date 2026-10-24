#include "ui/tools/print/print_tool.h"

#include <QCoreApplication>
#include <QLabel>
#include <QPushButton>
#include <QVBoxLayout>

#include "services/print_job.h"
#include "ui/shell/document_view.h"
#include "ui/shell/main_window.h"
#include "ui/theme/icons.h"
#include "ui/theme/theme.h"
#include "ui/tools/tool_registry.h"

namespace ot::ui {

namespace {

const QString kToolPrintToPdf = QStringLiteral("Microsoft Print to PDF");

MainWindow* windowOf(DocumentView* view) {
    return view != nullptr ? qobject_cast<MainWindow*>(view->window()) : nullptr;
}

}  // namespace

PrintTool::PrintTool(ToolRegistry& registry) {
    QObject::connect(&registry, &ToolRegistry::activated, &context_, [this, &registry](Tool* tool) {
        if (tool == this) {
            view_ = registry.view();
            openDialog();
        }
    });
}

// Реестр — статический: после QApplication панель не удаляется (её окно уже закрыто).
PrintTool::~PrintTool() {
    if (QCoreApplication::instance() != nullptr) {
        delete panel_.data();
    }
}

QString PrintTool::title() const { return QCoreApplication::translate("ot::ui::ToolRegistry", "Печать"); }

QString PrintTool::icon() const { return QLatin1String(toolIconName("print")); }

QWidget* PrintTool::panel() {
    if (panel_ != nullptr) {
        return panel_;
    }
    const Metrics& m = Theme::sizes();
    auto* panel = new QWidget();
    panel->setObjectName(QStringLiteral("printToolPanel"));
    auto* column = new QVBoxLayout(panel);
    column->setContentsMargins(m.grid * 4, m.grid * 4, m.grid * 4, m.grid * 4);
    column->setSpacing(m.grid * 3);
    note_ = new QLabel(panel);
    note_->setObjectName(QStringLiteral("printToolNote"));
    note_->setWordWrap(true);
    note_->setText(QCoreApplication::translate(
        "ot::ui::PrintTool",
        "Печать на принтер с предпросмотром: несколько страниц на листе, буклет, постер, "
        "оттенки серого."));
    column->addWidget(note_);
    print_ = new QPushButton(Icons::icon(QStringLiteral("printer")),
                             QCoreApplication::translate("ot::ui::PrintTool", "Печать… (Ctrl+P)"), panel);
    print_->setObjectName(QStringLiteral("printToolPrint"));
    print_->setDefault(true);
    QObject::connect(print_, &QPushButton::clicked, &context_, [this] { openDialog(); });
    column->addWidget(print_);
    pdf_ = new QPushButton(Icons::icon(QStringLiteral("file-text")),
                           QCoreApplication::translate("ot::ui::PrintTool", "Печать в PDF…"), panel);
    pdf_->setObjectName(QStringLiteral("printToolPdf"));
    pdf_->setToolTip(QCoreApplication::translate(
        "ot::ui::PrintTool", "Принтер «Microsoft Print to PDF»: файл — в папку из «Установки → Печать»"));
    pdf_->setEnabled(PrintDevices::printers().contains(kToolPrintToPdf));
    QObject::connect(pdf_, &QPushButton::clicked, &context_, [this] { openDialog(kToolPrintToPdf); });
    column->addWidget(pdf_);
    prefs_ = new QPushButton(Icons::icon(QStringLiteral("settings")),
                             QCoreApplication::translate("ot::ui::PrintTool", "Установки печати…"), panel);
    prefs_->setObjectName(QStringLiteral("printToolPreferences"));
    QObject::connect(prefs_, &QPushButton::clicked, &context_, [this] {
        if (MainWindow* w = windowOf(view_)) {
            w->showPreferences(QStringLiteral("print"));
        }
    });
    column->addWidget(prefs_);
    column->addStretch(1);
    panel_ = panel;
    return panel;
}

void PrintTool::openDialog(const QString& printer) {
    if (MainWindow* w = windowOf(view_)) {
        w->showPrintDialog({}, printer);
    }
}

}  // namespace ot::ui
