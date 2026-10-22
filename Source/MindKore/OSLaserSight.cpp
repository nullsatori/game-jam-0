#include "OSLaserSight.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "OSWeapons.h"
#include "OSWeldProjector.h"

UOSLaserSightComponent::UOSLaserSightComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// After the gun is posed this frame (UOSWeaponsComponent adds itself as the prerequisite): the lens where it is drawn.
	PrimaryComponentTick.TickGroup = TG_PostPhysics;
	// Asleep until a laser is on the gun in view (Wake).
	PrimaryComponentTick.bStartWithTickEnabled = false;
	DotMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Weapons/M_OS_SightDot.M_OS_SightDot")));
}

void UOSLaserSightComponent::BeginPlay()
{
	Super::BeginPlay();
	Guns = GetOwner()->FindComponentByClass<UOSWeaponsComponent>();
	const auto Make = [this](const TCHAR* Name, const TCHAR* MeshPath)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner(), Name);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetUsingAbsoluteLocation(true);
		Part->SetUsingAbsoluteRotation(true);
		Part->SetUsingAbsoluteScale(true);
		Part->SetupAttachment(GetOwner()->GetRootComponent());
		Part->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, MeshPath));
		Part->RegisterComponent();
		Part->SetVisibility(false);
		return Part;
	};
	Dot = Make(TEXT("LaserDot"), TEXT("/Engine/BasicShapes/Plane.Plane"));
	Thread = Make(TEXT("LaserThread"), TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	if (UMaterialInterface* Material = DotMaterial.LoadSynchronous())
	{
		DotMID = UMaterialInstanceDynamic::Create(Material, this);
		ThreadMID = UMaterialInstanceDynamic::Create(Material, this);
		for (UMaterialInstanceDynamic* Each : { DotMID.Get(), ThreadMID.Get() })
		{
			Each->SetVectorParameterValue(TEXT("Red"), RedColour);
		}
		Dot->SetMaterial(0, DotMID);
		Thread->SetMaterial(0, ThreadMID);
	}
	else
	{
		UE_LOG(LogMindKore, Warning, TEXT("Laser sight: %s did not load - the dot and thread draw in the engine's default material"), *DotMaterial.ToString());
	}
}

void UOSLaserSightComponent::Wake(bool bAwake)
{
	if (IsComponentTickEnabled() == bAwake)
	{
		return;
	}
	SetComponentTickEnabled(bAwake);
	if (!bAwake)
	{
		Hide();
	}
}

bool UOSLaserSightComponent::IsThreadShown() const
{
	return bLit && Thread && Thread->IsVisible();
}

void UOSLaserSightComponent::Hide()
{
	if (bLit || (Dot && Dot->IsVisible()))
	{
		Dot->SetVisibility(false);
		Thread->SetVisibility(false);
	}
	bLit = bDotHit = false;
}

void UOSLaserSightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	FTransform Lens;
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	if (!Weapon || !Dot || !Guns->GetLaserLens(Lens) || !Guns->ViewModel || !Guns->ViewModel->GetAttachParent())
	{
		Hide();
		return;
	}
	bLit = true;
	// The dot: the first thing along the view within the gun's reach - where the round goes.
	const USceneComponent* Eye = Guns->ViewModel->GetAttachParent();
	const FVector From = Eye->GetComponentLocation();
	const FVector Along = Eye->GetForwardVector();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSLaserSight), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	++Traces;
	bDotHit = GetWorld()->LineTraceSingleByChannel(Hit, From, From + Along * Weapon->RangeCm, ECC_GameTraceChannel2, Params);
	DotAt = bDotHit ? FVector(Hit.ImpactPoint) + FVector(Hit.ImpactNormal) * DotLiftCm : From + Along * Weapon->RangeCm;
	// The flutter of the diode's drive with a rare drop, and grain: a new draw every frame.
	const float Drive = (FMath::FRand() < 0.03f ? 0.7f : 1.0f + FMath::FRandRange(-0.03f, 0.03f)) * (1.0f + FMath::FRandRange(-Grain, Grain));
	if (bDotHit)
	{
		const float Distance = float(FVector::Dist(From, DotAt));
		const float Across = UOSWeldProjectorComponent::ViewPixelAngle(Cast<APawn>(GetOwner())) * DotPixels * Distance;
		// The engine plane is 100 cm across, its face up its Z: turned to the eye.
		Dot->SetWorldLocationAndRotation(DotAt, FRotationMatrix::MakeFromZ(From - DotAt).ToQuat());
		Dot->SetWorldScale3D(FVector(Across / 100.0f));
		Dot->SetVisibility(true);
		if (DotMID)
		{
			DotMID->SetScalarParameterValue(TEXT("Glow"), Glow * Drive);
		}
	}
	else
	{
		Dot->SetVisibility(false);
	}
	// The thread: only where there is something to scatter it - air round the eye (polled), smoke later.
	SinceAirPoll += DeltaTime;
	if (SinceAirPoll >= AirPollSeconds)
	{
		SinceAirPoll = 0.0f;
		bAir = Guns->IsInAir();
	}
	AirShare = FMath::FInterpConstantTo(AirShare, bAir ? 1.0f : 0.0f, DeltaTime, 1.0f / AirFadeSeconds);
	if (AirShare <= 0.0f)
	{
		Thread->SetVisibility(false);
		return;
	}
	const FVector Start = Lens.GetLocation();
	const FVector Way = DotAt - Start;
	const float Length = float(Way.Size());
	Thread->SetWorldLocationAndRotation(Start + Way * 0.5f, FRotationMatrix::MakeFromZ(Way).ToQuat());
	Thread->SetWorldScale3D(FVector(ThreadCm / 100.0f, ThreadCm / 100.0f, Length / 100.0f));
	Thread->SetVisibility(true);
	if (ThreadMID)
	{
		ThreadMID->SetScalarParameterValue(TEXT("Glow"), Glow * ThreadShare * AirShare * Drive);
	}
}
