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
#include "MKGlassPane.h"
#include "MKImpactTypes.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "OSWeapons.h"
#include "OSSuitProjector.h"
#include "OSWeldProjector.h"
#include "Engine/Texture2D.h"
#include "OSLens.h"
#include "StaticMeshResources.h"

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
	DotLight->SpecularScale = 0.0f; // its highlight on a glossy surface read as a second dot (playtest 23.10)
	DotLight->SetSourceRadius(0.0f);
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
	bLit = bDotHit = bDotSeen = bThreadShown = bLastDotTexel = false;
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
	// A dropout: a hard blink - down at once for DropSeconds, back at once (owner 23.10: lasers blink, they do not fade).
	SinceDrop += DeltaTime;
	if (DropsPerSecond > 0.0f && FMath::FRand() < DropsPerSecond * DeltaTime)
	{
		SinceDrop = 0.0f;
	}
	const float Drop = SinceDrop < DropSeconds ? DropDepth : 0.0f;
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
		if (!Part || !Part->IsVisible() || !Part->GetStaticMesh())
		{
			continue;
		}
		const FTransform ToLocal = Part->GetComponentTransform().Inverse();
		const FBox Box = Part->GetStaticMesh()->GetBoundingBox();
		const FVector Size = Box.GetSize();
		if (Part == Guns->ViewModel && Size.Z > 0.45 * Size.X)
		{
			// A pistol's frame is an L: the slide's cover along the top and the grip at the back. One box over it
			// also covers the air under the cover in front of the trigger guard, where the lens sits and the beam was
			// hidden though in plain view (playtest 23.10: «оторванный»). Two boxes: the cover, the grip.
			Boxes.Emplace(ToLocal, FBox(FVector(Box.Min.X, Box.Min.Y, Box.Max.Z - Size.Z * 0.38), Box.Max));
			Boxes.Emplace(ToLocal, FBox(Box.Min, FVector(Box.Min.X + Size.X * 0.55, Box.Max.Y, Box.Max.Z)));
			continue;
		}
		Boxes.Emplace(ToLocal, Box);
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
	LensAt = Lens.GetLocation();
	const FVector EyeAt = RefreshedView.GetLocation();
	// The dot: the first thing on the round's line within the gun's reach - out of the muzzle along the bore as the
	// gun is drawn, as UOSWeaponsComponent::Fire sends it (owner 23.10: the laser tells honestly where the round flies).
	FVector From;
	FVector Along;
	Guns->GetShotLine(EyeAt, Eye->GetForwardVector(), From, Along);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSLaserSight), /*bTraceComplex=*/false, GetOwner());
	Params.bReturnPhysicalMaterial = true;
	FHitResult Hit;
	Surface = EOSLaserSurface::Plain;
	// What a hit lies on: the round's physical material (MindKore's surfaces), or the glass pane's class.
	const auto KindOf = [](const FHitResult& On)
	{
		return On.PhysMaterial.IsValid() ? MKSurfaceKindFromPhysical(On.PhysMaterial->SurfaceType) : EMKSurfaceKind::Default;
	};
	const auto IsGlass = [&KindOf](const FHitResult& On)
	{
		return KindOf(On) == EMKSurfaceKind::Glass || Cast<AMKGlassPane>(On.GetActor()) != nullptr;
	};
	// Glass is passed through, every pane and shard of it (a broken pane's pieces are glass too): the dot lies on the
	// first thing behind that is not glass.
	const auto PassGlass = [](FCollisionQueryParams& Into, const FHitResult& On)
	{
		Into.AddIgnoredComponent(On.GetComponent());
		if (Cast<AMKGlassPane>(On.GetActor()))
		{
			Into.AddIgnoredActor(On.GetActor());
		}
	};
	FVector Start = From;
	for (int32 Pass = 0; Pass < 6; ++Pass)
	{
		++Traces;
		bDotHit = GetWorld()->LineTraceSingleByChannel(Hit, Start, From + Along * Weapon->RangeCm, ECC_GameTraceChannel2, Params);
		if (!bDotHit || !IsGlass(Hit))
		{
			break;
		}
		PassGlass(Params, Hit);
		Start = FVector(Hit.ImpactPoint) + Along * 0.5;
		bDotHit = false;
	}
	if (bDotHit)
	{
		const EMKSurfaceKind Kind = KindOf(Hit);
		Surface = Kind == EMKSurfaceKind::Metal ? EOSLaserSurface::Metal : Kind == EMKSurfaceKind::Flesh ? EOSLaserSurface::Flesh : EOSLaserSurface::Plain;
	}
	DotAt = bDotHit ? FVector(Hit.ImpactPoint) + FVector(Hit.ImpactNormal) * DotLiftCm : From + Along * Weapon->RangeCm;
	// The ghost: where the beam itself - from the lens as the gun is drawn to the dot, the line the HUD draws - meets
	// glass first, on its own trace (owner 23.10: it sat on the round's line from the muzzle; then, by the pane's plane,
	// it hung in the air past the pane's edge). No glass on that line, no ghost.
	bGhost = false;
	if (bDotHit && GlassGhost > 0.0f)
	{
		FCollisionQueryParams BeamParams(SCENE_QUERY_STAT(OSLaserBeam), /*bTraceComplex=*/false, GetOwner());
		BeamParams.bReturnPhysicalMaterial = true;
		const FVector LensDrawn = Guns->GunToDrawn(LensAt);
		FHitResult OnBeam;
		++Traces;
		if (GetWorld()->LineTraceSingleByChannel(OnBeam, LensDrawn, DotAt, ECC_GameTraceChannel2, BeamParams) && IsGlass(OnBeam)
			&& OnBeam.Distance < FVector::Dist(LensDrawn, DotAt) - 1.0f)
		{
			bGhost = true;
			GhostAt = FVector(OnBeam.ImpactPoint) + FVector(OnBeam.ImpactNormal).GetSafeNormal() * DotLiftCm;
		}
	}
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
		// Seen from the eye? The round's line starts at the muzzle, the eye is elsewhere: an edge can hide the dot. Glass
		// does not (the eye's line crosses other panes and shards than the round's did).
		const FVector SightEnd = DotAt - FVector(Hit.ImpactNormal) * (DotLiftCm * 0.5f);
		const double SightLength = FVector::Dist(EyeAt, DotAt);
		FCollisionQueryParams SightParams = Params;
		FVector SightFrom = EyeAt;
		bDotSeen = true;
		for (int32 Pass = 0; Pass < 6; ++Pass)
		{
			FHitResult Sight;
			++Traces;
			if (!GetWorld()->LineTraceSingleByChannel(Sight, SightFrom, SightEnd, ECC_Visibility, SightParams)
				|| FVector::Dist(EyeAt, FVector(Sight.ImpactPoint)) > SightLength - 2.0)
			{
				break;
			}
			if (!IsGlass(Sight))
			{
				bDotSeen = false;
				break;
			}
			PassGlass(SightParams, Sight);
			SightFrom = FVector(Sight.ImpactPoint) + (SightEnd - EyeAt).GetSafeNormal() * 0.5;
		}
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

bool UOSLaserSightComponent::DrawGunMask(const UCanvas& Canvas, float Texel, const FIntRect& Region)
{
	// A depth buffer of the gun on the light's grid, over the beam's region only: each part's triangles, as drawn (first
	// person field of view, the bodycam lens' bend), their nearest 1/depth per texel centre. The parts' boxes stood out
	// past the gun's silhouette and the beam began a gap away from it (owner 23.10: «все еще отсоединен»).
#if !WITH_EDITOR
	// A cooked mesh keeps its triangles on the CPU only with Allow CPU Access: the boxes then.
	return false;
#else
	MaskRegion = Region;
	const int32 Width = Region.Width();
	const int32 Height = Region.Height();
	if (Width <= 0 || Height <= 0 || int64(Width) * Height > 600000)
	{
		return false;
	}
	MaskNearInv.Reset();
	MaskNearInv.SetNumZeroed(Width * Height);
	const FMatrix ToView = Camera->GetComponentTransform().ToMatrixNoScale().Inverse();
	const float FieldOfView = Camera->bEnableFirstPersonFieldOfView ? Camera->FirstPersonFieldOfView : Camera->FieldOfView;
	const double Half = Canvas.ClipX * 0.5;
	const double Scale = Half / FMath::Tan(FMath::DegreesToRadians(FMath::Clamp(FieldOfView, 5.0f, 170.0f) * 0.5f));
	const FVector2D Clip(Canvas.ClipX, Canvas.ClipY);
	const double LensK = OSLensK(*Camera);
	TArray<const UStaticMeshComponent*, TInlineAllocator<8>> Parts;
	Parts.Add(Guns->ViewModel);
	Parts.Add(Guns->ActionInView);
	Parts.Add(Guns->MagInView);
	for (const UStaticMeshComponent* Module : Guns->ModulesInView)
	{
		Parts.Add(Module);
	}
	TArray<FVector3f> Drawn;
	bool bAny = false;
	for (const UStaticMeshComponent* Part : Parts)
	{
		if (!Part || !Part->IsVisible() || !Part->GetStaticMesh())
		{
			continue;
		}
		const FStaticMeshRenderData* Render = Part->GetStaticMesh()->GetRenderData();
		if (!Render || Render->LODResources.Num() == 0)
		{
			return false;
		}
		const FStaticMeshLODResources& Lod = Render->LODResources[0];
		const FPositionVertexBuffer& Positions = Lod.VertexBuffers.PositionVertexBuffer;
		const FIndexArrayView Indices = Lod.IndexBuffer.GetArrayView();
		const int32 Vertices = int32(Positions.GetNumVertices());
		if (Vertices == 0 || Indices.Num() < 3 || !const_cast<FPositionVertexBuffer&>(Positions).GetVertexData())
		{
			return false;
		}
		const FMatrix ToEye = Part->GetComponentTransform().ToMatrixWithScale() * ToView;
		Drawn.SetNumUninitialized(Vertices);
		for (int32 Vertex = 0; Vertex < Vertices; ++Vertex)
		{
			const FVector Local = ToEye.TransformPosition(FVector(Positions.VertexPosition(Vertex)));
			if (Local.X < 1.0)
			{
				Drawn[Vertex] = FVector3f::ZeroVector;
				continue;
			}
			FVector2D At(Half + Local.Y / Local.X * Scale, Clip.Y * 0.5 - Local.Z / Local.X * Scale);
			At = OSLensScreenUV(At / Clip, LensK) * Clip / Texel;
			Drawn[Vertex] = FVector3f(float(At.X), float(At.Y), float(1.0 / Local.X));
		}
		for (int32 Index = 0; Index + 2 < Indices.Num(); Index += 3)
		{
			const FVector3f& A = Drawn[Indices[Index]];
			const FVector3f& B = Drawn[Indices[Index + 1]];
			const FVector3f& C = Drawn[Indices[Index + 2]];
			if (A.Z <= 0.0f || B.Z <= 0.0f || C.Z <= 0.0f)
			{
				continue;
			}
			// The texel centres (x + 0.5) inside the triangle's box, within the region.
			const int32 X0 = FMath::Max(Region.Min.X, FMath::CeilToInt32(FMath::Min3(A.X, B.X, C.X) - 0.5f));
			const int32 X1 = FMath::Min(Region.Max.X - 1, FMath::FloorToInt32(FMath::Max3(A.X, B.X, C.X) - 0.5f));
			const int32 Y0 = FMath::Max(Region.Min.Y, FMath::CeilToInt32(FMath::Min3(A.Y, B.Y, C.Y) - 0.5f));
			const int32 Y1 = FMath::Min(Region.Max.Y - 1, FMath::FloorToInt32(FMath::Max3(A.Y, B.Y, C.Y) - 0.5f));
			if (X0 > X1 || Y0 > Y1)
			{
				continue;
			}
			const float Area = (B.X - A.X) * (C.Y - A.Y) - (B.Y - A.Y) * (C.X - A.X);
			if (FMath::Abs(Area) < 1.0e-8f)
			{
				continue;
			}
			const float InvArea = 1.0f / Area;
			for (int32 Y = Y0; Y <= Y1; ++Y)
			{
				for (int32 X = X0; X <= X1; ++X)
				{
					const float PX = X + 0.5f;
					const float PY = Y + 0.5f;
					const float WA = ((C.X - B.X) * (PY - B.Y) - (C.Y - B.Y) * (PX - B.X)) * InvArea;
					const float WB = ((A.X - C.X) * (PY - C.Y) - (A.Y - C.Y) * (PX - C.X)) * InvArea;
					const float WC = 1.0f - WA - WB;
					if (WA < -1.0e-4f || WB < -1.0e-4f || WC < -1.0e-4f)
					{
						continue;
					}
					float& Near = MaskNearInv[(Y - Region.Min.Y) * Width + (X - Region.Min.X)];
					Near = FMath::Max(Near, WA * A.Z + WB * B.Z + WC * C.Z);
				}
			}
		}
		bAny = true;
	}
	// The silhouette eroded by MaskErodeTexels: the beam runs that far over the gun's edge, so its texel's soft tent
	// meets the edge instead of stopping a step short of it (owner 23.10: «небольшой зазор остался»).
	for (int32 Pass = 0; bAny && Pass < MaskErodeTexels; ++Pass)
	{
		const TArray<float> Before = MaskNearInv;
		const auto Covered = [&Before, Width, Height](int32 X, int32 Y)
		{
			return X < 0 || Y < 0 || X >= Width || Y >= Height || Before[Y * Width + X] > 0.0f;
		};
		for (int32 Y = 0; Y < Height; ++Y)
		{
			for (int32 X = 0; X < Width; ++X)
			{
				if (Before[Y * Width + X] > 0.0f && !(Covered(X - 1, Y) && Covered(X + 1, Y) && Covered(X, Y - 1) && Covered(X, Y + 1)))
				{
					MaskNearInv[Y * Width + X] = 0.0f;
				}
			}
		}
	}
	return bAny;
#endif
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
	const FLinearColor Hot = HotColour;

	FVector2D Dot2D;
	const bool bDot = bDotHit && bDotSeen && OSProjectToHud(Camera, Canvas, DotAt, false, Dot2D);
	FVector2D Ghost2D;
	const bool bGhostShown = bGhost && bDotHit && OSProjectToHud(Camera, Canvas, GhostAt, false, Ghost2D);
	// The gun's own depth on the grid over the beam (and the ghost): the beam leaves from behind its silhouette exactly.
	bool bMask = false;
	FVector2D Lens2D;
	FVector2D End2D;
	if (bThreadShown && OSProjectToHud(Camera, Canvas, LensAt, true, Lens2D) && OSProjectToHud(Camera, Canvas, BeamEnd, true, End2D))
	{
		FBox2D Reach(Lens2D / Texel, Lens2D / Texel);
		Reach += End2D / Texel;
		if (bGhostShown)
		{
			Reach += Ghost2D / Texel;
		}
		const FIntRect Region(
			FMath::Max(0, FMath::FloorToInt32(float(Reach.Min.X)) - 3), FMath::Max(0, FMath::FloorToInt32(float(Reach.Min.Y)) - 3),
			FMath::Min(FMath::CeilToInt32(Canvas.ClipX / Texel), FMath::CeilToInt32(float(Reach.Max.X)) + 4),
			FMath::Min(FMath::CeilToInt32(Canvas.ClipY / Texel), FMath::CeilToInt32(float(Reach.Max.Y)) + 4));
		bMask = DrawGunMask(Canvas, Texel, Region);
	}
	// Is the gun nearer than Depth (cm from the eye; the world behind it: a huge depth) at this texel?
	const auto GunBefore = [this, bMask](int32 X, int32 Y, double Depth)
	{
		if (!bMask || !MaskRegion.Contains(FIntPoint(X, Y)))
		{
			return false;
		}
		const float Near = MaskNearInv[(Y - MaskRegion.Min.Y) * MaskRegion.Width() + (X - MaskRegion.Min.X)];
		return Near > 0.0f && 1.0 / Near < Depth - 0.4;
	};
	const FTransform View = Camera->GetComponentTransform();
	const auto DepthOf = [&View](const FVector& At) { return FMath::Max(1.0, View.InverseTransformPositionNoScale(At).X); };
	TArray<FVector2f, TInlineAllocator<1>> WholeBeam;
	WholeBeam.Emplace(0.0f, 1.0f);
	const TArray<FVector2f>& Spans = BeamSpans;
	if (bThreadShown && (bMask || Spans.Num() > 0))
	{
		// One texel wide, Wu-weighted across the rows (a hair's line steps softly as it moves), a dim sheath either
		// side; its light falls from the lens to the dot and varies with the haze it passes through - noise fixed in the
		// world, drifting slowly, which the beam sweeps across as the gun sways; the densest haze glints.
		const float Base = ThreadLevel * ThreadBrightness;
		const float Half = BeamTexels * 0.5f;
		const FVector Drift = FVector(0.3, 0.9, 0.2) * (HazeDriftCmPerSec * Now);
		const auto HazeAt = [&](float T)
		{
			const FVector P = (LensAt + (BeamEnd - LensAt) * T + Drift) / HazeCellCm;
			return 0.6f * OSNoise3(P * 0.35) + 0.4f * OSNoise3(P + FVector(37.0, 11.0, 5.0));
		};
		// With the mask the whole beam is drawn and each texel tested; without it, the boxes' spans.
		const TArrayView<const FVector2f> Drawn = bMask ? TArrayView<const FVector2f>(WholeBeam) : TArrayView<const FVector2f>(Spans);
		for (int32 SpanIndex = 0; SpanIndex < Drawn.Num(); ++SpanIndex)
		{
			const FVector2f& Span = Drawn[SpanIndex];
			// The root: the first BeamRootTexels the beam shows past the gun's silhouette always light, whatever the haze,
			// fading into it - a gap of thin haze there read as the beam torn off the module (owner 23.10: «обрезается»).
			int32 FirstShown = SpanIndex == 0 ? -1 : TNumericLimits<int32>::Max();
			FVector2D A2;
			FVector2D B2;
			const FVector From3 = LensAt + (BeamEnd - LensAt) * Span.X;
			const FVector To3 = LensAt + (BeamEnd - LensAt) * Span.Y;
			if (!OSProjectToHud(Camera, Canvas, From3, true, A2) || !OSProjectToHud(Camera, Canvas, To3, true, B2))
			{
				continue;
			}
			const double InvFrom = 1.0 / DepthOf(From3);
			const double InvTo = 1.0 / DepthOf(To3);
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
				// Its depth here (1/depth runs straight on screen).
				const double Depth = 1.0 / FMath::Lerp(InvFrom, InvTo, double(S));
				if (FirstShown < 0 && !GunBefore(FMath::FloorToInt32(float(At.X)), FMath::FloorToInt32(float(At.Y)), Depth))
				{
					FirstShown = Step;
				}
				const float Root = FirstShown >= 0 && FirstShown <= Step && BeamRootTexels > 0.0f
					? FMath::Clamp(1.0f - float(Step - FirstShown) / BeamRootTexels, 0.0f, 1.0f) : 0.0f;
				// Nothing to show in below the haze's floor: the beam breaks into a sparse line (not at its root).
				const float Shown = FMath::Max(Root, FMath::Pow(FMath::Clamp((Haze - HazeFloor) / (1.0f - HazeFloor), 0.0f, 1.0f), 0.8f));
				const float Light = Base * (0.35f + 0.65f * FMath::Pow(1.0f - T, 1.3f)) * Shown;
				// The glints brighten the beam's own red, never shift it (owner 23.10: no flashing in another colour).
				const FLinearColor Colour = BeamColour;
				const float Flash = Haze > GlintAbove ? 1.0f + Glint * (Haze - GlintAbove) / (1.0f - GlintAbove) : 1.0f;
				// The major axis steps a texel; across it the line's width is shared out by coverage over the rows it
				// touches (a hair under a texel lights its row by its share), the sheath either side of those.
				const double Major = bFlat ? At.X : At.Y;
				const double Minor = bFlat ? At.Y : At.X;
				const int32 M = FMath::FloorToInt32(float(Major));
				const float Low = float(Minor) - Half;
				const float High = float(Minor) + Half;
				const int32 NLow = FMath::FloorToInt32(Low);
				const int32 NHigh = FMath::FloorToInt32(High);
				const auto Put = [&](int32 N, float Level)
				{
					const int32 X = bFlat ? M : N;
					const int32 Y = bFlat ? N : M;
					if (!GunBefore(X, Y, Depth))
					{
						Frame.Add(X, Y, Colour, Level * Light * Flash);
					}
				};
				for (int32 N = NLow; N <= NHigh; ++N)
				{
					Put(N, FMath::Min(High, float(N + 1)) - FMath::Max(Low, float(N)));
				}
				Put(NLow - 1, BeamSheath * (1.0f - (Low - NLow)));
				Put(NHigh + 1, BeamSheath * (High - NHigh));
			}
		}
	}
	if (bGhostShown)
	{
		// The ghost on the glass: a faint small blob where the beam went through.
		const FVector2D G = Ghost2D / Texel;
		const int32 GX = FMath::FloorToInt32(float(G.X));
		const int32 GY = FMath::FloorToInt32(float(G.Y));
		const float Drive = FMath::Clamp(DotDrive, 0.0f, 1.2f) * GlassGhost;
		for (int32 Y = GY - 1; Y <= GY + 1; ++Y)
		{
			for (int32 X = GX - 1; X <= GX + 1; ++X)
			{
				const FVector2D P(X + 0.5 - G.X, Y + 0.5 - G.Y);
				if (!GunBefore(X, Y, UE_BIG_NUMBER))
				{
					Frame.Add(X, Y, RedColour, Drive * FMath::Exp(-float(P.SizeSquared()) * 1.6f));
				}
			}
		}
	}
	if (bDot)
	{
		// The disc as it lies on the surface, in texels, never under DotCoreTexels across: a blob with a hot centre
		// in a red glow, and grains of speckle that boil as the dot moves over the surface and rest when it rests.
		// It reads the surface: tight with a spark on metal; sunk into flesh as a wide dark-red glow that throbs.
		float CoreShare = 1.0f;
		float GlowTexels = DotGlowTexels;
		float GlowShare = DotGlowShare;
		float SpeckleShare = Speckle;
		FLinearColor GlowColour = RedColour;
		if (Surface == EOSLaserSurface::Metal)
		{
			GlowShare *= MetalGlow;
			SpeckleShare *= MetalSpeckle;
		}
		else if (Surface == EOSLaserSurface::Flesh)
		{
			const float Throb = 1.0f + FleshPulse * FMath::Sin(float(Now) * FleshPulseHz * UE_TWO_PI);
			CoreShare = FleshCore;
			GlowTexels *= FleshGlowTimes;
			GlowShare = FleshGlowShare * Throb;
			SpeckleShare = 0.0f;
			GlowColour = FLinearColor(0.75f, 0.02f, 0.01f);
		}
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
		const float Reach = float(FMath::Max(AxisU.Size(), AxisV.Size())) + 3.0f * GlowTexels;
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
				const float Glow = GlowShare * FMath::Exp(-float(P.SizeSquared()) / (2.0f * GlowTexels * GlowTexels));
				Frame.Add(X, Y, FMath::Lerp(GlowColour, Hot, FMath::Clamp(Core * CoreShare * 1.2f - 0.3f, 0.0f, 1.0f)), (Core * CoreShare + Glow) * Drive);
			}
		}
		if (SpeckleShare > 0.0f)
		{
			// One set of grains at a time, switched hard (owner 23.10: lasers blink, the cross-fade read soft).
			const int64 Set = FMath::FloorToInt64(Boil);
			for (int32 Grain = 0; Grain < 3; ++Grain)
			{
				const int64 Seed = Set * 7 + Grain;
				const float Angle = UE_TWO_PI * OSProjectorHash(Seed, 3301);
				const float Out = 0.3f + 0.8f * OSProjectorHash(Seed, 4409);
				const FVector2D At = Centre + (AxisU * FMath::Cos(Angle) + AxisV * FMath::Sin(Angle)) * Out;
				Frame.Add(FMath::FloorToInt32(float(At.X)), FMath::FloorToInt32(float(At.Y)), Hot, SpeckleShare * Drive);
			}
		}
		// The streak as the dot sweeps the screen: back toward last frame's place, a hair thin and at most TrailTexels long,
		// fading out (owner 23.10: «максимально коротким, тоненьким»; the world light's motion blur drew it long).
		if (bLastDotTexel && TrailTexels > 0.0f && TrailShare > 0.0f)
		{
			const FVector2D Back = LastDotTexel - Centre;
			const double Length = FMath::Min(Back.Size(), double(TrailTexels));
			if (Length > 0.5)
			{
				const FVector2D Way = Back.GetSafeNormal() * Length;
				const int32 Steps = FMath::Max(2, FMath::CeilToInt32(float(Length) * 2.0f));
				for (int32 Step = 1; Step <= Steps; ++Step)
				{
					const float S = float(Step) / Steps;
					// A point splat over its four texels (bilinear): a line under a texel wide.
					const FVector2D At = Centre + Way * S - FVector2D(0.5, 0.5);
					const int32 X = FMath::FloorToInt32(float(At.X));
					const int32 Y = FMath::FloorToInt32(float(At.Y));
					const float FX = float(At.X) - X;
					const float FY = float(At.Y) - Y;
					const float Level = TrailShare * Drive * (1.0f - S) * float(Length) / Steps;
					Frame.Add(X, Y, RedColour, Level * (1.0f - FX) * (1.0f - FY));
					Frame.Add(X + 1, Y, RedColour, Level * FX * (1.0f - FY));
					Frame.Add(X, Y + 1, RedColour, Level * (1.0f - FX) * FY);
					Frame.Add(X + 1, Y + 1, RedColour, Level * FX * FY);
				}
			}
		}
		LastDotTexel = Centre;
		bLastDotTexel = true;
	}
	else
	{
		bLastDotTexel = false;
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
