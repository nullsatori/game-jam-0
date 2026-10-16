#include "MKGate.h"

#include "Components/StaticMeshComponent.h"
#include "MKInteractable.h"
#include "MKMechanismMotion.h"
#include "MindKore.h"

AMKGate::AMKGate()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);

	Panel = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Panel"));
	Panel->SetupAttachment(Root);
	Panel->SetMobility(EComponentMobility::Movable);
	Panel->SetCollisionProfileName(TEXT("BlockAll"));
	Panel->SetGenerateOverlapEvents(false);
	Panel->SetCanEverAffectNavigation(true);
}

void AMKGate::BeginPlay()
{
	Super::BeginPlay();
	ClosedRelativeLocation = Panel->GetRelativeLocation();
	Alpha = bStartOpen ? 1.0f : 0.0f;
	State = bStartOpen ? EMKGateState::Open : EMKGateState::Closed;
	ApplyAlpha(/*bSweep=*/false);
}

void AMKGate::SetState(EMKGateState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Gate %s: %s -> %s (alpha %.2f)"), *GetName(),
		*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState), Alpha);
	State = NewState;
}

void AMKGate::ApplyAlpha(bool bSweep)
{
	const FVector Target = ClosedRelativeLocation + OpenOffsetCm * Alpha;
	FHitResult Hit;
	Panel->SetRelativeLocation(Target, bSweep, bSweep ? &Hit : nullptr);
	if (bSweep && Hit.bBlockingHit && State == EMKGateState::Closing)
	{
		// Something under the panel. Reverse: a gate that stops half-way is a
		// trap for the navmesh and for whoever is under it.
		UE_LOG(LogMindKore, Log, TEXT("Gate %s: closing blocked by %s, reopening"), *GetName(),
			Hit.GetActor() ? *Hit.GetActor()->GetName() : TEXT("?"));
		Alpha = FMath::Clamp((Panel->GetRelativeLocation() - ClosedRelativeLocation).Size() / FMath::Max(OpenOffsetCm.Size(), 1.0f), 0.0f, 1.0f);
		SetState(EMKGateState::Opening);
	}
}

void AMKGate::Open()
{
	if (State == EMKGateState::Open || State == EMKGateState::Opening)
	{
		return;
	}
	MKReportMechanismNoise(this, Panel->GetComponentLocation(), MoveNoiseLoudness, NoiseTag);
	SetState(EMKGateState::Opening);
}

void AMKGate::Close()
{
	if (State == EMKGateState::Closed || State == EMKGateState::Closing)
	{
		return;
	}
	MKReportMechanismNoise(this, Panel->GetComponentLocation(), MoveNoiseLoudness, NoiseTag);
	SetState(EMKGateState::Closing);
}

void AMKGate::Toggle()
{
	if (State == EMKGateState::Open || State == EMKGateState::Opening)
	{
		Close();
	}
	else
	{
		Open();
	}
}

void AMKGate::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (State != EMKGateState::Opening && State != EMKGateState::Closing)
	{
		return;
	}
	const bool bToOpen = State == EMKGateState::Opening;
	Alpha = MKAdvanceAlpha(Alpha, bToOpen, TravelSeconds, DeltaSeconds);
	ApplyAlpha(/*bSweep=*/!bToOpen);
	if (State == EMKGateState::Opening && Alpha >= 1.0f)
	{
		SetState(EMKGateState::Open);
	}
	else if (State == EMKGateState::Closing && Alpha <= 0.0f)
	{
		SetState(EMKGateState::Closed);
	}
}
