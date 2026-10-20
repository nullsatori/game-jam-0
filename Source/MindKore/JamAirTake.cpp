// jam.Weapons.AirTake [B|sync]: the player's guns in air and in vacuum on Lvl_Jam_Ship (Docs/RecoilWeapons-Sound.md,
// «В воздухе»). First the switch itself: the pawn is carried from the cabin through the airlock and out, and at
// every step the guns' medium (UJamWeaponsComponent::IsInAir) must be the air of the zone it stands in
// (JamAirZoneAt) - PASS / FAIL per step. Then a take for listening: the same sequence in the cabin and outside -
// the pistol (a shot, a burst of three), the shotgun with its pump, the SV-98 with its bolt, the pistol's last
// round and empty reload (magazines, the slide stop), the switches between them (holster and sling), and the
// room alone (the pistol's crack by itself, sent to the room in air, dry in vacuum). The master submix goes to
// Saved/Clips/AirTake_<A|B>.wav, every event's time into the take to the log (Scripts/jam_air_reduce.py reads
// both), with the least gain the master brickwall applied over it. B: the flyers' room (SM_Jam_AirReverb)
// instead of the cabin's; sync: only single pistol shots, in the cabin and outside in turn, for the shot-to-mix
// delay of each path (AirTake_sync.wav). Needs sound: -nullrhi without -nosound, -ini:Engine:[Audio]:UnfocusedVolumeMultiplier=1.0.

#include "AudioMixerBlueprintLibrary.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "JamBrickwall.h"
#include "JamFlyer.h"
#include "JamGravity.h"
#include "JamShip.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MindKore.h"
#include "Misc/Paths.h"
#include "Sound/SoundSubmix.h"
#include "TimerManager.h"

namespace JamFlyerProbe // JamFlyer.cpp
{
	bool AirlockExit(UWorld* World, FVector& OutPoint, FVector& OutOut);
	void Teleport(AActor* Actor, const FVector& Location);
}

namespace JamAirTake
{
	struct FTake
	{
		FString Name;
		double Started = 0.0; // world seconds at the recording's start
		int32 Checks = 0;
		int32 Failed = 0;
		FVector Spot = FVector::ZeroVector;
		bool bDone = false;
	};

	void Check(FTake& Take, bool bPass, const FString& What)
	{
		++Take.Checks;
		Take.Failed += bPass ? 0 : 1;
		UE_LOG(LogMindKore, Log, TEXT("Air take [switch]: %s: %s"), *What, bPass ? TEXT("PASS") : TEXT("FAIL"));
	}

	bool ZoneAir(const UWorld* World, const FVector& Where, FString& OutZone)
	{
		const AJamGravityZone* Zone = JamAirZoneAt(World, Where);
#if WITH_EDITOR
		OutZone = Zone ? Zone->GetActorLabel() : TEXT("none");
#else
		OutZone = GetNameSafe(Zone);
#endif
		return Zone && Zone->bBreathable;
	}

	void RunAirTake(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		FVector Exit, Out;
		APlayerStart* Start = nullptr;
		for (TActorIterator<APlayerStart> It(World); It; ++It)
		{
			Start = (!Start || It->ActorHasTag(TEXT("Cabin"))) ? *It : Start;
		}
		if (!Pawn || !Start || !JamFlyerProbe::AirlockExit(World, Exit, Out))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Air take: needs Lvl_Jam_Ship (the zero-G pawn, the cabin's PlayerStart, the airlock's doors)"));
			return;
		}
		UJamWeaponsComponent* Guns = Pawn->Guns;
		const bool bB = Args.Contains(TEXT("B"));
		if (bB)
		{
			Guns->AirReverb = LoadObject<USoundSubmixBase>(nullptr, TEXT("/Game/JamArt/Audio/Mix/SM_Jam_AirReverb.SM_Jam_AirReverb"));
		}
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
		{
			Var->Set(1, ECVF_SetByCode);
		}
		for (TActorIterator<AJamFlyer> It(World); It; ++It)
		{
			It->Destroy(); // nothing else makes sound
		}
		for (int32 Gun = 0; Gun < Guns->Weapons.Num(); ++Gun)
		{
			Guns->UnlockWeapon(Gun);
		}
		TSharedRef<FTake> Take = MakeShared<FTake>();
		Take->Name = bB ? TEXT("B") : TEXT("A");
		const FVector Cabin = Start->GetActorLocation();
		const FVector Outside = Exit + Out * 300.0f;
		UE_LOG(LogMindKore, Log, TEXT("Air take %s: room %s (send %.2f); cabin %s, airlock exit %s, outside %s"), *Take->Name,
			*GetNameSafe(Guns->AirReverb), Guns->AirReverbSend, *Cabin.ToCompactString(), *Exit.ToCompactString(), *Outside.ToCompactString());
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};

		// 1. The switch: the cabin to 3 m outside, a step every 0.15 s (the game mode sets the air on its tick).
		TArray<FVector> Path;
		for (int32 Step = 0; Step <= 10; ++Step)
		{
			Path.Add(FMath::Lerp(Cabin, Exit, Step / 10.0f));
		}
		Path.Add(Exit + Out * 150.0f);
		Path.Add(Outside);
		struct FWay
		{
			int32 Switches = 0;
			bool bFirst = false;
			bool bLast = true;
			bool bPrev = false;
		};
		TSharedRef<FWay> Way = MakeShared<FWay>();
		float At = 0.2f;
		for (int32 Index = 0; Index < Path.Num(); ++Index)
		{
			const FVector Where = Path[Index];
			After(At, [Pawn, Where]() { JamFlyerProbe::Teleport(Pawn, Where); });
			After(At + 0.1f, [Pawn, Guns, Take, Way, Index, Count = Path.Num()]()
			{
				FString Zone;
				const FVector Here = Pawn->GetActorLocation();
				const bool bZone = ZoneAir(Pawn->GetWorld(), Here, Zone);
				const bool bGuns = Guns->IsInAir();
				Check(*Take, bZone == bGuns, FString::Printf(TEXT("step %d at %s, zone %s (%s), the guns hear %s"), Index, *Here.ToCompactString(), *Zone,
					bZone ? TEXT("air") : TEXT("vacuum"), bGuns ? TEXT("air") : TEXT("vacuum")));
				Way->Switches += Index > 0 && bGuns != Way->bPrev ? 1 : 0;
				Way->bFirst = Index == 0 ? bGuns : Way->bFirst;
				Way->bLast = bGuns;
				Way->bPrev = bGuns;
			});
			At += 0.15f;
		}
		After(At + 0.1f, [Take, Way]()
		{
			Check(*Take, Way->bFirst, TEXT("the cabin is air"));
			Check(*Take, !Way->bLast, TEXT("3 m outside the airlock is vacuum"));
			Check(*Take, Way->Switches == 1, FString::Printf(TEXT("the way out switches once (%d)"), Way->Switches));
		});

		// 2. The take: the same sequence in the cabin, then outside (sync: single pistol shots, cabin and outside in turn).
		const bool bSync = Args.Contains(TEXT("sync"));
		const float TakeStart = At + 0.5f;
		const float Segment = 21.0f;
		const int32 SyncShots = 16;
		const float Seconds = bSync ? SyncShots * 1.2f + 1.0f : 2.0f * Segment + 0.5f;
		if (bSync)
		{
			Take->Name = TEXT("sync");
		}
		After(TakeStart, [Pawn, Take, Seconds]()
		{
			UAudioMixerBlueprintLibrary::StartRecordingOutput(Pawn->GetWorld(), Seconds);
			Take->Started = Pawn->GetWorld()->GetTimeSeconds();
			JamBrickwallTakeLeastGain();
			UE_LOG(LogMindKore, Log, TEXT("Air take: recording started at platform %.4f s"), FPlatformTime::Seconds());
		});
		const auto Mark = [Pawn, Take](const TCHAR* Medium, const FString& What)
		{
			UE_LOG(LogMindKore, Log, TEXT("Air take: [%s] %s at +%.4f s (platform %.4f)"), Medium, *What, Pawn->GetWorld()->GetTimeSeconds() - Take->Started,
				FPlatformTime::Seconds());
		};
		const auto Hold = [Pawn, Take]()
		{
			JamFlyerProbe::Teleport(Pawn, Take->Spot);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		};
		// A shot where the pawn stands (moved there a frame or more before: the game mode sets the air on its tick).
		const auto Shoot = [Pawn, Guns, Mark, Hold, Take](const TCHAR* Name, bool bAir, const FString& What, bool bCount)
		{
			Hold();
			const int32 Air = Guns->GetSoundsInAir();
			const int32 Vacuum = Guns->GetSoundsInVacuum();
			const bool bFired = Pawn->Fire();
			Mark(Name, FString::Printf(TEXT("%s %s"), *What, bFired ? TEXT("fired") : TEXT("REFUSED")));
			if (bCount)
			{
				// The shot itself took the right path: in air the suit's shot and its crack, both in air; in vacuum one sound.
				const int32 InAir = Guns->GetSoundsInAir() - Air;
				const int32 InVacuum = Guns->GetSoundsInVacuum() - Vacuum;
				Check(*Take, bFired && (bAir ? InAir == 2 && InVacuum == 0 : InAir == 0 && InVacuum == 1),
					FString::Printf(TEXT("[%s] %s started %d sound(s) in air, %d in vacuum"), Name, *What, InAir, InVacuum));
			}
		};
		const auto Ready = [Guns]()
		{
			Guns->SelectWeapon(0);
			Guns->FinishSwitch();
			for (FJamWeapon& Gun : Guns->Weapons)
			{
				Gun.Ammo = Gun.MagazineSize;
				Gun.bSlideLocked = false;
			}
		};
		for (int32 Shot = 0; bSync && Shot < SyncShots; ++Shot)
		{
			const bool bAir = Shot % 2 == 0;
			const FVector Spot = bAir ? Cabin : Outside;
			const float T = TakeStart + 0.5f + Shot * 1.2f;
			After(T - 0.3f, [Take, Hold, Ready, Spot]() { Take->Spot = Spot; Hold(); Ready(); });
			After(T, [Shoot, bAir]() { Shoot(bAir ? TEXT("air") : TEXT("vacuum"), bAir, TEXT("sync shot"), true); });
		}
		for (int32 Medium = 0; !bSync && Medium < 2; ++Medium)
		{
			const TCHAR* Name = Medium == 0 ? TEXT("air") : TEXT("vacuum");
			const bool bAir = Medium == 0;
			const FVector Spot = bAir ? Cabin : Outside;
			const float T = TakeStart + 0.3f + Medium * Segment;
			const auto Select = [Guns, Mark, Name](int32 Gun)
			{
				Guns->SelectWeapon(Gun);
				Mark(Name, FString::Printf(TEXT("switch to %d"), Gun));
			};
			const auto Brickwall = [Name](const TCHAR* Over)
			{
				UE_LOG(LogMindKore, Log, TEXT("Air take: [%s] the brickwall over %s: least gain %.2f dB"), Name, Over, 20.0f * FMath::LogX(10.0f, JamBrickwallTakeLeastGain()));
			};
			After(T, [Take, Spot, Hold, Ready, Brickwall]()
			{
				Take->Spot = Spot;
				Hold();
				Ready();
				Brickwall(TEXT("the way here"));
			});
			After(T + 0.5f, [Shoot, Name, bAir]() { Shoot(Name, bAir, TEXT("pistol shot"), true); });
			for (int32 Round = 0; Round < 3; ++Round)
			{
				// FireInterval 0.15 s, and the timers jitter
				After(T + 2.0f + Round * 0.16f, [Shoot, Name, bAir]() { Shoot(Name, bAir, TEXT("pistol burst"), false); });
			}
			After(T + 3.9f, [Brickwall]() { Brickwall(TEXT("the pistol")); });
			After(T + 4.0f, [Select]() { Select(1); });
			After(T + 5.5f, [Shoot, Name, bAir]() { Shoot(Name, bAir, TEXT("shotgun shot"), true); });
			After(T + 7.9f, [Brickwall]() { Brickwall(TEXT("the shotgun")); });
			After(T + 8.0f, [Select]() { Select(2); });
			After(T + 9.5f, [Shoot, Name, bAir]() { Shoot(Name, bAir, TEXT("sv98 shot"), true); });
			After(T + 14.4f, [Brickwall]() { Brickwall(TEXT("the SV-98")); });
			After(T + 14.5f, [Select]() { Select(0); });
			After(T + 15.9f, [Guns]() { Guns->Weapons[0].Ammo = 1; });
			After(T + 16.0f, [Shoot, Name, bAir]() { Shoot(Name, bAir, TEXT("pistol last round"), false); });
			After(T + 17.0f, [Guns, Mark, Hold, Name]()
			{
				Hold();
				Guns->Reload();
				Mark(Name, TEXT("pistol empty reload"));
			});
			After(T + 18.9f, [Brickwall]() { Brickwall(TEXT("the reload")); });
			// The room alone: the pistol's crack by itself - into the room at the guns' send in air, dry in vacuum.
			After(T + 19.0f, [Pawn, Guns, Mark, Name, bAir]()
			{
				UAudioComponent* Audio = UGameplayStatics::CreateSound2D(Pawn, Guns->Weapons[0].ShotAirSound);
				if (Audio && bAir && Guns->AirReverb)
				{
					Audio->SetSubmixSend(Guns->AirReverb, Guns->AirReverbSend);
				}
				if (Audio)
				{
					Audio->Play();
				}
				Mark(Name, Audio ? TEXT("room alone") : TEXT("room alone NOT PLAYED"));
			});
		}
		After(TakeStart + Seconds + 0.2f, [Pawn, Take]()
		{
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips"));
			const FString File = FString::Printf(TEXT("AirTake_%s"), *Take->Name);
			UAudioMixerBlueprintLibrary::StopRecordingOutput(Pawn->GetWorld(), EAudioRecordingExportType::WavFile, File, Dir);
			UE_LOG(LogMindKore, Log, TEXT("Air take: %d checks, %d failed"), Take->Checks, Take->Failed);
			UE_LOG(LogMindKore, Log, TEXT("Air take: done - %s/%s.wav"), *Dir, *File);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GAirTake(
		TEXT("jam.Weapons.AirTake"),
		TEXT("jam.Weapons.AirTake [B|sync]: on Lvl_Jam_Ship, the guns' medium from the cabin through the airlock (PASS / FAIL), then the same sequence in the cabin and outside recorded (Saved/Clips/AirTake_<A|B>.wav). B: the flyers' room. sync: 16 pistol shots, cabin and outside in turn (AirTake_sync.wav)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunAirTake));
}
