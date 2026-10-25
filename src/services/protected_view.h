// Защищённый просмотр для чужих файлов (план 2.5b; эталон 7.5; security-35, security-37; замена песочницы
// security-34): файл «чужой», если у него поток Zone.Identifier (Mark-of-the-Web) с ZoneId ≥ 3 (Интернет,
// ненадёжные узлы: скачан браузером, сохранён из почты), или он лежит во временной папке (%TEMP%, кеш
// вложений Outlook), или открыт как PDF-вложение. Чужой файл окно открывает только для чтения: правки и
// сохранение запрещены (печать — можно), JavaScript выключен, внешние ссылки и SubmitForm не выполняются,
// вложения — только «Сохранить…», жёлтая полоса «[Включить все функции]». «Включить» делает файл своим —
// запись в state.sqlite (trusted_files, scope 'protectedView') по отпечатку содержимого fileKey; сам
// Zone.Identifier не трогается. Это не песочница: устойчивость парсера держат mu::call, ASan и фаззинг
// (эталон 7.5, 9.5).
//
// Настройки (Установки → Диспетчер доверия, 7.6; settings_keys.h): trust/protectedView — off | unsafe | all,
// trust/trustedFolders — привилегированные папки (файлы в них и в подпапках — свои). Поток: любой; запросы к
// базе — под StateDb::mutex().
#pragma once

#include <QString>
#include <QStringList>

#include <optional>

namespace ot {

class Settings;
class StateDb;

namespace protected_view {

// Почему файл открыт в защищённом просмотре (None — не открыт).
enum class Origin { None, Internet, TempFolder, Attachment, AllFiles };

// ZoneId из потока <путь>:Zone.Identifier ([ZoneTransfer] ZoneId=N; ANSI, UTF-8 или UTF-16 с BOM); −1 —
// потока нет, файла нет или ZoneId не указан. 0 — компьютер, 1 — интрасеть, 2 — надёжные, 3 — Интернет, 4 —
// ненадёжные.
int zoneId(const QString& path);
// Записать поток Zone.Identifier (тесты, сцены; так помечает файлы браузер). false — не NTFS или нет доступа.
bool writeZoneIdentifier(const QString& path, int zone, const QString& hostUrl = {});

// Временные папки: %TEMP% и кеш вложений почты (%LOCALAPPDATA%\Microsoft\Windows\INetCache). Тесты подменяют
// список (папки самих тестов лежат в %TEMP%): nullopt — снова настоящий.
QStringList tempFolders();
void setTempFoldersForTesting(std::optional<QStringList> folders);
// path лежит в folder или его подпапке (без учёта регистра, по полному пути).
bool isUnder(const QString& path, const QString& folder);

// Отпечаток файла: SHA-256 первых 64 КиБ и размер (тот же ключ — «JavaScript для этого файла», эталон 7.2);
// пусто — файла нет.
QString fileKey(const QString& path);

// Свой ли файл по «Включить все функции» (state.sqlite).
bool isTrusted(const QString& path);
bool isTrusted(const QString& path, StateDb& db);
// «Включить все функции»: запомнить отпечаток файла. false — файла нет или база не открыта.
bool trust(const QString& path);
bool trust(const QString& path, StateDb& db);
// То же для другой области доверия (scope): «protectedView», «javascript» (6.1: «Включить для этого файла»,
// эталон 7.2, security-40) — один ключ fileKey, своя строка trusted_files.
bool isTrustedFor(const QString& path, const QString& scope, StateDb& db);
bool trustFor(const QString& path, const QString& scope, StateDb& db);

// Решение для открываемого файла (attachment — PDF-вложение из временной папки): режим, свои файлы и
// привилегированные папки, вложение, Zone.Identifier, временные папки.
Origin check(const QString& path, bool attachment = false);
Origin check(const QString& path, bool attachment, const Settings& settings, StateDb& db);

}  // namespace protected_view
}  // namespace ot
