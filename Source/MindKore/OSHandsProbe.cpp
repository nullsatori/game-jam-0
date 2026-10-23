// os.Hands.Probe (Lvl_OS_Ship): the hands and their keys (Docs/Review-2026-10-19.md #25,
// Docs/ZeroG.md «Руки»). Every key goes in through its real input action (injected, as the
// mouse and keyboard would), in each move mode and with each thing in hand, and what changed
// is compared with the controls table of README.md, written out again here - not read from
// AOSZeroGPawn::Route. Before that: a hull tool taken mid-reload abandons the reload (no
// magazine dropped, no rounds counted), and with a tool in hand there is no gun (GetWeapon,
// the HUD's RMB line). Each line ends in PASS or FAIL; the last is "Hands probe: done".

#include "OSAttachMenu.h"
#include "OSLadder.h"
#include "OSScope.h"
#include "OSShip.h"
#include "OSGravity.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "MindKore.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "TimerManager.h"

// Named, not anonymous: in a unity build this shares a file with other probes.
namespace OSHandsProbe
{
	void After(AOSZeroGPawn* Pawn, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), FMath::Max(Seconds, 0.001f), false);
	}

	/** Step every frame until it returns true. */
	void EveryFrame(AOSZeroGPawn* Pawn, TFunction<bool()> Step)
	{
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		Pawn->GetWorldTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Handle, Step = MoveTemp(Step)]()
		{
			if (Handle->IsValid() && Step())
			{
				Pawn->GetWorldTimerManager().ClearTimer(*Handle);
			}
		}), 0.0005f, true);
	}

	/** One frame of the action named Name on the pawn, as its key would give it. */
	bool Inject(AOSZeroGPawn* Pawn, const FString& Name, const FInputActionValue& Value)
	{
		const APlayerController* Controller = Cast<APlayerController>(Pawn->GetController());
		UEnhancedInputLocalPlayerSubsystem* Input = Controller ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()) : nullptr;
		UInputAction* Action = FindObject<UInputAction>(Pawn, *Name);
		if (Input && Action)
		{
			Input->InjectInputForAction(Action, Value, {}, {});
		}
		return Input && Action;
	}

	/** The key held for Seconds (injected every frame), or pressed for one frame (Seconds 0). */
	void Press(AOSZeroGPawn* Pawn, const FString& Name, const FInputActionValue& Value, float Seconds)
	{
		if (!Inject(Pawn, Name, Value))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hands probe: no input action %s - FAIL"), *Name);
			return;
		}
		const double End = Pawn->GetWorld()->GetTimeSeconds() + Seconds;
		EveryFrame(Pawn, [Pawn, Name, Value, End]()
		{
			if (Pawn->GetWorld()->GetTimeSeconds() >= End)
			{
				return true;
			}
			Inject(Pawn, Name, Value);
			return false;
		});
	}

	const TCHAR* Verdict(bool bPass)
	{
		return bPass ? TEXT("PASS") : TEXT("FAIL");
	}

	/** What the keys can change, read in one frame. */
	struct FSnap
	{
		EOSHands Hands = EOSHands::Gun;
		EOSHullTool Tool = EOSHullTool::None;
		int32 Current = 0;
		int32 Ammo = 0;
		bool bReload = false;
		int32 Lines = 0;
		float LastLength = 0.0f;
		bool bHeld = false;
		bool bCling = false;
		float Aim = 0.0f;
		/** The attachment menu up (Tab held, Docs/Attachments.md): a key must not open or leave it by itself. */
		bool bMenu = false;

		FString ToString() const
		{
			return FString::Printf(TEXT("%s%s gun %d ammo %d%s lines %d (%.0f cm)%s%s aim %.2f%s"), *UEnum::GetDisplayValueAsText(Hands).ToString(),
				Tool == EOSHullTool::Strap ? TEXT("(strap)") : Tool == EOSHullTool::Welder ? TEXT("(welder)") : TEXT(""), Current, Ammo,
				bReload ? TEXT(" reloading") : TEXT(""), Lines, LastLength, bHeld ? TEXT(" holding") : TEXT(""), bCling ? TEXT(" clinging") : TEXT(""), Aim,
				bMenu ? TEXT(" menu") : TEXT(""));
		}
	};

	FSnap Snap(const AOSZeroGPawn* Pawn)
	{
		FSnap S;
		const UOSWeaponsComponent* Guns = Pawn->Guns;
		S.Hands = Pawn->GetHands();
		S.Tool = Pawn->HullTools->GetTool();
		S.Current = Guns->CurrentWeapon;
		S.Ammo = Guns->Weapons.IsValidIndex(Guns->CurrentWeapon) ? Guns->Weapons[Guns->CurrentWeapon].Ammo : -1;
		S.bReload = Guns->GetReloadRemaining() > 0.0f;
		S.Lines = Pawn->HullTools->GetLines().Num();
		S.LastLength = S.Lines > 0 ? Pawn->HullTools->GetLines().Last().LengthCm : 0.0f;
		S.bHeld = Pawn->GetGrabbed() != nullptr;
		S.bCling = Pawn->IsClinging();
		S.Aim = Guns->Scope ? Guns->Scope->GetAimAlpha() : 0.0f;
		S.bMenu = Guns->AttachMenu && Guns->AttachMenu->IsActive();
		return S;
	}

	/** What a key should have done (README.md, «Управление»). */
	enum class EOutcome : uint8 { Nothing, Shot, Hooked, Thrown, Grabbed, LetGoBody, LetGoHold, Aimed, NextGun, Reeled, Reloaded, Cut, Gun1, Gun2, Strap, Welder, PutAway };

	const TCHAR* OutcomeName(EOutcome Outcome)
	{
		static const TCHAR* Names[] = { TEXT("nothing"), TEXT("a shot"), TEXT("a strap line hooked"), TEXT("the body thrown"), TEXT("the body taken"),
			TEXT("the body let go"), TEXT("the hold let go"), TEXT("the aim up"), TEXT("the next gun"), TEXT("the line reeled"), TEXT("a reload"),
			TEXT("a line cut"), TEXT("gun 1 in hand"), TEXT("gun 2 in hand"), TEXT("the strap in hand"), TEXT("the welder in hand"), TEXT("the tool put away") };
		return Names[uint8(Outcome)];
	}

	bool Happened(EOutcome Outcome, const FSnap& B, const FSnap& A)
	{
		switch (Outcome)
		{
		case EOutcome::Nothing:
			return A.Hands == B.Hands && A.Tool == B.Tool && A.Current == B.Current && A.Ammo == B.Ammo && A.bReload == B.bReload && A.Lines == B.Lines
				&& FMath::Abs(A.LastLength - B.LastLength) < 0.5f && A.bHeld == B.bHeld && A.bCling == B.bCling && A.Aim - B.Aim < 0.05f && A.bMenu == B.bMenu;
		case EOutcome::Shot: return A.Ammo == B.Ammo - 1;
		case EOutcome::Hooked: return A.Lines == B.Lines + 1;
		case EOutcome::Thrown: return B.bHeld && !A.bHeld;
		case EOutcome::Grabbed: return A.bHeld && A.Hands == EOSHands::Held;
		case EOutcome::LetGoBody: return B.bHeld && !A.bHeld && A.Hands != EOSHands::Held && A.bCling == B.bCling;
		case EOutcome::LetGoHold: return B.bCling && !A.bCling && A.bHeld == B.bHeld && A.Hands == B.Hands;
		case EOutcome::Aimed: return A.Aim - B.Aim > 0.05f;
		case EOutcome::NextGun: return A.Hands == EOSHands::Gun && A.Current != B.Current && !A.bHeld;
		case EOutcome::Reeled: return A.Lines == B.Lines && FMath::Abs(A.LastLength - B.LastLength) > 1.0f;
		case EOutcome::Reloaded: return A.bReload && !B.bReload;
		case EOutcome::Cut: return A.Lines == B.Lines - 1;
		case EOutcome::Gun1: return A.Hands == EOSHands::Gun && A.Current == 0 && !A.bHeld;
		case EOutcome::Gun2: return A.Hands == EOSHands::Gun && A.Current == 1 && !A.bHeld;
		case EOutcome::Strap: return A.Hands == EOSHands::Tool && A.Tool == EOSHullTool::Strap && !A.bHeld;
		case EOutcome::Welder: return A.Hands == EOSHands::Tool && A.Tool == EOSHullTool::Welder && !A.bHeld;
		case EOutcome::PutAway: return B.Hands == EOSHands::Tool && A.Hands == EOSHands::Gun;
		}
		return false;
	}

	/** The keys under test: the input action, its value, how long held, its README name. */
	struct FKeyCase
	{
		const TCHAR* Name;
		FString Action;
		FInputActionValue Value;
		float HoldSeconds;
	};

	/** README.md «Управление», one row per EOSHands (Empty: nothing picked up yet), one column per key. RMB: see Expected. */
	EOutcome Expected(EOSMoveMode Mode, EOSHands Hands, int32 Key)
	{
		using O = EOutcome;
		//                              LMB         RMB         MMB         wheel       R           1           2           strap       welder
		static const O Table[4][9] = {
			/* Gun */                 { O::Shot,    O::Grabbed, O::Aimed,   O::NextGun, O::Reloaded, O::Nothing, O::Gun2, O::Strap,   O::Welder },
			/* Tool (the strap) */    { O::Hooked,  O::Grabbed, O::Nothing, O::Reeled,  O::Cut,     O::Gun1,    O::Gun2,    O::PutAway, O::Welder },
			/* Held */                { O::Thrown,  O::LetGoBody, O::Nothing, O::NextGun, O::Nothing, O::Gun1,  O::Gun2,    O::Strap,   O::Welder },
			/* Empty */               { O::Nothing, O::Grabbed, O::Nothing, O::Nothing, O::Nothing, O::Nothing, O::Nothing, O::Strap,  O::Welder },
		};
		if (Key == 1)
		{
			// RMB with a loose body in front: on the ladder the hands are on the rungs; hanging, RMB lets go (the body held first).
			if (Mode == EOSMoveMode::Ladder)
			{
				return O::Nothing;
			}
			if (Mode == EOSMoveMode::Hang)
			{
				return Hands == EOSHands::Held ? O::LetGoBody : O::LetGoHold;
			}
		}
		return Table[uint8(Hands)][Key];
	}

	struct FStand
	{
		AOSZeroGPawn* Pawn = nullptr;
		UPrimitiveComponent* Loose = nullptr;
		UPrimitiveComponent* Floor = nullptr;
		UPrimitiveComponent* Ceiling = nullptr;
		FVector FloorPoint = FVector::ZeroVector;
		FVector CeilingPoint = FVector::ZeroVector;
		FVector WalkSpot = FVector::ZeroVector;
		FVector FloatSpot = FVector::ZeroVector;
		AOSLadder* Ladder = nullptr;
		TArray<FKeyCase> Keys;
		/** The mode under test and where it holds: the spot and view to come back to, the hold (Hang). */
		EOSMoveMode Mode = EOSMoveMode::Walk;
		FVector Spot = FVector::ZeroVector;
		FRotator View = FRotator::ZeroRotator;
		bool bReplace = true;
		int32 Checks = 0;
		int32 Failed = 0;
	};

	/** The loose body Distance in front of the eye, at rest. It must stay there: the zones' body gravity
	 *  (OSPullBodies) skips bodies with gravity off, so the cabin's pull does not drop it out of reach. */
	void PlaceLoose(const FStand& Stand, float Distance)
	{
		const AOSZeroGPawn* Pawn = Stand.Pawn;
		Stand.Loose->SetEnableGravity(false);
		Stand.Loose->SetWorldLocation(Pawn->ShoulderLocation() + Pawn->GetControlRotation().Vector() * Distance, false, nullptr, ETeleportType::TeleportPhysics);
		Stand.Loose->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Stand.Loose->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	}

	float LooseDistance(const FStand& Stand)
	{
		return FMath::Max(90.0f, Stand.Loose->Bounds.SphereRadius + 50.0f);
	}

	/** Back to the mode's spot (Walk, Float) or hold (Hang), the guns reset (pistol, one round short), Hands in hand, one strap line. */
	void Reset(const TSharedRef<FStand>& Stand, EOSHands Hands)
	{
		AOSZeroGPawn* Pawn = Stand->Pawn;
		UOSWeaponsComponent* Guns = Pawn->Guns;
		if (Pawn->GetGrabbed())
		{
			Pawn->Release();
		}
		if (Stand->bReplace)
		{
			Pawn->LetGoCling();
			Pawn->Body->SetWorldLocation(Stand->Spot, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		}
		Pawn->GetController()->SetControlRotation(Stand->View);
		if (Stand->Mode == EOSMoveMode::Hang && !Pawn->IsClinging())
		{
			Pawn->Cling(Stand->Ceiling, Stand->CeilingPoint);
		}
		Pawn->HullTools->CutAll();
		Pawn->HullTools->AddLine(Stand->Floor, Stand->FloorPoint, Stand->Ceiling, Stand->CeilingPoint);
		for (FOSWeapon& Gun : Guns->Weapons)
		{
			Gun.bUnlocked = true;
			Gun.Ammo = Gun.MagazineSize - 1;
			Gun.Reserve = FMath::Max(Gun.Reserve, 20);
			Gun.SinceShot = 100.0f;
			Gun.FireReadySince = 0.0f;
		}
		Guns->SelectWeapon(0);
		Pawn->SetHands(EOSHands::Held); // any reload abandoned, the gun out of view
		Guns->Scope->SetHeld(false);
		Guns->Scope->SetAimHeld(false);
		switch (Hands)
		{
		case EOSHands::Gun:
			Pawn->SetHands(EOSHands::Gun);
			Guns->FinishSwitch();
			break;
		case EOSHands::Tool:
			Pawn->SetHands(EOSHands::Tool, EOSHullTool::Strap);
			break;
		case EOSHands::Held:
			PlaceLoose(*Stand, LooseDistance(*Stand));
			Pawn->Grab(Stand->Loose, Stand->Loose->GetComponentLocation());
			break;
		default:
			Pawn->SetHands(EOSHands::Gun);
			for (FOSWeapon& Gun : Guns->Weapons)
			{
				Gun.bUnlocked = false;
			}
			break;
		}
		if (Hands != EOSHands::Held)
		{
			PlaceLoose(*Stand, LooseDistance(*Stand));
		}
	}

	/** Every key with every thing in hand, in the stand's mode; then Then. */
	void RunKeys(const TSharedRef<FStand>& Stand, int32 Index, TFunction<void()> Then)
	{
		const int32 Count = 4 * Stand->Keys.Num();
		if (Index >= Count)
		{
			Then();
			return;
		}
		AOSZeroGPawn* Pawn = Stand->Pawn;
		const EOSHands Hands = EOSHands(Index / Stand->Keys.Num());
		const int32 Key = Index % Stand->Keys.Num();
		Reset(Stand, Hands);
		After(Pawn, 0.15f, [Stand, Pawn, Hands, Key, Index, Then]()
		{
			const FSnap Before = Snap(Pawn);
			const FKeyCase& Case = Stand->Keys[Key];
			const EOSMoveMode Mode = Pawn->GetMoveMode();
			Press(Pawn, Case.Action, Case.Value, Case.HoldSeconds);
			// Read while a held key is still down (the aim), the next check once it is up.
			After(Pawn, 0.5f, [Stand, Index, Then]() { RunKeys(Stand, Index + 1, Then); });
			After(Pawn, 0.3f, [Stand, Pawn, Hands, Key, Before, Mode]()
			{
				const FSnap Now = Snap(Pawn);
				const FKeyCase& Case = Stand->Keys[Key];
				const EOutcome Want = Expected(Stand->Mode, Hands, Key);
				const bool bPass = Mode == Stand->Mode && Before.Hands == Hands && Happened(Want, Before, Now);
				++Stand->Checks;
				Stand->Failed += bPass ? 0 : 1;
				UE_LOG(LogMindKore, Log, TEXT("Hands probe [%s, %s, %s]: expected %s - %s -> %s%s - %s"), *UEnum::GetDisplayValueAsText(Stand->Mode).ToString(),
					*UEnum::GetDisplayValueAsText(Hands).ToString(), Case.Name, OutcomeName(Want), *Before.ToString(), *Now.ToString(),
					Mode == Stand->Mode ? TEXT("") : *FString::Printf(TEXT(" (mode was %s)"), *UEnum::GetDisplayValueAsText(Mode).ToString()), Verdict(bPass));
				Pawn->ProbeThrust(FVector::ZeroVector, false);
			});
		});
	}

	/** A hull tool taken mid-reload: the control (no tool, the magazine drops and seats), then the strap 0.2 s in. */
	void CheckReloadCancel(const TSharedRef<FStand>& Stand, TFunction<void()> Then)
	{
		AOSZeroGPawn* Pawn = Stand->Pawn;
		UOSWeaponsComponent* Guns = Pawn->Guns;
		const auto Start = [Stand, Pawn, Guns](bool bTool, TFunction<void()> Next)
		{
			Reset(Stand, EOSHands::Gun);
			Guns->SelectWeapon(2);
			Guns->FinishSwitch();
			FOSWeapon& Rifle = Guns->Weapons[2];
			Rifle.Ammo = 1;
			const int32 Reserve = Rifle.Reserve;
			const int32 Debris = Guns->GetDebrisCount();
			const float Length = Rifle.ReloadSeconds;
			After(Pawn, 0.15f, [Stand, Pawn, Guns, bTool, Next, Reserve, Debris, Length]()
			{
				Press(Pawn, TEXT("IA_ZG_Reload"), FInputActionValue(true), 0.0f);
				After(Pawn, 0.2f, [Stand, Pawn, Guns, bTool, Next, Reserve, Debris, Length]()
				{
					const bool bRunning = Guns->GetReloadRemaining() > 0.0f;
					if (bTool)
					{
						Press(Pawn, FString::Printf(TEXT("IA_ZG_Slot%d"), Pawn->GetToolSlot(EOSHullTool::Strap) + 1), FInputActionValue(true), 0.0f);
					}
					After(Pawn, 0.1f, [Stand, Pawn, Guns, bTool, Next, Reserve, Debris, Length, bRunning]()
					{
						const float Left = Guns->GetReloadRemaining();
						After(Pawn, Length + 0.5f, [Stand, Pawn, Guns, bTool, Next, Reserve, Debris, bRunning, Left]()
						{
							const FOSWeapon& Rifle = Guns->Weapons[2];
							const int32 Dropped = Guns->GetDebrisCount() - Debris;
							const bool bPass = bRunning && (bTool
								? Left <= 0.0f && Dropped == 0 && Rifle.Ammo == 1 && Rifle.Reserve == Reserve && Pawn->GetHands() == EOSHands::Tool
								: Dropped == 1 && Rifle.Ammo == Rifle.MagazineSize && Rifle.Reserve == Reserve - Rifle.MagazineSize); // the dropped round leaves with its magazine
							++Stand->Checks;
							Stand->Failed += bPass ? 0 : 1;
							UE_LOG(LogMindKore, Log, TEXT("Hands probe [reload, %s]: SV-98 from 1 round, reload %s 0.2 s in, %.2f s left 0.1 s after %s; after its length: %d magazine(s) dropped, %d/%d (reserve was %d), hands %s - %s"),
								bTool ? TEXT("the strap taken 0.2 s in") : TEXT("control, no tool"), bRunning ? TEXT("running") : TEXT("NOT running"), Left,
								bTool ? TEXT("the strap") : TEXT("that"), Dropped, Rifle.Ammo, Rifle.Reserve, Reserve,
								*UEnum::GetDisplayValueAsText(Pawn->GetHands()).ToString(), Verdict(bPass));
							Next();
						});
					});
				});
			});
		};
		Start(false, [Start, Then]() { Start(true, Then); });
	}

	/** A loose body in reach: with the gun out the HUD offers to take it and put the gun away; with the strap in hand, no gun at all. */
	void CheckToolHasNoGun(const TSharedRef<FStand>& Stand, TFunction<void()> Then)
	{
		AOSZeroGPawn* Pawn = Stand->Pawn;
		Reset(Stand, EOSHands::Gun);
		After(Pawn, 0.15f, [Stand, Pawn, Then]()
		{
			const FString GunPrompt = Pawn->GetRmbPrompt();
			const bool bGun = Pawn->Guns->GetWeapon() != nullptr;
			Press(Pawn, FString::Printf(TEXT("IA_ZG_Slot%d"), Pawn->GetToolSlot(EOSHullTool::Strap) + 1), FInputActionValue(true), 0.0f);
			After(Pawn, 0.2f, [Stand, Pawn, Then, GunPrompt, bGun]()
			{
				const FString ToolPrompt = Pawn->GetRmbPrompt();
				const FOSWeapon* Weapon = Pawn->Guns->GetWeapon();
				const bool bPass = bGun && GunPrompt == TEXT("[ПКМ] взять (оружие убрать)") && Pawn->GetHands() == EOSHands::Tool && !Weapon
					&& ToolPrompt == TEXT("[ПКМ] взять");
				Stand->Checks += 1;
				Stand->Failed += bPass ? 0 : 1;
				UE_LOG(LogMindKore, Log, TEXT("Hands probe [tool, no gun]: gun out - GetWeapon %s, HUD '%s'; the strap taken - hands %s, GetWeapon %s, HUD '%s' (the recoil path draws only with GetWeapon) - %s"),
					bGun ? TEXT("set") : TEXT("null"), *GunPrompt, *UEnum::GetDisplayValueAsText(Pawn->GetHands()).ToString(), Weapon ? TEXT("SET") : TEXT("null"),
					*ToolPrompt, Verdict(bPass));
				Then();
			});
		});
	}

	/** Hold W toward the ladder from its foot until it takes the body (3 s at most), then 0.6 s up off the foot (at rest on the foot the climb ends); Then(on it). */
	void MountLadder(const TSharedRef<FStand>& Stand, TFunction<void(bool)> Then)
	{
		AOSZeroGPawn* Pawn = Stand->Pawn;
		const FTransform Frame = Stand->Ladder->GetActorTransform();
		const FRotator Facing(0.0f, Frame.GetUnitAxis(EAxis::Y).Rotation().Yaw, 0.0f);
		Pawn->LetGoCling();
		Pawn->Body->SetWorldLocation(Frame.TransformPosition(Stand->Ladder->Bottom), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Pawn->GetController()->SetControlRotation(Facing);
		const double End = Pawn->GetWorld()->GetTimeSeconds() + 3.0;
		After(Pawn, 1.0f, [Pawn, End, Then]()
		{
			EveryFrame(Pawn, [Pawn, End, Then]()
			{
				const bool bOn = Pawn->IsClimbingLadder();
				if (bOn || Pawn->GetWorld()->GetTimeSeconds() > End + 1.0)
				{
					Pawn->ProbeThrust(FVector::ZeroVector, false);
					if (!bOn)
					{
						Then(false);
						return true;
					}
					Pawn->GetController()->SetControlRotation(Pawn->GetControlRotation() + FRotator(30.0f, 0.0f, 0.0f));
					const double Up = Pawn->GetWorld()->GetTimeSeconds() + 0.6;
					EveryFrame(Pawn, [Pawn, Up, Then]()
					{
						const bool bDone = Pawn->GetWorld()->GetTimeSeconds() > Up;
						Pawn->ProbeThrust(bDone ? FVector::ZeroVector : FVector(1.0f, 0.0f, 0.0f), false);
						if (bDone)
						{
							Then(Pawn->IsClimbingLadder());
						}
						return bDone;
					});
					return true;
				}
				Pawn->ProbeThrust(FVector(1.0f, 0.0f, 0.0f), false);
				return false;
			});
		});
	}

	void Finish(const TSharedRef<FStand>& Stand)
	{
		UE_LOG(LogMindKore, Log, TEXT("Hands probe: %d checks, %d failed - %s"), Stand->Checks, Stand->Failed, Verdict(Stand->Failed == 0));
		UE_LOG(LogMindKore, Log, TEXT("Hands probe: done"));
	}

	void RunHandsProbe(UWorld* World)
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		// The cabin: the biggest box gravity zone (as os.Move.Probe); zero-G: 2 m past the lowest X of them all, facing away from the ship.
		const AOSGravityZone* Cabin = nullptr;
		double MinX = TNumericLimits<double>::Max();
		for (TActorIterator<AOSGravityZone> It(World); It; ++It)
		{
			if (It->SphereRadiusCm > 0.0f)
			{
				continue;
			}
			const FVector Extent = It->Zone->GetScaledBoxExtent();
			MinX = FMath::Min(MinX, It->GetActorLocation().X - Extent.X);
			if (!Cabin || Extent.X * Extent.Y * Extent.Z > Cabin->Zone->GetScaledBoxExtent().X * Cabin->Zone->GetScaledBoxExtent().Y * Cabin->Zone->GetScaledBoxExtent().Z)
			{
				Cabin = *It;
			}
		}
		const TSharedRef<FStand> Stand = MakeShared<FStand>();
		Stand->Pawn = Pawn;
		for (TActorIterator<AOSLadder> It(World); It; ++It)
		{
			Stand->Ladder = *It;
			break;
		}
		for (TActorIterator<AActor> It(World); It && Pawn; ++It)
		{
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (Root && Root != Pawn->Body && Root->IsSimulatingPhysics() && Root->Bounds.SphereRadius < 60.0f && Root->GetMass() < Pawn->MaxGrabMassKg)
			{
				Stand->Loose = Root;
				break;
			}
		}
		FHitResult Up;
		FHitResult Down;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSHandsProbe), false, Pawn);
		const FVector Centre = Cabin ? Cabin->GetActorLocation() : FVector::ZeroVector;
		if (!Pawn || !Cabin || !Stand->Loose || !Stand->Ladder
			|| !World->LineTraceSingleByChannel(Up, Centre, Centre + FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params)
			|| !World->LineTraceSingleByChannel(Down, Centre, Centre - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hands probe: needs Lvl_OS_Ship (an AOSZeroGPawn, the cabin's gravity zone with a floor and a ceiling, a small loose body, a ladder) - FAIL"));
			return;
		}
		Stand->Floor = Down.GetComponent();
		Stand->FloorPoint = Down.ImpactPoint;
		Stand->Ceiling = Up.GetComponent();
		Stand->CeilingPoint = Up.ImpactPoint;
		Stand->WalkSpot = FVector(Centre.X, Centre.Y, Down.ImpactPoint.Z + 45.0);
		Stand->FloatSpot = FVector(MinX - 200.0, Centre.Y, Centre.Z);
		const FString Strap = FString::Printf(TEXT("IA_ZG_Slot%d"), Pawn->GetToolSlot(EOSHullTool::Strap) + 1);
		const FString Welder = FString::Printf(TEXT("IA_ZG_Slot%d"), Pawn->GetToolSlot(EOSHullTool::Welder) + 1);
		Stand->Keys = {
			// A press (one frame): held, the pistol fires again (automatic).
			{ TEXT("LMB"), TEXT("IA_ZG_Primary"), FInputActionValue(true), 0.0f },
			{ TEXT("RMB"), TEXT("IA_ZG_Secondary"), FInputActionValue(true), 0.4f },
			{ TEXT("MMB"), TEXT("IA_ZG_Middle"), FInputActionValue(true), 0.4f },
			{ TEXT("wheel"), TEXT("IA_ZG_Wheel"), FInputActionValue(1.0f), 0.0f },
			{ TEXT("R"), TEXT("IA_ZG_Reload"), FInputActionValue(true), 0.0f },
			{ TEXT("1"), TEXT("IA_ZG_Slot1"), FInputActionValue(true), 0.0f },
			{ TEXT("2"), TEXT("IA_ZG_Slot2"), FInputActionValue(true), 0.0f },
			{ TEXT("strap key"), Strap, FInputActionValue(true), 0.0f },
			{ TEXT("welder key"), Welder, FInputActionValue(true), 0.0f },
		};
		UE_LOG(LogMindKore, Log, TEXT("Hands probe: cabin %s, loose body %s (%.0f kg, %.0f cm), ladder %s; keys: guns 1..%d, the strap %s, the welder %s"),
			*Cabin->GetName(), *GetNameSafe(Stand->Loose->GetOwner()), Stand->Loose->GetMass(), Stand->Loose->Bounds.SphereRadius, *Stand->Ladder->GetName(),
			Pawn->Guns->Weapons.Num(), *Strap, *Welder);

		// Walk: standing in the cabin, looking along it.
		Stand->Mode = EOSMoveMode::Walk;
		Stand->Spot = Stand->WalkSpot;
		Stand->View = FRotator::ZeroRotator;
		const float Arm = Pawn->ArmLengthCm;
		Reset(Stand, EOSHands::Gun);
		After(Pawn, 1.5f, [Stand, Pawn, Arm]()
		{
			CheckReloadCancel(Stand, [Stand, Pawn, Arm]()
			{
				CheckToolHasNoGun(Stand, [Stand, Pawn, Arm]()
				{
					RunKeys(Stand, 0, [Stand, Pawn, Arm]()
					{
						// Hang: in the cabin, the hand on the ceiling, the arm's rest at that reach (as os.Move.Probe), the view along the cabin.
						Stand->Mode = EOSMoveMode::Hang;
						Reset(Stand, EOSHands::Gun);
						Pawn->ArmLengthCm = Pawn->GetArmLengthCm();
						Stand->bReplace = false;
						RunKeys(Stand, 0, [Stand, Pawn, Arm]()
						{
							Pawn->ArmLengthCm = Arm;
							Pawn->LetGoCling();
							// Ladder: on the rungs, turned away from them (the space behind is free for the loose body).
							MountLadder(Stand, [Stand, Pawn](bool bOn)
							{
								Stand->Mode = EOSMoveMode::Ladder;
								Stand->View = FRotator(0.0f, Pawn->GetControlRotation().Yaw + 180.0f, 0.0f);
								UE_LOG(LogMindKore, Log, TEXT("Hands probe [ladder]: %s - %s"), bOn ? TEXT("on the rungs") : TEXT("NOT on the ladder"), Verdict(bOn));
								RunKeys(Stand, 0, [Stand, Pawn]()
								{
									// Float: outside the ship, looking away from it.
									Stand->Mode = EOSMoveMode::Float;
									Stand->Spot = Stand->FloatSpot;
									Stand->View = FRotator(0.0f, 180.0f, 0.0f);
									Stand->bReplace = true;
									Reset(Stand, EOSHands::Gun);
									After(Pawn, 0.5f, [Stand]() { RunKeys(Stand, 0, [Stand]() { Finish(Stand); }); });
								});
							});
						});
					});
				});
			});
		});
	}

	FAutoConsoleCommandWithWorld GHandsProbe(
		TEXT("os.Hands.Probe"),
		TEXT("Lvl_OS_Ship: a hull tool taken mid-reload abandons it (no magazine, no rounds); a tool in hand has no gun (GetWeapon, the HUD's RMB line); then every hands key (LMB, RMB, MMB, wheel, R, 1, 2, the tool keys) injected through its input action with each thing in hand (gun, strap, a held body, nothing) in Walk, Hang, Ladder and Float, against the README controls table (PASS/FAIL each)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunHandsProbe));
}
