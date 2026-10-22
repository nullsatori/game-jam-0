"""Jam -> OS rename, asset side (editor Python). Two passes, around the code rename:

  1. With the OLD build (before os_rename_code.py), in the editor's Output Log:
         py "<repo>/Scripts/rename/os_rename_assets.py" rename
     - opens a blank map (no renamed map stays loaded);
     - /Game/Jam -> /Game/OS, /Game/JamArt -> /Game/OSArt, _Jam_/Jam in asset names -> _OS_/OS
       (Jamb door meshes in /Game/USD are left alone), through AssetTools so references follow;
     - fixes up every redirector under /Game and deletes them;
     - renames actor and component tags Jam.* -> OS.* in the renamed maps;
     - saves. The emptied Jam folders stay on disk (git drops them); Content/JamArt/README.md is
       moved by os_rename_code.py.
     Assets are moved first, while the C++ still finds them by their old paths, so no
     constructor default comes up empty while maps and blueprints are being saved.

  2. Close the editor, run os_rename_code.py --apply, build, open the editor, then:
         py "<repo>/Scripts/rename/os_rename_assets.py" resave
     - force-resaves every package under /Game whose bytes still name Jam (old class/struct names
       held through [CoreRedirects], old tags), then scans again and prints what is left.

  `py ... scan` only scans and prints. A trailing `quit` closes the editor afterwards, for
  UnrealEditor.exe <uproject> -ExecutePythonScript="<script> rename quit".
  keep_jam.txt next to this file lists packages/folders that keep Jam for now.
"""

import os
import re
import sys

import unreal

MODE = sys.argv[1] if len(sys.argv) > 1 else "scan"
NAME_RX = re.compile(r"(?<![A-Za-z0-9])([AUFEIT]?)Jam(?![a-z])")
BYTES_RX = re.compile(rb"(?<![A-Za-z0-9])[AUFE]?Jam(?![a-z])|(?<![A-Za-z0-9])jam\.")
ROOTS = ("/Game/Jam", "/Game/JamArt")

registry = unreal.AssetRegistryHelpers.get_asset_registry()
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def log(msg):
    unreal.log("OSRENAME: " + msg)


def load_keep():
    """Packages/folders that keep Jam for now (another branch edits them): keep_jam.txt next to this file."""
    path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "keep_jam.txt")
    if not os.path.exists(path):
        return []
    with open(path, encoding="utf-8") as fh:
        return [l.split("#", 1)[0].strip() for l in fh if l.split("#", 1)[0].strip()]


KEEP = load_keep()


def kept(pkg):
    return any(pkg == k or pkg.startswith(k.rstrip("/") + "/") for k in KEEP)


def renamed(s):
    return NAME_RX.sub(r"\1OS", s)


def all_assets(path):
    return registry.get_assets_by_path(path, recursive=True)


def rename_pass():
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)

    datas, plan = [], []
    seen = set()
    candidates = []
    for root in ROOTS:
        candidates += all_assets(root)
    candidates += [a for a in all_assets("/Game") if NAME_RX.search(str(a.asset_name))]
    for a in candidates:
        pkg = str(a.package_name)
        if pkg in seen:
            continue
        seen.add(pkg)
        if kept(pkg):
            log(f"kept (keep_jam.txt): {pkg}")
            continue
        new_pkg = renamed(pkg)
        if new_pkg == pkg:
            continue
        obj = a.get_asset()
        if obj is None:
            log(f"cannot load {pkg}, skipped")
            continue
        new_path, new_name = new_pkg.rsplit("/", 1)
        datas.append(unreal.AssetRenameData(obj, new_path, new_name))
        plan.append((pkg, new_pkg))
    log(f"renaming {len(datas)} assets")
    for old, new in plan:
        log(f"  {old} -> {new}")
    if not tools.rename_assets(datas):
        log("rename_assets reported a failure: check the log above before going on")

    fix_redirectors()
    rename_tags([new for old, new in plan if new.split("/")[-1].startswith("Lvl_")])

    eal.save_directory("/Game", only_if_is_dirty=True, recursive=True)
    log("rename pass done")


def fix_redirectors():
    flt = unreal.ARFilter(package_paths=["/Game"], recursive_paths=True,
                          class_paths=[unreal.TopLevelAssetPath("/Script/CoreUObject", "ObjectRedirector")])
    redirs = [a.get_asset() for a in registry.get_assets(flt)]
    redirs = [r for r in redirs if r]
    log(f"fixing up {len(redirs)} redirectors")
    if redirs:
        tools.fixup_referencers(redirs)


def rename_tags(maps):
    les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    eas = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    for m in maps:
        if not les.load_level(m):
            log(f"cannot open {m} for tags")
            continue
        changed = 0
        for actor in eas.get_all_level_actors():
            tags = list(actor.tags)
            new = [unreal.Name(renamed(str(t))) for t in tags]
            if [str(t) for t in new] != [str(t) for t in tags]:
                actor.modify()
                actor.tags = new
                changed += 1
            for comp in actor.get_components_by_class(unreal.ActorComponent):
                ctags = list(comp.component_tags)
                cnew = [unreal.Name(renamed(str(t))) for t in ctags]
                if [str(t) for t in cnew] != [str(t) for t in ctags]:
                    comp.modify()
                    comp.component_tags = cnew
                    changed += 1
        log(f"{m}: {changed} actors/components retagged")
        if changed:
            les.save_current_level()
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)


def scan():
    content = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_content_dir())
    hits = {}
    for dirpath, _, files in os.walk(content):
        for f in files:
            if not f.endswith((".uasset", ".umap")):
                continue
            full = os.path.join(dirpath, f)
            with open(full, "rb") as fh:
                data = fh.read()
            found = sorted({m.group(0).decode("latin-1") for m in BYTES_RX.finditer(data)})
            if found:
                rel = os.path.relpath(full, content).replace("\\", "/")
                hits["/Game/" + rel.rsplit(".", 1)[0]] = found
    log(f"{len(hits)} packages still name Jam")
    for pkg, found in sorted(hits.items()):
        log(f"  {pkg}: {', '.join(found[:8])}")
    return hits


def resave_pass():
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    hits = scan()
    for pkg in sorted(hits):
        if pkg.split("/")[-1].startswith("Lvl_"):
            les = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
            if les.load_level(pkg):
                unreal.EditorLoadingAndSavingUtils.save_current_level()
        elif eal.load_asset(pkg):
            eal.save_asset(pkg, only_if_is_dirty=False)
    unreal.EditorLoadingAndSavingUtils.new_blank_map(False)
    log("after resave:")
    scan()


if MODE == "rename":
    rename_pass()
elif MODE == "resave":
    resave_pass()
else:
    scan()

if "quit" in sys.argv[2:]:
    # For -ExecutePythonScript runs: close the editor once the pass is done.
    unreal.SystemLibrary.quit_editor()
