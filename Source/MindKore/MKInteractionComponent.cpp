#include "MKInteractionComponent.h"

#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
// Jam lift: MKDeviceHUD.h include cut - the bodycam device journal (UMKDeviceMemory) is not lifted.
#include "MindKore.h"

UMKInteractionComponent::UMKInteractionComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PrePhysics;
}

bool UMKInteractionComponent::GetViewPoint(FVector& OutLocation, FVector& OutDirection) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	const APlayerController* PlayerController = Pawn ? Cast<APlayerController>(Pawn->GetController()) : nullptr;
	if (!PlayerController)
	{
		return false;
	}
	FRotator ViewRotation;
	PlayerController->GetPlayerViewPoint(OutLocation, ViewRotation);
	OutDirection = ViewRotation.Vector();
	return true;
}

void UMKInteractionComponent::RefreshCandidate()
{
	AActor* NewCandidate = nullptr;
	FMKInteractionOffer NewOffer;

	FVector Start;
	FVector Direction;
	UWorld* World = GetWorld();
	if (World && GetViewPoint(Start, Direction))
	{
		const auto Offers = [](const FHitResult& Found)
		{
			const AActor* FoundActor = Found.GetActor();
			return FoundActor && FoundActor->Implements<UMKInteractable>();
		};
		FCollisionQueryParams Params(SCENE_QUERY_STAT(MKInteractionLook), /*bTraceComplex=*/false, GetOwner());
		const FVector End = Start + Direction * ReachCm;
		FHitResult Hit;
		const bool bMet = World->LineTraceSingleByChannel(Hit, Start, End, ECC_Visibility, Params);
		bool bOn = bMet && Offers(Hit);
		if (!bOn && LookSlackCm > 0.0f)
		{
			// A near miss. The ball goes no farther than what the line itself met, so nothing is found
			// behind a wall or behind the thing looked at; a ball that starts inside something finds nothing.
			FHitResult Near;
			bOn = World->SweepSingleByChannel(Near, Start, bMet ? FVector(Hit.ImpactPoint) : End, FQuat::Identity, ECC_Visibility,
				FCollisionShape::MakeSphere(LookSlackCm), Params) && !Near.bStartPenetrating && Offers(Near);
			if (bOn)
			{
				Hit = Near;
			}
		}
		const double Now = World->GetTimeSeconds();
		if (bOn)
		{
			NewCandidate = Hit.GetActor();
			CandidateComponent = Hit.GetComponent();
			CandidateHitPoint = Hit.ImpactPoint;
			CandidateSeenSeconds = Now;
		}
		else if (IsValid(Candidate) && CandidateComponent && Now - CandidateSeenSeconds <= HoldSeconds
			&& FVector::DistSquared(Start, CandidateHitPoint) <= FMath::Square(ReachCm + LookSlackCm))
		{
			// The look slipped off a moment ago and found nothing else: still the same thing, where it was found.
			NewCandidate = Candidate;
		}
		if (NewCandidate)
		{
			NewOffer = Cast<IMKInteractable>(NewCandidate)->QueryInteraction(GetOwner());
		}
	}
	if (!NewCandidate)
	{
		CandidateComponent = nullptr;
	}

	if (NewCandidate != Candidate)
	{
		UE_LOG(LogMindKore, Verbose, TEXT("Interaction candidate: %s (%s)"),
			NewCandidate ? *NewCandidate->GetName() : TEXT("none"),
			NewOffer.IsOffered() ? *NewOffer.Action.ToString() : TEXT("-"));
	}
	Candidate = NewCandidate;
	CurrentOffer = NewOffer;
}

void UMKInteractionComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	RefreshCandidate();
}

void UMKInteractionComponent::Interact()
{
	RefreshCandidate();
	if (!Candidate)
	{
		UE_LOG(LogMindKore, Verbose, TEXT("Interact: nothing in reach"));
		return;
	}
	if (!CurrentOffer.bAvailable)
	{
		UE_LOG(LogMindKore, Log, TEXT("Interact: %s refuses '%s': %s"),
			*Candidate->GetName(), *CurrentOffer.Action.ToString(), *CurrentOffer.Reason.ToString());
		// Jam lift: UMKDeviceMemory::NoteRefusal cut - the device journal is not lifted.
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Interact: %s '%s'"), *Candidate->GetName(), *CurrentOffer.Action.ToString());
	Cast<IMKInteractable>(Candidate)->CompleteInteraction(GetOwner());
	// Jam lift: UMKDeviceMemory::Learn cut - the device journal is not lifted.
	RefreshCandidate();
}

UBoxComponent* MKCreateHandTarget(AActor& Owner, FName Name, USceneComponent* Parent, const FVector& Location, const FVector& HalfSizeCm)
{
	UBoxComponent* Box = Owner.CreateDefaultSubobject<UBoxComponent>(Name);
	Box->SetupAttachment(Parent);
	Box->SetRelativeLocation(Location);
	Box->SetBoxExtent(HalfSizeCm);
	Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Box->SetCollisionObjectType(ECC_WorldDynamic);
	Box->SetCollisionResponseToAllChannels(ECR_Ignore);
	Box->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	Box->SetGenerateOverlapEvents(false);
	Box->SetCanEverAffectNavigation(false);
	return Box;
}

UStaticMeshComponent* MKCreatePartMesh(AActor& Owner, FName Name, USceneComponent* Parent, UStaticMesh* Shape, const FVector& Location, const FVector& SizeCm)
{
	UStaticMeshComponent* Part = Owner.CreateDefaultSubobject<UStaticMeshComponent>(Name);
	Part->SetupAttachment(Parent);
	Part->SetStaticMesh(Shape);
	Part->SetRelativeLocation(Location);
	Part->SetRelativeScale3D(SizeCm / 100.0f);
	Part->SetCollisionProfileName(UCollisionProfile::NoCollision_ProfileName);
	Part->SetGenerateOverlapEvents(false);
	Part->SetCanEverAffectNavigation(false);
	Part->SetCastShadow(false);
	return Part;
}

const UPrimitiveComponent* MKLookedAtComponent(const AActor* Hand)
{
	const UMKInteractionComponent* Look = Hand ? Hand->FindComponentByClass<UMKInteractionComponent>() : nullptr;
	return Look ? Look->CandidateComponent.Get() : nullptr;
}
