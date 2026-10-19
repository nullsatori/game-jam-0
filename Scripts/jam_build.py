"""Shared helpers for the editor Python scripts.

save() replaces bare EditorAssetLibrary.save_asset / save_loaded_asset calls, whose
False result (read-only file, package the editor will not write) the scripts used
to ignore: on a fresh clone LFS makes lockable .uasset files read-only unless
lfs.setlockablereadonly is false (README, "Открыть и собрать").
"""
import unreal


def save(asset, only_if_dirty=False):
    """Save one asset, given by package path or as a loaded object; raise if the editor does not save it."""
    eal = unreal.EditorAssetLibrary
    if isinstance(asset, str):
        ok = eal.save_asset(asset, only_if_dirty)
    else:
        ok = eal.save_loaded_asset(asset, only_if_dirty)
    if not ok:
        name = asset if isinstance(asset, str) else asset.get_path_name()
        raise RuntimeError("save failed: " + name + " (read-only file? see README: lfs.setlockablereadonly)")
