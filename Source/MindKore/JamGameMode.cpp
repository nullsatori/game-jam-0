#include "JamGameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "JamCharacter.h"
#include "JamCollisionImpact.h"
#include "JamDockZone.h"
#include "JamOxygen.h"
#include "JamZeroGPawn.h"
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

	FString State = FString::Printf(TEXT("HP %.0f  AR %.0f"), Pawn->Vitals->Health, Pawn->Vitals->Armor);
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
		if (!PropBody || !PropBody->IsSimulatingPhysics())
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

	// O2 bar, bottom left: red under a fifth.
	const float Fraction = FMath::Clamp(Pawn->Oxygen->GetFraction(), 0.0f, 1.0f);
	const float BarX = 24.0f;
	const float BarY = Canvas->ClipY - 80.0f;
	DrawRect(FLinearColor(0.1f, 0.1f, 0.1f, 0.7f), BarX, BarY, 240.0f, 14.0f);
	DrawRect(Fraction < 0.2f ? FLinearColor(0.9f, 0.2f, 0.15f) : FLinearColor(0.3f, 0.75f, 0.95f), BarX, BarY, 240.0f * Fraction, 14.0f);
	DrawText(FString::Printf(TEXT("O2 %.0f%% (%.0f s)"), Fraction * 100.0f, Pawn->Oxygen->Seconds), FLinearColor::White, BarX + 250.0f, BarY - 2.0f, Font);

	FString State = FString::Printf(TEXT("HP %.0f   speed %.1f m/s"), Pawn->Vitals->Health, Pawn->GetBodyVelocity().Size() / 100.0f);
	if (const FJamWeapon* Weapon = Pawn->GetWeapon())
	{
		State += FString::Printf(TEXT("   [%d] %s %d/%d"), Pawn->CurrentWeapon + 1, *Weapon->Name.ToString(), Weapon->Ammo, Weapon->Reserve);
		if (const float Reload = Pawn->GetReloadRemaining(); Reload > 0.0f)
		{
			State += FString::Printf(TEXT(" (перезарядка %.1f)"), Reload);
		}
	}
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
	for (TActorIterator<AJamDockZone> Dock(GetWorld()); Dock; ++Dock)
	{
		State += FString::Printf(TEXT("   Детали: %d/%d%s"), Dock->GetCount(), Dock->RequiredCount, Dock->IsCompleted() ? TEXT(" - готово") : TEXT(""));
		break;
	}
	if (Pawn->Oxygen->IsDead())
	{
		State += TEXT("   DEAD - restarting");
	}
	DrawText(State, FLinearColor(0.8f, 0.8f, 0.8f), BarX, Canvas->ClipY - 48.0f, Font);
}
