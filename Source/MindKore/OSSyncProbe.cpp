// os.Sync.Probe: is a shot's sound in time with what the player sees? (owner 19.10:
// «какой-то еще десинк между выстрелом и его звуком будто бы»). Records the viewport and
// the master submix (OSClipRecorder) while the player fires single shots with each gun,
// logs every game frame (its video frame, platform time, the gun's pose in view) to
// Saved/Clips/SyncFrames.csv. Scripts/os_sync_reduce.py reads them.

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "EngineUtils.h"
#include "OSClipRecorder.h"
#include "OSFlyer.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "MindKore.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Sound/SoundWave.h"

namespace OSSyncProbe
{
	struct FSync
	{
		TUniquePtr<FOSClipRecorder> Recorder;
		double Started = 0.0;      // game seconds
		TArray<FString> Rows;      // the per-frame table
		TArray<TPair<float, int32>> Plan; // (seconds into the take, action: gun index to select, or -1 fire)
		int32 Next = 0;
		float Seconds = 0.0f;
		bool bStopped = false;
	};

	TSharedPtr<FSync> GSync;

	AOSZeroGPawn* Pawn(UWorld* World)
	{
		const APlayerController* Controller = World ? World->GetFirstPlayerController() : nullptr;
		return Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
	}

	FString FindFfmpeg()
	{
		TArray<FString> Dirs;
		FPlatformMisc::GetEnvironmentVariable(TEXT("PATH")).ParseIntoArray(Dirs, TEXT(";"));
		for (const FString& Dir : Dirs)
		{
			const FString Candidate = FPaths::Combine(Dir, TEXT("ffmpeg.exe"));
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return TEXT("ffmpeg.exe");
	}

	bool SyncTick(UWorld* World, float DeltaSeconds)
	{
		TSharedPtr<FSync> Sync = GSync;
		if (!Sync.IsValid())
		{
			return false;
		}
		AOSZeroGPawn* Player = Pawn(World);
		const double Now = World->GetTimeSeconds() - Sync->Started;
		// The frame being built now: its row first (the pose after this tick's updates is the next row's).
		if (!Sync->bStopped && Player)
		{
			Player->ProbeThrust(FVector::ZeroVector, /*bStabilise=*/true);
			const UStaticMeshComponent* Gun = Player->Guns->ViewModel;
			const FTransform InView = Gun ? Gun->GetComponentTransform().GetRelativeTransform(Player->Camera->GetComponentTransform()) : FTransform::Identity;
			const FRotator Turn = InView.Rotator();
			Sync->Rows.Add(FString::Printf(TEXT("%d,%.6f,%.6f,%.3f,%.3f,%.3f,%.3f,%.3f,%.3f,%.4f"), Sync->Recorder->GetRequested(),
				FPlatformTime::Seconds(), Now, InView.GetLocation().X, InView.GetLocation().Y, InView.GetLocation().Z,
				Turn.Pitch, Turn.Yaw, Turn.Roll, Player->GetControlRotation().Pitch));
		}
		while (!Sync->bStopped && Sync->Next < Sync->Plan.Num() && Now >= Sync->Plan[Sync->Next].Key && Player)
		{
			const int32 Action = Sync->Plan[Sync->Next++].Value;
			if (Action >= 0)
			{
				Player->Guns->UnlockWeapon(Action);
				UE_LOG(LogMindKore, Log, TEXT("Sync: gun %d in hand"), Action);
				continue;
			}
			const double Before = FPlatformTime::Seconds();
			const bool bFired = Player->Fire(); // the shot sound (PlaySound2D) and the muzzle flash (UOSMuzzleFxComponent::Shot) both start in here
			UE_LOG(LogMindKore, Log, TEXT("Sync: fire %s at platform %.4f s (call took %.2f ms), game +%.4f s, video frame %d, gun %d"),
				bFired ? TEXT("ok") : TEXT("REFUSED"), Before, (FPlatformTime::Seconds() - Before) * 1000.0, Now, Sync->Recorder->GetRequested(),
				Player->Guns->CurrentWeapon);
		}
		Sync->Recorder->Tick();
		if (!Sync->bStopped && Now >= Sync->Seconds)
		{
			Sync->bStopped = true;
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips"));
			Sync->Recorder->Stop(World, TEXT("SyncProbe"), Dir);
			FFileHelper::SaveStringArrayToFile(Sync->Rows, *(Dir / TEXT("SyncFrames.csv")));
			UE_LOG(LogMindKore, Log, TEXT("Sync: stopped, %d rows in %s/SyncFrames.csv"), Sync->Rows.Num(), *Dir);
		}
		if (Sync->bStopped && Sync->Recorder->IsFinished())
		{
			UE_LOG(LogMindKore, Log, TEXT("Sync: done - %d frames over %.3f s real; video Saved/Clips/SyncProbe.mp4, sound Saved/Clips/SyncProbe.wav"),
				Sync->Recorder->GetFrames(), Sync->Recorder->GetSeconds());
			GSync.Reset();
			return false;
		}
		return true;
	}

	/** os.Sync.Probe [Shots]: per gun (pistol, shotgun, SV-98) Shots single shots 3.5 s apart, recorded. */
	void RunSyncProbe(const TArray<FString>& Args, UWorld* World)
	{
		AOSZeroGPawn* Player = Pawn(World);
		if (!Player || GSync.IsValid())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Sync: needs the zero-G pawn (and no probe running)"));
			return;
		}
		const int32 Shots = Args.Num() > 0 ? FMath::Max(1, FCString::Atoi(*Args[0])) : 3;
		if (IConsoleVariable* Var = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
		{
			Var->Set(1, ECVF_SetByCode);
		}
		// Nothing else makes sound: the flyers of a test map go, the view is set once (the guns kick it).
		for (TActorIterator<AOSFlyer> It(World); It; ++It)
		{
			It->Destroy();
		}
		Player->GetController()->SetControlRotation(FRotator(-5.0f, 90.0f, 0.0f));
		TSharedPtr<FSync> Sync = MakeShared<FSync>();
		float At = 1.0f;
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			Sync->Plan.Add({ At, Gun });
			At += 1.5f; // drawn
			for (int32 Shot = 0; Shot < Shots; ++Shot)
			{
				Sync->Plan.Add({ At, -1 });
				At += 3.5f; // the SV-98's tail is long: each onset over silence
			}
		}
		Sync->Seconds = At + 0.5f;
		Sync->Recorder = MakeUnique<FOSClipRecorder>();
		FString Error;
		const FString Video = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips/SyncProbe.mp4"));
		if (!Sync->Recorder->Start(World, Args.Num() > 1 ? Args[1] : FindFfmpeg(), Video, 1280, Error))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Sync: %s"), *Error);
			return;
		}
		Sync->Started = World->GetTimeSeconds();
		UE_LOG(LogMindKore, Log, TEXT("Sync: recording started at platform %.4f s; %d shots per gun, %.1f s"), FPlatformTime::Seconds(), Shots, Sync->Seconds);
		// How the shots' audio is loaded: on demand, the first play waits for its first chunk from disk.
		UE_LOG(LogMindKore, Log, TEXT("Sync: default loading behaviour (%s) %s"), USoundWave::GetDefaultLoadingBehaviorCVarName(),
			*UEnum::GetValueAsString(USoundWave::GetDefaultLoadingBehavior()));
		for (const FOSWeapon& Gun : Player->Guns->Weapons)
		{
			if (const USoundWave* Wave = Cast<USoundWave>(Gun.ShotSound))
			{
				UE_LOG(LogMindKore, Log, TEXT("Sync: %s loads %s"), *Wave->GetName(), *UEnum::GetValueAsString(Wave->GetLoadingBehavior()));
			}
		}
		Sync->Rows.Add(TEXT("video_frame,platform_s,game_s,gun_x,gun_y,gun_z,gun_pitch,gun_yaw,gun_roll,view_pitch"));
		GSync = Sync;
		TWeakObjectPtr<UWorld> WeakWorld = World;
		FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld](float DeltaSeconds)
		{
			return WeakWorld.IsValid() && SyncTick(WeakWorld.Get(), DeltaSeconds);
		}));
	}

	FAutoConsoleCommandWithWorldAndArgs GSyncProbe(
		TEXT("os.Sync.Probe"),
		TEXT("os.Sync.Probe [Shots] [Ffmpeg]: single shots per gun, recorded (video, master submix, per-frame gun pose, mixed onsets) for the shot/sound sync check."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunSyncProbe));
}
