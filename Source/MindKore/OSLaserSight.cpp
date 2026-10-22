#include "OSLaserSight.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Canvas.h"
#include "GameFramework/HUD.h"
#include "OSAttachMenu.h"
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
	Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
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
	return bLit && bThreadShown;
}

void UOSLaserSightComponent::Hide()
{
	if (bLit || (Dot && Dot->IsVisible()))
	{
		Dot->SetVisibility(false);
		Thread->SetVisibility(false);
	}
	bLit = bDotHit = bThreadShown = false;
}

void UOSLaserSightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if !UE_BUILD_SHIPPING
	const FOSAttachCostScope Timed(GOSAttachCost.LaserSeconds, GOSAttachCost.LaserMaxSeconds, GOSAttachCost.LaserFrames);
#endif
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
		(void)Across; // the HUD draws it a constant few pixels across (DrawHud)
		DotDrive = Drive;
	}
	// The thread: only where there is something to scatter it - air round the eye (polled), smoke later.
	SinceAirPoll += DeltaTime;
	if (SinceAirPoll >= AirPollSeconds)
	{
		SinceAirPoll = 0.0f;
		bAir = Guns->IsInAir();
	}
	AirShare = FMath::FInterpConstantTo(AirShare, bAir ? 1.0f : 0.0f, DeltaTime, 1.0f / AirFadeSeconds);
	LensAt = Lens.GetLocation();
	bThreadShown = AirShare > 0.0f;
	ThreadLevel = ThreadShare * AirShare * Drive;
}

void UOSLaserSightComponent::DrawHud(AHUD& Hud, UCanvas& Canvas) const
{
	if (!bLit || !Camera)
	{
		return;
	}
	const float Scale = Canvas.ClipY / 1080.0f;
	const FLinearColor Core = FLinearColor::LerpUsingHSV(RedColour, FLinearColor(1.0f, 0.85f, 0.8f), 0.35f);
	FVector2D Dot2D;
	const bool bDot = bDotHit && OSProjectToHud(Camera, Canvas, DotAt, false, Dot2D);
	// The thread first, under the dot: a pixel line from the lens, fading toward the dot (the beam spreads and thins).
	FVector2D Lens2D;
	if (bThreadShown && bDot && OSProjectToHud(Camera, Canvas, LensAt, true, Lens2D))
	{
		const float Pixel = FMath::Max(1.0f, FMath::RoundToFloat(Scale));
		const FVector2D Way = Dot2D - Lens2D;
		const int32 Steps = FMath::Clamp(FMath::CeilToInt32(float(Way.Size()) / (Pixel * 2.0f)), 1, 600);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float Share = float(Step) / float(Steps);
			const FVector2D At = Lens2D + Way * Share;
			Hud.DrawRect(FLinearColor(RedColour.R, RedColour.G, RedColour.B, FMath::Clamp(ThreadLevel * 2.4f * (1.0f - 0.7f * Share), 0.0f, 1.0f)),
				FMath::RoundToFloat(At.X / Pixel) * Pixel, FMath::RoundToFloat(At.Y / Pixel) * Pixel, Pixel, Pixel);
		}
	}
	if (bDot)
	{
		// The halo, then the core: whole pixels on the screen's grid, the drive's flutter in the halo's alpha only.
		const float CoreSize = FMath::Max(1.0f, FMath::RoundToFloat(HudDotPixels * Scale));
		const float HaloSize = CoreSize + 2.0f * FMath::RoundToFloat(HudHaloPixels * Scale);
		const float X = FMath::RoundToFloat(Dot2D.X);
		const float Y = FMath::RoundToFloat(Dot2D.Y);
		Hud.DrawRect(FLinearColor(RedColour.R, RedColour.G, RedColour.B, FMath::Clamp(0.22f * DotDrive, 0.0f, 1.0f)),
			X - FMath::RoundToFloat(HaloSize * 0.5f), Y - FMath::RoundToFloat(HaloSize * 0.5f), HaloSize, HaloSize);
		Hud.DrawRect(FLinearColor(Core.R, Core.G, Core.B, 1.0f), X - FMath::RoundToFloat(CoreSize * 0.5f), Y - FMath::RoundToFloat(CoreSize * 0.5f), CoreSize, CoreSize);
	}
}
