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

// --- Документы (documents/; автосохранение, восстановление и PDF/A в окне — сессия 2.5) ---
inline constexpr BoolKey kRestoreLastView{"documents/restoreLastView", true};
inline constexpr BoolKey kAutosave{"documents/autosave", true};
inline constexpr IntKey kAutosaveMinutes{"documents/autosaveMinutes", 5, 1, 30};  // AutosaveRunner: 1–30
inline constexpr BoolKey kOfferRecovery{"documents/offerRecovery", true};
inline constexpr BoolKey kPdfaReadOnly{"documents/pdfaReadOnly", true};

// --- Доверие (trust/; Диспетчер доверия целиком — 7.6) ---
inline constexpr BoolKey kAllowOpenAttachments{"trust/allowOpenAttachments", true};

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

}  // namespace ot::settings
