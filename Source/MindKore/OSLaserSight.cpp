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
#include "Engine/Texture2D.h"

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
	// The dot: the first thing on the round's line within the gun's reach - out of the muzzle along the bore, as
	// UOSWeaponsComponent::Fire sends it (owner 23.10: the laser tells honestly where the round flies).
	const USceneComponent* Eye = Guns->ViewModel->GetAttachParent();
	FVector From;
	FVector Along;
	Guns->GetShotLine(Eye->GetComponentLocation(), Eye->GetForwardVector(), From, Along);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSLaserSight), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	++Traces;
	bDotHit = GetWorld()->LineTraceSingleByChannel(Hit, From, From + Along * Weapon->RangeCm, ECC_GameTraceChannel2, Params);
	DotAt = bDotHit ? FVector(Hit.ImpactPoint) + FVector(Hit.ImpactNormal) * DotLiftCm : From + Along * Weapon->RangeCm;
	// The flutter of the diode's drive with a rare drop, and grain: a new draw every frame.
	DotDrive = (FMath::FRand() < 0.03f ? 0.7f : 1.0f + FMath::FRandRange(-0.03f, 0.03f)) * (1.0f + FMath::FRandRange(-Grain, Grain));
	const float Drive = DotDrive;
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

namespace
{
	/** A colour's light times Level, opaque (the additive blend adds the colour as it is). */
	FLinearColor OSLit(const FLinearColor& Colour, float Level)
	{
		return FLinearColor(Colour.R * Level, Colour.G * Level, Colour.B * Level, 1.0f);
	}

	/** One triangle of the HUD's light, untextured, colour per corner (Gouraud). */
	void OSLightTri(TArray<FCanvasUVTri>& Tris, const FVector2D& P0, const FLinearColor& C0, const FVector2D& P1, const FLinearColor& C1,
		const FVector2D& P2, const FLinearColor& C2)
	{
		FCanvasUVTri& Tri = Tris.AddDefaulted_GetRef();
		Tri.V0_Pos = P0;
		Tri.V0_Color = C0;
		Tri.V1_Pos = P1;
		Tri.V1_Color = C1;
		Tri.V2_Pos = P2;
		Tri.V2_Color = C2;
		Tri.V0_UV = Tri.V1_UV = Tri.V2_UV = FVector2D::ZeroVector;
	}

	/** A soft band A -> B: its colour along the middle, nothing at its edges (half widths at either end). */
	void OSSoftBand(TArray<FCanvasUVTri>& Tris, const FVector2D& A, const FVector2D& B, float HalfA, float HalfB, const FLinearColor& ColourA,
		const FLinearColor& ColourB)
	{
		const FVector2D Way = B - A;
		const FVector2D Side = FVector2D(-Way.Y, Way.X).GetSafeNormal();
		if (Side.IsZero())
		{
			return;
		}
		const FLinearColor Dark(0.0f, 0.0f, 0.0f, 1.0f);
		for (const float Sign : { 1.0f, -1.0f })
		{
			const FVector2D EdgeA = A + Side * (HalfA * Sign);
			const FVector2D EdgeB = B + Side * (HalfB * Sign);
			OSLightTri(Tris, EdgeA, Dark, A, ColourA, EdgeB, Dark);
			OSLightTri(Tris, EdgeB, Dark, A, ColourA, B, ColourB);
		}
	}

	/** A soft ellipse: its colour at the centre, nothing at the rim. */
	void OSSoftDisc(TArray<FCanvasUVTri>& Tris, const FVector2D& Centre, float RadiusX, float RadiusY, const FLinearColor& Colour, int32 Sides = 18)
	{
		const FLinearColor Dark(0.0f, 0.0f, 0.0f, 1.0f);
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const float A0 = UE_TWO_PI * Side / Sides;
			const float A1 = UE_TWO_PI * (Side + 1) / Sides;
			OSLightTri(Tris, Centre, Colour, Centre + FVector2D(FMath::Cos(A0) * RadiusX, FMath::Sin(A0) * RadiusY), Dark,
				Centre + FVector2D(FMath::Cos(A1) * RadiusX, FMath::Sin(A1) * RadiusY), Dark);
		}
	}
}

void UOSLaserSightComponent::DrawHud(AHUD& Hud, UCanvas& Canvas) const
{
	if (!bLit || !Camera)
	{
		return;
	}
	// Light adds over the picture (never darkens it), smooth-shaded like the PS1/PS2's lights: soft bands and soft
	// discs, colour per corner, no pixel grid (owner 23.10: «ближе к psx-ps2 эстетике», not pixel art).
	const float Res = Canvas.ClipY / 1080.0f;
	const double Now = GetWorld()->GetTimeSeconds();
	const int64 DeviceTick = int64(Now * OSProjectorClockHz);
	const FLinearColor Hot(1.0f, 0.82f, 0.74f);
	const FLinearColor CoreTint = FMath::Lerp(RedColour, Hot, 0.45f);
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(512);
	// A world size in pixels where a point is: the view's scale over its depth (the gun's own field of view for the lens).
	const FTransform View = Camera->GetComponentTransform();
	const auto PixelsPerCm = [&Canvas, &View, this](const FVector& World, bool bFirstPerson)
	{
		const float FieldOfView = bFirstPerson && Camera->bEnableFirstPersonFieldOfView ? Camera->FirstPersonFieldOfView : Camera->FieldOfView;
		const float Scale = Canvas.ClipX * 0.5f / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FieldOfView, 5.0f, 170.0f) * 0.5f));
		return Scale / FMath::Max(1.0f, float(View.InverseTransformPositionNoScale(World).X));
	};

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
		const FVector2D Way = Dot2D - Lens2D;
		const float Length = float(Way.Size());
		const double Exit = Hidden0 <= 0.0 ? FMath::Min(Hidden1, 1.0) : 0.0;
		// The beam's real thickness on screen at either end: fat by the lens, a hair at the dot - never thinner than BeamMinPixels.
		const float HalfLens = FMath::Clamp(ThreadCm * 0.5f * PixelsPerCm(LensAt, true), BeamMinPixels * 0.5f * Res, 7.0f * Res);
		const float HalfDot = FMath::Clamp(ThreadCm * 0.5f * PixelsPerCm(DotAt, false), BeamMinPixels * 0.5f * Res, 7.0f * Res);
		const float Base = ThreadLevel * ThreadBrightness;
		// The light along the beam: brightest out of the gun, thinning toward the dot, coming up over a few pixels as it
		// leaves the gun, and the haze it lights moving through it (two slow bands sliding toward the dot).
		const auto LightAt = [&](double Share)
		{
			const float T = float(Share);
			const float Emerge = FMath::Clamp(float(Share - Exit) * Length / (14.0f * Res), 0.0f, 1.0f);
			const float Haze = 0.82f + 0.12f * FMath::Sin(38.0f * T - 5.0f * float(Now)) + 0.08f * FMath::Sin(91.0f * T - 9.0f * float(Now) + 1.7f);
			return Base * Emerge * Haze * (0.28f + 0.72f * FMath::Pow(1.0f - T, 1.5f));
		};
		const auto DrawSpan = [&](double From, double To)
		{
			if (To <= From)
			{
				return;
			}
			const int32 Pieces = FMath::Clamp(FMath::CeilToInt32(float(To - From) * Length / (12.0f * Res)), 1, 48);
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				const double S0 = FMath::Lerp(From, To, double(Piece) / Pieces);
				const double S1 = FMath::Lerp(From, To, double(Piece + 1) / Pieces);
				const FVector2D A = Lens2D + Way * S0;
				const FVector2D B = Lens2D + Way * S1;
				const float W0 = FMath::Lerp(HalfLens, HalfDot, float(S0));
				const float W1 = FMath::Lerp(HalfLens, HalfDot, float(S1));
				const float L0 = LightAt(S0);
				const float L1 = LightAt(S1);
				// The halo wide and dim, the body, then the hot core: three soft bands make the glow's falloff.
				OSSoftBand(Tris, A, B, W0 * 5.0f + 2.0f * Res, W1 * 5.0f + 2.0f * Res, OSLit(RedColour, 0.16f * L0), OSLit(RedColour, 0.16f * L1));
				OSSoftBand(Tris, A, B, W0 * 2.2f + Res, W1 * 2.2f + Res, OSLit(RedColour, 0.5f * L0), OSLit(RedColour, 0.5f * L1));
				OSSoftBand(Tris, A, B, W0, W1, OSLit(CoreTint, L0), OSLit(CoreTint, L1));
			}
		};
		DrawSpan(0.0, FMath::Min(Hidden0, 1.0));
		DrawSpan(FMath::Max(Hidden1, 0.0), 1.0);
		// Where it leaves the gun the haze by the muzzle glows most.
		if (Exit < 1.0)
		{
			const FVector2D Out = Lens2D + Way * Exit;
			OSSoftDisc(Tris, Out, HalfLens * 6.0f + 4.0f * Res, HalfLens * 4.0f + 3.0f * Res, OSLit(RedColour, 0.35f * Base));
		}
		// Dust in the beam: motes catch the light and drift along it toward the dot, each its own speed and twinkle.
		for (int32 Mote = 0; Mote < DustMotes; ++Mote)
		{
			const float Seed = OSProjectorHash(Mote, 9137);
			const float Phase = FMath::Frac(Seed + float(Now) * DustDrift * (0.6f + 0.8f * OSProjectorHash(Mote, 271)));
			if (Phase < Exit || (Phase > Hidden0 && Phase < Hidden1))
			{
				continue;
			}
			const float Twinkle = FMath::Sin(UE_PI * Phase) * (0.6f + 0.4f * FMath::Sin(float(Now) * (7.0f + 9.0f * Seed)));
			const float Half = FMath::Lerp(HalfLens, HalfDot, Phase) * 1.6f + 1.2f * Res;
			OSSoftDisc(Tris, Lens2D + Way * Phase, Half, Half, OSLit(Hot, FMath::Clamp(Base * 1.5f, 0.0f, 1.0f) * FMath::Max(0.0f, Twinkle)), 8);
		}
	}
	if (bDot)
	{
		// The dot: a white-hot centre, a red glow, a wide dim halo and a faint flat streak across (the lens it is seen
		// through); a little smaller far off, all of it on the diode's flutter and the device's ticks.
		const float Far = FMath::Clamp(FMath::Sqrt(400.0f / FMath::Max(1.0f, float(FVector::Dist(View.GetLocation(), DotAt)))), 0.55f, 1.25f);
		const float Size = Res * Far;
		const float Drive = FMath::Clamp(DotDrive, 0.0f, 1.2f) * (0.93f + 0.07f * OSProjectorHash(DeviceTick, 4421));
		OSSoftDisc(Tris, Dot2D, DotHaloPixels * Size, DotHaloPixels * Size, OSLit(RedColour, 0.22f * Drive), 24);
		OSSoftDisc(Tris, Dot2D, DotStreakPixels * Size, 1.6f * Size + 0.8f, OSLit(RedColour, 0.3f * Drive), 16);
		OSSoftDisc(Tris, Dot2D, DotGlowPixels * Size, DotGlowPixels * Size, OSLit(RedColour, 0.85f * Drive), 20);
		OSSoftDisc(Tris, Dot2D, DotCorePixels * Size + 0.5f, DotCorePixels * Size + 0.5f, OSLit(Hot, Drive), 12);
	}
	// The canvas' own white texture: GWhiteTexture lives in RenderCore (not linked), and a triangle item asserts on none.
	const FTexture* White = Canvas.DefaultTexture ? Canvas.DefaultTexture->GetResource() : nullptr;
	if (Tris.Num() > 0 && White)
	{
		FCanvasTriangleItem Light(Tris, White);
		Light.BlendMode = SE_BLEND_Additive;
		Canvas.DrawItem(Light);
	}
}
