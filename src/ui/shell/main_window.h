// Главное окно (этап 0 — каркас): меню, Главная-заглушка, простой просмотр страницы, строка состояния.
// Раскладка нового Acrobat (глобальная панель, «Все инструменты», вкладки) — сессии 2.3–2.4.
#pragma once

#include <QMainWindow>

class QLabel;
class QScrollArea;
class QStackedWidget;

namespace ot {
class DocumentSession;
}

namespace ot::ui {

class MainWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    bool openFile(const QString& path);
    void showHome();
    int currentPage() const { return page_; }
    ot::DocumentSession* session() const { return session_; }

protected:
    void changeEvent(QEvent* event) override;

private:
    void retranslateTitle();
    void buildMenus();
    QWidget* buildHome();
    void showPage(int index);
    void openDialog();
    void about();

    ot::DocumentSession* session_ = nullptr;
    QStackedWidget* stack_ = nullptr;
    QWidget* home_ = nullptr;
    QScrollArea* view_ = nullptr;
    QLabel* pageLabel_ = nullptr;
    QLabel* pageStatus_ = nullptr;
    int page_ = 0;
    QString documentName_;  // имя открытого файла для заголовка; пусто — Главная
};

}  // namespace ot::ui
