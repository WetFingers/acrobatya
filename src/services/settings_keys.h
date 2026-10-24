// Ключи settings.ini и их умолчания — в одном месте (эталон 4.7; рецепт «Новая настройка» docs/map.md).
// Группы — по категориям диалога «Установки» (эталон 3.18): general/, pageDisplay/, documents/, commenting/,
// forms/, editing/, signatures/, trust/, search/, reading/, ocr/, convert/, print/, hotkeys/, quickbar/,
// language/; состояние окна между запусками — window/, tools/, navpane/, fullScreen/. Читать и писать — через
// типизированный Settings (services/settings.h). Смена смысла ключа — новый шаг миграции по meta/version
// (Settings::migrate), а не молчаливая подмена умолчания.
#pragma once

namespace ot::settings {

struct BoolKey {
    const char* name;
    bool def;
};

// Целое в пределах [min, max]; вне пределов или не число — умолчание.
struct IntKey {
    const char* name;
    int def;
    int min;
    int max;
};

// Строка; choices — допустимые значения через «|» (пусто — любая строка), чужое значение — умолчание.
struct StringKey {
    const char* name;
    const char* def;
    const char* choices = "";
};

// Список строк (порядок важен); умолчание — пустой список.
struct ListKey {
    const char* name;
};

// Версия схемы настроек (Settings::kVersion; 0 — файл до 2.4b).
inline constexpr IntKey kMetaVersion{"meta/version", 0, 0, 1000};

// --- Общие (general/, language/, hotkeys/) ---
inline constexpr StringKey kTheme{"general/theme", "system", "system|light|dark|contrast"};  // view-25
inline constexpr BoolKey kLargeElements{"general/largeElements", false};  // ocr-42: Tokens::scaled(1.25)
// «При запуске»: вкладки прошлого сеанса вместо Главной (services/session_store.h).
inline constexpr BoolKey kRestoreSession{"general/restoreSession", false};
inline constexpr StringKey kLanguage{"language/ui", "system", "system|ru|en"};
// «Использовать однобуквенные ускорители для выбора инструментов» (H, V, Z…; эталон 6.8).
inline constexpr BoolKey kSingleKeyAccelerators{"hotkeys/singleKeyAccelerators", false};
// Переназначенные клавиши команд: hotkeys/<id> = список PortableText (services/commands.h).
inline constexpr char kHotkeysGroup[] = "hotkeys";

// --- Просмотр страницы (pageDisplay/; действие сглаживания, тонких линий, рамок, масштаба и раскладки по
// умолчанию — сессия 2.5b) ---
inline constexpr StringKey kDefaultLayout{"pageDisplay/defaultLayout", "auto",
                                          "auto|single|continuous|facing|facingContinuous"};
inline constexpr StringKey kDefaultZoom{"pageDisplay/defaultZoom", "auto",
                                        "auto|fitPage|fitWidth|fitVisible|actual|50|75|100|125|150|200|400"};
inline constexpr BoolKey kSmoothText{"pageDisplay/smoothText", true};
inline constexpr BoolKey kSmoothImages{"pageDisplay/smoothImages", true};
inline constexpr BoolKey kSmoothLineArt{"pageDisplay/smoothLineArt", true};
inline constexpr BoolKey kEnhanceThinLines{"pageDisplay/enhanceThinLines", true};
inline constexpr BoolKey kUseLogicalPageNumbers{"pageDisplay/useLogicalPageNumbers", true};  // view-08
inline constexpr BoolKey kShowPageBoxes{"pageDisplay/showPageBoxes", false};  // рамки Art/Trim/Bleed
// Предпросмотр наложения красок (pro-47): «Никогда / Всегда / Только для PDF/X» (как Acrobat — по умолчанию
// PDF/X).
inline constexpr StringKey kOverprintPreview{"pageDisplay/overprintPreview", "pdfx", "never|always|pdfx"};

// --- Документы (documents/; автосохранение, восстановление и PDF/A в окне — сессия 2.5) ---
inline constexpr BoolKey kRestoreLastView{"documents/restoreLastView", true};
inline constexpr BoolKey kAutosave{"documents/autosave", true};
inline constexpr IntKey kAutosaveMinutes{"documents/autosaveMinutes", 5, 1, 30};  // AutosaveRunner: 1–30
inline constexpr BoolKey kOfferRecovery{"documents/offerRecovery", true};
inline constexpr BoolKey kPdfaReadOnly{"documents/pdfaReadOnly", true};

// --- Чтение и специальные возможности (reading/; страница Установок — 8.3) ---
// Замена цветов документа (2.5b; view-26, ocr-38): нет / ночной режим / высокая контрастность / свои цвета /
// инвертировать всё (команды «Вид → Ночной режим», «Вид → Высокая контрастность»).
inline constexpr StringKey kRecolor{"reading/recolor", "none", "none|night|highContrast|custom|invert"};
inline constexpr StringKey kHighContrastScheme{"reading/highContrastScheme", "whiteOnBlack",
                                               "greenOnBlack|yellowOnBlack|whiteOnBlack|blackOnWhite"};
inline constexpr StringKey kRecolorText{"reading/recolorText", "#000000"};              // свои цвета: текст
inline constexpr StringKey kRecolorBackground{"reading/recolorBackground", "#FFFFFF"};  // свои цвета: фон
inline constexpr BoolKey kRecolorLineArt{"reading/recolorLineArt", true};       // «…и векторной графики»
inline constexpr BoolKey kRecolorOnlyBlack{"reading/recolorOnlyBlack", false};  // «только чёрный текст…»

// --- Доверие (trust/; Диспетчер доверия целиком — 7.6) ---
inline constexpr BoolKey kAllowOpenAttachments{"trust/allowOpenAttachments", true};
// Защищённый просмотр (2.5b, security-35; эталон 7.5): «Выкл.» / «Файлы из потенциально опасных расположений»
// (Zone.Identifier с ZoneId ≥ 3, временные папки, PDF-вложения) / «Все файлы».
inline constexpr StringKey kProtectedView{"trust/protectedView", "unsafe", "off|unsafe|all"};
// Привилегированные расположения (security-37): файлы этих папок (с подпапками) не идут в защищённый
// просмотр.
inline constexpr ListKey kTrustedFolders{"trust/trustedFolders"};
// Доступ PDF к сайтам (security-42; эталон 7.4): сайт не из списка — спросить / открывать все / запретить
// все.
inline constexpr StringKey kInternetAccess{"trust/internetAccess", "ask", "ask|allowAll|blockAll"};
// Сеть самой программы (эталон 7.4; security-36; план 4.1): «разрешена» / «только подписи» (TSA, OCSP, CRL,
// AIA, списки доверия) / «выключена» — services/http_fetcher.h, единая точка сети.
inline constexpr StringKey kNetworkAccess{"trust/network", "allowed", "allowed|signaturesOnly|off"};

// --- Подписи (signatures/; эталон 3.8, вопросы 12.2, 12.3; страница «Установок» — 4.6) ---
// Серверы времени — JSON core/sign/tsa_servers.h (имя, адрес, логин; по умолчанию DigiCert), пусто —
// умолчания; пароли — не здесь.
inline constexpr StringKey kTsaServers{"signatures/tsaServers", ""};
// Служба времени недоступна при подписи: спросить / подписать без метки / отменить подпись.
inline constexpr StringKey kTimestampFallback{"signatures/timestampFallback", "ask",
                                              "ask|withoutTimestamp|cancel"};
// Подпись с меткой времени (B-T, решение 4) — по умолчанию; выкл. — B-B.
inline constexpr BoolKey kTimestampSignatures{"signatures/timestamp", true};
// «Совместимость»: /SubFilter adbe.pkcs7.detached вместо ETSI.CAdES.detached.
inline constexpr BoolKey kSignCompatibility{"signatures/pkcs7Compatibility", false};
// «Автоматически добавлять сведения LTV при сохранении» (вопрос 12.3, core/sign/ltv.h decideAutoLtv): никогда
// / спросить, если данных больше 10 % размера файла + 10 КБ / всегда.
inline constexpr StringKey kAutoAddLtv{"signatures/autoAddLtv", "ask", "never|ask|always"};
// /VRI в /DSS по хэшу подписи — для старых проверяющих (по умолчанию нет).
inline constexpr BoolKey kLtvVri{"signatures/ltvVri", false};

// --- Печать (print/; эталон 3.3, 3.18 «Печать»; view-37, create-16; сессия 2.6) ---
// Принтер по умолчанию диалога «Печать» (пусто — системный).
inline constexpr StringKey kPrintPrinter{"print/printer", ""};
// «Качество»: потолок dpi растра печати (у «Microsoft Print to PDF» 1200 dpi — растр 300).
inline constexpr IntKey kPrintQuality{"print/quality", 300, 72, 1200};
// Масштаб по умолчанию: «Вписать» / «Реальный размер» / «Уменьшить негабаритные».
inline constexpr StringKey kPrintScale{"print/scale", "fit", "fit|actual|shrink"};
// «Печать в PDF» (create-16): папка (пусто — «Документы») и имя файла — шаблон, {name} — имя документа.
inline constexpr StringKey kPrintPdfFolder{"print/pdfFolder", ""};
inline constexpr StringKey kPrintPdfName{"print/pdfName", "{name}"};
// Спрашивать имя файла при «Печать в PDF» (иначе — папка и шаблон, существующий файл получает « (2)»).
inline constexpr BoolKey kPrintPdfAsk{"print/pdfAsk", true};

// --- Инструменты и панель быстрых действий ---
inline constexpr ListKey kPinnedTools{"tools/pinned"};
inline constexpr BoolKey kToolPanelCollapsed{"tools/panelCollapsed", false};  // Shift+F4
inline constexpr ListKey kQuickBarOrder{"quickbar/order"};                    // cloud-38

// --- Окно и правая навигация (состояние между запусками) ---
inline constexpr BoolKey kPanelsHidden{"window/panelsHidden", false};  // F8
inline constexpr char kNavPaneGroup[] = "navpane";
inline constexpr BoolKey kNavPaneOpen{"navpane/open", false};
inline constexpr StringKey kNavPaneCurrent{"navpane/current", "thumbnails"};
// Ширина правой панели: 280, тянется 220–480 (эталон 6.1) — пределы и умолчание даёт токен (× 1,25 у
// «Крупных элементов»), здесь только «есть ли сохранённое число».
inline constexpr IntKey kNavPaneWidth{"navpane/width", 0, 0, 10000};
inline constexpr BoolKey kNavPaneDestinations{"navpane/destinations", false};

// --- Полноэкранный режим (fullScreen/; страница Установок — 9.2) ---
inline constexpr IntKey kFullScreenAdvanceSeconds{"fullScreen/advanceSeconds", 0, 0, 3600};  // 0 — не листать
inline constexpr BoolKey kFullScreenUseDocDuration{"fullScreen/useDocDuration", true};
inline constexpr BoolKey kFullScreenLoop{"fullScreen/loop", false};
inline constexpr BoolKey kFullScreenEscapeExits{"fullScreen/escapeExits", true};
inline constexpr BoolKey kFullScreenClickAdvances{"fullScreen/clickAdvances", true};
inline constexpr BoolKey kFullScreenTransitions{"fullScreen/transitions", true};
inline constexpr StringKey kFullScreenDefaultTransition{"fullScreen/defaultTransition", ""};
inline constexpr StringKey kFullScreenBackground{"fullScreen/background", "#000000"};

// --- Редактирование (editing/; страница Установок — 3.4; edit-49) ---
// Шрифт подстановки, когда шрифта документа нет в системе (эталон 3.9, правило 3 FontResolver): семейство;
// пусто — «Выбрать автоматически» (Segoe UI или Times New Roman по засечкам).
inline constexpr StringKey kEditFallbackFont{"editing/fallbackFont", ""};
// Шрифт нового текста («Добавить текст», edit-02; эталон 3.9): семейство и кегль, пт.
inline constexpr StringKey kEditDefaultFont{"editing/defaultFont", "Arial"};
inline constexpr IntKey kEditDefaultFontSize{"editing/defaultFontSize", 12, 4, 144};
// Системные шрифты в списке FORMAT TEXT (кроме шрифтов документа).
inline constexpr BoolKey kEditSystemFonts{"editing/showSystemFonts", true};

}  // namespace ot::settings
