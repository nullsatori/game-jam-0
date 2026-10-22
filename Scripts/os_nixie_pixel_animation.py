"""Marks M_OS_Nixie as "Has Pixel Animation" and saves it, without rebuilding the material graph.

Run once in the editor (Output Log, Python): exec(open(r"Scripts/os_nixie_pixel_animation.py").read())
ArtSource/Instruments/Nixie/build_ue_nixie.py sets the same flag, so a full rebuild keeps it.

Why: the digit's glow changes in place (ignition, afterglow, the warm-up stutter when the gun is drawn) and
the cathodes are drawn behind the face by parallax. Neither shows in the velocity buffer, so TSR reprojects
the old glow and the digit ghosts on a weapon switch or a turn (owner 22.10). The flag tells TSR not to
trust its history on these pixels.
"""
import unreal

PATH = "/Game/OSArt/Materials/Instruments/Nixie/M_OS_Nixie"
mat = unreal.load_asset(PATH)
assert mat, "missing " + PATH
mat.set_editor_property("has_pixel_animation", True)
unreal.MaterialEditingLibrary.recompile_material(mat)
assert unreal.EditorAssetLibrary.save_loaded_asset(mat, False)
unreal.log("Nixie: has_pixel_animation = %s, saved" % mat.get_editor_property("has_pixel_animation"))
