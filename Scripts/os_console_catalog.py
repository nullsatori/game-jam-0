"""The console catalog: every console command and variable the C++ registers, into Docs/Console.md.

Scans Source/ for registrations - FAutoConsoleCommand* and FAutoConsoleVariable* objects,
TAutoConsoleVariable<T>, IConsoleManager::Get().RegisterConsoleCommand / RegisterConsoleVariable*,
and calls of a file's own helper that wraps RegisterConsole* (OSDebug.cpp's OSDebugSystemCommand) -
and writes one table per prefix: the name, its arguments (what the help string opens with after the
name, up to ':'), the help string and the file. Registrations under a UE_BUILD_SHIPPING condition are
marked «не в Shipping».

    python Scripts/os_console_catalog.py           # rewrite Docs/Console.md
    python Scripts/os_console_catalog.py --check   # exit 1 when Docs/Console.md is stale (CI, before a merge)

Plain Python 3, no Unreal: it reads the source as text. Paths come from the repo root (this file's
parent's parent), never from a project name.
"""
import argparse
import os
import re
import sys

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCE = os.path.join(ROOT, "Source")
OUT = os.path.join(ROOT, "Docs", "Console.md")

# Object types whose constructor registers: (name, help, ...) - or (name, default, help, ...) for variables.
COMMAND_TYPES = re.compile(r"^FAutoConsoleCommand\w*$")
VARIABLE_TYPES = re.compile(r"^(TAutoConsoleVariable|FAutoConsoleVariable\w*)$")
REGISTER_CALLS = {"RegisterConsoleCommand": "command", "RegisterConsoleVariable": "variable",
                  "RegisterConsoleVariableRef": "variable"}


def strip_comments(text):
    """Comments out, strings and line breaks kept (so offsets map to lines)."""
    out = []
    i = 0
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            out.append(text[i:j + 1])
            i = j + 1
        elif text.startswith("//", i):
            j = text.find("\n", i)
            j = n if j < 0 else j
            i = j
        elif text.startswith("/*", i):
            j = text.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append("\n" * text.count("\n", i, j))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


def call_args(text, open_paren):
    """The top-level arguments of the call whose '(' is at open_paren, as stripped strings."""
    depth = 0
    args = []
    start = open_paren + 1
    i = open_paren
    n = len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            j = i + 1
            while j < n and text[j] != c:
                j += 2 if text[j] == "\\" else 1
            i = j + 1
            continue
        if c in "([{":
            depth += 1
        elif c in ")]}":
            depth -= 1
            if depth == 0:
                args.append(text[start:i].strip())
                return args
        elif c == "," and depth == 1:
            args.append(text[start:i].strip())
            start = i + 1
        i += 1
    return args


LITERAL = re.compile(r'(?:TEXT\s*\(\s*)?"((?:[^"\\]|\\.)*)"\s*\)?')


def literal(arg):
    """The text of an argument made only of string literals (TEXT("a") TEXT("b") joined), else None."""
    if not arg:
        return None
    pos = 0
    parts = []
    while pos < len(arg):
        m = LITERAL.match(arg, pos)
        if not m:
            return None
        parts.append(re.sub(r"\\(.)", lambda e: {"n": " ", "t": " "}.get(e.group(1), e.group(1)), m.group(1)))
        pos = m.end()
        while pos < len(arg) and arg[pos].isspace():
            pos += 1
    return "".join(parts)


def shipping_lines(text):
    """For each line, whether it sits under a preprocessor condition naming UE_BUILD_SHIPPING."""
    stack = []
    flags = []
    for line in text.split("\n"):
        s = line.strip()
        if s.startswith("#if"):
            stack.append("UE_BUILD_SHIPPING" in s)
        elif s.startswith("#elif") and stack:
            stack[-1] = "UE_BUILD_SHIPPING" in s or stack[-1]
        elif s.startswith("#else") and stack:
            pass
        elif s.startswith("#endif") and stack:
            stack.pop()
        flags.append(any(stack))
    return flags


def helpers(text):
    """A file's own wrappers of RegisterConsole*: function name -> (index of the name parameter, index of the help one)."""
    found = {}
    for m in re.finditer(r"\b(\w+)\s*\(([^()]*)\)\s*\{", text):
        name, params = m.group(1), m.group(2)
        if name in ("if", "for", "while", "switch") or "TCHAR" not in params:
            continue
        body_start = m.end()
        depth = 1
        i = body_start
        while i < len(text) and depth:
            if text[i] == "{":
                depth += 1
            elif text[i] == "}":
                depth -= 1
            i += 1
        if not re.search(r"RegisterConsole(Command|Variable)", text[body_start:i]):
            continue
        names = [p.strip().split()[-1].lstrip("*&") if p.strip() else "" for p in params.split(",")]
        if "Name" in names and "Help" in names:
            found[name] = (names.index("Name"), names.index("Help"))
    return found


def scan_file(path, rel):
    with open(path, encoding="utf-8-sig", errors="replace") as f:
        raw = f.read()
    text = strip_comments(raw)
    ship = shipping_lines(text)
    wrap = helpers(text)
    entries = []

    def line_of(pos):
        return text.count("\n", 0, pos)

    def add(kind, name, help_text, default, how, pos):
        if not name:
            return
        entries.append({"kind": kind, "name": name, "help": help_text, "default": default, "file": rel,
                        "how": how, "dev": ship[line_of(pos)] if line_of(pos) < len(ship) else False})

    # Objects: FAutoConsoleCommandWithWorld GName(TEXT("os.X"), TEXT("help"), ...); TAutoConsoleVariable<float> CVar(...).
    for m in re.finditer(r"\b(FAutoConsole\w+|TAutoConsoleVariable)\s*(<[^;{}()]*?>)?\s+\w+\s*\(", text):
        kind_type = m.group(1)
        args = call_args(text, m.end() - 1)
        if COMMAND_TYPES.match(kind_type) and len(args) >= 2:
            add("command", literal(args[0]), literal(args[1]), None, kind_type, m.start())
        elif VARIABLE_TYPES.match(kind_type) and len(args) >= 3:
            add("variable", literal(args[0]), literal(args[2]), args[1], kind_type + (m.group(2) or ""), m.start())
    # IConsoleManager::Get().RegisterConsoleCommand(TEXT("os.X"), TEXT("help"), ...) with literal names.
    for m in re.finditer(r"\b(RegisterConsoleCommand|RegisterConsoleVariable\w*)\s*\(", text):
        call = m.group(1)
        args = call_args(text, m.end() - 1)
        name = literal(args[0]) if args else None
        if not name:
            continue  # a helper's own body: its callers are listed below
        if REGISTER_CALLS.get(call, "variable") == "command":
            add("command", name, literal(args[1]) if len(args) > 1 else None, None, call, m.start())
        else:
            add("variable", name, literal(args[2]) if len(args) > 2 else None, args[1] if len(args) > 1 else None, call, m.start())
    # The file's helpers.
    for helper, (name_at, help_at) in wrap.items():
        for m in re.finditer(r"\b" + re.escape(helper) + r"\s*\(", text):
            args = call_args(text, m.end() - 1)
            if len(args) <= max(name_at, help_at):
                continue
            name = literal(args[name_at])
            if name:
                add("command", name, literal(args[help_at]), None, helper, m.start())
    return entries


def scan():
    entries = []
    for base, dirs, files in os.walk(SOURCE):
        dirs.sort()
        for file in sorted(files):
            if file.endswith((".cpp", ".h", ".inl")):
                path = os.path.join(base, file)
                rel = os.path.relpath(path, ROOT).replace(os.sep, "/")
                entries.extend(scan_file(path, rel))
    unique = {}
    for e in entries:
        unique.setdefault((e["name"], e["kind"]), e)
    return sorted(unique.values(), key=lambda e: (e["name"].lower(), e["kind"]))


def cell(text):
    return (text or "").replace("|", "\\|").replace("\n", " ").strip()


def arguments(e):
    help_text = e["help"] or ""
    if e["kind"] == "variable":
        default = (e["default"] or "").strip()
        return "значение (умолч. `%s`)" % default if default else "значение"
    if help_text.startswith(e["name"]):
        rest = help_text[len(e["name"]):]
        head = rest.split(":", 1)[0].strip() if ":" in rest else rest.strip()
        if head and len(head) < 60:
            return "`%s`" % head
    m = re.match(r"\s*((?:[<\[][^>\]]*[>\]]\s*)+)", help_text)
    if m:
        return "`%s`" % m.group(1).strip()
    return "—"


def prefix(name):
    parts = name.split(".")
    return ".".join(parts[:2]) if len(parts) > 2 else parts[0]


def render(entries):
    lines = [
        "# Консольные команды",
        "",
        "Каталог собран скриптом `Scripts/os_console_catalog.py` из регистраций в",
        "`Source/` (`FAutoConsoleCommand*`, `TAutoConsoleVariable`,",
        "`IConsoleManager::Get().RegisterConsole*` и обёрток над ними). Руками не",
        "править: изменил команду или её строку помощи — перезапусти скрипт;",
        "`python Scripts/os_console_catalog.py --check` падает, если каталог отстал",
        "от кода ([Probes](Probes.md#каталог-команд)). Аргументы — начало строки",
        "помощи после имени (до двоеточия), если она так начинается. «не в Shipping»",
        "— регистрация под `#if !UE_BUILD_SHIPPING`.",
        "",
        "Что проверяет каждая проба и на какой карте её гонять — [Probes](Probes.md).",
        "",
        "Всего: %d команд, %d переменных." % (sum(e["kind"] == "command" for e in entries),
                                             sum(e["kind"] == "variable" for e in entries)),
    ]
    groups = {}
    for e in entries:
        groups.setdefault(prefix(e["name"]), []).append(e)
    for group in sorted(groups, key=str.lower):
        lines += ["", "## `%s`" % group, "", "| Команда | Аргументы | Что делает | Файл |", "|---|---|---|---|"]
        for e in groups[group]:
            name = "`%s`" % e["name"] + (" (переменная)" if e["kind"] == "variable" else "")
            help_text = cell(e["help"]) if e["help"] is not None else "(строка помощи не литерал)"
            if e["dev"]:
                help_text += " *не в Shipping*"
            lines.append("| %s | %s | %s | `%s` |" % (name, cell(arguments(e)), help_text, e["file"]))
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__.split("\n")[0])
    parser.add_argument("--check", action="store_true", help="exit 1 when Docs/Console.md differs from the code")
    options = parser.parse_args()
    text = render(scan())
    if options.check:
        try:
            with open(OUT, encoding="utf-8") as f:
                current = f.read()
        except FileNotFoundError:
            current = ""
        if current != text:
            print("Docs/Console.md is stale: run python Scripts/os_console_catalog.py", file=sys.stderr)
            return 1
        print("Docs/Console.md is up to date")
        return 0
    with open(OUT, "w", encoding="utf-8", newline="\n") as f:
        f.write(text)
    print("wrote %s" % os.path.relpath(OUT, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
