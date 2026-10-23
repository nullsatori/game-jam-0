// os.Weapons.PortSmoke: the RMB-93's shroud smoke up close (Docs/RecoilWeapons-Rmb93.md, «Дым из перфорации кожуха»).

#if !UE_BUILD_SHIPPING
#include "EngineUtils.h"
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

	FAutoConsoleCommandWithWorld GPortSmoke(
		TEXT("os.Weapons.PortSmoke"),
		TEXT("The RMB-93's shroud smoke up close on Lvl_OS_TestRoom (HighResShot OSPorts_*; needs a real RHI, -UseFixedTimeStep -FPS=60): three quick shots, stills 0.2-2 s after, two more and a turn."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunPortSmoke));
}
#endif
