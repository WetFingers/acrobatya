#include "ui/shell/main_window.h"

#include <QAction>
#include <QApplication>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QKeySequence>
#include <QLabel>
#include <QMenuBar>
#include <QMessageBox>
#include <QPushButton>
#include <QScrollArea>
#include <QStackedWidget>
#include <QStatusBar>
#include <QVBoxLayout>
#include <algorithm>

#include "services/app_info.h"
#include "services/document_session.h"

namespace ot::ui {

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), session_(new ot::DocumentSession(this)) {
    setObjectName(QStringLiteral("mainWindow"));
    setWindowTitle(tr("АкроБатя"));
    resize(1280, 800);

    stack_ = new QStackedWidget(this);
    home_ = buildHome();
    stack_->addWidget(home_);

    view_ = new QScrollArea(stack_);
    view_->setObjectName(QStringLiteral("pageView"));
    view_->setAlignment(Qt::AlignHCenter | Qt::AlignTop);
    view_->setBackgroundRole(QPalette::Dark);
    pageLabel_ = new QLabel(view_);
    pageLabel_->setObjectName(QStringLiteral("pageImage"));
    view_->setWidget(pageLabel_);
    stack_->addWidget(view_);
    setCentralWidget(stack_);

    buildMenus();
    pageStatus_ = new QLabel(this);
    statusBar()->addPermanentWidget(pageStatus_);
    statusBar()->showMessage(tr("Готово"));
}

MainWindow::~MainWindow() = default;

void MainWindow::buildMenus() {
    QMenu* file = menuBar()->addMenu(tr("&Файл"));
    QAction* open = file->addAction(tr("&Открыть…"), this, &MainWindow::openDialog);
    open->setShortcut(QKeySequence::Open);
    QAction* close = file->addAction(tr("&Закрыть"), this, [this] {
        session_->close();
        showHome();
    });
    close->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_W));
    file->addSeparator();
    QAction* quit = file->addAction(tr("В&ыход"), qApp, &QApplication::closeAllWindows);
    quit->setShortcut(QKeySequence(Qt::CTRL | Qt::Key_Q));

    QMenu* view = menuBar()->addMenu(tr("&Вид"));
    QAction* next = view->addAction(tr("Следующая страница"), this, [this] { showPage(page_ + 1); });
    next->setShortcut(QKeySequence(Qt::Key_PageDown));
    QAction* prev = view->addAction(tr("Предыдущая страница"), this, [this] { showPage(page_ - 1); });
    prev->setShortcut(QKeySequence(Qt::Key_PageUp));
    view->addSeparator();
    view->addAction(tr("Главная"), this, &MainWindow::showHome);

    QMenu* help = menuBar()->addMenu(tr("&Справка"));
    help->addAction(tr("&О программе"), this, &MainWindow::about);
}

QWidget* MainWindow::buildHome() {
    auto* page = new QWidget(this);
    page->setObjectName(QStringLiteral("homePage"));
    auto* layout = new QVBoxLayout(page);
    layout->setContentsMargins(48, 40, 48, 40);
    layout->setSpacing(16);

    auto* title = new QLabel(tr("Главная"), page);
    title->setObjectName(QStringLiteral("homeTitle"));
    QFont f = title->font();
    f.setPointSizeF(f.pointSizeF() * 2.2);
    f.setWeight(QFont::DemiBold);
    title->setFont(f);
    layout->addWidget(title);

    auto* hint = new QLabel(tr("Откройте PDF-файл, чтобы начать. Недавние файлы появятся здесь."), page);
    hint->setObjectName(QStringLiteral("homeHint"));
    hint->setWordWrap(true);
    layout->addWidget(hint);

    auto* row = new QHBoxLayout();
    auto* openButton = new QPushButton(tr("Открыть файл…"), page);
    openButton->setObjectName(QStringLiteral("homeOpen"));
    connect(openButton, &QPushButton::clicked, this, &MainWindow::openDialog);
    row->addWidget(openButton);
    row->addStretch(1);
    layout->addLayout(row);

    auto* recent = new QLabel(tr("Недавние: пока пусто"), page);
    recent->setObjectName(QStringLiteral("homeRecent"));
    recent->setEnabled(false);
    layout->addWidget(recent);
    layout->addStretch(1);
    return page;
}

bool MainWindow::openFile(const QString& path) {
    if (!session_->open(path)) {
        statusBar()->showMessage(tr("Не удалось открыть файл: %1").arg(session_->errorString()));
        return false;
    }
    setWindowTitle(tr("%1 — АкроБатя").arg(QFileInfo(path).fileName()));
    stack_->setCurrentWidget(view_);
    showPage(0);
    return true;
}

void MainWindow::showHome() {
    stack_->setCurrentWidget(home_);
    setWindowTitle(tr("АкроБатя"));
    pageStatus_->clear();
}

void MainWindow::showPage(int index) {
    const int count = session_->pageCount();
    if (count <= 0) {
        return;
    }
    page_ = std::clamp(index, 0, count - 1);
    const qreal dpi = 96.0 * devicePixelRatioF();
    QImage img = session_->renderPage(page_, dpi);
    img.setDevicePixelRatio(devicePixelRatioF());
    pageLabel_->setPixmap(QPixmap::fromImage(img));
    pageLabel_->adjustSize();
    pageStatus_->setText(tr("Страница %1 из %2").arg(page_ + 1).arg(count));
}

void MainWindow::openDialog() {
    const QString path = QFileDialog::getOpenFileName(this, tr("Открыть PDF"), QString(),
                                                      tr("Документы PDF (*.pdf);;Все файлы (*)"));
    if (!path.isEmpty()) {
        openFile(path);
    }
}

void MainWindow::about() {
    QMessageBox::about(this, tr("О программе"),
                       tr("АкроБатя %1\nPDF-движок MuPDF %2 (AGPL-3.0)\nQt %3 (LGPL-3.0)")
                           .arg(ot::app::version(), ot::app::mupdfVersion(), ot::app::qtVersion()));
}

}  // namespace ot::ui
