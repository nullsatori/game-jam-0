#include "JamWeapons.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "JamLivingWall.h"
#include "JamMuzzleFx.h"
#include "JamScope.h"
#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MKImpactDelivery.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** jam.Weapons.PoseAt: the gun in view held this many seconds after a shot (stills through a cycle); < 0 off. */
	TAutoConsoleVariable<float> CVarPoseAt(TEXT("jam.Weapons.PoseAt"), -1.0f,
		TEXT("Hold the gun in view (kick and moving part) this many seconds after its last shot; < 0: live."));

	/**
	 * Cubic keys with broken tangents: the slopes are the curve's shape. A part that
	 * hits a stop arrives with speed (a steep arrive) and leaves the other way (the
	 * rebound) - the knock MindKore's hammer gets from its damped cosine
	 * (MKComputeFireReaction), here as keys the owner can drag in the editor.
	 */
	FRuntimeFloatCurve MakeCycleCurve(std::initializer_list<FJamCycleKey> Keys)
	{
		TArray<FRichCurveKey> RichKeys;
		for (const FJamCycleKey& Key : Keys)
		{
			FRichCurveKey& Rich = RichKeys.Emplace_GetRef(Key.Time, Key.Value, Key.Arrive, Key.Leave, RCIM_Cubic);
			Rich.TangentMode = RCTM_Break;
		}
		FRuntimeFloatCurve Curve;
		Curve.GetRichCurve()->SetKeys(RichKeys);
		return Curve;
	}

	float EvalCycle(const FRuntimeFloatCurve& Curve, float Seconds)
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->Eval(Seconds) : 0.0f;
	}
}

FRuntimeFloatCurve JamMakeCycleCurve(std::initializer_list<FJamCycleKey> Keys)
{
	return MakeCycleCurve(Keys);
}

float JamEvalCycle(const FRuntimeFloatCurve& Curve, float Seconds)
{
	return EvalCycle(Curve, Seconds);
}

float FJamWeapon::GetActionCycleSeconds() const
{
	float Seconds = 0.0f;
	for (const FRuntimeFloatCurve* Curve : { &ActionTravel, &ActionTurn, &ActionBody })
	{
		const FRichCurve* Rich = Curve->GetRichCurveConst();
		if (Rich && Rich->GetNumKeys() > 0)
		{
			Seconds = FMath::Max(Seconds, Rich->GetLastKey().Time);
		}
	}
	return Seconds;
}

UJamWeaponsComponent::UJamWeaponsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Guns as movement. Pistol: quick nudges (40 cm/s in zero-G); shotgun: a shove
	// and a spray that scatters junk; sniper: one slow shot that throws you back 7 m/s.
	// The rounds fly slow enough to be seen: the shotgun's pellets spread as a cloud.
	// MindKore's own meshes (CREDITS.md): its enemies' USP, the hero's pump gun;
	// the SV-98 built the same way (Scripts/blender/jam_guns.py).
	// Each in two parts, the frame and what moves (Scripts/blender/jam_usp_split.py, jam_guns.py).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Usp(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_USP_Frame.SM_Jam_USP_Frame"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UspSlide(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_USP_Slide.SM_Jam_USP_Slide"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PumpGun(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_Shotgun.SM_Jam_Shotgun"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pump(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_Shotgun_Pump.SM_Jam_Shotgun_Pump"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sv98(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_SV98.SM_Jam_SV98"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sv98Bolt(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_SV98_Bolt.SM_Jam_SV98_Bolt"));
	// Recorded CC0 donors heard through the suit (CREDITS.md, Docs/RecoilWeapons.md "Звук").
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolShot(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Shot_Pistol.SW_Jam_Shot_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunShot(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Shot_Shotgun.SW_Jam_Shot_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunPump(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Pump_Shotgun.SW_Jam_Pump_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperShot(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Shot_SV98.SW_Jam_Shot_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperBolt(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Bolt_SV98.SW_Jam_Bolt_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolEmpty(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Empty_Pistol.SW_Jam_Empty_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunEmpty(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Empty_Shotgun.SW_Jam_Empty_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperEmpty(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Empty_SV98.SW_Jam_Empty_SV98"));
	// The mechanism hooks of JamWeaponParts.cpp (hammer, magazines, shells), same donors' treatment.
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolHammer(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Hammer_Pistol.SW_Jam_Hammer_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolMagOut(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_MagOut_Pistol.SW_Jam_MagOut_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolMagIn(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_MagIn_Pistol.SW_Jam_MagIn_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperMagOut(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_MagOut_SV98.SW_Jam_MagOut_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperMagIn(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_MagIn_SV98.SW_Jam_MagIn_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunShell(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Shell_Shotgun.SW_Jam_Shell_Shotgun"));
	FJamWeapon Pistol;
	Pistol.Name = NSLOCTEXT("JamZeroG", "Pistol", "Пистолет");
	Pistol.Damage = 25.0f;
	Pistol.FireInterval = 0.15f;
	Pistol.MagazineSize = 12;
	Pistol.Reserve = 60;
	Pistol.ReloadSeconds = 0.97f; // the seat at 0.86 s, out by 0.95 (JamWeaponParts.cpp; owner 19.10: the tail halved)
	Pistol.SpreadDeg = 0.5f;
	Pistol.RecoilImpulse = 4000.0f;
	Pistol.HitImpulse = 3000.0f;
	Pistol.ShotSound = PistolShot.Object;
	Pistol.EmptySound = PistolEmpty.Object;
	Pistol.HammerSound = PistolHammer.Object;
	Pistol.MagOutSound = PistolMagOut.Object;
	Pistol.MagInSound = PistolMagIn.Object;
	Pistol.bAutomatic = true;
	Pistol.ProjectileSpeed = 9000.0f;
	Pistol.KickPitchDeg = 1.2f;
	Pistol.KickYawDeg = 0.3f;
	Pistol.ViewKickBackCm = 3.0f;
	Pistol.ViewKickRiseDeg = 10.0f;
	// The USP's muzzle is its -X, the grip at the origin: turned to face the view.
	Pistol.ViewMesh = Usp.Object;
	Pistol.ViewOffset = FTransform(FRotator(0.0f, 180.0f, 0.0f), FVector(30.0f, 9.0f, -10.0f));
	// The slide: blown back 4 cm in 35 ms (fast out, slowing on the spring), a beat on
	// the stop, driven home faster and faster, a 7% rebound off the frame, settled at
	// 0.145 s - inside the 0.15 s of automatic fire.
	Pistol.ActionMesh = UspSlide.Object;
	Pistol.ActionTravelCm = 4.0f;
	Pistol.ActionTravel = MakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 70.0f }, { 0.035f, 1.0f, 8.0f, 0.0f }, { 0.05f, 0.96f, -3.0f, -3.0f },
		{ 0.1f, 0.0f, -38.0f, 6.0f }, { 0.115f, 0.07f, 0.0f, 0.0f }, { 0.145f, 0.0f, 0.0f, 0.0f } });
	FJamWeapon Shotgun;
	Shotgun.Name = NSLOCTEXT("JamZeroG", "Shotgun", "Дробовик");
	Shotgun.Damage = 12.0f;
	// The pump stroke sets the pace (its cycle ends at 0.56 s).
	Shotgun.FireInterval = 0.5f;
	Shotgun.MagazineSize = 6;
	Shotgun.Reserve = 24;
	Shotgun.ReloadSeconds = 0.42f; // per round: it loads round by round (JamWeaponParts.cpp)
	Shotgun.Pellets = 8;
	Shotgun.SpreadDeg = 6.0f;
	Shotgun.RecoilImpulse = 25000.0f;
	Shotgun.HitImpulse = 4000.0f;
	Shotgun.RangeCm = 3000.0f;
	Shotgun.ShotSound = ShotgunShot.Object;
	Shotgun.CycleSound = ShotgunPump.Object;
	Shotgun.CycleDelaySeconds = 0.15f; // the pump's clicks at 0.26 and 0.40 s after the shot, as its animation
	Shotgun.EmptySound = ShotgunEmpty.Object;
	Shotgun.ShellInsertSound = ShotgunShell.Object;
	Shotgun.ProjectileSpeed = 6000.0f;
	Shotgun.KickPitchDeg = 5.0f;
	Shotgun.KickYawDeg = 1.2f;
	Shotgun.ViewKickBackCm = 7.0f;
	Shotgun.ViewKickRiseDeg = 14.0f;
	// Butt -> muzzle along +X, the origin on the trigger.
	Shotgun.ViewMesh = PumpGun.Object;
	Shotgun.ViewOffset = FTransform(FRotator::ZeroRotator, FVector(26.0f, 11.0f, -12.0f));
	// The fore-end, racked by itself 0.15 s after the shot (owner: «более резво
	// передергивать»): yanked back 6 cm into the stop in 0.11 s, a 6% bounce off
	// it, slammed home 0.14 s after the back click with a double knock (6%, 1.5%),
	// settled at 0.47 s; the hand drives it, so both strokes speed up into their stops. The
	// stroke runs along the line of sight, so the gun comes in to meet it - canted
	// to show its underside, swung across the view and pulled 8 cm closer: ActionBody
	// is where its pose springs pull (JamWeaponParts.cpp), in at 0.12 s, out at 0.36 s.
	Shotgun.ActionMesh = Pump.Object;
	// 6 cm (owner, round 2: «уменьши ход»): the fore-end, 3 cm further forward now, stops at the receiver's front.
	Shotgun.ActionTravelCm = -6.0f;
	Shotgun.ActionTravel = MakeCycleCurve({ { 0.15f, 0.0f, 0.0f, 2.0f }, { 0.26f, 1.0f, 24.0f, -5.0f }, { 0.28f, 0.92f, 0.0f, 0.0f },
		{ 0.4f, 0.0f, -26.0f, 4.0f }, { 0.415f, 0.06f, 0.0f, 0.0f }, { 0.43f, 0.0f, -2.0f, 1.5f },
		{ 0.44f, 0.015f, 0.0f, 0.0f }, { 0.47f, 0.0f, 0.0f, 0.0f } });
	Shotgun.ActionBodyTurn = FRotator(6.0f, -15.0f, 22.0f);
	Shotgun.ActionBodyOffsetCm = FVector(-8.0f, -3.0f, 2.0f);
	Shotgun.ActionBody = MakeCycleCurve({ { 0.12f, 0.0f, 0.0f, 0.0f }, { 0.135f, 1.0f, 0.0f, 0.0f }, { 0.36f, 1.0f, 0.0f, 0.0f },
		{ 0.375f, 0.0f, 0.0f, 0.0f }, { 0.56f, 0.0f, 0.0f, 0.0f } });
	FJamWeapon Sniper;
	Sniper.Name = NSLOCTEXT("JamZeroG", "Sniper", "Снапа");
	Sniper.Damage = 150.0f;
	// The bolt sets the pace (its cycle ends at 1.25 s).
	Sniper.FireInterval = 0.5f;
	Sniper.MagazineSize = 4;
	Sniper.Reserve = 16;
	Sniper.ReloadSeconds = 1.7f; // the magazine out and in (JamWeaponParts.cpp)
	Sniper.RecoilImpulse = 70000.0f;
	Sniper.HitImpulse = 60000.0f;
	Sniper.RangeCm = 15000.0f;
	Sniper.ShotSound = SniperShot.Object;
	Sniper.CycleSound = SniperBolt.Object;
	Sniper.CycleDelaySeconds = 0.25f; // the bolt's clicks at 0.35, 0.63, 0.87, 1.09 s after the shot
	Sniper.EmptySound = SniperEmpty.Object;
	Sniper.MagOutSound = SniperMagOut.Object;
	Sniper.MagInSound = SniperMagIn.Object;
	Sniper.ProjectileSpeed = 40000.0f;
	Sniper.KickPitchDeg = 8.0f;
	Sniper.KickYawDeg = 1.0f;
	Sniper.ViewKickBackCm = 9.0f;
	Sniper.ViewKickRiseDeg = 18.0f;
	// Low and to the right: its scope sits 11.5 cm over the trigger.
	Sniper.ViewMesh = Sv98.Object;
	Sniper.ViewOffset = FTransform(FRotator::ZeroRotator, FVector(22.0f, 13.0f, -16.0f));
	// The bolt at the rear of the receiver, turning about the bore at its handle
	// (1.5 cm behind the trigger, 4.5 cm up), on the sound's clicks (up +0.35 s,
	// back +0.63 s, home +0.87 s, down +1.09 s), one arc: lifted 60 deg into its stop
	// (5% drop back), drawing the bolt 4% back as it turns; pulled 9 cm back - starting
	// while the lift still rocks, faster and faster - into the stop with an 8% bounce;
	// a beat; slammed home with a double knock (4%, 1.2%); the lock-down starts early,
	// cams the bolt 1.5% forward and bottoms with a double bounce (6%, 1.5%).
	// The rifle cants to show its right side meanwhile: ActionBody, in at 0.2 s,
	// out at 1.05 s, is where its pose springs pull.
	Sniper.ActionMesh = Sv98Bolt.Object;
	Sniper.ActionPivot = FVector(-1.5f, 0.0f, 4.5f);
	Sniper.ActionTravelCm = -9.0f;
	Sniper.ActionTurnDeg = 60.0f;
	Sniper.ActionTurn = MakeCycleCurve({ { 0.25f, 0.0f, 0.0f, 1.0f }, { 0.35f, 1.0f, 14.0f, -3.0f }, { 0.38f, 0.95f, 0.0f, 0.0f },
		{ 0.42f, 1.0f, 0.0f, 0.0f }, { 0.95f, 1.0f, 0.0f, 0.0f }, { 1.09f, 0.0f, -16.0f, 3.0f },
		{ 1.105f, 0.06f, 0.0f, 0.0f }, { 1.12f, 0.0f, -2.0f, 1.0f }, { 1.13f, 0.015f, 0.0f, 0.0f },
		{ 1.17f, 0.0f, 0.0f, 0.0f } });
	Sniper.ActionTravel = MakeCycleCurve({ { 0.25f, 0.0f, 0.0f, 0.0f }, { 0.35f, 0.04f, 0.6f, 0.0f }, { 0.38f, 0.04f, 0.0f, 0.0f },
		{ 0.63f, 1.0f, 13.0f, -4.0f }, { 0.66f, 0.92f, 0.0f, 0.0f }, { 0.72f, 0.93f, 0.0f, 0.0f },
		{ 0.87f, 0.0f, -16.0f, 3.0f }, { 0.89f, 0.04f, 0.0f, 0.0f }, { 0.905f, 0.0f, -1.0f, 1.0f },
		{ 0.915f, 0.012f, 0.0f, 0.0f }, { 0.95f, 0.0f, 0.0f, 0.0f }, { 1.02f, -0.015f, 0.0f, 0.0f },
		{ 1.09f, 0.0f, 0.0f, 0.0f } });
	Sniper.ActionBodyTurn = FRotator(3.0f, -8.0f, -14.0f);
	Sniper.ActionBody = MakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.215f, 1.0f, 0.0f, 0.0f }, { 1.05f, 1.0f, 0.0f, 0.0f },
		{ 1.065f, 0.0f, 0.0f, 0.0f }, { 1.25f, 0.0f, 0.0f, 0.0f } });
	Weapons = { Pistol, Shotgun, Sniper };
	SetupWeaponParts();

	// MindKore's own content.
	static ConstructorHelpers::FObjectFinder<UMKImpactDataAsset> DefaultImpactData(
		TEXT("/Game/MindKore/Impacts/DA_Impact_Default.DA_Impact_Default"));
	ImpactData = DefaultImpactData.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Decal(
		TEXT("/Game/MindKore/Impacts/Decals/M_Decal_Default.M_Decal_Default"));
	DefaultImpactDecal = Decal.Object;
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Streak(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	TracerMesh = Streak.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Tracer(TEXT("/Game/MindKore/Weapons/M_MK_Tracer.M_MK_Tracer"));
	TracerMaterial = Tracer.Object;
}

void UJamWeaponsComponent::BeginPlay()
{
	Super::BeginPlay();
	for (FJamWeapon& Weapon : Weapons)
	{
		Weapon.Ammo = Weapon.MagazineSize;
		Weapon.bUnlocked = bStartWithAllWeapons;
	}
	// The gun in view hangs off the owner's camera (the zero-G pawn's and the test room's).
	if (UCameraComponent* Camera = GetOwner()->FindComponentByClass<UCameraComponent>())
	{
		ViewModel = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("GunInView"));
		ViewModel->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ViewModel->SetCastShadow(false);
		ViewModel->SetOnlyOwnerSee(true);
		ViewModel->SetupAttachment(Camera);
		ViewModel->RegisterComponent();
		ActionInView = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("GunActionInView"));
		ActionInView->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		ActionInView->SetCastShadow(false);
		ActionInView->SetOnlyOwnerSee(true);
		ActionInView->SetupAttachment(ViewModel);
		ActionInView->RegisterComponent();
		MakePartComponents(*ViewModel);
		// Muzzle flash and powder smoke, in the same camera space as the gun (JamMuzzleFx.h).
		NewObject<UJamMuzzleFxComponent>(GetOwner(), TEXT("MuzzleFx"))->RegisterComponent();
	}
	// Every gun's aim and the SV-98's scope; it builds itself once the gun in view exists (UJamScopeComponent::Build).
	Scope = NewObject<UJamScopeComponent>(GetOwner(), TEXT("Scope"));
	Scope->RegisterComponent();
}

bool UJamWeaponsComponent::IsInHand() const
{
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(GetOwner());
	return !Pawn || Pawn->GetHands() == EJamHands::Gun;
}

const FJamWeapon* UJamWeaponsComponent::GetWeapon() const
{
	return IsInHand() && IsWeaponUnlocked(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
}

void UJamWeaponsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	UpdatePartEvents(DeltaTime);
	// An R pressed during the shot's cycle, now through.
	if (bReloadQueued && Weapons.IsValidIndex(CurrentWeapon) && Weapons[CurrentWeapon].SinceShot >= Weapons[CurrentWeapon].FireReadySince)
	{
		bReloadQueued = false;
		Reload();
	}
	// The rounds were counted on the way (UpdatePartEvents: the magazine's seat, each round's); the end only frees the gun.
	if (ReloadEndSeconds > 0.0 && GetWorld()->GetTimeSeconds() >= ReloadEndSeconds && Weapons.IsValidIndex(CurrentWeapon))
	{
		const FJamWeapon& Weapon = Weapons[CurrentWeapon];
		ReloadEndSeconds = 0.0;
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s reloaded, %d/%d"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString(), Weapon.Ammo, Weapon.Reserve);
	}
	UpdateRounds(DeltaTime);
	UpdateRecoil(DeltaTime);
	UpdateViewModel(DeltaTime);
}

void UJamWeaponsComponent::UpdateRounds(float DeltaTime)
{
	for (int32 Index = Rounds.Num() - 1; Index >= 0; --Index)
	{
		FRound& Round = Rounds[Index];
		const FJamWeapon& Weapon = Weapons[Round.Weapon];
		const float Step = FMath::Min(Weapon.ProjectileSpeed * DeltaTime, Weapon.RangeCm - Round.TravelledCm);
		const FVector Next = Round.Position + Round.Direction * Step;
		if (Step <= 0.0f || TraceShot(Weapon, Round.Position, Next, Round.Direction) || Round.TravelledCm + Step >= Weapon.RangeCm)
		{
			if (UStaticMeshComponent* Tracer = Round.Tracer.Get())
			{
				Tracer->DestroyComponent();
			}
			Rounds.RemoveAtSwap(Index);
			continue;
		}
		Round.Position = Next;
		Round.TravelledCm += Step;
		if (UStaticMeshComponent* Tracer = Round.Tracer.Get())
		{
			// The streak's head on the round (joining it from the muzzle), its tail behind it.
			const FVector Head = Round.Position + Round.MuzzleOffset * FMath::Max(0.0f, 1.0f - Round.TravelledCm / TracerConvergeCm);
			const float Length = FMath::Min(TracerLengthCm, Round.TravelledCm);
			Tracer->SetWorldLocationAndRotation(Head - Round.Direction * Length * 0.5f, Round.Direction.Rotation());
			Tracer->SetWorldScale3D(FVector(Length, TracerThicknessCm, TracerThicknessCm) / 100.0f);
			Tracer->SetVisibility(true);
		}
	}
}

void UJamWeaponsComponent::TurnView(float PitchDeg, float YawDeg) const
{
	const APawn* Pawn = Cast<APawn>(GetOwner());
	AController* Controller = Pawn ? Pawn->GetController() : nullptr;
	if (!Controller || (PitchDeg == 0.0f && YawDeg == 0.0f))
	{
		return;
	}
	// In degrees, as MindKore's AddRecoilRotationDegrees (AddControllerPitchInput
	// would scale them by the input settings). The ship's controller takes it as
	// its own turn in its next UpdateRotation: a SetControlRotation behind its back
	// read as a foreign write in gravity and rebuilt the view from the shown one,
	// the roll left from zero-G included.
	if (AJamShipController* Ship = Cast<AJamShipController>(Controller))
	{
		Ship->AddViewKick(FRotator(PitchDeg, YawDeg, 0.0f));
		return;
	}
	// Another controller: at once, in the view's own frame (a rolled view kicks toward its own up).
	const FQuat Kick = FRotator(PitchDeg, YawDeg, 0.0f).Quaternion();
	Controller->SetControlRotation((Controller->GetControlRotation().Quaternion() * Kick).Rotator());
}

void UJamWeaponsComponent::UpdateRecoil(float DeltaTime)
{
	// MindKore's camera recoil, smaller: the kick arrives over RecoilAttackSeconds,
	// then its settling share comes back linearly; the rest stays as climb.
	if (KickAttackLeft > 0.0f)
	{
		const float Fraction = FMath::Min(DeltaTime / KickAttackLeft, 1.0f);
		TurnView(KickPitchLeft * Fraction, KickYawLeft * Fraction);
		KickPitchLeft -= KickPitchLeft * Fraction;
		KickYawLeft -= KickYawLeft * Fraction;
		KickAttackLeft = FMath::Max(0.0f, KickAttackLeft - DeltaTime);
		return;
	}
	const float Step = RecoilRecoverDegPerSec * DeltaTime;
	const float Pitch = FMath::Min(SettlePitch, Step);
	const float Yaw = FMath::Clamp(SettleYaw, -Step, Step);
	SettlePitch -= Pitch;
	SettleYaw -= Yaw;
	TurnView(-Pitch, -Yaw);
}

void UJamWeaponsComponent::UpdateViewModel(float DeltaTime)
{
	if (!ViewModel)
	{
		return;
	}
	// The hands on something else (a hull tool) put the gun away; a switch puts one away and
	// draws the next (JamWeaponParts.cpp). The gun shown is the one on its way, not yet the current one.
	const FJamWeapon* Weapon = UpdateSwitch(DeltaTime, !IsInHand(), PoseSwitch);
	// The cycle's clock runs for the gun in hand and wanted (drawn or being drawn); a gun put away keeps its cycle
	// where it was, its sound stopped, and picks it up again when it is back (owner 19.10: «механика не врёт»).
	const int32 Running = SwitchStage == ESwitch::Drawn || SwitchStage == ESwitch::Draw ? ShownWeapon : INDEX_NONE;
	if (Running != CycleWeapon)
	{
		// Only a cycle still running freezes: a finished one's reverb tail plays out and is not picked up again.
		if (Weapons.IsValidIndex(CycleWeapon) && Weapons[CycleWeapon].SinceShot < Weapons[CycleWeapon].GetActionCycleSeconds())
		{
			if (UAudioComponent* Sound = Weapons[CycleWeapon].MechanismSound.Get())
			{
				Sound->Stop();
			}
		}
		if (Weapons.IsValidIndex(Running) && Weapons[Running].SinceShot < Weapons[Running].GetActionCycleSeconds())
		{
			FJamWeapon& Gun = Weapons[Running];
			for (const TPair<USoundBase*, float> Each : { TPair<USoundBase*, float>(Gun.HammerSound, Gun.HammerSoundSeconds),
				TPair<USoundBase*, float>(Gun.CycleSound, Gun.CycleDelaySeconds) })
			{
				if (Each.Key && Each.Value <= Gun.SinceShot && Gun.SinceShot < Each.Value + Each.Key->GetDuration())
				{
					PlayMechanismSound(Gun, Each.Key, Gun.SinceShot - Each.Value);
				}
			}
		}
		CycleWeapon = Running;
	}
	if (Weapons.IsValidIndex(Running))
	{
		Weapons[Running].SinceShot += DeltaTime;
	}
	if (!Weapon)
	{
		UpdateParts(nullptr, 0.0f);
		return;
	}
	SinceKickSeconds += DeltaTime;
	const float PoseAt = CVarPoseAt.GetValueOnGameThread();
	const float Since = PoseAt >= 0.0f ? PoseAt : Weapon->SinceShot;
	// Back along the view, the muzzle up and a little roll, about the grip (the
	// meshes' origin); the rise turns about the camera's right axis whichever
	// way the mesh itself faces. Springs back at ViewKickReturnRate.
	// Aimed (JamScope.h), the gun rests at the eye and kicks less.
	FTransform Offset = Weapon->ViewOffset;
	float KickScale = 1.0f;
	if (Scope)
	{
		Scope->AimPose(ShownWeapon, Offset, KickScale);
	}
	const float Kick = KickScale * FMath::Exp(-ViewKickReturnRate * (PoseAt >= 0.0f ? PoseAt : SinceKickSeconds));
	const FQuat Rise = FRotator(Weapon->ViewKickRiseDeg * Kick, 0.0f, -0.3f * Weapon->ViewKickRiseDeg * Kick).Quaternion();
	// The whole gun's pose in the cycle and the reload: a mass on springs (JamWeaponParts.cpp).
	UpdatePose(*Weapon, DeltaTime);
	Offset.SetRotation(Rise * FRotator(Pose.Turn.X, Pose.Turn.Y, Pose.Turn.Z).Quaternion() * Offset.GetRotation());
	Offset.AddToTranslation(Pose.Place);
	Offset.AddToTranslation(FVector(-Weapon->ViewKickBackCm * Kick, 0.0f, 0.0f));
	ViewModel->SetRelativeTransform(Offset);
	// The moving part runs its cycle along/about the gun's own X.
	ActionInView->SetRelativeLocationAndRotation(
		Weapon->ActionPivot + FVector(Weapon->ActionTravelCm * EvalCycle(Weapon->ActionTravel, Since), 0.0f, 0.0f),
		FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Weapon->ActionTurnDeg * EvalCycle(Weapon->ActionTurn, Since))));
	UpdateParts(Weapon, Since);
	UpdateCycleEvents();
}

FTransform UJamWeaponsComponent::GetMuzzleTransform() const
{
	const FJamWeapon* Weapon = GetWeapon();
	if (!ViewModel || !ViewModel->GetStaticMesh() || !Weapon)
	{
		const APawn* Pawn = Cast<APawn>(GetOwner());
		return FTransform(Pawn ? Pawn->GetViewRotation() : GetOwner()->GetActorRotation(), GetOwner()->GetActorLocation());
	}
	// The bore's line is the view's forward carried through the kick: the gun's
	// world rotation without its rest turn in ViewOffset (the USP sits turned 180).
	const FQuat Bore = ViewModel->GetComponentQuat() * Weapon->ViewOffset.GetRotation().Inverse();
	const FVector Location = ViewModel->DoesSocketExist(TEXT("Muzzle"))
		? ViewModel->GetSocketLocation(TEXT("Muzzle"))
		// No socket (a mesh not from Scripts/jam_weapons.py): its front, on its origin's line.
		: ViewModel->GetComponentTransform().TransformPosition(FVector(ViewModel->GetStaticMesh()->GetBoundingBox().Max.X, 0.0f, 0.0f));
	return FTransform(Bore, Location);
}

void UJamWeaponsComponent::SelectStep(int32 Step)
{
	// The next unlocked gun in the wheel's direction.
	const int32 Count = Weapons.Num();
	for (int32 Tries = 1; Tries <= Count; ++Tries)
	{
		const int32 Index = ((CurrentWeapon + Step * Tries) % Count + Count) % Count;
		if (Weapons[Index].bUnlocked)
		{
			SelectWeapon(Index);
			return;
		}
	}
}

void UJamWeaponsComponent::UnlockWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index))
	{
		return;
	}
	Weapons[Index].bUnlocked = true;
	CurrentWeapon = INDEX_NONE; // so SelectWeapon takes it even when it was the current slot
	SelectWeapon(Index);
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s unlocked"), *GetNameSafe(GetOwner()), *Weapons[Index].Name.ToString());
}

void UJamWeaponsComponent::Holster(bool bAtOnce)
{
	bReloadQueued = false;
	ReloadEndSeconds = 0.0; // as a switch: the reload is abandoned, the gun's cycle freezes (UpdateViewModel)
	if (bAtOnce)
	{
		ClearView(); // gone this frame, no put-away (JamWeaponParts.cpp)
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s"), *GetNameSafe(GetOwner()), bAtOnce ? TEXT("unarmed") : TEXT("put away"));
}

void UJamWeaponsComponent::Rearm()
{
	if (!IsWeaponUnlocked(CurrentWeapon))
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: rearmed - %s"), *GetNameSafe(GetOwner()), *Weapons[CurrentWeapon].Name.ToString());
	Draw();
}

void UJamWeaponsComponent::Draw()
{
	// Only the log line: with the gun wanted again, UpdateSwitch (JamWeaponParts.cpp) draws it in from away
	// along its own path - the pistol from the right-hip holster, the shotgun over the right shoulder, the
	// SV-98 over the left - on the pose springs; it fires once drawn and settled (IsDrawing).
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: draw %s"), *GetNameSafe(GetOwner()), Weapons.IsValidIndex(CurrentWeapon) ? *Weapons[CurrentWeapon].Name.ToString() : TEXT("-"));
}

bool UJamWeaponsComponent::IsDrawing() const
{
	return GetWeapon() && !IsWeaponReady();
}

void UJamWeaponsComponent::SelectWeapon(int32 Index)
{
	// Any switch is UpdateSwitch's: it puts the shown gun away and draws the current one. The hands are the
	// owner's: out of them (a tool, a body held) this only picks the gun they take next (AJamZeroGPawn::PressSlot).
	if (!Weapons.IsValidIndex(Index) || Index == CurrentWeapon || !Weapons[Index].bUnlocked)
	{
		return;
	}
	CurrentWeapon = Index;
	bReloadQueued = false;
	ReloadEndSeconds = 0.0; // A switch abandons a reload; the last gun's cycle freezes until it is back (UpdateViewModel).
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s (%d/%d)"), *GetNameSafe(GetOwner()), *Weapons[Index].Name.ToString(),
		Weapons[Index].Ammo, Weapons[Index].Reserve);
}

float UJamWeaponsComponent::GetReloadRemaining() const
{
	return FMath::Max(0.0f, float(ReloadEndSeconds - GetWorld()->GetTimeSeconds()));
}

FString UJamWeaponsComponent::Describe() const
{
	// A gun put away for a hull tool keeps its line (its rounds still count); holding a body is unarmed.
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(GetOwner());
	const FJamWeapon* Weapon = IsWeaponUnlocked(CurrentWeapon) && !(Pawn && Pawn->GetHands() == EJamHands::Held) ? &Weapons[CurrentWeapon] : nullptr;
	if (!Weapon)
	{
		return TEXT("без оружия");
	}
	FString Line = FString::Printf(TEXT("[%d] %s %d/%d"), CurrentWeapon + 1, *Weapon->Name.ToString(), Weapon->Ammo, Weapon->Reserve);
	if (const float Reload = GetReloadRemaining(); Reload > 0.0f)
	{
		Line += FString::Printf(TEXT(" (перезарядка %.1f)"), Reload);
	}
	return Line;
}

void UJamWeaponsComponent::Reload()
{
	FJamWeapon* Weapon = GetWeapon() ? &Weapons[CurrentWeapon] : nullptr;
	if (!Weapon || !IsWeaponReady() || Weapon->Ammo >= Weapon->MagazineSize || Weapon->Reserve <= 0 || GetReloadRemaining() > 0.0f)
	{
		return;
	}
	// Not over the shot's cycle (review 19.10: R right after a shot ran the bolt and the magazine at once):
	// it waits for the cycle's end (TickComponent).
	if (Weapon->SinceShot < Weapon->FireReadySince)
	{
		bReloadQueued = true;
		return;
	}
	const float Seconds = StartReloadTimeline(*Weapon);
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: reloading %s, %.2f s"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString(), Seconds);
}

bool UJamWeaponsComponent::Fire(const FVector& Start, const FVector& Direction, FVector& OutRecoil)
{
	OutRecoil = FVector::ZeroVector;
	FJamWeapon* Weapon = GetWeapon() ? &Weapons[CurrentWeapon] : nullptr;
	// Round-by-round loading breaks off for a shot - once the shot is sure (review 19.10: it broke off before the
	// checks below, and a refused shot lost both).
	const bool bBreakLoading = Weapon && Weapon->ShellMesh && Weapon->Ammo > 0 && GetReloadRemaining() > 0.0f;
	// Not before the gun is drawn and has settled (IsDrawing), nor before its own cycle is through.
	if (!Weapon || Weapon->SinceShot < Weapon->FireReadySince || (GetReloadRemaining() > 0.0f && !bBreakLoading) || IsDrawing())
	{
		return false;
	}
	if (bBreakLoading)
	{
		ReloadEndSeconds = 0.0;
	}
	if (Weapon->Ammo <= 0)
	{
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s is empty"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString());
		// The dry click, once per interval while the trigger is held (no reserve: no reload to stop it); the
		// reload starts first, the cycle being through, so it does not wait out the click's interval.
		UGameplayStatics::PlaySound2D(this, Weapon->EmptySound);
		Reload();
		Weapon->FireReadySince = Weapon->SinceShot + Weapon->FireInterval;
		return false;
	}
	--Weapon->Ammo;
	bReloadQueued = false; // the trigger won over a waiting R
	// Not before the slide, the pump or the bolt is home again.
	Weapon->FireReadySince = FMath::Max(Weapon->FireInterval, Weapon->GetActionCycleSeconds());
	// One kick per shot, whatever the pellets do. Guns cost no oxygen.
	OutRecoil = -Direction * Weapon->RecoilImpulse;
	const float Yaw = Weapon->KickYawDeg * (FMath::RandBool() ? 1.0f : -1.0f);
	KickPitchLeft += Weapon->KickPitchDeg;
	KickYawLeft += Yaw;
	KickAttackLeft = RecoilAttackSeconds;
	SettlePitch += Weapon->KickPitchDeg * RecoilSettleFraction;
	SettleYaw += Yaw * RecoilSettleFraction;
	SinceKickSeconds = 0.0f;
	Weapon->SinceShot = 0.0f;
	Weapon->PrevSinceShot = -KINDA_SMALL_NUMBER; // a new shot: its case, hammer and cycle sound (UpdatePartEvents)
	// A slight pitch spread so a held trigger does not repeat one sample.
	UGameplayStatics::PlaySound2D(this, Weapon->ShotSound, 1.0f, FMath::FRandRange(0.97f, 1.03f));
	const FVector Muzzle = GetMuzzleTransform().GetLocation();
	if (UJamMuzzleFxComponent* Fx = GetOwner()->FindComponentByClass<UJamMuzzleFxComponent>())
	{
		Fx->Shot(CurrentWeapon, Muzzle, ViewModel);
	}
	for (int32 Pellet = 0; Pellet < Weapon->Pellets; ++Pellet)
	{
		const FVector PelletDirection = Weapon->SpreadDeg > 0.0f
			? FMath::VRandCone(Direction, FMath::DegreesToRadians(Weapon->SpreadDeg))
			: Direction;
		if (Weapon->ProjectileSpeed <= 0.0f)
		{
			TraceShot(*Weapon, Start, Start + PelletDirection * Weapon->RangeCm, PelletDirection);
			continue;
		}
		FRound& Round = Rounds.AddDefaulted_GetRef();
		Round.Weapon = CurrentWeapon;
		Round.Position = Start;
		Round.Direction = PelletDirection;
		Round.MuzzleOffset = Muzzle - Start;
		if (TracerMesh)
		{
			// Hidden until its first step: a zero-length streak at the eye would fill the view.
			UStaticMeshComponent* Tracer = NewObject<UStaticMeshComponent>(GetOwner());
			Tracer->SetUsingAbsoluteLocation(true);
			Tracer->SetUsingAbsoluteRotation(true);
			Tracer->SetUsingAbsoluteScale(true);
			Tracer->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Tracer->SetCastShadow(false);
			Tracer->SetVisibility(false);
			Tracer->SetStaticMesh(TracerMesh);
			Tracer->SetMaterial(0, TracerMaterial);
			Tracer->RegisterComponent();
			Round.Tracer = Tracer;
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s fired, %d pellet(s), recoil %.0f kg cm/s, %d/%d left, %s"), *GetNameSafe(GetOwner()),
		*Weapon->Name.ToString(), Weapon->Pellets, Weapon->RecoilImpulse, Weapon->Ammo, Weapon->Reserve, *GetNameSafe(Weapon->ShotSound));
	return true;
}

bool UJamWeaponsComponent::TraceShot(const FJamWeapon& Weapon, const FVector& Start, const FVector& End, const FVector& Direction)
{
	UWorld* World = GetWorld();
	AActor* Shooter = GetOwner();
	// As UMKFirearmComponent traces: the Shot channel (ECC_GameTraceChannel2),
	// complex collision (the glass pane's shard triangles), physical material;
	// multi-trace and MKSelectResolvedHit, so hit-zone boxes (overlaps in front
	// of the blocking hit) win on zone priority within the same actor.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamShot), /*bTraceComplex=*/true, Shooter);
	Params.bReturnPhysicalMaterial = true;
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, End, ECC_GameTraceChannel2, Params);
	TArray<FMKHitCandidate, TInlineAllocator<8>> Candidates;
	int32 NearestZone = INDEX_NONE;
	for (const FHitResult& Each : Hits)
	{
		FMKHitCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Distance = Each.Distance;
		Candidate.bBlocking = Each.bBlockingHit;
		Candidate.ActorKey = Each.GetActor();
		Candidate.HitZone = MKResolveHitZoneFromComponent(Each.GetComponent());
		if (!Candidate.bBlocking && Candidate.HitZone != EMKHitZone::Default
			&& (NearestZone == INDEX_NONE || Candidate.Distance < Candidates[NearestZone].Distance))
		{
			NearestZone = Candidates.Num() - 1;
		}
	}
	// Not in MindKore: a body made only of hit-zone boxes (AMKImpactDummy) has
	// nothing that blocks Shot, so its nearest zone stands in for the blocking hit.
	if (NearestZone != INDEX_NONE)
	{
		Candidates[NearestZone].bBlocking = true;
	}
	const int32 Selected = MKSelectResolvedHit(Candidates, /*SameActorToleranceCm=*/2.5f);
	if (Selected == INDEX_NONE)
	{
		return false;
	}
	const FHitResult& Hit = Hits[Selected];
	FMKImpactResolveParams ResolveParams;
	ResolveParams.ImpactData = ImpactData;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, ResolveParams, Feedback);
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Round;
	Resolved.SourceActor = Shooter;
	Resolved.ImpactTimeSeconds = World->GetTimeSeconds();
	Resolved.RangeCm = FVector::Dist(Shooter->GetActorLocation(), Hit.ImpactPoint);
	Resolved.DamageAmount = Weapon.Damage * Resolved.DamageMultiplier;
	// N s for receivers that read it (another pawn); kg cm/s / 100.
	Resolved.ImpulseNewtonSeconds = Resolved.ShotDirection * Weapon.HitImpulse / 100.0f;
	Resolved.bHasPhysicsImpulse = true;
	const bool bDelivered = MKDeliverResolvedImpact(Shooter, Shooter, Resolved);
	// Every loose body takes the gun's push at the hit point (MindKore's props
	// ignore rounds; here the push is the point).
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->IsSimulatingPhysics() && HitComponent->GetOwner() != Shooter)
	{
		HitComponent->AddImpulseAtLocation(Direction * Weapon.HitImpulse, Hit.ImpactPoint);
	}
	UE_LOG(LogMindKore, Verbose, TEXT("Jam shot: %s / %s, zone %d, damage %.1f, %s"), *GetNameSafe(Hit.GetActor()),
		*GetNameSafe(HitComponent), int32(Resolved.HitZone), Resolved.DamageAmount, bDelivered ? TEXT("delivered") : TEXT("no receiver"));
	if (Feedback.ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Feedback.ImpactSound, Hit.ImpactPoint);
	}
	if (UMaterialInterface* DecalMaterial = Feedback.DecalMaterial ? Feedback.DecalMaterial.Get() : DefaultImpactDecal.Get())
	{
		UGameplayStatics::SpawnDecalAttached(DecalMaterial, FVector(4.0f, 6.0f, 6.0f), HitComponent, NAME_None,
			Hit.ImpactPoint, (-Hit.ImpactNormal).Rotation(), EAttachLocation::KeepWorldPosition, 30.0f);
	}
	return true;
}

namespace
{
	/**
	 * jam.Weapons.Shots: first-person stills of the guns (HighResShot; needs a real
	 * RHI). The zero-G pawn floats 6 m in front of a living wall if there is one
	 * and looks at it; for the pistol, then the shotgun: the gun at rest, the
	 * shot's first frames (kick, tracers) and the pellets/rounds landing. Files
	 * land in Saved/Screenshots in this order; each is logged first.
	 */
	void RunWeaponShots(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Weapon shots: needs the AJamZeroGPawn"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
		FVector Stand = Pawn->GetActorLocation();
		if (TActorIterator<AJamLivingWall> Wall(World); Wall)
		{
			Target = Wall->GetActorLocation();
			Stand = Target + Wall->GetFrontNormal() * 600.0f;
		}
		const auto Hold = [Pawn, Controller, Stand, Target]()
		{
			Pawn->Body->SetWorldLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		};
		const auto Shot = [Pawn, Controller](const TCHAR* Name)
		{
			UE_LOG(LogMindKore, Log, TEXT("Weapon shots: %s - %s, %d round(s) in flight, view %s"), Name, *Pawn->Guns->Describe(),
				Pawn->Guns->GetRoundsInFlight(), *Controller->GetControlRotation().ToCompactString());
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		};
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		Hold();
		// Long first wait: shaders may still compile.
		for (int32 Gun = 0; Gun < 2; ++Gun)
		{
			const float At = 8.0f + Gun * 5.0f;
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			if (Gun == 1)
			{
				// The SV-98 at rest only (its shot would throw the pawn 7 m/s).
				After(At + 4.0f, [Pawn, Hold]() { Pawn->Guns->UnlockWeapon(2); Hold(); });
				After(At + 4.8f, [Shot]() { Shot(TEXT("SV-98 at rest")); });
			}
			After(At + 1.5f, [Shot, Gun]() { Shot(Gun ? TEXT("shotgun at rest") : TEXT("pistol at rest")); });
			After(At + 2.5f, [Pawn, Hold]() { Hold(); Pawn->Fire(); });
			After(At + 2.55f, [Shot, Gun]() { Shot(Gun ? TEXT("shotgun +0.05 s") : TEXT("pistol +0.05 s")); });
			After(At + (Gun ? 2.95f : 2.62f), [Shot, Gun]() { Shot(Gun ? TEXT("shotgun +0.45 s") : TEXT("pistol +0.12 s")); });
		}
	}

	FAutoConsoleCommandWithWorld GWeaponShots(
		TEXT("jam.Weapons.Shots"),
		TEXT("First-person stills of the pistol and the shotgun, then the SV-98 at rest: at rest, +0.05 s after a shot, then +0.12 s (pistol) / +0.45 s (shotgun, the hole) (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponShots));

	/**
	 * jam.Weapons.Cycles: the moving parts frame by frame (HighResShot; needs a real
	 * RHI). Logs every gun's curves at 5 ms, then for each gun: jam.Weapons.PoseAt
	 * holds the gun in view at points through its cycle (kick included) and a still
	 * is taken of each (JamCycle_<gun>_<nn>_<ms>.png in Saved/Screenshots), then one
	 * live shot with a still at +0.05 s; 1.5 s later, away from the still's long
	 * frame, one more shot and two tries to fire again - inside the cycle (refused)
	 * and just after it (fired), each logged with the seconds really gone by.
	 */
	void RunWeaponCycles(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		IConsoleVariable* PoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.PoseAt"));
		if (!Pawn || !PoseAt)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Weapon cycles: needs the AJamZeroGPawn"));
			return;
		}
		for (const FJamWeapon& Weapon : Pawn->Guns->Weapons)
		{
			const float Cycle = Weapon.GetActionCycleSeconds();
			UE_LOG(LogMindKore, Log, TEXT("Weapon cycle: %s cycle %.3f s, fire interval %.2f s, travel %.1f cm, turn %.0f deg"),
				*Weapon.Name.ToString(), Cycle, Weapon.FireInterval, Weapon.ActionTravelCm, Weapon.ActionTurnDeg);
			for (int32 Step = 0; Step * 0.005f <= Cycle + 0.0001f; ++Step)
			{
				const float T = Step * 0.005f;
				UE_LOG(LogMindKore, Log, TEXT("Weapon cycle sample: %d t=%.3f travel=%.4f turn=%.4f body=%.4f"), int32(&Weapon - Pawn->Guns->Weapons.GetData()), T,
					EvalCycle(Weapon.ActionTravel, T), EvalCycle(Weapon.ActionTurn, T), EvalCycle(Weapon.ActionBody, T));
			}
		}
		FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
		FVector Stand = Pawn->GetActorLocation();
		if (TActorIterator<AJamLivingWall> Wall(World); Wall)
		{
			Target = Wall->GetActorLocation();
			Stand = Target + Wall->GetFrontNormal() * 600.0f;
		}
		const auto Hold = [Pawn, Controller, Stand, Target]()
		{
			Pawn->Body->SetWorldLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		};
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		const TCHAR* Names[3] = { TEXT("pistol"), TEXT("shotgun"), TEXT("sv98") };
		const TArray<float> Phases[3] = {
			{ 0.0f, 0.012f, 0.025f, 0.035f, 0.05f, 0.07f, 0.09f, 0.1f, 0.115f, 0.145f },
			{ 0.12f, 0.18f, 0.22f, 0.26f, 0.28f, 0.33f, 0.37f, 0.4f, 0.42f, 0.47f, 0.51f, 0.56f },
			{ 0.25f, 0.3f, 0.35f, 0.38f, 0.5f, 0.63f, 0.665f, 0.75f, 0.82f, 0.87f, 0.99f, 1.05f, 1.09f, 1.12f, 1.25f } };
		// Inside the cycle, then just after it (or the fire interval, whichever is longer).
		const float Retry[3][2] = { { 0.1f, 0.17f }, { 0.4f, 0.6f }, { 1.0f, 1.3f } };
		float At = 8.0f; // shaders may still compile
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			After(At, [Pawn, Hold, PoseAt, Gun]() { PoseAt->Set(-1.0f, ECVF_SetByCode); Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			At += 1.5f;
			for (int32 Index = 0; Index < Phases[Gun].Num(); ++Index)
			{
				const float T = Phases[Gun][Index];
				After(At, [Hold, PoseAt, T]() { Hold(); PoseAt->Set(T, ECVF_SetByCode); });
				After(At + 0.2f, [Pawn, Controller, Name = Names[Gun], Index, T]()
				{
					const FJamWeapon* Weapon = Pawn->Guns->GetWeapon();
					const UStaticMeshComponent* Part = Pawn->Guns->ActionInView;
					UE_LOG(LogMindKore, Log, TEXT("Weapon cycles: %s %02d at %.3f s - travel %.3f turn %.3f, part at %s %s, muzzle %s"), Name, Index, T,
						Weapon ? EvalCycle(Weapon->ActionTravel, T) : 0.0f, Weapon ? EvalCycle(Weapon->ActionTurn, T) : 0.0f,
						*Part->GetRelativeLocation().ToCompactString(), *Part->GetRelativeRotation().ToCompactString(),
						*Pawn->Guns->GetMuzzleTransform().ToHumanReadableString());
					Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamCycle_%s_%02d_%04d"), Name, Index, FMath::RoundToInt(T * 1000.0f)));
				});
				At += 0.5f;
			}
			TSharedRef<double> ShotAt = MakeShared<double>(0.0);
			After(At, [Pawn, Hold, PoseAt, ShotAt]() { PoseAt->Set(-1.0f, ECVF_SetByCode); Hold(); Pawn->Fire(); *ShotAt = Pawn->GetWorld()->GetTimeSeconds(); });
			After(At + 0.05f, [Controller, Name = Names[Gun]]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Weapon cycles: %s live +0.05 s"), Name);
				Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamCycle_%s_live_0050"), Name));
			});
			After(At + 1.5f, [Pawn, Hold, ShotAt]() { Hold(); Pawn->Fire(); *ShotAt = Pawn->GetWorld()->GetTimeSeconds(); });
			for (const float Try : Retry[Gun])
			{
				After(At + 1.5f + Try, [Pawn, Hold, Name = Names[Gun], ShotAt]()
				{
					Hold();
					const double Since = Pawn->GetWorld()->GetTimeSeconds() - *ShotAt;
					const bool bFired = Pawn->Fire();
					UE_LOG(LogMindKore, Log, TEXT("Weapon cycles: %s fire again %.3f s after the shot (cycle %.3f s): %s"), Name, Since,
						Pawn->Guns->GetWeapon()->GetActionCycleSeconds(), bFired ? TEXT("fired") : TEXT("refused"));
				});
			}
			At += 4.0f;
		}
		After(At, [Hold]() { Hold(); UE_LOG(LogMindKore, Log, TEXT("Weapon cycles: done")); });
	}

	FAutoConsoleCommandWithWorld GWeaponCycles(
		TEXT("jam.Weapons.Cycles"),
		TEXT("The guns' moving parts frame by frame (jam.Weapons.PoseAt), a live shot each and the fire gate (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponCycles));
}
