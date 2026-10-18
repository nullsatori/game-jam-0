// The guns' smaller moving parts and what they throw into the world: the USP's
// hammer, spent cases, magazines out and in, the pump gun's round-by-round tube
// loading. Members of UJamWeaponsComponent (JamWeapons.h), kept apart from
// JamWeapons.cpp so the parts and the shooting change in separate files.

#include "JamWeapons.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "JamLivingWall.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	/** jam.Weapons.ReloadPoseAt: the gun in view held this many seconds into a reload (stills); < 0 off. */
	TAutoConsoleVariable<float> CVarReloadPoseAt(TEXT("jam.Weapons.ReloadPoseAt"), -1.0f,
		TEXT("Hold the gun in view this many seconds into a reload (a round-by-round gun loads two rounds); < 0: live."));

	/** Rounds a held reload pose loads, for jam.Weapons.ReloadPoseAt. */
	constexpr int32 PoseShells = 2;

	float LastKeyTime(const FRuntimeFloatCurve& Curve)
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->GetLastKey().Time : 0.0f;
	}

	float ReloadDuration(const FJamWeapon& Weapon, int32 Shells)
	{
		// Round by round: the lead in (the gun turns up), the rounds, the lead out.
		return Weapon.ShellMesh ? 2.0f * LastKeyTime(Weapon.ReloadBody) + Shells * Weapon.ReloadSeconds : Weapon.ReloadSeconds;
	}

	bool Crossed(float Before, float Now, float At)
	{
		return Before < At && Now >= At;
	}

	void SetPart(UStaticMeshComponent* Part, UStaticMesh* Mesh, bool bVisible)
	{
		Part->SetStaticMesh(Mesh);
		Part->SetVisibility(Mesh && bVisible);
	}
}

void UJamWeaponsComponent::SetupWeaponParts()
{
	if (Weapons.Num() < 3)
	{
		return;
	}
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UspHammer(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_USP_Hammer.SM_Jam_USP_Hammer"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> UspMag(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_USP_Mag.SM_Jam_USP_Mag"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Sv98Mag(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_SV98_Mag.SM_Jam_SV98_Mag"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Case9(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_Case_9mm.SM_Jam_Case_9mm"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Case762(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_Case_762x54R.SM_Jam_Case_762x54R"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Shell12(TEXT("/Game/JamArt/Meshes/Weapons/SM_Jam_Shell_12ga.SM_Jam_Shell_12ga"));

	// --- USP ---
	FJamWeapon& Pistol = Weapons[0];
	// The hammer stands cocked. The shot drops it forward in 12 ms; the slide, running
	// back over it, shoves it 27 deg past cocked; coming home, the slide lets it ride
	// up into the sear with a 6% nod forward. Mostly under the slide: the fall and
	// the return are what shows.
	Pistol.HammerMesh = UspHammer.Object;
	Pistol.HammerPivot = FVector(3.5f, -1.45f, 2.75f);
	Pistol.HammerTurnDeg = -50.0f;
	Pistol.HammerTurn = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.012f, 1.0f, 150.0f, 0.0f }, { 0.016f, 1.0f, 0.0f, -80.0f },
		{ 0.035f, -0.55f, -20.0f, 0.0f }, { 0.05f, -0.55f, 0.0f, 0.0f }, { 0.085f, 0.0f, 25.0f, 6.0f }, { 0.1f, 0.06f, 0.0f, 0.0f },
		{ 0.13f, 0.0f, 0.0f, 0.0f } });
	Pistol.HammerSoundSeconds = 0.085f;
	// The case leaves the port on the right as the slide is 80% open.
	Pistol.CaseMesh = Case9.Object;
	Pistol.EjectPort = FVector(-5.5f, -2.6f, 5.0f);
	Pistol.EjectSeconds = 0.02f;
	Pistol.EjectVelocity = FVector(-50.0f, 250.0f, 140.0f);
	// The magazine: a press on the catch nudges it in, then it is flicked out along the
	// grip (12 cm, faster and faster) and let go at 0.36 s (~0.8 m/s, tumbling about
	// the gun's Y); the fresh one swings in from the front-left on an arc, tilted
	// 25 deg and straightening, stops short of the well (0.74 s), is drawn back a
	// hair (0.79 s) and driven home - seated hard at 0.86 s, 2.5% rebound.
	Pistol.MagMesh = UspMag.Object;
	Pistol.MagTravelCm = FVector(0.275f, 0.0f, -0.962f) * 12.0f;
	Pistol.MagEjectSeconds = 0.36f;
	Pistol.MagInSeconds = 0.62f;
	Pistol.MagTravel = JamMakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.24f, -0.02f, 0.0f, 0.0f }, { 0.36f, 1.0f, 7.0f, 7.0f },
		{ 0.62f, 1.6f, -5.0f, -5.0f }, { 0.74f, 0.3f, -3.0f, 0.0f }, { 0.79f, 0.36f, 0.0f, 0.0f }, { 0.86f, 0.0f, -14.0f, 2.0f },
		{ 0.875f, 0.025f, 0.0f, 0.0f }, { 0.9f, 0.0f, 0.0f, 0.0f } });
	Pistol.MagArcCm = FVector(-2.0f, 4.0f, 0.0f);
	Pistol.MagArc = JamMakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.36f, 0.15f, 1.5f, 1.5f }, { 0.62f, 1.0f, -4.0f, -4.0f },
		{ 0.74f, 0.3f, -3.0f, -1.0f }, { 0.79f, 0.06f, 0.0f, 0.0f }, { 0.86f, 0.0f, 0.0f, 0.0f } });
	Pistol.MagTurnDeg = 25.0f;
	Pistol.MagTurn = JamMakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.36f, 0.4f, 4.0f, 4.0f }, { 0.62f, 1.0f, -3.0f, -3.0f },
		{ 0.74f, 0.4f, -3.0f, -2.0f }, { 0.79f, 0.08f, -1.0f, 0.0f }, { 0.83f, 0.0f, 0.0f, 0.0f } });
	Pistol.MagEjectSpinDeg = 320.0f;
	Pistol.MagInSoundSeconds = 0.86f;
	// Muzzle up and canted, raised toward the middle: the well faces the eye. A
	// target the pose springs pull to (in at once, out 0.03 s before the end).
	Pistol.ReloadBodyTurn = FRotator(40.0f, -15.0f, 30.0f);
	Pistol.ReloadBodyOffsetCm = FVector(-3.0f, -6.0f, 11.0f);
	Pistol.ReloadBody = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.03f, 1.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.0f, 0.0f } });
	// The gun's mass on springs: brisk places, turns softer and later (arcs), floating ~15% past.
	Pistol.PoseSprings = { { 5.5f, 0.55f, 0.0f }, { 5.0f, 0.55f, 0.0f }, { 5.0f, 0.5f, 0.0f },
		{ 4.0f, 0.55f, 0.03f }, { 4.0f, 0.55f, 0.03f }, { 3.5f, 0.55f, 0.05f } };
	// The catch pressed, the magazine flicked off (the gun jumps up), the fresh one slapped home (up, muzzle down, a twist).
	Pistol.ReloadKicks = { { 0.24f, FVector(0.0f, 0.0f, -22.0f), FRotator::ZeroRotator },
		{ 0.36f, FVector(0.0f, 0.0f, 70.0f), FRotator(110.0f, 0.0f, 0.0f) },
		{ 0.86f, FVector(0.0f, 0.0f, 120.0f), FRotator(-180.0f, 0.0f, 60.0f) } };

	// --- pump gun ---
	FJamWeapon& Shotgun = Weapons[1];
	// The spent hull flies out as the fore-end hits the back stop.
	Shotgun.CaseMesh = Shell12.Object;
	Shotgun.EjectPort = FVector(1.6f, 1.6f, 5.2f);
	Shotgun.EjectSeconds = 0.26f;
	Shotgun.EjectVelocity = FVector(-30.0f, 200.0f, 110.0f);
	// Round by round into the loading port under the receiver, 0.42 s a round: each
	// rises from below on an arc swinging in toward the eye, nose up 45 deg, slowing
	// to the port (0.17 s); drawn back a hair and tipped back (0.22 s); then thumbed
	// in, turning level as it goes and accelerating into the tube - seated at 0.3 s,
	// a hard stop, 1.5% rebound, the tilt overshooting 10% past level.
	Shotgun.ShellMesh = Shell12.Object;
	Shotgun.ShellFrom = FVector(1.6f, 0.0f, -11.5f);
	Shotgun.ShellTo = FVector(11.6f, 0.0f, 2.2f);
	Shotgun.ShellTiltDeg = -35.0f;
	Shotgun.ShellInsert = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 5.0f }, { 0.17f, 0.66f, 2.0f, 0.0f }, { 0.22f, 0.62f, 0.0f, 0.0f },
		{ 0.3f, 1.0f, 10.0f, -1.0f }, { 0.315f, 0.985f, 0.0f, 0.0f }, { 0.33f, 1.0f, 0.0f, 0.0f } });
	Shotgun.ShellArcCm = FVector(-1.0f, -3.0f, 0.0f);
	Shotgun.ShellArc = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 10.0f }, { 0.08f, 1.0f, 0.0f, 0.0f }, { 0.17f, 0.25f, -5.0f, -2.0f },
		{ 0.22f, 0.05f, 0.0f, 0.0f }, { 0.3f, 0.0f, 0.0f, 0.0f } });
	Shotgun.ShellTurn = JamMakeCycleCurve({ { 0.0f, 1.3f, 0.0f, 0.0f }, { 0.17f, 1.0f, -1.0f, 0.0f }, { 0.22f, 1.08f, 0.0f, 0.0f },
		{ 0.28f, 0.0f, -22.0f, -4.0f }, { 0.3f, -0.1f, 0.0f, 0.0f }, { 0.33f, 0.0f, 0.0f, 0.0f } });
	Shotgun.ShellSeatSeconds = 0.3f;
	Shotgun.ReloadBodyTurn = FRotator(10.0f, -15.0f, 30.0f);
	Shotgun.ReloadBodyOffsetCm = FVector(-6.0f, -4.0f, 3.0f);
	// The last key is the lead before the first round (0.25 s; the sounds are cut to it).
	Shotgun.ReloadBody = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.03f, 1.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.0f, 0.0f } });
	Shotgun.PoseSprings = { { 7.0f, 0.5f, 0.0f }, { 6.0f, 0.55f, 0.0f }, { 6.0f, 0.5f, 0.01f },
		{ 5.0f, 0.5f, 0.02f }, { 5.0f, 0.55f, 0.02f }, { 4.5f, 0.5f, 0.03f } };
	// The fore-end slammed back (the gun jerks toward the eye, muzzle up) and home (thrown forward, muzzle down).
	Shotgun.ShotKicks = { { 0.26f, FVector(-80.0f, 0.0f, 20.0f), FRotator(100.0f, 0.0f, 35.0f) },
		{ 0.4f, FVector(95.0f, 0.0f, -20.0f), FRotator(-130.0f, 0.0f, -50.0f) } };
	// Each round: drawn back a hair, then pushed home (the gun pushed forward and up, a twist).
	Shotgun.RoundKicks = { { 0.22f, FVector(0.0f, 0.0f, -15.0f), FRotator::ZeroRotator },
		{ 0.3f, FVector(50.0f, 0.0f, 45.0f), FRotator(-90.0f, 0.0f, 40.0f) } };

	// --- SV-98 ---
	FJamWeapon& Sniper = Weapons[2];
	// The case is thrown out as the bolt hits its back stop.
	Sniper.CaseMesh = Case762.Object;
	Sniper.EjectPort = FVector(6.0f, 1.6f, 5.0f);
	Sniper.EjectSeconds = 0.63f;
	Sniper.EjectVelocity = FVector(-40.0f, 200.0f, 110.0f);
	// The box magazine: the catch pressed (nudged up), flicked 9 cm out of the stock and
	// let go at 0.5 s (~0.6 m/s, tumbling); the fresh one swings in from the front on
	// an arc, rocked 18 deg front-first and straightening, stops short (1.1 s), is
	// drawn back a hair (1.17 s) and slapped home at 1.25 s, 2.5% rebound.
	Sniper.MagMesh = Sv98Mag.Object;
	Sniper.MagTravelCm = FVector(0.0f, 0.0f, -9.0f);
	Sniper.MagEjectSeconds = 0.5f;
	Sniper.MagInSeconds = 0.95f;
	Sniper.MagTravel = JamMakeCycleCurve({ { 0.3f, 0.0f, 0.0f, 0.0f }, { 0.34f, -0.03f, 0.0f, 0.0f }, { 0.5f, 1.0f, 7.0f, 7.0f },
		{ 0.95f, 1.7f, -5.0f, -5.0f }, { 1.1f, 0.3f, -3.0f, 0.0f }, { 1.17f, 0.37f, 0.0f, 0.0f }, { 1.25f, 0.0f, -14.0f, 2.0f },
		{ 1.265f, 0.025f, 0.0f, 0.0f }, { 1.3f, 0.0f, 0.0f, 0.0f } });
	Sniper.MagArcCm = FVector(5.0f, 3.0f, 0.0f);
	Sniper.MagArc = JamMakeCycleCurve({ { 0.3f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.12f, 1.0f, 1.0f }, { 0.95f, 1.0f, -3.0f, -3.0f },
		{ 1.1f, 0.3f, -3.0f, -1.0f }, { 1.17f, 0.06f, 0.0f, 0.0f }, { 1.25f, 0.0f, 0.0f, 0.0f } });
	Sniper.MagTurnDeg = 18.0f;
	Sniper.MagTurn = JamMakeCycleCurve({ { 0.3f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.35f, 3.0f, 3.0f }, { 0.95f, 1.0f, -3.0f, -3.0f },
		{ 1.1f, 0.45f, -3.0f, -2.0f }, { 1.17f, 0.1f, -1.0f, 0.0f }, { 1.22f, 0.0f, 0.0f, 0.0f } });
	Sniper.MagEjectSpinDeg = 220.0f;
	Sniper.MagInSoundSeconds = 1.25f;
	Sniper.ReloadBodyTurn = FRotator(30.0f, -12.0f, 30.0f);
	Sniper.ReloadBodyOffsetCm = FVector(-6.0f, -5.0f, 6.0f);
	Sniper.ReloadBody = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.03f, 1.0f, 0.0f, 0.0f }, { 0.25f, 1.0f, 0.0f, 0.0f } });
	// A heavier gun: slower, later turns.
	Sniper.PoseSprings = { { 4.5f, 0.55f, 0.0f }, { 4.0f, 0.55f, 0.0f }, { 4.0f, 0.5f, 0.01f },
		{ 3.2f, 0.55f, 0.04f }, { 3.2f, 0.55f, 0.04f }, { 2.8f, 0.55f, 0.06f } };
	// The bolt unlocked (a twist), slammed back (toward the eye, muzzle up), home (forward, muzzle down), locked (down, a twist).
	Sniper.ShotKicks = { { 0.35f, FVector::ZeroVector, FRotator(0.0f, 0.0f, -100.0f) },
		{ 0.63f, FVector(-60.0f, 0.0f, 0.0f), FRotator(70.0f, 0.0f, 0.0f) },
		{ 0.87f, FVector(75.0f, 0.0f, 0.0f), FRotator(-90.0f, 0.0f, 0.0f) },
		{ 1.09f, FVector(0.0f, 0.0f, -25.0f), FRotator(0.0f, 0.0f, 110.0f) } };
	Sniper.ReloadKicks = { { 0.34f, FVector(0.0f, 0.0f, -18.0f), FRotator::ZeroRotator },
		{ 0.5f, FVector(0.0f, 0.0f, 60.0f), FRotator(90.0f, 0.0f, 0.0f) },
		{ 1.25f, FVector(0.0f, 0.0f, 100.0f), FRotator(-130.0f, 0.0f, 50.0f) } };
}

void UJamWeaponsComponent::MakePartComponents(USceneComponent& Parent)
{
	const auto Make = [this, &Parent](const TCHAR* Name)
	{
		UStaticMeshComponent* Part = NewObject<UStaticMeshComponent>(GetOwner(), Name);
		Part->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Part->SetCastShadow(false);
		Part->SetOnlyOwnerSee(true);
		Part->SetupAttachment(&Parent);
		Part->RegisterComponent();
		return Part;
	};
	HammerInView = Make(TEXT("GunHammerInView"));
	MagInView = Make(TEXT("GunMagInView"));
	ShellInView = Make(TEXT("GunShellInView"));
}

float UJamWeaponsComponent::StartReloadTimeline(const FJamWeapon& Weapon)
{
	ShellsPlanned = Weapon.ShellMesh ? FMath::Min(Weapon.MagazineSize - Weapon.Ammo, Weapon.Reserve) : 0;
	const float Seconds = ReloadDuration(Weapon, ShellsPlanned);
	ReloadStartSeconds = GetWorld()->GetTimeSeconds();
	ReloadEndSeconds = ReloadStartSeconds + Seconds;
	return Seconds;
}

float UJamWeaponsComponent::GetReloadTime() const
{
	const float PoseAt = CVarReloadPoseAt.GetValueOnGameThread();
	if (PoseAt >= 0.0f)
	{
		return PoseAt;
	}
	return ReloadEndSeconds > 0.0 ? float(GetWorld()->GetTimeSeconds() - ReloadStartSeconds) : -1.0f;
}

void UJamWeaponsComponent::StepPose(const FJamWeapon& Weapon, FPoseState& State, float Since0, float Since1, float Reload0, float Reload1,
	float ReloadLength, int32 Shells, float DeltaTime) const
{
	// Where each channel is pulled at shot time S and reload time R (< 0: no reload).
	const auto Goal = [&Weapon, ReloadLength](int32 Channel, float S, float R)
	{
		const float Shot = JamEvalCycle(Weapon.ActionBody, S);
		// In from the reload's start, out into its end: the same curve both ways.
		const float Reload = R >= 0.0f ? FMath::Min(JamEvalCycle(Weapon.ReloadBody, R), JamEvalCycle(Weapon.ReloadBody, ReloadLength - R)) : 0.0f;
		if (Channel < 3)
		{
			return Weapon.ActionBodyOffsetCm[Channel] * Shot + Weapon.ReloadBodyOffsetCm[Channel] * Reload;
		}
		const FVector ShotTurn(Weapon.ActionBodyTurn.Pitch, Weapon.ActionBodyTurn.Yaw, Weapon.ActionBodyTurn.Roll);
		const FVector ReloadTurn(Weapon.ReloadBodyTurn.Pitch, Weapon.ReloadBodyTurn.Yaw, Weapon.ReloadBodyTurn.Roll);
		return ShotTurn[Channel - 3] * Shot + ReloadTurn[Channel - 3] * Reload;
	};
	const TArray<FJamAimSpring>& Springs = Weapon.PoseSprings;
	if (Springs.Num() < 6)
	{
		// No springs: the targets themselves.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			State.Place[Axis] = Goal(Axis, Since1, Reload1);
			State.Turn[Axis] = Goal(3 + Axis, Since1, Reload1);
		}
		return;
	}
	const auto Kick = [&State](const FJamPoseKick& Each)
	{
		State.PlaceVelocity += Each.Move;
		State.TurnVelocity += FVector(Each.Turn.Pitch, Each.Turn.Yaw, Each.Turn.Roll);
	};
	const float Lead = LastKeyTime(Weapon.ReloadBody);
	// x'' = w^2 (goal - x) - 2 zeta w x', semi-implicit Euler in steps of at most 4 ms (as UJamScopeComponent).
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / 0.004f), 1, 512);
	const float Dt = DeltaTime / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		const float A = float(Step) / Steps;
		const float B = float(Step + 1) / Steps;
		const float S0 = FMath::Lerp(Since0, Since1, A);
		const float S1 = FMath::Lerp(Since0, Since1, B);
		const float R0 = Reload1 >= 0.0f ? FMath::Lerp(Reload0, Reload1, A) : -1.0f;
		const float R1 = Reload1 >= 0.0f ? FMath::Lerp(Reload0, Reload1, B) : -1.0f;
		for (const FJamPoseKick& Each : Weapon.ShotKicks)
		{
			if (Crossed(S0, S1, Each.Time))
			{
				Kick(Each);
			}
		}
		if (R1 >= 0.0f)
		{
			for (const FJamPoseKick& Each : Weapon.ReloadKicks)
			{
				if (Crossed(R0, R1, Each.Time))
				{
					Kick(Each);
				}
			}
			for (int32 Round = 0; Round < Shells; ++Round)
			{
				for (const FJamPoseKick& Each : Weapon.RoundKicks)
				{
					if (Crossed(R0, R1, Lead + Round * Weapon.ReloadSeconds + Each.Time))
					{
						Kick(Each);
					}
				}
			}
		}
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FJamAimSpring& Spring = Springs[Axis];
			const float W = 2.0f * PI * Spring.Hz;
			const float To = Goal(Axis, S1 - Spring.Delay, R1 >= 0.0f ? R1 - Spring.Delay : -1.0f);
			State.PlaceVelocity[Axis] += (W * W * (To - State.Place[Axis]) - 2.0f * Spring.Damping * W * State.PlaceVelocity[Axis]) * Dt;
			State.Place[Axis] += State.PlaceVelocity[Axis] * Dt;
		}
		// The turns trail the motion: the muzzle dips while the gun rises, points back across, cants into it.
		const FVector Drag(-PoseTurnLag.X * State.PlaceVelocity.Z, -PoseTurnLag.Y * State.PlaceVelocity.Y, PoseTurnLag.Z * State.PlaceVelocity.Y);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FJamAimSpring& Spring = Springs[3 + Axis];
			const float W = 2.0f * PI * Spring.Hz;
			const float To = Goal(3 + Axis, S1 - Spring.Delay, R1 >= 0.0f ? R1 - Spring.Delay : -1.0f) + Drag[Axis];
			State.TurnVelocity[Axis] += (W * W * (To - State.Turn[Axis]) - 2.0f * Spring.Damping * W * State.TurnVelocity[Axis]) * Dt;
			State.Turn[Axis] += State.TurnVelocity[Axis] * Dt;
		}
	}
}

void UJamWeaponsComponent::UpdatePose(const FJamWeapon& Weapon, float DeltaTime)
{
	static IConsoleVariable* ShotPoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.PoseAt"));
	if (PoseWeapon != CurrentWeapon)
	{
		Pose = FPoseState();
		PoseWeapon = CurrentWeapon;
		PosePrevSince = SinceShotSeconds;
		PosePrevReload = -1.0f;
	}
	// Held poses for stills: from rest up to the held moment, the same every frame.
	const float ShotAt = ShotPoseAt ? ShotPoseAt->GetFloat() : -1.0f;
	const float ReloadAt = CVarReloadPoseAt.GetValueOnGameThread();
	if (ShotAt >= 0.0f || ReloadAt >= 0.0f)
	{
		Pose = FPoseState();
		if (ShotAt >= 0.0f)
		{
			StepPose(Weapon, Pose, 0.0f, ShotAt, -1.0f, -1.0f, 0.0f, 0, ShotAt);
		}
		else
		{
			StepPose(Weapon, Pose, 1000.0f, 1000.0f, 0.0f, ReloadAt, ReloadDuration(Weapon, PoseShells), PoseShells, ReloadAt);
		}
		PosePrevSince = SinceShotSeconds;
		PosePrevReload = -1.0f;
		return;
	}
	// Live: a new shot or reload starts its timeline at 0; the springs carry on from where they are.
	const float Since = SinceShotSeconds;
	if (Since < PosePrevSince)
	{
		PosePrevSince = 0.0f;
	}
	const float Reload = ReloadEndSeconds > 0.0 ? float(GetWorld()->GetTimeSeconds() - ReloadStartSeconds) : -1.0f;
	if (Reload >= 0.0f && (PosePrevReload < 0.0f || Reload < PosePrevReload))
	{
		PosePrevReload = 0.0f;
	}
	StepPose(Weapon, Pose, PosePrevSince, Since, PosePrevReload, Reload, float(ReloadEndSeconds - ReloadStartSeconds), ShellsPlanned, DeltaTime);
	PosePrevSince = Since;
	PosePrevReload = Reload;
}

void UJamWeaponsComponent::TracePose(int32 Weapon, bool bReload, int32 Shells, float Seconds, float Step,
	TArray<FVector>& OutPlace, TArray<FVector>& OutTurn) const
{
	if (!Weapons.IsValidIndex(Weapon) || Step <= 0.0f)
	{
		return;
	}
	const FJamWeapon& Gun = Weapons[Weapon];
	FPoseState State;
	const float Length = ReloadDuration(Gun, Shells);
	for (float T = 0.0f; T <= Seconds + KINDA_SMALL_NUMBER; T += Step)
	{
		OutPlace.Add(State.Place);
		OutTurn.Add(State.Turn);
		if (bReload)
		{
			StepPose(Gun, State, 1000.0f, 1000.0f, T, T + Step, Length, Shells, Step);
		}
		else
		{
			StepPose(Gun, State, T, T + Step, -1.0f, -1.0f, 0.0f, 0, Step);
		}
	}
}

void UJamWeaponsComponent::UpdateParts(const FJamWeapon* Weapon, float SinceShot)
{
	if (!HammerInView)
	{
		return;
	}
	if (!Weapon)
	{
		SetPart(HammerInView, nullptr, false);
		SetPart(MagInView, nullptr, false);
		SetPart(ShellInView, nullptr, false);
		return;
	}
	SetPart(HammerInView, Weapon->HammerMesh, true);
	HammerInView->SetRelativeLocationAndRotation(Weapon->HammerPivot,
		FQuat(FVector::RightVector, FMath::DegreesToRadians(Weapon->HammerTurnDeg * JamEvalCycle(Weapon->HammerTurn, SinceShot))));

	const float Time = GetReloadTime();
	const bool bMagGone = Time >= Weapon->MagEjectSeconds && Time < Weapon->MagInSeconds;
	SetPart(MagInView, Weapon->MagMesh, !bMagGone);
	MagInView->SetRelativeLocationAndRotation(Time >= 0.0f
		? Weapon->MagTravelCm * JamEvalCycle(Weapon->MagTravel, Time) + Weapon->MagArcCm * JamEvalCycle(Weapon->MagArc, Time) : FVector::ZeroVector,
		FQuat(FVector::RightVector, FMath::DegreesToRadians(Time >= 0.0f ? Weapon->MagTurnDeg * JamEvalCycle(Weapon->MagTurn, Time) : 0.0f)));

	// The round being loaded: which one and how far along it is.
	bool bShell = false;
	if (Weapon->ShellMesh && Time >= 0.0f && Weapon->ReloadSeconds > 0.0f)
	{
		const float Local = Time - LastKeyTime(Weapon->ReloadBody);
		const int32 Planned = CVarReloadPoseAt.GetValueOnGameThread() >= 0.0f ? PoseShells : ShellsPlanned;
		const int32 Round = FMath::FloorToInt(Local / Weapon->ReloadSeconds);
		const float InRound = Local - Round * Weapon->ReloadSeconds;
		// Shown to just past the seat, for the rebound; then it is in the tube.
		if (Local >= 0.0f && Round < Planned && InRound <= Weapon->ShellSeatSeconds + 0.04f)
		{
			const float Along = JamEvalCycle(Weapon->ShellInsert, InRound);
			ShellInView->SetRelativeLocationAndRotation(
				FMath::Lerp(Weapon->ShellFrom, Weapon->ShellTo, Along) + Weapon->ShellArcCm * JamEvalCycle(Weapon->ShellArc, InRound),
				FQuat(FVector::RightVector, FMath::DegreesToRadians(Weapon->ShellTiltDeg * JamEvalCycle(Weapon->ShellTurn, InRound))));
			bShell = true;
		}
	}
	SetPart(ShellInView, Weapon->ShellMesh, bShell);
}

void UJamWeaponsComponent::UpdatePartEvents(float DeltaTime)
{
	// Loose bodies: the oldest beyond the cap, and any drifted out of range, go.
	const FVector Here = GetOwner()->GetActorLocation();
	Debris.RemoveAll([](const TWeakObjectPtr<AActor>& Each) { return !Each.IsValid(); });
	for (int32 Index = Debris.Num() - 1; Index >= 0; --Index)
	{
		if (Index < Debris.Num() - MaxDebris || FVector::DistSquared(Debris[Index]->GetActorLocation(), Here) > FMath::Square(DebrisRangeCm))
		{
			Debris[Index]->Destroy();
			Debris.RemoveAt(Index);
		}
	}
	if (!IsWeaponUnlocked(CurrentWeapon) || !ViewModel || !ViewModel->GetAttachParent())
	{
		return;
	}
	FJamWeapon& Weapon = Weapons[CurrentWeapon];
	const FTransform Gun = ViewModel->GetComponentTransform();
	const FTransform View = ViewModel->GetAttachParent()->GetComponentTransform();
	const auto Play = [this](USoundBase* Sound)
	{
		if (Sound)
		{
			UGameplayStatics::PlaySound2D(this, Sound);
			UE_LOG(LogMindKore, Log, TEXT("Weapon %s: sound %s"), *GetNameSafe(GetOwner()), *Sound->GetName());
		}
	};

	// After the shot (live time: a held pose throws nothing).
	const float Since = SinceShotSeconds;
	if (Since < PrevSinceShot)
	{
		PrevSinceShot = -KINDA_SMALL_NUMBER; // a new shot
	}
	if (Weapon.CaseMesh && Crossed(PrevSinceShot, Since, Weapon.EjectSeconds))
	{
		// Out of the port to the right and up, end over end about the line of sight.
		const FVector Velocity = GetOwner()->GetVelocity() + View.TransformVectorNoScale(Weapon.EjectVelocity * FMath::FRandRange(0.85f, 1.15f)
			+ FVector(FMath::FRandRange(-20.0f, 20.0f), 0.0f, FMath::FRandRange(-25.0f, 25.0f)));
		const FVector Spin = View.GetUnitAxis(EAxis::X) * FMath::FRandRange(900.0f, 1600.0f) * (FMath::RandBool() ? 1.0f : -1.0f)
			+ FMath::VRand() * 300.0f;
		const FRotator Facing = FRotationMatrix::MakeFromXZ(View.GetUnitAxis(EAxis::Y), View.GetUnitAxis(EAxis::Z)).Rotator()
			+ FRotator(FMath::FRandRange(-15.0f, 15.0f), FMath::FRandRange(-20.0f, 20.0f), 0.0f);
		SpawnDebris(Weapon.CaseMesh, FTransform(Facing, Gun.TransformPosition(Weapon.EjectPort)), Velocity, Spin, 0.012f);
	}
	if (Crossed(PrevSinceShot, Since, Weapon.HammerSoundSeconds))
	{
		Play(Weapon.HammerSound);
	}
	PrevSinceShot = Since;

	// Into the reload (live time).
	const float Time = ReloadEndSeconds > 0.0 ? float(GetWorld()->GetTimeSeconds() - ReloadStartSeconds) : -1.0f;
	if (Time < 0.0f)
	{
		PrevReloadTime = -1.0f;
		return;
	}
	if (Time < PrevReloadTime)
	{
		PrevReloadTime = -KINDA_SMALL_NUMBER;
	}
	if (Weapon.MagMesh && Crossed(PrevReloadTime, Time, Weapon.MagEjectSeconds))
	{
		// Let go where it is drawn, moving the way the curve moved it, turning a little.
		const float Step = 0.01f;
		const float Speed = (JamEvalCycle(Weapon.MagTravel, Weapon.MagEjectSeconds) - JamEvalCycle(Weapon.MagTravel, Weapon.MagEjectSeconds - Step)) / Step;
		const FVector Velocity = GetOwner()->GetVelocity() + Gun.TransformVectorNoScale(Weapon.MagTravelCm) * Speed;
		// The flick: tumbling about the gun's Y, plus a little wobble.
		const FVector Spin = Gun.GetUnitAxis(EAxis::Y) * Weapon.MagEjectSpinDeg * FMath::FRandRange(0.75f, 1.25f) + FMath::VRand() * 40.0f;
		SpawnDebris(Weapon.MagMesh, MagInView->GetComponentTransform(), Velocity, Spin, 0.25f);
		Play(Weapon.MagOutSound);
	}
	if (Crossed(PrevReloadTime, Time, Weapon.MagInSoundSeconds))
	{
		Play(Weapon.MagInSound);
	}
	if (Weapon.ShellMesh)
	{
		const float Lead = LastKeyTime(Weapon.ReloadBody);
		for (int32 Round = 0; Round < ShellsPlanned; ++Round)
		{
			if (Crossed(PrevReloadTime, Time, Lead + Round * Weapon.ReloadSeconds + Weapon.ShellSeatSeconds)
				&& Weapon.Ammo < Weapon.MagazineSize && Weapon.Reserve > 0)
			{
				++Weapon.Ammo;
				--Weapon.Reserve;
				Play(Weapon.ShellInsertSound);
				UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s round in, %d/%d"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString(), Weapon.Ammo, Weapon.Reserve);
			}
		}
	}
	PrevReloadTime = Time;
}

void UJamWeaponsComponent::SpawnDebris(UStaticMesh* Mesh, const FTransform& Where, const FVector& Velocity, const FVector& SpinDegPerSec, float MassKg)
{
	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* Actor = GetWorld()->SpawnActor<AStaticMeshActor>(AStaticMeshActor::StaticClass(), Where, Params);
	if (!Actor)
	{
		return;
	}
	// A loose body: it drifts and turns in zero-G and knocks off what it meets, but
	// not off the shooter (the pawn's body) and not in the way of rounds or the camera.
	Actor->SetMobility(EComponentMobility::Movable);
	UStaticMeshComponent* Body = Actor->GetStaticMeshComponent();
	Body->SetStaticMesh(Mesh);
	Body->SetCollisionProfileName(UCollisionProfile::PhysicsActor_ProfileName);
	Body->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	Body->SetCollisionResponseToChannel(ECC_GameTraceChannel2, ECR_Ignore); // Shot
	Body->SetCastShadow(false);
	Body->BodyInstance.bUseCCD = true;
	Body->SetSimulatePhysics(true);
	Body->SetMassOverrideInKg(NAME_None, MassKg, true);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.0f);
	Body->SetPhysicsLinearVelocity(Velocity);
	Body->SetPhysicsAngularVelocityInDegrees(SpinDegPerSec);
	Debris.Add(Actor);
}

namespace
{
	/**
	 * jam.Weapons.Reloads: the reloads frame by frame and the loose bodies (HighResShot;
	 * needs a real RHI). For each gun: jam.Weapons.ReloadPoseAt holds the reload at
	 * points through it (JamReload_<gun>_<nn>_<ms>.png); then live: two shots with a
	 * still as the second case flies, a reload with stills of the dropped magazine
	 * drifting (the pump gun loads three and is fired out of the loading), each event
	 * logged with the seconds really gone by.
	 */
	void RunWeaponReloads(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		IConsoleVariable* PoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.ReloadPoseAt"));
		if (!Pawn || !PoseAt)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Weapon reloads: needs the AJamZeroGPawn"));
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
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		const auto Still = [Controller](const FString& Name)
		{
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=%s"), *Name));
		};
		const TCHAR* Names[3] = { TEXT("pistol"), TEXT("shotgun"), TEXT("sv98") };
		const TArray<float> Phases[3] = {
			{ 0.1f, 0.25f, 0.33f, 0.36f, 0.5f, 0.62f, 0.72f, 0.8f, 0.86f, 0.89f, 1.1f },
			{ 0.12f, 0.25f, 0.33f, 0.45f, 0.5f, 0.55f, 0.7f, 0.87f, 0.97f, 1.2f },
			{ 0.15f, 0.33f, 0.42f, 0.5f, 0.7f, 0.95f, 1.08f, 1.18f, 1.25f, 1.28f, 1.5f } };
		TSharedRef<double> T0 = MakeShared<double>(0.0);
		float At = 8.0f; // shaders may still compile
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			const FString Name = Names[Gun];
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			At += 1.5f;
			for (int32 Index = 0; Index < Phases[Gun].Num(); ++Index)
			{
				const float T = Phases[Gun][Index];
				After(At, [Hold, PoseAt, T]() { Hold(); PoseAt->Set(T, ECVF_SetByCode); });
				After(At + 0.2f, [Pawn, Still, Name, Index, T]()
				{
					const UJamWeaponsComponent* Guns = Pawn->Guns;
					UE_LOG(LogMindKore, Log, TEXT("Weapon reloads: %s %02d at %.3f s - mag %s %s, shell %s %s"), *Name, Index, T,
						Guns->MagInView->IsVisible() ? TEXT("shown") : TEXT("hidden"), *Guns->MagInView->GetRelativeLocation().ToCompactString(),
						Guns->ShellInView->IsVisible() ? TEXT("shown") : TEXT("hidden"), *Guns->ShellInView->GetRelativeLocation().ToCompactString());
					Still(FString::Printf(TEXT("JamReload_%s_%02d_%04d"), *Name, Index, FMath::RoundToInt(T * 1000.0f)));
				});
				At += 0.5f;
			}
			// Live: two shots, the second case caught in flight.
			const float Eject = Pawn->Guns->Weapons[Gun].EjectSeconds;
			const float Interval = FMath::Max(Pawn->Guns->Weapons[Gun].FireInterval, Pawn->Guns->Weapons[Gun].GetActionCycleSeconds()) + 0.05f;
			After(At, [Pawn, Hold, PoseAt]() { PoseAt->Set(-1.0f, ECVF_SetByCode); Hold(); Pawn->Fire(); });
			After(At + Interval, [Pawn, Hold, T0]() { Hold(); Pawn->Fire(); *T0 = Pawn->GetWorld()->GetTimeSeconds(); });
			After(At + Interval + Eject + 0.06f, [Pawn, Still, Name, T0]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Weapon reloads: %s case out, %.3f s after the shot, %d loose bodies"), *Name,
					Pawn->GetWorld()->GetTimeSeconds() - *T0, Pawn->Guns->GetDebrisCount());
				Still(FString::Printf(TEXT("JamReload_%s_live_case"), *Name));
			});
			At += Interval + 1.5f;
			// Live reload; the pump gun keeps three rounds out to load and is fired out of it.
			After(At, [Pawn, Hold, Gun, T0]()
			{
				Hold();
				FJamWeapon& Weapon = Pawn->Guns->Weapons[Gun];
				Weapon.Ammo = Weapon.ShellMesh ? Weapon.MagazineSize - 3 : 1;
				Pawn->Guns->Reload();
				*T0 = Pawn->GetWorld()->GetTimeSeconds();
			});
			const float Drop = Pawn->Guns->Weapons[Gun].MagEjectSeconds;
			for (const float Late : { 0.25f, 0.8f })
			{
				After(At + Drop + Late, [Pawn, Still, Name, T0, Late]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Weapon reloads: %s live +%.3f s into the reload - %s, %d loose bodies"), *Name,
						Pawn->GetWorld()->GetTimeSeconds() - *T0, *Pawn->Guns->Describe(), Pawn->Guns->GetDebrisCount());
					Still(FString::Printf(TEXT("JamReload_%s_live_%04d"), *Name, FMath::RoundToInt(Late * 1000.0f)));
				});
			}
			if (Gun == 1)
			{
				// Two rounds in (0.25 + 0.3, + 0.42), then a shot breaks the loading off.
				After(At + 1.05f, [Pawn, Hold, T0]()
				{
					Hold();
					const FString Before = Pawn->Guns->Describe();
					const bool bFired = Pawn->Fire();
					UE_LOG(LogMindKore, Log, TEXT("Weapon reloads: shotgun fired out of the loading %.3f s in: %s -> %s, %s"),
						Pawn->GetWorld()->GetTimeSeconds() - *T0, *Before, *Pawn->Guns->Describe(), bFired ? TEXT("fired") : TEXT("refused"));
				});
			}
			At += 3.5f;
		}
		After(At, [Hold]() { Hold(); UE_LOG(LogMindKore, Log, TEXT("Weapon reloads: done")); });
	}

	/**
	 * jam.Weapons.PoseTrace: the pose springs and the part curves of every gun from rest,
	 * every 10 ms through a shot's cycle and a reload (two rounds for the pump gun), as
	 * log lines "Pose trace: <gun> <cycle|reload> t=.. place=(x,y,z) turn=(p,y,r) part=(a,b,c)"
	 * (part: cycle - travel, turn, hammer; reload - magazine travel, arc, turn or round along, arc, turn).
	 */
	void RunPoseTrace(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Pose trace: needs the AJamZeroGPawn"));
			return;
		}
		const UJamWeaponsComponent* Guns = Pawn->Guns;
		const TCHAR* Names[3] = { TEXT("pistol"), TEXT("shotgun"), TEXT("sv98") };
		for (int32 Gun = 0; Gun < FMath::Min(3, Guns->Weapons.Num()); ++Gun)
		{
			const FJamWeapon& Weapon = Guns->Weapons[Gun];
			for (const bool bReload : { false, true })
			{
				const int32 Shells = Weapon.ShellMesh ? PoseShells : 0;
				const float Seconds = (bReload ? ReloadDuration(Weapon, Shells) : Weapon.GetActionCycleSeconds()) + 0.4f;
				TArray<FVector> Place, Turn;
				Guns->TracePose(Gun, bReload, Shells, Seconds, 0.01f, Place, Turn);
				for (int32 Index = 0; Index < Place.Num(); ++Index)
				{
					const float T = Index * 0.01f;
					FVector Part;
					if (!bReload)
					{
						Part = FVector(JamEvalCycle(Weapon.ActionTravel, T), JamEvalCycle(Weapon.ActionTurn, T), JamEvalCycle(Weapon.HammerTurn, T));
					}
					else if (Weapon.ShellMesh)
					{
						const float Local = T - LastKeyTime(Weapon.ReloadBody);
						const float InRound = Local - FMath::FloorToInt(Local / Weapon.ReloadSeconds) * Weapon.ReloadSeconds;
						Part = Local < 0.0f ? FVector::ZeroVector : FVector(JamEvalCycle(Weapon.ShellInsert, InRound),
							JamEvalCycle(Weapon.ShellArc, InRound), JamEvalCycle(Weapon.ShellTurn, InRound));
					}
					else
					{
						Part = FVector(JamEvalCycle(Weapon.MagTravel, T), JamEvalCycle(Weapon.MagArc, T), JamEvalCycle(Weapon.MagTurn, T));
					}
					UE_LOG(LogMindKore, Log, TEXT("Pose trace: %s %s t=%.2f place=(%.3f,%.3f,%.3f) turn=(%.3f,%.3f,%.3f) part=(%.4f,%.4f,%.4f)"),
						Names[Gun], bReload ? TEXT("reload") : TEXT("cycle"), T, Place[Index].X, Place[Index].Y, Place[Index].Z,
						Turn[Index].X, Turn[Index].Y, Turn[Index].Z, Part.X, Part.Y, Part.Z);
				}
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Pose trace: done"));
	}

	FAutoConsoleCommandWithWorld GPoseTrace(
		TEXT("jam.Weapons.PoseTrace"),
		TEXT("Log the guns' pose springs and part curves every 10 ms through a cycle and a reload."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunPoseTrace));

	/**
	 * jam.Weapons.Frames: every gun's cycle and reload as stills every 33 ms (HighResShot;
	 * needs a real RHI), held with jam.Weapons.PoseAt / ReloadPoseAt - the springs re-run
	 * from rest to each moment, so the frames are the motion at 30 fps.
	 * JamFrames_<gun>_<cycle|reload>_<ms>.png.
	 */
	void RunWeaponFrames(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		IConsoleVariable* ShotAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.PoseAt"));
		IConsoleVariable* ReloadAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.ReloadPoseAt"));
		if (!Pawn || !ShotAt || !ReloadAt)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Weapon frames: needs the AJamZeroGPawn"));
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
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		const TCHAR* Names[3] = { TEXT("pistol"), TEXT("shotgun"), TEXT("sv98") };
		float At = 8.0f; // shaders may still compile
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			At += 1.0f;
			const FJamWeapon& Weapon = Pawn->Guns->Weapons[Gun];
			for (const bool bReload : { false, true })
			{
				const float Seconds = bReload ? ReloadDuration(Weapon, Weapon.ShellMesh ? PoseShells : 0) + 0.3f : Weapon.GetActionCycleSeconds() + 0.2f;
				for (float T = 0.0f; T <= Seconds; T += 1.0f / 30.0f)
				{
					After(At, [Hold, ShotAt, ReloadAt, bReload, T]()
					{
						Hold();
						ShotAt->Set(bReload ? -1.0f : T, ECVF_SetByCode);
						ReloadAt->Set(bReload ? T : -1.0f, ECVF_SetByCode);
					});
					After(At + 0.12f, [Controller, Name = Names[Gun], bReload, T]()
					{
						Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamFrames_%s_%s_%04d"), Name,
							bReload ? TEXT("reload") : TEXT("cycle"), FMath::RoundToInt(T * 1000.0f)));
					});
					At += 0.25f;
				}
			}
			After(At, [ShotAt, ReloadAt]() { ShotAt->Set(-1.0f, ECVF_SetByCode); ReloadAt->Set(-1.0f, ECVF_SetByCode); });
			At += 0.5f;
		}
		After(At, [Hold]() { Hold(); UE_LOG(LogMindKore, Log, TEXT("Weapon frames: done")); });
	}

	FAutoConsoleCommandWithWorld GWeaponFrames(
		TEXT("jam.Weapons.Frames"),
		TEXT("Every gun's cycle and reload as stills every 33 ms (jam.Weapons.PoseAt / ReloadPoseAt; HighResShot, needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponFrames));

	FAutoConsoleCommandWithWorld GWeaponReloads(
		TEXT("jam.Weapons.Reloads"),
		TEXT("The reloads frame by frame (jam.Weapons.ReloadPoseAt), spent cases and dropped magazines live (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponReloads));
}
