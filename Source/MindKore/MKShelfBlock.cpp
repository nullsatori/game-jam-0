#include "MKShelfBlock.h"

#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "MKInteractable.h"
#include "MKMechanismMotion.h"
#include "MindKore.h"

AMKShelfBlock::AMKShelfBlock()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Movable);
}

void AMKShelfBlock::BeginPlay()
{
	Super::BeginPlay();
	PositionILocation = GetActorLocation();
	Alpha = 0.0f;
	State = EMKShelfPosition::PositionI;
}

FVector AMKShelfBlock::WorldTravel() const
{
	return GetActorRotation().RotateVector(TravelOffsetCm);
}

void AMKShelfBlock::SetState(EMKShelfPosition NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("ShelfBlock %s: %s -> %s (alpha %.2f)"), *GetName(),
		*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState), Alpha);
	State = NewState;
	bReportedBlock = false;
}

void AMKShelfBlock::MoveTo(EMKShelfPosition Target)
{
	if (Target != EMKShelfPosition::PositionI && Target != EMKShelfPosition::PositionII)
	{
		return;
	}
	const bool bWantII = Target == EMKShelfPosition::PositionII;
	if ((bWantII && Alpha >= 1.0f) || (!bWantII && Alpha <= 0.0f))
	{
		SetState(Target);
		return;
	}
	bTowardII = bWantII;
	MKReportMechanismNoise(this, GetActorLocation() + GetActorRotation().RotateVector(FootprintCenterCm), MoveNoiseLoudness, TEXT("MK_Shelf"));
	SetState(EMKShelfPosition::Moving);
}

void AMKShelfBlock::Toggle()
{
	switch (State)
	{
	case EMKShelfPosition::PositionI:
		MoveTo(EMKShelfPosition::PositionII);
		break;
	case EMKShelfPosition::PositionII:
		MoveTo(EMKShelfPosition::PositionI);
		break;
	case EMKShelfPosition::Moving:
	case EMKShelfPosition::Blocked:
		// The crank turned the other way: go back where it came from.
		MoveTo(bTowardII ? EMKShelfPosition::PositionI : EMKShelfPosition::PositionII);
		break;
	}
}

TArray<AActor*> AMKShelfBlock::FindObstructions(bool bInTowardII) const
{
	TArray<AActor*> Result;
	UWorld* World = GetWorld();
	if (!World)
	{
		return Result;
	}
	const FVector Step = WorldTravel() * (bInTowardII ? 1.0f : -1.0f) * (1.0f / FMath::Max(TravelSeconds, 0.1f)) * 0.25f;
	const FVector Center = GetActorLocation() + GetActorRotation().RotateVector(FootprintCenterCm);
	const FVector HalfExtent = GetActorRotation().RotateVector(FootprintHalfExtentCm).GetAbs();
	const FBox Slab = MKShelfStepBox(Center, HalfExtent, Step, ObstructionPaddingCm);

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectParams;
	ObjectParams.AddObjectTypesToQuery(ECC_Pawn);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(MKShelfObstruction), false, this);
	World->OverlapMultiByObjectType(Overlaps, Slab.GetCenter(), FQuat::Identity, ObjectParams,
		FCollisionShape::MakeBox(Slab.GetExtent()), Params);
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		if (Actor && Actor->IsA<APawn>() && !Actor->IsAttachedTo(this))
		{
			Result.AddUnique(Actor);
		}
	}
	return Result;
}

void AMKShelfBlock::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (State != EMKShelfPosition::Moving && State != EMKShelfPosition::Blocked)
	{
		return;
	}

	const TArray<AActor*> Obstructions = FindObstructions(bTowardII);
	if (Obstructions.Num() > 0)
	{
		if (!bReportedBlock)
		{
			UE_LOG(LogMindKore, Log, TEXT("ShelfBlock %s: held by %s"), *GetName(), *Obstructions[0]->GetName());
			bReportedBlock = true;
		}
		SetState(EMKShelfPosition::Blocked);
		bReportedBlock = true;
		return;
	}
	if (State == EMKShelfPosition::Blocked)
	{
		SetState(EMKShelfPosition::Moving);
	}

	Alpha = MKAdvanceAlpha(Alpha, bTowardII, TravelSeconds, DeltaSeconds);
	SetActorLocation(PositionILocation + WorldTravel() * Alpha, /*bSweep=*/false);

	if (bTowardII && Alpha >= 1.0f)
	{
		SetState(EMKShelfPosition::PositionII);
	}
	else if (!bTowardII && Alpha <= 0.0f)
	{
		SetState(EMKShelfPosition::PositionI);
	}
}
