// os.Weapons.PortSmoke: the RMB-93's shroud smoke up close (Docs/RecoilWeapons-Rmb93.md, «Дым из перфорации кожуха»).

#if !UE_BUILD_SHIPPING
#include "EngineUtils.h"
#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "OSMuzzleFx.h"
#include "OSScope.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "MindKore.h"
#include "TimerManager.h"

namespace
{
	/**
	 * On Lvl_OS_TestRoom (run with -RenderOffscreen -UseFixedTimeStep -FPS=60): the RMB-93 in hand, the view a little
	 * down along the gun, the hall's flicker held. Three quick shots, stills 0.2 / 0.5 / 1 / 2 s after the last
	 * (HighResShot OSPorts_*); then two more and a 60-degree turn over a second, stills at a third and two thirds of
	 * it. The muzzle's cloud is cleared after each shot: the shroud's smoke is the subject. The pawn stands where the map put it. Ends "Port smoke: done".
	 */
	void RunPortSmoke(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn || !Pawn->Guns || !Pawn->Guns->Weapons.IsValidIndex(1))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Port smoke: needs the AOSZeroGPawn with its guns"));
			return;
		}
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.0001f), false);
		};
		// The hall's flicker held at its base: the same light in every still.
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (FProperty* Flicker = It->GetClass()->FindPropertyByName(TEXT("FlickerIntensity")))
			{
				*Flicker->ContainerPtrToValuePtr<float>(*It) = 0.0f;
				if (FProperty* Stutter = It->GetClass()->FindPropertyByName(TEXT("StutterChancePerSecond")))
				{
					*Stutter->ContainerPtrToValuePtr<float>(*It) = 0.0f;
				}
			}
		}
		const FRotator Look(-8.0f, Controller->GetControlRotation().Yaw, 0.0f);
		const auto Still = [Controller](const FString& Name)
		{
			UE_LOG(LogMindKore, Log, TEXT("Port smoke: %s"), *Name);
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSPorts_%s"), *Name));
		};
		// The shroud is the subject: each shot's muzzle cloud is cleared a frame after it (it would fill the frame).
		const auto Shoot = [Pawn, Controller, Look, After]()
		{
			Controller->SetControlRotation(Look);
			const bool bFired = Pawn->Fire();
			After(0.02f, [Pawn]() { if (UOSMuzzleFxComponent* Fx = Pawn->FindComponentByClass<UOSMuzzleFxComponent>()) { Fx->ClearClouds(); } });
			UE_LOG(LogMindKore, Log, TEXT("Port smoke: shot %s (%s)"), bFired ? TEXT("fired") : TEXT("NOT FIRED"), *Pawn->Guns->Describe());
		};
		After(2.0f, [Pawn, Controller, Look]()
		{
			UOSWeaponsComponent* Guns = Pawn->Guns;
			Guns->ApplyPickupWeapon(1, OSMakeRmb93(GetDefault<UOSWeaponsComponent>()->Weapons[1]));
			Guns->Scope->Aims[1] = OSMakeRmb93Aim(GetDefault<UOSScopeComponent>()->Aims[1]);
			Guns->Weapons[1].Reserve = 24;
			Guns->UnlockWeapon(1);
			Guns->FinishSwitch();
			Controller->SetControlRotation(Look);
		});
		After(3.0f, Shoot); // the shaders' warm-up
		// Three quick shots (the pump's cycle is ~0.45 s), stills after the last.
		for (const float At : { 9.0f, 9.6f, 10.2f })
		{
			After(At, Shoot);
		}
		for (const float T : { 0.2f, 0.5f, 1.0f, 2.0f })
		{
			After(10.2f + T, [Still, T]() { Still(FString::Printf(TEXT("%04dms"), FMath::RoundToInt(T * 1000.0f))); });
		}
		// Two more, then a turn of 60 degrees over a second (eased), stills during it.
		After(13.0f, Shoot);
		After(13.6f, Shoot);
		for (int32 Frame = 1; Frame <= 60; ++Frame)
		{
			After(14.0f + Frame / 60.0f, [Controller, Look, Frame]()
			{
				Controller->SetControlRotation(Look + FRotator(0.0f, 60.0f * FMath::SmoothStep(0.0f, 1.0f, Frame / 60.0f), 0.0f));
			});
		}
		After(14.0f + 0.335f, [Still]() { Still(TEXT("turn_0330ms")); });
		After(14.0f + 0.668f, [Still]() { Still(TEXT("turn_0660ms")); });
		After(15.2f, [Still]() { Still(TEXT("turn_1200ms")); });
		After(16.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Port smoke: done")); });
	}

	/**
	 * os.Weapons.PortSmokeSide [side -1|1] [distance cm]: the RMB-93 seen from outside, 3/4 from a side (style stills):
	 * the gun in view shown to every camera, a camera beside its shroud, one shot, HighResShot OSPortsSide_* 0.35 and
	 * 1 s after. Ends "Port smoke side: done". The view stays on that camera (a still stand, not for play).
	 */
	void RunPortSmokeSide(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn || !Pawn->Guns || !Pawn->Guns->ViewModel || !Pawn->Guns->Weapons.IsValidIndex(1))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Port smoke side: needs the AOSZeroGPawn with its guns"));
			return;
		}
		const float Side = Args.Num() > 0 ? FMath::Sign(FCString::Atof(*Args[0]) + 1e-3f) : -1.0f;
		const float Distance = Args.Num() > 1 ? FMath::Clamp(FCString::Atof(*Args[1]), 20.0f, 300.0f) : 110.0f;
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.0001f), false);
		};
		const FRotator Look(-8.0f, Controller->GetControlRotation().Yaw, 0.0f);
		After(1.0f, [Pawn, Controller, Look]()
		{
			UOSWeaponsComponent* Guns = Pawn->Guns;
			Guns->ApplyPickupWeapon(1, OSMakeRmb93(GetDefault<UOSWeaponsComponent>()->Weapons[1]));
			Guns->Scope->Aims[1] = OSMakeRmb93Aim(GetDefault<UOSScopeComponent>()->Aims[1]);
			Guns->Weapons[1].Reserve = 24;
			Guns->UnlockWeapon(1);
			Guns->FinishSwitch();
			Controller->SetControlRotation(Look);
		});
		After(3.0f, [Pawn, Controller, Side, Distance, After]()
		{
			// The gun in view to every camera; a camera at 3/4 beside its shroud (the holes at 38-47 cm along the action).
			TArray<USceneComponent*> Parts;
			Pawn->Guns->ViewModel->GetChildrenComponents(true, Parts);
			Parts.Add(Pawn->Guns->ViewModel);
			for (USceneComponent* Part : Parts)
			{
				if (UPrimitiveComponent* Primitive = Cast<UPrimitiveComponent>(Part))
				{
					Primitive->SetOnlyOwnerSee(false);
				}
			}
			const USceneComponent* Action = Pawn->Guns->ActionInView ? static_cast<USceneComponent*>(Pawn->Guns->ActionInView) : Pawn->Guns->ViewModel.Get();
			const FTransform Frame = Action->GetComponentTransform();
			const FVector Target = Frame.TransformPosition(FVector(42.0f, 0.0f, 0.0f));
			const FVector Fwd = Frame.GetUnitAxis(EAxis::X), Right = Frame.GetUnitAxis(EAxis::Y), Up = Frame.GetUnitAxis(EAxis::Z);
			const FVector Eye = Target + (Right * (0.8f * Side) - Fwd * 0.35f + Up * 0.3f).GetSafeNormal() * Distance;
			ACameraActor* Camera = Pawn->GetWorld()->SpawnActor<ACameraActor>(Eye, (Target - Eye).Rotation());
			Camera->GetCameraComponent()->SetFieldOfView(50.0f);
			Camera->GetCameraComponent()->bConstrainAspectRatio = false;
			Controller->SetViewTargetWithBlend(Camera, 0.0f);
			UE_LOG(LogMindKore, Log, TEXT("Port smoke side: camera %.0f cm to the %s of the shroud"), Distance, Side < 0.0f ? TEXT("left") : TEXT("right"));
			After(0.5f, [Pawn, Controller, After]()
			{
				const bool bFired = Pawn->Fire();
				UE_LOG(LogMindKore, Log, TEXT("Port smoke side: shot %s"), bFired ? TEXT("fired") : TEXT("NOT FIRED"));
				After(0.35f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 1 filename=OSPortsSide_0350ms")); });
				After(1.0f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 1 filename=OSPortsSide_1000ms")); });
				After(2.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Port smoke side: done")); });
			});
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GPortSmokeSide(
		TEXT("os.Weapons.PortSmokeSide"),
		TEXT("[side -1|1] [distance cm]: the RMB-93 from outside, 3/4 beside its shroud (style stills): one shot, HighResShot OSPortsSide_* 0.35 and 1 s after. The view stays on that camera."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunPortSmokeSide));

	FAutoConsoleCommandWithWorld GPortSmoke(
		TEXT("os.Weapons.PortSmoke"),
		TEXT("The RMB-93's shroud smoke up close on Lvl_OS_TestRoom (HighResShot OSPorts_*; needs a real RHI, -UseFixedTimeStep -FPS=60): three quick shots, stills 0.2-2 s after, two more and a turn."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunPortSmoke));
}
#endif
