#include "JamShip.h"

#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "HAL/IConsoleManager.h"
#include "JamDockZone.h"
#include "JamGravity.h"
#include "JamOxygen.h"
#include "JamPickups.h"
#include "JamShipParts.h"
#include "JamZeroGPawn.h"
#include "MKInteractionComponent.h"
#include "MKKeyring.h"
#include "MKPhysicsProp.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "TimerManager.h"

#define LOCTEXT_NAMESPACE "JamShip"

// --- walker ------------------------------------------------------------------------

AJamShipWalker::AJamShipWalker(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UJamWalkerMovement>(ACharacter::CharacterMovementComponentName))
{
	// The capsule is turned by Tick (up against gravity), not by the controller's yaw.
	bUseControllerRotationYaw = false;
	Oxygen = CreateDefaultSubobject<UJamOxygenComponent>(TEXT("Oxygen"));
}

void AJamShipWalker::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const FVector Up = -GetCharacterMovement()->GetGravityDirection();
	FVector Forward = FVector::VectorPlaneProject(GetControlRotation().Vector(), Up);
	if (Forward.IsNearlyZero())
	{
		Forward = FVector::VectorPlaneProject(GetActorForwardVector(), Up);
	}
	SetActorRotation(FRotationMatrix::MakeFromZX(Up, Forward).Rotator());
}

// --- controller ----------------------------------------------------------------------

void AJamShipController::UpdateRotation(float DeltaTime)
{
	// Epic's custom-gravity pattern: work in gravity space, carry the view
	// along when gravity turns, write the result back in world space.
	FVector GravityDirection = FVector::DownVector;
	if (const ACharacter* Walker = Cast<ACharacter>(GetPawn()))
	{
		if (const UCharacterMovementComponent* Movement = Walker->GetCharacterMovement())
		{
			GravityDirection = Movement->GetGravityDirection();
		}
	}
	FRotator ViewRotation = GetControlRotation();
	if (!LastFrameGravity.Equals(FVector::ZeroVector))
	{
		const FQuat DeltaGravityRotation = FQuat::FindBetweenNormals(LastFrameGravity, GravityDirection);
		ViewRotation = (DeltaGravityRotation * FQuat(ViewRotation)).Rotator();
	}
	LastFrameGravity = GravityDirection;
	ViewRotation = JamGravityRelativeRotation(ViewRotation, GravityDirection);

	FRotator DeltaRotation(RotationInput);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ProcessViewRotation(DeltaTime, ViewRotation, DeltaRotation);
		ViewRotation.Roll = 0.0f;
		SetControlRotation(JamGravityWorldRotation(ViewRotation, GravityDirection));
	}
	if (APawn* const ViewPawn = GetPawnOrSpectator())
	{
		ViewPawn->FaceRotation(ViewRotation, DeltaTime);
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
	WalkerClass = AJamShipWalker::StaticClass();
	FloaterClass = AJamZeroGPawn::StaticClass();
	DefaultPawnClass = WalkerClass;
	HUDClass = AJamShipHUD::StaticClass();
	PlayerControllerClass = AJamShipController::StaticClass();
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
	// Air: any breathable zone (the planetoid's warm shell is wider than its walking shell).
	const AJamGravityZone* Air = ZoneAt(Location, 0.0f, /*bWalkOnly=*/false);
	if (UJamOxygenComponent* Oxygen = Pawn->FindComponentByClass<UJamOxygenComponent>())
	{
		Oxygen->SetBreathable(Air && Air->bBreathable, Air ? Air->RefillPerSecond : 0.0f);
	}
	if (AJamShipWalker* Walker = Cast<AJamShipWalker>(Pawn))
	{
		// Out of every zone (past the margin): float.
		const AJamGravityZone* Zone = ZoneAt(Location, -SwapMarginCm);
		if (!Zone)
		{
			SwapPawn(PlayerController, FloaterClass);
			return;
		}
		Walker->GetCharacterMovement()->SetGravityDirection(Zone->GravityAt(Location));
		if (UJamWalkerMovement* Movement = Cast<UJamWalkerMovement>(Walker->GetCharacterMovement()))
		{
			Movement->WalkerGravityZ = -Zone->GravityCm;
		}
	}
	else if (Cast<AJamZeroGPawn>(Pawn) && ZoneAt(Location, SwapMarginCm))
	{
		// Well inside a zone: walk.
		SwapPawn(PlayerController, WalkerClass);
	}
}

void AJamShipGameMode::StoreProgress(APawn* Pawn)
{
	if (const UMKVitalsComponent* Vitals = Pawn->FindComponentByClass<UMKVitalsComponent>())
	{
		Progress.Health = Vitals->Health;
		Progress.Armor = Vitals->Armor;
	}
	if (const UMKKeyringComponent* Keyring = MKFindKeyring(Pawn))
	{
		Progress.Keys = Keyring->GetKeyIds();
	}
	Progress.Flags = Pawn->Tags;
	if (const AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn))
	{
		Progress.Weapons.Reset();
		for (int32 Index = 0; Index < Floater->Weapons.Num(); ++Index)
		{
			Progress.Weapons.Add(Floater->IsWeaponUnlocked(Index));
		}
	}
	if (const UJamOxygenComponent* Oxygen = Pawn->FindComponentByClass<UJamOxygenComponent>())
	{
		Progress.Oxygen = Oxygen->Seconds;
	}
}

void AJamShipGameMode::ApplyProgress(APawn* Pawn) const
{
	if (UMKVitalsComponent* Vitals = Pawn->FindComponentByClass<UMKVitalsComponent>(); Vitals && Progress.Health >= 0.0f)
	{
		Vitals->Health = Progress.Health;
		Vitals->Armor = Progress.Armor;
	}
	if (UMKKeyringComponent* Keyring = MKFindKeyring(Pawn))
	{
		for (const FName& Key : Progress.Keys)
		{
			Keyring->AddKey(Key);
		}
	}
	for (const FName& Flag : Progress.Flags)
	{
		Pawn->Tags.AddUnique(Flag);
	}
	if (AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn))
	{
		// The ship starts with no guns: only what pickups unlocked comes along.
		int32 FirstUnlocked = INDEX_NONE;
		for (int32 Index = 0; Index < Floater->Weapons.Num(); ++Index)
		{
			const bool bUnlocked = Progress.Weapons.IsValidIndex(Index) && Progress.Weapons[Index];
			Floater->SetWeaponUnlocked(Index, bUnlocked);
			FirstUnlocked = FirstUnlocked == INDEX_NONE && bUnlocked ? Index : FirstUnlocked;
		}
		if (FirstUnlocked != INDEX_NONE && !Floater->IsWeaponUnlocked(Floater->CurrentWeapon))
		{
			Floater->SelectWeapon(FirstUnlocked);
		}
	}
	if (UJamOxygenComponent* Oxygen = Pawn->FindComponentByClass<UJamOxygenComponent>(); Oxygen && Progress.Oxygen >= 0.0f)
	{
		Oxygen->Seconds = Progress.Oxygen;
	}
}

APawn* AJamShipGameMode::SwapPawn(APlayerController* PlayerController, TSubclassOf<APawn> Class)
{
	APawn* Old = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Old || !Class)
	{
		return nullptr;
	}
	const FRotator View = PlayerController->GetControlRotation();
	const FVector Velocity = Old->GetVelocity();
	StoreProgress(Old);

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	APawn* New = GetWorld()->SpawnActor<APawn>(Class, Old->GetActorLocation(), FRotator(0.0f, View.Yaw, 0.0f), Params);
	if (!New)
	{
		UE_LOG(LogMindKore, Error, TEXT("Ship: could not spawn %s at %s"), *GetNameSafe(Class.Get()), *Old->GetActorLocation().ToString());
		return nullptr;
	}
	if (AJamZeroGPawn* OldFloater = Cast<AJamZeroGPawn>(Old))
	{
		OldFloater->Release();
		OldFloater->DetachTether();
	}
	// The old pawn's mapping context goes with it; the new pawn adds its own on possession.
	if (const ULocalPlayer* LocalPlayer = PlayerController->GetLocalPlayer())
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input = LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>())
		{
			Input->ClearAllMappings();
		}
	}
	PlayerController->Possess(New);
	// Possession resets the control rotation to the pawn's: the view is put back.
	PlayerController->SetControlRotation(View);
	ApplyProgress(New);
	if (AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(New))
	{
		Floater->Body->SetPhysicsLinearVelocity(Velocity);
	}
	else if (ACharacter* Walker = Cast<ACharacter>(New))
	{
		Walker->GetCharacterMovement()->Velocity = Velocity;
		Walker->GetCharacterMovement()->SetMovementMode(MOVE_Falling);
	}
	Old->Destroy();
	UE_LOG(LogMindKore, Log, TEXT("Ship: swapped %s -> %s at %s, velocity %.0f cm/s, view %s"), *GetNameSafe(Old->GetClass()),
		*New->GetName(), *New->GetActorLocation().ToCompactString(), Velocity.Size(), *View.ToCompactString());
	return New;
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
	UE_LOG(LogMindKore, Log, TEXT("Ship: launch - Улетел, cargo %d/%d; restarting in %.0f s"), Progress.Cargo, CargoTotal, EndRestartSeconds);
	Toast(FText::Format(LOCTEXT("Flew", "Улетел! Груз {0}/{1}"), Progress.Cargo, CargoTotal));
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
	Super::DrawHUD(); // the zero-G HUD draws itself only for a zero-G pawn
	if (!Canvas)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const APawn* Pawn = GetOwningPawn();
	const AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>();

	if (const AJamShipWalker* Walker = Cast<AJamShipWalker>(Pawn))
	{
		const float CenterX = Canvas->ClipX * 0.5f;
		const float CenterY = Canvas->ClipY * 0.5f;
		DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), CenterX - 2.0f, CenterY - 2.0f, 4.0f, 4.0f);
		const FMKInteractionOffer& Offer = Walker->Interaction->CurrentOffer;
		if (Offer.IsOffered())
		{
			FString Line = TEXT("[E] ") + Offer.Action.ToString();
			if (!Offer.bAvailable)
			{
				Line += TEXT(" - ") + Offer.Reason.ToString();
			}
			float Width = 0.0f;
			float Height = 0.0f;
			GetTextSize(Line, Width, Height, Font);
			DrawText(Line, Offer.bAvailable ? FLinearColor::White : FLinearColor(1.0f, 0.45f, 0.35f), CenterX - Width * 0.5f, CenterY + 24.0f, Font);
		}
		DrawText(FString::Printf(TEXT("HP %.0f   O2 %.0f s%s"), Walker->Vitals->Health, Walker->Oxygen->Seconds,
			MKHolderHasKey(Walker, MKJamSuitKey()) ? TEXT("   скафандр") : TEXT("")),
			FLinearColor(0.8f, 0.8f, 0.8f), 24.0f, Canvas->ClipY - 48.0f, Font);
	}

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
		DrawText(FString::Printf(TEXT("Груз %d/%d"), Mode->Progress.Cargo, Mode->CargoTotal), FLinearColor(1.0f, 0.85f, 0.4f), 24.0f, Y, Font);
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

// --- console probe ----------------------------------------------------------------------------

namespace
{
	/** Run Step after Seconds of game time (the probe's chain of steps). */
	void After(AJamShipGameMode* Mode, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Mode->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Mode, MoveTemp(Step)), Seconds, false);
	}

	APawn* PlayerPawn(const UWorld* World)
	{
		const APlayerController* PlayerController = World->GetFirstPlayerController();
		return PlayerController ? PlayerController->GetPawn() : nullptr;
	}

	void LogCarry(const TCHAR* Stage, const APawn* Pawn)
	{
		const UMKVitalsComponent* Vitals = Pawn ? Pawn->FindComponentByClass<UMKVitalsComponent>() : nullptr;
		const UJamOxygenComponent* Oxygen = Pawn ? Pawn->FindComponentByClass<UJamOxygenComponent>() : nullptr;
		const AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn);
		UE_LOG(LogMindKore, Log, TEXT("Ship probe [%s]: pawn %s, health %.0f, suit %s, crowbar %s, pistol %s, O2 %.0f, view %s"), Stage,
			*GetNameSafe(Pawn), Vitals ? Vitals->Health : -1.0f, MKHolderHasKey(Pawn, MKJamSuitKey()) ? TEXT("yes") : TEXT("no"),
			Pawn && Pawn->ActorHasTag(TEXT("MK.Crowbar")) ? TEXT("yes") : TEXT("no"),
			Floater ? (Floater->IsWeaponUnlocked(0) ? TEXT("unlocked") : TEXT("locked")) : TEXT("(walker)"),
			Oxygen ? Oxygen->Seconds : -1.0f, Pawn && Pawn->GetController() ? *Pawn->GetController()->GetControlRotation().ToCompactString() : TEXT("-"));
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

	/** (a) Walker -> zero-G -> walker -> zero-G through the cabin zone's edge. */
	void RunShipProbe(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		APawn* Walker = PlayerPawn(World);
		// The box zones (cabin, airlock): inside = the biggest one's centre,
		// outside = 2 m past the lowest X of them all (into the hull, toward the break).
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
		if (!Mode || !Walker || !Cabin)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: needs AJamShipGameMode, a pawn and a box gravity zone"));
			return;
		}
		LogPanel(World, TEXT("start"));
		if (UMKVitalsComponent* Vitals = Walker->FindComponentByClass<UMKVitalsComponent>())
		{
			Vitals->Health = 77.0f;
		}
		if (UMKKeyringComponent* Keyring = MKFindKeyring(Walker))
		{
			Keyring->AddKey(MKJamSuitKey());
		}
		Walker->Tags.AddUnique(TEXT("MK.Crowbar"));
		LogCarry(TEXT("walker before"), Walker);
		const FVector Inside = Cabin->GetActorLocation();
		const FVector Outside(MinX - 200.0, Inside.Y, Inside.Z);
		Teleport(Walker, Outside);
		After(Mode, 0.3f, [Mode, Inside, Outside]()
		{
			APawn* Floater = PlayerPawn(Mode->GetWorld());
			LogCarry(TEXT("out: zero-G"), Floater);
			for (TActorIterator<AJamWeaponPickup> Pickup(Mode->GetWorld()); Pickup; ++Pickup)
			{
				if (Pickup->WeaponIndex == 0)
				{
					Pickup->CompleteInteraction(Floater);
				}
			}
			LogCarry(TEXT("pistol picked up"), Floater);
			Teleport(Floater, Inside);
			After(Mode, 0.3f, [Mode, Outside]()
			{
				APawn* Back = PlayerPawn(Mode->GetWorld());
				LogCarry(TEXT("in: walker"), Back);
				Teleport(Back, Outside);
				After(Mode, 0.3f, [Mode]()
				{
					LogCarry(TEXT("out again: zero-G"), PlayerPawn(Mode->GetWorld()));
					ProbePlate(Mode);
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

	FAutoConsoleCommandWithWorld GShipProbe(
		TEXT("jam.Ship.Probe"),
		TEXT("Ship blockout: pawn swaps with carried progress, a plate refused and fitted, every part in, the lever."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipProbe));
}

#undef LOCTEXT_NAMESPACE
