// Окно «Расширенный поиск» (план 2.5; эталон 3.2 «Поиск», 6.3 «Просмотр»; view-19): Ctrl+Shift+F, не
// модальное. Где искать — текущий документ, все открытые документы или папка (с подпапками: индекс FTS5
// state.sqlite, services/folder_search.h); что — строка с подсказками по индексу, «Только целые слова», «С
// учётом регистра» (для папки — всегда целые слова без регистра: так устроен индекс), «Включить закладки» и
// «Включить вложения» (только для открытых документов: индекс хранит текст страниц). Результаты — деревом
// «файл → страница → контекст» (совпадение жирным), зашифрованные — с пометкой «пропущен», без текста — «нет
// текста — распознать?» с кнопкой (8.1), «Стоп» прерывает, двойной щелчок открывает документ на совпадении
// (панель «Найти» подсвечивает его), «Экспорт CSV…» — таблица результатов (UTF-8 с BOM, «;», CRLF —
// открывается в Excel с русскими настройками).
#pragma once

#include <QDialog>
#include <QFuture>
#include <QPointer>
#include <QVector>

#include <memory>
#include <vector>

#include "core/text/search.h"
#include "services/folder_search.h"

class QCheckBox;
class QComboBox;
class QCompleter;
class QLabel;
class QLineEdit;
class QPushButton;
class QStringListModel;
class QTreeWidget;
class QTreeWidgetItem;

namespace ot {
namespace search {
struct Control;
struct Result;
struct BookmarkHit;
struct AttachmentHits;
}  // namespace search
}  // namespace ot

namespace ot::ui {

class DocumentView;
class MainWindow;

class SearchWindow : public QDialog {
    Q_OBJECT
public:
    enum class Scope { Current, Open, Folder };

    // Строка результатов (дерево и CSV).
    struct Row {
        enum class Kind { Text, Bookmark, Attachment, Encrypted, Failed, NoText };
        Kind kind = Kind::Text;
        QString path;         // файл документа
        int page = -1;        // с нуля; −1 — нет
        QString context;      // совпадение между U+0002 и U+0003
        QString detail;       // закладка — её заголовок; вложение — имя файла
        quint64 session = 0;  // открытый документ (0 — из папки)
        int firstChar = -1;   // совпадение на странице (открытый документ)
    };

    explicit SearchWindow(MainWindow& window);
    ~SearchWindow() override;

    QLineEdit* queryField() const { return query_; }
    QComboBox* scopeBox() const { return scope_; }
    QTreeWidget* tree() const { return tree_; }
    QLabel* statusLabel() const { return status_; }
    QPushButton* stopButton() const { return stop_; }
    QPushButton* exportButton() const { return export_; }

    void setQuery(const QString& text);
    void setScope(Scope scope);
    Scope scope() const;
    void setFolder(const QString& folder);
    QString folder() const;
    void setRecursive(bool on);
    void setWholeWord(bool on);
    void setCaseSensitive(bool on);
    void setIncludeBookmarks(bool on);
    void setIncludeAttachments(bool on);

    // Найти (прежний поиск останавливается); Стоп — прервать.
    void start();
    void stop();
    bool isRunning() const { return running_ > 0; }

    const QVector<Row>& rows() const { return rows_; }
    // Таблица результатов: «Файл;Путь;Страница;Где;Контекст», UTF-8 с BOM, CRLF, кавычки по RFC 4180.
    static QByteArray toCsv(const QVector<Row>& rows);
    bool exportCsv(const QString& path) const;
    // Подсказки по индексу (QCompleter поля).
    QStringList suggestions(const QString& text) const;
    // Двойной щелчок по строке results — открыть документ на совпадении.
    void openRow(const Row& row);

signals:
    void searchFinished();

protected:
    void changeEvent(QEvent* event) override;
    void closeEvent(QCloseEvent* event) override;

private:
    void retranslate();
    void updateScopeControls();
    void browse();
    void runDocument(DocumentView* view, const SearchOptions& options);
    void addDocumentResult(const QString& path, quint64 session, const search::Result& result,
                           const QVector<search::BookmarkHit>& bookmarks,
                           const QVector<search::AttachmentHits>& attachments);
    void addFolderFile(const FolderFileHits& hits);
    void finishFolder(const FolderSearchResult& result);
    void doneOne();
    QTreeWidgetItem* fileItem(const QString& path, const QString& title);
    void addMarked(const QString& path, const QString& title, Row::Kind kind);
    void updateStatus();
    void exportDialog();

    MainWindow& window_;
    QLineEdit* query_ = nullptr;
    QCompleter* completer_ = nullptr;
    QStringListModel* suggestModel_ = nullptr;
    QComboBox* scope_ = nullptr;
    QLineEdit* folder_ = nullptr;
    QPushButton* browse_ = nullptr;
    QCheckBox* recursive_ = nullptr;
    QCheckBox* wholeWord_ = nullptr;
    QCheckBox* caseSensitive_ = nullptr;
    QCheckBox* bookmarks_ = nullptr;
    QCheckBox* attachments_ = nullptr;
    QPushButton* find_ = nullptr;
    QPushButton* stop_ = nullptr;
    QPushButton* export_ = nullptr;
    QPushButton* close_ = nullptr;
    QTreeWidget* tree_ = nullptr;
    QLabel* status_ = nullptr;
    QLabel* whereLabel_ = nullptr;
    QLabel* whatLabel_ = nullptr;

    std::unique_ptr<FolderSearch> folderSearch_;
    QFuture<FolderSearchResult> folderFuture_;
    std::vector<std::shared_ptr<search::Control>> controls_;
    QVector<Row> rows_;
    QString searched_;  // строка идущего / прошлого поиска
    SearchOptions searchedOptions_;
    quint64 generation_ = 0;
    int running_ = 0;
    int files_ = 0;  // файлов с совпадениями
    int hits_ = 0;   // строк-совпадений
    int filesDone_ = 0;
    int filesTotal_ = 0;
    bool stopped_ = false;
};

}  // namespace ot::ui
