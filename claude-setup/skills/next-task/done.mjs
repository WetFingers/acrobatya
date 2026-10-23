// Пункт готов: коммит и отметка в плане — одной командой, а не тремя вызовами.
//
//   node .claude/skills/next-task/done.mjs "этап 25: что сделано" "кусок текста пункта"
//   node .claude/skills/next-task/done.mjs --manual "этап 34: …" "кусок"   → [~] вместо [x]
//
// 1. `git add -A` и `git commit` с этим сообщением (нечего коммитить — шаг пропускается).
// 2. В `docs/plan.md` строка `- [ ] …`, где есть этот кусок, становится `- [x] …`.
//    Кусок должен найтись ровно в одной невыполненной строке — иначе ошибка и ничего не отмечено.
// Отметка остаётся в рабочей копии и уходит со следующим коммитом — как и прежде:
// отметка после коммита, чтобы оборванная сессия не оставила отмеченным несделанное.

import { execFileSync } from "node:child_process";
import { readFileSync, writeFileSync } from "node:fs";
import process from "node:process";

const args = process.argv.slice(2);
const manual = args[0] === "--manual";
const [message, fragment] = manual ? args.slice(1) : args;
if (!message || !fragment) {
  console.error('Нужно: done.mjs [--manual] "этап N: что сделано" "кусок текста пункта"');
  process.exit(2);
}

const plan = "docs/plan.md";
const text = readFileSync(plan, "utf8");
const eol = text.includes("\r\n") ? "\r\n" : "\n";
const lines = text.split(eol);
const hits = lines
  .map((line, i) => ({ line, i }))
  .filter(({ line }) => line.startsWith("- [ ] ") && line.includes(fragment));
if (hits.length !== 1) {
  console.error(
    hits.length === 0
      ? `В плане нет невыполненного пункта с «${fragment}»`
      : `«${fragment}» есть в ${hits.length} пунктах — нужен кусок длиннее:\n` +
          hits.map((h) => `  ${h.line.slice(0, 100)}`).join("\n"),
  );
  process.exit(1);
}

execFileSync("git", ["add", "-A"], { stdio: "inherit" });
const staged = execFileSync("git", ["diff", "--cached", "--name-only"], { encoding: "utf8" }).trim();
if (staged) {
  execFileSync("git", ["commit", "-q", "-m", message], { stdio: "inherit" });
  console.log(`коммит: ${message}`);
} else {
  console.log("нечего коммитить — только отметка");
}

const { i } = hits[0];
lines[i] = lines[i].replace("- [ ] ", manual ? "- [~] " : "- [x] ");
writeFileSync(plan, lines.join(eol), "utf8");
console.log(`отмечено ${manual ? "[~]" : "[x]"}: ${lines[i].slice(6, 90)}…`);
