# -*- coding: utf-8 -*-
"""make_enc.py — матрица шифрования: tests/fixtures/enc/*.pdf + passwords.json.

Что (основа — свой plain.pdf, 2 страницы, кириллица и латиница):
  enc-rc4-40.pdf            — RC4 40 бит, V1/R2 (qpdf --allow-weak-crypto --bits=40);
  enc-rc4-128.pdf           — RC4 128 бит, V2/R3, ограничения: печать low, без правки и извлечения;
  enc-aes-128.pdf           — AES-128, V4/R4 (qpdf --use-aes=y);
  enc-aes-256-r6.pdf        — AES-256, V5/R6 (pyHanko, ISO 32000-2);
  enc-aes-256-emptyuser.pdf — AES-256 R6, пустой пароль пользователя (открывается без пароля);
  enc-aes-256-unicode.pdf   — AES-256 R6, кириллический пароль пользователя «пароль» (SASLprep, UTF-8);
  passwords.json            — пароли, V/R, шифр, права (по qpdf --show-encryption).
Пароли: пользователь «user», владелец «owner» (кроме указанных выше).
Откуда: qpdf 12.4.2 (C:\\Tools\\qpdf) для R2/R3/R4; pyHanko 0.38.0 для R6.
Детерминизм: qpdf — --static-id и --static-aes-iv (тестовые ключи qpdf; --deterministic-id
с шифрованием qpdf не принимает); R6 у qpdf берёт соли и ключ файла из системного ГСЧ,
поэтому R6 делает pyHanko с солями из _genlib.DetRandom(seed) — два прогона дают одинаковые байты.
Запуск: "C:\\Tools\\venv-test\\Scripts\\python.exe" -X utf8 -I tools/gen/make_enc.py [--out <корень>]
"""
import re
import sys
from pathlib import Path

sys.dont_write_bytecode = True                     # без __pycache__ в tools/gen
sys.path.insert(0, str(Path(__file__).resolve().parent))

import _genlib as g  # noqa: E402
import _texts as tx  # noqa: E402


def plain(out: Path):
    doc = g.Doc("Шифрование: основа", "tests/fixtures/enc/plain.pdf", creator="tools/gen/make_enc.py")
    f = doc.font("arial")
    fb = doc.font("arialbd")
    for n in (1, 2):
        cv = doc.canvas()
        cv.text(72, 760, "Секретный документ — страница %d" % n, fb, 18)
        y = 720
        for par in (tx.RU[6:9] if n == 1 else tx.EN[:3]):
            y = cv.paragraph(72, y, 450, par, f, 11) - 8
        cv.text(72, 100, "Пароль пользователя: user · владельца: owner", f, 9, rgb=(0.4, 0.4, 0.4))
        doc.add_page(cv)
    doc.save(out)


def qpdf_enc(src: Path, dst: Path, user: str, owner: str, bits: int, extra=()):
    cmd = [g.qpdf_path(), "--static-id", "--static-aes-iv"]
    if bits in (40,) or "--use-aes=n" in extra:
        cmd.append("--allow-weak-crypto")
    cmd += ["--encrypt", "--user-password=" + user, "--owner-password=" + owner, "--bits=%d" % bits]
    cmd += list(extra)
    cmd += ["--", str(src), str(dst)]
    g.run(cmd)


def show(dst: Path, pw: str) -> dict:
    r = g.run([g.qpdf_path(), "--show-encryption", "--password=" + pw, str(dst)], check=False)
    text = r.stdout.decode("utf-8", "replace")
    info = {"R": None, "P": None, "perms": {}}
    for line in text.splitlines():
        m = re.match(r"^R = (\d+)", line)
        if m:
            info["R"] = int(m.group(1))
        m = re.match(r"^P = (-?\d+)", line)
        if m:
            info["P"] = int(m.group(1))
        m = re.match(r"^(.+): (allowed|not allowed)$", line)
        if m:
            info["perms"][m.group(1)] = m.group(2) == "allowed"
        m = re.match(r"^(stream|string|file) encryption method: (\S+)", line)
        if m:
            info[m.group(1) + "_method"] = m.group(2)
    return info


def main():
    args = g.common_args(__doc__)
    root = Path(args.out)
    dst = root / "tests" / "fixtures" / "enc"
    dst.mkdir(parents=True, exist_ok=True)
    base = dst / "plain.pdf"
    plain(base)
    files = []
    qpdf_enc(base, dst / "enc-rc4-40.pdf", "user", "owner", 40)
    files.append(("enc-rc4-40.pdf", "user", "owner", "RC4-40", 1, "qpdf"))
    qpdf_enc(base, dst / "enc-rc4-128.pdf", "user", "owner", 128,
             ["--use-aes=n", "--print=low", "--modify=none", "--extract=n"])
    files.append(("enc-rc4-128.pdf", "user", "owner", "RC4-128", 2, "qpdf"))
    qpdf_enc(base, dst / "enc-aes-128.pdf", "user", "owner", 128, ["--use-aes=y"])
    files.append(("enc-aes-128.pdf", "user", "owner", "AES-128", 4, "qpdf"))
    g.encrypt_aes256_r6(base, dst / "enc-aes-256-r6.pdf", owner="owner", user="user", seed=g.SEED)
    files.append(("enc-aes-256-r6.pdf", "user", "owner", "AES-256", 5, "pyhanko"))
    g.encrypt_aes256_r6(base, dst / "enc-aes-256-emptyuser.pdf", owner="owner", user="", seed=g.SEED + 1)
    files.append(("enc-aes-256-emptyuser.pdf", "", "owner", "AES-256", 5, "pyhanko"))
    g.encrypt_aes256_r6(base, dst / "enc-aes-256-unicode.pdf", owner="владелец", user="пароль", seed=g.SEED + 2)
    files.append(("enc-aes-256-unicode.pdf", "пароль", "владелец", "AES-256", 5, "pyhanko"))
    items = []
    for name, user, owner, cipher, v, tool in files:
        p = dst / name
        info = show(p, owner)
        txt = g.pdf_text(p, password=user if user else None)
        if "Секретный документ" not in txt:
            raise RuntimeError("%s: текст не извлечён с паролем пользователя" % name)
        txt = g.pdf_text(p, password=owner)
        if "Секретный документ" not in txt:
            raise RuntimeError("%s: текст не извлечён с паролем владельца" % name)
        items.append({"file": name, "cipher": cipher, "V": v, "R": info["R"], "P": info["P"],
                      "user_password": user, "owner_password": owner,
                      "opens_without_password": user == "", "tool": tool,
                      "permissions": info["perms"]})
        g.say("enc: %-26s R%s P=%s %6d байт" % (name, info["R"], info["P"], p.stat().st_size))
    g.write_json(dst / "passwords.json", {"generator": "tools/gen/make_enc.py", "plain": "plain.pdf",
                                          "pages": 2, "text_contains": "Секретный документ", "files": items})


if __name__ == "__main__":
    main()
