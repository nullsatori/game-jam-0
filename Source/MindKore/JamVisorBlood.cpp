#include "JamVisorBlood.h"

#include "Camera/CameraComponent.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"

UJamVisorBloodComponent::UJamVisorBloodComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false; // only while a blot lasts
}

UJamVisorBloodComponent* UJamVisorBloodComponent::FindOrAdd(AActor* Owner)
{
	if (!Owner)
	{
		return nullptr;
	}
	if (UJamVisorBloodComponent* Found = Owner->FindComponentByClass<UJamVisorBloodComponent>())
	{
		return Found;
	}
	UJamVisorBloodComponent* Added = NewObject<UJamVisorBloodComponent>(Owner, TEXT("VisorBlood"));
	Added->RegisterComponent();
	return Added;
}

UCameraComponent* UJamVisorBloodComponent::Camera() const
{
	return GetOwner() ? GetOwner()->FindComponentByClass<UCameraComponent>() : nullptr;
}

FVector2D UJamVisorBloodComponent::Splash(float Strength)
{
	// The free slot, or the older blot.
	int32 Slot = Blots[0].Size <= 0.0f ? 0 : Blots[1].Size <= 0.0f ? 1 : (Blots[0].Age > Blots[1].Age ? 0 : 1);
	FBlot& Blot = Blots[Slot];
	// Below the middle and to a side (the side away from the other blot): the point of aim stays clear.
	const FBlot& Other = Blots[1 - Slot];
	const float Side = Other.Size > 0.0f ? (Other.Centre.X < 0.5 ? 1.0f : -1.0f) : (FMath::RandBool() ? 1.0f : -1.0f);
	Blot.Centre = FVector2D(0.5 + Side * FMath::FRandRange(0.12f, 0.3f), FMath::FRandRange(0.55f, 0.78f));
	Blot.Size = BlotSize * FMath::Clamp(Strength, 0.0f, 1.0f);
	Blot.Age = 0.0f;
	Blot.Seed = FMath::FRandRange(0.0f, 8.0f);
	SetComponentTickEnabled(true);
	return Blot.Centre;
}

int32 UJamVisorBloodComponent::GetBlotCount() const
{
	return (Blots[0].Size > 0.0f ? 1 : 0) + (Blots[1].Size > 0.0f ? 1 : 0);
}

void UJamVisorBloodComponent::SetInChain(bool bWanted)
{
	UCameraComponent* View = Camera();
	if (bWanted == bInChain || !View || !MID)
	{
		return;
	}
	TArray<FWeightedBlendable>& Chain = View->PostProcessSettings.WeightedBlendables.Array;
	if (bWanted)
	{
		Chain.Insert(FWeightedBlendable(1.0f, MID), 0); // before the lens pass: on the outside of the glass
	}
	else
	{
		Chain.RemoveAll([this](const FWeightedBlendable& Each) { return Each.Object == MID; });
	}
	bInChain = bWanted;
}

void UJamVisorBloodComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	for (FBlot& Blot : Blots)
	{
		if (Blot.Size > 0.0f && (Blot.Age += DeltaTime) >= LifeSeconds)
		{
			Blot = FBlot();
		}
	}
	if (GetBlotCount() == 0)
	{
		SetInChain(false);
		SetComponentTickEnabled(false);
		return;
	}
	if (!MID)
	{
		if (UMaterialInterface* Base = Material.LoadSynchronous())
		{
			MID = UMaterialInstanceDynamic::Create(Base, this);
		}
	}
	if (MID)
	{
		static const FName BlotNames[] = { TEXT("BlotA"), TEXT("BlotB") };
		static const FName SeedNames[] = { TEXT("SeedA"), TEXT("SeedB") };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FBlot& Blot = Blots[Index];
			MID->SetVectorParameterValue(BlotNames[Index], FLinearColor(float(Blot.Centre.X), float(Blot.Centre.Y), Blot.Size, Blot.Age));
			MID->SetScalarParameterValue(SeedNames[Index], Blot.Seed);
		}
		MID->SetScalarParameterValue(TEXT("Life"), LifeSeconds);
		SetInChain(true);
	}
}
