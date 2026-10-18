#include "JamGameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Camera/CameraComponent.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "EngineUtils.h"
#include "JamCharacter.h"
#include "JamCollisionImpact.h"
#include "JamDockZone.h"
#include "JamOxygen.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "JamShip.h"
#include "MKPhysicsProp.h"
#include "MindKore.h"
#include "MKCarryComponent.h"
#include "MKInteractionComponent.h"
#include "MKTimeDomainSubsystem.h"
#include "MKVitals.h"

AJamGameMode::AJamGameMode()
{
	DefaultPawnClass = AJamCharacter::StaticClass();
	HUDClass = AJamHUD::StaticClass();
}

void AJamHUD::DrawHUD()
{
	Super::DrawHUD();
	if (!Canvas)
	{
		return;
	}
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), CenterX - 2.0f, CenterY - 2.0f, 4.0f, 4.0f);

	const AJamCharacter* Pawn = Cast<AJamCharacter>(GetOwningPawn());
	if (!Pawn)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();

	const FMKInteractionOffer& Offer = Pawn->Interaction->CurrentOffer;
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
		DrawText(Line, Offer.bAvailable ? FLinearColor::White : FLinearColor(1.0f, 0.45f, 0.35f),
			CenterX - Width * 0.5f, CenterY + 24.0f, Font);
	}

	FString State = FString::Printf(TEXT("HP %.0f  AR %.0f   %s"), Pawn->Vitals->Health, Pawn->Vitals->Armor, *Pawn->Guns->Describe());
	if (Pawn->Carry->IsHolding())
	{
		State += TEXT("   carrying (LMB throw, RMB/F put)");
	}
	if (Pawn->HasCrowbar())
	{
		State += TEXT("   crowbar");
	}
	if (const UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		State += FString::Printf(TEXT("   bullet time %.1f s%s"), Time->GetRemainingSeconds(), Time->IsSlowed() ? TEXT(" ACTIVE") : TEXT(""));
	}
	DrawText(State, FLinearColor(0.8f, 0.8f, 0.8f), 24.0f, Canvas->ClipY - 48.0f, Font);
}

AJamZeroGGameMode::AJamZeroGGameMode()
{
	PlayerControllerClass = AJamShipController::StaticClass();
	DefaultPawnClass = AJamZeroGPawn::StaticClass();
	HUDClass = AJamZeroGHUD::StaticClass();
}

void AJamZeroGGameMode::StartPlay()
{
	Super::StartPlay();
	FRandomStream Random(DriftSeed);
	int32 Count = 0;
	for (TActorIterator<AMKPhysicsProp> It(GetWorld()); It; ++It)
	{
		UPrimitiveComponent* PropBody = It->Body;
		// Jam.NoDrift: placed parts (a module on the asteroid) stay where the level put them.
		if (!PropBody || !PropBody->IsSimulatingPhysics() || It->ActorHasTag(TEXT("Jam.NoDrift")))
		{
			continue;
		}
		PropBody->SetLinearDamping(0.0f);
		PropBody->SetAngularDamping(0.0f);
		PropBody->SetPhysicsLinearVelocity(Random.GetUnitVector() * Random.FRandRange(0.0f, InitialDriftMaxCm));
		PropBody->SetPhysicsAngularVelocityInDegrees(Random.GetUnitVector() * Random.FRandRange(0.0f, InitialSpinMaxDeg));
		PropBody->WakeAllRigidBodies();
		// Its collisions become blows: a heavy fast prop breaks glass and hurts.
		if (!It->FindComponentByClass<UJamCollisionImpactComponent>())
		{
			UJamCollisionImpactComponent* Impacts = NewObject<UJamCollisionImpactComponent>(*It);
			Impacts->RegisterComponent();
		}
		++Count;
	}
	UE_LOG(LogMindKore, Log, TEXT("ZeroG mode: world gravity Z %.1f, %d props set drifting (up to %.0f cm/s)"),
		GetWorld()->GetGravityZ(), Count, InitialDriftMaxCm);
}

void AJamZeroGHUD::DrawHUD()
{
	Super::DrawHUD();
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(GetOwningPawn());
	if (!Canvas || !Pawn)
	{
		return;
	}
	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	DrawRect(FLinearColor(1.0f, 1.0f, 1.0f, 0.8f), CenterX - 2.0f, CenterY - 2.0f, 4.0f, 4.0f);
	UFont* Font = GEngine->GetMediumFont();

	// Where you are drifting: a diamond on the prograde point (camera + velocity
	// direction), or a cross on the retrograde point when the motion is behind
	// the view; the speed next to it.
	const FVector Velocity = Pawn->GetBodyVelocity();
	if (PlayerOwner && Velocity.Size() > 10.0f)
	{
		const FVector Eye = Pawn->Camera->GetComponentLocation();
		const FVector Along = Velocity.GetSafeNormal() * 1000.0f;
		const FString Speed = FString::Printf(TEXT("%.1f m/s"), Velocity.Size() / 100.0f);
		FVector2D Screen;
		if (PlayerOwner->ProjectWorldLocationToScreen(Eye + Along, Screen, false))
		{
			const FLinearColor Green(0.4f, 1.0f, 0.5f);
			DrawLine(Screen.X - 10.0f, Screen.Y, Screen.X, Screen.Y - 10.0f, Green, 2.0f);
			DrawLine(Screen.X, Screen.Y - 10.0f, Screen.X + 10.0f, Screen.Y, Green, 2.0f);
			DrawLine(Screen.X + 10.0f, Screen.Y, Screen.X, Screen.Y + 10.0f, Green, 2.0f);
			DrawLine(Screen.X, Screen.Y + 10.0f, Screen.X - 10.0f, Screen.Y, Green, 2.0f);
			DrawText(Speed, Green, Screen.X + 14.0f, Screen.Y - 8.0f, Font);
		}
		else if (PlayerOwner->ProjectWorldLocationToScreen(Eye - Along, Screen, false))
		{
			const FLinearColor Red(1.0f, 0.45f, 0.35f);
			DrawLine(Screen.X - 9.0f, Screen.Y - 9.0f, Screen.X + 9.0f, Screen.Y + 9.0f, Red, 2.0f);
			DrawLine(Screen.X - 9.0f, Screen.Y + 9.0f, Screen.X + 9.0f, Screen.Y - 9.0f, Red, 2.0f);
			DrawText(Speed, Red, Screen.X + 14.0f, Screen.Y - 8.0f, Font);
		}
	}

	const FMKInteractionOffer& Offer = Pawn->Interaction->CurrentOffer;
	if (Offer.IsOffered())
	{
		FString Line = TEXT("[F] ") + Offer.Action.ToString(); // the zero-G pawn interacts on F
		if (!Offer.bAvailable)
		{
			Line += TEXT(" - ") + Offer.Reason.ToString();
		}
		float Width = 0.0f;
		float Height = 0.0f;
		GetTextSize(Line, Width, Height, Font);
		DrawText(Line, Offer.bAvailable ? FLinearColor::White : FLinearColor(1.0f, 0.45f, 0.35f),
			CenterX - Width * 0.5f, CenterY + 24.0f, Font);
	}

	// O2 bar, bottom left: red under a fifth.
	const float Fraction = FMath::Clamp(Pawn->Oxygen->GetFraction(), 0.0f, 1.0f);
	const float BarX = 24.0f;
	const float BarY = Canvas->ClipY - 80.0f;
	DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.7f), BarX, BarY, 240.0f, 14.0f);
	DrawRect(Fraction < 0.2f ? FLinearColor(0.9f, 0.2f, 0.15f) : FLinearColor(0.3f, 0.75f, 0.95f), BarX, BarY, 240.0f * Fraction, 14.0f);
	DrawText(FString::Printf(TEXT("O2 %.0f%% (%.0f s)"), Fraction * 100.0f, Pawn->Oxygen->Seconds), FLinearColor::White, BarX + 250.0f, BarY - 2.0f, Font);

	FString State = FString::Printf(TEXT("HP %.0f   speed %.1f m/s"), Pawn->Vitals->Health, Pawn->GetBodyVelocity().Size() / 100.0f);
	State += TEXT("   ") + Pawn->Guns->Describe();
	if (const float Held = Pawn->GetHeldMassKg(); Held > 0.0f)
	{
		State += FString::Printf(TEXT("   holding %.0f kg"), Held);
	}
	if (const float Cooldown = Pawn->GetPushCooldownRemaining(); Cooldown > 0.0f)
	{
		State += FString::Printf(TEXT("   push %.1f s"), Cooldown);
	}
	if (Pawn->HasCrowbar())
	{
		State += TEXT("   crowbar");
	}
	// The ship-part dock of the zero-G map only: the ship map's docks (fuel,
	// navigation, the hull's junk counter) are shown by AJamShipHUD's lamps.
	for (TActorIterator<AJamDockZone> Dock(GetWorld()); Dock; ++Dock)
	{
		if (Dock->RequiredTag != TEXT("Ship.Part"))
		{
			continue;
		}
		State +=FString::Printf(TEXT("   Детали: %d/%d%s"), Dock->GetCount(), Dock->RequiredCount, Dock->IsCompleted() ? TEXT(" - готово") : TEXT(""));
		break;
	}
	if (Pawn->Oxygen->IsDead())
	{
		State += TEXT("   DEAD - restarting");
	}
	DrawText(State, FLinearColor(0.8f, 0.8f, 0.8f), BarX, Canvas->ClipY - 48.0f, Font);
}
