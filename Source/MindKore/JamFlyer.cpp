#include "JamFlyer.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "Containers/Ticker.h"
#include "JamClipRecorder.h"
#include "JamShip.h"
#include "JamVisorBlood.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MKImpactDelivery.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Components/AudioComponent.h"
#include "JamOxygen.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundSubmix.h"
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
	FleshMaterial = Flesh.Object; // set on the body in BeginPlay (not in a constructor: the CDO has no engine yet)

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

	// The stems of Scripts/jam_flyer_sounds.py (recorded CC0 donors, CREDITS.md): per event Attack,
	// Body, Character, Sub and the suit's Tail (vacuum only); 0 dB plays the rendered mix.
	const auto Stem = [](const TCHAR* EventName, const TCHAR* StemRole, float JitterDb, bool bVacuumOnly = false)
	{
		FJamSoundLayer Layer;
		const FString Name = FString::Printf(TEXT("SW_Jam_Flyer_%s_%s"), EventName, StemRole);
		const FString Path = FString::Printf(TEXT("/Game/JamArt/Audio/Flyer/%s.%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<USoundBase> Finder(*Path); // loads now (the optional finder keeps the pointer)
		Layer.Sound = Finder.Object;
		Layer.GainJitterDb = JitterDb;
		Layer.bVacuumOnly = bVacuumOnly;
		return Layer;
	};
	const auto Event = [&Stem](const TCHAR* Name, std::initializer_list<const TCHAR*> Roles, float Volume, FVector2D Pitch)
	{
		FJamSoundEvent Result;
		for (const TCHAR* StemRole : Roles)
		{
			const bool bTail = FCString::Strcmp(StemRole, TEXT("Tail")) == 0;
			const bool bAttack = FCString::Strcmp(StemRole, TEXT("Attack")) == 0;
			Result.Layers.Add(Stem(Name, StemRole, bAttack || FCString::Strcmp(StemRole, TEXT("Character")) == 0 ? 2.0f : 1.0f, bTail));
		}
		Result.Volume = Volume;
		Result.Pitch = Pitch;
		return Result;
	};
	// Levels (Docs/Flyers.md, "the mix"): the stems are normalised per event; these keep a screech
	// 5-7 dB under the player's own pistol at 10 m and every wing beat far under both.
	AggroSound = Event(TEXT("Aggro"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.94f, 1.06f));
	DiveSound = Event(TEXT("Dive"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.97f, 1.07f));
	DeathSound = Event(TEXT("Death"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 0.8f, FVector2D(0.95f, 1.03f));
	FlapSound = Event(TEXT("Flap"), { TEXT("Attack"), TEXT("Body"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.94f, 1.06f));
	FlyBySound = Event(TEXT("FlyBy"), { TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 0.9f, FVector2D(0.9f, 1.1f));
	HitSound = Event(TEXT("Hit"), { TEXT("Attack"), TEXT("Body"), TEXT("Sub"), TEXT("Tail") }, 0.9f, FVector2D(0.9f, 1.1f));
	BiteSound = Event(TEXT("Bite"), { TEXT("Attack"), TEXT("Body"), TEXT("Character"), TEXT("Sub"), TEXT("Tail") }, 1.0f, FVector2D(0.95f, 1.05f));
	{
		// MindKore's gristle crunch under the bite (recorded donors, CREDITS.md).
		static ConstructorHelpers::FObjectFinder<USoundBase> Crunch(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Limb.SC_MK_Hit_Limb"));
		FJamSoundLayer Layer;
		Layer.Sound = Crunch.Object;
		Layer.GainDb = -6.0f;
		Layer.GainJitterDb = 2.0f;
		BiteSound.Layers.Add(Layer);
	}
	DiveSound.AirReverbSend = 0.2f;
	FlapSound.AirReverbSend = 0.15f;
	FlapSound.StartJitterSeconds = 0.01f;
	static ConstructorHelpers::FObjectFinder<USoundBase> FlutterWave(TEXT("/Game/JamArt/Audio/Flyer/SW_Jam_Flyer_Flutter_Loop.SW_Jam_Flyer_Flutter_Loop"));
	FlutterLoop = FlutterWave.Object;
	static ConstructorHelpers::FObjectFinder<USoundMix> Duck(TEXT("/Game/JamArt/Audio/Mix/SMIX_Jam_FlyerDuck.SMIX_Jam_FlyerDuck"));
	AggroDuck = Duck.Object;
	static ConstructorHelpers::FObjectFinder<USoundSubmixBase> AirReverbSubmix(TEXT("/Game/JamArt/Audio/Mix/SM_Jam_AirReverb.SM_Jam_AirReverb"));
	AirReverb = AirReverbSubmix.Object;
	VoiceAttenuation = JamFlyerAttenuation(5000.0f, 800.0f);
	// A call carries: full volume inside 6 m, -40 dB at 50 m (jam.Flyer.Clip takes, max momentary loudness
	// over 0.6 s: with 2 m and -60 dB a dive shriek at 10-14 m sat 11-13 dB under the pistol shot, quieter
	// than a bite; the aim is 5-7 dB under the player's own shot).
	VoiceAttenuation.AttenuationShapeExtents = FVector(600.0f, 0.0f, 0.0f);
	VoiceAttenuation.dBAttenuationAtMax = -40.0f;
	WingsAttenuation = JamFlyerAttenuation(2500.0f, 400.0f);
	ImpactAttenuation = JamFlyerAttenuation(3000.0f, 600.0f);
}

void AJamFlyer::BeginPlay()
{
	Super::BeginPlay();
	Health = MaxHealth;
	Seed = FMath::FRandRange(0.0f, 100.0f);
	HomeCentre = GetActorLocation();
	Body->SetMassOverrideInKg(NAME_None, MassKg, true);
	Body->SetPhysMaterialOverride(FleshMaterial);
	Body->SetPhysicsLinearVelocity(FMath::VRand() * CruiseSpeedCm);
	AllFlyers.Add(this);
	if (FlutterLoop)
	{
		Flutter = NewObject<UAudioComponent>(this);
		Flutter->bAutoActivate = false;
		Flutter->bAllowSpatialization = true;
		Flutter->bOverrideAttenuation = true;
		Flutter->AttenuationOverrides = WingsAttenuation;
		Flutter->SetSound(FlutterLoop);
		Flutter->SetupAttachment(Torso);
		Flutter->RegisterComponent();
	}
}

void AJamFlyer::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	AllFlyers.Remove(this);
	Super::EndPlay(EndPlayReason);
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

namespace JamFlyerParts
{
	TAutoConsoleVariable<bool> CVarLogFlaps(TEXT("jam.Flyer.LogFlaps"), false, TEXT("Log every audible wing beat of every flyer (jam.Flyer.Probe turns it on)."));
	/** The screeches playing now (their flyers), for Mix.MaxScreeches. */
	TArray<TWeakObjectPtr<AJamFlyer>> Screamers;
}

TArray<TWeakObjectPtr<AJamFlyer>> AJamFlyer::AllFlyers;

bool AJamFlyer::ListenerInAir() const
{
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Player());
	return Pawn && Pawn->Oxygen && Pawn->Oxygen->IsBreathable();
}

bool AJamFlyer::WingsHeard() const
{
	const APawn* Listener = Player();
	if (!Listener || State == EJamFlyerState::Dead)
	{
		return false;
	}
	const float Mine = FVector::DistSquared(Listener->GetActorLocation(), GetActorLocation());
	if (Mine > FMath::Square(WingsAttenuation.FalloffDistance + 200.0f))
	{
		return false;
	}
	int32 Nearer = 0;
	for (const TWeakObjectPtr<AJamFlyer>& Weak : AllFlyers)
	{
		const AJamFlyer* Other = Weak.Get();
		if (Other && Other != this && Other->GetWorld() == GetWorld() && Other->State != EJamFlyerState::Dead
			&& FVector::DistSquared(Listener->GetActorLocation(), Other->GetActorLocation()) < Mine)
		{
			++Nearer;
		}
	}
	return Nearer < Mix.MaxWingSets;
}

bool AJamFlyer::TakeScreechSlot(const TCHAR* Why)
{
	const APawn* Listener = Player();
	const FVector Ear = Listener ? Listener->GetActorLocation() : GetActorLocation();
	Screamers.RemoveAll([this](const TWeakObjectPtr<AJamFlyer>& Weak)
	{
		return !Weak.IsValid() || Weak.Get() == this || Weak->GetWorld() != GetWorld() || !Weak->VoicePlay.IsPlaying();
	});
	if (Screamers.Num() >= Mix.MaxScreeches)
	{
		// The quietest goes first: the farthest of the playing ones, if this one is nearer.
		AJamFlyer* Farthest = nullptr;
		for (const TWeakObjectPtr<AJamFlyer>& Weak : Screamers)
		{
			if (!Farthest || FVector::DistSquared(Weak->GetActorLocation(), Ear) > FVector::DistSquared(Farthest->GetActorLocation(), Ear))
			{
				Farthest = Weak.Get();
			}
		}
		if (FVector::DistSquared(GetActorLocation(), Ear) >= FVector::DistSquared(Farthest->GetActorLocation(), Ear))
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: screech (%s) dropped - %d nearer screeches playing"), *GetName(), Why, Screamers.Num());
			return false;
		}
		Farthest->VoicePlay.Stop(0.1f);
		Screamers.Remove(Farthest);
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: screech (%s) takes the place of %s's (%.1f m away, the farthest)"), *GetName(), Why,
			*Farthest->GetName(), FVector::Dist(Farthest->GetActorLocation(), Ear) / 100.0f);
	}
	Screamers.Add(this);
	return true;
}

void AJamFlyer::Screech(const TCHAR* Why, bool bDive)
{
	if (!TakeScreechSlot(Why))
	{
		return;
	}
	VoicePlay.Stop(0.05f);
	const bool bAir = ListenerInAir();
	VoicePlay = JamPlaySoundEvent(this, bDive ? DiveSound : AggroSound, FVector::ZeroVector, Torso, VoiceAttenuation, bAir, AirReverb);
	bVoiceDoppler = bDive;
	if (!bDive && AggroDuck)
	{
		UGameplayStatics::PushSoundMixModifier(this, AggroDuck);
	}
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: screech (%s) - %s, %s%s; %.1f m from the player"), *GetName(), Why, bDive ? TEXT("Dive") : TEXT("Aggro"),
		*VoicePlay.Describe(), bDive ? TEXT(", Doppler") : TEXT(", ambience ducked"),
		Player() ? FVector::Dist(Player()->GetActorLocation(), GetActorLocation()) / 100.0f : -1.0f);
}

float AJamFlyer::Doppler() const
{
	const APawn* Listener = Player();
	if (!Listener || Mix.DopplerScale <= 0.0f)
	{
		return 1.0f;
	}
	// f' = f (c + v_listener) / (c - v_source), along the line between them; scaled in log pitch.
	constexpr float SpeedOfSound = 34300.0f;
	const FVector ToListener = (Listener->GetActorLocation() - GetActorLocation()).GetSafeNormal();
	const float Source = FVector::DotProduct(GetVelocity(), ToListener);
	const float Receiver = FVector::DotProduct(Listener->GetVelocity(), -ToListener);
	const float Ratio = (SpeedOfSound + Receiver) / FMath::Max(SpeedOfSound - Source, 1000.0f);
	return FMath::Clamp(FMath::Pow(Ratio, Mix.DopplerScale), 0.5f, 2.0f);
}

void AJamFlyer::PlayFlap(float Strength, float Rate)
{
	if (Strength < 0.35f || !WingsHeard())
	{
		return;
	}
	// A big beat is louder and opens up; a fast one is higher; a half-glide beat is soft and dark.
	const float Size = FMath::Clamp(Strength, 0.0f, 1.2f);
	const float Speed = FMath::Clamp((Rate - 0.8f) / 0.8f, 0.0f, 1.0f);
	const FJamSoundPlay Play = JamPlaySoundEvent(this, FlapSound, FVector::ZeroVector, Torso, WingsAttenuation, ListenerInAir(), AirReverb,
		FlapVolume * Size, FMath::Lerp(0.92f, 1.12f, Speed), FMath::Lerp(0.45f, 1.0f, FMath::Min(Size, 1.0f)));
	if (CVarLogFlaps.GetValueOnGameThread())
	{
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: flap - size %.2f, rate %.1f: %s, %s, %.1f m from the player"), *GetName(), Size, Rate,
			*Play.Describe(), StateName(State), Player() ? FVector::Dist(Player()->GetActorLocation(), GetActorLocation()) / 100.0f : -1.0f);
	}
}

void AJamFlyer::UpdateSounds(float DeltaSeconds)
{
	const APawn* Listener = Player();
	// Doppler on the dive shriek and the fly-by while they play (Pitch is the drawn pitch, without it).
	if (bVoiceDoppler && VoicePlay.IsPlaying())
	{
		VoicePlay.SetPitch(VoicePlay.Pitch * Doppler());
	}
	if (FlyByPlay.IsPlaying())
	{
		FlyByPlay.SetPitch(FlyByPlay.Pitch * Doppler());
	}
	if (!Listener)
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	// A close pass: the whoosh starts a quarter second before the closest approach.
	const FVector Offset = Listener->GetActorLocation() - GetActorLocation();
	const FVector Relative = GetVelocity() - Listener->GetVelocity();
	const float RelativeSpeed = Relative.Size();
	if (RelativeSpeed > FlyByMinSpeedCm && Now - LastFlyBySeconds > 1.5)
	{
		const float ToClosest = FVector::DotProduct(Offset, Relative) / FMath::Square(RelativeSpeed);
		const float Miss = (Offset - Relative * ToClosest).Size();
		if (ToClosest > 0.0f && ToClosest < 0.25f && Miss < FlyByDistanceCm)
		{
			LastFlyBySeconds = Now;
			FlyByPlay = JamPlaySoundEvent(this, FlyBySound, FVector::ZeroVector, Torso, WingsAttenuation, ListenerInAir(), AirReverb,
				FMath::Clamp(RelativeSpeed / 1800.0f, 0.6f, 1.2f));
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: fly-by - %.1f m miss at %.0f cm/s, closest in %.2f s: %s"), *GetName(), Miss / 100.0f,
				RelativeSpeed, ToClosest, *FlyByPlay.Describe());
		}
	}
	// The cloth flutter between beats, with the speed; only for the wings that are heard.
	if (Flutter)
	{
		const float Target = WingsHeard() ? FlutterVolume * FMath::Clamp(GetVelocity().Size() / 1200.0f, 0.3f, 1.2f) : 0.0f;
		if (Target > 0.0f && !Flutter->IsPlaying())
		{
			Flutter->SetLowPassFilterEnabled(true);
			Flutter->SetLowPassFilterFrequency(ListenerInAir() ? 9000.0f : 3500.0f);
			Flutter->SetVolumeMultiplier(Target);
			Flutter->Play(FMath::FRandRange(0.0f, 1.9f));
		}
		else if (Target <= 0.0f && Flutter->IsPlaying())
		{
			Flutter->FadeOut(0.3f, 0.0f);
		}
		else if (Target > 0.0f)
		{
			Flutter->SetVolumeMultiplier(Target);
		}
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
	Screech(TEXT("aggro"), false);
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
			Screech(TEXT("dive"), true);
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
	const FJamSoundPlay BitePlay = JamPlaySoundEvent(this, BiteSound, Hit.ImpactPoint, nullptr, ImpactAttenuation, ListenerInAir(), AirReverb);
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: bite sound - %s"), *GetName(), *BitePlay.Describe());
	// On the player's side: the camera jolts and blood hits the visor.
	if (AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(Target))
	{
		Pawn->KickCamera(BiteShakeCm);
		FVector2D Blot(-1.0, -1.0);
		if (BiteVisorBlood > 0.0f && Target->IsPlayerControlled())
		{
			Blot = UJamVisorBloodComponent::FindOrAdd(Pawn)->Splash(BiteVisorBlood);
		}
		UE_LOG(LogMindKore, Log, TEXT("Flyer %s: bite feedback - camera shake %.0f cm, visor blood %.2f at (%.2f, %.2f)"), *GetName(), BiteShakeCm,
			Blot.X >= 0.0 ? BiteVisorBlood : 0.0f, Blot.X, Blot.Y);
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
	// The thwack of a round into it, at the hit, louder with the damage.
	const FJamSoundPlay HitPlay = JamPlaySoundEvent(this, HitSound, ResolvedImpact.Hit.ImpactPoint, nullptr, ImpactAttenuation, ListenerInAir(),
		AirReverb, FMath::Clamp(ResolvedImpact.DamageAmount / 25.0f, 0.6f, 1.2f));
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: hit sound - %s"), *GetName(), *HitPlay.Describe());
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
	DiedSeconds = GetWorld()->GetTimeSeconds();
	VoicePlay.Stop(0.05f);
	if (Flutter)
	{
		Flutter->FadeOut(0.2f, 0.0f);
	}
	VoicePlay = JamPlaySoundEvent(this, DeathSound, FVector::ZeroVector, Torso, VoiceAttenuation, ListenerInAir(), AirReverb);
	bVoiceDoppler = false;
	UE_LOG(LogMindKore, Log, TEXT("Flyer %s: death cry - %s"), *GetName(), *VoicePlay.Describe());
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
	const float LastPhase = FlapPhase;
	FlapPhase = FMath::Fmod(FlapPhase + DeltaSeconds * UE_TWO_PI * FlapHz * Rate, UE_TWO_PI);
	// The beat is heard mid-downstroke (the wing passes level going down: phase pi).
	if (LastPhase < UE_PI && FlapPhase >= UE_PI)
	{
		PlayFlap(Amplitude / FMath::Max(FlapAmplitudeDeg, 1.0f), Rate);
	}
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

void AJamFlyer::FallAsCorpse(float DeltaSeconds)
{
	// Gravity zones pull only the pawn (AJamShipGameMode); a corpse inside one falls the same way.
	const AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>();
	const FVector Location = Body->GetComponentLocation();
	const AJamGravityZone* Zone = Mode ? Mode->GravityZoneAt(Location) : nullptr;
	if (!Zone)
	{
		return; // zero-G: it drifts
	}
	const FVector Down = Zone->GravityAt(Location);
	Body->AddForce(Down * Zone->GravityCm, NAME_None, /*bAccelChange=*/true);
	// The body is a sphere: without losses it rolls down the planetoid's slopes for ever. A carcass drags.
	if (Body->GetAngularDamping() < CorpseAngularDamping)
	{
		Body->SetLinearDamping(CorpseLinearDamping);
		Body->SetAngularDamping(CorpseAngularDamping);
	}
	// Once, for the probe and the log: at rest on the ground.
	if (!bLanded && GetWorld()->GetTimeSeconds() - DiedSeconds > 0.5 && Body->GetPhysicsLinearVelocity().Size() < 30.0f)
	{
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerGround), false, this);
		if (GetWorld()->LineTraceSingleByChannel(Ground, Location, Location + Down * 150.0f, ECC_Visibility, Params))
		{
			bLanded = true;
			UE_LOG(LogMindKore, Log, TEXT("Flyer %s: corpse landed on %s in %s, %.1f s after death, %.0f cm above the ground"), *GetName(),
				*GetNameSafe(Ground.GetActor()), *GetNameSafe(Zone), float(GetWorld()->GetTimeSeconds() - DiedSeconds), Ground.Distance);
		}
	}
}

void AJamFlyer::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (State == EJamFlyerState::Dead)
	{
		FallAsCorpse(DeltaSeconds);
		return;
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
	UpdateSounds(DeltaSeconds);
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
		JamFlyerParts::CVarLogFlaps->Set(false, ECVF_SetByCode);
		UE_LOG(LogMindKore, Log, TEXT("Flyer probe: done"));
	}

	/** Pistol shots every 0.3 s at where the flyer is now, until it dies (20 at most); then the corpse. */
	void ProbeShoot(UWorld* World, TSharedRef<FProbe> P)
	{
		for (int32 Shot = 0; Shot < 20; ++Shot)
		{
			After(World, 0.01f + Shot * 0.3f, [World, P, Shot]() // a timer of 0 s never fires
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
			// Around a planetoid the corpse falls to it (its "landed" line is the flyer's own); elsewhere it drifts.
			const bool bShell = P->Home.IsValid() && P->Home->InnerRadiusCm > 0.0f;
			const int32 Seconds = bShell ? 15 : 4;
			for (int32 Step = 0; Step <= Seconds; ++Step)
			{
				After(World, Step * 1.0f, [P, Step]() { LogFlyer(*FString::Printf(TEXT("corpse +%d s"), Step), P->Flyer.Get(), P->Home.Get()); });
			}
			After(World, Seconds + 0.5f, [World, P]() { ProbeEnd(World, P); });
		});
	}

	void ProbeKill(UWorld* World, TSharedRef<FProbe> P)
	{
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Pawn || !P->Flyer.IsValid() || !P->Home.IsValid())
		{
			return;
		}
		if (!Pawn->Guns->IsWeaponUnlocked(0))
		{
			Pawn->Guns->UnlockWeapon(0);
		}
		Pawn->Guns->SelectWeapon(0);
		const AJamFlyerHabitat* Home = P->Home.Get();
		if (Home->InnerRadiusCm > 0.0f)
		{
			// Around a planetoid: the player on the ground under it, so it dies diving low, inside the planetoid's gravity.
			const FVector Centre = Home->GetActorLocation();
			const FVector Spot = Centre + (P->Flyer->GetActorLocation() - Centre).GetSafeNormal() * (Home->InnerRadiusCm - 200.0f);
			Teleport(Pawn, Spot);
			P->Flyer->Aggro(TEXT("probe: the player on the ground below"));
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: the player dropped on the planetoid under %s, %.1f m from the centre, %s"),
				*P->Flyer->GetName(), FVector::Dist(Spot, Centre) / 100.0f, *Pawn->Guns->Describe());
			Poll(World, 0.05f, 15.0f, [World, P]()
			{
				const AJamZeroGPawn* Now = ProbePawn(World);
				return !P->Flyer.IsValid() || !Now
					|| (P->Flyer->GetState() == EJamFlyerState::Dive && FVector::Dist(P->Flyer->GetActorLocation(), Now->GetActorLocation()) < 900.0f);
			}, [World, P](bool bDiving)
			{
				LogFlyer(bDiving ? TEXT("shoot: it dives at the player") : TEXT("shoot: no dive in 15 s, shooting anyway"), P->Flyer.Get(), P->Home.Get());
				ProbeShoot(World, P);
			});
			return;
		}
		Poll(World, 0.2f, 10.0f, [P]()
		{
			FVector Spot;
			return P->Flyer.IsValid() && P->Home.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1200.0f, Spot);
		}, [World, P](bool bOk)
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			AJamFlyer* Flyer = P->Flyer.Get();
			if (!Now || !Flyer)
			{
				return;
			}
			FVector Spot;
			InFront(Flyer, P->Home.Get(), 1200.0f, Spot);
			Teleport(Now, Spot);
			UE_LOG(LogMindKore, Log, TEXT("Flyer probe [shoot]: the player 12 m ahead of %s (%s), the pistol out: %s"), *Flyer->GetName(),
				bOk ? TEXT("it flew inward") : TEXT("placed anyway after 10 s"), *Now->Guns->Describe());
			After(World, 0.3f, [World, P]() { ProbeShoot(World, P); });
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
		JamFlyerParts::CVarLogFlaps->Set(true, ECVF_SetByCode); // every audible beat in the log while the probe runs
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
	 * jam.Flyer.BiteShot: rendered stills of a bite (needs a real RHI). The first
	 * habitat's first flyer gets the player 10 m ahead (the rest stop noticing);
	 * the view follows it until it bites, then "Shot" at +0.2, +1.2 and +2.5 s:
	 * the visor blood landing, running and fading.
	 */
	void RunFlyerBiteShot(UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer bite shot: needs the zero-G pawn and a habitat with flyers"));
			return;
		}
		TSharedRef<FProbe> P = MakeShared<FProbe>();
		P->Home = Home;
		P->Flyer = Home->GetFlock()[0];
		for (int32 Index = 1; Index < Home->GetFlock().Num(); ++Index)
		{
			if (AJamFlyer* Other = Home->GetFlock()[Index].Get())
			{
				Other->SightRangeCm = 0.0f;
			}
		}
		Poll(World, 0.2f, 10.0f, [P]()
		{
			FVector Spot;
			return P->Flyer.IsValid() && InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
		}, [World, P](bool)
		{
			AJamZeroGPawn* Now = ProbePawn(World);
			if (!Now || !P->Flyer.IsValid())
			{
				return;
			}
			FVector Spot;
			InFront(P->Flyer.Get(), P->Home.Get(), 1000.0f, Spot);
			Teleport(Now, Spot);
			const int32 Before = P->Flyer->GetBiteCount();
			Poll(World, 0.01f, 15.0f, [World, P, Before]()
			{
				AJamZeroGPawn* Watcher = ProbePawn(World);
				if (!Watcher || !P->Flyer.IsValid())
				{
					return true;
				}
				Watcher->GetController()->SetControlRotation((P->Flyer->GetActorLocation() - Watcher->Camera->GetComponentLocation()).Rotation());
				return P->Flyer->GetBiteCount() > Before;
			}, [World](bool bBitten)
			{
				UE_LOG(LogMindKore, Log, TEXT("Flyer bite shot: %s"), bBitten ? TEXT("bitten") : TEXT("no bite in 15 s"));
				for (const float Delay : { 0.2f, 1.2f, 2.5f })
				{
					After(World, Delay, [World, Delay]()
					{
						const AJamZeroGPawn* Watcher = ProbePawn(World);
						const UJamVisorBloodComponent* Blood = Watcher ? Watcher->FindComponentByClass<UJamVisorBloodComponent>() : nullptr;
						UE_LOG(LogMindKore, Log, TEXT("Flyer bite shot: +%.1f s, %d blot(s) on the visor"), Delay, Blood ? Blood->GetBlotCount() : 0);
						World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
					});
				}
			});
		});
	}

	FAutoConsoleCommandWithWorld GFlyerBiteShot(
		TEXT("jam.Flyer.BiteShot"),
		TEXT("Rendered stills of a flyer bite: the visor blood at +0.2, +1.2, +2.5 s (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerBiteShot));

	/**
	 * jam.Flyer.CorpseShot: a still of a dead flyer on the planetoid (needs a real
	 * RHI and a shell habitat). The first shell habitat's first flyer is put 6 m
	 * over the surface under where it flies and killed by a round there; 7 s later
	 * the player stands 4 m from the corpse, looks at it and "Shot".
	 */
	void RunFlyerCorpseShot(UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = It->InnerRadiusCm > 0.0f ? *It : nullptr;
		}
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !ProbePawn(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer corpse shot: needs the zero-G pawn and a habitat round a planetoid"));
			return;
		}
		AJamFlyer* Flyer = Home->GetFlock()[0].Get();
		const FVector Centre = Home->GetActorLocation();
		const FVector Out = (Flyer->GetActorLocation() - Centre).GetSafeNormal();
		// The ground under it: a line down from the habitat's inner radius.
		FHitResult Ground;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamFlyerCorpseShot), false, Flyer);
		const FVector Top = Centre + Out * Home->InnerRadiusCm;
		const FVector Surface = World->LineTraceSingleByChannel(Ground, Top, Centre, ECC_Visibility, Params) ? FVector(Ground.ImpactPoint) : Top;
		Teleport(Flyer, Surface + Out * 600.0f);
		// A round from above: the flyer's own receiver (damage, push, death) as a gun delivers it.
		FMKResolvedImpact Kill;
		Kill.ShotId = FGuid::NewGuid();
		Kill.Kind = EMKImpactKind::Round;
		Kill.DamageAmount = 1000.0f;
		Kill.Hit = FHitResult(Flyer, Flyer->Body, Flyer->GetActorLocation(), Out);
		Kill.ImpulseNewtonSeconds = -Out * 30.0f;
		Kill.bHasPhysicsImpulse = true;
		Kill.SourceActor = ProbePawn(World);
		MKDeliverResolvedImpact(World, ProbePawn(World), Kill);
		UE_LOG(LogMindKore, Log, TEXT("Flyer corpse shot: %s killed 6 m over the surface, %.1f m from the centre"), *Flyer->GetName(),
			FVector::Dist(Flyer->GetActorLocation(), Centre) / 100.0f);
		TWeakObjectPtr<AJamFlyer> Weak = Flyer;
		After(World, 7.0f, [World, Weak, Centre]()
		{
			AJamZeroGPawn* Pawn = ProbePawn(World);
			const AJamFlyer* Corpse = Weak.Get();
			if (!Pawn || !Corpse)
			{
				return;
			}
			const FVector Up = (Corpse->GetActorLocation() - Centre).GetSafeNormal();
			const FVector Side = FVector::CrossProduct(Up, FVector::UpVector).GetSafeNormal(UE_SMALL_NUMBER, FVector::ForwardVector);
			Teleport(Pawn, Corpse->GetActorLocation() + Side * 400.0f + Up * 150.0f);
		});
		for (const float Delay : { 7.6f, 8.4f })
		{
			After(World, Delay, [World, Weak, Centre, Delay]()
			{
				AJamZeroGPawn* Pawn = ProbePawn(World);
				const AJamFlyer* Corpse = Weak.Get();
				if (!Pawn || !Corpse)
				{
					return;
				}
				Pawn->GetController()->SetControlRotation((Corpse->GetActorLocation() - Pawn->Camera->GetComponentLocation()).Rotation());
				if (Delay > 8.0f)
				{
					UE_LOG(LogMindKore, Log, TEXT("Flyer corpse shot: corpse %.1f m from the centre, %.0f cm/s; the player %.1f m from it"),
						FVector::Dist(Corpse->GetActorLocation(), Centre) / 100.0f, Corpse->GetVelocity().Size(),
						FVector::Dist(Corpse->GetActorLocation(), Pawn->GetActorLocation()) / 100.0f);
					World->GetFirstPlayerController()->ConsoleCommand(TEXT("Shot"));
				}
			});
		}
	}

	FAutoConsoleCommandWithWorld GFlyerCorpseShot(
		TEXT("jam.Flyer.CorpseShot"),
		TEXT("A rendered still of a flyer killed over the planetoid, lying where it fell (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunFlyerCorpseShot));

	/**
	 * jam.Flyer.Clip [Seconds] [Ffmpeg]: a recorded take of a fly-by attack with its sound (needs a
	 * real RHI and a window): the player 14 m ahead of the first habitat's first flyer, the view
	 * following it, the flock free; after 60 % of the take the pistol until it dies. The player's
	 * health is topped up (the take is about the flyers). Video: Saved/Clips/FlyerClip.mp4 (60 fps
	 * nominal), sound: Saved/Clips/FlyerClip.wav (the master submix); the log gives the real seconds
	 * and the frame count for the mux (Docs/Flyers.md).
	 */
	struct FClip
	{
		TUniquePtr<FJamClipRecorder> Recorder;
		TWeakObjectPtr<AJamFlyer> Subject;
		FTSTicker::FDelegateHandle Ticker;
		float Seconds = 26.0f;
		double Started = 0.0;
		bool bShooting = false;
		bool bStopped = false;
		double NextShot = 0.0;
		FRotator View = FRotator::ZeroRotator;
	};

	TSharedPtr<FClip> GClip;

	FString FindFfmpeg()
	{
		FString Path = FPlatformMisc::GetEnvironmentVariable(TEXT("PATH"));
		TArray<FString> Dirs;
		Path.ParseIntoArray(Dirs, TEXT(";"));
		for (const FString& Dir : Dirs)
		{
			const FString Candidate = FPaths::Combine(Dir, TEXT("ffmpeg.exe"));
			if (FPaths::FileExists(Candidate))
			{
				return Candidate;
			}
		}
		return TEXT("ffmpeg.exe");
	}

	bool ClipTick(UWorld* World, float DeltaSeconds)
	{
		TSharedPtr<FClip> Clip = GClip;
		if (!Clip.IsValid() || !Clip->Recorder)
		{
			return false;
		}
		Clip->Recorder->Tick();
		const double Now = World->GetTimeSeconds();
		AJamZeroGPawn* Pawn = ProbePawn(World);
		AJamFlyer* Flyer = Clip->Subject.Get();
		if (Pawn && Flyer && !Clip->bStopped)
		{
			// The view follows the flyer (eased, as a person turning to it).
			const FRotator Want = (Flyer->GetActorLocation() - Pawn->Camera->GetComponentLocation()).Rotation();
			Clip->View = FMath::RInterpTo(Clip->View, Want, DeltaSeconds, 5.0f);
			Pawn->GetController()->SetControlRotation(Clip->View);
			// The player brakes after every shove (the stabiliser, X), so the fight stays in the habitat.
			Pawn->ProbeThrust(FVector::ZeroVector, /*bStabilise=*/true);
			if (Pawn->Vitals->Health < 60.0f)
			{
				Pawn->Vitals->Restore(100.0f, 0.0f);
			}
			if (!Clip->bShooting && Now - Clip->Started > Clip->Seconds * 0.6f)
			{
				Clip->bShooting = true;
				UE_LOG(LogMindKore, Log, TEXT("Flyer clip: +%.1f s, the pistol"), Now - Clip->Started);
			}
			if (Clip->bShooting && Flyer->GetState() != EJamFlyerState::Dead && Now >= Clip->NextShot
				&& FVector::Dist(Flyer->GetActorLocation(), Pawn->GetActorLocation()) < 4000.0f)
			{
				Clip->NextShot = Now + 0.35;
				// Rounds fly (ProjectileSpeed): lead it by their travel time.
				const FJamWeapon* Gun = Pawn->Guns->GetWeapon();
				const float Travel = FVector::Dist(Flyer->GetActorLocation(), Pawn->Camera->GetComponentLocation()) / FMath::Max(Gun ? Gun->ProjectileSpeed : 9000.0f, 100.0f);
				const FVector Aim = Flyer->GetActorLocation() + (Flyer->GetVelocity() - Pawn->GetBodyVelocity()) * Travel;
				Clip->View = (Aim - Pawn->Camera->GetComponentLocation()).Rotation();
				Pawn->GetController()->SetControlRotation(Clip->View);
				Pawn->Fire();
			}
		}
		if (!Clip->bStopped && Now - Clip->Started >= Clip->Seconds)
		{
			Clip->bStopped = true;
			const FString Dir = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips"));
			Clip->Recorder->Stop(World, TEXT("FlyerClip"), Dir);
			UE_LOG(LogMindKore, Log, TEXT("Flyer clip: stopped after %.2f s real, %d frames so far; sound %s/FlyerClip.wav"),
				Clip->Recorder->GetSeconds(), Clip->Recorder->GetFrames(), *Dir);
		}
		if (Clip->bStopped && Clip->Recorder->IsFinished())
		{
			UE_LOG(LogMindKore, Log, TEXT("Flyer clip: done - %d frames over %.3f s real (%.2f fps); video Saved/Clips/FlyerClip.mp4 at a nominal 60 fps"),
				Clip->Recorder->GetFrames(), Clip->Recorder->GetSeconds(), Clip->Recorder->GetFrames() / FMath::Max(Clip->Recorder->GetSeconds(), 0.001));
			GClip.Reset();
			return false;
		}
		return true;
	}

	void RunFlyerClip(const TArray<FString>& Args, UWorld* World)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = *It;
		}
		AJamZeroGPawn* Pawn = ProbePawn(World);
		if (!Home || Home->GetFlock().IsEmpty() || !Home->GetFlock()[0].IsValid() || !Pawn || GClip.IsValid())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer clip: needs the zero-G pawn and a habitat with flyers (and no clip running)"));
			return;
		}
		// A silent master submix switches off and the recording skips it (MindKore, 12.10).
		if (IConsoleVariable* Never = IConsoleManager::Get().FindConsoleVariable(TEXT("au.NeverDisableSubmixes")))
		{
			Never->Set(1, ECVF_SetByCode);
		}
		JamFlyerParts::CVarLogFlaps->Set(true, ECVF_SetByCode);
		TSharedPtr<FClip> Clip = MakeShared<FClip>();
		Clip->Seconds = Args.Num() > 0 ? FCString::Atof(*Args[0]) : 26.0f;
		Clip->Subject = Home->GetFlock()[0];
		FVector Spot;
		InFront(Clip->Subject.Get(), Home, 1400.0f, Spot);
		Teleport(Pawn, Spot);
		Clip->View = (Clip->Subject->GetActorLocation() - Spot).Rotation();
		Clip->Recorder = MakeUnique<FJamClipRecorder>();
		FString Error;
		const FString Video = FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("Clips/FlyerClip.mp4"));
		if (!Clip->Recorder->Start(World, Args.Num() > 1 ? Args[1] : FindFfmpeg(), Video, 1280, Error))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Flyer clip: %s"), *Error);
			return;
		}
		Clip->Started = World->GetTimeSeconds();
		GClip = Clip;
		TWeakObjectPtr<UWorld> WeakWorld = World;
		Clip->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([WeakWorld](float DeltaSeconds)
		{
			return WeakWorld.IsValid() && ClipTick(WeakWorld.Get(), DeltaSeconds);
		}));
		// A second of it flying, then it notices (the take is about the attack, not the search).
		TWeakObjectPtr<AJamFlyer> Subject = Clip->Subject;
		After(World, 1.0f, [Subject]()
		{
			if (AJamFlyer* Flyer = Subject.Get())
			{
				Flyer->Aggro(TEXT("clip: the player in front"));
			}
		});
		UE_LOG(LogMindKore, Log, TEXT("Flyer clip: %.0f s take, the player 14 m ahead of %s"), Clip->Seconds, *Clip->Subject->GetName());
	}

	FAutoConsoleCommandWithWorldAndArgs GFlyerClip(
		TEXT("jam.Flyer.Clip"),
		TEXT("jam.Flyer.Clip [Seconds] [Ffmpeg]: a recorded take (video and sound) of a fly-by attack; Saved/Clips (needs a real RHI and a window)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunFlyerClip));

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
