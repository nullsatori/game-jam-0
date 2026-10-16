#include "MKLift.h"

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "MKGate.h"
#include "MKKeyring.h"
#include "MindKore.h"

#define LOCTEXT_NAMESPACE "MKLift"

AMKLift::AMKLift()
{
	PrimaryActorTick.bCanEverTick = true;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);
	Root->SetMobility(EComponentMobility::Static);

	CabinVolume = CreateDefaultSubobject<UBoxComponent>(TEXT("CabinVolume"));
	CabinVolume->SetupAttachment(Root);
	CabinVolume->SetBoxExtent(FVector(90.0f, 135.0f, 120.0f));
	CabinVolume->SetRelativeLocation(FVector(-100.0f, 0.0f, 120.0f));
	CabinVolume->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CabinVolume->SetGenerateOverlapEvents(false);
	CabinVolume->SetCanEverAffectNavigation(false);

	// A line trace that starts inside a box reports no hit, so the offer
	// cannot hang on the volume itself: it hangs on a button the eye finds
	// on the back wall of the cabin.
	DepartButton = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("DepartButton"));
	DepartButton->SetupAttachment(Root);
	DepartButton->SetRelativeLocation(FVector(-212.0f, 0.0f, 100.0f));
	DepartButton->SetCollisionProfileName(TEXT("BlockAll"));
	DepartButton->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	DepartButton->SetGenerateOverlapEvents(false);
	DepartButton->SetCanEverAffectNavigation(false);
	DepartButton->SetMobility(EComponentMobility::Static);

	Indicator = CreateDefaultSubobject<UPointLightComponent>(TEXT("Indicator"));
	Indicator->SetupAttachment(Root);
	Indicator->SetRelativeLocation(FVector(30.0f, 0.0f, 250.0f));
	Indicator->SetLightColor(FLinearColor(1.0f, 0.62f, 0.18f));
	Indicator->SetIntensity(IndicatorIdleIntensity);
	Indicator->SetAttenuationRadius(500.0f);
	Indicator->SetCastShadows(false);
	Indicator->SetMobility(EComponentMobility::Movable);

	NoClearanceReason = LOCTEXT("NoClearance", "Нет допуска");
}

void AMKLift::BeginPlay()
{
	Super::BeginPlay();
	State = EMKLiftState::Idle;
	Indicator->SetIntensity(IndicatorIdleIntensity);
	if (!Door)
	{
		UE_LOG(LogMindKore, Warning, TEXT("Lift %s has no Door gate; the cabin will arrive with nothing to open"), *GetName());
	}
}

void AMKLift::SetState(EMKLiftState NewState)
{
	if (State == NewState)
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Lift %s: %s -> %s"), *GetName(),
		*UEnum::GetValueAsString(State), *UEnum::GetValueAsString(NewState));
	State = NewState;
}

bool AMKLift::IsInsideCabin(const AActor* Actor) const
{
	if (!Actor)
	{
		return false;
	}
	const FVector Local = CabinVolume->GetComponentTransform().InverseTransformPosition(Actor->GetActorLocation());
	const FVector Extent = CabinVolume->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

bool AMKLift::AcceptsCaller(const AActor* Caller) const
{
	return RequiredKeyId.IsNone() || MKHolderHasKey(Caller, RequiredKeyId);
}

void AMKLift::Call()
{
	if (State != EMKLiftState::Idle)
	{
		UE_LOG(LogMindKore, Log, TEXT("Lift %s: call ignored in state %s"), *GetName(), *UEnum::GetValueAsString(State));
		return;
	}
	TravelElapsed = 0.0f;
	NoiseAccumulator = 0.0f;
	MKReportMechanismNoise(this, GetActorLocation(), TravelNoiseLoudness, TEXT("MK_Lift"));
	SetState(EMKLiftState::Called);
}

void AMKLift::Depart()
{
	if (State != EMKLiftState::Arrived)
	{
		return;
	}
	if (Door)
	{
		Door->Close();
	}
	SetState(EMKLiftState::Departed);
	UE_LOG(LogMindKore, Log, TEXT("Admin: lift departed - level goal reached"));
	OnDeparted.Broadcast();
}

FMKInteractionOffer AMKLift::QueryInteraction(const AActor* Hand) const
{
	if (!Hand)
	{
		return FMKInteractionOffer::None();
	}
	switch (State)
	{
	case EMKLiftState::Arrived:
		return IsInsideCabin(Hand)
			? FMKInteractionOffer::Available(LOCTEXT("Depart", "Отправить"))
			: FMKInteractionOffer::None();
	default:
		return FMKInteractionOffer::None();
	}
}

void AMKLift::CompleteInteraction(AActor* Hand)
{
	if (State == EMKLiftState::Arrived && IsInsideCabin(Hand))
	{
		Depart();
	}
}

void AMKLift::UpdateIndicator(float DeltaSeconds)
{
	switch (State)
	{
	case EMKLiftState::Idle:
		Indicator->SetIntensity(IndicatorIdleIntensity);
		break;
	case EMKLiftState::Called:
		BlinkAccumulator += DeltaSeconds;
		Indicator->SetIntensity(FMath::Fmod(BlinkAccumulator, 1.0f) < 0.5f ? IndicatorActiveIntensity : IndicatorIdleIntensity);
		break;
	case EMKLiftState::Arrived:
		Indicator->SetIntensity(IndicatorActiveIntensity);
		break;
	case EMKLiftState::Departed:
		Indicator->SetIntensity(IndicatorIdleIntensity);
		break;
	}
}

void AMKLift::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateIndicator(DeltaSeconds);

	if (State != EMKLiftState::Called)
	{
		return;
	}
	TravelElapsed += DeltaSeconds;
	NoiseAccumulator += DeltaSeconds;
	if (NoiseAccumulator >= 1.0f)
	{
		NoiseAccumulator -= 1.0f;
		MKReportMechanismNoise(this, GetActorLocation(), TravelNoiseLoudness, TEXT("MK_Lift"));
	}
	if (TravelElapsed >= TravelSeconds)
	{
		MKReportMechanismNoise(this, GetActorLocation(), ArriveNoiseLoudness, TEXT("MK_LiftArrive"));
		if (Door)
		{
			Door->Open();
		}
		SetState(EMKLiftState::Arrived);
	}
}

#undef LOCTEXT_NAMESPACE
