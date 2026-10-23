"""Тихая установка, самопроверка и удаление «АкроБати» (ctest-метка build, только рубеж и выпуск).

    "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tests/build/install_test.py --root <репозиторий>
        [--setup dist\\AcroBatya-<версия>-setup.exe] [--keep] [--smoke СЕКУНДЫ]

0. Защита данных человека: тот же AppId (из installer/ottisk.iss) уже установлен (запись удаления
   …\\Uninstall\\{AppId}_is1 в HKCU/HKLM) или в %LOCALAPPDATA%\\AcroBatya\\autosave есть файлы — отказ с
   кодом 1 и объяснением: тихая установка в другой каталог перехватила бы ярлык «Пуск» и запись удаления
   установленной копии, а деинсталляция стёрла бы autosave человека. Обойти — OTTISK_INSTALL_TEST_FORCE=1.

1. Установщик — `--setup` или `dist/AcroBatya-<версия>-setup.exe` (версия — из cmake/product.cmake,
   `set(OTTISK_VERSION "x.y.z")`). Нет установщика — сообщение и код 1.
2. `<setup> /VERYSILENT /SUPPRESSMSGBOXES /NORESTART /CURRENTUSER /DIR=<временный каталог> /TASKS=""`:
   без ярлыка на рабочем столе и без записей ассоциации в реестр. С `OTTISK_ALLOW_REGISTRY=1`
   ставится с `/TASKS="fileassoc"` и проверяются HKCU\\Software\\Classes\\AcroBatya.pdf и
   RegisteredApplications\\AcroBatya (после удаления — что их нет; эталон 10.4); значение
   .pdf\\OpenWithProgids\\AcroBatya.pdf деинсталляция по правилу Windows оставляет — тест это проверяет и,
   если до установки значения не было, удаляет его сам (машина возвращается в исходное состояние).
   Установщик Inno Setup всё равно пишет свою запись удаления в HKCU (…\\Uninstall\\{AppId}_is1)
   и ярлык в меню «Пуск» — деинсталляция их убирает.
3. Файлы: AcroBatya.exe, Qt6Core.dll, platforms\\qwindows.dll и qoffscreen.dll (в plugins\\ или рядом),
   Microsoft.VC143.CRT или msvcp140.dll, qpdf.exe, tessdata\\rus.traineddata, unins000.exe.
4. `<dir>\\AcroBatya.exe --self-check` дважды, с PATH только из системных папок и данными во временном
   каталоге: без QT_QPA_PLATFORM (в stderr нет «Could not load the Qt platform plugin») и с
   QT_QPA_PLATFORM=offscreen — оба с кодом 0, в выводе есть «version: <версия>», «qt:», «mupdf:», «data:».
   --self-check работает на QCoreApplication и платформенный плагин не грузит, поэтому плагины
   проверяются отдельно: `--shots home --out <tmp>` под QT_QPA_PLATFORM=offscreen (+ QT_QPA_FONTDIR) —
   код 0 и непустой PNG (грузится plugins\\platforms\\qoffscreen.dll установленной копии), и
   `--smoke N` без QT_QPA_PLATFORM (окно на экране N секунд, qwindows.dll; по умолчанию 3, 0 — не запускать).
5. `unins000.exe /VERYSILENT /SUPPRESSMSGBOXES /NORESTART`, каталог программы должен исчезнуть
   (ждём до 60 с: вторая фаза удаления стирает unins000.exe после выхода первой).
   `--keep` — не удалять (для ручного осмотра; каталог печатается).
Код выхода: 0 — всё прошло, 1 — нет установщика или проверка не прошла.
Только стандартная библиотека Python.
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
import tempfile
import time
from pathlib import Path

PLATFORM_ERROR = "Could not load the Qt platform plugin"
SELF_CHECK_KEYS = ("version:", "qt:", "mupdf:", "data:")
REGISTRY_FLAG = "OTTISK_ALLOW_REGISTRY"
FORCE_FLAG = "OTTISK_INSTALL_TEST_FORCE"
PROG_ID = "AcroBatya.pdf"
OPEN_WITH_KEY = r"Software\Classes\.pdf\OpenWithProgids"


def say(text: str) -> None:
    print(text, flush=True)


class ExitOneParser(argparse.ArgumentParser):
    """Ошибка в аргументах — код 1 (как любой провал теста), а не 2 по умолчанию argparse."""

    def error(self, message: str):  # type: ignore[override]
        self.print_usage(sys.stderr)
        self.exit(1, f"{self.prog}: ошибка аргументов: {message}\n")


def product_version(root: Path) -> str | None:
    path = root / "cmake" / "product.cmake"
    if not path.is_file():
        return None
    match = re.search(r'set\s*\(\s*OTTISK_VERSION\s+"?(\d+\.\d+\.\d+)"?\s*\)', path.read_text(encoding="utf-8"))
    return match.group(1) if match else None


def app_id(root: Path) -> str | None:
    """GUID из строки AppId={{…} в installer/ottisk.iss (без фигурных скобок)."""
    path = root / "installer" / "ottisk.iss"
    if not path.is_file():
        return None
    match = re.search(r"^AppId=\{\{([0-9A-Fa-f-]{36})\}", path.read_text(encoding="utf-8-sig"), re.M)
    return match.group(1) if match else None


def installed_copy(guid: str) -> str | None:
    """Каталог уже установленного «АкроБати» с тем же AppId (реестр только читается) или None."""
    import winreg

    subkey = rf"Software\Microsoft\Windows\CurrentVersion\Uninstall\{{{guid}}}_is1"
    for hive in (winreg.HKEY_CURRENT_USER, winreg.HKEY_LOCAL_MACHINE):
        try:
            with winreg.OpenKey(hive, subkey) as key:
                try:
                    return str(winreg.QueryValueEx(key, "InstallLocation")[0]) or subkey
                except OSError:
                    return subkey
        except OSError:
            continue
    return None


def user_autosave_files() -> int:
    """Файлов в %LOCALAPPDATA%\\AcroBatya\\autosave человека — их стёр бы [UninstallDelete] установщика."""
    base = os.environ.get("LOCALAPPDATA")
    if not base:
        return 0
    autosave = Path(base) / "AcroBatya" / "autosave"
    if not autosave.is_dir():
        return 0
    return sum(1 for item in autosave.rglob("*") if item.is_file())


def open_with_value_exists() -> bool:
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, OPEN_WITH_KEY) as key:
            winreg.QueryValueEx(key, PROG_ID)
        return True
    except OSError:
        return False


def remove_open_with_value() -> bool:
    import winreg

    try:
        with winreg.OpenKey(winreg.HKEY_CURRENT_USER, OPEN_WITH_KEY, 0, winreg.KEY_SET_VALUE) as key:
            winreg.DeleteValue(key, PROG_ID)
        return True
    except OSError:
        return False


def system_path() -> str:
    windir = os.environ.get("SystemRoot", r"C:\Windows")
    return os.pathsep.join(
        [rf"{windir}\System32", windir, rf"{windir}\System32\Wbem", rf"{windir}\System32\WindowsPowerShell\v1.0"]
    )


def clean_env(data_dir: Path, platform: str | None) -> dict[str, str]:
    env = {k: v for k, v in os.environ.items() if not k.upper().startswith(("QT_", "OTTISK_"))}
    env["PATH"] = system_path()  # установленная копия должна обходиться без Qt и C:\Tools в PATH
    env["OTTISK_DATA_DIR"] = str(data_dir)
    env["OTTISK_INSTANCE"] = "install-test"
    env["QT_FORCE_STDERR_LOGGING"] = "1"
    if platform:
        env["QT_QPA_PLATFORM"] = platform
    if platform == "offscreen":
        # offscreen берёт шрифты только из QT_QPA_FONTDIR — без неё вместо текста квадраты
        windir = os.environ.get("SystemRoot", r"C:\Windows")
        env["QT_QPA_FONTDIR"] = (Path(windir) / "Fonts").as_posix()
    return env


def run(command: list[str], timeout: int, env: dict[str, str] | None = None) -> subprocess.CompletedProcess:
    return subprocess.run(command, capture_output=True, timeout=timeout, env=env,
                          encoding="utf-8", errors="replace")


class Checker:
    def __init__(self) -> None:
        self.failures: list[str] = []

    def check(self, ok: bool, what: str) -> bool:
        say(f"  {'ok ' if ok else 'НЕТ'} {what}")
        if not ok:
            self.failures.append(what)
        return ok


def find_platform_dir(app: Path) -> Path | None:
    for candidate in (app / "plugins" / "platforms", app / "platforms"):
        if (candidate / "qwindows.dll").is_file():
            return candidate
    return None


def registry_values() -> dict[str, bool]:
    import winreg

    def key_exists(path: str) -> bool:
        try:
            winreg.CloseKey(winreg.OpenKey(winreg.HKEY_CURRENT_USER, path))
            return True
        except OSError:
            return False

    def value_exists(path: str, name: str) -> bool:
        try:
            with winreg.OpenKey(winreg.HKEY_CURRENT_USER, path) as key:
                winreg.QueryValueEx(key, name)
            return True
        except OSError:
            return False

    return {
        "ProgID AcroBatya.pdf": key_exists(r"Software\Classes\AcroBatya.pdf\shell\open\command"),
        "Capabilities": key_exists(r"Software\AcroBatya\Capabilities\FileAssociations"),
        "RegisteredApplications\\AcroBatya": value_exists(r"Software\RegisteredApplications", "AcroBatya"),
    }


def self_check(checker: Checker, exe: Path, version: str | None, data_dir: Path, platform: str | None) -> None:
    label = f"QT_QPA_PLATFORM={platform}" if platform else "без QT_QPA_PLATFORM"
    try:
        result = run([str(exe), "--self-check"], timeout=120, env=clean_env(data_dir, platform))
    except subprocess.TimeoutExpired:
        checker.check(False, f"--self-check ({label}) завершился за 120 с")
        return
    for line in result.stdout.splitlines():
        say(f"      {line}")
    checker.check(result.returncode == 0, f"--self-check ({label}): код {result.returncode}")
    if result.returncode != 0:
        for line in result.stderr.splitlines()[-20:]:
            say(f"      stderr: {line}")
    checker.check(PLATFORM_ERROR not in result.stderr, f"--self-check ({label}): в stderr нет «{PLATFORM_ERROR}»")
    lower = result.stdout.lower()
    missing = [key for key in SELF_CHECK_KEYS if key not in lower]
    checker.check(not missing, f"--self-check ({label}): строки {', '.join(SELF_CHECK_KEYS)}"
                  + (f" — нет {', '.join(missing)}" if missing else ""))
    if version:
        version_line = next((l for l in result.stdout.splitlines() if l.lower().startswith("version:")), "")
        checker.check(version in version_line, f"--self-check ({label}): version: содержит {version}")


def offscreen_shot(checker: Checker, exe: Path, data_dir: Path, out_dir: Path) -> None:
    """--shots home под offscreen: грузится plugins\\platforms\\qoffscreen.dll установленной копии."""
    out_dir.mkdir(parents=True, exist_ok=True)
    try:
        result = run([str(exe), "--shots", "home", "--out", str(out_dir)], timeout=120,
                     env=clean_env(data_dir, "offscreen"))
    except subprocess.TimeoutExpired:
        checker.check(False, "--shots home (offscreen) завершился за 120 с")
        return
    checker.check(result.returncode == 0, f"--shots home (QT_QPA_PLATFORM=offscreen): код {result.returncode}")
    if result.returncode != 0:
        for line in result.stderr.splitlines()[-20:]:
            say(f"      stderr: {line}")
    checker.check(PLATFORM_ERROR not in result.stderr, f"--shots home: в stderr нет «{PLATFORM_ERROR}»")
    pngs = [p for p in sorted(out_dir.glob("home*.png")) if p.stat().st_size > 0]
    checker.check(bool(pngs), "--shots home: непустой PNG"
                  + (f" ({pngs[0].name}, {pngs[0].stat().st_size} байт)" if pngs else ""))


def smoke(checker: Checker, exe: Path, seconds: int, data_dir: Path) -> None:
    try:
        result = run([str(exe), "--smoke", str(seconds)], timeout=seconds + 60, env=clean_env(data_dir, None))
    except subprocess.TimeoutExpired:
        checker.check(False, f"--smoke {seconds} завершился за {seconds + 60} с")
        return
    checker.check(result.returncode == 0, f"--smoke {seconds} (окно, без QT_QPA_PLATFORM): код {result.returncode}")
    checker.check(PLATFORM_ERROR not in result.stderr, f"--smoke: в stderr нет «{PLATFORM_ERROR}»")


def wait_removed(path: Path, seconds: int) -> bool:
    deadline = time.monotonic() + seconds
    while time.monotonic() < deadline:
        if not path.exists():
            return True
        time.sleep(1)
    return not path.exists()


def main() -> int:
    for stream in (sys.stdout, sys.stderr):
        if hasattr(stream, "reconfigure"):
            stream.reconfigure(encoding="utf-8", errors="replace")
    parser = ExitOneParser(description="Тихая установка, --self-check и удаление «АкроБати»")
    parser.add_argument("--root", type=Path, default=Path(__file__).resolve().parents[2],
                        help="корень репозитория (по умолчанию — два уровня выше tests/build)")
    parser.add_argument("--setup", type=Path, help="установщик (по умолчанию dist/AcroBatya-<версия>-setup.exe)")
    parser.add_argument("--keep", action="store_true", help="не удалять программу после проверок")
    parser.add_argument("--smoke", type=int, default=3, metavar="СЕКУНДЫ",
                        help="AcroBatya.exe --smoke N без QT_QPA_PLATFORM — окно на экране (по умолчанию 3, 0 — нет)")
    options = parser.parse_args()

    root = options.root.resolve()
    if not root.is_dir():
        say(f"нет каталога репозитория: {root}")
        return 1
    version = product_version(root)
    if options.setup:
        setup = options.setup if options.setup.is_absolute() else (Path.cwd() / options.setup)
    elif version:
        setup = root / "dist" / f"AcroBatya-{version}-setup.exe"
    else:
        candidates = sorted((root / "dist").glob("AcroBatya-*-setup.exe"), key=lambda p: p.stat().st_mtime)
        setup = candidates[-1] if candidates else root / "dist" / "AcroBatya-<версия>-setup.exe"
    if not setup.is_file():
        say(f"нет установщика: {setup}")
        if not version and not options.setup:
            say("  (нет cmake/product.cmake с set(OTTISK_VERSION \"x.y.z\") — версия неизвестна)")
        say("  соберите: powershell -NoProfile -ExecutionPolicy Bypass -File tools\\build.ps1")
        return 1
    if not version:
        match = re.search(r"AcroBatya-(\d+\.\d+\.\d+)-setup\.exe$", setup.name)
        version = match.group(1) if match else None

    allow_registry = os.environ.get(REGISTRY_FLAG) == "1"
    guid = app_id(root)
    existing = installed_copy(guid) if guid else None
    autosave = user_autosave_files()
    if (existing or autosave) and os.environ.get(FORCE_FLAG) != "1":
        if existing:
            say(f"«АкроБатя» уже установлен (AppId {{{guid}}}): {existing}")
            say("  тихая установка с тем же AppId в другой каталог перехватила бы его ярлык в «Пуске» и запись")
            say("  удаления, а деинсталляция тестовой копии оставила бы установленную без них")
        if autosave:
            say(f"в %LOCALAPPDATA%\\AcroBatya\\autosave {autosave} файлов — деинсталляция ([UninstallDelete]) стёрла бы их")
        say(f"  удалите установленную копию / сохраните документы или задайте {FORCE_FLAG}=1")
        return 1
    if not guid:
        say("  (AppId в installer/ottisk.iss не найден — проверка установленной копии пропущена)")
    open_with_before = open_with_value_exists() if allow_registry else False
    work = Path(tempfile.mkdtemp(prefix="ottisk-install-"))
    app = work / "app"
    data_dir = work / "data"
    data_dir.mkdir()
    checker = Checker()
    say(f"установщик: {setup} ({setup.stat().st_size / 1048576:.1f} МБ), версия {version or '?'}")
    say(f"каталог установки: {app}; реестр ассоциаций: {'да' if allow_registry else 'нет'} ({REGISTRY_FLAG})")

    tasks = "fileassoc" if allow_registry else ""
    started = time.monotonic()
    try:
        result = run([str(setup), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART", "/CURRENTUSER",
                      f"/DIR={app}", f"/TASKS={tasks}", f"/LOG={work / 'setup.log'}"], timeout=600)
    except subprocess.TimeoutExpired:
        say("установщик не завершился за 600 с")
        return 1
    if not checker.check(result.returncode == 0 and (app / "AcroBatya.exe").is_file(),
                         f"тихая установка: код {result.returncode}, {time.monotonic() - started:.1f} с"):
        say(f"  журнал установки: {work / 'setup.log'}")
        if (app / "unins000.exe").is_file():  # установка прошла частично — убрать за собой
            try:
                run([str(app / "unins000.exe"), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=600)
            except subprocess.TimeoutExpired:
                pass
        return 1

    say("файлы:")
    checker.check((app / "AcroBatya.exe").is_file(), "AcroBatya.exe")
    checker.check((app / "Qt6Core.dll").is_file(), "Qt6Core.dll рядом с AcroBatya.exe")
    platforms = find_platform_dir(app)
    checker.check(platforms is not None, "platforms\\qwindows.dll (plugins\\platforms или platforms)")
    checker.check(platforms is not None and (platforms / "qoffscreen.dll").is_file(), "qoffscreen.dll рядом с qwindows.dll")
    checker.check((app / "Microsoft.VC143.CRT").is_dir() or (app / "msvcp140.dll").is_file(),
                  "CRT: Microsoft.VC143.CRT или msvcp140.dll")
    checker.check((app / "qpdf.exe").is_file(), "qpdf.exe")
    checker.check((app / "tessdata" / "rus.traineddata").is_file(), "tessdata\\rus.traineddata")
    checker.check((app / "unins000.exe").is_file(), "unins000.exe")
    if allow_registry:
        for name, present in registry_values().items():
            checker.check(present, f"реестр HKCU: {name}")

    say("самопроверка:")
    exe = app / "AcroBatya.exe"
    self_check(checker, exe, version, data_dir, None)
    self_check(checker, exe, version, data_dir, "offscreen")
    say("платформенные плагины:")
    offscreen_shot(checker, exe, data_dir, work / "shots")
    if options.smoke > 0:
        smoke(checker, exe, options.smoke, data_dir)

    if options.keep:
        say(f"--keep: программа оставлена в {app} (удаление: {app / 'unins000.exe'} /VERYSILENT)")
    else:
        say("удаление:")
        uninstaller = app / "unins000.exe"
        try:
            result = run([str(uninstaller), "/VERYSILENT", "/SUPPRESSMSGBOXES", "/NORESTART"], timeout=600)
            checker.check(result.returncode == 0, f"unins000.exe /VERYSILENT: код {result.returncode}")
        except subprocess.TimeoutExpired:
            checker.check(False, "unins000.exe завершился за 600 с")
        checker.check(wait_removed(app, 60), f"каталог программы удалён: {app}")
        if allow_registry:
            for name, present in registry_values().items():
                checker.check(not present, f"реестр HKCU после удаления: нет {name}")
            if not open_with_before:
                checker.check(open_with_value_exists(),
                              f"реестр HKCU после удаления: .pdf\\OpenWithProgids\\{PROG_ID} остаётся (правило Windows)")
                checker.check(remove_open_with_value(), f"тест убрал за собой .pdf\\OpenWithProgids\\{PROG_ID}")
        shutil.rmtree(work, ignore_errors=True)

    if checker.failures:
        say(f"ПРОВАЛ: {len(checker.failures)} проверок не прошли")
        for failure in checker.failures:
            say(f"  - {failure}")
        return 1
    say("установка, самопроверка и удаление — ок")
    return 0


if __name__ == "__main__":
    sys.exit(main())
