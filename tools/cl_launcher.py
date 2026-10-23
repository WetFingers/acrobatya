"""Обёртка компилятора MSVC для Ninja (CMAKE_<LANG>_COMPILER_LAUNCHER, см. CMakeLists.txt).

У Build Tools на этой машине только русские ресурсы (VC\\Tools\\MSVC\\…\\bin\\Hostx64\\x64\\1049, VSLANG не
помогает), а системные кодировки — 1252/850: строка /showIncludes «Примечание: включение файла:» приходит как
UTF-8 (консоль 65001), CP866 или «??????????» (без консоли). Ninja узнаёт её только по точному префиксу
(msvc_deps_prefix), иначе не пишет зависимости от заголовков — и правка .h не пересобирает зависящие .cpp.
Обёртка запускает компилятор как есть и заменяет локализованный префикс строк /showIncludes на
«Note: including file:» (его задаёт CMakeLists.txt); остальной вывод, код выхода и stderr — без изменений.
"""

import re
import subprocess
import sys

# Префикс /showIncludes: два поля без «:», «\», «/», «(», «)» и двоеточие после каждого, затем пробелы и
# абсолютный путь. Строки диагностики начинаются с пути («C:\…\x.cpp(12): error …») и сюда не попадают.
_INCLUDE = re.compile(rb"^[^:\\/()\r\n]+:[^:\\/()\r\n]+:( +[A-Za-z]:[\\/])")
_ENGLISH = b"Note: including file:"


def main() -> int:
    if len(sys.argv) < 2:
        sys.stderr.write("cl_launcher: no compiler command\n")
        return 2
    proc = subprocess.run(sys.argv[1:], stdout=subprocess.PIPE)
    out = sys.stdout.buffer
    for line in proc.stdout.splitlines(keepends=True):
        if line.startswith(_ENGLISH):
            out.write(line)
            continue
        m = _INCLUDE.match(line)
        out.write(_ENGLISH + line[m.start(1):] if m else line)
    out.flush()
    return proc.returncode


if __name__ == "__main__":
    sys.exit(main())
