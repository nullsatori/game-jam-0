#include "MKImpactDummy.h"

#include "MKHitZoneComponent.h"
#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "UObject/ConstructorHelpers.h"

AMKImpactDummy::AMKImpactDummy()
{
	VisualRoot = CreateDefaultSubobject<USceneComponent>(TEXT("VisualRoot"));
	SetRootComponent(VisualRoot);
	FlinchRoot = VisualRoot;

	Mesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("Mesh"));
	HeadMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("HeadMesh"));
	LeftArmMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftArmMesh"));
	RightArmMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightArmMesh"));
	LeftLegMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("LeftLegMesh"));
	RightLegMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("RightLegMesh"));
	ArmorPlateMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ArmorPlateMesh"));

	TorsoHitZone = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("TorsoHitZone"));
	HeadHitZone = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("HeadHitZone"));
	LeftArmHitZone = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("LeftArmHitZone"));
	RightArmHitZone = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("RightArmHitZone"));
	LeftLegHitZone = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("LeftLegHitZone"));
	RightLegHitZone = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("RightLegHitZone"));
	ArmorPlate = CreateDefaultSubobject<UMKHitZoneComponent>(TEXT("ArmorPlate"));

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ConfigureVisualPart(Mesh, CubeMesh.Object, FVector(0.0f, 0.0f, 108.0f), FVector(0.5f, 0.18f, 0.9f));
		ConfigureVisualPart(HeadMesh, CubeMesh.Object, FVector(0.0f, 0.0f, 210.0f), FVector(0.28f, 0.22f, 0.28f));
		ConfigureVisualPart(LeftArmMesh, CubeMesh.Object, FVector(0.0f, -34.0f, 124.0f), FVector(0.18f, 0.14f, 0.95f));
		ConfigureVisualPart(RightArmMesh, CubeMesh.Object, FVector(0.0f, 34.0f, 124.0f), FVector(0.18f, 0.14f, 0.95f));
		ConfigureVisualPart(LeftLegMesh, CubeMesh.Object, FVector(0.0f, -15.0f, 36.0f), FVector(0.2f, 0.13f, 0.72f));
		ConfigureVisualPart(RightLegMesh, CubeMesh.Object, FVector(0.0f, 15.0f, 36.0f), FVector(0.2f, 0.13f, 0.72f));
		ConfigureVisualPart(ArmorPlateMesh, CubeMesh.Object, FVector(-8.0f, 0.0f, 125.0f), FVector(0.56f, 0.08f, 0.58f));
	}

	ConfigureHitZone(TorsoHitZone, EMKHitZone::Torso, EMKSurfaceKind::Flesh, FVector(0.0f, 0.0f, 108.0f), FVector(25.0f, 9.0f, 45.0f));
	ConfigureHitZone(HeadHitZone, EMKHitZone::Head, EMKSurfaceKind::Flesh, FVector(0.0f, 0.0f, 210.0f), FVector(14.0f, 11.0f, 14.0f));
	ConfigureHitZone(LeftArmHitZone, EMKHitZone::LeftArm, EMKSurfaceKind::Flesh, FVector(0.0f, -34.0f, 124.0f), FVector(9.0f, 7.0f, 48.0f));
	ConfigureHitZone(RightArmHitZone, EMKHitZone::RightArm, EMKSurfaceKind::Flesh, FVector(0.0f, 34.0f, 124.0f), FVector(9.0f, 7.0f, 48.0f));
	ConfigureHitZone(LeftLegHitZone, EMKHitZone::LeftLeg, EMKSurfaceKind::Flesh, FVector(0.0f, -15.0f, 36.0f), FVector(10.0f, 6.5f, 36.0f));
	ConfigureHitZone(RightLegHitZone, EMKHitZone::RightLeg, EMKSurfaceKind::Flesh, FVector(0.0f, 15.0f, 36.0f), FVector(10.0f, 6.5f, 36.0f));
	ConfigureHitZone(ArmorPlate, EMKHitZone::Armor, EMKSurfaceKind::Armor, FVector(-8.0f, 0.0f, 125.0f), FVector(28.0f, 4.0f, 29.0f));

	MaxHealth = 100.0f;
	CurrentHealth = 100.0f;
}

void AMKImpactDummy::ConfigureVisualPart(UStaticMeshComponent* Component, UStaticMesh* MeshAsset, const FVector& RelativeLocation, const FVector& RelativeScale)
{
	if (!Component || !MeshAsset || !VisualRoot)
	{
		return;
	}

	Component->SetupAttachment(VisualRoot);
	Component->SetStaticMesh(MeshAsset);
	Component->SetRelativeLocation(RelativeLocation);
	Component->SetRelativeScale3D(RelativeScale);
	Component->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Component->SetGenerateOverlapEvents(false);
	Component->SetCanEverAffectNavigation(false);
}

void AMKImpactDummy::ConfigureHitZone(UMKHitZoneComponent* HitZone, EMKHitZone HitZoneType, EMKSurfaceKind SurfaceKind, const FVector& RelativeLocation, const FVector& BoxExtent)
{
	if (!HitZone || !VisualRoot)
	{
		return;
	}

	HitZone->HitZone = HitZoneType;
	HitZone->SurfaceKind = SurfaceKind;
	HitZone->SetupAttachment(VisualRoot);
	HitZone->SetRelativeLocation(RelativeLocation);
	HitZone->SetRelativeRotation(FRotator::ZeroRotator);
	HitZone->SetBoxExtent(BoxExtent);
}
