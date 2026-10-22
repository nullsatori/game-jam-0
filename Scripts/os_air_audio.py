"""The guns' air assets (editor Python, headless). Docs/RecoilWeapons-Sound.md, «В воздухе».

  OS_WAV_DIR=<out dir of Scripts/os_air_sounds.py> UnrealEditor-Cmd.exe <path>/MKJamStarter.uproject
      -ExecutePythonScript="<path>/Scripts/os_air_audio.py" -unattended -nop4 -nosplash -nullrhi

The cracks (SW_OS_ShotAir_*.wav, same dir) are imported by Scripts/os_import_audio.py. Here, in
/Game/OSArt/Audio/Mix:
- IR_OS_Cabin: the cabin's impulse response from IR_OS_Cabin.wav (stored at half scale: x2 back to unit
  energy per channel), filled through UOSAudioTools.set_impulse_response (Python cannot write its samples);
- SMX_OS_CabinReverb: a convolution reverb on it, the wet only (the send carries it), block 256 frames;
- SM_OS_CabinReverb: the submix carrying it, into the master and its limiter (UOSWeaponsComponent::AirReverb);
- SFX_OS_GunAirLift (EQ: +3 dB at 3.5 kHz, 1.5 octaves wide) in SEC_OS_GunAirLift, the chain every gun sound
  but the shot plays through in air (UOSWeaponsComponent::AirLift). Light: +5 dB at 3 kHz over two octaves
  lifted the slide's slam (made to -1 dBFS) 2.3 dB and the master's brickwall took 2.2 dB in the empty reload;
  this one adds 0.7 dB to its peak and 1.6x its energy over 2 kHz.
"""
import array
import os
import sys
import wave

import unreal

sys.path.insert(0, os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "Scripts"))
import os_build  # noqa: E402

MIX = "/Game/OSArt/Audio/Mix"
eal = unreal.EditorAssetLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()


def log(msg):
    unreal.log("OSAIRAUDIO: " + msg)


def asset(name, cls, factory):
    full = f"{MIX}/{name}"
    if eal.does_asset_exist(full):
        return unreal.load_asset(full)
    made = tools.create_asset(name, MIX, cls, factory)
    if not made:
        raise RuntimeError(f"could not create {full} ({cls.__name__})")
    return made


def set_all(obj, pairs):
    for key, value in pairs:
        obj.set_editor_property(key, value)


# --- the cabin's room ---
with wave.open(os.path.join(os.environ["OS_WAV_DIR"], "IR_OS_Cabin.wav")) as w:
    channels, rate = w.getnchannels(), w.getframerate()
    pcm = array.array("h", w.readframes(w.getnframes()))
samples = [2.0 * v / 32768.0 for v in pcm]
ir = asset("IR_OS_Cabin", unreal.AudioImpulseResponse, unreal.AudioImpulseResponseFactory())
unreal.OSAudioTools.set_impulse_response(ir, samples, channels, rate)
ir.set_editor_property("normalization_volume_db", 0.0)
os_build.save(ir)
log(f"{ir.get_name()}: {len(samples) // channels / rate:.3f} s, {channels} ch, {rate} Hz")

factory = unreal.SoundSubmixEffectFactory()
factory.set_editor_property("sound_effect_submix_preset_class", unreal.SubmixEffectConvolutionReverbPreset)
conv = asset("SMX_OS_CabinReverb", unreal.SubmixEffectConvolutionReverbPreset, factory)
settings = conv.get_editor_property("settings")
set_all(settings, (("wet_volume_db", 0.0), ("dry_volume_db", -96.0), ("bypass", False),
                   ("mix_input_channel_format_to_impulse_response_format", True),
                   ("mix_reverb_output_to_output_channel_format", True)))
conv.set_editor_property("settings", settings)
try:  # the tail's start waits a block: 256 frames is 5 ms, the walls of a small room answer in 2-10 ms
    conv.set_editor_property("block_size", unreal.SubmixEffectConvolutionReverbBlockSize.BLOCK_SIZE256)
except Exception as error:  # noqa: BLE001 - log what this engine version names differently
    log(f"block size not set: {error}")
conv.set_editor_property("impulse_response", ir)
os_build.save(conv)
room = asset("SM_OS_CabinReverb", unreal.SoundSubmix, unreal.SoundSubmixFactory())
room.set_editor_property("submix_effect_chain", [conv])
os_build.save(room)
log(f"{room.get_name()} <- {conv.get_name()} ({conv.get_editor_property('impulse_response').get_name()}, "
    f"block {conv.get_editor_property('block_size')})")

# --- the mechanisms' lift ---
factory = unreal.SoundSourceEffectFactory()
factory.set_editor_property("sound_effect_sourcepreset_class", unreal.SourceEffectEQPreset)
eq = asset("SFX_OS_GunAirLift", unreal.SourceEffectEQPreset, factory)
band = unreal.SourceEffectEQBand()
set_all(band, (("frequency", 3500.0), ("bandwidth", 1.5), ("gain_db", 3.0), ("enabled", True)))
eq_settings = eq.get_editor_property("settings")
eq_settings.set_editor_property("eq_bands", [band])
eq.set_editor_property("settings", eq_settings)
os_build.save(eq)
chain = asset("SEC_OS_GunAirLift", unreal.SoundEffectSourcePresetChain, unreal.SoundSourceEffectChainFactory())
entry = unreal.SourceEffectChainEntry()
set_all(entry, (("preset", eq), ("bypass", False)))
chain.set_editor_property("chain", [entry])
os_build.save(chain)
log(f"{chain.get_name()} <- {eq.get_name()}: " + ", ".join(
    f"{b.get_editor_property('frequency'):.0f} Hz {b.get_editor_property('gain_db'):+.1f} dB / {b.get_editor_property('bandwidth'):.1f} oct"
    for b in eq.get_editor_property("settings").get_editor_property("eq_bands")))
unreal.SystemLibrary.quit_editor()
