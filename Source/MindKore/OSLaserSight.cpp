#include "OSLaserSight.h"

#include "Camera/CameraComponent.h"
#include "Components/StaticMeshComponent.h"
#include "CanvasItem.h"
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
#include "OSSuitProjector.h"
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

namespace
{
	/** The 4 x 4 ordered dither (Bayer), 0..15. */
	constexpr int32 OSBayer4[4][4] = { { 0, 8, 2, 10 }, { 12, 4, 14, 6 }, { 3, 11, 1, 9 }, { 15, 7, 13, 5 } };

	/** The convex hull of the points, counter-clockwise on screen (Andrew's monotone chain). */
	TArray<FVector2D> OSHull(TArray<FVector2D> Points)
	{
		Points.Sort([](const FVector2D& A, const FVector2D& B) { return A.X < B.X || (A.X == B.X && A.Y < B.Y); });
		if (Points.Num() < 3)
		{
			return Points;
		}
		const auto Turn = [](const FVector2D& O, const FVector2D& A, const FVector2D& B) { return (A.X - O.X) * (B.Y - O.Y) - (A.Y - O.Y) * (B.X - O.X); };
		TArray<FVector2D> Hull;
		Hull.SetNum(Points.Num() * 2);
		int32 Count = 0;
		for (int32 Index = 0; Index < Points.Num(); ++Index)
		{
			while (Count >= 2 && Turn(Hull[Count - 2], Hull[Count - 1], Points[Index]) <= 0.0)
			{
				--Count;
			}
			Hull[Count++] = Points[Index];
		}
		for (int32 Index = Points.Num() - 2, Lower = Count + 1; Index >= 0; --Index)
		{
			while (Count >= Lower && Turn(Hull[Count - 2], Hull[Count - 1], Points[Index]) <= 0.0)
			{
				--Count;
			}
			Hull[Count++] = Points[Index];
		}
		Hull.SetNum(FMath::Max(Count - 1, 0));
		return Hull;
	}

	/** Where the segment From -> To is inside the convex polygon (Cyrus-Beck): [OutIn, OutOut] in 0..1; false if never. */
	bool OSInsideSpan(const TArray<FVector2D>& Hull, const FVector2D& From, const FVector2D& To, double& OutIn, double& OutOut)
	{
		if (Hull.Num() < 3)
		{
			return false;
		}
		// The hull's winding decides which side is out.
		double Area = 0.0;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const FVector2D& A = Hull[Index];
			const FVector2D& B = Hull[(Index + 1) % Hull.Num()];
			Area += A.X * B.Y - B.X * A.Y;
		}
		const double Wind = Area >= 0.0 ? 1.0 : -1.0;
		double Enter = 0.0;
		double Leave = 1.0;
		const FVector2D Way = To - From;
		for (int32 Index = 0; Index < Hull.Num(); ++Index)
		{
			const FVector2D& A = Hull[Index];
			const FVector2D Edge = Hull[(Index + 1) % Hull.Num()] - A;
			const FVector2D Outward = FVector2D(Edge.Y, -Edge.X) * Wind;
			const double Num = FVector2D::DotProduct(Outward, A - From);
			const double Den = FVector2D::DotProduct(Outward, Way);
			if (FMath::IsNearlyZero(Den))
			{
				if (Num < 0.0)
				{
					return false;
				}
				continue;
			}
			const double T = Num / Den;
			if (Den > 0.0)
			{
				Leave = FMath::Min(Leave, T);
			}
			else
			{
				Enter = FMath::Max(Enter, T);
			}
		}
		OutIn = Enter;
		OutOut = Leave;
		return Enter < Leave;
	}
}

void UOSLaserSightComponent::DrawHud(AHUD& Hud, UCanvas& Canvas) const
{
	if (!bLit || !Camera)
	{
		return;
	}
	// Light adds (the beam and the dot glow over the dark, never darken it), on the game's coarse pixel grid.
	const float Pixel = FMath::Max(1.0f, FMath::RoundToFloat(Canvas.ClipY / PixelLines));
	const auto Emit = [&Canvas, Pixel](int32 GX, int32 GY, const FLinearColor& Colour, float Level)
	{
		if (Level <= 0.0f)
		{
			return;
		}
		FCanvasTileItem Tile(FVector2D(GX * Pixel, GY * Pixel), FVector2D(Pixel, Pixel), FLinearColor(Colour.R * Level, Colour.G * Level, Colour.B * Level, 1.0f));
		Tile.BlendMode = SE_BLEND_Additive;
		Canvas.DrawItem(Tile);
	};
	const FLinearColor Hot(1.0f, 0.78f, 0.72f);
	const int64 DeviceTick = int64(GetWorld()->GetTimeSeconds() * OSProjectorClockHz);
	const double Now = GetWorld()->GetTimeSeconds();

	FVector2D Dot2D;
	const bool bDot = bDotHit && OSProjectToHud(Camera, Canvas, DotAt, false, Dot2D);
	FVector2D Lens2D;
	if (bThreadShown && bDot && Guns && OSProjectToHud(Camera, Canvas, LensAt, true, Lens2D))
	{
		// The gun is nearer than the beam: what of it lies over the gun's outline on screen is not drawn (the hull of
		// its meshes' boxes - the beam comes out of the gun's front, not over the slide).
		TArray<FVector2D> Points;
		OSGunScreenPoints(*Guns, Camera, Canvas, Points);
		const TArray<FVector2D> Hull = OSHull(MoveTemp(Points));
		double Hidden0 = 2.0;
		double Hidden1 = 2.0;
		if (!OSInsideSpan(Hull, Lens2D, Dot2D, Hidden0, Hidden1))
		{
			Hidden0 = Hidden1 = 2.0;
		}
		// The beam in grid steps along its longer axis: one pixel a step, no gaps, no slant smoothing.
		const FVector2D Way = Dot2D - Lens2D;
		const int32 Steps = FMath::Clamp(FMath::CeilToInt32(float(FMath::Max(FMath::Abs(Way.X), FMath::Abs(Way.Y))) / Pixel), 1, 1200);
		const bool bFlat = FMath::Abs(Way.X) >= FMath::Abs(Way.Y);
		const double Exit = Hidden0 <= 0.0 ? Hidden1 : 0.0;
		const int32 DustStep = FMath::FloorToInt32(float(Now) * DustDrift);
		int32 LastX = MIN_int32;
		int32 LastY = MIN_int32;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double Share = double(Step) / double(Steps);
			if (Share > Hidden0 && Share < Hidden1)
			{
				continue;
			}
			const FVector2D At = Lens2D + Way * Share;
			const int32 GX = FMath::FloorToInt32(float(At.X / Pixel));
			const int32 GY = FMath::FloorToInt32(float(At.Y / Pixel));
			if (GX == LastX && GY == LastY)
			{
				continue;
			}
			LastX = GX;
			LastY = GY;
			// Bright out of the lens, thinning toward the dot; it comes up over a few pixels where it leaves the gun.
			const float Along = float(Share);
			const float Emerge = FMath::Clamp(float((Share - Exit) * Steps) / 6.0f, 0.0f, 1.0f);
			const float Light = ThreadLevel * ThreadBrightness * Emerge * (0.3f + 0.7f * FMath::Pow(1.0f - Along, 1.6f));
			// Posterised into ThreadLevels with the ordered dither between: steps of light, as the low-res screen has.
			const float Scaled = FMath::Clamp(Light, 0.0f, 1.0f) * ThreadLevels;
			const int32 Base = FMath::FloorToInt32(Scaled);
			const float Threshold = (OSBayer4[GY & 3][GX & 3] + 0.5f) / 16.0f;
			const int32 Level = FMath::Min(Base + (Scaled - Base > Threshold ? 1 : 0), ThreadLevels);
			if (Level <= 0)
			{
				continue;
			}
			const float Value = float(Level) / ThreadLevels;
			Emit(GX, GY, Level == ThreadLevels ? FMath::Lerp(RedColour, Hot, 0.35f) : RedColour, Value);
			// The halo either side, near the lens only, on a checker (half the pixels): the beam's soft edge.
			if (Along < 0.5f && ((GX + GY) & 1) == 0)
			{
				const float Side = Value * 0.35f * (1.0f - Along * 2.0f);
				Emit(bFlat ? GX : GX - 1, bFlat ? GY - 1 : GY, RedColour, Side);
				Emit(bFlat ? GX : GX + 1, bFlat ? GY + 1 : GY, RedColour, Side);
			}
			// Dust in the beam: a few pixels glint and drift along it toward the dot.
			if (OSProjectorHash(Step - DustStep, 9137) < DustShare)
			{
				Emit(GX, GY, Hot, FMath::Min(1.0f, 0.5f + ThreadLevel * ThreadBrightness) * Emerge);
			}
		}
	}
	if (bDot)
	{
		// The dot as a sprite of the grid: a white-hot centre, a red cross, a dithered rim; the outer cross twinkles on
		// the device's ticks, the drive's flutter scales it all.
		const int32 CX = FMath::FloorToInt32(float(Dot2D.X / Pixel));
		const int32 CY = FMath::FloorToInt32(float(Dot2D.Y / Pixel));
		const float Drive = FMath::Clamp(DotDrive, 0.0f, 1.2f) * (0.92f + 0.08f * OSProjectorHash(DeviceTick, 4421));
		for (int32 DY = -3; DY <= 3; ++DY)
		{
			for (int32 DX = -3; DX <= 3; ++DX)
			{
				const int32 Manhattan = FMath::Abs(DX) + FMath::Abs(DY);
				const int32 Chebyshev = FMath::Max(FMath::Abs(DX), FMath::Abs(DY));
				float Level = 0.0f;
				FLinearColor Colour = RedColour;
				if (Manhattan == 0)
				{
					Level = 1.0f;
					Colour = Hot;
				}
				else if (Manhattan == 1)
				{
					Level = 0.95f;
					Colour = FMath::Lerp(RedColour, Hot, 0.25f);
				}
				else if (Chebyshev == 1)
				{
					Level = 0.55f;
				}
				else if (Manhattan == 2)
				{
					Level = 0.4f;
				}
				else if (Manhattan == 3 && Chebyshev == 2)
				{
					Level = ((CX + DX + CY + DY) & 1) == 0 ? 0.22f : 0.0f;
				}
				else if (Manhattan == 3 && Chebyshev == 3)
				{
					Level = OSProjectorHash(DeviceTick, 811) < 0.6f ? 0.14f : 0.05f;
				}
				Emit(CX + DX, CY + DY, Colour, Level * Drive);
			}
		}
	}
}
