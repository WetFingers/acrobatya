// Журнал правок и история undo/redo (эталон 4.3). Журнал MuPDF (pdf_enable_journal) включает Document при
// открытии PDF; после этого правка вне операции — ошибка MuPDF (FZ_ERROR_ARGUMENT).
//
//   Journal::Operation op(doc, "Add note");  // pdf_begin_operation
//   … правка через mu::call …; op.invalidate(page);
//   op.commit();                             // pdf_end_operation; без commit (исключение) —
//   pdf_abandon_operation
//
// Вложенная операция (Operation внутри Operation того же документа) присоединяется к внешней: begin внутрь
// begin не вкладывается, имя и шаг — внешней; вложенная без commit делает внешнюю неудачной — её commit()
// откатывает всё и бросает MuError. Операции самого MuPDF (pdf_add_image, pdf_create_annot,
// pdf_set_metadata…) уже обёрнуты: внутри нашей они вложенные, без нашей — свой шаг, его UndoStack
// подхватывает сам.
//
// UndoStack — шаги для меню «Отменить: …» поверх шагов MuPDF (pdf_undoredo_state/pdf_undoredo_step): шаг —
// одна операция или группа одноимённых операций с промежутком не больше окна (1 с — набор текста); undo/redo
// шага — pdf_undo/pdf_redo столько раз, сколько в нём операций, и страницы шага (их display lists сбросить;
// fz_page через undo не держать — MuPDF пересобирает дерево страниц сам). Расхождение с историей MuPDF (чужая
// операция, сброс журнала при ремонте) — шаги перестраиваются по MuPDF со всеми страницами.
// Имена операций — английские коды (журнал); подпись меню по-русски выбирает ui. Поток — поток документа.
#pragma once

#include <chrono>
#include <functional>
#include <string>
#include <vector>

namespace ot {

class Document;

// Страницы, которые затронул шаг истории.
struct TouchedPages {
    std::vector<int> pages;  // по возрастанию, без повторов
    bool all = false;        // структурная правка или неизвестно — все страницы (и раскладка)

    void add(int page);
    void addAll() noexcept { all = true; }
    void merge(const TouchedPages& other);
    bool empty() const noexcept { return !all && pages.empty(); }
};

class UndoStack {
public:
    using Clock = std::chrono::steady_clock;
    static constexpr std::chrono::milliseconds kGroupWindow{1000};

    // Включает журнал MuPDF у документа (Document делает это при открытии PDF).
    explicit UndoStack(Document& doc);
    UndoStack(const UndoStack&) = delete;
    UndoStack& operator=(const UndoStack&) = delete;

    bool canUndo();
    bool canRedo();
    // Имя шага, который отменит undo() / вернёт redo(); пусто — нечего.
    std::string undoName();
    std::string redoName();
    // Шагов всего и сделано (позиция курсора: 0 — исходный документ).
    int size();
    int position();

    // Отменить / вернуть шаг; страницы шага. Нечего — MuError(FZ_ERROR_ARGUMENT).
    TouchedPages undo();
    TouchedPages redo();

    // Забыть историю (pdf_discard_journal): после записи документа (xref изменён) и после восстановления.
    void clear();

    // Окно группировки одноимённых операций (0 — не группировать).
    void setGroupWindow(std::chrono::milliseconds window) noexcept { window_ = window; }
    void setClock(std::function<Clock::time_point()> now) { now_ = std::move(now); }
    // Вызывается после каждого изменения истории (новый шаг, undo, redo, перестройка).
    void setListener(std::function<void()> listener) { listener_ = std::move(listener); }

    // Идёт наша операция (глубина вложенности > 0).
    bool inOperation() const noexcept { return depth_ > 0; }

private:
    friend class Journal;

    struct Step {
        std::string name;
        int operations = 1;  // шагов MuPDF в группе
        TouchedPages touched;
        Clock::time_point last{};
        bool sealed = false;  // после undo/redo к шагу не присоединяется набор
    };

    struct MuState {
        int position = 0;
        int steps = 0;
    };

    // Операция (Journal::Operation) — только через эти вызовы.
    bool begin(const std::string& name);  // true — внешняя (begin в MuPDF), false — вложенная
    void touch(int page) { pending_.add(page); }
    void touchAll() noexcept { pending_.addAll(); }
    void commit(bool outer);
    void abandon(bool outer) noexcept;

    // Позиция и число шагов MuPDF (незакрытую операцию самого MuPDF откатывает).
    MuState muState();
    // Привести шаги к истории MuPDF; true — менялись.
    bool sync();
    void notify() const;

    Document& doc_;
    std::vector<Step> steps_;
    int cursor_ = 0;  // шагов сделано
    std::chrono::milliseconds window_ = kGroupWindow;
    std::function<Clock::time_point()> now_;
    std::function<void()> listener_;

    // Текущая внешняя операция.
    int depth_ = 0;
    std::string name_;
    TouchedPages pending_;
    MuState before_;
    bool failed_ = false;
};

class Journal {
public:
    // RAII-операция правки. Объявлять в потоке документа; деструктор не бросает.
    class Operation {
    public:
        Operation(Document& doc, std::string name);
        ~Operation();
        Operation(const Operation&) = delete;
        Operation& operator=(const Operation&) = delete;

        // Правка затронула страницу / структуру (все страницы).
        void invalidate(int page);
        void invalidateAll();
        // Зафиксировать: pdf_end_operation (у внешней). Ошибка — MuError; операция тогда откатана.
        void commit();

        bool nested() const noexcept { return !outer_; }
        const std::string& name() const noexcept { return name_; }

    private:
        UndoStack& stack_;
        std::string name_;
        bool outer_ = false;
        bool done_ = false;
    };
};

}  // namespace ot
