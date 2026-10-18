#include "JamFlyer.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MKImpactDelivery.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace JamFlyerParts
{
	// The joints of the Blender parts (Scripts/blender/jam_flyer.py, metres there), cm.
	const FVector ShoulderCm(18.0f, 9.0f, 7.0f);
	const FVector TailRootCm(-42.0f, 0.0f, 2.0f);
	const FVector TailSplitCm(-155.0f, 0.0f, 0.0f);
	const FVector HeadCm(105.0f, 0.0f, 18.0f);

	/** A wing's turn: the flap about the body's long axis, then the sweep back. Side +1 right, -1 left (mirrored mesh). */
	FQuat WingTurn(float Side, float FlapDeg, float SweepDeg)
	{
		return FQuat(FVector::UpVector, Side * FMath::DegreesToRadians(SweepDeg)) * FQuat(FVector::ForwardVector, Side * FMath::DegreesToRadians(FlapDeg));
	}

	UStaticMeshComponent* MakePart(AActor* Owner, const TCHAR* Name, UStaticMesh* Mesh, USceneComponent* Parent, const FVector& At)
	{
		UStaticMeshComponent* Part = Owner->CreateDefaultSubobject<UStaticMeshComponent>(Name);
		Part->SetupAttachment(Parent);
		Part->SetRelativeLocation(At);
		Part->SetStaticMesh(Mesh);
		// Only rounds (the Shot channel, complex) hit the parts; the sphere is the physical body.
		Part->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		Part->SetCollisionResponseToAllChannels(ECR_Ignore);
		Part->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Block);
		Part->SetGenerateOverlapEvents(false);
		Part->SetCanEverAffectNavigation(false);
		return Part;
	}
}

using namespace JamFlyerParts;

// --- habitat -------------------------------------------------------------------------------

AJamFlyerHabitat::AJamFlyerHabitat()
{
	Zone = CreateDefaultSubobject<USphereComponent>(TEXT("Zone"));
	SetRootComponent(Zone);
	Zone->InitSphereRadius(1500.0f);
	Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Zone->ShapeColor = FColor(200, 80, 40);
	FlyerClass = AJamFlyer::StaticClass();
}

float AJamFlyerHabitat::GetOuterRadius() const
{
	return Zone->GetScaledSphereRadius();
}

void AJamFlyerHabitat::BeginPlay()
{
	Super::BeginPlay();
	const float Outer = GetOuterRadius();
	const float Inner = FMath::Min(InnerRadiusCm, Outer * 0.9f);
	UClass* Class = FlyerClass ? FlyerClass.Get() : AJamFlyer::StaticClass();
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButAlwaysSpawn;
	for (int32 Index = 0; Index < FlockSize; ++Index)
	{
		const float Distance = FMath::FRandRange(Inner + 0.25f * (Outer - Inner), Inner + 0.85f * (Outer - Inner));
		const FVector Where = GetActorLocation() + FMath::VRand() * Distance;
		if (AJamFlyer* Flyer = GetWorld()->SpawnActor<AJamFlyer>(Class, Where, FRotator::ZeroRotator, Params))
		{
			Flyer->SetHome(this);
			Flock.Add(Flyer);
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Flyer habitat %s: %d flyers at %s, radius %.0f m (inner %.0f m), leash %.0f m"), *GetName(), Flock.Num(),
		*GetActorLocation().ToCompactString(), Outer / 100.0f, Inner / 100.0f, LeashCm / 100.0f);
}

// --- flyer ---------------------------------------------------------------------------------------

AJamFlyer::AJamFlyer()
{
	PrimaryActorTick.bCanEverTick = true;

	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitSphereRadius(40.0f);
	Body->SetCollisionObjectType(ECC_PhysicsBody);
	Body->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Body->SetCollisionResponseToAllChannels(ECR_Block);
	// Alive it passes through the player (the bite is the contact); dead it is a body to push.
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.0f);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(MassKg);
	Body->BodyInstance.DOFMode = EDOFMode::SixDOF;
	Body->BodyInstance.bLockXRotation = true;
	Body->BodyInstance.bLockYRotation = true;
	Body->BodyInstance.bLockZRotation = true;
	Body->SetCanEverAffectNavigation(false);
	// A round in the body reads flesh, as one in the mesh parts does (their material's).
	static ConstructorHelpers::FObjectFinder<UPhysicalMaterial> Flesh(TEXT("/Game/MindKore/Materials/Physical/PM_MK_Flesh.PM_MK_Flesh"));
	Body->SetPhysMaterialOverride(Flesh.Object);

	Visual = CreateDefaultSubobject<USceneComponent>(TEXT("Visual"));
	Visual->SetupAttachment(Body);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> TorsoMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_Body.SM_Jam_Flyer_Body"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> WingMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_Wing.SM_Jam_Flyer_Wing"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TailMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_Tail.SM_Jam_Flyer_Tail"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> TailTipMesh(TEXT("/Game/JamArt/Meshes/Flyer/SM_Jam_Flyer_TailTip.SM_Jam_Flyer_TailTip"));
	Torso = MakePart(this, TEXT("Torso"), TorsoMesh.Object, Visual, FVector::ZeroVector);
	WingR = MakePart(this, TEXT("WingR"), WingMesh.Object, Torso, ShoulderCm);
	WingL = MakePart(this, TEXT("WingL"), WingMesh.Object, Torso, FVector(ShoulderCm.X, -ShoulderCm.Y, ShoulderCm.Z));
	WingL->SetRelativeScale3D(FVector(1.0f, -1.0f, 1.0f)); // the right wing mirrored
	Tail = MakePart(this, TEXT("Tail"), TailMesh.Object, Torso, TailRootCm);
	TailTip = MakePart(this, TEXT("TailTip"), TailTipMesh.Object, Tail, TailSplitCm);

	// MindKore's recorded donors (CREDITS.md): the breath grunt pitched up as the screech, the gristle crunch as the bite.
	static ConstructorHelpers::FObjectFinder<USoundBase> Grunt(TEXT("/Game/MindKore/Audio/Enemy/SC_MK_Enemy_Grunt.SC_MK_Enemy_Grunt"));
	ScreechSound = Grunt.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> Crunch(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Limb.SC_MK_Hit_Limb"));
	BiteSound = Crunch.Object;
}

void AJamFlyer::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	Seed = FMath::FRandRange(0.0f, 100.0f);
	HomeCentre = GetActorLocation();
	Body->SetMassOverrideInKg(NAME_None, MassKg, true);
	Body->SetPhysicsLinearVelocity(FMath::VRand() * CruiseSpeedCm);
	SoundAttenuation = NewObject<USoundAttenuation>(this);
	SoundAttenuation->Attenuation.bAttenuate = true;
	SoundAttenuation->Attenuation.bSpatialize = true;
	SoundAttenuation->Attenuation.AttenuationShapeExtents = FVector(300.0f);
	SoundAttenuation->Attenuation.FalloffDistance = SoundFalloffCm;
}

void AJamFlyer::SetHome(AJamFlyerHabitat* InHabitat)
{
	Habitat = InHabitat;
	if (InHabitat)
	{
		HomeCentre = InHabitat->GetActorLocation();
		HomeOuterCm = InHabitat->GetOuterRadius();
		HomeInnerCm = FMath::Min(InHabitat->InnerRadiusCm, HomeOuterCm * 0.9f);
		LeashCm = InHabitat->LeashCm;
		AggroMarginCm = InHabitat->AggroMarginCm;
	}
}

const TCHAR* AJamFlyer::StateName(EJamFlyerState InState)
{
	switch (InState)
	{
	case EJamFlyerState::Wander: return TEXT("wander");
	case EJamFlyerState::Circle: return TEXT("circle");
	case EJamFlyerState::Dive: return TEXT("dive");
	case EJamFlyerState::Retreat: return TEXT("retreat");
	default: return TEXT("dead");
	}
}

APawn* AJamFlyer::Player() const
{
	return UGameplayStatics::GetPlayerPawn(this, 0);
}

FVector AJamFlyer::HeadLocation() const
{
	return Visual->GetComponentTransform().TransformPosition(HeadCm);
}

FVector AJamFlyer::Up(const FVector& Location) const
{
	return HomeInnerCm > 0.0f ? (Location - HomeCentre).GetSafeNormal(UE_SMALL_NUMBER, FVector::UpVector) : FVector::UpVector;
}

bool AJamFlyer::AtHome(const FVector& Location, float ExtraCm) const
{
	return FVector::DistSquared(Location, HomeCentre) <= FMath::Square(HomeOuterCm + ExtraCm);
}

FVector AJamFlyer::PickWanderPoint(const FVector& From, const FVector& Forward, bool bSwoop) const
{
	// Ahead and to a side, so it wheels instead of zigzagging; then into the shell.
	const FVector Direction = (Forward + FMath::VRand() * 0.9f).GetSafeNormal(UE_SMALL_NUMBER, Forward);
	const FVector Ahead = From + Direction * FMath::FRandRange(1500.0f, 3000.0f);
	const FVector FromCentre = Ahead - HomeCentre;
	const float Low = HomeInnerCm + 300.0f;
	const float High = FMath::Max(Low, HomeOuterCm - 300.0f);
	// A swoop: down to just over the inner surface (a planetoid) or low through the ball.
	const float Distance = bSwoop ? (HomeInnerCm > 0.0f ? Low : HomeOuterCm * 0.5f) : FMath::Clamp(float(FromCentre.Size()), Low * (HomeInnerCm > 0.0f ? 1.0f : 0.0f), High);
	const FVector Out = bSwoop && HomeInnerCm <= 0.0f ? (FromCentre.GetSafeNormal() - FVector::UpVector).GetSafeNormal() : FromCentre.GetSafeNormal();
	return HomeCentre + Out * Distance;
}

void AJamFlyer::Screech(float PitchScale)
{
	if (ScreechSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, ScreechSound, HeadLocation(), 1.0f,
			FMath::FRandRange(ScreechPitch.X, ScreechPitch.Y) * PitchScale, 0.0f, SoundAttenuation);
	}
}

void AJamFlyer::SetState(EJamFlyerState NewState, const FString& Why)
{
	const double Now = GetWorld()->GetTimeSeconds();
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: %s -> %s (%s)"), *GetName(), StateName(State), StateName(NewState), *Why);
	State = NewState;
	StateSeconds = Now;
	switch (NewState)
	{
	case EJamFlyerState::Circle:
		StateUntil = Now + FMath::FRandRange(CircleSeconds.X, CircleSeconds.Y);
		break;
	case EJamFlyerState::Retreat:
		StateUntil = Now + RetreatSeconds;
		break;
	case EJamFlyerState::Wander:
		WanderPickedSeconds = -100.0; // a fresh target
		break;
	default:
		break;
	}
}

void AJamFlyer::Aggro(const TCHAR* Why)
{
	if (State != EJamFlyerState::Wander)
	{
		return;
	}
	LastSeenSeconds = GetWorld()->GetTimeSeconds(); // a woken flockmate gets LoseSightSeconds to find the player
	SetState(EJamFlyerState::Circle, FString::Printf(TEXT("aggro: %s"), Why));
	Screech(1.0f);
	if (const AJamFlyerHabitat* Home = Habitat.Get())
	{
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			AJamFlyer* Other = Weak.Get();
			if (Other && Other != this && FVector::Dist(Other->GetActorLocation(), GetActorLocation()) <= Home->AlertRadiusCm)
			{
				Other->Aggro(*FString::Printf(TEXT("woken by %s's screech"), *GetName()));
			}
		}
	}
}

void AJamFlyer::Sense()
{
	bSeesPlayer = false;
	const APawn* Target = Player();
	if (!Target)
	{
		return;
	}
	const FVector Eye = HeadLocation();
	const FVector To = Target->GetActorLocation() - Eye;
	const float Distance = To.Size();
	if (Distance > SightRangeCm)
	{
		return;
	}
	// Noticing needs the cone; a hunt keeps track all around.
	if (!IsHunting() && FVector::DotProduct(To / FMath::Max(Distance, 1.0f), Visual->GetForwardVector()) < FMath::Cos(FMath::DegreesToRadians(SightHalfAngleDeg)))
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerSight), /*bTraceComplex=*/false, this);
	Params.AddIgnoredActor(Target);
	bSeesPlayer = !GetWorld()->LineTraceTestByChannel(Eye, Target->GetActorLocation(), ECC_Visibility, Params);
	if (bSeesPlayer)
	{
		LastSeenSeconds = GetWorld()->GetTimeSeconds();
	}
}

void AJamFlyer::Think(const FVector& Location, const FVector& Velocity)
{
	const double Now = GetWorld()->GetTimeSeconds();
	APawn* Target = Player();
	if (State == EJamFlyerState::Wander)
	{
		if (bSeesPlayer && Now >= CalmUntil && Target && AtHome(Target->GetActorLocation(), AggroMarginCm))
		{
			const FVector To = Target->GetActorLocation() - HeadLocation();
			Aggro(*FString::Printf(TEXT("saw the player %.1f m away, %.0f deg off the nose"), To.Size() / 100.0f,
				FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(To.GetSafeNormal(), Visual->GetForwardVector())))));
		}
		return;
	}
	if (!Target)
	{
		SetState(EJamFlyerState::Wander, TEXT("no player"));
		return;
	}
	const FVector PlayerAt = Target->GetActorLocation();
	if (!AtHome(PlayerAt, LeashCm))
	{
		CalmUntil = Now + CalmSeconds;
		SetState(EJamFlyerState::Wander, FString::Printf(TEXT("de-aggro: the player left the habitat, %.0f m from its centre, leash at %.0f m"),
			FVector::Dist(PlayerAt, HomeCentre) / 100.0f, (HomeOuterCm + LeashCm) / 100.0f));
		return;
	}
	if (Now - LastSeenSeconds > LoseSightSeconds)
	{
		CalmUntil = Now + CalmSeconds;
		SetState(EJamFlyerState::Wander, FString::Printf(TEXT("de-aggro: lost sight of the player for %.1f s"), float(Now - LastSeenSeconds)));
		return;
	}
	const float Distance = FVector::Dist(PlayerAt, Location);
	switch (State)
	{
	case EJamFlyerState::Circle:
		if (Now >= StateUntil && bSeesPlayer)
		{
			SetState(EJamFlyerState::Dive, FString::Printf(TEXT("dive from %.1f m"), Distance / 100.0f));
			Screech(1.15f);
		}
		break;
	case EJamFlyerState::Dive:
		if (Distance <= BiteReachCm)
		{
			Bite(Target);
			SetState(EJamFlyerState::Retreat, TEXT("bit, pulling away"));
		}
		else if (Now - StateSeconds > MaxDiveSeconds)
		{
			SetState(EJamFlyerState::Retreat, TEXT("missed: the dive timed out"));
		}
		// Overshot: it came close and now flies away (a dive that starts flying away still has the time-out).
		else if (Distance < 600.0f && FVector::DotProduct(Velocity, PlayerAt - Location) < 0.0f)
		{
			SetState(EJamFlyerState::Retreat, FString::Printf(TEXT("missed: overshot by %.1f m"), Distance / 100.0f));
		}
		break;
	case EJamFlyerState::Retreat:
		if (Now >= StateUntil)
		{
			SetState(EJamFlyerState::Circle, TEXT("wheeling for another dive"));
		}
		break;
	default:
		break;
	}
}

FVector AJamFlyer::DesiredVelocity(const FVector& Location, const FVector& Velocity, float& OutAccel)
{
	const double Now = GetWorld()->GetTimeSeconds();
	const APawn* Target = Player();
	const FVector Forward = Velocity.GetSafeNormal(UE_SMALL_NUMBER, Visual->GetForwardVector());
	FVector Desired = FVector::ZeroVector;
	OutAccel = HuntAccelCm;
	switch (State)
	{
	case EJamFlyerState::Wander:
	{
		if (Now - WanderPickedSeconds > WanderRetargetSeconds || FVector::Dist(Location, WanderPoint) < WanderArriveCm)
		{
			const bool bSwoop = FMath::FRand() < SwoopChance;
			WanderPoint = PickWanderPoint(Location, Forward, bSwoop);
			WanderSpeedScale = bSwoop ? 1.5f : FMath::FRandRange(0.8f, 1.15f);
			WanderPickedSeconds = Now;
		}
		// Never static: always at cruise, turning at most 60 degrees off the flight at a time
		// (a target behind is reached in a wheeling arc, not by braking), bobbing along the up.
		const FVector ToPoint = (WanderPoint - Location).GetSafeNormal(UE_SMALL_NUMBER, Forward);
		const float Angle = FMath::Acos(FMath::Clamp(float(FVector::DotProduct(Forward, ToPoint)), -1.0f, 1.0f));
		const FVector Heading = Angle <= FMath::DegreesToRadians(60.0f) ? ToPoint
			: FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(Forward, ToPoint), FMath::DegreesToRadians(60.0f) / Angle).RotateVector(Forward);
		Desired = Heading * CruiseSpeedCm * WanderSpeedScale
			+ Up(Location) * 150.0f * FMath::Sin(float(Now) * 0.8f + Seed);
		OutAccel = WanderAccelCm;
		break;
	}
	case EJamFlyerState::Circle:
	{
		const FVector Axis = Up(Target->GetActorLocation());
		const FVector Rel = Location - (Target->GetActorLocation() + Axis * CircleHeightCm);
		const float Height = FVector::DotProduct(Rel, Axis);
		FVector Radial = Rel - Axis * Height;
		const float RadialCm = Radial.Size();
		Radial = RadialCm > 1.0f ? Radial / RadialCm : FVector::CrossProduct(Axis, FVector::RightVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
		const float Turn = FMath::Fmod(Seed, 2.0f) < 1.0f ? 1.0f : -1.0f; // each one wheels its own way round
		Desired = FVector::CrossProduct(Axis, Radial) * Turn * CircleSpeedCm + Radial * (CircleRadiusCm - RadialCm) * 1.5f
			- Axis * Height * 1.5f + Axis * 200.0f * FMath::Sin(float(Now) * 2.0f + Seed);
		break;
	}
	case EJamFlyerState::Dive:
	{
		// At where the player will be when it arrives (at most 0.8 s ahead).
		const FVector To = Target->GetActorLocation() - Location;
		const float Lead = FMath::Min(To.Size() / FMath::Max(DiveSpeedCm, 1.0f), 0.8f);
		DiveDirection = (To + Target->GetVelocity() * Lead).GetSafeNormal(UE_SMALL_NUMBER, Forward);
		Desired = DiveDirection * DiveSpeedCm;
		OutAccel = HuntAccelCm * 1.3f;
		break;
	}
	case EJamFlyerState::Retreat:
		Desired = ((Location - Target->GetActorLocation()).GetSafeNormal() + Up(Target->GetActorLocation()) * 0.8f).GetSafeNormal() * CruiseSpeedCm * 1.3f;
		break;
	default:
		break;
	}
	// Keep out of each other's wings (the dive alone ignores the flock).
	if (const AJamFlyerHabitat* Home = Habitat.Get(); Home && State != EJamFlyerState::Dive)
	{
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			const AJamFlyer* Other = Weak.Get();
			if (Other && Other != this && Other->State != EJamFlyerState::Dead)
			{
				const FVector Away = Location - Other->GetActorLocation();
				const float Distance = Away.Size();
				if (Distance < SeparationCm && Distance > 1.0f)
				{
					Desired += Away / Distance * (SeparationCm - Distance) / SeparationCm * 600.0f;
				}
			}
		}
	}
	// The circle's pull grows with the distance (a player across a planetoid): never ask for more than a dive.
	return Desired.GetClampedToMaxSize(State == EJamFlyerState::Dive ? DiveSpeedCm : FMath::Max(CircleSpeedCm, CruiseSpeedCm * 1.5f));
}

FVector AJamFlyer::Avoid(const FVector& Location, const FVector& Desired) const
{
	const float Speed = FMath::Max(float(Desired.Size()), 100.0f);
	const FVector Direction = Desired.GetSafeNormal(UE_SMALL_NUMBER, Visual->GetForwardVector());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerAvoid), /*bTraceComplex=*/false, this);
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	Objects.AddObjectTypesToQuery(ECC_WorldDynamic);
	Objects.AddObjectTypesToQuery(ECC_PhysicsBody);
	FHitResult Hit;
	if (!GetWorld()->SweepSingleByObjectType(Hit, Location, Location + Direction * (Speed * AvoidSeconds + 150.0f), FQuat::Identity,
		Objects, FCollisionShape::MakeSphere(60.0f), Params))
	{
		return Desired;
	}
	// A dive goes for the player even on the ground: only what is in front of the player is avoided.
	if (State == EJamFlyerState::Dive)
	{
		if (const APawn* Target = Player(); Target && Hit.Distance > FVector::Dist(Location, Target->GetActorLocation()) - 200.0f)
		{
			return Desired;
		}
	}
	const float Urgency = Hit.bStartPenetrating ? 1.0f : 1.0f - Hit.Time;
	return (Direction + FVector(Hit.ImpactNormal) * 2.0f * Urgency).GetSafeNormal(UE_SMALL_NUMBER, Direction) * Speed;
}

void AJamFlyer::Bite(APawn* Target)
{
	UPrimitiveComponent* TargetBody = Cast<UPrimitiveComponent>(Target->GetRootComponent());
	const FVector Location = GetActorLocation();
	const FVector Direction = (Target->GetActorLocation() - Location).GetSafeNormal(UE_SMALL_NUMBER, DiveDirection);
	// The blow goes through the impact system like a round: damage, and the shove as its physics impulse.
	FHitResult Hit(Target, TargetBody, Target->GetActorLocation() - Direction * 40.0f, -Direction);
	Hit.bBlockingHit = true;
	FMKImpactResolveParams Params;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, Params, Feedback);
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Blunt;
	Resolved.SourceActor = this;
	Resolved.ImpactTimeSeconds = GetWorld()->GetTimeSeconds();
	Resolved.RangeCm = FVector::Dist(Location, Target->GetActorLocation());
	Resolved.DamageAmount = BiteDamage;
	Resolved.ImpulseNewtonSeconds = Direction * BiteImpulse / 100.0f;
	Resolved.bHasPhysicsImpulse = true;
	const float SpeedBefore = Target->GetVelocity().Size();
	const float OwnSpeed = Body->GetPhysicsLinearVelocity().Size();
	const bool bDelivered = MKDeliverResolvedImpact(this, this, Resolved);
	++BiteCount;
	if (BiteSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, BiteSound, Hit.ImpactPoint, 1.0f, FMath::FRandRange(0.7f, 0.85f), 0.0f, SoundAttenuation);
	}
	if (Feedback.ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Feedback.ImpactSound, Hit.ImpactPoint, 1.0f, 1.0f, 0.0f, SoundAttenuation);
	}
	// It bounces off what it bit and climbs away.
	Body->SetPhysicsLinearVelocity(-Direction * 300.0f + Up(Location) * 500.0f);
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: bite %d on %s at %.0f cm/s - damage %.0f, shove %.0f kg cm/s, %s; the player moved %.0f cm/s before"),
		*GetName(), BiteCount, *GetNameSafe(Target), OwnSpeed, BiteDamage, BiteImpulse, bDelivered ? TEXT("delivered") : TEXT("no receiver"), SpeedBefore);
}

void AJamFlyer::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	const FVector Push = ResolvedImpact.bHasPhysicsImpulse ? ResolvedImpact.ImpulseNewtonSeconds * 100.0 : FVector::ZeroVector;
	// The gun already pushes a simulated body it hits (UJamWeaponsComponent::FireRay): only a hit on a mesh part needs it here.
	const UPrimitiveComponent* HitComponent = ResolvedImpact.Hit.GetComponent();
	const bool bPushHere = !Push.IsNearlyZero() && !(HitComponent && HitComponent->IsSimulatingPhysics());
	if (State == EJamFlyerState::Dead)
	{
		if (bPushHere)
		{
			Body->AddImpulseAtLocation(Push, ResolvedImpact.Hit.ImpactPoint);
		}
		return;
	}
	if (bPushHere)
	{
		Body->AddImpulse(Push);
	}
	Health -= ResolvedImpact.DamageAmount;
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: hit in %s (%s) by %s - damage %.1f, health %.0f/%.0f"), *GetName(), *GetNameSafe(HitComponent),
		*UEnum::GetValueAsString(ResolvedImpact.SurfaceKind), *GetNameSafe(ResolvedImpact.SourceActor.Get()), ResolvedImpact.DamageAmount,
		FMath::Max(Health, 0.0f), MaxHealth);
	if (Health <= 0.0f)
	{
		Die(Push);
	}
	else if (State == EJamFlyerState::Wander)
	{
		Aggro(TEXT("shot"));
	}
	else if (State == EJamFlyerState::Circle)
	{
		StateUntil = GetWorld()->GetTimeSeconds(); // enraged: dive now
	}
}

void AJamFlyer::Die(const FVector& Push)
{
	SetState(EJamFlyerState::Dead, FString::Printf(TEXT("killed, drifting at %.0f cm/s"), Body->GetPhysicsLinearVelocity().Size()));
	Screech(0.55f);
	// A corpse: free to tumble, solid to the player, limp.
	Body->BodyInstance.SetDOFLock(EDOFMode::None);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Block);
	Body->SetPhysicsAngularVelocityInDegrees(FMath::VRand() * FMath::FRandRange(60.0f, 180.0f) + Push.GetSafeNormal() * 90.0f);
	WingR->SetRelativeRotation(WingTurn(1.0f, -30.0f, 25.0f));
	WingL->SetRelativeRotation(WingTurn(-1.0f, -20.0f, 35.0f));
	Tail->SetRelativeRotation(FRotator(-10.0f, 25.0f, 0.0f));
	TailTip->SetRelativeRotation(FRotator(-15.0f, 40.0f, 0.0f));
	Torso->SetRelativeLocation(FVector::ZeroVector);
}

void AJamFlyer::Animate(float DeltaSeconds, const FVector& Velocity, const FVector& Accel)
{
	const float Now = float(GetWorld()->GetTimeSeconds());
	const FVector Location = Body->GetComponentLocation();
	// Turn to the flight and bank into the turn: "lift" is the habitat's up plus the sideways pull.
	const float Speed = Velocity.Size();
	const FVector Forward = Speed > 50.0f ? Velocity / Speed : Visual->GetForwardVector();
	SmoothedAccel = FMath::VInterpTo(SmoothedAccel, Accel, DeltaSeconds, 4.0f);
	const FVector Lateral = SmoothedAccel - Forward * FVector::DotProduct(SmoothedAccel, Forward);
	const FVector Lift = Up(Location) * 980.0f + Lateral.GetClampedToMaxSize(2500.0f);
	const FQuat Target = FRotationMatrix::MakeFromXZ(Forward, Lift).ToQuat();
	Visual->SetWorldRotation(FQuat::Slerp(Visual->GetComponentQuat(), Target, 1.0f - FMath::Exp(-6.0f * DeltaSeconds)));

	// Wings: glide and flap while wandering, hard beats when hunting, tucked back in a dive.
	float Rate = 1.0f, Amplitude = FlapAmplitudeDeg, Sweep = 0.0f, Dihedral = 8.0f;
	switch (State)
	{
	case EJamFlyerState::Wander:
		Amplitude *= 0.25f + 0.75f * FMath::Clamp(0.6f + 0.8f * FMath::Sin(Now * 0.45f + Seed), 0.0f, 1.0f);
		break;
	case EJamFlyerState::Circle:
		Rate = 1.2f;
		break;
	case EJamFlyerState::Dive:
		Rate = 0.8f;
		Amplitude = 6.0f;
		Sweep = 45.0f;
		Dihedral = -4.0f;
		break;
	case EJamFlyerState::Retreat:
		Rate = 1.6f;
		Amplitude *= 1.15f;
		break;
	default:
		break;
	}
	FlapPhase = FMath::Fmod(FlapPhase + DeltaSeconds * UE_TWO_PI * FlapHz * Rate, UE_TWO_PI);
	const float Flap = Dihedral + Amplitude * FMath::Sin(FlapPhase);
	WingR->SetRelativeRotation(WingTurn(1.0f, Flap, Sweep));
	WingL->SetRelativeRotation(WingTurn(-1.0f, Flap, Sweep));
	// The body heaves against the beat.
	Torso->SetRelativeLocation(FVector(0.0f, 0.0f, -0.12f * Amplitude * FMath::Cos(FlapPhase)));

	// The tail swings, the tip a beat later, and trails out of a turn.
	const float Turn = FMath::Clamp(FVector::DotProduct(Lateral, Visual->GetRightVector()) / 1500.0f, -1.0f, 1.0f);
	Tail->SetRelativeRotation(FRotator(5.0f * FMath::Sin(Now * 1.1f + Seed), 10.0f * FMath::Sin(Now * 1.7f + Seed) - 15.0f * Turn, 0.0f));
	TailTip->SetRelativeRotation(FRotator(8.0f * FMath::Sin(Now * 1.1f - 0.8f + Seed), 18.0f * FMath::Sin(Now * 1.7f - 1.1f + Seed) - 10.0f * Turn, 0.0f));
}

void AJamFlyer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (State == EJamFlyerState::Dead)
	{
		return; // the corpse is physics alone
	}
	const double Now = GetWorld()->GetTimeSeconds();
	if (Now >= NextSenseSeconds)
	{
		Sense();
		NextSenseSeconds = Now + 0.1;
	}
	const FVector Location = Body->GetComponentLocation();
	const FVector Velocity = Body->GetPhysicsLinearVelocity();
	Think(Location, Velocity);
	if (State == EJamFlyerState::Dead)
	{
		return;
	}
	float MaxAccel = 0.0f;
	const FVector Desired = Avoid(Location, DesiredVelocity(Location, Velocity, MaxAccel));
	const FVector Accel = ((Desired - Velocity) / 0.35f).GetClampedToMaxSize(MaxAccel);
	Body->AddForce(Accel, NAME_None, /*bAccelChange=*/true);
	Animate(DeltaSeconds, Velocity, Accel);
}

// --- console probes ------------------------------------------------------------------------------

namespace JamFlyerProbe
{
	void After(UWorld* World, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda(MoveTemp(Step)), Seconds, false);
	}

	/** Every Interval until Done() is true (then Next) or Timeout passes (then Next too, logged). */
	void Poll(UWorld* World, float Interval, float Timeout, TFunction<bool()> Done, TFunction<void(bool)> Next)
	{
		if (Done())
		{
			Next(true);
			return;
		}
		if (Timeout <= 0.0f)
		{
			Next(false);
			return;
		}
		After(World, Interval, [World, Interval, Timeout, Done, Next]() { Poll(World, Interval, Timeout - Interval, Done, Next); });
	}

	void Teleport(AActor* Actor, const FVector& Location)
	{
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
		{
			Root->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			return;
		}
		Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}

	AJamZeroGPawn* ProbePawn(UWorld* World)
	{
		const APlayerController* Controller = World->GetFirstPlayerController();
		return Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
	}

	void LogFlyer(const TCHAR* Stage, const AJamFlyer* Flyer, const AJamFlyerHabitat* Home)
	{
		const AJamZeroGPawn* Pawn = Flyer ? ProbePawn(Flyer->GetWorld()) : nullptr;
		if (!Flyer || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer probe [%s]: the flyer or the pawn is gone"), Stage);
			return;
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [%s]: %s %s, %.0f cm/s, spin %.0f deg/s, %.1f m from home, %.1f m from the player; player %.0f cm/s, HP %.0f, %.1f m from home"),
			Stage, *Flyer->GetName(), AJamFlyer::StateName(Flyer->GetState()), Flyer->GetVelocity().Size(),
			Flyer->Body->GetPhysicsAngularVelocityInDegrees().Size(), FVector::Dist(Flyer->GetActorLocation(), Home->GetActorLocation()) / 100.0f,
			FVector::Dist(Flyer->GetActorLocation(), Pawn->GetActorLocation()) / 100.0f, Pawn->GetBodyVelocity().Size(), Pawn->Vitals->Health,
			FVector::Dist(Pawn->GetActorLocation(), Home->GetActorLocation()) / 100.0f);
	}

	/** A point 10-12 m ahead of the flyer, inside its habitat and clear of a planetoid; false while it flies outward. */
	bool InFront(const AJamFlyer* Flyer, const AJamFlyerHabitat* Home, float AheadCm, FVector& Out)
	{
		const FVector Forward = Flyer->Visual->GetForwardVector();
		Out = Flyer->GetActorLocation() + Forward * AheadCm;
		const float FromCentre = FVector::Dist(Out, Home->GetActorLocation());
		return FromCentre < Home->GetOuterRadius() && FromCentre > Home->InnerRadiusCm + 300.0f;
	}

	struct FProbe
	{
		TWeakObjectPtr<AJamFlyer> Flyer;
		TWeakObjectPtr<AJamFlyerHabitat> Home;
		TArray<TWeakObjectPtr<AJamFlyer>> Frozen;
		FVector Far = FVector::ZeroVector;
	};

	void ProbeEnd(UWorld* World, TSharedRef<FProbe> P)
	{
		for (const TWeakObjectPtr<AJamFlyer>& Weak : P->Frozen)
		{
			if (AJamFlyer* Other = Weak.Get())
			{
				Other->SetActorTickEnabled(true);
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe: done"));
	}

	void ProbeKill(UWorld* World, TSharedRef<FProbe> P)
	{
		Poll(World, 0.2f, 10.0f, [P]()
		{
			FVector Spot;
			return P->Flyer.IsValid() && P->Home.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1200.0f, Spot);
		}, [World, P](bool bOk)
		{
			AJamZeroGPawn* Pawn = ProbePawn(World);
			AJamFlyer* Flyer = P->Flyer.Get();
			if (!Pawn || !Flyer)
			{
				return;
			}
			FVector Spot;
			InFront(Flyer, P->Home.Get(), 1200.0f, Spot);
			Teleport(Pawn, Spot);
			if (!Pawn->Guns->IsWeaponUnlocked(0))
			{
				Pawn->Guns->UnlockWeapon(0);
			}
			Pawn->Guns->SelectWeapon(0);
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: the player 12 m ahead of %s (%s), the pistol out: %s"), *Flyer->GetName(),
				bOk ? TEXT("it flew inward") : TEXT("placed anyway after 10 s"), *Pawn->Guns->Describe());
			// A shot every 0.3 s at where it is now, until it dies or 20 shots.
			for (int32 Shot = 0; Shot < 20; ++Shot)
			{
				After(World, 0.3f + Shot * 0.3f, [World, P, Shot]()
				{
					AJamZeroGPawn* Now = ProbePawn(World);
					AJamFlyer* Target = P->Flyer.Get();
					if (!Now || !Target || Target->GetState() == EJamFlyerState::Dead)
					{
						return;
					}
					Now->GetController()->SetControlRotation((Target->GetActorLocation() - Now->Camera->GetComponentLocation()).Rotation());
					const bool bFired = Now->Fire();
					UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: shot %d %s at %.1f m, flyer %s, health %.0f"), Shot + 1, bFired ? TEXT("fired") : TEXT("not fired"),
						FVector::Dist(Target->GetActorLocation(), Now->GetActorLocation()) / 100.0f, AJamFlyer::StateName(Target->GetState()), Target->GetHealth());
				});
			}
			Poll(World, 0.3f, 8.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Dead; }, [World, P](bool bDead)
			{
				if (!bDead)
				{
					UE_LOG(LogMindKore, Warning, TEXT("Flyer probe [shoot]: still alive after the shots"));
					ProbeEnd(World, P);
					return;
				}
				for (int32 Step = 0; Step <= 4; ++Step)
				{
					After(World, Step * 1.0f, [P, Step]() { LogFlyer(*FString::Printf(TEXT("corpse +%d s"), Step), P->Flyer.Get(), P->Home.Get()); });
				}
				After(World, 4.5f, [World, P]() { ProbeEnd(World, P); });
			});
		});
	}

	void ProbeLeave(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Pawn)
		{
			return;
		}
		Teleport(Pawn, P->Far);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [leave]: the player moved %.1f m from the habitat's centre (radius %.0f m + leash %.0f m)"),
			FVector::Dist(P->Far, P->Home->GetActorLocation()) / 100.0f, P->Home->GetOuterRadius() / 100.0f, P->Home->LeashCm / 100.0f);
		Poll(World, 0.1f, 3.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Wander; }, [World, P](bool bCalm)
		{
			LogFlyer(bCalm ? TEXT("leave: back to wandering") : TEXT("leave: STILL HUNTING after 3 s"), P->Flyer.Get(), P->Home.Get());
			After(World, 2.0f, [World, P]() { LogFlyer(TEXT("leave +2 s"), P->Flyer.Get(), P->Home.Get()); ProbeKill(World, P); });
		});
	}

	/** Behind the planetoid (a shell habitat only): the far side's surface, inside the leash, out of sight. */
	void ProbeHide(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		const AJamFlyer* Flyer = P->Flyer.Get();
		const AJamFlyerHabitat* Home = P->Home.Get();
		if (!Pawn || !Flyer || !Home || Home->InnerRadiusCm <= 0.0f)
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hide]: skipped - no planetoid in this habitat to hide behind (lost sight is not probed here)"));
			ProbeLeave(World, P);
			return;
		}
		const FVector Centre = Home->GetActorLocation();
		const FVector Spot = Centre - (Flyer->GetActorLocation() - Centre).GetSafeNormal() * (Home->InnerRadiusCm - 200.0f);
		Teleport(Pawn, Spot);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hide]: the player on the far side of the planetoid, %.1f m from the centre (inside the leash)"), FVector::Dist(Spot, Centre) / 100.0f);
		Poll(World, 0.25f, 12.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->GetState() == EJamFlyerState::Wander; }, [World, P](bool bCalm)
		{
			LogFlyer(bCalm ? TEXT("hide: back to wandering") : TEXT("hide: STILL HUNTING after 12 s"), P->Flyer.Get(), P->Home.Get());
			// Find it again before the leash step: in sight of it, so it hunts once more.
			After(World, 3.5f, [World, P]()
			{
				Poll(World, 0.2f, 10.0f, [P]()
				{
					FVector Spot;
					return P->Flyer.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
				}, [World, P](bool)
				{
					FVector Spot;
					if (AJamZeroGPawn* Now = ProbePawn(World); Now && P->Flyer.IsValid())
					{
						InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
						Teleport(Now, Spot);
					}
					Poll(World, 0.1f, 3.0f, [P]() { return !P->Flyer.IsValid() || P->Flyer->IsHunting(); }, [World, P](bool bHunting)
					{
						LogFlyer(bHunting ? TEXT("hide: hunting again") : TEXT("hide: did not notice the player again"), P->Flyer.Get(), P->Home.Get());
						After(World, 1.0f, [World, P]() { ProbeLeave(World, P); });
					});
				});
			});
		});
	}

	/** jam.Flyer.Probe [all]: wander, sight, aggro, bites, the player hides (a planetoid) and leaves, shot dead, the corpse. */
	void RunFlyerProbe(const TArray<FString>& Args, UWorld* World)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		// Args: "all", and a part of a habitat's name or label (the first habitat otherwise).
		const bool bAll = Args.Contains(TEXT("all"));
		const FString* Filter = Args.FindByPredicate([](const FString& Arg) { return Arg != TEXT("all"); });
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
#if WITH_EDITOR
			const FString Label = It->GetActorLabel();
#else
			const FString Label;
#endif
			if (!Filter || It->GetName().Contains(*Filter) || Label.Contains(*Filter))
			{
				Home = *It;
			}
		}
		if (!Pawn || !Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer probe: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		TSharedRef<FProbe> P = MakeShared<FProbe>();
		P->Home = Home;
		P->Flyer = Home->GetFlock()[0];
		// One subject; its flockmates hold still unless "all" (a screech still wakes them: they hunt once the probe ends).
		for (int32 Index = 1; Index < Home->GetFlock().Num() && !bAll; ++Index)
		{
			if (AJamFlyer* Other = Home->GetFlock()[Index].Get())
			{
				Other->SetActorTickEnabled(false);
				Other->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				P->Frozen.Add(Other);
			}
		}
		const FVector Centre = Home->GetActorLocation();
		P->Far = Centre + (Pawn->GetActorLocation() - Centre).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector) * (Home->GetOuterRadius() + Home->LeashCm + 800.0f);
		Teleport(Pawn, P->Far);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe: habitat %s, subject %s, %d flockmates %s; the player waits %.1f m from the centre"), *Home->GetName(),
			*P->Flyer->GetName(), Home->GetFlock().Num() - 1, bAll ? TEXT("free") : TEXT("held still"), FVector::Dist(P->Far, Centre) / 100.0f);
		for (int32 Step = 0; Step < 4; ++Step)
		{
			After(World, 0.5f + Step * 1.0f, [P, Step]() { LogFlyer(*FString::Printf(TEXT("wander %d"), Step), P->Flyer.Get(), P->Home.Get()); });
		}
		After(World, 4.0f, [World, P]()
		{
			Poll(World, 0.2f, 10.0f, [P]()
			{
				FVector Spot;
				return P->Flyer.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
			}, [World, P](bool bOk)
			{
				AJamZeroGPawn* Now = ProbePawn(World);
				if (!Now || !P->Flyer.IsValid())
				{
					return;
				}
				FVector Spot;
				InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
				Teleport(Now, Spot);
				UE_LOG(LogMindKore, Log, TEXT("Flyer probe [sight]: the player 10 m ahead of %s%s"), *P->Flyer->GetName(), bOk ? TEXT("") : TEXT(" (it was flying outward; placed anyway)"));
				const int32 BitesBefore = P->Flyer->GetBiteCount();
				for (int32 Step = 0; Step < 30; ++Step)
				{
					After(World, 0.5f * Step, [P, Step]() { LogFlyer(*FString::Printf(TEXT("hunt %.1f s"), 0.5f * Step), P->Flyer.Get(), P->Home.Get()); });
				}
				// Two bites, or 15 s.
				Poll(World, 0.1f, 15.0f, [P, BitesBefore]() { return !P->Flyer.IsValid() || P->Flyer->GetBiteCount() >= BitesBefore + 2; }, [World, P](bool bBit)
				{
					UE_LOG(LogMindKore, Log, TEXT("Flyer probe [hunt]: %s"), bBit ? TEXT("two bites landed") : TEXT("fewer than two bites in 15 s"));
					ProbeHide(World, P);
				});
			});
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerProbe(
		TEXT("jam.Flyer.Probe"),
		TEXT("jam.Flyer.Probe [all] [Habitat]: one flyer of the first (or the named) habitat - wander, sight, aggro, bites, the player hides and leaves (de-aggro), shot dead, the corpse drifts. 'all' lets its flockmates fly too."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerProbe));

	/**
	 * jam.Flyer.Shots: rendered stills (needs a real RHI). The flyers stop noticing
	 * the player; the camera rides along the first habitat's first flyer - ahead
	 * above, below, behind above, wider - then looks over the flock.
	 * "Shot" every 3 s; files in Saved/Screenshots.
	 */
	void RunFlyerShots(UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		if (!Home || Home->GetFlock().IsEmpty() || !ProbePawn(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer shots: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Home->GetFlock())
		{
			if (AJamFlyer* Flyer = Weak.Get())
			{
				Flyer->SightRangeCm = 0.0f;
			}
		}
		TWeakObjectPtr<AJamFlyer> Subject = Home->GetFlock()[0];
		TWeakObjectPtr<AJamFlyerHabitat> WeakHome = Home;
		struct FView { const TCHAR* Name; FVector Offset; }; // flyer frame: X forward, Y right, Z up
		const TArray<FView> Views = {
			{ TEXT("front_above"), FVector(320.0f, 220.0f, 130.0f) },
			{ TEXT("below"), FVector(40.0f, 90.0f, -320.0f) },
			{ TEXT("behind_above"), FVector(-380.0f, 140.0f, 220.0f) },
			{ TEXT("wide"), FVector(900.0f, -300.0f, 350.0f) },
			{ TEXT("flock"), FVector::ZeroVector },
		};
		for (int32 Index = 0; Index < Views.Num(); ++Index)
		{
			const FView View = Views[Index];
			// Ride along every 10 ms for 2.5 s, then the shot.
			for (int32 Tick = 0; Tick < 250; ++Tick)
			{
				After(World, Index * 3.0f + Tick * 0.01f, [World, Subject, WeakHome, View]()
				{
					AJamZeroGPawn* Pawn = ProbePawn(World);
					const AJamFlyer* Flyer = Subject.Get();
					const AJamFlyerHabitat* Home = WeakHome.Get();
					if (!Pawn || !Flyer || !Home)
					{
						return;
					}
					FVector Eye, Look;
					if (View.Offset.IsZero())
					{
						const FVector Up = Home->InnerRadiusCm > 0.0f ? (Flyer->GetActorLocation() - Home->GetActorLocation()).GetSafeNormal() : FVector::UpVector;
						Eye = Flyer->GetActorLocation() + Up * 1500.0f + (Flyer->GetActorLocation() - Home->GetActorLocation()).GetSafeNormal() * 500.0f;
						Look = Home->InnerRadiusCm > 0.0f ? Flyer->GetActorLocation() : Home->GetActorLocation();
					}
					else
					{
						Eye = Flyer->Visual->GetComponentTransform().TransformPosition(View.Offset);
						Look = Flyer->GetActorLocation();
					}
					Teleport(Pawn, Eye - (Pawn->Camera->GetComponentLocation() - Pawn->GetActorLocation()));
					Pawn->GetController()->SetControlRotation((Look - Eye).Rotation());
				});
			}
			After(World, Index * 3.0f + 2.6f, [World, Subject, View, Index]()
			{
				const AJamFlyer* Flyer = Subject.Get();
				UE_LOG(LogMindKore, Log, TEXT("Flyer shots: %d %s, %s %s at %.0f cm/s"), Index, View.Name, *GetNameSafe(Flyer),
					Flyer ? AJamFlyer::StateName(Flyer->GetState()) : TEXT("-"), Flyer ? Flyer->GetVelocity().Size() : 0.0f);
				World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
			});
		}
	}

	FAutoConsoleCommandWithWorld GFlyerShots(
		TEXT("jam.Flyer.Shots"),
		TEXT("Rendered stills riding along a flyer (ahead above, below, behind above, wider) and over its flock (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerShots));
}
