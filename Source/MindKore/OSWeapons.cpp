#include "OSWeapons.h"

#include "OSFlyer.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "OSLivingWall.h"
#include "OSMuzzleFx.h"
#include "OSNixieCounter.h"
#include "OSScope.h"
#include "OSShip.h"
#include "OSZeroGPawn.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInterface.h"
#include "MKImpactDelivery.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"
#include "Sound/SoundEffectSource.h"
#include "Sound/SoundSubmix.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** os.Weapons.PoseAt: the gun in view held this many seconds after a shot (stills through a cycle); < 0 off. */
	TAutoConsoleVariable<float> CVarPoseAt(TEXT("os.Weapons.PoseAt"), -1.0f,
		TEXT("Hold the gun in view (kick and moving part) this many seconds after its last shot; < 0: live."));

	/**
	 * Cubic keys with broken tangents: the slopes are the curve's shape. A part that
	 * hits a stop arrives with speed (a steep arrive) and leaves the other way (the
	 * rebound) - the knock MindKore's hammer gets from its damped cosine
	 * (MKComputeFireReaction), here as keys the owner can drag in the editor.
	 */
	FRuntimeFloatCurve MakeCycleCurve(std::initializer_list<FOSCycleKey> Keys)
	{
		TArray<FRichCurveKey> RichKeys;
		for (const FOSCycleKey& Key : Keys)
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

FRuntimeFloatCurve OSMakeCycleCurve(std::initializer_list<FOSCycleKey> Keys)
{
	return MakeCycleCurve(Keys);
}

float OSEvalCycle(const FRuntimeFloatCurve& Curve, float Seconds)
{
	return EvalCycle(Curve, Seconds);
}

float FOSWeapon::GetActionCycleSeconds() const
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

bool FOSWeapon::CanLockOpen() const
{
	const FRichCurve* Rich = SlideLockTravel.GetRichCurveConst();
	return Rich && Rich->GetNumKeys() > 0;
}

UOSWeaponsComponent::UOSWeaponsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;

	// Guns as movement. Pistol: quick nudges (40 cm/s in zero-G); shotgun: a shove
	// and a spray that scatters junk; sniper: one slow shot that throws you back 7 m/s.
	// The rounds fly near real speeds (350 / 400 / 800 m/s); a tracer streaks the whole step each frame so they stay visible.
	// MindKore's own meshes (CREDITS.md): its enemies' USP, the hero's pump gun;
	// the SV-98 built the same way (Scripts/blender/os_guns.py).
	// Each in two parts, the frame and what moves (Scripts/blender/os_usp_split.py, os_guns.py).
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Usp(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_USP_Frame.SM_OS_USP_Frame"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UspSlide(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_USP_Slide.SM_OS_USP_Slide"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> PumpGun(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_Shotgun.SM_OS_Shotgun"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Pump(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_Shotgun_Pump.SM_OS_Shotgun_Pump"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sv98(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_SV98.SM_OS_SV98"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sv98Bolt(TEXT("/Game/OSArt/Meshes/Weapons/SM_OS_SV98_Bolt.SM_OS_SV98_Bolt"));
	// Recorded CC0 donors heard through the suit (CREDITS.md, Docs/RecoilWeapons-Sound.md).
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolShot(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Shot_Pistol.SW_OS_Shot_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunShot(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Shot_Shotgun.SW_OS_Shot_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunPump(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Pump_Shotgun.SW_OS_Pump_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperShot(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Shot_SV98.SW_OS_Shot_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperBolt(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Bolt_SV98.SW_OS_Bolt_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolEmpty(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Empty_Pistol.SW_OS_Empty_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunEmpty(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Empty_Shotgun.SW_OS_Empty_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperEmpty(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Empty_SV98.SW_OS_Empty_SV98"));
	// The mechanism hooks of OSWeaponParts.cpp (hammer, magazines, shells), same donors' treatment.
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolHammer(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Hammer_Pistol.SW_OS_Hammer_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolMagOut(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_MagOut_Pistol.SW_OS_MagOut_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolMagIn(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_MagIn_Pistol.SW_OS_MagIn_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperMagOut(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_MagOut_SV98.SW_OS_MagOut_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperMagIn(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_MagIn_SV98.SW_OS_MagIn_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunShell(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_Shell_Shotgun.SW_OS_Shell_Shotgun"));
	// In air (Docs/RecoilWeapons-Sound.md, «В воздухе»): each shot's crack (Scripts/os_air_sounds.py), the cabin's
	// short metal room and the mechanisms' lift of the highs (Scripts/os_air_audio.py).
	static ConstructorHelpers::FObjectFinder<USoundBase> PistolAir(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_ShotAir_Pistol.SW_OS_ShotAir_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> ShotgunAir(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_ShotAir_Shotgun.SW_OS_ShotAir_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> SniperAir(TEXT("/Game/OSArt/Audio/Weapons/SW_OS_ShotAir_SV98.SW_OS_ShotAir_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundSubmixBase> CabinReverb(TEXT("/Game/OSArt/Audio/Mix/SM_OS_CabinReverb.SM_OS_CabinReverb"));
	static ConstructorHelpers::FObjectFinder<USoundEffectSourcePresetChain> GunAirLift(TEXT("/Game/OSArt/Audio/Mix/SEC_OS_GunAirLift.SEC_OS_GunAirLift"));
	AirReverb = CabinReverb.Object;
	AirLift = GunAirLift.Object;
	FOSWeapon Pistol;
	Pistol.Name = NSLOCTEXT("OSZeroG", "Pistol", "Пистолет");
	Pistol.Damage = 25.0f;
	Pistol.FireInterval = 0.15f;
	Pistol.MagazineSize = 12;
	Pistol.Reserve = 60;
	Pistol.ReloadSeconds = 0.97f; // the seat at 0.86 s, out by 0.95 (OSWeaponParts.cpp; owner 19.10: the tail halved)
	Pistol.SpreadDeg = 0.5f;
	Pistol.RecoilImpulse = 4000.0f;
	Pistol.HitImpulse = 3000.0f;
	Pistol.ShotSound = PistolShot.Object;
	Pistol.ShotAirSound = PistolAir.Object;
	Pistol.EmptySound = PistolEmpty.Object;
	Pistol.HammerSound = PistolHammer.Object;
	Pistol.MagOutSound = PistolMagOut.Object;
	Pistol.MagInSound = PistolMagIn.Object;
	// One shot per click on every gun (owner 22.10: no fire while LMB is held); bAutomatic stays for a gun that wants it.
	Pistol.bAutomatic = false;
	// Near real muzzle velocities (owner 19.10: slow rounds made the flesh walls answer late): 9 mm 350 m/s.
	Pistol.ProjectileSpeed = 35000.0f;
	Pistol.SmokeHoleCm = 4.0f;
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
	FOSWeapon Shotgun;
	Shotgun.Name = NSLOCTEXT("OSZeroG", "Shotgun", "Дробовик");
	Shotgun.Damage = 12.0f;
	// The pump stroke sets the pace (its cycle ends at 0.56 s).
	Shotgun.FireInterval = 0.5f;
	Shotgun.MagazineSize = 6;
	Shotgun.Reserve = 24;
	Shotgun.ReloadSeconds = 0.42f; // per round: it loads round by round (OSWeaponParts.cpp)
	Shotgun.Pellets = 8;
	Shotgun.SpreadDeg = 6.0f;
	Shotgun.RecoilImpulse = 25000.0f;
	Shotgun.HitImpulse = 4000.0f;
	Shotgun.RangeCm = 3000.0f;
	Shotgun.ShotSound = ShotgunShot.Object;
	Shotgun.ShotAirSound = ShotgunAir.Object;
	Shotgun.CycleSound = ShotgunPump.Object;
	Shotgun.CycleDelaySeconds = 0.15f; // the pump's clicks at 0.26 and 0.40 s after the shot, as its animation
	Shotgun.EmptySound = ShotgunEmpty.Object;
	Shotgun.ShellInsertSound = ShotgunShell.Object;
	Shotgun.ProjectileSpeed = 40000.0f; // buckshot ~400 m/s
	Shotgun.SmokeHoleCm = 4.5f; // each pellet its own
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
	// is where its pose springs pull (OSWeaponParts.cpp), in at 0.12 s, out at 0.36 s.
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
	FOSWeapon Sniper;
	Sniper.Name = NSLOCTEXT("OSZeroG", "Sniper", "Снапа");
	Sniper.Damage = 150.0f;
	// The bolt sets the pace (its cycle ends at 1.25 s).
	Sniper.FireInterval = 0.5f;
	Sniper.MagazineSize = 4;
	Sniper.Reserve = 16;
	Sniper.ReloadSeconds = 1.7f; // the magazine out and in (OSWeaponParts.cpp)
	Sniper.RecoilImpulse = 70000.0f;
	Sniper.HitImpulse = 60000.0f;
	Sniper.RangeCm = 15000.0f;
	Sniper.ShotSound = SniperShot.Object;
	Sniper.ShotAirSound = SniperAir.Object;
	Sniper.CycleSound = SniperBolt.Object;
	Sniper.CycleDelaySeconds = 0.25f; // the bolt's clicks at 0.35, 0.63, 0.87, 1.09 s after the shot
	Sniper.EmptySound = SniperEmpty.Object;
	Sniper.MagOutSound = SniperMagOut.Object;
	Sniper.MagInSound = SniperMagIn.Object;
	Sniper.ProjectileSpeed = 80000.0f; // 7.62x54R ~800 m/s
	Sniper.SmokeHoleCm = 8.0f;
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
	// The gas (FOSGasVent, Docs/RecoilWeapons-Fx.md «Газовые отверстия»): a jet into the smoke grid and a thin stream
	// of wisps out of each muzzle (the stream is the first thing seen, the jet takes over); the SV-98's brake throws
	// most of it out of two side ports.
	const auto Vent = [](const FVector& Direction, bool bJet, float Seconds, float Decay, float Speed, float Cone, float Rate, float Width)
	{
		FOSGasVent V;
		V.Direction = Direction;
		V.bJet = bJet;
		V.Seconds = Seconds;
		V.DecaySeconds = Decay;
		V.SpeedCm = Speed;
		V.ConeDeg = Cone;
		V.Rate = Rate;
		V.WidthCm = Width;
		return V;
	};
	// A short sharp blast - the burst dies with DecaySeconds (0.05-0.07 s), a thin dribble to Seconds (0.2 s at most;
	// v6, owner: «слишком долго льется из ствола») - fast enough that the front still runs out about as far
	// (SpeedCm x DecaySeconds: the shotgun's ~1.05 m; further than a real one, the art first).
	Pistol.GasVents = { Vent(FVector::ForwardVector, true, 0.15f, 0.05f, 900.0f, 8.0f, 0.5f, 2.5f),
		Vent(FVector::ForwardVector, false, 0.1f, 0.04f, 150.0f, 10.0f, 40.0f, 1.0f) };
	Shotgun.GasVents = { Vent(FVector::ForwardVector, true, 0.2f, 0.07f, 1500.0f, 6.0f, 0.7f, 6.0f),
		Vent(FVector::ForwardVector, false, 0.12f, 0.05f, 220.0f, 8.0f, 60.0f, 1.5f) };
	Sniper.GasVents = { Vent(FVector::ForwardVector, true, 0.15f, 0.05f, 1200.0f, 4.0f, 0.4f, 3.0f),
		Vent(FVector(-0.35f, 1.0f, 0.0f), true, 0.15f, 0.05f, 1000.0f, 10.0f, 0.6f, 4.0f),
		Vent(FVector(-0.35f, -1.0f, 0.0f), true, 0.15f, 0.05f, 1000.0f, 10.0f, 0.6f, 4.0f),
		Vent(FVector::ForwardVector, false, 0.1f, 0.04f, 220.0f, 6.0f, 40.0f, 1.0f) };
	// The muzzle streams' wisps: thin, short-lived.
	for (FOSWeapon* Gun : { &Pistol, &Shotgun, &Sniper })
	{
		Gun->PortSmoke.StartCm = 1.5f;
		Gun->PortSmoke.EndCm = 7.0f;
		Gun->PortSmoke.Seconds = 1.0f;
		Gun->PortSmoke.Opacity = 0.3f;
	}
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

void UOSWeaponsComponent::BeginPlay()
{
	Super::BeginPlay();
	for (FOSWeapon& Weapon : Weapons)
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
		// Muzzle flash (on the gun in view) and powder smoke (in the world): OSMuzzleFx.h.
		UOSMuzzleFxComponent* Fx = NewObject<UOSMuzzleFxComponent>(GetOwner(), TEXT("MuzzleFx"));
		Fx->RegisterComponent();
		MuzzleFx = Fx;
	}
	// Every gun's aim and the SV-98's scope; it builds itself once the gun in view exists (UOSScopeComponent::Build).
	Scope = NewObject<UOSScopeComponent>(GetOwner(), TEXT("Scope"));
	Scope->RegisterComponent();
}

bool UOSWeaponsComponent::IsInHand() const
{
	const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner());
	return !Pawn || Pawn->GetHands() == EOSHands::Gun;
}

const FOSWeapon* UOSWeaponsComponent::GetWeapon() const
{
	return IsInHand() && IsWeaponUnlocked(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
}

void UOSWeaponsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
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
		const FOSWeapon& Weapon = Weapons[CurrentWeapon];
		ReloadEndSeconds = 0.0;
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s reloaded, %d/%d"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString(), Weapon.Ammo, Weapon.Reserve);
	}
	UpdateRounds(DeltaTime);
	UpdateRecoil(DeltaTime);
	UpdateViewModel(DeltaTime);
}

void UOSWeaponsComponent::UpdateRounds(float DeltaTime)
{
	for (int32 Index = Rounds.Num() - 1; Index >= 0; --Index)
	{
		FRound& Round = Rounds[Index];
		const FOSWeapon& Weapon = Weapons[Round.Weapon];
		const float Step = FMath::Min(Weapon.ProjectileSpeed * DeltaTime, Weapon.RangeCm - Round.TravelledCm);
		const FVector Next = Round.Position + Round.Direction * Step;
		FVector Impact = Next;
		// A landed round's streak was drawn to its impact for one frame; now it goes.
		const bool bLands = !Round.bLanded && Step > 0.0f && TraceShot(Weapon, Round.Position, Next, Round.Direction, &Impact);
		if (Round.bLanded || (!bLands && (Step <= 0.0f || Round.TravelledCm + Step >= Weapon.RangeCm)))
		{
			if (UStaticMeshComponent* Tracer = Round.Tracer.Get())
			{
				Tracer->DestroyComponent();
			}
			Rounds.RemoveAtSwap(Index);
			continue;
		}
		const FVector Tail = TracerHead(Round);
		Round.TravelledCm += float(FVector::Dist(Round.Position, Impact));
		Round.Position = Impact;
		Round.bLanded = bLands;
		// Its flight this frame punches a tunnel through any smoke on the way (along the streak: the round's path).
		if (UOSMuzzleFxComponent* Fx = GetOwner()->FindComponentByClass<UOSMuzzleFxComponent>())
		{
			Fx->CarveRay(Tail, TracerHead(Round), Weapon.SmokeHoleCm);
		}
		if (UStaticMeshComponent* Tracer = Round.Tracer.Get())
		{
			// The streak covers this frame's flight (a fast round is a long streak, as on film; at
			// 400 m/s and 60 fps 6.7 m), at least TracerLengthCm behind the head (slow time, slow rounds).
			const FVector Head = TracerHead(Round);
			const FVector Along = (Head - Tail).GetSafeNormal(UE_SMALL_NUMBER, Round.Direction);
			const float Length = FMath::Max(float(FVector::Dist(Tail, Head)), FMath::Min(TracerLengthCm, Round.TravelledCm));
			Tracer->SetWorldLocationAndRotation(Head - Along * Length * 0.5f, Along.Rotation());
			Tracer->SetWorldScale3D(FVector(Length, TracerThicknessCm, TracerThicknessCm) / 100.0f);
			Tracer->SetVisibility(true);
		}
	}
}

void UOSWeaponsComponent::TurnView(float PitchDeg, float YawDeg) const
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
	if (AOSShipController* Ship = Cast<AOSShipController>(Controller))
	{
		Ship->AddViewKick(FRotator(PitchDeg, YawDeg, 0.0f));
		return;
	}
	// Another controller: at once, in the view's own frame (a rolled view kicks toward its own up).
	const FQuat Kick = FRotator(PitchDeg, YawDeg, 0.0f).Quaternion();
	Controller->SetControlRotation((Controller->GetControlRotation().Quaternion() * Kick).Rotator());
}

void UOSWeaponsComponent::UpdateRecoil(float DeltaTime)
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

void UOSWeaponsComponent::UpdateViewModel(float DeltaTime)
{
	if (!ViewModel)
	{
		return;
	}
	// The hands on something else (a hull tool) put the gun away; a switch puts one away and
	// draws the next (OSWeaponParts.cpp). The gun shown is the one on its way, not yet the current one.
	const FOSWeapon* Weapon = UpdateSwitch(DeltaTime, !IsInHand(), PoseSwitch);
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
			FOSWeapon& Gun = Weapons[Running];
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
		LidTrackFrames = 0; // the lid's motion is tracked afresh when a gun is back in view
		return;
	}
	SinceKickSeconds += DeltaTime;
	const float PoseAt = CVarPoseAt.GetValueOnGameThread();
	const float Since = PoseAt >= 0.0f ? PoseAt : Weapon->SinceShot;
	// Back along the view, the muzzle up and a little roll, about the grip (the
	// meshes' origin); the rise turns about the camera's right axis whichever
	// way the mesh itself faces. Springs back at ViewKickReturnRate.
	// Aimed (OSScope.h), the gun rests at the eye and kicks less.
	FTransform Offset = Weapon->ViewOffset;
	float KickScale = 1.0f;
	if (Scope)
	{
		Scope->AimPose(ShownWeapon, Offset, KickScale);
	}
	const float Kick = KickScale * FMath::Exp(-ViewKickReturnRate * (PoseAt >= 0.0f ? PoseAt : SinceKickSeconds));
	const FQuat Rise = FRotator(Weapon->ViewKickRiseDeg * Kick, 0.0f, -0.3f * Weapon->ViewKickRiseDeg * Kick).Quaternion();
	// The whole gun's pose in the cycle and the reload: a mass on springs (OSWeaponParts.cpp).
	UpdatePose(*Weapon, DeltaTime);
	// The gun as its springs hold it, without the drawn kick: the frame the lid swings in (the kick reaches it as ShotSpeedDeg).
	FTransform Steady = Offset;
	Steady.SetRotation(FRotator(Pose.Turn.X, Pose.Turn.Y, Pose.Turn.Z).Quaternion() * Offset.GetRotation());
	Steady.AddToTranslation(Pose.Place);
	Offset.SetRotation(Rise * Steady.GetRotation());
	Offset.SetTranslation(Steady.GetTranslation() + FVector(-Weapon->ViewKickBackCm * Kick, 0.0f, 0.0f));
	ViewModel->SetRelativeTransform(Offset);
	// The moving part runs its cycle along/about the gun's own X (or stands locked open, or runs home on its release).
	ActionInView->SetRelativeLocationAndRotation(
		Weapon->ActionPivot + FVector(Weapon->ActionTravelCm * GetSlideAndHammer(*Weapon, Since).X, 0.0f, 0.0f),
		FQuat(FVector::ForwardVector, FMath::DegreesToRadians(Weapon->ActionTurnDeg * EvalCycle(Weapon->ActionTurn, Since))));
	UpdateParts(Weapon, Since);
	UpdateCycleEvents();
	// The RMB-93's lid and shroud (OSWeaponLid.cpp); nothing for a gun without them.
	FOSWeapon& Shown = Weapons[ShownWeapon];
	if (Shown.Lid.Mesh || Shown.HasSeep())
	{
		UpdateLid(Shown, Steady * ViewModel->GetAttachParent()->GetComponentTransform(), DeltaTime);
		UpdatePortSmoke(Shown, DeltaTime);
	}
}

FTransform UOSWeaponsComponent::GetMuzzleTransform() const
{
	const FOSWeapon* Weapon = GetWeapon();
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
		// No socket (a mesh not from Scripts/os_weapons.py): its front, on its origin's line.
		: ViewModel->GetComponentTransform().TransformPosition(FVector(ViewModel->GetStaticMesh()->GetBoundingBox().Max.X, 0.0f, 0.0f));
	return FTransform(Bore, Location);
}

void UOSWeaponsComponent::SelectStep(int32 Step)
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

void UOSWeaponsComponent::UnlockWeapon(int32 Index)
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

bool UOSWeaponsComponent::ApplyPickupWeapon(int32 Index, const FOSWeapon& Definition)
{
	if (!Weapons.IsValidIndex(Index) || !Definition.ViewMesh || Definition.MagazineSize < 0) return false;
	// A definition may come from this slot; copy it before clearing its old runtime state.
	FOSWeapon Fresh = Definition;
	Fresh.Ammo = Fresh.MagazineSize;
	Fresh.bUnlocked = false;
	Fresh.bSlideLocked = false;
	Fresh.SinceShot = Fresh.PrevSinceShot = 1000.0f;
	Fresh.FireReadySince = 0.0f;
	Fresh.MechanismSound.Reset();
	Fresh.Lid.State = EOSLidState::Latched;
	Fresh.Lid.Angle = Fresh.Lid.Speed = 0.0f;
	Fresh.Lid.bHeldClear = false;
	Fresh.Lid.bCatchPending = false;
	Fresh.PortSmoke.Heat = Fresh.PortSmoke.SeepCarry = 0.0f;
	if (UAudioComponent* Sound = Weapons[Index].MechanismSound.Get()) Sound->Stop();
	if (CurrentWeapon == Index || ShownWeapon == Index) Holster(true);
	if (CycleWeapon == Index) CycleWeapon = INDEX_NONE;
	Weapons[Index] = MoveTemp(Fresh);
	return true;
}

void UOSWeaponsComponent::SetViewMeshes(const FOSWeapon* Weapon)
{
	// Release before changing the asset: an old counter override must never leak to another gun.
	if (AmmoCounter)
	{
		AmmoCounter->ReleaseBinding();
		AmmoCounter->SetCounterEnabled(false);
	}
	ViewModel->SetStaticMesh(Weapon ? Weapon->ViewMesh.Get() : nullptr);
	ActionInView->SetStaticMesh(Weapon ? Weapon->ActionMesh.Get() : nullptr);
	bCounterPowerPending = false;
	if (!Weapon || Weapon->NixieMaterialSlot < 0) return;
	if (!AmmoCounter)
	{
		AmmoCounter = NewObject<UOSNixieCounterComponent>(GetOwner(), TEXT("WeaponAmmoCounter"));
		AmmoCounter->MaterialSlots.Reset(); // Register/BeginPlay must not bind the pawn's root mesh.
		AmmoCounter->RegisterComponent();
		AmmoCounter->SetCounterEnabled(false); // made lit by default: dark until PowerCounter
	}
	AmmoCounter->SetDigit(Weapon->Ammo);
	AmmoCounter->BindMesh(ViewModel, { Weapon->NixieMaterialSlot });
	// Dark until the draw brings it into frame (PowerCounter): the tube's warm-up plays where it is seen, not behind
	// the shoulder (owner 22.10).
	bCounterPowerPending = true;
}

void UOSWeaponsComponent::PowerCounter()
{
	if (!bCounterPowerPending || !AmmoCounter) return;
	bCounterPowerPending = false;
	AmmoCounter->SetCounterEnabled(true);
}

void UOSWeaponsComponent::UpdateCounterAmmo()
{
	if (AmmoCounter && Weapons.IsValidIndex(ShownWeapon)) AmmoCounter->SetDigit(Weapons[ShownWeapon].Ammo);
}

void UOSWeaponsComponent::Holster(bool bAtOnce)
{
	bReloadQueued = false;
	ReloadEndSeconds = 0.0; // as a switch: the reload is abandoned, the gun's cycle freezes (UpdateViewModel)
	if (bAtOnce)
	{
		ClearView(); // gone this frame, no put-away (OSWeaponParts.cpp)
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s"), *GetNameSafe(GetOwner()), bAtOnce ? TEXT("unarmed") : TEXT("put away"));
}

void UOSWeaponsComponent::Rearm()
{
	if (!IsWeaponUnlocked(CurrentWeapon))
	{
		return;
	}
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: rearmed - %s"), *GetNameSafe(GetOwner()), *Weapons[CurrentWeapon].Name.ToString());
	Draw();
}

void UOSWeaponsComponent::Draw()
{
	// Only the log line: with the gun wanted again, UpdateSwitch (OSWeaponParts.cpp) draws it in from away
	// along its own path - the pistol from the right-hip holster, the shotgun over the right shoulder, the
	// SV-98 over the left - on the pose springs; it fires once drawn and settled (IsDrawing).
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: draw %s"), *GetNameSafe(GetOwner()), Weapons.IsValidIndex(CurrentWeapon) ? *Weapons[CurrentWeapon].Name.ToString() : TEXT("-"));
}

bool UOSWeaponsComponent::IsDrawing() const
{
	return GetWeapon() && !IsWeaponReady();
}

void UOSWeaponsComponent::SelectWeapon(int32 Index)
{
	// Any switch is UpdateSwitch's: it puts the shown gun away and draws the current one. The hands are the
	// owner's: out of them (a tool, a body held) this only picks the gun they take next (AOSZeroGPawn::PressSlot).
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

float UOSWeaponsComponent::GetReloadRemaining() const
{
	return FMath::Max(0.0f, float(ReloadEndSeconds - GetWorld()->GetTimeSeconds()));
}

FString UOSWeaponsComponent::Describe() const
{
	// A gun put away for a hull tool keeps its line (its rounds still count); holding a body is unarmed.
	const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner());
	const FOSWeapon* Weapon = IsWeaponUnlocked(CurrentWeapon) && !(Pawn && Pawn->GetHands() == EOSHands::Held) ? &Weapons[CurrentWeapon] : nullptr;
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

void UOSWeaponsComponent::Reload()
{
	FOSWeapon* Weapon = GetWeapon() ? &Weapons[CurrentWeapon] : nullptr;
	// A magazine seated in a locked gun (an empty reload cut between the seat and the release) needs only the slide stop.
	const bool bReleaseOnly = Weapon && Weapon->bSlideLocked && Weapon->Ammo > 0;
	// A loading lid left open (a reload broken off by a shot) is shut by a reload even with nothing to load (OSWeaponLid.cpp).
	const bool bLidOnly = Weapon && Weapon->Lid.Mesh && Weapon->Lid.State == EOSLidState::Free;
	if (!Weapon || !IsWeaponReady() || (!bReleaseOnly && !bLidOnly && (Weapon->Ammo >= Weapon->MagazineSize || Weapon->Reserve <= 0))
		|| GetReloadRemaining() > 0.0f)
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
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: reloading %s, %.2f s%s"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString(), Seconds,
		!bEmptyReload ? TEXT("") : ReloadFromSeconds > 0.0f ? TEXT(" (the slide release only)") : TEXT(" (empty: the slide released at the end)"));
}

bool UOSWeaponsComponent::Fire(const FVector& Start, const FVector& Direction, FVector& OutRecoil)
{
	OutRecoil = FVector::ZeroVector;
	FOSWeapon* Weapon = GetWeapon() ? &Weapons[CurrentWeapon] : nullptr;
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
	if (Weapon->bSlideLocked)
	{
		// Held open by the slide stop: the slide out of battery keeps the disconnector down, so the trigger moves
		// dead - no shot, no hammer fall and no click (a real USP is silent here). Only R reloads (owner 22.10: no auto reload).
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s - the slide is locked open, the trigger is dead"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString());
		Weapon->FireReadySince = Weapon->SinceShot + Weapon->FireInterval;
		return false;
	}
	if (Weapon->Ammo <= 0)
	{
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s is empty"), *GetNameSafe(GetOwner()), *Weapon->Name.ToString());
		// The dry click, once per pull; only R reloads (owner 22.10: no auto reload).
		PlayGunSound(Weapon->EmptySound);
		Weapon->FireReadySince = Weapon->SinceShot + Weapon->FireInterval;
		return false;
	}
	--Weapon->Ammo;
	if (Weapon->CounterSeconds <= 0.0f)
	{
		UpdateCounterAmmo(); // else with the cycle (UpdatePartEvents): the pump's home counts it off
	}
	// The last round: the empty magazine's follower lifts the slide stop, and this stroke ends locked open (SlideLockTravel).
	Weapon->bSlideLocked = Weapon->Ammo == 0 && Weapon->CanLockOpen();
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
	PlayShotSound(*Weapon, FMath::FRandRange(0.97f, 1.03f));
	const FTransform MuzzleTransform = GetMuzzleTransform();
	const FVector Muzzle = MuzzleTransform.GetLocation();
	AOSFlyerHabitat::ReportNoise(this, Muzzle, nullptr, TEXT("a shot")); // flyers hear it only through air
	if (UOSMuzzleFxComponent* Fx = GetOwner()->FindComponentByClass<UOSMuzzleFxComponent>())
	{
		// Before the kick (the caller adds OutRecoil): the smoke takes the gun's velocity at the shot.
		Fx->Shot(CurrentWeapon, MuzzleTransform, ViewModel);
		// The gas out of every vent of the gun (the muzzle's jet, a brake's ports, a shroud's holes).
		Fx->Vents(*Weapon, MuzzleTransform, ActionInView);
	}
	// The RMB-93: its open lid jerked by the kick, its shroud's holes puffing (OSWeaponLid.cpp).
	bLidShot = Weapon->Lid.Mesh != nullptr;
	PortSmokeShot(*Weapon);
	for (int32 Pellet = 0; Pellet < Weapon->Pellets; ++Pellet)
	{
		const FVector PelletDirection = Weapon->SpreadDeg > 0.0f
			? FMath::VRandCone(Direction, FMath::DegreesToRadians(Weapon->SpreadDeg))
			: Direction;
		if (Weapon->ProjectileSpeed <= 0.0f)
		{
			FVector Impact = Start + PelletDirection * Weapon->RangeCm;
			TraceShot(*Weapon, Start, Impact, PelletDirection, &Impact);
			if (UOSMuzzleFxComponent* Fx = GetOwner()->FindComponentByClass<UOSMuzzleFxComponent>())
			{
				Fx->CarveRay(Start, Impact, Weapon->SmokeHoleCm);
			}
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

FVector UOSWeaponsComponent::TracerHead(const FRound& Round) const
{
	// On the round, joining its path from the muzzle over TracerConvergeCm.
	return Round.Position + Round.MuzzleOffset * FMath::Max(0.0f, 1.0f - Round.TravelledCm / TracerConvergeCm);
}

bool UOSWeaponsComponent::TraceShot(const FOSWeapon& Weapon, const FVector& Start, const FVector& End, const FVector& Direction, FVector* OutImpact)
{
	UWorld* World = GetWorld();
	AActor* Shooter = GetOwner();
	// As UMKFirearmComponent traces: the Shot channel (ECC_GameTraceChannel2),
	// complex collision (the glass pane's shard triangles), physical material;
	// multi-trace and MKSelectResolvedHit, so hit-zone boxes (overlaps in front
	// of the blocking hit) win on zone priority within the same actor.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSShot), /*bTraceComplex=*/true, Shooter);
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
	if (OutImpact)
	{
		*OutImpact = Hit.ImpactPoint;
	}
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
	AOSFlyerHabitat::ReportNoise(this, Hit.ImpactPoint, Hit.GetActor(), TEXT("a round landing")); // air, or a roost's hull (contact)
	// Every loose body takes the gun's push at the hit point (MindKore's props
	// ignore rounds; here the push is the point).
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->IsSimulatingPhysics() && HitComponent->GetOwner() != Shooter)
	{
		HitComponent->AddImpulseAtLocation(Direction * Weapon.HitImpulse, Hit.ImpactPoint);
	}
	UE_LOG(LogMindKore, Verbose, TEXT("OS shot: %s / %s, zone %d, damage %.1f, %s"), *GetNameSafe(Hit.GetActor()),
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
	 * os.Weapons.Shots: first-person stills of the guns (HighResShot; needs a real
	 * RHI). The zero-G pawn floats 6 m in front of a living wall if there is one
	 * and looks at it; for the pistol, then the shotgun: the gun at rest, the
	 * shot's first frames (kick, tracers) and the pellets/rounds landing. Files
	 * land in Saved/Screenshots in this order; each is logged first.
	 */
	void RunWeaponShots(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Weapon shots: needs the AOSZeroGPawn"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(600.0f, 0.0f, 0.0f);
		FVector Stand = Pawn->GetActorLocation();
		if (TActorIterator<AOSLivingWall> Wall(World); Wall)
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
		TEXT("os.Weapons.Shots"),
		TEXT("First-person stills of the pistol and the shotgun, then the SV-98 at rest: at rest, +0.05 s after a shot, then +0.12 s (pistol) / +0.45 s (shotgun, the hole) (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponShots));

	/**
	 * os.Weapons.Cycles: the moving parts frame by frame (HighResShot; needs a real
	 * RHI). Logs every gun's curves at 5 ms, then for each gun: os.Weapons.PoseAt
	 * holds the gun in view at points through its cycle (kick included) and a still
	 * is taken of each (OSCycle_<gun>_<nn>_<ms>.png in Saved/Screenshots), then one
	 * live shot with a still at +0.05 s; 1.5 s later, away from the still's long
	 * frame, one more shot and two tries to fire again - inside the cycle (refused)
	 * and just after it (fired), each logged with the seconds really gone by.
	 */
	void RunWeaponCycles(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		IConsoleVariable* PoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Weapons.PoseAt"));
		if (!Pawn || !PoseAt)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Weapon cycles: needs the AOSZeroGPawn"));
			return;
		}
		for (const FOSWeapon& Weapon : Pawn->Guns->Weapons)
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
		if (TActorIterator<AOSLivingWall> Wall(World); Wall)
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
					const FOSWeapon* Weapon = Pawn->Guns->GetWeapon();
					const UStaticMeshComponent* Part = Pawn->Guns->ActionInView;
					UE_LOG(LogMindKore, Log, TEXT("Weapon cycles: %s %02d at %.3f s - travel %.3f turn %.3f, part at %s %s, muzzle %s"), Name, Index, T,
						Weapon ? EvalCycle(Weapon->ActionTravel, T) : 0.0f, Weapon ? EvalCycle(Weapon->ActionTurn, T) : 0.0f,
						*Part->GetRelativeLocation().ToCompactString(), *Part->GetRelativeRotation().ToCompactString(),
						*Pawn->Guns->GetMuzzleTransform().ToHumanReadableString());
					Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSCycle_%s_%02d_%04d"), Name, Index, FMath::RoundToInt(T * 1000.0f)));
				});
				At += 0.5f;
			}
			TSharedRef<double> ShotAt = MakeShared<double>(0.0);
			After(At, [Pawn, Hold, PoseAt, ShotAt]() { PoseAt->Set(-1.0f, ECVF_SetByCode); Hold(); Pawn->Fire(); *ShotAt = Pawn->GetWorld()->GetTimeSeconds(); });
			After(At + 0.05f, [Controller, Name = Names[Gun]]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Weapon cycles: %s live +0.05 s"), Name);
				Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSCycle_%s_live_0050"), Name));
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
		TEXT("os.Weapons.Cycles"),
		TEXT("The guns' moving parts frame by frame (os.Weapons.PoseAt), a live shot each and the fire gate (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponCycles));
}
