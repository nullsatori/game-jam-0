#include "MKControlSwitch.h"

#include "Components/LightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "MKDoor.h"
#include "MKFlickerLightActor.h"
#include "MKGate.h"
#include "MKLift.h"
#include "MKMechanismMotion.h"
#include "MKShelfBlock.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"

#define LOCTEXT_NAMESPACE "MKControlSwitch"

AMKControlSwitch::AMKControlSwitch()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = false;

	Body = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->SetMobility(EComponentMobility::Static);
	Body->SetCollisionProfileName(TEXT("BlockAll"));
	Body->SetGenerateOverlapEvents(false);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	// A wall box the size of a hand must not carve the navmesh.
	Body->SetCanEverAffectNavigation(false);

	// Handle and lamp are looked at, not touched: the hand aims at the Body.
	Handle = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Handle"));
	Handle->SetupAttachment(Body);
	// Pivot of SM_MK_Switch_Handle in SM_MK_Switch_Body space (author_switch.py HANDLE_PIVOT).
	Handle->SetRelativeLocation(FVector(5.5f, 0.0f, -3.0f));
	Handle->SetMobility(EComponentMobility::Movable);
	Handle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Handle->SetCanEverAffectNavigation(false);

	Lamp = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Lamp"));
	Lamp->SetupAttachment(Body);
	Lamp->SetMobility(EComponentMobility::Movable);
	Lamp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lamp->SetCanEverAffectNavigation(false);
	Lamp->SetCastShadow(false);

	ActionLabel = LOCTEXT("DefaultAction", "Включить");
	LightsOnActionLabel = LOCTEXT("LightsOnAction", "Выключить");
}

void AMKControlSwitch::BeginPlay()
{
	Super::BeginPlay();
	HandleAlpha = 0.0f;
	bHandleThrown = false;
	ApplyHandle();
	ApplyLamp();
	if (Lights.Num() > 0)
	{
		bLightsOn = IsLightLit(Lights[0]);
	}
}

void AMKControlSwitch::ApplyHandle()
{
	const float Eased = FMath::SmoothStep(0.0f, 1.0f, HandleAlpha);
	Handle->SetRelativeRotation(FRotator(FMath::Lerp(HandleRestPitch, HandleThrownPitch, Eased), 0.0f, 0.0f));
}

void AMKControlSwitch::ApplyLamp()
{
	if (UMaterialInterface* Material = IsLive() ? LampLitMaterial : LampDarkMaterial)
	{
		Lamp->SetMaterial(0, Material);
	}
}

void AMKControlSwitch::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	HandleAlpha = MKAdvanceAlpha(HandleAlpha, bHandleThrown, HandleSeconds, DeltaSeconds);
	ApplyHandle();
	const bool bArrived = HandleAlpha == (bHandleThrown ? 1.0f : 0.0f);
	if (!bArrived)
	{
		return;
	}
	if (bMomentary && bHandleThrown)
	{
		bHandleThrown = false;
		return;
	}
	SetActorTickEnabled(false);
}

FMKInteractionOffer AMKControlSwitch::QueryInteraction(const AActor* Hand) const
{
	if (!Hand)
	{
		return FMKInteractionOffer::None();
	}
	if (bOneShot && bUsed)
	{
		return FMKInteractionOffer::Refused(ActionLabel, LOCTEXT("AlreadyUsed", "Уже сработало"));
	}
	if (Lift && !Lift->AcceptsCaller(Hand))
	{
		return FMKInteractionOffer::Refused(ActionLabel, Lift->NoClearanceReason);
	}
	// A packet switch without Lights names its next position by its handle, when it was given the second verb.
	const bool bOnVerb = Lights.Num() > 0 ? bLightsOn : !bOneShot && !bMomentary && !bHandleThrown && !LightsOnActionLabel.IsEmpty();
	FMKInteractionOffer Offer = FMKInteractionOffer::Available(bOnVerb ? LightsOnActionLabel : ActionLabel);
	Offer.bAlwaysSpelled = bAlwaysSpelled;
	return Offer;
}

bool AMKControlSwitch::IsOn() const
{
	if (Lights.Num() > 0)
	{
		return bLightsOn;
	}
	return bOneShot || bMomentary || !bHandleThrown;
}

bool MKCurrentThrough(TConstArrayView<TObjectPtr<AMKControlSwitch>> Switches)
{
	for (const AMKControlSwitch* Switch : Switches)
	{
		if (Switch && !Switch->IsOn())
		{
			return false;
		}
	}
	return true;
}

bool AMKControlSwitch::IsLightLit(const AActor* Light)
{
	if (const AMKFlickerLightActor* Flicker = Cast<AMKFlickerLightActor>(Light))
	{
		return Flicker->IsLit();
	}
	const ULightComponent* Component = Light ? Light->FindComponentByClass<ULightComponent>() : nullptr;
	return Component && Component->IsVisible();
}

void AMKControlSwitch::SetLightLit(AActor* Light, bool bLit)
{
	if (AMKFlickerLightActor* Flicker = Cast<AMKFlickerLightActor>(Light))
	{
		Flicker->SetLit(bLit);
		return;
	}
	TInlineComponentArray<ULightComponent*> Components(Light);
	for (ULightComponent* Component : Components)
	{
		Component->SetVisibility(bLit);
	}
}

void AMKControlSwitch::CompleteInteraction(AActor* Hand)
{
	Activate(Hand);
}

void AMKControlSwitch::Activate(AActor* User)
{
	// Before anything is spent or made to sound: a refused caller leaves the
	// switch exactly as it was.
	if (Lift && !Lift->AcceptsCaller(User))
	{
		UE_LOG(LogMindKore, Log, TEXT("Switch %s: lift %s refuses %s (needs '%s')"), *GetName(), *Lift->GetName(),
			User ? *User->GetName() : TEXT("script"), *Lift->RequiredKeyId.ToString());
		return;
	}
	if (bOneShot && bUsed)
	{
		return;
	}
	bUsed = true;
	++UseCount;
	UE_LOG(LogMindKore, Log, TEXT("Switch %s used (%d) by %s"), *GetName(), UseCount,
		User ? *User->GetName() : TEXT("script"));
	MKReportMechanismNoise(this, GetActorLocation(), UseNoiseLoudness, UseNoiseTag);
	if (UseSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, UseSound, GetActorLocation());
	}
	bHandleThrown = bMomentary || bOneShot ? true : !bHandleThrown;
	SetActorTickEnabled(true);
	ApplyLamp();

	if (Lights.Num() > 0)
	{
		bLightsOn = !bLightsOn;
		for (AActor* Light : Lights)
		{
			if (Light)
			{
				SetLightLit(Light, bLightsOn);
			}
		}
	}

	for (AMKGate* Gate : Gates)
	{
		if (!Gate)
		{
			continue;
		}
		if (bToggleGates)
		{
			Gate->Toggle();
		}
		else
		{
			Gate->Open();
		}
	}
	for (AMKDoor* Door : DoorsToUnlock)
	{
		if (Door)
		{
			Door->Unlock();
		}
	}
	for (AMKShelfBlock* Block : ShelfBlocks)
	{
		if (Block)
		{
			Block->Toggle();
		}
	}
	if (Lift)
	{
		Lift->Call();
	}
}

#undef LOCTEXT_NAMESPACE
