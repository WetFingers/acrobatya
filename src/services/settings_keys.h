// Ключи settings.ini и их умолчания — в одном месте (эталон 4.7; рецепт «Новая настройка» docs/map.md).
// Группы — по категориям диалога «Установки» (эталон 3.18): general/, pageDisplay/, documents/, commenting/,
// measuring/ (5.5), forms/, editing/, signatures/, trust/, search/, reading/, ocr/, convert/, print/,
// hotkeys/, quickbar/, language/; состояние окна между запусками — window/, tools/, navpane/, fullScreen/.
// Читать и писать — через типизированный Settings (services/settings.h). Смена смысла ключа — новый шаг
// миграции по meta/version (Settings::migrate), а не молчаливая подмена умолчания.
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

// --- Формы (forms/; эталон 3.6, 3.18; план 6.1; forms-02, forms-43; страница Установок «Формы» — 6.1, п. 5)
// --- Подсветка полей (кнопка «Подсветить поля» фиолетовой полосы — то же), свои цвета подсветки и
// обязательных полей (пусто — токены formField и err темы), автопересчёт после правки поля, рамка фокуса
// поля, фиолетовая полоса «Документ содержит поля». Автозаполнения нет (forms-04 вне объёма).
inline constexpr BoolKey kFormHighlight{"forms/highlightFields", false};
inline constexpr StringKey kFormFieldColor{"forms/fieldColor", ""};
inline constexpr StringKey kFormRequiredColor{"forms/requiredColor", ""};
inline constexpr BoolKey kFormAutoCalculate{"forms/autoCalculate", true};
inline constexpr BoolKey kFormFocusRect{"forms/focusRect", true};
inline constexpr BoolKey kFormShowBar{"forms/showBar", true};

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
// JavaScript документов (эталон 7.2; security-37, security-40; план 6.1): «выкл.» / «только доверенные папки
// и файлы» («Включить для этого файла» — state.sqlite) / «все документы» (кроме защищённого просмотра).
inline constexpr StringKey kJavaScript{"trust/javascript", "trusted", "off|trusted|all"};
// Сеть самой программы (эталон 7.4; security-36; план 4.1): «разрешена» / «только подписи» (TSA, OCSP, CRL,
// AIA, списки доверия) / «выключена» — services/http_fetcher.h, единая точка сети.
inline constexpr StringKey kNetworkAccess{"trust/network", "allowed", "allowed|signaturesOnly|off"};
// Списки доверия (эталон 3.8 «Доверие», вопрос 12.4; services/trust_updater.h): EUTL — включён, AATL —
// попытка; «спрашивать перед установкой»; время последней проверки (ISO, UTC; раз в 14 дней).
inline constexpr BoolKey kTrustEutl{"trust/eutl", true};
inline constexpr BoolKey kTrustAatl{"trust/aatl", true};
inline constexpr BoolKey kTrustListsAsk{"trust/listsAskBeforeInstall", true};
inline constexpr StringKey kTrustListsCheckedAt{"trust/listsCheckedAt", ""};

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
// «Создание и внешний вид» (4.6; sign-13, sign-14, sign-17): показывать причину и место с контактом в диалоге
// подписания (как Acrobat: причина — да, место — нет), свои причины (пусто — шесть стандартных из
// интерфейса), внешние виды — JSON services/sign_settings.h (пусто — только «Стандартный»), вид по умолчанию
// (имя), «Проверять документ перед подписью»: никогда / при сертификации / всегда (по умолчанию — всегда:
// план 4.6 даёт «Проверить документ» и диалогу подписи; у Acrobat — «при сертификации»).
inline constexpr BoolKey kSignShowReason{"signatures/showReason", true};
inline constexpr BoolKey kSignShowLocation{"signatures/showLocation", false};
inline constexpr ListKey kSignReasons{"signatures/reasons"};
inline constexpr StringKey kSignAppearances{"signatures/appearances", ""};
inline constexpr StringKey kSignAppearance{"signatures/appearance", ""};
inline constexpr StringKey kSignReviewWarnings{"signatures/reviewWarnings", "always", "never|certify|always"};
// «Проверка» (4.6; sign-25, sign-32; services/crypto_worker.h): проверять подписи при открытии документа (как
// Acrobat — да), проверка отзыва — обязательно / по возможности (по умолчанию) / никогда, просроченные метки
// времени — принимать (Acrobat «Использовать просроченные метки времени»), «Предлагать доверять подписанту»
// — кнопка на жёлтой полосе, когда подпись цела, а подписант не доверен.
inline constexpr BoolKey kVerifyOnOpen{"signatures/verifyOnOpen", true};
inline constexpr StringKey kVerifyRevocation{"signatures/revocation", "bestEffort",
                                             "required|bestEffort|never"};
inline constexpr BoolKey kVerifyExpiredTimestamps{"signatures/acceptExpiredTimestamps", true};
inline constexpr BoolKey kVerifyPromptTrust{"signatures/promptTrust", true};
// «Проверять сертификаты на время» (sign-32; core/sign/verifier.h VerificationTime): время создания подписи
// (метка, иначе /M) / безопасное время (метка, иначе текущее) / текущее время.
inline constexpr StringKey kVerifyTime{"signatures/verifyTime", "signing", "signing|secure|current"};

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

// --- Комментирование (commenting/; эталон 3.4, 3.18; страница Установок — 5.1; comment-24, comment-42) ---
// Identity: имя, должность, организация; «всегда имя входа» (по умолчанию, как у Acrobat) — автор /T новых
// комментариев — имя пользователя Windows (core/annot/defaults.h authorName).
inline constexpr StringKey kCommentIdentityName{"commenting/identityName", ""};
inline constexpr StringKey kCommentIdentityTitle{"commenting/identityTitle", ""};
inline constexpr StringKey kCommentIdentityOrganization{"commenting/identityOrganization", ""};
inline constexpr BoolKey kCommentAlwaysLoginName{"commenting/alwaysLoginName", true};
// Окно записки: шрифт и кегль текста, открывать окно при наведении и при создании записки, линии-связки от
// значка к окну.
inline constexpr StringKey kCommentPopupFont{"commenting/popupFont", "Segoe UI"};
inline constexpr IntKey kCommentPopupFontSize{"commenting/popupFontSize", 10, 6, 36};
inline constexpr BoolKey kCommentPopupOnHover{"commenting/popupOnHover", true};
inline constexpr BoolKey kCommentOpenPopupOnCreate{"commenting/openPopupOnCreate", true};
inline constexpr BoolKey kCommentConnectorLines{"commenting/connectorLines", true};
// «Закрепить инструмент» (comment-42): инструмент комментария остаётся выбранным после действия.
inline constexpr BoolKey kCommentKeepTool{"commenting/keepToolSelected", false};
// Стиль нового комментария по инструменту — JSON {"<инструмент>": {"color": "#rrggbb", "opacity": 1,
// "width": 1, "icon": "Comment"}} (панель инструмента «Добавить комментарии»; «Сделать свойствами по
// умолчанию»).
inline constexpr StringKey kCommentToolStyles{"commenting/toolStyles", ""};
// Штамп инструмента «Штамп» (5.2): id каталога (services/stamps_catalog.h) — последний выбранный в палитре.
inline constexpr StringKey kCommentStamp{"commenting/stamp", "standard/approved"};

// --- Измерения (2D) (measuring/; эталон 3.4 «Измерения»; Установки — 5.5; comment-29, comment-30) ---
// Масштаб пользователя — метка /R ядра (core/annot/measure.h parseRatio: "1 cm = 10 m") и точность (знаков).
inline constexpr StringKey kMeasureScale{"measuring/scale", "1 cm = 1 cm"};
inline constexpr IntKey kMeasurePrecision{"measuring/precision", 2, 0, 4};
// Масштаб и единицы документа (/VP страницы), если они есть, — вместо масштаба пользователя.
inline constexpr BoolKey kMeasureUseDocumentScale{"measuring/useDocumentScale", true};
// Привязка к вершинам и концам путей страницы; ортогональная привязка (отрезки — по горизонтали и вертикали).
inline constexpr BoolKey kMeasureSnap{"measuring/snapToPaths", true};
inline constexpr BoolKey kMeasureOrtho{"measuring/ortho", false};
// Пометка расстояния: концы линии, выносные линии (пт), подпись в разрыве линии или над ней.
inline constexpr StringKey kMeasureLineEnds{"measuring/lineEnds", "arrow", "arrow|tick|none"};
inline constexpr IntKey kMeasureLeaderLength{"measuring/leaderLength", 0, 0, 72};
inline constexpr IntKey kMeasureLeaderExtension{"measuring/leaderExtension", 0, 0, 72};
inline constexpr IntKey kMeasureLeaderOffset{"measuring/leaderOffset", 0, 0, 72};
inline constexpr StringKey kMeasureCaption{"measuring/caption", "inline", "inline|top"};
// Окно «Сведения об измерении» при включённом инструменте.
inline constexpr BoolKey kMeasureShowInfo{"measuring/showInfo", true};

}  // namespace ot::settings
