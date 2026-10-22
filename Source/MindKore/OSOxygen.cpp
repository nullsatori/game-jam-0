#include "OSOxygen.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OSOxygen"

UOSOxygenComponent::UOSOxygenComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UOSOxygenComponent::BeginPlay()
{
	Super::BeginPlay();
	Seconds = CapacitySeconds;
}

void UOSOxygenComponent::Spend(float Amount)
{
	Seconds = FMath::Max(0.0f, Seconds - FMath::Max(Amount, 0.0f));
}

void UOSOxygenComponent::Refill(float Amount)
{
	const float Before = Seconds;
	Seconds = FMath::Min(CapacitySeconds, Seconds + FMath::Max(Amount, 0.0f));
	UE_LOG(LogMindKore, Log, TEXT("Oxygen %s: %.0f -> %.0f s"), *GetNameSafe(GetOwner()), Before, Seconds);
}

void UOSOxygenComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (bBreathable)
	{
		Seconds = FMath::Min(CapacitySeconds, Seconds + BreathableRefillPerSecond * DeltaTime);
	}
	else
	{
		Spend(BreathingPerSecond * DeltaTime);
	}

	UMKVitalsComponent* Vitals = GetOwner()->FindComponentByClass<UMKVitalsComponent>();
	if (!Vitals || Vitals->IsDepleted())
	{
		return; // dead: nothing more to take until the revival
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
}

AOSOxygenCanister::AOSOxygenCanister()
{
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	Body->SetStaticMesh(Cylinder.Object);
	// 30 cm across, 60 cm tall: the engine cylinder is 100 x 100.
	Body->SetRelativeScale3D(FVector(0.3f, 0.3f, 0.6f));
	MassKg = 4.0f;
}

FMKInteractionOffer AOSOxygenCanister::QueryInteraction(const AActor* Hand) const
{
	return Hand && Hand->FindComponentByClass<UOSOxygenComponent>()
		? FMKInteractionOffer::Available(LOCTEXT("Breathe", "Подключить баллон O2"))
		: FMKInteractionOffer::None();
}

void AOSOxygenCanister::CompleteInteraction(AActor* Hand)
{
	if (UOSOxygenComponent* Oxygen = Hand ? Hand->FindComponentByClass<UOSOxygenComponent>() : nullptr)
	{
		Oxygen->Refill(RefillSeconds);
		Destroy();
	}
}

#undef LOCTEXT_NAMESPACE
