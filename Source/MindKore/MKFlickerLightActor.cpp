#include "MKFlickerLightActor.h"

#include "Components/PointLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

AMKFlickerLightActor::AMKFlickerLightActor()
{
	PrimaryActorTick.bCanEverTick = true;

	SceneRoot = CreateDefaultSubobject<USceneComponent>(TEXT("SceneRoot"));
	SetRootComponent(SceneRoot);

	FlickerLight = CreateDefaultSubobject<UPointLightComponent>(TEXT("FlickerLight"));
	FlickerLight->SetupAttachment(SceneRoot);
	FlickerLight->SetIntensity(BaseIntensity);
	FlickerLight->SetAttenuationRadius(AttenuationRadius);
	FlickerLight->SetLightColor(LightColor);
	FlickerLight->SetCastShadows(true);

	FixtureMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("FixtureMesh"));
	FixtureMesh->SetupAttachment(SceneRoot);
	FixtureMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	FixtureMesh->SetCastShadow(false);
	FixtureMesh->SetRelativeLocation(FVector(0.0f, 0.0f, 5.0f));
	FixtureMesh->SetRelativeScale3D(FVector(1.2f, 0.12f, 0.05f));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		FixtureMesh->SetStaticMesh(CubeMesh.Object);
	}
}

void AMKFlickerLightActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	FixtureMesh->SetVisibility(!IsValid(EmissiveFixtureActor));
}

void AMKFlickerLightActor::BeginPlay()
{
	Super::BeginPlay();
	TimeOffset = FMath::FRandRange(0.0f, 100.0f);
	if (IsValid(EmissiveFixtureActor))
	{
		FixtureMesh->SetVisibility(false);
		TInlineComponentArray<UStaticMeshComponent*> Components(EmissiveFixtureActor);
		for (UStaticMeshComponent* Component : Components)
		{
			for (int32 Slot = 0; Slot < Component->GetNumMaterials(); ++Slot)
			{
				UMaterialInterface* Material = Component->GetMaterial(Slot);
				float Brightness = 0.0f;
				if (Material && Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Brightness")), Brightness))
				{
					if (UMaterialInstanceDynamic* Dynamic = Component->CreateDynamicMaterialInstance(Slot))
					{
						FixtureMaterials.Add(Dynamic);
						FixturePeakBrightness.Add(Brightness);
					}
				}
			}
		}
	}
}

bool AMKFlickerLightActor::IsLit() const
{
	return FlickerLight->IsVisible();
}

void AMKFlickerLightActor::SetLit(bool bLit)
{
	FlickerLight->SetVisibility(bLit);
	// Tick drives the fixture's glow; with it stopped the glow is set here once.
	SetActorTickEnabled(bLit);
	if (!bLit)
	{
		for (UMaterialInstanceDynamic* Material : FixtureMaterials)
		{
			Material->SetScalarParameterValue(TEXT("Brightness"), 0.0f);
		}
	}
}

void AMKFlickerLightActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!FlickerLight)
	{
		return;
	}

	if (StutterTimeRemaining <= 0.0f && FMath::FRand() < StutterChancePerSecond * DeltaSeconds)
	{
		StutterTimeRemaining = StutterDuration;
	}

	StutterTimeRemaining = FMath::Max(0.0f, StutterTimeRemaining - DeltaSeconds);

	const float Time = GetWorld() ? GetWorld()->GetTimeSeconds() + TimeOffset : TimeOffset;
	const float SlowWave = FMath::Sin(Time * FlickerSpeed) * 0.5f + 0.5f;
	const float FastBuzz = FMath::Sin(Time * FlickerSpeed * 3.7f) * 0.5f + 0.5f;
	const float Irregular = FMath::Lerp(SlowWave, FastBuzz, 0.35f);
	const float StutterMultiplier = StutterTimeRemaining > 0.0f ? StutterIntensityMultiplier : 1.0f;

	const float NewIntensity = (BaseIntensity + FlickerIntensity * Irregular) * StutterMultiplier;
	FlickerLight->SetIntensity(NewIntensity);
	const float RelativeIntensity = FMath::Clamp(NewIntensity / FMath::Max(BaseIntensity + FlickerIntensity, 1.0f), 0.0f, 1.0f);
	for (int32 Index = 0; Index < FixtureMaterials.Num(); ++Index)
	{
		FixtureMaterials[Index]->SetScalarParameterValue(TEXT("Brightness"), FixturePeakBrightness[Index] * RelativeIntensity);
	}
	FlickerLight->SetAttenuationRadius(AttenuationRadius);
	FlickerLight->SetLightColor(LightColor);
}
