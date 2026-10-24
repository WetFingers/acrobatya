// Режим «Редактировать» инструмента «Редактировать PDF» (план 3.4, п. 1; эталон 3.9 «Инструмент», 6.2 п. 1;
// edit-01 — UI): инструмент вида в слоте ViewToolId::Edit. По входу у видимых страниц — рамки блоков
// (абзацев модели контент-потока, services/edit_ops.h): серые пунктирные, «только чтение» — с замком и
// подсказкой причины. Щелчок в блоке — каретка (ParagraphEditor::positionAt), перетаскивание — выделение,
// двойной щелчок — слово, Shift+стрелки, Ctrl+A — весь блок, Ctrl+B/I/U — начертание выделенного; набор —
// keyPressEvent и QInputMethodEvent (IME для кириллицы и составных символов); Ctrl+C/X/V — простой текст.
//
// Правка идёт в копию редактора блока сразу: курсор и раскладка — по раскладке Rewriter (layoutText, задачей
// потока документа), локальная копия блока рисуется слоем поверх старого растра (маска цветом бумаги по
// старой и новой рамкам), пока документ не перерисован. Запись в документ (DocumentSession::editText, шаг
// истории «Edit text») — при выходе из блока, Esc, смене инструмента и через 1,5 с без набора; после неё
// страница перерисовывается, блоки страницы — заново. Ctrl+Z до записи бросает незаписанную правку блока,
// после — отменяет шаг истории (команда окна). Документ подписан, с /Perms или открыт только для чтения —
// рамки видны, каретка не ставится (setLocked: причина — в подсказке).
#pragma once

#include <QByteArray>
#include <QElapsedTimer>
#include <QFileSystemWatcher>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QSet>
#include <QString>
#include <QTimer>

#include <functional>
#include <optional>
#include <vector>

#include "core/edit/paragraph_editor.h"
#include "core/edit/rewriter.h"
#include "services/edit_ops.h"
#include "ui/view/tools/view_tool.h"

namespace ot::ui {

class EditMode : public QObject, public ViewTool {
    Q_OBJECT
public:
    static constexpr int kCommitDelayMs = 1500;   // запись после паузы в наборе
    static constexpr int kBlinkMs = 530;          // мигание каретки
    static constexpr int kRenderWaitMs = 3000;    // локальная копия после записи — пока растр не готов
    static constexpr float kNewBlockWidth = 200;  // «Добавить текст»: ширина нового блока, пт

    // Рамка блока на экране (вьюпорт).
    struct Frame {
        int page = -1;
        int block = -1;
        QRectF rect;
        bool editable = false;
        bool active = false;
        QString reason;  // код readOnlyCode у «только чтение»
    };

    EditMode(PageView& view, ToolController& controller);
    ~EditMode() override;

    ViewToolId id() const noexcept override { return ViewToolId::Edit; }
    QString title() const override;
    void activate() override;
    void deactivate() override;
    bool mousePress(QMouseEvent* event) override;
    bool mouseMove(QMouseEvent* event) override;
    bool mouseRelease(QMouseEvent* event) override;
    bool mouseDoubleClick(QMouseEvent* event) override;
    bool keyPress(QKeyEvent* event) override;
    void paintOverlay(QPainter& painter) override;
    QCursor cursor(QPointF point, Qt::KeyboardModifiers modifiers) const override;
    bool cancel() override;
    bool escape(bool cancelled) override;
    void documentChanged() override;
    void pageChanged(int page) override;
    bool capturesKeyboard() const override { return active_.has_value(); }
    bool wantsShortcut(const QKeyEvent* event) const override;
    bool acceptsInputMethod() const override { return active_.has_value(); }
    bool inputMethodEvent(QInputMethodEvent* event) override;
    QVariant inputMethodQuery(Qt::InputMethodQuery query) const override;

    // Правка документа запрещена (подписан, /Perms, только чтение): reason — подсказка по-русски; пусто —
    // можно.
    void setLocked(const QString& reason);
    bool locked() const noexcept { return !locked_.isEmpty(); }
    const QString& lockedReason() const noexcept { return locked_; }

    // Блоки страницы: загружены — указатель, нет — запрос в сессию и nullptr (придут — contentLoaded).
    const editing::PageContent* content(int page);
    bool hasContent(int page) const { return pages_.contains(page); }
    // Рамки блоков видимых страниц на экране (что рисует слой).
    std::vector<Frame> frames() const;
    // Блок под точкой вьюпорта: {страница, блок}; нет — {−1, −1}.
    std::pair<int, int> blockAt(QPointF viewportPoint) const;
    // Подсказка «только чтение» над точкой (пусто — не над таким блоком).
    QString readOnlyTip(QPointF viewportPoint) const;
    static QString reasonText(const QString& code);
    // Семейство Qt стиля: выбранное человеком или из /BaseFont («Arial-BoldMT» → «Arial»).
    static QString familyOf(const edit::CharStyle& style);

    // Каретка в блоке.
    bool editing() const noexcept { return active_.has_value(); }
    int activePage() const noexcept { return active_ ? active_->page : -1; }
    int activeBlock() const noexcept { return active_ ? active_->block : -1; }
    const edit::ParagraphEditor* editor() const noexcept { return active_ ? &active_->editor : nullptr; }
    int caretPosition() const noexcept { return active_ ? active_->caret : 0; }
    edit::TextRange selection() const noexcept;
    bool hasSelection() const noexcept { return !selection().empty(); }
    // Правка блока не записана в документ (или пишется сейчас).
    bool hasPendingEdit() const noexcept { return (active_ && active_->dirty) || committing_; }
    bool isCommitting() const noexcept { return committing_; }
    // Локальная копия блока рисуется поверх растра.
    bool showsLocalCopy() const noexcept;

    // Войти в блок block страницы page, каретка — pos (−1 — в конце); false — блок нельзя править.
    bool enterBlock(int page, int block, int pos = -1);
    void setSelection(int anchor, int caret);
    void selectAllInBlock();
    // Набрать текст в позиции каретки (вместо выделения).
    void insertText(const QString& text);
    void eraseSelectionOrChar(bool forward);
    // Стиль к выделению (без выделения — к стилю набора у каретки, следующий набор пойдёт им).
    void applyStyle(const edit::StyleChange& change);
    void toggleBold();
    void toggleItalic();
    void toggleUnderline();
    // Стиль символа у каретки (или первого выделенного) — панель FORMAT TEXT показывает его.
    std::optional<edit::CharStyle> currentStyle() const;
    // Записать незаписанную правку сейчас (таймер не ждёт).
    void commit();
    // Записать и выйти из блока.
    void leaveBlock();
    // Ctrl+Z до записи: бросить незаписанную правку блока; false — бросать нечего.
    bool revertPending();

    // Абзац целиком (панель FORMAT TEXT): выравнивание, межстрочный интервал (доля кегля), индекс.
    void setAlignment(edit::Align align);
    void setLineSpacing(float factor);
    void setScript(edit::Script script);

    // «Добавить текст» (edit-02; add_text.cpp): взведён — щелчок на странице вне блоков создаёт пустой блок
    // шириной kNewBlockWidth стилем newTextStyle (шрифт по умолчанию из «Установки → Редактирование»); блок
    // растёт вниз при наборе; после создания режим снимается сам.
    void setAddText(bool armed);
    bool addTextArmed() const noexcept { return addText_; }
    void setNewTextStyle(const edit::CharStyle& style) { newStyle_ = style; }
    const edit::CharStyle& newTextStyle() const noexcept { return newStyle_; }
    // Создать блок в точке страницы (левый верхний угол; задачей — каретка в нём, когда придёт ответ).
    void addTextAt(int page, QPointF pagePoint);
    // Рамка активного блока на экране (с маркерами ширины) и перемещение/ширина ручками.
    std::optional<QRectF> activeFrame() const;
    enum class Handle { None, Move, Left, Right };
    Handle handleAt(QPointF viewportPoint) const;
    void moveActive(QPointF pageDelta);
    void setActiveWidth(float width);

    // Картинки (план 3.4, п. 3; edit-06, edit-07, create-32; image_tool.cpp): щелчок вне текста — выбор
    // картинки (рамка accent, восемь маркеров: перетаскивание — перемещение, маркер — масштаб), правая кнопка
    // — меню (повернуть, отразить, обрезать, заменить, «Редактировать в…», вырезать/копировать/вставить,
    // удалить, экспорт), Del, Ctrl+C/X/V. Обрезка — рамкой с маркерами, Enter или щелчок вне — применить, Esc
    // — бросить.
    struct ImageRef {
        int page = -1;
        int index = -1;
        bool valid() const noexcept { return page >= 0 && index >= 0; }
        bool operator==(const ImageRef&) const = default;
    };
    enum class Grip { None, Inside, TopLeft, Top, TopRight, Right, BottomRight, Bottom, BottomLeft, Left };
    // Выбор файла (заменить, добавить — открыть; экспорт — сохранить): тесты подставляют свой.
    using FileChooser = std::function<QString(QWidget* parent, bool save, const QString& suggested)>;
    static void setFileChooser(FileChooser chooser);

    const edit::PageImage* image(ImageRef ref) const;
    ImageRef imageAt(QPointF viewportPoint) const;
    ImageRef selectedImage() const noexcept { return selected_; }
    void selectImage(ImageRef ref);
    void clearImageSelection();
    // Рамка выбранной на экране (с предпросмотром перетаскивания и обрезки) и маркер под точкой.
    std::optional<QRectF> imageFrame() const;
    Grip gripAt(QPointF viewportPoint) const;
    void rotateImage(float degrees);
    void flipImage(bool horizontal);
    void deleteImage();
    void startCrop();
    void applyCrop();
    void cancelCrop();
    bool cropping() const noexcept { return crop_.has_value(); }
    void setCropRect(const RectF& pageRect);
    void replaceImage(const QByteArray& bytes);
    void replaceImageFromFile();
    void addImage(const QByteArray& bytes, int page, std::optional<RectF> rect = std::nullopt);
    void addImageFromFile();
    void copyImage(bool cut);
    bool pasteImage();
    void exportImage();
    // «Редактировать в…»: картинка во временный файл, программа по расширению (ShellLauncher::openFile);
    // сохранение файла — замена картинки.
    void editImageExternally();
    QString externalFile() const { return externalPath_; }
    // Контекстное меню выбранной (вызывающий владеет; тесты зовут действия по objectName).
    QMenu* imageMenu(QWidget* parent);

signals:
    void editingChanged(bool editing);
    // Каретка, выделение или стиль под кареткой сменились (панель FORMAT TEXT).
    void caretChanged();
    void contentLoaded(int page);
    // Абзац записан (отчёт Rewriter) или запись не удалась (текст по-русски).
    void committed(const ot::edit::RewriteReport& report);
    void editFailed(const QString& message);
    void addTextChanged(bool armed);
    void imageSelectionChanged();

private:
    struct Active {
        quint64 id = 0;  // номер копии: ответ записи находит свою (активную или отложенную)
        int page = -1;
        int block = -1;  // индекс в PageContent::blocks (по нему рамка); −1 — блок ещё не найден заново
        RectF original;  // рамка блока до правки (координаты страницы) — маска локальной копии
        edit::ParagraphEditor editor;
        edit::ParagraphEditor clean;  // как в документе (Ctrl+Z до записи)
        int caret = 0;
        int anchor = 0;
        bool dirty = false;
        quint64 change = 0;                     // номер правки копии (раскладка, запись)
        std::optional<edit::CharStyle> typing;  // стиль набора у каретки (Ctrl+B без выделения)
        std::optional<edit::CharStyle> base;    // стиль пустого абзаца («Добавить текст»)
        bool fresh = false;                     // новый блок: в документе его ещё нет
        QString preedit;                        // строка IME, ещё не набранная
    };

    // edit_mode.cpp — страницы, вход и выход, запись.
    void requestContent(int page);
    void contentArrived(int page, quint64 generation, editing::PageContent content);
    void matchActive(int page);
    void changed();
    void requestLayout();
    void commitCopy(Active& copy);
    Active* copyById(quint64 id);
    void finishCommit(quint64 id, quint64 change, const EditTextResult& result);
    void failCommit(quint64 id, const QString& message);
    void rebaseActive(int page);
    void dropActive();
    void startRenderWait();
    PointF pagePoint(int page, QPointF viewportPoint) const;
    // Каретка (верх и низ, координаты страницы): у пустого абзаца — по геометрии набора.
    std::pair<PointF, PointF> caretSegment(int pos) const;
    // Рамка активного блока (координаты страницы).
    RectF activeRect() const;

    // image_tool.cpp — картинки.
    bool imagePress(QMouseEvent* event);
    bool imageMove(QMouseEvent* event);
    bool imageRelease(QMouseEvent* event);
    bool imageKey(QKeyEvent* event);
    void paintImages(QPainter& painter) const;
    RectF draggedImageRect() const;
    void imageEdited(QFuture<void> future, const QString& failure);
    void externalChanged(const QString& path);

    // add_text.cpp — новый блок, ручки рамки.
    bool pressHandle(QMouseEvent* event);
    bool dragHandle(QPointF viewportPoint);
    void paintHandles(QPainter& painter) const;

    // caret.cpp — клавиши, курсор, буфер обмена.
    bool editKey(QKeyEvent* event);
    void moveCaret(int pos, bool extend);
    int lineEdge(int pos, bool end) const;
    int wordStep(int pos, int delta) const;
    void copySelection(bool cut);
    void paste();
    void caretMoved();
    void restartBlink();

    // edit_overlay.cpp — рамки, замки, локальная копия, каретка и выделение.
    void paintFrames(QPainter& painter) const;
    void paintLocalCopy(QPainter& painter, const edit::ParagraphEditor& editor, int page,
                        const RectF& original) const;
    void paintCaret(QPainter& painter) const;

    QHash<int, editing::PageContent> pages_;
    QHash<int, quint64> generation_;  // правка страницы делает ответы старше неё устаревшими
    QSet<int> requested_;
    quint64 epoch_ = 0;  // смена документа
    std::optional<Active> active_;
    std::vector<Active> detached_;  // вышли из блока, а запись ещё идёт (или ждёт своей очереди)
    struct Written {
        int page = -1;
        RectF original;
        edit::ParagraphEditor editor;
    };
    std::vector<Written> afterglow_;  // записаны вне блока: копия рисуется, пока растр старый
    quint64 nextId_ = 0;
    QString locked_;
    bool pressing_ = false;
    bool addText_ = false;
    edit::CharStyle newStyle_;
    Handle drag_ = Handle::None;
    QPointF dragLast_;      // точка страницы прошлого шага перетаскивания
    float dragWidth0_ = 0;  // ширина набора в начале
    QPointF dragStart_;     // точка страницы в начале
    quint64 addRequest_ = 0;
    ImageRef selected_;
    Grip imageDrag_ = Grip::None;
    QPointF imagePress_;         // точка страницы нажатия
    QPointF imageNow_;           // точка страницы сейчас
    std::optional<RectF> crop_;  // рамка обрезки (координаты страницы)
    QFileSystemWatcher watcher_;
    QString externalPath_;
    ImageRef externalImage_;
    bool committing_ = false;
    bool commitAgain_ = false;  // во время записи набрали ещё — записать снова
    bool layoutBusy_ = false;
    bool layoutAgain_ = false;
    bool renderWait_ = false;  // запись сделана, растр страницы ещё старый
    int renderWaitPaints_ = 0;
    QTimer commitTimer_;
    QTimer blink_;
    QTimer renderTimer_;
    bool caretOn_ = true;
    QElapsedTimer lastClick_;
    QPointF lastClickPos_;
    int clicks_ = 0;
};

}  // namespace ot::ui
