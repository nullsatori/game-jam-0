"""The master limiter (editor Python, headless).

  UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject -ExecutePythonScript="<path>/Scripts/jam_master_limiter.py"
      -unattended -nop4 -nosplash -nullrhi

Makes (or updates) /Game/JamArt/Audio/Mix/SMX_Jam_MasterLimiter, a dynamics processor in
limiter mode, and /Game/JamArt/Audio/Mix/SM_Jam_Master, the submix carrying it.
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

submix = asset("SM_Jam_Master", unreal.SoundSubmix, unreal.SoundSubmixFactory())
submix.set_editor_property("submix_effect_chain", [preset])
eal.save_loaded_asset(submix)

s = preset.get_editor_property("settings")
unreal.log(f"JAMLIMITER: {submix.get_path_name()} chain "
           f"{[p.get_name() for p in submix.get_editor_property('submix_effect_chain')]}; "
           f"{s.get_editor_property('dynamics_processor_type')} threshold {s.get_editor_property('threshold_db')} dB, "
           f"look-ahead {s.get_editor_property('look_ahead_msec')} ms, attack {s.get_editor_property('attack_time_msec')} ms, "
           f"release {s.get_editor_property('release_time_msec')} ms")
unreal.SystemLibrary.quit_editor()
