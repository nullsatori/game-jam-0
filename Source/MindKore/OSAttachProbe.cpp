// os.Attach.Probe (Lvl_OS_TestRoom / Lvl_OS_ZeroG, -nullrhi): the attachment system step by step, as
// Docs/Vision/Attachments.md §11 and the 14:16 addendum (the USP's 20-round magazine) list it. Tab is
// pressed and let go through AOSZeroGPawn::MenuPressed / MenuReleased (the keys' own handlers); the rest
// through UOSWeaponsComponent's API. Each check ends in PASS or FAIL; the last line is
// "Attach probe: done, N FAIL". Docs/Attachments.md, «Пробы».

#include "MindKore.h"
#include "OSAttachMenu.h"
#include "OSLaserSight.h"
#include "OSScope.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

#if !UE_BUILD_SHIPPING
// Named, not anonymous: in a unity build this shares a file with other probes.
namespace OSAttachProbe
{
	void After(AOSZeroGPawn* Pawn, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.001f), false);
	}

	/** Step every frame until it returns true (or 5 s pass). */
	void EveryFrame(AOSZeroGPawn* Pawn, TFunction<bool()> Step)
	{
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		const double Until = Pawn->GetWorld()->GetTimeSeconds() + 5.0;
		Pawn->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Handle, Until, Step = MoveTemp(Step)]()
		{
			if (Handle->IsValid() && (Step() || Pawn->GetWorld()->GetTimeSeconds() > Until))
			{
				Pawn->GetWorldTimerManager().ClearTimer(*Handle);
			}
		}), 0.0005f, true);
	}

	const TCHAR* OutcomeName(EOSAttachMenuOutcome Outcome)
	{
		static const TCHAR* Names[] = { TEXT("None"), TEXT("Applied"), TEXT("Cancelled"), TEXT("Killed"), TEXT("Refused") };
		return Names[uint8(Outcome)];
	}

	struct FRun
	{
		TWeakObjectPtr<AOSZeroGPawn> Pawn;
		int32 Fails = 0;

		void Check(const TCHAR* What, bool bPass, const FString& Detail = FString())
		{
			Fails += bPass ? 0 : 1;
			UE_LOG(LogMindKore, Log, TEXT("Attach probe: %s%s%s - %s"), What, Detail.IsEmpty() ? TEXT("") : TEXT(": "), *Detail, bPass ? TEXT("PASS") : TEXT("FAIL"));
		}
	};

	FName Laser() { return TEXT("Laser"); }
	FName Torch() { return TEXT("Torch"); }
	FName Suppressor() { return TEXT("Suppressor"); }
	FName BigMag() { return TEXT("BigMag"); }

	/** The pistol in hand, settled, with a clean pouch. */
	void Reset(AOSZeroGPawn* Pawn, int32 Gun = 0)
	{
		UOSWeaponsComponent* Guns = Pawn->Guns;
		Guns->AttachMenu->Kill();
		Pawn->SetHands(EOSHands::Gun);
		Guns->UnlockWeapon(Gun);
		Guns->SelectWeapon(Gun);
		Guns->FinishSwitch();
	}

	FString Ammo(const FOSWeapon& Weapon)
	{
		return FString::Printf(TEXT("%d/%d of %d%s"), Weapon.Ammo, Weapon.Reserve, Weapon.MagazineSize, Weapon.bSlideLocked ? TEXT(" slide locked") : TEXT(""));
	}

	void Run(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn || !Pawn->Guns || !Pawn->Guns->AttachMenu || Pawn->Guns->Weapons.Num() < 3 || !Pawn->Guns->FindAttachment(BigMag()))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Attach probe: needs the AOSZeroGPawn with its guns and the test set - FAIL"));
			return;
		}
		const TSharedRef<FRun> R = MakeShared<FRun>();
		R->Pawn = Pawn;
		UOSWeaponsComponent* Guns = Pawn->Guns;
		UOSAttachMenu* Menu = Guns->AttachMenu;
		float T = 1.0f;
		const auto At = [Pawn, &T](float Wait, TFunction<void()> Step) { T += Wait; After(Pawn, T, MoveTemp(Step)); };

		// 1. No attachments: Tab held opens «ОБВЕСА НЕТ», the gun in the menu's pose, no shot; let go: nothing.
		At(0.0f, [Pawn, Guns]()
		{
			for (const FOSAttachment& Each : TArray<FOSAttachment>(Guns->Attachments))
			{
				Guns->TakeAttachment(Each.Id);
			}
			Reset(Pawn);
			Guns->Weapons[0].Ammo = 12;
			Guns->Weapons[0].Reserve = 60;
		});
		At(1.0f, [Pawn]() { Pawn->MenuPressed(); });
		At(0.7f, [Pawn, Guns, Menu, R]()
		{
			R->Check(TEXT("no attachments, Tab held 0.7 s: open with no rows"), Menu->IsOpen() && Menu->GetRowCount() == 0, Menu->DescribeRows());
			R->Check(TEXT("the gun in the menu's pose"), Guns->Scope && Guns->Scope->GetMenuAlpha() > 0.3f, FString::Printf(TEXT("alpha %.2f"), Guns->Scope ? Guns->Scope->GetMenuAlpha() : -1.0f));
			const int32 Before = Guns->Weapons[0].Ammo;
			R->Check(TEXT("not ready, LMB does not fire"), !Guns->IsWeaponReady() && !Pawn->Fire() && Guns->Weapons[0].Ammo == Before);
			Pawn->MenuReleased();
		});
		At(1.0f, [Guns, Menu, R]()
		{
			R->Check(TEXT("let go: closed, nothing on"), !Menu->IsActive() && !Guns->HasAttachment(Laser()), OutcomeName(Menu->GetLastOutcome()));
			R->Check(TEXT("the gun free again"), Menu->IsGunFree());
		});

		// 2. The laser in the pouch: let go before the frame is up (415 ms) - nothing applied.
		At(0.2f, [Pawn, Guns]() { Guns->GiveAttachment(Laser(), true); Pawn->MenuPressed(); });
		At(0.3f, [Pawn]() { Pawn->MenuReleased(); });
		At(0.5f, [Guns, Menu, R]()
		{
			int32 Gun, Mount;
			R->Check(TEXT("let go at 300 ms: cancelled, the laser in the pouch"), Menu->GetLastOutcome() == EOSAttachMenuOutcome::Cancelled && !Guns->FindMounted(Laser(), Gun, Mount),
				OutcomeName(Menu->GetLastOutcome()));
		});

		// 3. Held: the row; the wheel round; let go: the laser rolled on, the dot lit; the gun back.
		At(0.3f, [Pawn]() { Pawn->MenuPressed(); });
		At(0.7f, [Pawn, Guns, Menu, R]()
		{
			R->Check(TEXT("the laser's row"), Menu->IsOpen() && Menu->GetRowCount() >= 1 && Menu->GetSelected() == 0, Menu->DescribeRows());
			Menu->Step(1);
			Menu->Step(-1);
			const double Released = Pawn->GetWorld()->GetTimeSeconds();
			Pawn->MenuReleased();
			const TSharedRef<double> SeatedAt = MakeShared<double>(-1.0);
			EveryFrame(Pawn, [Pawn, Guns, Menu, R, Released, SeatedAt]()
			{
				const double Now = Pawn->GetWorld()->GetTimeSeconds();
				if (*SeatedAt < 0.0 && Guns->GetActiveOn(0, EOSMountType::Rail))
				{
					*SeatedAt = Now;
					R->Check(TEXT("the laser seated"), Now - Released < 1.6, FString::Printf(TEXT("%.2f s after the release (close + roll-on)"), Now - Released));
				}
				if (*SeatedAt >= 0.0 && Menu->IsGunFree())
				{
					R->Check(TEXT("the gun back after the seat"), Now - *SeatedAt <= 0.6 + 0.3, FString::Printf(TEXT("%.2f s"), Now - *SeatedAt));
					return true;
				}
				return false;
			});
		});
		At(2.5f, [Guns, Menu, R]()
		{
			R->Check(TEXT("applied, the laser on the USP's rail"), Menu->GetLastOutcome() == EOSAttachMenuOutcome::Applied && Guns->Weapons[0].Mounts[0].Mounted == Laser(),
				OutcomeName(Menu->GetLastOutcome()));
			R->Check(TEXT("the laser lit, its module shown"), Guns->LaserSight && Guns->LaserSight->IsLit() && Guns->ModulesInView.IsValidIndex(0) && Guns->ModulesInView[0]->IsVisible());
		});

		// 4. A tap: off; again: on.
		At(0.2f, [Pawn]() { Pawn->MenuPressed(); });
		At(0.1f, [Pawn]() { Pawn->MenuReleased(); });
		At(0.2f, [Guns, Menu, R]() { R->Check(TEXT("tap: the laser off"), !Guns->IsSwitchOn(Laser()) && !Guns->LaserSight->IsLit() && !Menu->IsActive()); });
		At(0.2f, [Pawn]() { Pawn->MenuPressed(); });
		At(0.1f, [Pawn]() { Pawn->MenuReleased(); });
		At(0.2f, [Guns, R]() { R->Check(TEXT("tap again: on"), Guns->IsSwitchOn(Laser()) && Guns->LaserSight->IsLit()); });

		// 5. Interruptions: a slot key, a grab - dark at once, nothing applied.
		At(0.5f, [Pawn]() { Pawn->MenuPressed(); });
		At(0.7f, [Pawn, Guns, Menu, R]()
		{
			Pawn->PressSlot(0);
			R->Check(TEXT("Tab + 1: killed"), Menu->GetLastOutcome() == EOSAttachMenuOutcome::Killed && !Menu->IsActive() && Guns->Weapons[0].Mounts[0].Mounted == Laser(),
				OutcomeName(Menu->GetLastOutcome()));
			Pawn->MenuReleased();
		});
		At(0.8f, [Pawn]() { Reset(Pawn); Pawn->MenuPressed(); });
		At(0.7f, [Pawn, Menu, R]()
		{
			Pawn->SetHands(EOSHands::Held);
			R->Check(TEXT("a grab: killed"), Menu->GetLastOutcome() == EOSAttachMenuOutcome::Killed && !Menu->IsActive(), OutcomeName(Menu->GetLastOutcome()));
			Pawn->MenuReleased();
		});

		// 6. Off: the module, the dot and the thread gone the next frame.
		At(0.5f, [Pawn]() { Reset(Pawn); });
		At(1.0f, [Pawn, Guns, R]()
		{
			Guns->UnmountAttachment(Laser());
			After(Pawn, 0.02f, [Guns, R]()
			{
				R->Check(TEXT("unmounted: module, dot and thread hidden"), !Guns->ModulesInView[0]->IsVisible() && !Guns->LaserSight->IsLit() && !Guns->LaserSight->IsThreadShown());
			});
		});

		// 7. Tab in a reload: refused.
		At(0.5f, [Pawn, Guns, Menu, R]()
		{
			Guns->Weapons[0].Ammo = 5;
			Guns->Reload();
			After(Pawn, 0.1f, [Pawn, Menu, R]()
			{
				Pawn->MenuPressed();
				R->Check(TEXT("Tab in a reload: refused"), Menu->GetLastOutcome() == EOSAttachMenuOutcome::Refused && !Menu->IsActive(), OutcomeName(Menu->GetLastOutcome()));
				Pawn->MenuReleased();
			});
		});

		// 8. Tab in the pump's cycle: it waits for the cycle's end, then opens.
		At(2.0f, [Pawn, Guns]() { Guns->GiveAttachment(Torch(), true); Reset(Pawn, 1); });
		At(1.5f, [Pawn, Guns, Menu, R]()
		{
			const bool bFired = Pawn->Fire();
			Pawn->MenuPressed();
			R->Check(TEXT("Tab in the pump's cycle: waiting"), bFired && Menu->IsActive() && !Menu->IsOpen(), bFired ? TEXT("") : TEXT("the shotgun did not fire"));
			EveryFrame(Pawn, [Pawn, Menu, R]()
			{
				if (!Menu->IsOpen())
				{
					return false;
				}
				R->Check(TEXT("the cycle through: open"), true);
				Pawn->MenuReleased();
				Menu->Kill();
				return true;
			});
		});

		// 9. The RMB-93 into the shotgun's slot: the torch moves over; and back.
		At(3.0f, [Guns, R]()
		{
			Guns->MountAttachment(Torch(), 1);
			OSSwapGun(*Guns, 1, TEXT("Rmb93"));
			const bool bOn = Guns->Weapons[1].Id == FName(TEXT("Rmb93")) && Guns->Weapons[1].Mounts.ContainsByPredicate([](const FOSWeaponMount& Each) { return Each.Mounted == Torch(); });
			R->Check(TEXT("RMB-93 in slot 2: the torch on its rail"), bOn, Guns->DescribeGear().Replace(TEXT("\n"), TEXT("; ")));
			OSSwapGun(*Guns, 1, TEXT("Slot"));
			R->Check(TEXT("the pump back: the torch on its rail"), Guns->Weapons[1].Id == FName(TEXT("Shotgun")) && Guns->Weapons[1].Mounts[0].Mounted == Torch());
		});

		// 10. The torch onto the rail the laser holds: the laser rolls off into the pouch, the torch on.
		At(0.3f, [Pawn, Guns]()
		{
			Reset(Pawn);
			Guns->UnmountAttachment(Torch());
			Guns->MountAttachment(Laser(), 0);
		});
		At(1.0f, [Guns, R]() { R->Check(TEXT("apply the torch on the laser's rail"), Guns->ApplyAttachment(0, Torch())); });
		At(2.5f, [Guns, R]()
		{
			R->Check(TEXT("the torch on, the laser in the pouch"), Guns->Weapons[0].Mounts[0].Mounted == Torch() && Guns->Carried.Contains(Laser()) && !Guns->IsSeating(),
				Guns->DescribeGear().Replace(TEXT("\n"), TEXT("; ")));
		});

		// 11. The suppressor: the muzzle at its end, the shot's sound its own; off: as it was.
		const TSharedRef<FVector> Bare = MakeShared<FVector>();
		At(0.3f, [Guns, Bare]()
		{
			*Bare = Guns->ViewModel->GetComponentTransform().InverseTransformPosition(Guns->GetMuzzleTransform().GetLocation());
			Guns->GiveAttachment(Suppressor(), true);
			Guns->MountAttachment(Suppressor(), 0);
		});
		At(0.3f, [Guns, Bare, R]()
		{
			const FOSAttachment* On = Guns->GetActiveOn(0, EOSMountType::Muzzle);
			const FVector Now = Guns->ViewModel->GetComponentTransform().InverseTransformPosition(Guns->GetMuzzleTransform().GetLocation());
			R->Check(TEXT("suppressor: the muzzle moved out, the sound replaced"), On && On->Id == Suppressor() && FVector::Dist(Now, *Bare) > 8.0f && !On->ShotSoundOverride.IsNull(),
				FString::Printf(TEXT("%.1f cm"), FVector::Dist(Now, *Bare)));
			Guns->UnmountAttachment(Suppressor());
		});
		At(0.3f, [Guns, Bare, R]()
		{
			const FVector Now = Guns->ViewModel->GetComponentTransform().InverseTransformPosition(Guns->GetMuzzleTransform().GetLocation());
			R->Check(TEXT("suppressor off: the muzzle back"), !Guns->GetActiveOn(0, EOSMountType::Muzzle) && FVector::Dist(Now, *Bare) < 2.0f, FString::Printf(TEXT("%.1f cm"), FVector::Dist(Now, *Bare)));
		});

		// 12. The 20-round magazine (14:16): swapped through the reload, the rounds kept; R fills it; off cuts to 12.
		At(0.3f, [Guns, R]()
		{
			FOSWeapon& Usp = Guns->Weapons[0];
			Usp.Ammo = 7;
			Usp.Reserve = 60;
			Usp.bSlideLocked = false;
			Guns->GiveAttachment(BigMag(), true);
			R->Check(TEXT("apply BigMag at 7/60"), Guns->ApplyAttachment(2, BigMag()));
		});
		At(2.0f, [Guns, R]()
		{
			const FOSWeapon& Usp = Guns->Weapons[0];
			R->Check(TEXT("BigMag on: 7/60 of 20"), Usp.Ammo == 7 && Usp.Reserve == 60 && Usp.MagazineSize == 20 && Usp.Mounts[2].Mounted == BigMag(), Ammo(Usp));
			R->Check(TEXT("the clear magazine shows 7 rounds"), Guns->RoundsInView && Guns->RoundsInView->GetInstanceCount() == 7,
				FString::Printf(TEXT("%d"), Guns->RoundsInView ? Guns->RoundsInView->GetInstanceCount() : -1));
			Guns->Reload();
		});
		At(2.2f, [Guns, R]()
		{
			const FOSWeapon& Usp = Guns->Weapons[0];
			R->Check(TEXT("R: 20/40 (the dropped magazine takes its 7)"), Usp.Ammo == 20 && Usp.Reserve == 40, Ammo(Usp));
			R->Check(TEXT("the clear magazine shows 20"), Guns->RoundsInView->GetInstanceCount() == 20, FString::Printf(TEXT("%d"), Guns->RoundsInView->GetInstanceCount()));
			FOSWeapon& Edit = Guns->Weapons[0];
			Edit.Ammo = 18;
			Edit.Reserve = 40;
			R->Check(TEXT("take BigMag off at 18/40"), Guns->ApplyAttachment(2, NAME_None));
		});
		At(2.0f, [Guns, R]()
		{
			const FOSWeapon& Usp = Guns->Weapons[0];
			R->Check(TEXT("stock back: 12/46 of 12"), Usp.Ammo == 12 && Usp.Reserve == 46 && Usp.MagazineSize == 12 && Usp.Mounts[2].Mounted.IsNone(), Ammo(Usp));
			FOSWeapon& Edit = Guns->Weapons[0];
			Edit.Ammo = 0;
			Edit.bSlideLocked = true;
			R->Check(TEXT("apply BigMag on the slide stop"), Guns->ApplyAttachment(2, BigMag()));
		});
		At(2.2f, [Pawn, Guns, R]()
		{
			const FOSWeapon& Usp = Guns->Weapons[0];
			R->Check(TEXT("slide released, 20 in"), Usp.Ammo == 20 && !Usp.bSlideLocked && Usp.MagazineSize == 20, Ammo(Usp));
			UE_LOG(LogMindKore, Log, TEXT("Attach probe: gear at the end - %s"), *Guns->DescribeGear().Replace(TEXT("\n"), TEXT("; ")));
			UE_LOG(LogMindKore, Log, TEXT("Attach probe: done, %d FAIL"), R->Fails);
		});
	}

	FAutoConsoleCommandWithWorld GAttachProbe(
		TEXT("os.Attach.Probe"),
		TEXT("os.Attach.Probe: the attachment menu, seating, the laser, the torch, the suppressor, the RMB-93 swap and the 20-round magazine, step by step (Lvl_OS_TestRoom, -nullrhi); PASS/FAIL per line."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&Run));
}
#endif
