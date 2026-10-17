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
#include "JamLivingWall.h"
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
	const AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(GetPawn());
	const bool bInGravity = Floater && Floater->IsInGravity();
	// Only the view turns, never the physics body. Teleporting the sphere's
	// rotation every frame against its rotation lock left it spinning at
	// 10^5..10^6 deg/s, and on touchdown friction turned that spin into a
	// sideways kick (7.5 m/s in jam.Ship.Land); it also turned the grab's
	// body-space hold target away from the view.
	if (Floater && !bInGravity && Floater->bFreeBodyRotation)
	{
		// Compose in body space: after a flip, yaw still turns around the
		// player's own up axis. Euler pitch/yaw accumulation cannot do this.
		const FQuat View = (GetControlRotation().Quaternion() * RotationInput.Quaternion()).GetNormalized();
		LastWrittenRotation = View.Rotator();
		SetControlRotation(LastWrittenRotation);
		bWasFreeBodyRotation = true;
		return;
	}
	if (bWasFreeBodyRotation)
	{
		// Leaving free rotation (landing, or entering the cabin): build the
		// upright frame for the target gravity at once, keeping where the view
		// looks, and fade the leftover roll out as a world-space offset. Easing
		// the gravity itself from the free pose meant turning through up to 180
		// degrees with FindBetweenNormals, whose axis is arbitrary near opposite
		// vectors: an upside-down landing nodded the view 60 degrees in pitch on
		// its way upright (jam.Ship.Land 0 180).
		const FQuat ExitView = GetControlRotation().Quaternion();
		const FVector Gravity = bInGravity ? Floater->GetGravityDirection()
			: (SmoothedGravity.IsNearlyZero() ? FVector::DownVector : SmoothedGravity);
		const FVector Up = -Gravity;
		const FVector Forward = ExitView.GetForwardVector();
		FVector Ahead = FVector::VectorPlaneProject(Forward, Up);
		if (!Ahead.Normalize(0.05f))
		{
			// Looking straight along gravity: the view's own up (or down) gives the heading.
			Ahead = FVector::VectorPlaneProject(FVector::DotProduct(Forward, Up) < 0.0f ? ExitView.GetUpVector() : -ExitView.GetUpVector(), Up).GetSafeNormal();
		}
		GravityFrame = FRotationMatrix::MakeFromXZ(Ahead, Up).ToQuat();
		const float Pitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Forward, Up), -1.0f, 1.0f)));
		RelativeView = FRotator(FMath::Clamp(Pitch, -89.0f, 89.0f), 0.0f, 0.0f);
		SmoothedGravity = Gravity;
		RecoveryOffset = (ExitView * (GravityFrame * RelativeView.Quaternion()).Inverse()).GetNormalized();
		if (RecoveryOffset.W < 0.0f)
		{
			RecoveryOffset = -RecoveryOffset; // the short way round: GetAngle stays within 180 degrees
		}
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
	// The roll left over from free rotation fades at the gravity easing rate,
	// but never faster than RecoveryMaxDegPerSec: a 180-degree flip would
	// otherwise start at 720 deg/s.
	if (const float Angle = RecoveryOffset.GetAngle(); Angle > UE_KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Angle * (1.0f - FMath::Exp(-GravityAlignRate * DeltaTime)),
			FMath::DegreesToRadians(RecoveryMaxDegPerSec) * DeltaTime);
		RecoveryOffset = FQuat::Slerp(RecoveryOffset, FQuat::Identity, Step / Angle).GetNormalized();
	}
	LastWrittenRotation = (RecoveryOffset * GravityFrame * View.Quaternion()).Rotator();
	SetControlRotation(LastWrittenRotation);
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

	/**
	 * jam.Ship.Land [Pitch] [Roll] [Speed]: start outside the planetoid's shell
	 * with a free (rolled) view, fall onto the surface at Speed and log every
	 * frame-ish for 4 s: grounded, radial and tangential speed, the body's
	 * angular velocity and its up against the local up, the view's roll/pitch
	 * in gravity space and how far the view turned since the last sample.
	 */
	void RunShipLand(const TArray<FString>& Args, UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Mode || !Shell || !Pawn || !PlayerController)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship land: needs the ship mode, the planetoid shell and the pawn"));
			return;
		}
		const float Pitch = Args.Num() > 0 ? FCString::Atof(*Args[0]) : -60.0f;
		const float Roll = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 150.0f;
		const float Speed = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 600.0f;
		const FVector Centre = Shell->GetActorLocation();
		const FVector Start = Centre + FVector::UpVector * (Shell->SphereRadiusCm + 300.0f);
		Teleport(Pawn, Start);
		PlayerController->SetControlRotation(FRotator(Pitch, 30.0f, Roll));
		After(Mode, 0.5f, [Mode, Centre, Speed]()
		{
			if (AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld()))
			{
				Now->Body->SetPhysicsLinearVelocity((Centre - Now->GetActorLocation()).GetSafeNormal() * Speed);
			}
		});
		TSharedRef<FQuat> LastView = MakeShared<FQuat>(FQuat::Identity);
		for (int32 Step = 0; Step <= 120; ++Step)
		{
			After(Mode, 0.5f + Step * 0.033f, [Mode, Centre, Step, LastView]()
			{
				const AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld());
				if (!Now)
				{
					return;
				}
				const FVector Up = (Now->GetActorLocation() - Centre).GetSafeNormal();
				const FVector Velocity = Now->GetBodyVelocity();
				const float Radial = FVector::DotProduct(Velocity, Up);
				const FQuat View = Now->GetControlRotation().Quaternion();
				const float Turned = Step == 0 ? 0.0f : FMath::RadiansToDegrees(View.AngularDistance(*LastView));
				*LastView = View;
				const FRotator Relative = GravityView(Now);
				UE_LOG(LogMindKore, Log, TEXT("Ship land %3d: %s %s, height %.0f, radial %.0f, tangential %.0f cm/s, body spin %.1f deg/s, body up . local up %.3f, view pitch %.1f roll %.1f, view turned %.2f deg"),
					Step, Now->IsInGravity() ? TEXT("gravity") : TEXT("zero-G "), Now->IsGrounded() ? TEXT("ground") : TEXT("air   "),
					FVector::Dist(Now->GetActorLocation(), Centre), Radial, (Velocity - Radial * Up).Size(),
					Now->Body->GetPhysicsAngularVelocityInDegrees().Size(), FVector::DotProduct(Now->Body->GetUpVector(), Up),
					Relative.Pitch, Relative.Roll, Turned);
			});
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GShipLand(
		TEXT("jam.Ship.Land"),
		TEXT("jam.Ship.Land [Pitch] [Roll] [Speed]: fall onto the planetoid from a free, rolled view and log the landing every ~33 ms."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShipLand));

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

	/** The first living wall with this tag (crater membranes: Jam.Crater, cave plugs: Jam.CavePlug), or the named one. */
	AJamLivingWall* TaggedWall(UWorld* World, FName Tag, const FString& Name = FString())
	{
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			if (Name.IsEmpty() ? It->ActorHasTag(Tag) : It->GetName() == Name)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Take the shotgun, look at Target through the gravity-framed controller, fire. */
	void ShotgunAt(AJamZeroGPawn* Pawn, const FVector& Target)
	{
		Pawn->Guns->UnlockWeapon(1);
		Pawn->GetController()->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		if (!Pawn->Fire())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Caves probe: %s did not fire"), *Pawn->Guns->Describe());
		}
	}

	/** Signed height of the pawn over a wall's plane along its front normal, cm. */
	float OverPlane(const AJamLivingWall* Wall, const AJamZeroGPawn* Pawn)
	{
		return FVector::DotProduct(Pawn->GetActorLocation() - Wall->GetActorLocation(), Wall->GetFrontNormal());
	}

	/**
	 * (b) A cave plug: the pawn 5 m in front on the tunnel floor walks at it -
	 * the whole plug stops it; then a shotgun blast straight ahead at eye level
	 * and the same walk: through the hole before it closes.
	 */
	void ProbePlug(AJamShipGameMode* Mode, AJamLivingWall* Plug)
	{
		const FVector Front = Plug->GetFrontNormal();
		const FVector Up = Plug->GetActorUpVector();
		const FVector Centre = Plug->GetActorLocation();
		const FVector Start = Centre + Front * 500.0f - Up * 130.0f;
		const auto Walk = [Mode, Plug, Start, Front, Centre](const TCHAR* Label, bool bShoot, TFunction<void()> Next)
		{
			Teleport(PlayerPawn(Mode->GetWorld()), Start);
			const FString Name = Label;
			After(Mode, 1.2f, [Mode, Plug, Front, Centre, Name, bShoot, Next]()
			{
				AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
				// As a player would: straight ahead at eye level.
				const FVector Eye = Pawn->Camera->GetComponentLocation();
				const FVector Aim = Eye - Front * FVector::DotProduct(Eye - Centre, Front);
				LogState(*FString::Printf(TEXT("plug %s: start"), *Name), Pawn);
				if (bShoot)
				{
					ShotgunAt(Pawn, Aim);
				}
				Pawn->GetController()->SetControlRotation((Aim - Pawn->Camera->GetComponentLocation()).Rotation());
				const float HP = Pawn->Vitals->Health;
				After(Mode, 0.15f, [Mode, Plug, Name, HP, Next]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug %s]: open cells %d, clear radius at the hole %.0f cm; walking at it for 3 s"),
						*Name, Plug->GetOpenCellCount(), Plug->ClearRadiusAt(Plug->GetLastWoundCentre()));
					HoldInput(Mode, FVector(1.0f, 0.0f, 0.0f), 3.0f);
					for (int32 Step = 1; Step <= 6; ++Step)
					{
						After(Mode, Step * 0.5f, [Mode, Plug, Name, Step]()
						{
							const AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
							const FVector Local = Plug->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
							UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug %s +%.1f s]: %.0f cm in front, %.0f cm across, %.0f cm over the axis, %s, speed %.0f cm/s"), *Name, Step * 0.5f,
								Local.Y, Local.X, Local.Z, Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"), Pawn->GetBodyVelocity().Size());
						});
					}
					After(Mode, 3.2f, [Mode, Plug, Name, HP, Next]()
					{
						const AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
						const float Over = OverPlane(Plug, Pawn);
						UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug %s]: through %s - the pawn is %.0f cm %s the plug, %s, HP %.0f -> %.0f, open cells %d"),
							*Name, Over < -(Plug->GetHalfDepthCm() + Pawn->Body->GetScaledSphereRadius()) ? TEXT("yes") : TEXT("no"),
							FMath::Abs(Over), Over < 0.0f ? TEXT("behind") : TEXT("in front of"),
							Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"), HP, Pawn->Vitals->Health, Plug->GetOpenCellCount());
						Next();
					});
				});
			});
		};
		UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug]: %s at %s, front %s, up %s"), *Plug->GetName(), *Centre.ToCompactString(),
			*Front.ToCompactString(), *Up.ToCompactString());
		Walk(TEXT("control, no shot"), false, [Mode, Walk]()
		{
			// The weave must be whole again before the real try (the bump may have torn a cell).
			After(Mode, 3.0f, [Walk]() { Walk(TEXT("after a shotgun blast"), true, []() { UE_LOG(LogMindKore, Log, TEXT("Caves probe: done")); }); });
		});
	}

	/**
	 * (a) A crater: the pawn 2.5 m over the membrane in radial gravity, the
	 * shotgun straight down; logs the height over the membrane's plane every
	 * 0.25 s for 5 s: does it drop through the hole and land in the tunnel below?
	 */
	void ProbeCrater(AJamShipGameMode* Mode, AJamLivingWall* Crater, TFunction<void()> Next)
	{
		const FVector Up = Crater->GetFrontNormal();
		const FVector Centre = Crater->GetActorLocation();
		AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
		UE_LOG(LogMindKore, Log, TEXT("Caves probe [crater]: %s at %s, up %s (radial up there %s)"), *Crater->GetName(), *Centre.ToCompactString(),
			*Up.ToCompactString(), *(Centre - PlanetShell(Mode->GetWorld())->GetActorLocation()).GetSafeNormal().ToCompactString());
		Teleport(Pawn, Centre + Up * 250.0f);
		After(Mode, 0.25f, [Mode, Crater, Centre]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("crater: over the membrane"), Pawn);
			ShotgunAt(Pawn, Centre);
		});
		const TSharedRef<float> Lowest = MakeShared<float>(TNumericLimits<float>::Max());
		for (int32 Step = 1; Step <= 20; ++Step)
		{
			After(Mode, 0.25f + Step * 0.25f, [Mode, Crater, Step, Lowest, Next]()
			{
				const AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
				const float Over = OverPlane(Crater, Pawn);
				*Lowest = FMath::Min(*Lowest, Over);
				UE_LOG(LogMindKore, Log, TEXT("Caves probe [crater +%.2f s]: %.0f cm %s the membrane, %s, speed %.0f cm/s, open cells %d"), Step * 0.25f,
					FMath::Abs(Over), Over >= 0.0f ? TEXT("over") : TEXT("under"), Pawn->IsInGravity() ? (Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air")) : TEXT("zero-G"),
					Pawn->GetBodyVelocity().Size(), Crater->GetOpenCellCount());
				if (Step == 20)
				{
					const bool bThrough = *Lowest < -(Crater->GetHalfDepthCm() + Pawn->Body->GetScaledSphereRadius());
					UE_LOG(LogMindKore, Log, TEXT("Caves probe [crater]: dropped through %s - lowest %.0f cm under the membrane, now %s, HP %.0f, membrane open cells %d"),
						bThrough ? TEXT("yes") : TEXT("no"), -*Lowest, Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"), Pawn->Vitals->Health,
						Crater->GetOpenCellCount());
					Next();
				}
			});
		}
	}

	/** jam.Ship.Caves [Crater] [Plug]: a crater membrane opened by the shotgun and the drop through it; a cave plug walked into, then shot and walked through. */
	void RunShipCaves(const TArray<FString>& Args, UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		AJamLivingWall* Crater = TaggedWall(World, TEXT("Jam.Crater"), Args.Num() > 0 ? Args[0] : FString());
		AJamLivingWall* Plug = TaggedWall(World, TEXT("Jam.CavePlug"), Args.Num() > 1 ? Args[1] : FString());
		if (!Mode || !PlayerPawn(World) || !Crater || !Plug || !PlanetShell(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Caves probe: needs the ship mode, the pawn, the planetoid shell, a Jam.Crater and a Jam.CavePlug wall"));
			return;
		}
		ProbeCrater(Mode, Crater, [Mode, Plug]() { After(Mode, 1.0f, [Mode, Plug]() { ProbePlug(Mode, Plug); }); });
	}

	FAutoConsoleCommandWithWorldAndArgs GShipCaves(
		TEXT("jam.Ship.Caves"),
		TEXT("jam.Ship.Caves [Crater] [Plug]: shotgun a crater membrane and drop through; walk at a cave plug, then shoot it and walk through."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShipCaves));

	/**
	 * jam.Ship.CaveShots: rendered stills (real RHI) - the planet from afar,
	 * standing on the relief, a crater membrane before and 0.35 s after a blast
	 * from its rim, a cave plug from 6 m down its tunnel.
	 */
	void RunCaveShots(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamLivingWall* Crater = TaggedWall(World, TEXT("Jam.Crater"));
		AJamLivingWall* Plug = TaggedWall(World, TEXT("Jam.CavePlug"));
		if (!Mode || !Shell || !Controller || !Crater || !Plug)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Cave shots: needs the ship mode, the planetoid, a crater and a plug"));
			return;
		}
		const FVector Centre = Shell->GetActorLocation();
		const auto Place = [Controller](const FVector& Where, const FVector& Target)
		{
			Teleport(Controller->GetPawn(), Where);
			Controller->SetControlRotation((Target - Where).Rotation());
		};
		const auto Shot = [Controller](const TCHAR* Name)
		{
			LogState(Name, Cast<AJamZeroGPawn>(Controller->GetPawn()));
			UE_LOG(LogMindKore, Log, TEXT("Cave shots: %s"), Name);
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		};
		const auto Look = [Controller](const FVector& Target)
		{
			if (const APawn* Pawn = Controller->GetPawn())
			{
				Controller->SetControlRotation((Target - Cast<AJamZeroGPawn>(Pawn)->Camera->GetComponentLocation()).Rotation());
			}
		};
		// 0: from afar (zero-G, 160 m out toward the ship side).
		const FVector Far = Centre + FVector(-0.3f, -1.0f, 0.35f).GetSafeNormal() * 25000.0f;
		After(Mode, 1.0f, [Place, Far, Centre]() { Place(Far, Centre); });
		After(Mode, 9.0f, [Place, Far, Centre, Shot]() { Place(Far, Centre); Shot(TEXT("planet from afar")); });
		// 1: standing on the relief at the top, looking along +X.
		const FVector Top = Centre + FVector::UpVector * (Shell->SphereRadiusCm - 600.0f);
		After(Mode, 10.0f, [Place, Top]() { Place(Top, Top + FVector(1000.0f, 0.0f, -300.0f)); });
		After(Mode, 14.0f, [Shot]() { Shot(TEXT("on the relief")); });
		// 2, 3: a crater from its rim (8.5 m out), before and after a blast.
		const FVector Up = Crater->GetFrontNormal();
		const FVector Side = FVector::CrossProduct(Up, FVector::RightVector).GetSafeNormal();
		const FVector Rim = Crater->GetActorLocation() + Side * 850.0f + Up * 500.0f;
		const FVector Membrane = Crater->GetActorLocation();
		After(Mode, 15.0f, [Place, Rim, Membrane]() { Place(Rim, Membrane); });
		After(Mode, 19.0f, [Look, Membrane]() { Look(Membrane); });
		After(Mode, 19.3f, [Shot]() { Shot(TEXT("crater, whole")); });
		After(Mode, 21.0f, [Controller, Membrane]() { ShotgunAt(Cast<AJamZeroGPawn>(Controller->GetPawn()), Membrane); });
		After(Mode, 21.05f, [Look, Membrane]() { Look(Membrane); });
		After(Mode, 21.35f, [Shot]() { Shot(TEXT("crater, +0.35 s after the blast")); });
		// 4: a cave plug from 6 m down its tunnel.
		const FVector PlugFront = Plug->GetFrontNormal();
		const FVector PlugUp = Plug->GetActorUpVector();
		const FVector Stand = Plug->GetActorLocation() + PlugFront * 600.0f - PlugUp * 130.0f;
		const FVector PlugCentre = Plug->GetActorLocation();
		After(Mode, 24.0f, [Place, Stand, PlugCentre]() { Place(Stand, PlugCentre); });
		After(Mode, 27.5f, [Look, PlugCentre]() { Look(PlugCentre); });
		After(Mode, 28.0f, [Shot]() { Shot(TEXT("cave plug")); });
	}

	FAutoConsoleCommandWithWorld GCaveShots(
		TEXT("jam.Ship.CaveShots"),
		TEXT("Rendered stills of the planetoid: from afar, on the relief, a crater membrane before/after the shotgun, a cave plug (HighResShot; real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunCaveShots));

	FAutoConsoleCommandWithWorld GShipProbe(
		TEXT("jam.Ship.Probe"),
		TEXT("Ship: standing, walking and jumping in the cabin, the suit and the airlock, zero-G outside, the planetoid; a plate; every part in, the lever."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipProbe));
}

#undef LOCTEXT_NAMESPACE
