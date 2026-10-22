#include "OSWeapons.h"

#if !UE_BUILD_SHIPPING
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "OSNixieCounter.h"
#include "OSPickups.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MindKore.h"
#include "TimerManager.h"

namespace
{
	/** Last line of every run, so Scripts/os_fps_sweep.ps1 stops the engine instead of waiting for its deadline. */
	void NixieProbeDone()
	{
		UE_LOG(LogMindKore, Log, TEXT("Nixie weapon probe: done"));
	}

	struct FNixieWeaponProbe
	{
		TWeakObjectPtr<UOSWeaponsComponent> Guns;
		TArray<FOSWeapon> SavedWeapons;
		TArray<FOSGunAim> SavedAims;
		int32 SavedCurrent = 0;
		int32 Passed = 0;
		int32 Failed = 0;
		void Check(bool Pass, const TCHAR* Label)
		{
			Pass ? ++Passed : ++Failed;
			UE_LOG(LogMindKore, Log, TEXT("Nixie weapon probe: %s - %s"), Pass ? TEXT("PASS") : TEXT("FAIL"), Label);
		}
		bool Shows(int32 Digit) const
		{
			const UOSWeaponsComponent* G = Guns.Get();
			if (!G || !G->AmmoCounter || G->AmmoCounter->GetBoundMaterialCount() != 1 || G->AmmoCounter->GetDigit() != Digit) return false;
			UMaterialInstanceDynamic* MID = Cast<UMaterialInstanceDynamic>(G->ViewModel->GetMaterial(1));
			float Value = -1;
			return MID && MID->GetScalarParameterValue(FMaterialParameterInfo(TEXT("SP_DigitIndex")), Value) && Value == float(Digit);
		}
	};

	void RunNixieWeaponProbe(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		UOSWeaponsComponent* Guns = Pawn ? Pawn->FindComponentByClass<UOSWeaponsComponent>() : nullptr;
		UStaticMesh* Body = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/OSArt/Weapons/RMB93/Meshes/SM_OS_RMB93_ViewBody.SM_OS_RMB93_ViewBody"));
		UStaticMesh* Action = LoadObject<UStaticMesh>(nullptr, TEXT("/Game/OSArt/Weapons/RMB93/Meshes/SM_OS_RMB93_Action.SM_OS_RMB93_Action"));
		if (!World || !World->IsGameWorld() || !Guns || !Guns->ViewModel || !Guns->Scope || !Guns->Weapons.IsValidIndex(1) || !Body || !Action)
		{
			UE_LOG(LogMindKore, Error, TEXT("Nixie weapon probe: FAIL - needs game/PIE pawn with guns and production RMB93 ViewBody/Action assets."));
			NixieProbeDone();
			return;
		}
		const auto State = MakeShared<FNixieWeaponProbe>();
		State->Guns = Guns;
		State->SavedWeapons = Guns->Weapons;
		State->SavedAims = Guns->Scope->Aims;
		State->SavedCurrent = Guns->CurrentWeapon;
		const UStaticMesh* DefaultShotgun = GetDefault<UOSWeaponsComponent>()->Weapons[1].ViewMesh;
		// Deferred: Gun and WeaponIndex must be set before BeginPlay builds the replacement.
		const auto Pickup = [World, Pawn](int32 Index, EOSPickupGun Gun)
		{
			AOSWeaponPickup* Item = World->SpawnActorDeferred<AOSWeaponPickup>(AOSWeaponPickup::StaticClass(), FTransform::Identity);
			Item->SetFlags(RF_Transient);
			Item->WeaponIndex = Index;
			Item->Gun = Gun;
			Item->FinishSpawning(FTransform::Identity);
			Item->SetActorEnableCollision(false);
			Item->SetActorHiddenInGame(true);
			Item->CompleteInteraction(Pawn);
			const bool Taken = Item->bTaken;
			Item->Destroy();
			return Taken;
		};
		State->Check(!Pickup(99, EOSPickupGun::Slot), TEXT("invalid pickup index is not consumed"));
		State->Check(Pickup(0, EOSPickupGun::Slot) && Guns->Weapons[0].ViewMesh == State->SavedWeapons[0].ViewMesh,
			TEXT("ordinary pickup keeps existing weapon definition"));
		const FOSWeapon Definition = OSMakeRmb93(GetDefault<UOSWeaponsComponent>()->Weapons[1]);
		const FOSGunAim Aim = OSMakeRmb93Aim(GetDefault<UOSScopeComponent>()->Aims[1]);
		State->Check(Definition.ViewMesh == Body && Definition.ActionMesh == Action && Definition.NixieMaterialSlot == 1,
			TEXT("OSMakeRmb93 builds on the production RMB93 meshes"));
		State->Check(Pickup(1, EOSPickupGun::Rmb93), TEXT("RMB-93 pickup is consumed successfully"));
		Guns->FinishSwitch();
		State->Check(Guns->ViewModel->GetStaticMesh() == Body && Guns->ActionInView->GetStaticMesh() == Action,
			TEXT("pickup installs production fixed body and separate forward pump in hands"));
		State->Check(Guns->Scope->Aims[1].SightRearCm.Equals(Aim.SightRearCm) && Guns->Scope->Aims[1].SightFrontCm.Equals(Aim.SightFrontCm),
			TEXT("pickup sight override applied before draw"));
		State->Check(Guns->Weapons[1].MagazineSize == 6 && Guns->Weapons[1].Ammo == 6 && State->Shows(6), TEXT("actual six-round magazine drives digit6"));
		FVector Recoil;
		// Shoot above the test room so this logic probe does not damage its targets.
		const bool Fired = Guns->Fire(Pawn->GetActorLocation() + FVector(0, 0, 10000), FVector::UpVector, Recoil);
		State->Check(Fired && Guns->Weapons[1].Ammo == 5 && State->Shows(6), TEXT("real Fire updates actual ammo; the MID waits for the pump (CounterSeconds)"));
		FTimerHandle CycleTimer;
		World->GetTimerManager().SetTimer(CycleTimer, FTimerDelegate::CreateLambda([State, World, Pickup, Definition, DefaultShotgun]()
		{
			UOSWeaponsComponent* G = State->Guns.Get();
			if (!G)
			{
				UE_LOG(LogMindKore, Error, TEXT("Nixie weapon probe: FAIL - the weapons component is gone before reload."));
				NixieProbeDone();
				return;
			}
			State->Check(State->Shows(5), TEXT("the pump's home counts the round off the MID"));
			G->Reload();
			State->Check(G->GetReloadRemaining() > 0 && G->Weapons[1].Ammo == 5 && State->Shows(5), TEXT("reload does not count a round before its seat event"));
			FTimerHandle ReloadTimer;
			World->GetTimerManager().SetTimer(ReloadTimer, FTimerDelegate::CreateLambda([State, Pickup, Definition, DefaultShotgun]()
			{
				UOSWeaponsComponent* Current = State->Guns.Get();
				if (!Current)
				{
					UE_LOG(LogMindKore, Error, TEXT("Nixie weapon probe: FAIL - the weapons component is gone after reload."));
					NixieProbeDone();
					return;
				}
				State->Check(Current->Weapons[1].Ammo == 6 && State->Shows(6), TEXT("real shell-seat event restores ammo and MID to6"));
				Current->SelectWeapon(0);
				Current->FinishSwitch();
				State->Check(Current->AmmoCounter->GetBoundMaterialCount() == 0
					&& Current->ViewModel->GetStaticMesh() == State->SavedWeapons[0].ViewMesh
					&& !Cast<UMaterialInstanceDynamic>(Current->ViewModel->GetMaterial(1)), TEXT("switch to pistol releases counter without stale MID"));
				Current->SelectWeapon(1);
				Current->FinishSwitch();
				State->Check(State->Shows(6), TEXT("switch back binds saved actual ammo"));
				Current->Holster(true);
				State->Check(!Current->ViewModel->GetStaticMesh() && Current->AmmoCounter->GetBoundMaterialCount() == 0, TEXT("holster clears display binding"));
				Current->Rearm();
				Current->FinishSwitch();
				State->Check(State->Shows(6), TEXT("rearm binds counter again"));
				State->Check(Pickup(1, EOSPickupGun::Rmb93) && Current->GetShownWeapon() == INDEX_NONE,
					TEXT("same-slot replacement forces redraw instead of leaving old mesh"));
				Current->FinishSwitch();
				State->Check(State->Shows(6), TEXT("same-slot replacement starts full and binds fresh display"));
				FOSWeapon BadSlot = Definition;
				BadSlot.NixieMaterialSlot = 99;
				Current->ApplyPickupWeapon(1, BadSlot);
				Current->UnlockWeapon(1);
				Current->FinishSwitch();
				State->Check(Current->AmmoCounter->GetBoundMaterialCount() == 0, TEXT("invalid material slot does not leave a stale display"));
				FOSWeapon MissingMaterial = GetDefault<UOSWeaponsComponent>()->Weapons[1];
				MissingMaterial.NixieMaterialSlot = 0;
				Current->ApplyPickupWeapon(1, MissingMaterial);
				Current->UnlockWeapon(1);
				Current->FinishSwitch();
				State->Check(Current->AmmoCounter->GetBoundMaterialCount() == 0, TEXT("non-Nixie material is skipped safely"));
				State->Check(GetDefault<UOSWeaponsComponent>()->Weapons[1].ViewMesh == DefaultShotgun
					&& GetDefault<UOSWeaponsComponent>()->Weapons[1].NixieMaterialSlot == INDEX_NONE,
					TEXT("global default shotgun is unchanged"));
				Current->Holster(true);
				Current->Weapons = State->SavedWeapons;
				Current->Scope->Aims = State->SavedAims;
				Current->CurrentWeapon = State->SavedCurrent;
				Current->FinishSwitch();
				UE_LOG(LogMindKore, Log, TEXT("Nixie weapon probe: %s (%d passed, %d failed). Original inventory restored; no assets saved."),
					State->Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), State->Passed, State->Failed);
				NixieProbeDone();
			}), FMath::Max(G->GetReloadRemaining() + 0.15f, 0.2f), false);
		}), FMath::Max(Guns->Weapons[1].GetActionCycleSeconds(), Guns->Weapons[1].FireInterval) + 0.15f, false);
	}

	FAutoConsoleCommandWithWorld GNixieWeaponProbe(
		TEXT("os.Nixie.WeaponProbe"),
		TEXT("Runtime pickup -> production RMB93 in hands -> actual fire/reload -> switch/holster/replacement -> restore inventory. Takes ~5s; use PIE/-game in Lvl_OS_TestRoom or ZeroG; NullRHI supported. No assets saved."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunNixieWeaponProbe));
}
#endif
