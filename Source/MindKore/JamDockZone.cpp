#include "JamDockZone.h"

#include "Components/BoxComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "MindKore.h"

AJamDockZone::AJamDockZone()
{
	PrimaryActorTick.bCanEverTick = true;
	Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
	SetRootComponent(Zone);
	Zone->SetBoxExtent(FVector(600.0f, 700.0f, 800.0f));
	// Counted by an explicit overlap query, not overlap events: nothing has to
	// remember to generate them. The box itself blocks and reports nothing.
	Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Zone->SetCanEverAffectNavigation(false);
}

void AJamDockZone::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams Objects(ECC_PhysicsBody);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	GetWorld()->OverlapMultiByObjectType(Overlaps, Zone->GetComponentLocation(), Zone->GetComponentQuat(), Objects,
		FCollisionShape::MakeBox(Zone->GetScaledBoxExtent()));

	TSet<AActor*> Parts;
	TArray<TWeakObjectPtr<UPrimitiveComponent>> Inside;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		AActor* Actor = Overlap.GetActor();
		UPrimitiveComponent* Root = Actor ? Cast<UPrimitiveComponent>(Actor->GetRootComponent()) : nullptr;
		if (!Root || !Root->IsSimulatingPhysics() || !Actor->ActorHasTag(RequiredTag) || Parts.Contains(Actor))
		{
			continue;
		}
		Parts.Add(Actor);
		Inside.Add(Root);
		// Soft capture: it slows down and is drawn to the centre, it is never snapped.
		Root->SetLinearDamping(CaptureDamping);
		Root->SetAngularDamping(CaptureDamping);
		Root->AddForce((Zone->GetComponentLocation() - Root->GetComponentLocation()).GetSafeNormal() * CapturePullAccelCm,
			NAME_None, /*bAccelChange=*/true);
	}

	for (const TWeakObjectPtr<UPrimitiveComponent>& Was : Captured)
	{
		if (Was.IsValid() && !Inside.Contains(Was))
		{
			Was->SetLinearDamping(0.0f);
			Was->SetAngularDamping(0.0f);
		}
	}
	Captured = MoveTemp(Inside);

	if (Parts.Num() != Count)
	{
		UE_LOG(LogMindKore, Log, TEXT("Dock %s: %d/%d parts"), *GetName(), Parts.Num(), RequiredCount);
		Count = Parts.Num();
	}
	if (!bCompleted && Count >= RequiredCount)
	{
		bCompleted = true;
		UE_LOG(LogMindKore, Log, TEXT("Dock %s: complete (%d/%d)"), *GetName(), Count, RequiredCount);
		OnCompleted.Broadcast(this);
	}
}
