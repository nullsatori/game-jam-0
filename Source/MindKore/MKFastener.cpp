#include "MKFastener.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "MKPhysicsProp.h"
#include "MindKore.h"

#define LOCTEXT_NAMESPACE "MKFastener"

AMKFastener::AMKFastener()
{
	PrimaryActorTick.bCanEverTick = false;

	Blocker = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Blocker"));
	SetRootComponent(Blocker);
	// A wall until pried: bodies, the eye (so the interaction trace finds it)
	// and the navmesh all see it.
	Blocker->SetCollisionProfileName(UCollisionProfile::BlockAll_ProfileName);
	Blocker->SetCanEverAffectNavigation(true);

	ActionLabel = LOCTEXT("Pry", "Вскрыть");
	NoToolReason = LOCTEXT("NoTool", "нечем вскрыть");
	ToolNotInHandReason = LOCTEXT("ToolNotInHand", "лом не в руках");
}

EMKFastenerOffer AMKFastener::ResolveOffer(const AActor* Hand) const
{
	// Jam lift: UMKHandsComponent slot/tool-ready checks cut - the crowbar is the MK.Crowbar tag on the hand.
	if (!Hand)
	{
		return EMKFastenerOffer::None;
	}
	return MKResolveFastenerOffer(bPried, Hand->ActorHasTag(MKFastenerCrowbarTag()));
}

FMKInteractionOffer AMKFastener::QueryInteraction(const AActor* Hand) const
{
	switch (ResolveOffer(Hand))
	{
	case EMKFastenerOffer::Available:
		return FMKInteractionOffer::Available(ActionLabel);
	case EMKFastenerOffer::RefuseNoTool:
		return FMKInteractionOffer::Refused(ActionLabel, NoToolReason);
	case EMKFastenerOffer::RefuseHandsBusy:
		// Jam lift: MKHandsBusyText() (MKHands) replaced by a local text.
		return FMKInteractionOffer::Refused(ActionLabel, LOCTEXT("HandsBusy", "руки заняты"));
	case EMKFastenerOffer::RefuseToolNotInHand:
		return FMKInteractionOffer::Refused(ActionLabel, ToolNotInHandReason);
	case EMKFastenerOffer::None:
	default:
		return FMKInteractionOffer::None();
	}
}

void AMKFastener::CompleteInteraction(AActor* Hand)
{
	// Re-checked here: the executor calls Complete after an available Query,
	// but a script or a test can call it with any hand.
	if (ResolveOffer(Hand) != EMKFastenerOffer::Available)
	{
		return;
	}
	bPried = true;
	// The same lever AMKDoor pulls on its leaf when it stops being a wall.
	Blocker->SetCanEverAffectNavigation(false);
	if (bBlockerFalls)
	{
		// Torn off with a little push away from the wall; then it is the floor's.
		constexpr float LidKickCmPerS = 80.0f;
		Blocker->SetMobility(EComponentMobility::Movable);
		Blocker->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
		PriedSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Blocker->SetNotifyRigidBodyCollision(true);
		Blocker->OnComponentHit.AddUniqueDynamic(this, &AMKFastener::HandleLidHit);
		Blocker->SetSimulatePhysics(true);
		Blocker->AddImpulse(GetActorForwardVector() * LidKickCmPerS, NAME_None, true);
	}
	else
	{
		SetActorHiddenInGame(true);
		SetActorEnableCollision(false);
	}

	int32 Spawned = 0;
	if (SpawnOnPry)
	{
		if (UWorld* World = GetWorld())
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			for (const FTransform& Relative : SpawnTransforms)
			{
				if (World->SpawnActor<AMKPhysicsProp>(SpawnOnPry, Relative * GetActorTransform(), Params))
				{
					++Spawned;
				}
			}
		}
	}

	MKReportMechanismNoise(this, GetActorLocation(), PryNoiseLoudness, TEXT("MK_Fastener"));
	UE_LOG(LogMindKore, Log, TEXT("Fastener %s: pried by %s, %d piece(s) dropped"),
		*GetName(), *GetNameSafe(Hand), Spawned);
	OnPried.Broadcast(this);
}

void AMKFastener::HandleLidHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	// The landing is the first real blow once it has left: the floor, or the
	// knees of whoever pried it crouching (the cabinet's door, PIE 11.10). The
	// closing speed is read as UMKPhysicsPropComponent reads it. Its first
	// 0.2 s are the push off the wall, the box or the desk it came from; a
	// bounce after the landing is not another one.
	constexpr float LandSpeedCm = 150.0f;
	constexpr double LeavingSeconds = 0.2;
	const UWorld* World = GetWorld();
	const float MassKg = HitComponent ? HitComponent->GetMass() : 0.0f;
	if (bLanded || !World || World->GetTimeSeconds() < PriedSeconds + LeavingSeconds || MassKg <= UE_SMALL_NUMBER
		|| NormalImpulse.Size() / MassKg < LandSpeedCm)
	{
		return;
	}
	bLanded = true;
	if (LandSound)
	{
		// A cap of a few grams ticks where a steel door clangs: full voice from 2 kg.
		UGameplayStatics::PlaySoundAtLocation(this, LandSound, Hit.ImpactPoint, FMath::Clamp(FMath::Sqrt(MassKg / 2.0f), 0.15f, 1.0f));
	}
	UE_LOG(LogMindKore, Log, TEXT("Fastener %s: the lid landed at %.0f cm/s, %.2f kg"), *GetName(), NormalImpulse.Size() / MassKg, MassKg);
}

#undef LOCTEXT_NAMESPACE
