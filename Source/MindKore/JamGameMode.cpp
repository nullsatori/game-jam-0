#include "JamGameMode.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "JamCharacter.h"
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
