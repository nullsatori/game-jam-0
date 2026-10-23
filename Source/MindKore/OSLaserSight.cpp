#include "OSLaserSight.h"

#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
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
	// The dot's spill on the surface: a tiny red light, no shadow, no bounce (the torch's reasons, OSWeaponAttach.cpp).
	DotLight = NewObject<UPointLightComponent>(GetOwner(), TEXT("LaserDotLight"));
	DotLight->SetupAttachment(GetOwner()->GetRootComponent());
	DotLight->SetUsingAbsoluteLocation(true);
	DotLight->SetUsingAbsoluteRotation(true);
	DotLight->CastShadows = false;
	DotLight->bAllowMegaLights = false;
	DotLight->IndirectLightingIntensity = 0.0f;
	DotLight->VolumetricScatteringIntensity = 0.0f;
	DotLight->IntensityUnits = ELightUnits::Candelas;
	DotLight->bUseInverseSquaredFalloff = true;
	DotLight->SetAttenuationRadius(DotLightRadiusCm);
	DotLight->SetSourceRadius(0.5f);
	DotLight->SetLightColor(RedColour);
	DotLight->SetIntensity(0.0f);
	DotLight->SetVisibleFlag(false);
	DotLight->RegisterComponent();
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
	if (DotLight && DotLight->IsVisible())
	{
		DotLight->SetVisibility(false);
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
	DeltaSecondsNow = DeltaTime;
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
		if (DotLight)
		{
			DotLight->SetIntensity(DotLightCandela * DotDrive);
		}
		return;
	}
	bLit = true;
	const bool bShot = Weapon->SinceShot < LastSinceShot;
	LastSinceShot = Weapon->SinceShot;
	DotDrive = StepDrive(DeltaTime, true, bShot);
	// The beam: only where there is something to scatter it - air round the eye (polled), smoke later.
	SinceAirPoll += DeltaTime;
	if (SinceAirPoll >= AirPollSeconds)
	{
		SinceAirPoll = 0.0f;
		bAir = Guns->IsInAir();
	}
	AirShare = FMath::FInterpConstantTo(AirShare, bAir ? 1.0f : 0.0f, DeltaTime, 1.0f / AirFadeSeconds);
	ThreadLevel = ThreadShare * AirShare * DotDrive;
	// The geometry from the camera as it stands now (the probes read it); DrawHud does it again if the camera has
	// turned since (the mouse's turn lands on the camera after this tick).
	Refresh();
}

void UOSLaserSightComponent::Refresh()
{
	FTransform Lens;
	const FOSWeapon* Weapon = Guns ? Guns->GetWeapon() : nullptr;
	if (!Weapon || !Guns->GetLaserLens(Lens) || !Guns->ViewModel || !Guns->ViewModel->GetAttachParent())
	{
		return;
	}
	const USceneComponent* Eye = Guns->ViewModel->GetAttachParent();
	RefreshedView = Eye->GetComponentTransform();
	const FVector EyeAt = RefreshedView.GetLocation();
	// The dot: the first thing on the round's line within the gun's reach - out of the muzzle along the bore as the
	// gun is drawn, as UOSWeaponsComponent::Fire sends it (owner 23.10: the laser tells honestly where the round flies).
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
		// The speckle boils with the dot's travel over the surface and barely at rest.
		Boil += float(FVector::Dist(LastDotAt, DotAt)) / SpeckleBoilCm + DeltaSecondsNow / SpeckleRestSeconds;
		DeltaSecondsNow = 0.0f; // the rest counts once a frame, whichever pass runs first
		LastDotAt = DotAt;
		if (Boil > 1.0e6f)
		{
			Boil = 0.0f;
		}
	}
	if (DotLight)
	{
		const bool bSpill = bDotHit && DotLightCandela > 0.0f;
		if (bSpill)
		{
			DotLight->SetWorldLocation(DotAt + FVector(Hit.ImpactNormal) * 1.0);
			DotLight->SetIntensity(DotLightCandela * DotDrive);
		}
		if (DotLight->IsVisible() != bSpill)
		{
			DotLight->SetVisibility(bSpill);
		}
	}
	LensAt = Lens.GetLocation();
	bThreadShown = AirShare > 0.0f && bDotHit;
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

	/**
	 * The low-res frame of the light: texels of the screen's coarse grid, their light summed as it comes; drawn each as
	 * a tent to its neighbours' centres (the tents sum to one: the bilinear upscale of that frame).
	 */
	struct FOSTexelFrame
	{
		float Texel = 5.0f;
		TMap<int64, FLinearColor> Light;

		static int64 KeyOf(int32 X, int32 Y) { return (int64(X) << 32) ^ int64(uint32(Y)); }

		void Add(int32 X, int32 Y, const FLinearColor& Colour, float Level)
		{
			if (Level <= 0.001f)
			{
				return;
			}
			FLinearColor& Sum = Light.FindOrAdd(KeyOf(X, Y), FLinearColor(0.0f, 0.0f, 0.0f, 1.0f));
			Sum.R += Colour.R * Level;
			Sum.G += Colour.G * Level;
			Sum.B += Colour.B * Level;
		}

		void Draw(TArray<FCanvasUVTri>& Tris) const
		{
			const FLinearColor Dark(0.0f, 0.0f, 0.0f, 1.0f);
			for (const TPair<int64, FLinearColor>& Each : Light)
			{
				const int32 X = int32(Each.Key >> 32);
				const int32 Y = int32(uint32(Each.Key & 0xFFFFFFFF));
				const FVector2D C((X + 0.5) * Texel, (Y + 0.5) * Texel);
				const FVector2D L = C - FVector2D(Texel, 0.0);
				const FVector2D R = C + FVector2D(Texel, 0.0);
				const FVector2D U = C - FVector2D(0.0, Texel);
				const FVector2D D = C + FVector2D(0.0, Texel);
				OSLightTri(Tris, C, Each.Value, L, Dark, U, Dark);
				OSLightTri(Tris, C, Each.Value, U, Dark, R, Dark);
				OSLightTri(Tris, C, Each.Value, R, Dark, D, Dark);
				OSLightTri(Tris, C, Each.Value, D, Dark, L, Dark);
			}
		}
	};
}

void UOSLaserSightComponent::DrawHud(AHUD& Hud, UCanvas& Canvas)
{
	if (!bLit || !Camera)
	{
		return;
	}
	// The camera has turned with the mouse since the tick: the lens, the dot and the spans from where it stands now.
	if (!Camera->GetComponentTransform().Equals(RefreshedView, 0.001))
	{
		Refresh();
	}
	// Light adds over the picture (never darkens it) on the game's coarse grid - not pixel art (owner 23.10): the
	// brightness is continuous and each texel a soft tent, as a low-res frame comes up through a bilinear upscale.
	FOSTexelFrame Frame;
	Frame.Texel = FMath::Max(1.0f, FMath::RoundToFloat(Canvas.ClipY / GridLines));
	const float Texel = Frame.Texel;
	const double Now = GetWorld()->GetTimeSeconds();
	const FLinearColor Hot(1.0f, 0.82f, 0.74f);
	const FLinearColor CoreTint = FMath::Lerp(RedColour, Hot, 0.45f);

	FVector2D Dot2D;
	const bool bDot = bDotHit && bDotSeen && OSProjectToHud(Camera, Canvas, DotAt, false, Dot2D);
	if (bThreadShown && BeamSpans.Num() > 0)
	{
		// One texel wide, Wu-weighted across the rows (a hair's line steps softly as it moves), a dim sheath either
		// side; its light falls from the lens to the dot and varies with the haze it passes through - noise fixed in the
		// world, drifting slowly, which the beam sweeps across as the gun sways; the densest haze glints.
		const float Base = ThreadLevel * ThreadBrightness;
		const FVector Drift = FVector(0.3, 0.9, 0.2) * (HazeDriftCmPerSec * Now);
		const auto HazeAt = [&](float T)
		{
			const FVector P = (LensAt + (BeamEnd - LensAt) * T + Drift) / HazeCellCm;
			return 0.6f * OSNoise3(P * 0.35) + 0.4f * OSNoise3(P + FVector(37.0, 11.0, 5.0));
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
			const FVector2D A = A2 / Texel;
			const FVector2D B = B2 / Texel;
			const FVector2D Way = B - A;
			const bool bFlat = FMath::Abs(Way.X) >= FMath::Abs(Way.Y);
			const int32 Steps = FMath::Clamp(FMath::CeilToInt32(float(bFlat ? FMath::Abs(Way.X) : FMath::Abs(Way.Y))), 1, 800);
			for (int32 Step = 0; Step <= Steps; ++Step)
			{
				const float S = float(Step) / Steps;
				const float T = FMath::Lerp(Span.X, Span.Y, S);
				const FVector2D At = A + Way * S;
				const float Haze = HazeAt(T);
				const float Light = Base * (0.35f + 0.65f * FMath::Pow(1.0f - T, 1.3f)) * (1.0f - HazeGrit + HazeGrit * 2.0f * Haze);
				const FLinearColor Colour = Haze > GlintAbove ? FMath::Lerp(CoreTint, Hot, (Haze - GlintAbove) / (1.0f - GlintAbove)) : CoreTint;
				const float Flash = Haze > GlintAbove ? 1.0f + Glint * (Haze - GlintAbove) / (1.0f - GlintAbove) : 1.0f;
				// The major axis steps a texel; the minor coordinate splits between its two rows (Wu).
				const double Major = bFlat ? At.X : At.Y;
				const double Minor = bFlat ? At.Y : At.X;
				const int32 M = FMath::FloorToInt32(float(Major));
				const int32 N0 = FMath::FloorToInt32(float(Minor - 0.5));
				const float Share = float(Minor - 0.5 - N0);
				const auto Put = [&](int32 N, float Level)
				{
					Frame.Add(bFlat ? M : N, bFlat ? N : M, Colour, Level * Light * Flash);
				};
				Put(N0, 1.0f - Share);
				Put(N0 + 1, Share);
				Put(N0 - 1, BeamSheath * (1.0f - Share));
				Put(N0 + 2, BeamSheath * Share);
			}
		}
	}
	if (bDot)
	{
		// The disc as it lies on the surface, in texels, never under DotCoreTexels across: a blob with a hot centre
		// in a red glow, and grains of speckle that boil as the dot moves over the surface and rest when it rests.
		FVector2D AxisU;
		FVector2D AxisV;
		if (!OSProjectToHud(Camera, Canvas, DotAt + DotAxisU, false, AxisU) || !OSProjectToHud(Camera, Canvas, DotAt + DotAxisV, false, AxisV))
		{
			AxisU = Dot2D + FVector2D(1.0, 0.0);
			AxisV = Dot2D + FVector2D(0.0, 1.0);
		}
		AxisU = (AxisU - Dot2D) / Texel;
		AxisV = (AxisV - Dot2D) / Texel;
		const auto AtLeast = [this](const FVector2D& Axis, const FVector2D& Fallback)
		{
			const double Size = Axis.Size();
			return Size < 0.01 ? Fallback * DotCoreTexels : Axis * FMath::Max(1.0, DotCoreTexels / Size);
		};
		AxisU = AtLeast(AxisU, FVector2D(1.0, 0.0));
		AxisV = AtLeast(AxisV, FVector2D(0.0, 1.0));
		const FVector2D Centre = Dot2D / Texel;
		const float Drive = FMath::Clamp(DotDrive, 0.0f, 1.2f);
		// The disc's metric: u, v along the axes; the core falls off past the rim, the glow wider.
		const double Det = AxisU.X * AxisV.Y - AxisU.Y * AxisV.X;
		const float Reach = float(FMath::Max(AxisU.Size(), AxisV.Size())) + 3.0f * DotGlowTexels;
		const int32 Span = FMath::Clamp(FMath::CeilToInt32(Reach), 1, 24);
		const int32 CX = FMath::FloorToInt32(float(Centre.X));
		const int32 CY = FMath::FloorToInt32(float(Centre.Y));
		for (int32 Y = CY - Span; Y <= CY + Span; ++Y)
		{
			for (int32 X = CX - Span; X <= CX + Span; ++X)
			{
				const FVector2D P(X + 0.5 - Centre.X, Y + 0.5 - Centre.Y);
				float Core = 0.0f;
				if (FMath::Abs(Det) > 1e-6)
				{
					const double U = (P.X * AxisV.Y - P.Y * AxisV.X) / Det;
					const double V = (AxisU.X * P.Y - AxisU.Y * P.X) / Det;
					const float R = float(FMath::Sqrt(U * U + V * V)); // 1 at the rim
					Core = FMath::Exp(-FMath::Square(R) * 1.4f);
				}
				const float Glow = DotGlowShare * FMath::Exp(-float(P.SizeSquared()) / (2.0f * DotGlowTexels * DotGlowTexels));
				Frame.Add(X, Y, FMath::Lerp(RedColour, Hot, FMath::Clamp(Core * 1.2f - 0.3f, 0.0f, 1.0f)), (Core + Glow) * Drive);
			}
		}
		if (Speckle > 0.0f)
		{
			const int64 Set = FMath::FloorToInt64(Boil);
			const float Blend = float(Boil - double(Set));
			for (int32 Grain = 0; Grain < 3; ++Grain)
			{
				for (int32 Which = 0; Which < 2; ++Which)
				{
					const int64 Seed = (Set + Which) * 7 + Grain;
					const float Weight = Which == 0 ? 1.0f - Blend : Blend;
					const float Angle = UE_TWO_PI * OSProjectorHash(Seed, 3301);
					const float Out = 0.3f + 0.8f * OSProjectorHash(Seed, 4409);
					const FVector2D At = Centre + (AxisU * FMath::Cos(Angle) + AxisV * FMath::Sin(Angle)) * Out;
					Frame.Add(FMath::FloorToInt32(float(At.X)), FMath::FloorToInt32(float(At.Y)), Hot, Speckle * Drive * Weight);
				}
			}
		}
	}
	TArray<FCanvasUVTri> Tris;
	Tris.Reserve(Frame.Light.Num() * 4);
	Frame.Draw(Tris);
	// The canvas' own white texture: GWhiteTexture lives in RenderCore (not linked), and a triangle item asserts on none.
	const FTexture* White = Canvas.DefaultTexture ? Canvas.DefaultTexture->GetResource() : nullptr;
	if (Tris.Num() > 0 && White)
	{
		FCanvasTriangleItem Light(Tris, White);
		Light.BlendMode = SE_BLEND_Additive;
		Canvas.DrawItem(Light);
	}
}
