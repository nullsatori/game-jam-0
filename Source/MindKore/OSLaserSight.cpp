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

void UOSLaserSightComponent::Wake(bool bWake)
{
	bAwake = bWake;
	// Off: the tick runs on through the fade (StepDrive) and stops itself.
	if (bWake && !IsComponentTickEnabled())
	{
		SetComponentTickEnabled(true);
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
	bLit = bDotHit = bDotSeen = bThreadShown = false;
	BeamSpans.Reset();
	OnLevel = OnVelocity = 0.0f;
	KickLeft = 0.0f;
}

float UOSLaserSightComponent::StepDrive(float DeltaTime, bool bWant, bool bShot)
{
	// On: a spring to full light, under-damped by OnOvershoot (the supply's capacitor charging past and settling);
	// off: a plain decay over OffSeconds.
	if (bWant)
	{
		const float Omega = 4.0f / OnSeconds;
		const float LogOver = FMath::Loge(FMath::Clamp(OnOvershoot, 0.001f, 0.5f));
		const float Zeta = -LogOver / FMath::Sqrt(UE_PI * UE_PI + LogOver * LogOver);
		OnVelocity += (Omega * Omega * (1.0f - OnLevel) - 2.0f * Zeta * Omega * OnVelocity) * DeltaTime;
		OnLevel += OnVelocity * DeltaTime;
	}
	else
	{
		OnVelocity = 0.0f;
		OnLevel = FMath::Max(0.0f, OnLevel - DeltaTime / OffSeconds);
	}
	// The drive's slow drift (two incommensurate slow waves) and its ripple, which beats against the frame rate.
	DriftPhase += DeltaTime;
	RipplePhase = FMath::Fmod(RipplePhase + DeltaTime * DriveRippleHz * UE_TWO_PI, UE_TWO_PI);
	const float Drift = DriveDrift * (0.6f * FMath::Sin(DriftPhase * 2.3f) + 0.4f * FMath::Sin(DriftPhase * 0.7f + 1.1f));
	const float Ripple = DriveRipple * FMath::Sin(RipplePhase);
	// A dropout: at once down, back up along an exponential (DropSeconds to within a few percent).
	SinceDrop += DeltaTime;
	if (DropsPerSecond > 0.0f && FMath::FRand() < DropsPerSecond * DeltaTime)
	{
		SinceDrop = 0.0f;
	}
	const float Drop = DropDepth * FMath::Exp(-SinceDrop / (DropSeconds * 0.3f));
	// The shot: the contacts rattle, chaotically, dying out over KickRattleSeconds.
	if (bShot)
	{
		KickLeft = KickRattleSeconds;
	}
	float Rattle = 0.0f;
	if (KickLeft > 0.0f)
	{
		KickLeft = FMath::Max(0.0f, KickLeft - DeltaTime);
		Rattle = KickRattle * (KickLeft / KickRattleSeconds) * FMath::FRand();
	}
	return FMath::Max(0.0f, OnLevel * (1.0f + Drift + Ripple) * (1.0f - Drop) * (1.0f - Rattle));
}

namespace
{
	/** Does the segment A -> B pass through the box (its own frame, shrunk a hair so a point on a face counts as outside)? */
	bool OSSegmentThroughBox(const FTransform& ToLocal, const FBox& Box, const FVector& A, const FVector& B)
	{
		const FVector LA = ToLocal.TransformPosition(A);
		const FVector LB = ToLocal.TransformPosition(B);
		const FBox Shrunk = Box.ExpandBy(-0.3);
		double Enter = 0.0;
		double Leave = 1.0;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const double Way = LB[Axis] - LA[Axis];
			if (FMath::IsNearlyZero(Way))
			{
				if (LA[Axis] < Shrunk.Min[Axis] || LA[Axis] > Shrunk.Max[Axis])
				{
					return false;
				}
				continue;
			}
			double T0 = (Shrunk.Min[Axis] - LA[Axis]) / Way;
			double T1 = (Shrunk.Max[Axis] - LA[Axis]) / Way;
			if (T0 > T1)
			{
				Swap(T0, T1);
			}
			Enter = FMath::Max(Enter, T0);
			Leave = FMath::Min(Leave, T1);
			if (Enter > Leave)
			{
				return false;
			}
		}
		return true;
	}

	/** Value noise, 0..1, on a unit lattice (OSProjectorHash at the corners, smooth between). */
	float OSNoise3(const FVector& P)
	{
		const FVector Cell(FMath::Floor(P.X), FMath::Floor(P.Y), FMath::Floor(P.Z));
		const FVector F = P - Cell;
		const FVector S(F.X * F.X * (3.0 - 2.0 * F.X), F.Y * F.Y * (3.0 - 2.0 * F.Y), F.Z * F.Z * (3.0 - 2.0 * F.Z));
		const int64 X = int64(Cell.X);
		const int64 Y = int64(Cell.Y);
		const int64 Z = int64(Cell.Z);
		const auto Corner = [X, Y, Z](int64 DX, int64 DY, int64 DZ)
		{
			return OSProjectorHash((X + DX) * 1619 + (Y + DY) * 31337, (Z + DZ) * 6971 + 977);
		};
		const float X00 = FMath::Lerp(Corner(0, 0, 0), Corner(1, 0, 0), float(S.X));
		const float X10 = FMath::Lerp(Corner(0, 1, 0), Corner(1, 1, 0), float(S.X));
		const float X01 = FMath::Lerp(Corner(0, 0, 1), Corner(1, 0, 1), float(S.X));
		const float X11 = FMath::Lerp(Corner(0, 1, 1), Corner(1, 1, 1), float(S.X));
		return FMath::Lerp(FMath::Lerp(X00, X10, float(S.Y)), FMath::Lerp(X01, X11, float(S.Y)), float(S.Z));
	}
}

void UOSLaserSightComponent::FindBeamSpans(const FVector& EyeAt)
{
	// The parts of the gun in view that could stand between the eye and the beam: their boxes, in their own frames.
	TArray<const UStaticMeshComponent*, TInlineAllocator<8>> Parts;
	Parts.Add(Guns->ViewModel);
	Parts.Add(Guns->ActionInView);
	Parts.Add(Guns->MagInView);
	for (const UStaticMeshComponent* Module : Guns->ModulesInView)
	{
		Parts.Add(Module);
	}
	TArray<TPair<FTransform, FBox>, TInlineAllocator<8>> Boxes;
	for (const UStaticMeshComponent* Part : Parts)
	{
		if (Part && Part->IsVisible() && Part->GetStaticMesh())
		{
			Boxes.Emplace(Part->GetComponentTransform().Inverse(), Part->GetStaticMesh()->GetBoundingBox());
		}
	}
	// Samples dense by the lens (where the gun is), sparse far out; the visible runs become spans.
	constexpr int32 Samples = 40;
	BeamSpans.Reset();
	float Open = -1.0f;
	for (int32 Index = 0; Index <= Samples; ++Index)
	{
		const float T = FMath::Pow(float(Index) / Samples, 2.5f);
		const FVector At = LensAt + (BeamEnd - LensAt) * T;
		bool bHidden = false;
		for (const TPair<FTransform, FBox>& Box : Boxes)
		{
			if (OSSegmentThroughBox(Box.Key, Box.Value, EyeAt, At))
			{
				bHidden = true;
				break;
			}
		}
		if (!bHidden && Open < 0.0f)
		{
			Open = T;
		}
		else if (bHidden && Open >= 0.0f)
		{
			BeamSpans.Emplace(Open, T);
			Open = -1.0f;
		}
	}
	if (Open >= 0.0f)
	{
		BeamSpans.Emplace(Open, 1.0f);
	}
}

void UOSLaserSightComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
#if !UE_BUILD_SHIPPING
	const FOSAttachCostScope Timed(GOSAttachCost.LaserSeconds, GOSAttachCost.LaserMaxSeconds, GOSAttachCost.LaserFrames);
#endif
	FTransform Lens;
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	const bool bWant = bAwake && Weapon && Dot && Guns->GetLaserLens(Lens) && Guns->ViewModel && Guns->ViewModel->GetAttachParent();
	if (!bWant)
	{
		// The light dies over OffSeconds where it was; then nothing, and no tick until the next Wake.
		if (OnLevel <= 0.0f || !bLit)
		{
			Hide();
			SetComponentTickEnabled(false);
			return;
		}
		DotDrive = StepDrive(DeltaTime, false, false);
		ThreadLevel = ThreadShare * AirShare * DotDrive;
		return;
	}
	bLit = true;
	const bool bShot = Weapon->SinceShot < LastSinceShot;
	LastSinceShot = Weapon->SinceShot;
	DotDrive = StepDrive(DeltaTime, true, bShot);
	// The dot: the first thing on the round's line within the gun's reach - out of the muzzle along the bore as the
	// gun is drawn, as UOSWeaponsComponent::Fire sends it (owner 23.10: the laser tells honestly where the round flies).
	const USceneComponent* Eye = Guns->ViewModel->GetAttachParent();
	const FVector EyeAt = Eye->GetComponentLocation();
	FVector From;
	FVector Along;
	Guns->GetShotLine(EyeAt, Eye->GetForwardVector(), From, Along);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSLaserSight), /*bTraceComplex=*/false, GetOwner());
	FHitResult Hit;
	++Traces;
	bDotHit = GetWorld()->LineTraceSingleByChannel(Hit, From, From + Along * Weapon->RangeCm, ECC_GameTraceChannel2, Params);
	DotAt = bDotHit ? FVector(Hit.ImpactPoint) + FVector(Hit.ImpactNormal) * DotLiftCm : From + Along * Weapon->RangeCm;
	bDotSeen = false;
	if (bDotHit)
	{
		// A disc on the surface: its radius grows with the range, the slant stretches it along the beam's lie.
		const FVector Normal = FVector(Hit.ImpactNormal).GetSafeNormal();
		const float Range = float(FVector::Dist(From, DotAt));
		const float Radius = DotRadiusCm + DotSpreadCmPerM * Range / 100.0f;
		const float Cos = FMath::Max(FMath::Abs(float(Along | Normal)), 0.02f);
		FVector U = (Along - Normal * (Along | Normal)).GetSafeNormal();
		if (U.IsNearlyZero())
		{
			U = FVector::CrossProduct(Normal, FMath::Abs(Normal.Z) < 0.9 ? FVector::UpVector : FVector::RightVector).GetSafeNormal();
		}
		DotAxisU = U * Radius * FMath::Min(1.0f / Cos, DotStretchMax);
		DotAxisV = FVector::CrossProduct(Normal, U) * Radius;
		// Seen from the eye? The round's line starts at the muzzle, the eye is elsewhere: an edge can hide the dot.
		FHitResult Sight;
		++Traces;
		bDotSeen = !GetWorld()->LineTraceSingleByChannel(Sight, EyeAt, DotAt - FVector(Hit.ImpactNormal) * (DotLiftCm * 0.5f), ECC_Visibility, Params)
			|| Sight.Distance > FVector::Dist(EyeAt, DotAt) - 2.0f;
	}
	// The beam: only where there is something to scatter it - air round the eye (polled), smoke later.
	SinceAirPoll += DeltaTime;
	if (SinceAirPoll >= AirPollSeconds)
	{
		SinceAirPoll = 0.0f;
		bAir = Guns->IsInAir();
	}
	AirShare = FMath::FInterpConstantTo(AirShare, bAir ? 1.0f : 0.0f, DeltaTime, 1.0f / AirFadeSeconds);
	LensAt = Lens.GetLocation();
	bThreadShown = AirShare > 0.0f && bDotHit;
	ThreadLevel = ThreadShare * AirShare * DotDrive;
	if (bThreadShown)
	{
		// From the lens to the dot in the gun's own space: the drawn gun's projection (OSProjectToHud, first person)
		// puts that end exactly on the dot - and the parts of the gun hide what they stand in front of.
		BeamEnd = Guns->DrawnToGun(DotAt);
		FindBeamSpans(EyeAt);
	}
	else
	{
		BeamSpans.Reset();
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

	/** A soft ellipse on two half axes: its colour at the centre, nothing at the rim. */
	void OSSoftEllipse(TArray<FCanvasUVTri>& Tris, const FVector2D& Centre, const FVector2D& AxisA, const FVector2D& AxisB, const FLinearColor& Colour, int32 Sides)
	{
		const FLinearColor Dark(0.0f, 0.0f, 0.0f, 1.0f);
		for (int32 Side = 0; Side < Sides; ++Side)
		{
			const float A0 = UE_TWO_PI * Side / Sides;
			const float A1 = UE_TWO_PI * (Side + 1) / Sides;
			OSLightTri(Tris, Centre, Colour, Centre + AxisA * FMath::Cos(A0) + AxisB * FMath::Sin(A0), Dark,
				Centre + AxisA * FMath::Cos(A1) + AxisB * FMath::Sin(A1), Dark);
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
	// discs, colour per corner, no pixel grid (owner 23.10: «ближе к psx-ps2 эстетике», not pixel art), and little
	// of it (owner 23.10: «subtle, industrial, gritty»).
	const float Res = Canvas.ClipY / 1080.0f;
	const double Now = GetWorld()->GetTimeSeconds();
	const int64 DeviceTick = int64(Now * OSProjectorClockHz);
	const float TickShare = float(Now * OSProjectorClockHz - double(DeviceTick));
	const FLinearColor Hot(1.0f, 0.82f, 0.74f);
	const FLinearColor CoreTint = FMath::Lerp(RedColour, Hot, 0.45f);
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(512);
	const FTransform View = Camera->GetComponentTransform();
	// A world size in pixels where a point is: the view's scale over its depth (the gun's own field of view for the lens).
	const auto PixelsPerCm = [&Canvas, &View, this](const FVector& World, bool bFirstPerson)
	{
		const float FieldOfView = bFirstPerson && Camera->bEnableFirstPersonFieldOfView ? Camera->FirstPersonFieldOfView : Camera->FieldOfView;
		const float Scale = Canvas.ClipX * 0.5f / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FieldOfView, 5.0f, 170.0f) * 0.5f));
		return Scale / FMath::Max(1.0f, float(View.InverseTransformPositionNoScale(World).X));
	};

	FVector2D Dot2D;
	const bool bDot = bDotHit && bDotSeen && OSProjectToHud(Camera, Canvas, DotAt, false, Dot2D);
	if (bThreadShown && BeamSpans.Num() > 0)
	{
		// A hard thin line out of the lens in a faint sheath: the ends of each visible span in the drawn gun's
		// projection; its light falls from the lens to the dot and varies with the haze it passes through - noise fixed
		// in the world, drifting slowly, which the beam sweeps across as the gun sways (no crawl of its own).
		const float HalfLens = FMath::Clamp(ThreadCm * 0.5f * PixelsPerCm(LensAt, true), BeamMinPixels * 0.5f * Res, 3.0f * Res);
		const float HalfDot = BeamMinPixels * 0.5f * Res;
		const float Base = ThreadLevel * ThreadBrightness;
		const FVector Drift = FVector(0.3, 0.9, 0.2) * (HazeDriftCmPerSec * Now);
		const auto HazeAt = [&](float T)
		{
			const FVector P = (LensAt + (BeamEnd - LensAt) * T + Drift) / HazeCellCm;
			const float Coarse = OSNoise3(P * 0.35);
			const float Fine = OSNoise3(P + FVector(37.0, 11.0, 5.0));
			return 1.0f - HazeGrit + HazeGrit * 2.0f * (0.6f * Coarse + 0.4f * Fine);
		};
		for (const FVector2f& Span : BeamSpans)
		{
			FVector2D A2;
			FVector2D B2;
			if (!OSProjectToHud(Camera, Canvas, LensAt + (BeamEnd - LensAt) * Span.X, true, A2)
				|| !OSProjectToHud(Camera, Canvas, LensAt + (BeamEnd - LensAt) * Span.Y, true, B2))
			{
				continue;
			}
			const float Length = float((B2 - A2).Size());
			const int32 Pieces = FMath::Clamp(FMath::CeilToInt32(Length / (8.0f * Res)), 1, 96);
			for (int32 Piece = 0; Piece < Pieces; ++Piece)
			{
				const float S0 = float(Piece) / Pieces;
				const float S1 = float(Piece + 1) / Pieces;
				const float T0 = FMath::Lerp(Span.X, Span.Y, S0);
				const float T1 = FMath::Lerp(Span.X, Span.Y, S1);
				const auto LightAt = [&](float T) { return Base * HazeAt(T) * (0.35f + 0.65f * FMath::Pow(1.0f - T, 1.3f)); };
				const float L0 = LightAt(T0);
				const float L1 = LightAt(T1);
				const FVector2D P0 = FMath::Lerp(A2, B2, double(S0));
				const FVector2D P1 = FMath::Lerp(A2, B2, double(S1));
				const float W0 = FMath::Lerp(HalfLens, HalfDot, T0);
				const float W1 = FMath::Lerp(HalfLens, HalfDot, T1);
				OSSoftBand(Tris, P0, P1, W0 * 2.6f + Res, W1 * 2.6f + Res, OSLit(RedColour, 0.12f * L0), OSLit(RedColour, 0.12f * L1));
				OSSoftBand(Tris, P0, P1, W0, W1, OSLit(CoreTint, 0.8f * L0), OSLit(CoreTint, 0.8f * L1));
			}
		}
	}
	if (bDot)
	{
		// The disc as it lies on the surface, never under DotCorePixels across: a small hard centre in a tight red
		// glow, a faint halo, and a few grains of speckle that re-roll on the device's ticks (blended between).
		FVector2D AxisU;
		FVector2D AxisV;
		if (!OSProjectToHud(Camera, Canvas, DotAt + DotAxisU, false, AxisU) || !OSProjectToHud(Camera, Canvas, DotAt + DotAxisV, false, AxisV))
		{
			AxisU = Dot2D + FVector2D(1.0, 0.0);
			AxisV = Dot2D + FVector2D(0.0, 1.0);
		}
		AxisU -= Dot2D;
		AxisV -= Dot2D;
		const float MinPx = DotCorePixels * Res;
		const auto AtLeast = [MinPx](const FVector2D& Axis, const FVector2D& Fallback)
		{
			const double Size = Axis.Size();
			return Size < 0.01 ? Fallback * MinPx : Axis * FMath::Max(1.0, MinPx / Size);
		};
		AxisU = AtLeast(AxisU, FVector2D(1.0, 0.0));
		AxisV = AtLeast(AxisV, FVector2D(0.0, 1.0));
		const auto Padded = [](const FVector2D& Axis, float Pad) { return Axis * ((Axis.Size() + Pad) / Axis.Size()); };
		const float Drive = FMath::Clamp(DotDrive, 0.0f, 1.2f);
		if (DotHaloPixels > 0.0f)
		{
			OSSoftEllipse(Tris, Dot2D, Padded(AxisU, DotHaloPixels * Res), Padded(AxisV, DotHaloPixels * Res), OSLit(RedColour, 0.1f * Drive), 12);
		}
		OSSoftEllipse(Tris, Dot2D, Padded(AxisU, DotGlowPixels * Res), Padded(AxisV, DotGlowPixels * Res), OSLit(RedColour, 0.7f * Drive), 10);
		OSSoftEllipse(Tris, Dot2D, Padded(AxisU, 0.5f), Padded(AxisV, 0.5f), OSLit(CoreTint, Drive), 8);
		if (Speckle > 0.0f)
		{
			for (int32 Grain = 0; Grain < 4; ++Grain)
			{
				for (int32 Which = 0; Which < 2; ++Which)
				{
					const int64 Tick = DeviceTick + Which;
					const float Weight = Which == 0 ? 1.0f - TickShare : TickShare;
					const float Angle = UE_TWO_PI * OSProjectorHash(Tick * 7 + Grain, 3301);
					const float Out = 0.3f + 0.9f * OSProjectorHash(Tick * 7 + Grain, 4409);
					const FVector2D At = Dot2D + (AxisU * FMath::Cos(Angle) + AxisV * FMath::Sin(Angle)) * Out;
					const FVector2D Tiny(FMath::Max(0.8f * Res, 0.8f), 0.0);
					OSSoftEllipse(Tris, At, Tiny, FVector2D(0.0, Tiny.X), OSLit(Hot, Speckle * Drive * Weight), 6);
				}
			}
		}
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
