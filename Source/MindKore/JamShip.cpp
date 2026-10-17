#include "JamShip.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "JamDockZone.h"
#include "JamGravity.h"
#include "JamOxygen.h"
#include "JamPickups.h"
#include "JamShipParts.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "MKDoor.h"
#include "MKKeyring.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "JamShip"

// --- controller ----------------------------------------------------------------------

void AJamShipController::UpdateRotation(float DeltaTime)
{
	// The pawn's gravity; outside every zone the frame keeps the last one.
	AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(GetPawn());
	const bool bInGravity = Floater && Floater->IsInGravity();
	const auto RotateBody = [Floater](const FQuat& Rotation)
	{
		if (!Floater) return;
		// Eye height is a world-space offset, eased by the pawn. Rotating the
		// parent must not swing that offset before the pawn's next tick.
		const FVector EyeLocation = Floater->Camera->GetComponentLocation();
		Floater->Body->SetWorldRotation(Rotation, false, nullptr, ETeleportType::TeleportPhysics);
		Floater->Camera->SetWorldLocation(EyeLocation);
	};
	if (Floater && !bInGravity && Floater->bFreeBodyRotation)
	{
		// Compose in body space: after a flip, yaw still turns around the
		// player's own up axis. Euler pitch/yaw accumulation cannot do this.
		const FQuat View = (GetControlRotation().Quaternion() * RotationInput.Quaternion()).GetNormalized();
		LastWrittenRotation = View.Rotator();
		SetControlRotation(LastWrittenRotation);
		// Teleport only orientation; preserve linear momentum. Physics rotation
		// stays locked so collisions and the tether cannot spin the player.
		RotateBody(View);
		bWasFreeBodyRotation = true;
		return;
	}
	if (bWasFreeBodyRotation)
	{
		// Rebase the walking frame on the actual exit orientation. Preserve
		// the entire pose (including roll), then let gravity align the frame
		// with the same easing used when walking around the planetoid.
		const FQuat ExitView = GetControlRotation().Quaternion();
		const FRotator Local = (GravityFrame.Inverse() * ExitView).Rotator();
		RelativeView = FRotator(FMath::Clamp(Local.Pitch, -89.0f, 89.0f), Local.Yaw, 0.0f);
		GravityFrame = (ExitView * RelativeView.Quaternion().Inverse()).GetNormalized();
		SmoothedGravity = -GravityFrame.GetUpVector();
		LastWrittenRotation = GetControlRotation();
		bWasFreeBodyRotation = false;
	}
	if (SmoothedGravity.IsNearlyZero())
	{
		SmoothedGravity = bInGravity ? Floater->GetGravityDirection() : FVector::DownVector;
		GravityFrame = FQuat::FindBetweenNormals(FVector::DownVector, SmoothedGravity);
	}
	else if (bInGravity)
	{
		// The view's "down" eases toward the pawn's gravity instead of snapping:
		// landing on the planetoid turns the world under you over ~0.75 s.
		const FVector TargetGravity = Floater->GetGravityDirection();
		const float Alpha = 1.0f - FMath::Exp(-GravityAlignRate * DeltaTime);
		const FQuat Turn = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(SmoothedGravity, TargetGravity), Alpha);
		const FVector Next = Turn.RotateVector(SmoothedGravity).GetSafeNormal(UE_SMALL_NUMBER, TargetGravity);
		// The view's frame is carried along with gravity (parallel transport),
		// all in quaternions: walking around a sphere keeps the view's pitch and
		// yaw relative to the local up, with no gimbal at the "equator".
		GravityFrame = FQuat::FindBetweenNormals(SmoothedGravity, Next) * GravityFrame;
		GravityFrame.Normalize();
		SmoothedGravity = Next;
	}

	// Someone else set the view (possession, a teleport, a probe): take it as the new one.
	const FRotator Control = GetControlRotation();
	if (!Control.Equals(LastWrittenRotation, 0.01f))
	{
		const FRotator Local = (GravityFrame.Inverse() * Control.Quaternion()).Rotator();
		RelativeView = FRotator(Local.Pitch, Local.Yaw, 0.0f);
	}

	bWasFreeBodyRotation = false;
	FRotator View = RelativeView;
	FRotator DeltaRotation(RotationInput);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ProcessViewRotation(DeltaTime, View, DeltaRotation);
	}
	else
	{
		View += DeltaRotation;
		View.Pitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch), -89.0f, 89.0f);
	}
	View.Roll = 0.0f;
	RelativeView = View;
	LastWrittenRotation = (GravityFrame * View.Quaternion()).Rotator();
	SetControlRotation(LastWrittenRotation);
	if (Floater)
	{
		// Walking keeps the body upright in the gravity frame; only the head pitches.
		RotateBody(GravityFrame * FRotator(0.0f, View.Yaw, 0.0f).Quaternion());
	}
	if (APawn* const ViewPawn = GetPawnOrSpectator())
	{
		ViewPawn->FaceRotation(LastWrittenRotation, DeltaTime);
	}
}

// --- gravity zone ----------------------------------------------------------------------

AJamGravityZone::AJamGravityZone()
{
	Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
	SetRootComponent(Zone);
	Zone->SetBoxExtent(FVector(400.0f, 250.0f, 150.0f));
	Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Zone->SetCanEverAffectNavigation(false);
}

bool AJamGravityZone::Contains(const FVector& Location, float InsetCm) const
{
	if (SphereRadiusCm > 0.0f)
	{
		return FVector::Dist(Location, GetActorLocation()) <= SphereRadiusCm - InsetCm;
	}
	const FVector Local = Zone->GetComponentTransform().InverseTransformPositionNoScale(Location);
	const FVector Extent = Zone->GetScaledBoxExtent() - FVector(InsetCm);
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

FVector AJamGravityZone::GravityAt(const FVector& Location) const
{
	return bRadial ? (GetActorLocation() - Location).GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector) : FVector::DownVector;
}

// --- game mode ---------------------------------------------------------------------------

AJamShipGameMode::AJamShipGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	// The pawn is the zero-G mode's (AJamZeroGPawn): it walks in the zones.
	HUDClass = AJamShipHUD::StaticClass();
	PlayerControllerClass = AJamShipController::StaticClass();
}

void AJamShipGameMode::StartPlay()
{
	Super::StartPlay();
	// After every actor's BeginPlay (which unlocks the guns by default): the ship starts unarmed.
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const APawn* Pawn = It->Get() ? It->Get()->GetPawn() : nullptr;
		if (UJamWeaponsComponent* Guns = Pawn ? Pawn->FindComponentByClass<UJamWeaponsComponent>() : nullptr)
		{
			for (FJamWeapon& Weapon : Guns->Weapons)
			{
				Weapon.bUnlocked = false;
			}
		}
	}
}

void AJamShipGameMode::Toast(const FText& Text)
{
	ToastText = Text;
	ToastUntil = GetWorld()->GetTimeSeconds() + ToastSeconds;
}

FText AJamShipGameMode::GetToast() const
{
	return GetWorld()->GetTimeSeconds() < ToastUntil ? ToastText : FText::GetEmpty();
}

const AJamGravityZone* AJamShipGameMode::ZoneAt(const FVector& Location, float InsetCm, bool bWalkOnly) const
{
	for (TActorIterator<AJamGravityZone> It(GetWorld()); It; ++It)
	{
		if ((It->bWalk || !bWalkOnly) && It->Contains(Location, InsetCm))
		{
			return *It;
		}
	}
	return nullptr;
}

const AJamGravityZone* AJamShipGameMode::ZoneOf(const APawn* Pawn) const
{
	return Pawn ? ZoneAt(Pawn->GetActorLocation(), 0.0f, /*bWalkOnly=*/false) : nullptr;
}

void AJamShipGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn || bEnded)
	{
		return;
	}
	const FVector Location = Pawn->GetActorLocation();
	// Air: any breathable zone (the planetoid's air shell is wider than its walking shell).
	const AJamGravityZone* Air = ZoneAt(Location, 0.0f, /*bWalkOnly=*/false);
	if (UJamOxygenComponent* Oxygen = Pawn->FindComponentByClass<UJamOxygenComponent>())
	{
		Oxygen->SetBreathable(Air && Air->bBreathable, Air ? Air->RefillPerSecond : 0.0f);
	}
	// Gravity: the walking zone's, or none (zero-G).
	if (AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn))
	{
		const AJamGravityZone* Zone = ZoneAt(Location, 0.0f);
		Floater->SetGravity(Zone ? Zone->GravityAt(Location) : FVector::ZeroVector, Zone ? Zone->GravityCm : 0.0f, Zone && Zone->bRadial);
	}
}

void AJamShipGameMode::EndGame()
{
	if (bEnded)
	{
		return;
	}
	bEnded = true;
	// TODO (boss): the planetoid wakes here and grabs the ship; for now the run simply ends.
	OnLaunchAttempt.Broadcast();
	UE_LOG(LogMindKore, Log, TEXT("Ship: launch - Улетел, cargo %d/%d; restarting in %.0f s"), Cargo, CargoTotal, EndRestartSeconds);
	Toast(FText::Format(LOCTEXT("Flew", "Улетел! Груз {0}/{1}"), Cargo, CargoTotal));
	ToastUntil = GetWorld()->GetTimeSeconds() + EndRestartSeconds + 1.0;
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		FTimerHandle Restart;
		GetWorldTimerManager().SetTimer(Restart, FTimerDelegate::CreateWeakLambda(PlayerController, [PlayerController]()
		{
			PlayerController->RestartLevel();
		}), FMath::Max(EndRestartSeconds, 0.01f), false);
	}
}

// --- HUD ------------------------------------------------------------------------------------

void AJamShipHUD::DrawHUD()
{
	Super::DrawHUD(); // the zero-G HUD: reticle, action, O2, HP, speed, the gun
	if (!Canvas)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const APawn* Pawn = GetOwningPawn();
	const AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>();

	// The three lamps, mirrored; cargo; the air here.
	float Y = 24.0f;
	for (TActorIterator<AJamShipPanel> Panel(GetWorld()); Panel; ++Panel)
	{
		const FLinearColor Red(1.0f, 0.35f, 0.3f);
		const FLinearColor Green(0.4f, 1.0f, 0.5f);
		DrawText(FString::Printf(TEXT("Корпус: %d/%d"), Panel->HullDone(), Panel->HullSockets.Num()), Panel->IsHullDone() ? Green : Red, 24.0f, Y, Font);
		DrawText(FString::Printf(TEXT("Топливо: %d/%d"), Panel->FuelDone(), Panel->FuelNeeded()), Panel->IsFuelDone() ? Green : Red, 24.0f, Y + 20.0f, Font);
		DrawText(FString::Printf(TEXT("Навигация: %d/%d"), Panel->NavDone(), Panel->NavNeeded()), Panel->IsNavDone() ? Green : Red, 24.0f, Y + 40.0f, Font);
		Y += 60.0f;
		break;
	}
	if (Mode)
	{
		DrawText(FString::Printf(TEXT("Груз %d/%d"), Mode->Cargo, Mode->CargoTotal), FLinearColor(1.0f, 0.85f, 0.4f), 24.0f, Y, Font);
		if (const AJamGravityZone* Zone = Mode->ZoneOf(Pawn); Zone && !Zone->HudText.IsEmpty())
		{
			DrawText(Zone->HudText.ToString(), FLinearColor(1.0f, 0.75f, 0.55f), 24.0f, Y + 20.0f, Font);
		}
		const FText Toast = Mode->GetToast();
		if (!Toast.IsEmpty())
		{
			float Width = 0.0f;
			float Height = 0.0f;
			const float Scale = Mode->bEnded ? 2.0f : 1.2f;
			GetTextSize(Toast.ToString(), Width, Height, Font, Scale);
			DrawText(Toast.ToString(), FLinearColor::White, (Canvas->ClipX - Width) * 0.5f, Canvas->ClipY * 0.25f, Font, Scale);
		}
	}
}

// --- console probes ----------------------------------------------------------------------------

namespace
{
	/** Run Step after Seconds of game time (the probe's chain of steps). */
	void After(AJamShipGameMode* Mode, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Mode->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Mode, MoveTemp(Step)), Seconds, false);
	}

	AJamZeroGPawn* PlayerPawn(const UWorld* World)
	{
		const APlayerController* PlayerController = World->GetFirstPlayerController();
		return PlayerController ? Cast<AJamZeroGPawn>(PlayerController->GetPawn()) : nullptr;
	}

	/** The pawn's view in its gravity space (world down in zero-G): roll should stay 0. */
	FRotator GravityView(const AJamZeroGPawn* Pawn)
	{
		const FVector Gravity = Pawn->IsInGravity() ? Pawn->GetGravityDirection() : FVector::DownVector;
		return JamGravityRelativeRotation(Pawn->GetControlRotation(), Gravity);
	}

	void LogState(const TCHAR* Stage, const AJamZeroGPawn* Pawn)
	{
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe [%s]: no AJamZeroGPawn"), Stage);
			return;
		}
		const FVector Eye = Pawn->Camera->GetComponentLocation() - Pawn->Body->GetComponentLocation();
		const FVector Up = Pawn->IsInGravity() ? -Pawn->GetGravityDirection() : FVector::UpVector;
		const AJamShipController* Controller = Cast<AJamShipController>(Pawn->GetController());
		const FRotator View = GravityView(Pawn);
		UE_LOG(LogMindKore, Log, TEXT("Ship probe [%s]: %s at %s, %s, speed %.0f cm/s, eye %.0f cm over the body (. up %.3f), view gravity . gravity %.3f, view pitch %.1f roll %.1f in gravity space, suit %s, guns %s, O2 %.0f, HP %.0f"),
			Stage, *Pawn->GetName(), *Pawn->GetActorLocation().ToCompactString(),
			Pawn->IsInGravity() ? (Pawn->IsGrounded() ? TEXT("walking, on the ground") : TEXT("walking, in the air")) : TEXT("zero-G"),
			Pawn->GetBodyVelocity().Size(), Eye.Size(), FVector::DotProduct(Eye.GetSafeNormal(), Up),
			Controller && Pawn->IsInGravity() ? FVector::DotProduct(Controller->GetViewGravity(), Pawn->GetGravityDirection()) : -2.0f,
			View.Pitch, View.Roll, MKHolderHasKey(Pawn, MKJamSuitKey()) ? TEXT("yes") : TEXT("no"), *Pawn->Guns->Describe(),
			Pawn->Oxygen->Seconds, Pawn->Vitals->Health);
	}

	void Teleport(AActor* Actor, const FVector& Location)
	{
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
		{
			Root->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			return;
		}
		Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}

	TArray<AActor*> Tagged(UWorld* World, FName Tag)
	{
		TArray<AActor*> Found;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				Found.Add(*It);
			}
		}
		return Found;
	}

	/** Hold scripted walking/thrust input (view frame: X forward) every frame for Seconds. */
	void HoldInput(AJamShipGameMode* Mode, const FVector& Input, float Seconds)
	{
		const double End = Mode->GetWorld()->GetTimeSeconds() + Seconds;
		const TSharedRef<FTimerHandle> Hold = MakeShared<FTimerHandle>();
		Mode->GetWorldTimerManager().SetTimer(*Hold, FTimerDelegate::CreateWeakLambda(Mode, [Mode, Hold, End, Input]()
		{
			if (Mode->GetWorld()->GetTimeSeconds() > End)
			{
				Mode->GetWorldTimerManager().ClearTimer(*Hold);
				return;
			}
			if (AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld()))
			{
				Pawn->ProbeThrust(Input, false);
			}
		}), 0.0005f, true);
	}

	void LogPanel(UWorld* World, const TCHAR* Stage)
	{
		for (TActorIterator<AJamShipPanel> Panel(World); Panel; ++Panel)
		{
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [%s]: hull %d/%d %s, fuel %d/%d %s, navigation %d/%d %s"), Stage,
				Panel->HullDone(), Panel->HullSockets.Num(), Panel->IsHullDone() ? TEXT("green") : TEXT("red"),
				Panel->FuelDone(), Panel->FuelNeeded(), Panel->IsFuelDone() ? TEXT("green") : TEXT("red"),
				Panel->NavDone(), Panel->NavNeeded(), Panel->IsNavDone() ? TEXT("green") : TEXT("red"));
		}
	}

	/** The walking shell of the planetoid (the radial walking zone), or null. */
	const AJamGravityZone* PlanetShell(UWorld* World)
	{
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			if (It->bRadial && It->bWalk && It->SphereRadiusCm > 0.0f)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** (c) Every part into place, then the lever. */
	void ProbeFinish(AJamShipGameMode* Mode)
	{
		UWorld* World = Mode->GetWorld();
		AJamShipPanel* Panel = nullptr;
		for (TActorIterator<AJamShipPanel> It(World); It; ++It)
		{
			Panel = *It;
		}
		if (!Panel)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: no panel"));
			return;
		}
		TArray<AActor*> Plates = Tagged(World, TEXT("Hull.Plate"));
		int32 Next = 0;
		for (const AJamPlateSocket* Socket : Panel->HullSockets)
		{
			if (Socket && !Socket->bFilled && Plates.IsValidIndex(Next))
			{
				Teleport(Plates[Next++], Socket->GetActorLocation());
			}
		}
		TArray<AActor*> Fuel = Tagged(World, TEXT("Ship.Fuel"));
		for (int32 Index = 0; Panel->FuelDock && Index < Fuel.Num(); ++Index)
		{
			Teleport(Fuel[Index], Panel->FuelDock->GetActorLocation() + FVector(0.0f, (Index - 0.5f) * 200.0f, 0.0f));
		}
		for (AActor* Nav : Tagged(World, TEXT("Ship.Nav")))
		{
			if (Panel->NavDock)
			{
				Teleport(Nav, Panel->NavDock->GetActorLocation());
			}
		}
		After(Mode, 2.0f, [Mode, Panel]()
		{
			LogPanel(Mode->GetWorld(), TEXT("all parts in"));
			Panel->CompleteInteraction(PlayerPawn(Mode->GetWorld()));
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [end]: ended %s"), Mode->bEnded ? TEXT("yes") : TEXT("no"));
		});
	}

	/** (b) A plate at a socket with junk in the hull, then with the hull cleaned. */
	void ProbePlate(AJamShipGameMode* Mode)
	{
		UWorld* World = Mode->GetWorld();
		AJamPlateSocket* Socket = nullptr;
		for (TActorIterator<AJamPlateSocket> It(World); It; ++It)
		{
			Socket = *It;
			break;
		}
		TArray<AActor*> Plates = Tagged(World, TEXT("Hull.Plate"));
		if (!Socket || Plates.IsEmpty())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: no socket or no plate"));
			return;
		}
		AActor* Plate = Plates[0];
		UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: %s to %s with junk in the hull: %d"), *Plate->GetName(), *Socket->GetName(),
			Socket->CleanZone ? Socket->CleanZone->GetCount() : -1);
		Teleport(Plate, Socket->GetActorLocation());
		After(Mode, 0.5f, [Mode, Socket, Plate]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: with junk - fitted %s"), Socket->bFilled ? TEXT("yes") : TEXT("no"));
			// Clear the hull: every foreign thing 150 m away.
			int32 Moved = 0;
			for (AActor* Junk : Tagged(Mode->GetWorld(), TEXT("Junk.Foreign")))
			{
				Teleport(Junk, FVector(0.0f, 0.0f, 15000.0f) + FVector(Moved++ * 300.0f, 0.0f, 0.0f));
			}
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: %d junk props moved out of the hull"), Moved);
			After(Mode, 0.5f, [Mode, Socket, Plate]()
			{
				Teleport(Plate, Socket->GetActorLocation());
				After(Mode, 0.5f, [Mode, Socket]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: clean hull (%d) - fitted %s"),
						Socket->CleanZone ? Socket->CleanZone->GetCount() : -1, Socket->bFilled ? TEXT("yes") : TEXT("no"));
					ProbeFinish(Mode);
				});
			});
		});
	}

	/** (a3) Out of the airlock into zero-G, the pistol, then down onto the planetoid. */
	void ProbeOutAndPlanet(AJamShipGameMode* Mode, const FVector& Outside)
	{
		Teleport(PlayerPawn(Mode->GetWorld()), Outside);
		After(Mode, 0.5f, [Mode]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("out of the airlock"), Pawn);
			for (TActorIterator<AJamWeaponPickup> Pickup(Mode->GetWorld()); Pickup; ++Pickup)
			{
				if (Pickup->WeaponIndex == 0)
				{
					Pickup->CompleteInteraction(Pawn);
				}
			}
			Pawn->Fire();
			LogState(TEXT("pistol taken, one shot"), Pawn);
			const AJamGravityZone* Shell = PlanetShell(Mode->GetWorld());
			if (!Shell)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Ship probe: no planetoid shell"));
				ProbePlate(Mode);
				return;
			}
			// 5 m over the top of the surface: falls, lands, the view turns to the local up.
			Teleport(Pawn, Shell->GetActorLocation() + FVector::UpVector * (Shell->SphereRadiusCm - 300.0f));
			After(Mode, 3.0f, [Mode]()
			{
				LogState(TEXT("on the planetoid"), PlayerPawn(Mode->GetWorld()));
				ProbePlate(Mode);
			});
		});
	}

	/**
	 * jam.Ship.Probe: (a) standing in the cabin, walking, stopping and a jump,
	 * the suit, both airlock doors, out into zero-G, the pistol, landing on the
	 * planetoid; (b) a plate refused and fitted; (c) every part in and the lever.
	 */
	void RunShipProbe(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		// The box zones (cabin, airlock): the cabin is the biggest; outside = 2 m
		// past the lowest X of them all (beyond the outer door).
		const AJamGravityZone* Cabin = nullptr;
		double MinX = TNumericLimits<double>::Max();
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
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
		if (!Mode || !Pawn || !Cabin)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: needs AJamShipGameMode, an AJamZeroGPawn and a box gravity zone"));
			return;
		}
		LogPanel(World, TEXT("start"));
		const FVector Outside(MinX - 200.0, Cabin->GetActorLocation().Y, Cabin->GetActorLocation().Z);
		// The back of the cabin, facing the panel (+X): room for a short walk.
		Teleport(Pawn, FVector(Cabin->GetActorLocation().X - Cabin->Zone->GetScaledBoxExtent().X + 60.0, 0.0, Cabin->GetActorLocation().Z));
		World->GetFirstPlayerController()->SetControlRotation(FRotator::ZeroRotator);
		After(Mode, 2.0f, [Mode, Outside]()
		{
			LogState(TEXT("standing, 2 s"), PlayerPawn(Mode->GetWorld()));
			HoldInput(Mode, FVector(1.0f, 0.0f, 0.0f), 0.8f);
			After(Mode, 0.4f, [Mode]() { LogState(TEXT("walking +0.4 s"), PlayerPawn(Mode->GetWorld())); });
			After(Mode, 0.8f, [Mode]() { LogState(TEXT("walking +0.8 s"), PlayerPawn(Mode->GetWorld())); });
			After(Mode, 1.4f, [Mode, Outside]()
			{
				AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
				LogState(TEXT("released +0.6 s"), Pawn);
				Pawn->JumpPressed();
				After(Mode, 0.15f, [Mode]() { LogState(TEXT("jump +0.15 s"), PlayerPawn(Mode->GetWorld())); });
				After(Mode, 1.5f, [Mode, Outside]()
				{
					AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
					LogState(TEXT("jump +1.5 s"), Pawn);
					for (TActorIterator<AJamSuitLocker> Locker(Mode->GetWorld()); Locker; ++Locker)
					{
						Locker->CompleteInteraction(Pawn);
					}
					// The doors by X: the inner one (cabin side) has the larger X.
					TArray<AMKDoor*> Doors;
					for (TActorIterator<AMKDoor> Door(Mode->GetWorld()); Door; ++Door)
					{
						Doors.Add(*Door);
					}
					Doors.Sort([](const AMKDoor& A, const AMKDoor& B) { return A.GetActorLocation().X > B.GetActorLocation().X; });
					for (AMKDoor* Door : Doors)
					{
						// A key lock takes one E to turn the key and another to open.
						const bool bKeyLock = Door->IsKeyLockEngaged();
						Door->CompleteInteraction(Pawn);
						if (bKeyLock)
						{
							Door->CompleteInteraction(Pawn);
						}
					}
					LogState(TEXT("suit taken"), Pawn);
					After(Mode, 1.5f, [Mode, Doors, Outside]()
					{
						for (const AMKDoor* Door : Doors)
						{
							UE_LOG(LogMindKore, Log, TEXT("Ship probe [airlock]: %s at x %.0f passable %s, key lock %s"), *Door->GetName(),
								Door->GetActorLocation().X, Door->IsPassable() ? TEXT("yes") : TEXT("no"), Door->IsKeyLockEngaged() ? TEXT("engaged") : TEXT("turned"));
						}
						ProbeOutAndPlanet(Mode, Outside);
					});
				});
			});
		});
	}

	void LogWhere(UWorld* World, FName Tag)
	{
		for (AActor* Actor : Tagged(World, Tag))
		{
			UE_LOG(LogMindKore, Log, TEXT("Where %s: %s at %s, %.0f cm/s"), *Tag.ToString(), *Actor->GetName(),
				*Actor->GetActorLocation().ToCompactString(), Actor->GetVelocity().Size());
		}
	}

	/** jam.Ship.Where [Tag] [Seconds]: where every actor with that tag is and how fast it moves; again after Seconds. */
	void RunShipWhere(const TArray<FString>& Args, UWorld* World)
	{
		const FName Tag(Args.Num() > 0 ? *Args[0] : TEXT("Junk.Foreign"));
		LogWhere(World, Tag);
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		if (Args.Num() > 1 && Mode)
		{
			After(Mode, FCString::Atof(*Args[1]), [World, Tag]() { LogWhere(World, Tag); });
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GShipWhere(
		TEXT("jam.Ship.Where"),
		TEXT("Logs location and speed of every actor with the given tag (default Junk.Foreign)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShipWhere));

	/**
	 * jam.Ship.Shots: rendered stills for a look check (needs a real RHI, not
	 * -nullrhi). Teleports the player (BugItGo) to each viewpoint, waits for the
	 * frame to settle, takes a HighResShot; the last one looks at the strongest
	 * directional light's star. Files land in Saved/Screenshots in this order.
	 */
	void RunShipShots(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Mode || !PlayerController)
		{
			return;
		}
		FRotator ToStar(-20.0f, 0.0f, 0.0f);
		float Brightest = -1.0f;
		for (TActorIterator<ADirectionalLight> Light(World); Light; ++Light)
		{
			const float Intensity = Light->GetLightComponent() ? Light->GetLightComponent()->Intensity : 0.0f;
			if (Intensity > Brightest)
			{
				Brightest = Intensity;
				ToStar = (-Light->GetActorForwardVector()).Rotation();
			}
		}
		struct FView { const TCHAR* Name; FVector Where; FRotator Look; };
		const TArray<FView> Views = {
			{ TEXT("cabin_panel"), FVector(1700.0f, 0.0f, -40.0f), FRotator(-8.0f, 0.0f, 0.0f) },
			{ TEXT("cabin_back"), FVector(1900.0f, 100.0f, -40.0f), FRotator(-5.0f, 190.0f, 0.0f) },
			{ TEXT("ship_outside"), FVector(600.0f, 1800.0f, 400.0f), FRotator(-10.0f, -100.0f, 0.0f) },
			{ TEXT("to_planetoid"), FVector(0.0f, 3500.0f, 300.0f), FRotator(-5.0f, 90.0f, 0.0f) },
			{ TEXT("to_star"), FVector(-1500.0f, -1500.0f, 1500.0f), ToStar },
		};
		for (int32 Index = 0; Index < Views.Num(); ++Index)
		{
			const FView View = Views[Index];
			After(Mode, 1.0f + Index * 4.0f, [PlayerController, View]()
			{
				PlayerController->ConsoleCommand(FString::Printf(TEXT("BugItGo %f %f %f %f %f 0"), View.Where.X, View.Where.Y, View.Where.Z,
					View.Look.Pitch, View.Look.Yaw));
			});
			After(Mode, 3.5f + Index * 4.0f, [PlayerController, View, Index]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Ship shots: %d %s at %s looking %s"), Index, View.Name, *View.Where.ToCompactString(), *View.Look.ToCompactString());
				LogState(View.Name, Cast<AJamZeroGPawn>(PlayerController->GetPawn()));
				PlayerController->ConsoleCommand(TEXT("HighResShot 1"));
			});
		}
	}

	FAutoConsoleCommandWithWorld GShipShots(
		TEXT("jam.Ship.Shots"),
		TEXT("Rendered stills of Lvl_Jam_Ship from five viewpoints (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipShots));

	/**
	 * jam.Ship.Lap: put the player on top of the planetoid and walk straight
	 * ahead for 35 s (scripted forward input every frame); every 3 s log the
	 * angle travelled around the centre, the camera's offset against the local
	 * up, and the view's roll in gravity space (should stay ~0).
	 */
	void RunShipLap(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		if (!Mode || !Shell || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship lap: needs the ship mode, a planetoid walking shell and the pawn"));
			return;
		}
		const FVector Centre = Shell->GetActorLocation();
		const FVector Start = Centre + FVector::UpVector * (Shell->SphereRadiusCm - 600.0f);
		Teleport(Pawn, Start);
		World->GetFirstPlayerController()->SetControlRotation(FRotator(-10.0f, 0.0f, 0.0f));
		After(Mode, 2.0f, [Mode]() { HoldInput(Mode, FVector(1.0f, 0.0f, 0.0f), 35.0f); });
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			After(Mode, 2.0f + Step * 3.0f, [Mode, Centre, Start, Step]()
			{
				const AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld());
				if (!Now)
				{
					return;
				}
				const FVector Up = (Now->GetActorLocation() - Centre).GetSafeNormal();
				const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Up, (Start - Centre).GetSafeNormal())));
				const FVector Eye = (Now->Camera->GetComponentLocation() - Now->Body->GetComponentLocation()).GetSafeNormal();
				const FRotator Relative = GravityView(Now);
				const AJamShipController* Controller = Cast<AJamShipController>(Now->GetController());
				UE_LOG(LogMindKore, Log, TEXT("Ship lap +%2d s: %.0f deg from the start, height %.0f cm, %s, speed %.0f cm/s, eye offset . local up %.3f, view roll in gravity space %.1f, pitch %.1f, view gravity . gravity %.3f"),
					Step * 3, Angle, FVector::Dist(Now->GetActorLocation(), Centre), Now->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"),
					Now->GetBodyVelocity().Size(), FVector::DotProduct(Eye, Up), Relative.Roll, Relative.Pitch,
					Controller ? FVector::DotProduct(Controller->GetViewGravity(), Now->GetGravityDirection()) : -2.0f);
			});
		}
	}

	FAutoConsoleCommandWithWorld GShipLap(
		TEXT("jam.Ship.Lap"),
		TEXT("Walk the player straight ahead around the planetoid for 35 s, logging angle, up vectors and view roll."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipLap));

	/** jam.Ship.JetExit: on the planetoid, jump and Space again in the air; logs leaving its gravity. */
	void RunJetExit(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		if (!Mode || !Shell || !Pawn)
		{
			return;
		}
		Teleport(Pawn, Shell->GetActorLocation() + FVector::UpVector * (Shell->SphereRadiusCm - 650.0f));
		After(Mode, 2.0f, [Mode]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("jet: before the jump"), Pawn);
			Pawn->JumpPressed();
		});
		After(Mode, 2.4f, [Mode]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("jet: before the second press"), Pawn);
			Pawn->JumpPressed();
		});
		After(Mode, 4.0f, [Mode]() { LogState(TEXT("jet: +1.6 s after"), PlayerPawn(Mode->GetWorld())); });
	}

	FAutoConsoleCommandWithWorld GJetExit(
		TEXT("jam.Ship.JetExit"),
		TEXT("On the planetoid: jump, then Space in the air - the jet leap that leaves its gravity; logs the state."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunJetExit));

	/** jam.Ship.PlanetShots: two stills standing on the planetoid (top and side). */
	void RunPlanetShots(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Mode || !Shell || !PlayerController)
		{
			return;
		}
		const FVector Centre = Shell->GetActorLocation();
		const float Radius = Shell->SphereRadiusCm - 650.0f;
		const FVector Spots[2] = { Centre + FVector::UpVector * Radius, Centre - FVector::YAxisVector * Radius };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FVector Spot = Spots[Index];
			After(Mode, 1.0f + Index * 6.0f, [PlayerController, Spot]()
			{
				Teleport(PlayerController->GetPawn(), Spot);
				PlayerController->SetControlRotation(FRotator(-5.0f, 0.0f, 0.0f));
			});
			After(Mode, 5.5f + Index * 6.0f, [PlayerController, Index]()
			{
				LogState(Index == 0 ? TEXT("planet shot top") : TEXT("planet shot side"), Cast<AJamZeroGPawn>(PlayerController->GetPawn()));
				PlayerController->ConsoleCommand(TEXT("HighResShot 1"));
			});
		}
	}

	FAutoConsoleCommandWithWorld GPlanetShots(
		TEXT("jam.Ship.PlanetShots"),
		TEXT("Two HighResShot stills standing on the planetoid, top and side (needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunPlanetShots));

	FAutoConsoleCommandWithWorld GShipProbe(
		TEXT("jam.Ship.Probe"),
		TEXT("Ship: standing, walking and jumping in the cabin, the suit and the airlock, zero-G outside, the planetoid; a plate; every part in, the lever."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipProbe));
}

#undef LOCTEXT_NAMESPACE
