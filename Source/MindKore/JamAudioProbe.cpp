// jam.Audio.LimiterProbe: proof that the master limiter (SM_Jam_Master, Docs/RecoilWeapons.md "Звук") runs.

#include "AudioDevice.h"
#include "AudioMixerBlueprintLibrary.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "MindKore.h"
#include "Misc/Paths.h"
#include "Sound/AudioSettings.h"
#include "Sound/SoundSubmix.h"
#include "Sound/SoundWave.h"
#include "SubmixEffects/AudioMixerSubmixEffectDynamicsProcessor.h"
#include "TimerManager.h"

namespace
{
	/**
	 * jam.Audio.LimiterProbe [Gain] [TestThresholdDb]: logs the live audio device's master submix and its
	 * effect chain, then records the master output for 4 s while the pistol's shot plays eight times at its
	 * fire rate and the SV-98's three times 0.1 s apart - sums far over full scale - each at Gain (default 1).
	 * The recording goes to Saved/BouncedWavFiles/JamLimiterProbe.wav: its peak shows what the limiter let
	 * through. It is 16-bit, so sums over full scale clip there with or without a limiter: TestThresholdDb
	 * (this run only, nothing is saved) moves the limiter's threshold down to where the recording can show it.
	 */
	void RunLimiterProbe(const TArray<FString>& Args, UWorld* World)
	{
		FAudioDevice* Device = World ? World->GetAudioDeviceRaw() : nullptr;
		if (!Device)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Limiter probe: no audio device"));
			return;
		}
		const float Gain = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 1.0f;
		USoundSubmix& Master = Device->GetMainSubmixObject();
		UE_LOG(LogMindKore, Log, TEXT("Limiter probe: device master submix %s (AudioSettings MasterSubmix '%s'), %d effect(s)"),
			*Master.GetPathName(), *GetDefault<UAudioSettings>()->MasterSubmix.ToString(), Master.SubmixEffectChain.Num());
		for (USoundEffectSubmixPreset* Preset : Master.SubmixEffectChain)
		{
			if (USubmixEffectDynamicsProcessorPreset* Dynamics = Cast<USubmixEffectDynamicsProcessorPreset>(Preset))
			{
				if (Args.Num() > 1)
				{
					FSubmixEffectDynamicsProcessorSettings Test = Dynamics->Settings;
					Test.ThresholdDb = FCString::Atof(*Args[1]);
					Dynamics->SetSettings(Test);
				}
				const FSubmixEffectDynamicsProcessorSettings& S = Dynamics->Settings;
				UE_LOG(LogMindKore, Log, TEXT("Limiter probe:   %s: %s, threshold %.1f dB, look-ahead %.1f ms, attack %.1f ms, release %.1f ms, bypass %d"),
					*Dynamics->GetName(), *UEnum::GetValueAsString(S.DynamicsProcessorType), S.ThresholdDb, S.LookAheadMsec,
					S.AttackTimeMsec, S.ReleaseTimeMsec, S.bBypass ? 1 : 0);
			}
			else
			{
				UE_LOG(LogMindKore, Log, TEXT("Limiter probe:   %s"), *GetNameSafe(Preset));
			}
		}
		USoundWave* Pistol = LoadObject<USoundWave>(nullptr, TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Shot_Pistol.SW_Jam_Shot_Pistol"));
		USoundWave* Sniper = LoadObject<USoundWave>(nullptr, TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Shot_SV98.SW_Jam_Shot_SV98"));
		UAudioMixerBlueprintLibrary::StartRecordingOutput(World, 4.0f);
		FTimerManager& Timers = World->GetTimerManager();
		TWeakObjectPtr<UWorld> WeakWorld(World);
		auto PlayAt = [&Timers, WeakWorld, Gain](USoundWave* Sound, float Seconds)
		{
			FTimerHandle Handle;
			Timers.SetTimer(Handle, FTimerDelegate::CreateLambda([WeakWorld, Sound, Gain]()
			{
				if (WeakWorld.IsValid())
				{
					UGameplayStatics::PlaySound2D(WeakWorld.Get(), Sound, Gain);
				}
			}), Seconds, false);
		};
		for (int32 Shot = 0; Shot < 8; ++Shot)
		{
			PlayAt(Pistol, 0.05f + Shot * 0.15f);
		}
		for (int32 Shot = 0; Shot < 3; ++Shot)
		{
			PlayAt(Sniper, 1.6f + Shot * 0.1f);
		}
		FTimerHandle Stop;
		Timers.SetTimer(Stop, FTimerDelegate::CreateLambda([WeakWorld]()
		{
			if (WeakWorld.IsValid())
			{
				const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("BouncedWavFiles"));
				UAudioMixerBlueprintLibrary::StopRecordingOutput(WeakWorld.Get(), EAudioRecordingExportType::WavFile, TEXT("JamLimiterProbe"), Dir);
				UE_LOG(LogMindKore, Log, TEXT("Limiter probe: recorded the master output to %s/JamLimiterProbe.wav"), *Dir);
			}
		}), 4.0f, false);
	}

	FAutoConsoleCommandWithWorldAndArgs GLimiterProbe(
		TEXT("jam.Audio.LimiterProbe"),
		TEXT("jam.Audio.LimiterProbe [Gain]: log the master submix's effect chain and record the master output under a pistol burst and three SV-98 shots."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLimiterProbe));
}
