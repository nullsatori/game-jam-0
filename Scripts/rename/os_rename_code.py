"""Jam -> OS rename, code side. Run from the repo root on a clean checkout:

    python Scripts/rename/os_rename_code.py            # dry run: prints what would change
    python Scripts/rename/os_rename_code.py --apply    # git mv + rewrite + CoreRedirects

What it does:
- renames tracked non-asset files whose path has Jam/jam_ (Source/MindKore/Jam*, Scripts/jam_*,
  Docs/JamCharacter.md, Content/JamArt/README.md);
- rewrites text: AJamX/UJamX/FJamX/EJamX/JamX -> ...OSX, MKJamX -> OSX (not MKJamStarter),
  JAMX log prefixes -> OSX, jam.* console commands -> os.*, jam_* script names -> os_*;
- appends [CoreRedirects] for every reflected Jam class, struct and enum, so maps and
  blueprints saved with /Script/MindKore.JamX still load;
- moves Saved/JamDebug to Saved/OSDebug (local debug-menu presets).

Assets (.uasset/.umap) are NOT touched here: Scripts/rename/os_rename_assets.py renames them
in the editor. Order: os_rename_assets.py rename (old build) -> close editor -> this script
--apply -> build -> os_rename_assets.py resave. Docs/Archive is history and stays as written. Lowercase prose "jam" is listed,
not changed: review it by hand.
"""

import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
APPLY = "--apply" in sys.argv
SELF = Path(__file__).resolve()

SKIP_PREFIXES = ("Docs/Archive/", "Scripts/rename/")
ASSET_EXT = {".uasset", ".umap"}
TEXT_EXT = {".cpp", ".h", ".inl", ".cs", ".py", ".ps1", ".bat", ".sh", ".ini", ".md", ".json",
            ".yml", ".yaml", ".txt", ".uproject", ".gitattributes", ".gitignore"}

RULES = [
    # MKJamGrappleTool -> OSGrappleTool; MKJamStarter (the .uproject and targets) stays.
    (re.compile(r"(?<![A-Za-z0-9])MKJam(?!Starter)(?=[A-Z])"), "OS"),
    # AJamCharacter, FJamWeapon, JamWeapons.h, SM_Jam_RMB93, /Game/Jam/, Jam|Flyer, Jam.Suit, "Jam lift".
    # Not Jamb (door jamb meshes) or any Jam followed by a lowercase letter.
    (re.compile(r"(?<![A-Za-z0-9])([AUFEIT]?)Jam(?![a-z])"), r"\1OS"),
    # JAMFLYERS: log prefixes.
    (re.compile(r"(?<![A-Za-z0-9])JAM(?=[A-Z_:])"), "OS"),
    # jam.Rmb93.Probe console commands.
    (re.compile(r"(?<![A-Za-z0-9])jam\.(?=[A-Za-z*])"), "os."),
    # jam_fps_sweep.ps1, import jam_flesh.
    (re.compile(r"(?<![A-Za-z0-9])jam_"), "os_"),
    # Prose in comments and docs. Branch names (jam-aim4, jam-sync...) are history and stay.
    (re.compile(r"\b([Tt])he jam's\b"), r"\1he game's"),
    (re.compile(r"\b([Ii])n the jam\b"), r"\1n the game"),
    (re.compile(r"\bThe jam\b"), "The game"),
    (re.compile(r"\bjam (pawn|pawn's|maps|guns|camera|starter)\b"), r"OS \1"),
    (re.compile(r"\ba jam (pawn|camera)\b"), r"an OS \1"),
]
PROSE = re.compile(r"(?<![A-Za-z0-9])[Jj]am(?![a-z])")


def load_keep() -> list[str]:
    """Assets that keep their Jam names for now (another branch is still editing them):
    Scripts/rename/keep_jam.txt, one /Game/... package path or folder per line."""
    path = Path(__file__).with_name("keep_jam.txt")
    if not path.exists():
        return []
    keep = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.split("#", 1)[0].strip()
        if not line:
            continue
        keep.append(line)
        keep.append("Content/" + line[len("/Game/"):] if line.startswith("/Game/") else line)
        name = line.rstrip("/").rsplit("/", 1)[-1]
        if not line.endswith("/") and "Jam" in name:
            keep.append(name)
    return sorted(set(keep), key=len, reverse=True)


KEEP = load_keep()


def rewrite(text: str) -> str:
    held = {}
    for i, k in enumerate(KEEP):
        if k in text:
            token = f"\x00KEEP{i}\x00"
            held[token] = k
            text = text.replace(k, token)
    for rx, rep in RULES:
        text = rx.sub(rep, text)
    for token, k in held.items():
        text = text.replace(token, k)
    return text


def git(*args: str) -> str:
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True, text=True).stdout


def tracked() -> list[str]:
    return [p for p in git("ls-files").splitlines()
            if not p.startswith(SKIP_PREFIXES) and (ROOT / p).resolve() != SELF]


def reflected_types(files: list[str]) -> dict[str, list[str]]:
    """Jam classes/structs/enums that UHT sees, from the headers, before anything is renamed."""
    found = {"class": [], "struct": [], "enum": []}
    decl = re.compile(r"^\s*(UCLASS|USTRUCT|UENUM)\s*\([^\n]*\n(?:\s*//[^\n]*\n)*\s*"
                      r"(class|struct|enum class|enum)\s+(?:\w+_API\s+)?(\w+)", re.M)
    for p in files:
        if not p.startswith("Source/") or not p.endswith(".h"):
            continue
        for macro, _, name in decl.findall((ROOT / p).read_text(encoding="utf-8", errors="replace")):
            if not re.match(r"[AUFE]?Jam[A-Z]", name):
                continue
            kind = {"UCLASS": "class", "USTRUCT": "struct", "UENUM": "enum"}[macro]
            found[kind].append(name)
    return found


def redirects_block(types: dict[str, list[str]]) -> str:
    lines = ["", "[CoreRedirects]",
             "; Jam -> OS rename (22.10.2026): maps and blueprints saved before it name the old types."]
    for name in sorted(types["class"]):
        old, new = name[1:], rewrite(name)[1:]  # class redirects drop the A/U prefix
        lines.append(f'+ClassRedirects=(OldName="/Script/MindKore.{old}",NewName="/Script/MindKore.{new}")')
    for name in sorted(types["struct"]):
        old, new = name[1:], rewrite(name)[1:]  # and the F prefix
        lines.append(f'+StructRedirects=(OldName="/Script/MindKore.{old}",NewName="/Script/MindKore.{new}")')
    for name in sorted(types["enum"]):
        lines.append(f'+EnumRedirects=(OldName="/Script/MindKore.{name}",NewName="/Script/MindKore.{rewrite(name)}")')
    return "\n".join(lines) + "\n"


def main() -> None:
    files = tracked()
    types = reflected_types(files)
    print(f"reflected: {len(types['class'])} classes, {len(types['struct'])} structs, {len(types['enum'])} enums")

    text_files = [p for p in files if Path(p).suffix.lower() not in ASSET_EXT
                  and (Path(p).suffix.lower() in TEXT_EXT or Path(p).name in TEXT_EXT)]
    changed, prose = 0, []
    for p in text_files:
        path = ROOT / p
        old = path.read_text(encoding="utf-8", errors="surrogateescape")
        new = rewrite(old)
        if new != old:
            changed += 1
            if APPLY:
                path.write_text(new, encoding="utf-8", errors="surrogateescape")
        for n, line in enumerate(new.splitlines(), 1):
            if PROSE.search(line):
                prose.append(f"{p}:{n}: {line.strip()[:140]}")
    print(f"text files rewritten: {changed}")

    moves = []
    for p in files:
        if Path(p).suffix.lower() in ASSET_EXT:
            continue
        q = rewrite(p)
        if q != p:
            moves.append((p, q))
    print(f"files renamed: {len(moves)}")
    for p, q in moves:
        print(f"  {p} -> {q}")
        if APPLY:
            (ROOT / q).parent.mkdir(parents=True, exist_ok=True)
            git("mv", p, q)

    if APPLY:
        ini = ROOT / "Config/DefaultEngine.ini"
        body = ini.read_text(encoding="utf-8")
        if "Jam -> OS rename" not in body:
            ini.write_text(body.rstrip("\n") + "\n" + redirects_block(types), encoding="utf-8")
        saved_old, saved_new = ROOT / "Saved/JamDebug", ROOT / "Saved/OSDebug"
        if saved_old.is_dir() and not saved_new.exists():
            saved_old.rename(saved_new)
            print("moved Saved/JamDebug -> Saved/OSDebug")
    else:
        print(redirects_block(types))

    print(f"\nleft for review ({len(prose)} lines with a bare jam/Jam):")
    for line in prose:
        print("  " + line)
    if not APPLY:
        print("\ndry run: nothing written. Re-run with --apply.")


if __name__ == "__main__":
    main()
