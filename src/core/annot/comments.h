// Модель комментариев (план 5.4; эталон 3.4 «Попапы и ответы», «Панель «Комментарии»», «Граничные случаи»;
// comment-19, comment-20, comment-21): дерево по всему документу — корень (комментарий без /IRT), ответы
// (/Text с /IRT и /RT /R, в том числе ответы на ответы), части группы (/RT /Group — зачёркивание «Заменить
// текст» при Caret) и статусы (/Text с /IRT, /StateModel Review — Accepted/Rejected/Cancelled/Completed/None,
// Marked — Marked/Unmarked, и /State). Связь — по номеру объекта /IRT; /IRT на отсутствующую запись — корень
// (ответ без родителя не теряется). Служебные записи — Popup (без /Parent или с ним), ссылки, виджеты и
// прочие не-комментарии (isComment) — в дерево не входят.
//
// Ответы и статусы, которые создаёт программа, — скрытые на странице (/F Hidden|Print|NoZoom|NoRotate, без
// окна /Popup): они видны в панели и в окне записки родителя, а на странице их не рисует ни одна читалка;
// у чужих — флаги как есть, но выбор на странице их пропускает (annotAt — isServiceAnnot).
//
// Сортировка и фильтр (comment-19) — значения без Qt: панель, оверлей (скрытые фильтром номера объектов по
// страницам) и сводка (5.4) берут одно и то же. /RC — простой XHTML Acrobat: абзацы, жирный, курсив,
// подчёркивание, цвет; «@имя» — просто текст.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "core/annot/annots.h"

namespace ot {

class Document;

namespace annot {

// /StateModel.
inline constexpr std::string_view kReviewModel = "Review";
inline constexpr std::string_view kMarkedModel = "Marked";
// Флаги ответов и статусов программы: скрыта на странице, печать, без масштаба и поворота (как у Acrobat).
inline constexpr int kServiceAnnotFlags = kAnnotHidden | kAnnotPrint | kAnnotNoZoom | kAnnotNoRotate;

// Статус рецензии (/StateModel Review).
enum class ReviewState { None, Accepted, Rejected, Cancelled, Completed };
inline constexpr int kReviewStateCount = 5;
// "None", "Accepted"… — как /State.
std::string_view reviewStateName(ReviewState state) noexcept;
// По /State (с учётом регистра); незнакомое — None.
ReviewState reviewStateFromName(std::string_view name) noexcept;

// Запись истории проверок: аннотация-статус.
struct StatusEntry {
    AnnotRef ref;
    std::string model;  // Review | Marked
    std::string state;  // Accepted, Rejected, Cancelled, Completed, None | Marked, Unmarked
    std::string author;
    std::int64_t modified = 0;  // /M, иначе /CreationDate (секунды Unix)
};

// Комментарий с веткой ответов.
struct Comment {
    AnnotRef ref;
    AnnotType type = AnnotType::Unknown;
    std::string subtype;
    RectF rect;
    int flags = 0;
    std::string author;
    std::string subject;
    std::string contents;
    std::string richContents;  // /RC
    std::string intent;        // /IT
    std::string icon;          // /Name
    AnnotColor color;
    std::int64_t created = 0;
    std::int64_t modified = 0;
    int index = 0;  // место в /Annots страницы
    int depth = 0;  // 0 — корень, 1 — ответ, 2 — ответ на ответ…
    // Ответы (/RT /R) по дате создания, затем по порядку /Annots.
    std::vector<Comment> replies;
    // Части группы (/RT /Group): у Caret «Заменить текст» — StrikeOut.
    std::vector<AnnotRef> group;
    // Статусы этой записи по дате.
    std::vector<StatusEntry> history;

    // Дата для сортировки и карточки: /M, иначе /CreationDate.
    std::int64_t date() const noexcept { return modified != 0 ? modified : created; }
    // Последний статус рецензии (None — не задан или сброшен) и его автор.
    ReviewState review() const noexcept;
    std::string reviewAuthor() const;
    // «Галочка» (/StateModel Marked): последний статус — Marked.
    bool marked() const noexcept;
    // Ответов в ветке (все уровни).
    int replyCount() const noexcept;
};

// Дерево документа.
struct CommentTree {
    std::vector<Comment> roots;  // по страницам и порядку /Annots
    int pageCount = 0;

    // Запись ветки (корень или ответ) по ссылке; нет — nullptr.
    const Comment* find(const AnnotRef& ref) const;
    // Корень ветки, в которой ref (сам корень, ответ или статус); нет — nullptr.
    const Comment* rootOf(const AnnotRef& ref) const;
    // Всё, что удаляется вместе с записью ref: она, её ответы на всех уровнях, их статусы и части групп.
    std::vector<AnnotRef> branch(const AnnotRef& ref) const;
    // Все записи (корни и ответы, без статусов) — обход в глубину.
    void forEach(const std::function<void(const Comment&)>& fn) const;
    // Число корней и ответов.
    int count() const;
};

// Дерево по всем страницам: один проход по /Annots каждой. Не PDF — пусто.
CommentTree buildComments(Document& doc);

// Ответ или статус: Text с /IRT не-группы (/RT /R) или с /State. Popup — тоже служебная. Такие запись выбор
// на странице пропускает.
bool isServiceAnnot(const AnnotInfo& info) noexcept;

// Ответ на запись parent (корень или ответ): /Text на той же странице с рамкой родителя, /IRT, /RT /R,
// kServiceAnnotFlags, без /Popup, цвет родителя, /Name Comment; richContents — /RC (пусто — нет). Операция
// журнала operation. Нет родителя — MuError(FZ_ERROR_ARGUMENT).
struct ReplyProps {
    std::string author;
    std::string contents;
    std::string richContents;
    std::string nm;            // пусто — UUID
    std::int64_t created = 0;  // 0 — сейчас
};
AnnotRef addReply(Document& doc, const AnnotRef& parent, const ReplyProps& reply,
                  const std::string& operation = "Add reply");

// Статус записи target: /Text с /IRT, /StateModel model (Review | Marked), /State state, автор author,
// kServiceAnnotFlags; /Contents — "<state> set by <author>" (как пишет Acrobat). Review — None сбрасывает.
// Операция «Set status» (Marked — «Set check mark»). Нет записи или незнакомая модель — MuError.
AnnotRef setStatus(Document& doc, const AnnotRef& target, std::string_view model, std::string_view state,
                   const std::string& author, std::int64_t when = 0);

// Ключ документа для «прочитанных» (services/read_state.h): SHA-256 первого /ID трейлера (постоянный
// идентификатор переживает запись), hex; нет /ID или не PDF — пусто.
std::string commentsDocumentKey(Document& doc);

// ---------------------------------------------------------------- сортировка и фильтр (comment-19)

enum class CommentSort { Page, Author, Date, Type, Status };

struct CommentFilter {
    std::uint32_t types = 0;           // бит — AnnotType показываемых; 0 — все
    std::vector<std::string> authors;  // пусто — все
    std::vector<ReviewState> states;   // пусто — все
    bool onlyMine = false;             // только автора me
    std::string me;
    bool unreadOnly = false;            // только непрочитанные (isUnread)
    bool markedOnly = false;            // только с «галочкой»
    std::vector<std::uint32_t> colors;  // 0xRRGGBB; пусто — все
    std::string search;                 // подстрока /Contents (и ответов) без учёта регистра
    bool active() const noexcept;
};

// Подходит ли корень под фильтр (поиск — по тексту корня и его ответов; автор «только мои» — корня).
// isUnread — «непрочитана» (для unreadOnly; пусто — все прочитаны).
bool matches(const Comment& comment, const CommentFilter& filter,
             const std::function<bool(const Comment&)>& isUnread = {});
// Корни под фильтром в порядке sort (descending — обратный); равные — по странице и порядку /Annots.
std::vector<const Comment*> arrange(const CommentTree& tree, CommentSort sort, const CommentFilter& filter,
                                    bool descending = false,
                                    const std::function<bool(const Comment&)>& isUnread = {});
// Скрытые фильтром на странице (оверлей): номера объектов корней не под фильтром и всего их
// ветки (части групп), по страницам. Фильтр неактивен — пусто.
std::map<int, std::vector<int>> hiddenByFilter(const CommentTree& tree, const CommentFilter& filter,
                                               const std::function<bool(const Comment&)>& isUnread = {});
// Строчные буквы (Unicode, fz_tolower) — для поиска без учёта регистра.
std::string foldCase(std::string_view text);

// ---------------------------------------------------------------- /RC (comment-20)

inline constexpr std::uint32_t kNoRichColor = 0xFFFFFFFFu;

// Отрезок текста одного стиля; '\n' — новый абзац.
struct RichSpan {
    std::string text;
    bool bold = false;
    bool italic = false;
    bool underline = false;
    std::uint32_t color = kNoRichColor;  // 0xRRGGBB или нет
    bool operator==(const RichSpan&) const = default;
};

// XHTML /RC в виде Acrobat (<body xmlns=… xfa:APIVersion…><p dir="ltr"><span style=…>…</span></p></body>).
std::string richContentsFrom(const std::vector<RichSpan>& spans);
// Разбор /RC: <p>, <br/>, <span style>, <b>, <i>, <u>, <font color>; стили font-weight, font-style,
// text-decoration, color; сущности XML. Не XHTML — один отрезок как текст.
std::vector<RichSpan> parseRichContents(std::string_view xhtml);
// Простой текст отрезков (/Contents).
std::string plainText(const std::vector<RichSpan>& spans);

}  // namespace annot
}  // namespace ot
