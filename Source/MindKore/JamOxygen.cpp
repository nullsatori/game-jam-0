#include "JamOxygen.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "JamOxygen"

UJamOxygenComponent::UJamOxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UJamOxygenComponent::BeginPlay()
{
	Super::BeginPlay();
	Seconds = CapacitySeconds;
}

void UJamOxygenComponent::Spend(float Amount)
{
	Seconds = FMath::Max(0.0f, Seconds - FMath::Max(Amount, 0.0f));
}

void UJamOxygenComponent::Refill(float Amount)
{
	const float Before = Seconds;
	Seconds = FMath::Min(CapacitySeconds, Seconds + FMath::Max(Amount, 0.0f));
	UE_LOG(LogMindKore, Log, TEXT("Oxygen %s: %.0f -> %.0f s"), *GetNameSafe(GetOwner()), Before, Seconds);
}

void UJamOxygenComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bDead)
	{
		return;
	}
	Spend(BreathingPerSecond * DeltaTime);

	UMKVitalsComponent* Vitals = GetOwner()->FindComponentByClass<UMKVitalsComponent>();
	if (!Vitals)
	{
		return;
	}
	if (IsEmpty())
	{
		// Paid once a second, not per frame: every ApplyIncoming is a log line.
		SuffocationAccumulator += DeltaTime;
		if (SuffocationAccumulator >= 1.0f)
		{
			SuffocationAccumulator -= 1.0f;
			Vitals->ApplyIncoming(SuffocationDamagePerSecond, EMKImpactKind::Round, /*bBlocked=*/false, FGuid());
		}
	}
	else
	{
		SuffocationAccumulator = 0.0f;
	}

	if (!Vitals->IsDepleted())
	{
		return;
	}
	bDead = true;
	APawn* Pawn = Cast<APawn>(GetOwner());
	APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	UE_LOG(LogMindKore, Warning, TEXT("Oxygen %s: dead (oxygen %.0f s); restarting the level in %.0f s"),
		*GetNameSafe(GetOwner()), Seconds, RestartDelaySeconds);
	if (!PlayerController)
	{
		return;
	}
	Pawn->DisableInput(PlayerController);
	FTimerHandle Restart;
	GetWorld()->GetTimerManager().SetTimer(Restart,
		FTimerDelegate::CreateWeakLambda(PlayerController, [PlayerController]() { PlayerController->RestartLevel(); }),
		FMath::Max(RestartDelaySeconds, 0.01f), /*bLoop=*/false);
}

AJamOxygenCanister::AJamOxygenCanister()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Body->SetStaticMesh(Cylinder.Object);
	// 30 cm across, 60 cm tall: the engine cylinder is 100 x 100.
	Body->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.6f));
	MassKg = 4.0f;
}

FMKInteractionOffer AJamOxygenCanister::QueryInteraction(const AActor* Hand) const
{
	return Hand && Hand->FindComponentByClass<UJamOxygenComponent>()
		? FMKInteractionOffer::Available(LOCTEXT("Breathe", "Подключить баллон O2"))
		: FMKInteractionOffer::None();
}

void AJamOxygenCanister::CompleteInteraction(AActor* Hand)
{
	if (UJamOxygenComponent* Oxygen = Hand ? Hand->FindComponentByClass<UJamOxygenComponent>() : nullptr)
	{
		Oxygen->Refill(RefillSeconds);
		Destroy();
	}
}

#undef LOCTEXT_NAMESPACE
