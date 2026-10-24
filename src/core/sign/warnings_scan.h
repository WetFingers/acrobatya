// «Проверить документ» перед подписанием — предупреждения (эталон 3.8 «Диалог подписания»; план 4.5, п. 3;
// sign-17): то, что может изменить вид или поведение документа после подписи и что Acrobat показывает в
// списке «Просмотреть»: JavaScript (/Names /JavaScript, /OpenAction, /AA каталога, страниц, комментариев и
// полей, действия ссылок и закладок, цепочки /Next), запуск программ (/Launch), ссылки на сайты (/URI),
// внешние файлы (/GoToR, /GoToE, /ImportData, /SubmitForm), мультимедиа (/RichMedia, /Movie, /Sound, /Screen,
// /3D и действия воспроизведения), вложения (/Names /EmbeddedFiles, /FileAttachment), формы XFA (/XFA) и
// /NeedAppearances true (вид полей перестраивается при открытии). Одно предупреждение на место (владелец и
// ключ); уровень — по виду. Только чтение. Поток — поток документа.
#pragma once

#include <string>
#include <vector>

namespace ot {
class Document;
}

namespace ot::sign {

enum class WarningKind {
    JavaScript,
    Launch,
    ExternalLink,  // /URI
    ExternalFile,  // /GoToR, /GoToE, /ImportData, /SubmitForm
    Multimedia,
    EmbeddedFile,
    Xfa,
    NeedAppearances,
};
const char* warningKindName(WarningKind kind);

enum class WarningLevel { Info, Warning, Danger };
const char* warningLevelName(WarningLevel level);
WarningLevel warningLevel(WarningKind kind);

struct DocumentWarning {
    WarningKind kind = WarningKind::JavaScript;
    WarningLevel level = WarningLevel::Info;
    int page = -1;   // страница (−1 — документ)
    int object = 0;  // номер объекта-владельца (0 — прямой)
    // Где (английский — «Подробнее»): «/OpenAction», «page /AA /O», «annotation /A /Next»…
    std::string where;
    std::string detail;  // файл, адрес, имя вложения или сценария (UTF-8)
};

struct WarningScan {
    std::vector<DocumentWarning> warnings;

    bool clean() const noexcept { return warnings.empty(); }
    int count(WarningKind kind) const;
    WarningLevel worst() const;  // пусто — Info
};

WarningScan scanWarnings(Document& doc);

}  // namespace ot::sign
