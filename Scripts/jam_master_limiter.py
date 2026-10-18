"""The master limiter (editor Python, headless).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_master_limiter.py"
      -unattended -nop4 -nosplash -nullrhi

Makes (or updates) /Game/JamArt/Audio/Mix/SMX_Jam_MasterBrickwall, the lookahead brickwall
(UJamBrickwallPreset, Source/MindKore/JamBrickwall.h: ceiling -1 dBFS, look-ahead 5 ms, release
80 ms, no makeup gain), and /Game/JamArt/Audio/Mix/SM_Jam_Master, the submix carrying it as its only
effect. SMX_Jam_MasterLimiter, the engine's dynamics processor in limiter mode, is still made but
no longer in the chain: an envelope follower, it let sums of shots through to 0 dBFS (19.10).
Config/DefaultEngine.ini [/Script/Engine.AudioSettings] MasterSubmix points at the
submix, so every sound in the game passes the limiter last (owner 18.10: «ставь лимитер»).
"""
import unreal

PATH = "/Game/JamArt/Audio/Mix"
tools = unreal.AssetToolsHelpers.get_asset_tools()
eal = unreal.EditorAssetLibrary


def asset(name, cls, factory):
    full = f"{PATH}/{name}"
    if eal.does_asset_exist(full):
        return unreal.load_asset(full)
    return tools.create_asset(name, PATH, cls, factory)


factory = unreal.SoundSubmixEffectFactory()
factory.set_editor_property("sound_effect_submix_preset_class", unreal.SubmixEffectDynamicsProcessorPreset)
preset = asset("SMX_Jam_MasterLimiter", unreal.SubmixEffectDynamicsProcessorPreset, factory)
s = preset.get_editor_property("settings")
s.set_editor_property("dynamics_processor_type", unreal.SubmixEffectDynamicsProcessorType.LIMITER)
s.set_editor_property("peak_mode", unreal.SubmixEffectDynamicsPeakMode.PEAK)
s.set_editor_property("link_mode", unreal.SubmixEffectDynamicsChannelLinkMode.PEAK)  # one gain for both ears
s.set_editor_property("input_gain_db", 0.0)
s.set_editor_property("threshold_db", -1.0)
s.set_editor_property("knee_bandwidth_db", 0.0)
s.set_editor_property("look_ahead_msec", 5.0)
s.set_editor_property("attack_time_msec", 1.0)
s.set_editor_property("release_time_msec", 60.0)
s.set_editor_property("output_gain_db", 0.0)
s.set_editor_property("analog_mode", False)
s.set_editor_property("bypass", False)
preset.set_editor_property("settings", s)
eal.save_loaded_asset(preset)

factory = unreal.SoundSubmixEffectFactory()
factory.set_editor_property("sound_effect_submix_preset_class", unreal.JamBrickwallPreset)
wall = asset("SMX_Jam_MasterBrickwall", unreal.JamBrickwallPreset, factory)
w = wall.get_editor_property("settings")
w.set_editor_property("ceiling_db", -1.0)
w.set_editor_property("lookahead_msec", 5.0)
w.set_editor_property("release_msec", 80.0)
wall.set_editor_property("settings", w)
eal.save_loaded_asset(wall, False)

submix = asset("SM_Jam_Master", unreal.SoundSubmix, unreal.SoundSubmixFactory())
submix.set_editor_property("submix_effect_chain", [wall])
eal.save_loaded_asset(submix, False)  # the chain change does not mark it dirty

try:  # no makeup after it: the submix's own output volume (1.0 = 0 dB)
    out_volume = submix.get_editor_property("output_volume_modulation").get_editor_property("value")
except Exception as error:  # noqa: BLE001 - log what this engine version names differently
    out_volume = f"? ({error})"
w = wall.get_editor_property("settings")
unreal.log(f"JAMLIMITER: {submix.get_path_name()} chain "
           f"{[p.get_name() for p in submix.get_editor_property('submix_effect_chain')]}; brickwall ceiling "
           f"{w.get_editor_property('ceiling_db')} dBFS, look-ahead {w.get_editor_property('lookahead_msec')} ms, "
           f"release {w.get_editor_property('release_msec')} ms; submix output volume "
           f"{out_volume}")
unreal.SystemLibrary.quit_editor()
