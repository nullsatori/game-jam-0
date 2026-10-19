// The guns' smaller moving parts and what they throw into the world: the USP's
// hammer, spent cases, magazines out and in, the pump gun's round-by-round tube
// loading. Members of UJamWeaponsComponent (JamWeapons.h), kept apart from
// JamWeapons.cpp so the parts and the shooting change in separate files.

#include "JamWeapons.h"

#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/CollisionProfile.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "JamHullTools.h"
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

	/** jam.Weapons.StowPoseAt / DrawPoseAt: the gun held this many seconds into its put-away / its draw (stills); < 0 off. */
	TAutoConsoleVariable<float> CVarStowPoseAt(TEXT("jam.Weapons.StowPoseAt"), -1.0f,
		TEXT("Hold the gun in view this many seconds into its put-away; < 0: live."));
	TAutoConsoleVariable<float> CVarDrawPoseAt(TEXT("jam.Weapons.DrawPoseAt"), -1.0f,
		TEXT("Hold the gun in view this many seconds into its draw; < 0: live."));

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
	// Holster and sling Foley from recorded CC0 donors, heard through the suit (Scripts/jam_switch_foley.py).
	static ConstructorHelpers::FObjectFinder<USoundBase> StowPistol(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Stow_Pistol.SW_Jam_Stow_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> DrawPistol(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Draw_Pistol.SW_Jam_Draw_Pistol"));
	static ConstructorHelpers::FObjectFinder<USoundBase> StowShotgun(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Stow_Shotgun.SW_Jam_Stow_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> DrawShotgun(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Draw_Shotgun.SW_Jam_Draw_Shotgun"));
	static ConstructorHelpers::FObjectFinder<USoundBase> StowSV98(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Stow_SV98.SW_Jam_Stow_SV98"));
	static ConstructorHelpers::FObjectFinder<USoundBase> DrawSV98(TEXT("/Game/JamArt/Audio/Weapons/SW_Jam_Draw_SV98.SW_Jam_Draw_SV98"));

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
	// 25 deg and straightening by the time it reaches the mouth of the well (0.71 s),
	// hangs there as if a hand were finding it (to 0.77 s), winds up a hair (0.80 s)
	// and is driven the whole way home along the well - seated hard at 0.86 s with a
	// double knock (2.5%, then 0.8%). Inside the well it only slides (MagClearCm).
	Pistol.MagMesh = UspMag.Object;
	Pistol.MagTravelCm = FVector(0.275f, 0.0f, -0.962f) * 12.0f;
	Pistol.MagEjectSeconds = 0.36f;
	Pistol.MagInSeconds = 0.62f;
	Pistol.MagTravel = JamMakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.24f, -0.02f, 0.0f, 0.0f }, { 0.36f, 1.0f, 7.0f, 7.0f },
		{ 0.62f, 1.6f, -5.0f, -5.0f }, { 0.71f, 0.82f, -3.0f, -0.3f }, { 0.77f, 0.8f, -0.3f, 0.0f },
		{ 0.8f, 0.83f, 0.0f, 0.0f }, { 0.86f, 0.0f, -22.0f, 2.0f }, { 0.875f, 0.025f, 0.0f, 0.0f },
		{ 0.888f, 0.0f, -1.0f, 1.0f }, { 0.897f, 0.008f, 0.0f, 0.0f }, { 0.92f, 0.0f, 0.0f, 0.0f } });
	Pistol.MagArcCm = FVector(-2.0f, 4.0f, 0.0f);
	Pistol.MagArc = JamMakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.36f, 0.15f, 1.5f, 1.5f }, { 0.62f, 1.0f, -4.0f, -4.0f },
		{ 0.71f, 0.05f, -2.0f, 0.0f }, { 0.75f, 0.0f, 0.0f, 0.0f } });
	Pistol.MagTurnDeg = 25.0f;
	// Its top on the well's axis; 9 cm long, so clear of the grip from 9.5 cm out.
	Pistol.MagPivot = FVector(-0.26f, -1.43f, 0.5f);
	Pistol.MagClearCm = 9.5f;
	Pistol.MagTurn = JamMakeCycleCurve({ { 0.2f, 0.0f, 0.0f, 0.0f }, { 0.36f, 0.4f, 4.0f, 4.0f }, { 0.62f, 1.0f, -3.0f, -3.0f },
		{ 0.71f, 0.06f, -2.0f, 0.0f }, { 0.75f, 0.0f, 0.0f, 0.0f } });
	Pistol.MagEjectSpinDeg = 320.0f;
	Pistol.MagInSoundSeconds = 0.86f;
	// Muzzle up and canted, raised toward the middle: the well faces the eye. Where
	// the pose springs pull, in stages: in; 8% further as the old magazine goes
	// (0.3 s); back 8% short to meet the new one (0.6 s); home on the seat (0.86 s),
	// then out at once under the magazine's double knock (owner 19.10: the tail after the
	// seat halved) - easing off at 0.88 s, fastest mid-way, level by 0.95 s; ready at
	// 0.97 s (ReloadSeconds), the springs floating it past rest and back.
	Pistol.ReloadBodyTurn = FRotator(32.0f, -12.0f, 30.0f);
	Pistol.ReloadBodyOffsetCm = FVector(-3.0f, -6.0f, 11.0f);
	Pistol.ReloadBody = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.03f, 1.0f, 0.0f, 0.0f }, { 0.28f, 1.0f, 0.0f, 0.0f },
		{ 0.32f, 1.08f, 0.0f, 0.0f }, { 0.58f, 1.08f, 0.0f, 0.0f }, { 0.62f, 0.92f, 0.0f, 0.0f },
		{ 0.84f, 0.92f, 0.0f, 0.0f }, { 0.88f, 1.0f, 0.0f, -2.0f }, { 0.95f, 0.0f, -1.0f, 0.0f } });
	// Snappy: brisk springs, turns a little softer and later (arcs), floating ~15% past.
	Pistol.PoseSprings = { { 6.5f, 0.55f, 0.0f }, { 6.0f, 0.55f, 0.0f }, { 6.0f, 0.5f, 0.01f },
		{ 5.0f, 0.55f, 0.02f }, { 5.0f, 0.55f, 0.03f }, { 4.5f, 0.55f, 0.04f } };
	// The catch pressed, the magazine flicked off (the gun jumps up, rocks), the gun meeting the
	// new one (down, a roll), the seat (up, muzzle down, a twist).
	Pistol.ReloadKicks = { { 0.24f, FVector(0.0f, 0.0f, -22.0f), FRotator::ZeroRotator },
		{ 0.36f, FVector(0.0f, 0.0f, 70.0f), FRotator(110.0f, 25.0f, -35.0f) },
		{ 0.71f, FVector(0.0f, 0.0f, -20.0f), FRotator(0.0f, 0.0f, -25.0f) },
		{ 0.86f, FVector(0.0f, 0.0f, 120.0f), FRotator(-180.0f, 30.0f, 60.0f) } };

	// Away to the holster on the right hip: a small lift first, then down and right, the
	// muzzle tipping down ahead of the grip, faster and faster into the holster (gone at
	// 0.32 s). The draw: a tug down out of it, then up fast - out of the holster at once,
	// slowing in - past the rest pose (the springs float it 15% past) with a snap; fires
	// once 0.3 s in and settled.
	Pistol.StowOffsetCm = FVector(-12.0f, 8.0f, -28.0f);
	Pistol.StowTurn = FRotator(-60.0f, 10.0f, 20.0f);
	Pistol.StowTurnLead = 0.04f;
	Pistol.StowPath = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.06f, -0.06f, 0.0f, 0.0f }, { 0.28f, 1.0f, 6.0f, 0.0f },
		{ 0.32f, 1.0f, 0.0f, 0.0f } });
	Pistol.DrawPath = JamMakeCycleCurve({ { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.04f, 1.03f, 0.0f, -3.0f }, { 0.22f, 0.0f, -5.0f, -1.5f },
		{ 0.38f, 0.0f, 0.0f, 0.0f } });
	Pistol.DrawReadySeconds = 0.3f;
	Pistol.DrawKicks = { { 0.22f, FVector(0.0f, 0.0f, 40.0f), FRotator(-60.0f, 0.0f, 20.0f) } };
	// The stows lead into their knock: the leather slide first, the seat at 0.3 s (0.14 + 0.16 into the wave).
	Pistol.StowSound = StowPistol.Object;
	Pistol.StowSoundSeconds = 0.14f;
	Pistol.DrawSound = DrawPistol.Object;
	Pistol.DrawSoundSeconds = 0.02f;

	// --- pump gun ---
	FJamWeapon& Shotgun = Weapons[1];
	// Slung over the right shoulder, muzzle up (owner 19.10): the muzzle swings up to vertical
	// as it goes, the gun comes to the frame's right third and rises up out of it. The draw is
	// the reverse: down into the right third, stock first, the muzzle tipping forward and down
	// about the grip all the way, fastest mid-way, into level - never toward the eye. The
	// shoulder takes it (the view nods); it lands with weight; fires once 0.45 s in and settled.
	Shotgun.StowOffsetCm = FVector(2.0f, 13.0f, 95.0f);
	Shotgun.StowMidCm = FVector(2.0f, 13.0f, -8.0f);
	Shotgun.StowMidShare = 0.5f;
	Shotgun.StowTurn = FRotator(86.0f, 0.0f, -10.0f);
	Shotgun.StowPath = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.06f, -0.04f, 0.0f, 0.0f }, { 0.22f, 0.5f, 3.0f, 3.0f },
		{ 0.42f, 1.0f, 3.0f, 0.0f }, { 0.5f, 1.0f, 0.0f, 0.0f } });
	Shotgun.StowTurnPath = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.22f, 0.6f, 3.5f, 3.5f }, { 0.42f, 1.0f, 1.0f, 0.0f },
		{ 0.5f, 1.0f, 0.0f, 0.0f } });
	Shotgun.DrawPath = JamMakeCycleCurve({ { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.04f, 1.02f, 0.0f, -3.0f }, { 0.24f, 0.5f, -2.5f, -2.5f },
		{ 0.45f, 0.0f, -2.5f, -0.5f }, { 0.6f, 0.0f, 0.0f, 0.0f } });
	Shotgun.DrawTurnPath = JamMakeCycleCurve({ { 0.0f, 1.0f, 0.0f, -1.2f }, { 0.25f, 0.45f, -3.2f, -3.2f }, { 0.5f, 0.0f, -0.6f, 0.0f },
		{ 0.6f, 0.0f, 0.0f, 0.0f } });
	Shotgun.DrawReadySeconds = 0.45f;
	Shotgun.DrawKicks = { { 0.45f, FVector(30.0f, 0.0f, -25.0f), FRotator(-50.0f, -20.0f, -30.0f) } };
	Shotgun.StowViewNudge = FRotator(0.8f, 0.4f, 0.0f);
	Shotgun.StowViewNudgeSeconds = 0.4f;
	Shotgun.DrawViewNudge = FRotator(-0.6f, 0.3f, 0.0f);
	Shotgun.DrawViewNudgeSeconds = 0.1f;
	// Webbing first, the stock on the back at 0.4 s (0.2 + 0.2); the draw's catch lands on the 0.45 s kick.
	Shotgun.StowSound = StowShotgun.Object;
	Shotgun.StowSoundSeconds = 0.2f;
	Shotgun.DrawSound = DrawShotgun.Object;
	Shotgun.DrawSoundSeconds = 0.05f;
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
	Shotgun.PoseSprings = { { 5.5f, 0.5f, 0.0f }, { 5.0f, 0.55f, 0.0f }, { 5.0f, 0.5f, 0.01f },
		{ 4.0f, 0.5f, 0.03f }, { 4.0f, 0.55f, 0.03f }, { 3.6f, 0.5f, 0.05f } };
	// The fore-end slammed back (the gun jerks toward the eye, muzzle up) and home (thrown forward, muzzle down).
	Shotgun.ShotKicks = { { 0.26f, FVector(-80.0f, 0.0f, 20.0f), FRotator(100.0f, 25.0f, 35.0f) },
		{ 0.4f, FVector(95.0f, 0.0f, -20.0f), FRotator(-130.0f, -30.0f, -50.0f) } };
	// Heavy and rhythmic: slower springs and three beats a round - the gun dips and rolls to
	// take it, sinks as it is drawn back a hair, is shoved forward and up as it goes home.
	Shotgun.RoundKicks = { { 0.02f, FVector(0.0f, -15.0f, -25.0f), FRotator(0.0f, 0.0f, 45.0f) },
		{ 0.22f, FVector(0.0f, 0.0f, -15.0f), FRotator::ZeroRotator },
		{ 0.3f, FVector(50.0f, 0.0f, 45.0f), FRotator(-90.0f, 20.0f, -50.0f) } };

	// --- SV-98 ---
	FJamWeapon& Sniper = Weapons[2];
	// Slung over the left shoulder, muzzle up (owner 19.10: «draw у снайперки все еще не под тем
	// углом»): the draw brings it down vertical - muzzle up, the bolt turned a little inward -
	// stock first toward the frame's left third, the muzzle tipping forward and down about the
	// grip from the first frame - one continuous turn, fastest mid-way (owner 19.10: «угол
	// менялся на протяжении анимации»), easing into level with the springs' overshoot (0.6 s)
	// - never toward the eye; the put-away is the reverse.
	// Heavier and slower than the shotgun; fires once 0.55 s in and settled.
	Sniper.StowOffsetCm = FVector(3.0f, -38.0f, 90.0f);
	Sniper.StowMidCm = FVector(3.0f, -38.0f, -4.0f);
	Sniper.StowMidShare = 0.5f;
	Sniper.StowTurn = FRotator(88.0f, 0.0f, 12.0f);
	Sniper.StowPath = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.08f, -0.04f, 0.0f, 0.0f }, { 0.25f, 0.5f, 3.0f, 3.0f },
		{ 0.5f, 1.0f, 3.0f, 0.0f }, { 0.6f, 1.0f, 0.0f, 0.0f } });
	Sniper.StowTurnPath = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 1.0f }, { 0.25f, 0.6f, 3.0f, 3.0f }, { 0.45f, 1.0f, 1.0f, 0.0f },
		{ 0.6f, 1.0f, 0.0f, 0.0f } });
	Sniper.DrawPath = JamMakeCycleCurve({ { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.05f, 1.02f, 0.0f, -3.0f }, { 0.3f, 0.5f, -2.2f, -2.2f },
		{ 0.6f, 0.0f, -1.5f, -0.3f }, { 0.8f, 0.0f, 0.0f, 0.0f } });
	Sniper.DrawTurnPath = JamMakeCycleCurve({ { 0.0f, 1.0f, 0.0f, -1.2f }, { 0.3f, 0.45f, -2.8f, -2.8f }, { 0.58f, 0.0f, -0.6f, 0.0f },
		{ 0.75f, 0.0f, 0.0f, 0.0f } });
	Sniper.DrawReadySeconds = 0.6f;
	Sniper.DrawKicks = { { 0.6f, FVector(25.0f, 0.0f, -20.0f), FRotator(-40.0f, 10.0f, 15.0f) } };
	Sniper.StowViewNudge = FRotator(1.0f, -0.5f, 0.0f);
	Sniper.StowViewNudgeSeconds = 0.45f;
	Sniper.DrawViewNudge = FRotator(-0.8f, -0.4f, 0.0f);
	Sniper.DrawViewNudgeSeconds = 0.12f;
	// The stock on the back at 0.45 s (0.2 + 0.25); the draw's catch lands on the 0.6 s kick.
	Sniper.StowSound = StowSV98.Object;
	Sniper.StowSoundSeconds = 0.2f;
	Sniper.DrawSound = DrawSV98.Object;
	Sniper.DrawSoundSeconds = 0.06f;
	// The case is thrown out as the bolt hits its back stop.
	Sniper.CaseMesh = Case762.Object;
	Sniper.EjectPort = FVector(6.0f, 1.6f, 5.0f);
	Sniper.EjectSeconds = 0.63f;
	Sniper.EjectVelocity = FVector(-40.0f, 200.0f, 110.0f);
	// The box magazine: the catch pressed (nudged up), flicked 9 cm out of the stock and
	// let go at 0.5 s (~0.6 m/s, tumbling); the fresh one swings in from the front on
	// an arc, rocked 18 deg front-first and straightening, hangs at the mouth of the
	// well (1.08-1.16 s), winds up (1.19 s) and is slapped home at 1.25 s, double knock.
	// The pose target in stages as the pistol's, 6% each way.
	Sniper.MagMesh = Sv98Mag.Object;
	Sniper.MagTravelCm = FVector(0.0f, 0.0f, -9.0f);
	Sniper.MagEjectSeconds = 0.5f;
	Sniper.MagInSeconds = 0.95f;
	Sniper.MagTravel = JamMakeCycleCurve({ { 0.3f, 0.0f, 0.0f, 0.0f }, { 0.34f, -0.03f, 0.0f, 0.0f }, { 0.5f, 1.0f, 7.0f, 7.0f },
		{ 0.95f, 1.7f, -5.0f, -5.0f }, { 1.08f, 0.22f, -3.0f, -0.3f }, { 1.16f, 0.2f, -0.3f, 0.0f },
		{ 1.19f, 0.26f, 0.0f, 0.0f }, { 1.25f, 0.0f, -12.0f, 2.0f }, { 1.265f, 0.025f, 0.0f, 0.0f },
		{ 1.278f, 0.0f, -1.0f, 1.0f }, { 1.287f, 0.008f, 0.0f, 0.0f }, { 1.31f, 0.0f, 0.0f, 0.0f } });
	Sniper.MagArcCm = FVector(5.0f, 3.0f, 0.0f);
	Sniper.MagArc = JamMakeCycleCurve({ { 0.3f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.12f, 1.0f, 1.0f }, { 0.95f, 1.0f, -3.0f, -3.0f },
		{ 1.08f, 0.05f, -2.0f, 0.0f }, { 1.12f, 0.0f, 0.0f, 0.0f } });
	Sniper.MagTurnDeg = 18.0f;
	Sniper.MagPivot = FVector(7.0f, 0.0f, 0.5f);
	Sniper.MagClearCm = 1.5f;
	Sniper.MagTurn = JamMakeCycleCurve({ { 0.3f, 0.0f, 0.0f, 0.0f }, { 0.5f, 0.35f, 3.0f, 3.0f }, { 0.95f, 1.0f, -3.0f, -3.0f },
		{ 1.08f, 0.06f, -2.0f, 0.0f }, { 1.12f, 0.0f, 0.0f, 0.0f } });
	Sniper.MagEjectSpinDeg = 220.0f;
	Sniper.MagInSoundSeconds = 1.25f;
	// Pointed down-range (owner 19.10: it hung broadside in the air): rolled 30 deg to present
	// the well, pitch 8 and yaw 6 only, drawn back 10 cm, in 6 and up 7 about the grip.
	Sniper.ReloadBodyTurn = FRotator(8.0f, -6.0f, 30.0f);
	Sniper.ReloadBodyOffsetCm = FVector(-10.0f, -6.0f, 7.0f);
	Sniper.ReloadBody = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.03f, 1.0f, 0.0f, 0.0f }, { 0.42f, 1.0f, 0.0f, 0.0f },
		{ 0.46f, 1.06f, 0.0f, 0.0f }, { 0.9f, 1.06f, 0.0f, 0.0f }, { 0.94f, 0.94f, 0.0f, 0.0f },
		{ 1.23f, 0.94f, 0.0f, 0.0f }, { 1.27f, 1.0f, 0.0f, 0.0f }, { 1.4f, 1.0f, 0.0f, 0.0f } });
	// Deliberate: a heavier gun, slower springs, later turns.
	Sniper.PoseSprings = { { 4.5f, 0.55f, 0.0f }, { 4.0f, 0.55f, 0.0f }, { 4.0f, 0.5f, 0.02f },
		{ 3.2f, 0.55f, 0.05f }, { 3.2f, 0.55f, 0.05f }, { 2.8f, 0.55f, 0.07f } };
	// The bolt unlocked (a twist), slammed back (toward the eye, muzzle up), home (forward, muzzle down), locked (down, a twist).
	Sniper.ShotKicks = { { 0.35f, FVector::ZeroVector, FRotator(0.0f, 10.0f, -100.0f) },
		{ 0.63f, FVector(-60.0f, 0.0f, 0.0f), FRotator(70.0f, -20.0f, 0.0f) },
		{ 0.87f, FVector(75.0f, 0.0f, 0.0f), FRotator(-90.0f, 20.0f, 0.0f) },
		{ 1.09f, FVector(0.0f, 0.0f, -25.0f), FRotator(0.0f, 0.0f, 110.0f) } };
	Sniper.ReloadKicks = { { 0.34f, FVector(0.0f, 0.0f, -18.0f), FRotator::ZeroRotator },
		{ 0.5f, FVector(0.0f, 0.0f, 60.0f), FRotator(90.0f, 20.0f, -30.0f) },
		{ 1.08f, FVector(0.0f, 0.0f, -18.0f), FRotator(0.0f, 0.0f, -20.0f) },
		{ 1.25f, FVector(0.0f, 0.0f, 100.0f), FRotator(-130.0f, 25.0f, 50.0f) } };
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
	float ReloadLength, int32 Shells, float DeltaTime, const FSwitchSpan& Switch) const
{
	// How far toward the put-away pose at switch time W (each channel its own W).
	const auto Away = [&Weapon, &Switch](float W)
	{
		switch (Switch.Stage)
		{
		case ESwitch::Stow: return JamEvalCycle(Weapon.StowPath, W);
		case ESwitch::Draw: return JamEvalCycle(Weapon.DrawPath, W);
		case ESwitch::Away: return 1.0f;
		default: return 0.0f;
		}
	};
	// Where each channel is pulled at shot time S and reload time R (< 0: no reload).
	// The turns' own paths, when the gun has them (a slung rifle: vertical first, levelled last).
	const bool bTurnPaths = Weapon.StowTurnPath.GetRichCurveConst()->GetNumKeys() > 0 && Weapon.DrawTurnPath.GetRichCurveConst()->GetNumKeys() > 0;
	const auto TurnAway = [&Weapon, &Switch, &Away, bTurnPaths](float W)
	{
		if (!bTurnPaths)
		{
			return Away(W + Weapon.StowTurnLead);
		}
		switch (Switch.Stage)
		{
		case ESwitch::Stow: return JamEvalCycle(Weapon.StowTurnPath, W);
		case ESwitch::Draw: return JamEvalCycle(Weapon.DrawTurnPath, W);
		case ESwitch::Away: return 1.0f;
		default: return 0.0f;
		}
	};
	// The place on the put-away path at share Off: straight, or bent through StowMidCm.
	const auto AwayPlace = [&Weapon](float Off)
	{
		const float Mid = Weapon.StowMidShare;
		if (Mid <= 0.0f)
		{
			return Weapon.StowOffsetCm * Off;
		}
		return Off <= Mid ? Weapon.StowMidCm * (Off / Mid) : FMath::Lerp(Weapon.StowMidCm, Weapon.StowOffsetCm, (Off - Mid) / (1.0f - Mid));
	};
	const auto Goal = [&Weapon, ReloadLength, &Away, &TurnAway, &AwayPlace](int32 Channel, float S, float R, float W)
	{
		const float Off = Channel < 3 ? Away(W) : TurnAway(W);
		const float Shot = JamEvalCycle(Weapon.ActionBody, S);
		// The reload's stages, stepping out over its last 30 ms.
		const float Reload = R >= 0.0f ? JamEvalCycle(Weapon.ReloadBody, R) * FMath::Clamp((ReloadLength - R) / 0.03f, 0.0f, 1.0f) : 0.0f;
		if (Channel < 3)
		{
			return Weapon.ActionBodyOffsetCm[Channel] * Shot + Weapon.ReloadBodyOffsetCm[Channel] * Reload + AwayPlace(Off)[Channel];
		}
		const FVector StowTurn(Weapon.StowTurn.Pitch, Weapon.StowTurn.Yaw, Weapon.StowTurn.Roll);
		const FVector ShotTurn(Weapon.ActionBodyTurn.Pitch, Weapon.ActionBodyTurn.Yaw, Weapon.ActionBodyTurn.Roll);
		const FVector ReloadTurn(Weapon.ReloadBodyTurn.Pitch, Weapon.ReloadBodyTurn.Yaw, Weapon.ReloadBodyTurn.Roll);
		return ShotTurn[Channel - 3] * Shot + ReloadTurn[Channel - 3] * Reload + StowTurn[Channel - 3] * Off;
	};
	const TArray<FJamAimSpring>& Springs = Weapon.PoseSprings;
	if (Springs.Num() < 6)
	{
		// No springs: the targets themselves.
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			State.Place[Axis] = Goal(Axis, Since1, Reload1, Switch.Time1);
			State.Turn[Axis] = Goal(3 + Axis, Since1, Reload1, Switch.Time1);
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
		const float W0 = FMath::Lerp(Switch.Time0, Switch.Time1, A);
		const float W1 = FMath::Lerp(Switch.Time0, Switch.Time1, B);
		if (Switch.Stage == ESwitch::Stow || Switch.Stage == ESwitch::Draw)
		{
			for (const FJamPoseKick& Each : Switch.Stage == ESwitch::Stow ? Weapon.StowKicks : Weapon.DrawKicks)
			{
				if (Crossed(W0, W1, Each.Time))
				{
					Kick(Each);
				}
			}
		}
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
			const float To = Goal(Axis, S1 - Spring.Delay, R1 >= 0.0f ? R1 - Spring.Delay : -1.0f, W1 - Spring.Delay);
			State.PlaceVelocity[Axis] += (W * W * (To - State.Place[Axis]) - 2.0f * Spring.Damping * W * State.PlaceVelocity[Axis]) * Dt;
			State.Place[Axis] += State.PlaceVelocity[Axis] * Dt;
		}
		// The turns trail the motion: the muzzle dips while the gun rises, points back across, cants into it.
		// (Not on a put-away or a draw: their turns are laid out on their own paths.)
		const float DragScale = Switch.Stage == ESwitch::Stow || Switch.Stage == ESwitch::Draw ? 0.0f : 1.0f;
		const FVector Drag = DragScale * FVector(-PoseTurnLag.X * State.PlaceVelocity.Z, -PoseTurnLag.Y * State.PlaceVelocity.Y,
			PoseTurnLag.Z * State.PlaceVelocity.Y);
		// The way back out of a reload swings less past rest in pitch and roll; elsewhere the float stays.
		if (R1 >= 0.0f)
		{
			State.bExit = R1 >= ReloadLength - 0.1f;
		}
		else if (State.bExit && State.Turn.GetAbsMax() < 0.5f && State.TurnVelocity.GetAbsMax() < 5.0f)
		{
			State.bExit = false;
		}
		// Own turn paths carry their own timing: their springs take them at once, or a draw
		// from rest would hold still for the delay (owner 19.10: «слишком долго вертикально»).
		const bool bOwnTurnTiming = bTurnPaths && (Switch.Stage == ESwitch::Stow || Switch.Stage == ESwitch::Draw);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FJamAimSpring& Spring = Springs[3 + Axis];
			const float W = 2.0f * PI * Spring.Hz;
			const float SwitchDelay = bOwnTurnTiming ? 0.0f : Spring.Delay;
			const float To = Goal(3 + Axis, S1 - Spring.Delay, R1 >= 0.0f ? R1 - Spring.Delay : -1.0f, W1 - SwitchDelay) + Drag[Axis];
			const float Damping = State.bExit && Axis != 1 ? FMath::Max(Spring.Damping, PoseExitTurnDamping) : Spring.Damping;
			State.TurnVelocity[Axis] += (W * W * (To - State.Turn[Axis]) - 2.0f * Damping * W * State.TurnVelocity[Axis]) * Dt;
			State.Turn[Axis] += State.TurnVelocity[Axis] * Dt;
		}
	}
}

void UJamWeaponsComponent::UpdatePose(const FJamWeapon& Weapon, float DeltaTime)
{
	static IConsoleVariable* ShotPoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.PoseAt"));
	if (PoseWeapon != ShownWeapon)
	{
		// A gun coming into view starts from its put-away pose (out of view), not from rest.
		Pose = FPoseState();
		if (PoseSwitch.Stage == ESwitch::Draw || PoseSwitch.Stage == ESwitch::Away)
		{
			Pose.Place = Weapon.StowOffsetCm;
			Pose.Turn = FVector(Weapon.StowTurn.Pitch, Weapon.StowTurn.Yaw, Weapon.StowTurn.Roll);
		}
		PoseWeapon = ShownWeapon;
		PosePrevSince = Weapon.SinceShot;
		PosePrevReload = -1.0f;
	}
	// Held poses for stills: from rest up to the held moment, the same every frame.
	const float ShotAt = ShotPoseAt ? ShotPoseAt->GetFloat() : -1.0f;
	const float ReloadAt = CVarReloadPoseAt.GetValueOnGameThread();
	const float StowAt = CVarStowPoseAt.GetValueOnGameThread();
	const float DrawAt = CVarDrawPoseAt.GetValueOnGameThread();
	if (StowAt >= 0.0f || DrawAt >= 0.0f)
	{
		Pose = FPoseState();
		FSwitchSpan Span;
		Span.Stage = StowAt >= 0.0f ? ESwitch::Stow : ESwitch::Draw;
		Span.Time1 = StowAt >= 0.0f ? StowAt : DrawAt;
		if (Span.Stage == ESwitch::Draw)
		{
			Pose.Place = Weapon.StowOffsetCm;
			Pose.Turn = FVector(Weapon.StowTurn.Pitch, Weapon.StowTurn.Yaw, Weapon.StowTurn.Roll);
		}
		StepPose(Weapon, Pose, 1000.0f, 1000.0f, -1.0f, -1.0f, 0.0f, 0, Span.Time1, Span);
		PosePrevSince = Weapon.SinceShot;
		PosePrevReload = -1.0f;
		return;
	}
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
		PosePrevSince = Weapon.SinceShot;
		PosePrevReload = -1.0f;
		return;
	}
	// Live: a new shot or reload starts its timeline at 0; the springs carry on from where they are.
	const float Since = Weapon.SinceShot;
	if (Since < PosePrevSince)
	{
		PosePrevSince = 0.0f;
	}
	const float Reload = ReloadEndSeconds > 0.0 ? float(GetWorld()->GetTimeSeconds() - ReloadStartSeconds) : -1.0f;
	if (Reload >= 0.0f && (PosePrevReload < 0.0f || Reload < PosePrevReload))
	{
		PosePrevReload = 0.0f;
	}
	StepPose(Weapon, Pose, PosePrevSince, Since, PosePrevReload, Reload, float(ReloadEndSeconds - ReloadStartSeconds), ShellsPlanned, DeltaTime,
		PoseSwitch);
	PosePrevSince = Since;
	PosePrevReload = Reload;
}

void UJamWeaponsComponent::TracePose(int32 Weapon, bool bReload, int32 Shells, float Seconds, float Step,
	TArray<FVector>& OutPlace, TArray<FVector>& OutTurn, int32 SwitchPhase) const
{
	if (!Weapons.IsValidIndex(Weapon) || Step <= 0.0f)
	{
		return;
	}
	const FJamWeapon& Gun = Weapons[Weapon];
	FPoseState State;
	const float Length = ReloadDuration(Gun, Shells);
	if (SwitchPhase == 2)
	{
		// A draw starts from the put-away pose.
		State.Place = Gun.StowOffsetCm;
		State.Turn = FVector(Gun.StowTurn.Pitch, Gun.StowTurn.Yaw, Gun.StowTurn.Roll);
	}
	for (float T = 0.0f; T <= Seconds + KINDA_SMALL_NUMBER; T += Step)
	{
		OutPlace.Add(State.Place);
		OutTurn.Add(State.Turn);
		if (SwitchPhase != 0)
		{
			FSwitchSpan Span;
			Span.Stage = SwitchPhase == 1 ? ESwitch::Stow : ESwitch::Draw;
			Span.Time0 = T;
			Span.Time1 = T + Step;
			StepPose(Gun, State, 1000.0f, 1000.0f, -1.0f, -1.0f, 0.0f, 0, Step, Span);
		}
		else if (bReload)
		{
			StepPose(Gun, State, 1000.0f, 1000.0f, T, T + Step, Length, Shells, Step);
		}
		else
		{
			StepPose(Gun, State, T, T + Step, -1.0f, -1.0f, 0.0f, 0, Step);
		}
	}
}

namespace
{
	/** The time on Path (sampled every 5 ms up to its end) where it is nearest Share: a redirect picks up where the gun is. */
	float TimeAtShare(const FRuntimeFloatCurve& Path, float Share)
	{
		const float End = LastKeyTime(Path);
		float Best = 0.0f;
		float BestError = TNumericLimits<float>::Max();
		for (float T = 0.0f; T <= End + KINDA_SMALL_NUMBER; T += 0.005f)
		{
			if (const float Error = FMath::Abs(JamEvalCycle(Path, T) - Share); Error < BestError)
			{
				Best = T;
				BestError = Error;
			}
		}
		return Best;
	}
}

void UJamWeaponsComponent::ClearView()
{
	SwitchStage = ESwitch::Away;
	SwitchTime = 0.0f;
	ShownWeapon = INDEX_NONE;
	PoseWeapon = INDEX_NONE; // the next draw starts from its put-away pose
	if (ViewModel)
	{
		ViewModel->SetStaticMesh(nullptr);
		ActionInView->SetStaticMesh(nullptr);
	}
}

void UJamWeaponsComponent::FinishSwitch()
{
	SwitchStage = ESwitch::Drawn;
	SwitchTime = 0.0f;
	ShownWeapon = IsWeaponUnlocked(CurrentWeapon) ? CurrentWeapon : INDEX_NONE;
	if (ViewModel)
	{
		const FJamWeapon* Gun = GetWeapon();
		ViewModel->SetStaticMesh(Gun ? Gun->ViewMesh.Get() : nullptr);
		ActionInView->SetStaticMesh(Gun ? Gun->ActionMesh.Get() : nullptr);
	}
	Pose = FPoseState();
	PoseWeapon = ShownWeapon;
}

bool UJamWeaponsComponent::IsWeaponReady() const
{
	if (!IsWeaponUnlocked(CurrentWeapon) || ShownWeapon != CurrentWeapon || !IsInHand())
	{
		return false;
	}
	return SwitchStage == ESwitch::Drawn;
}

const FJamWeapon* UJamWeaponsComponent::UpdateSwitch(float DeltaTime, bool bHandsBusy, FSwitchSpan& OutSpan)
{
	const int32 Wanted = bHandsBusy || !IsWeaponUnlocked(CurrentWeapon) ? INDEX_NONE : CurrentWeapon;
	const float Before = SwitchTime;
	SwitchTime += DeltaTime;
	const auto Show = [this](int32 Index)
	{
		ShownWeapon = Index;
		if (Index == INDEX_NONE)
		{
			PoseWeapon = INDEX_NONE; // the next draw starts from its put-away pose, the same gun too
		}
		const FJamWeapon* Gun = Weapons.IsValidIndex(Index) ? &Weapons[Index] : nullptr;
		ViewModel->SetStaticMesh(Gun ? Gun->ViewMesh.Get() : nullptr);
		ActionInView->SetStaticMesh(Gun ? Gun->ActionMesh.Get() : nullptr);
		SinceKickSeconds = 1000.0f; // a gun coming into view shows no kick (its cycle keeps its own clock)
	};
	// Per-play volume and pitch so repeated switches do not sound identical.
	const auto Play = [this](USoundBase* Sound)
	{
		if (Sound)
		{
			UGameplayStatics::PlaySound2D(this, Sound, FMath::FRandRange(0.85f, 1.0f), FMath::FRandRange(0.95f, 1.05f));
			UE_LOG(LogMindKore, Log, TEXT("Weapon %s: switch sound %s"), *GetNameSafe(GetOwner()), *Sound->GetName());
		}
	};
	// The shoulder taking the gun: a nudge of the view that comes all the way back.
	const auto Nudge = [this](const FRotator& By)
	{
		KickPitchLeft += By.Pitch;
		KickYawLeft += By.Yaw;
		KickAttackLeft = RecoilAttackSeconds;
		SettlePitch += By.Pitch;
		SettleYaw += By.Yaw;
	};
	switch (SwitchStage)
	{
	case ESwitch::Drawn:
		if (Wanted != ShownWeapon)
		{
			SwitchStage = ShownWeapon == INDEX_NONE ? ESwitch::Away : ESwitch::Stow;
			SwitchTime = 0.0f;
		}
		break;
	case ESwitch::Stow:
		if (Wanted == ShownWeapon)
		{
			// Changed its mind: back the way it went, from where it is.
			SwitchStage = ESwitch::Draw;
			SwitchTime = TimeAtShare(Weapons[ShownWeapon].DrawPath, JamEvalCycle(Weapons[ShownWeapon].StowPath, SwitchTime));
		}
		else
		{
			const FJamWeapon& Gun = Weapons[ShownWeapon];
			if (Crossed(Before, SwitchTime, Gun.StowSoundSeconds)) { Play(Gun.StowSound); }
			if (Crossed(Before, SwitchTime, Gun.StowViewNudgeSeconds)) { Nudge(Gun.StowViewNudge); }
			if (SwitchTime >= LastKeyTime(Gun.StowPath))
			{
				SwitchStage = ESwitch::Away;
				SwitchTime = 0.0f;
				Show(INDEX_NONE);
			}
		}
		break;
	case ESwitch::Draw:
		if (Wanted != ShownWeapon)
		{
			// Away again from where it is (a quick switch during the draw).
			SwitchStage = ESwitch::Stow;
			SwitchTime = TimeAtShare(Weapons[ShownWeapon].StowPath, JamEvalCycle(Weapons[ShownWeapon].DrawPath, SwitchTime));
		}
		else
		{
			const FJamWeapon& Gun = Weapons[ShownWeapon];
			if (Crossed(Before, SwitchTime, Gun.DrawSoundSeconds)) { Play(Gun.DrawSound); }
			if (Crossed(Before, SwitchTime, Gun.DrawViewNudgeSeconds)) { Nudge(Gun.DrawViewNudge); }
			// Drawn: far enough along its path and settled close to rest (latched - the float past rest
			// does not take it back), or at the path's end.
			if (SwitchTime >= LastKeyTime(Gun.DrawPath) || (SwitchTime >= Gun.DrawReadySeconds
				&& Pose.Place.Size() <= DrawReadyPlaceCm && Pose.Turn.GetAbsMax() <= DrawReadyTurnDeg))
			{
				SwitchStage = ESwitch::Drawn;
			}
		}
		break;
	case ESwitch::Away:
		if (Wanted != INDEX_NONE)
		{
			Show(Wanted);
			SwitchStage = ESwitch::Draw;
			SwitchTime = 0.0f;
		}
		break;
	}
	OutSpan.Stage = SwitchStage;
	OutSpan.Time1 = SwitchTime;
	OutSpan.Time0 = FMath::Min(SwitchTime, FMath::Max(0.0f, SwitchTime - DeltaTime));
	return Weapons.IsValidIndex(ShownWeapon) ? &Weapons[ShownWeapon] : nullptr;
}

FTransform UJamWeaponsComponent::GetMagPose(const FJamWeapon& Weapon, float Time)
{
	// Along the well's axis; the arc and the turn only once clear of the well (owner 19.10: it went
	// through the grip at an angle), the turn about the magazine's own top. All in ViewMesh's frame,
	// the same as the gun's own pose springs move it in.
	const float Along = JamEvalCycle(Weapon.MagTravel, Time);
	const float Out = Along * Weapon.MagTravelCm.Size();
	const float Free = FMath::SmoothStep(0.0f, 1.0f, (Out - Weapon.MagClearCm) / 2.0f);
	const FQuat Turn(FVector::RightVector, FMath::DegreesToRadians(Weapon.MagTurnDeg * JamEvalCycle(Weapon.MagTurn, Time) * Free));
	const FVector Place = Weapon.MagTravelCm * Along + Weapon.MagArcCm * JamEvalCycle(Weapon.MagArc, Time) * Free;
	return FTransform(Turn, Place + Weapon.MagPivot - Turn.RotateVector(Weapon.MagPivot));
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
	MagInView->SetRelativeTransform(Time >= 0.0f ? GetMagPose(*Weapon, Time) : FTransform::Identity);

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

void UJamWeaponsComponent::PlayMechanismSound(FJamWeapon& Gun, USoundBase* Sound, float Into)
{
	Gun.MechanismSound = UGameplayStatics::SpawnSound2D(this, Sound, 1.0f, 1.0f, Into);
	UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s %s from %.3f s"), *GetNameSafe(GetOwner()), Sound == Gun.CycleSound ? TEXT("cycle") : TEXT("sound"),
		*Sound->GetName(), Into);
}

void UJamWeaponsComponent::UpdateCycleEvents()
{
	const FTransform Gun = ViewModel->GetComponentTransform();
	const FTransform View = ViewModel->GetAttachParent()->GetComponentTransform();
	// After the shot, on the clock of the gun whose cycle runs (UpdateViewModel calls this once the gun is posed
	// this frame: the case leaves the port where it is drawn, a sound starts on the frame it is due; live time: a
	// held pose throws nothing). Only a shot restarts it (Fire sets PrevSinceShot). A gun put away keeps its clock
	// still, so no case flies and no cycle sound starts away from it; the rest of the cycle comes when it is back
	// (owner 19.10: «механика не врёт» - the pump, its sound and its case together, or none of them).
	if (Weapons.IsValidIndex(CycleWeapon))
	{
		FJamWeapon& Shot = Weapons[CycleWeapon];
		const float Prev = Shot.PrevSinceShot;
		const float Since = Shot.SinceShot;
		if (Shot.CaseMesh && Crossed(Prev, Since, Shot.EjectSeconds))
		{
			// Out of the port to the right and up, end over end about the line of sight.
			const FVector Velocity = GetOwner()->GetVelocity() + View.TransformVectorNoScale(Shot.EjectVelocity * FMath::FRandRange(0.85f, 1.15f)
				+ FVector(FMath::FRandRange(-20.0f, 20.0f), 0.0f, FMath::FRandRange(-25.0f, 25.0f)));
			const FVector Spin = View.GetUnitAxis(EAxis::X) * FMath::FRandRange(900.0f, 1600.0f) * (FMath::RandBool() ? 1.0f : -1.0f)
				+ FMath::VRand() * 300.0f;
			const FRotator Facing = FRotationMatrix::MakeFromXZ(View.GetUnitAxis(EAxis::Y), View.GetUnitAxis(EAxis::Z)).Rotator()
				+ FRotator(FMath::FRandRange(-15.0f, 15.0f), FMath::FRandRange(-20.0f, 20.0f), 0.0f);
			SpawnDebris(Shot.CaseMesh, FTransform(Facing, Gun.TransformPosition(Shot.EjectPort)), Velocity, Spin, 0.012f);
			++CasesThrown;
		}
		if (Shot.HammerSound && Crossed(Prev, Since, Shot.HammerSoundSeconds))
		{
			PlayMechanismSound(Shot, Shot.HammerSound, 0.0f);
		}
		// The pump or the bolt, on the same clock as its animation and case (it was a timer a switch cleared).
		if (Shot.CycleSound && Crossed(Prev, Since, Shot.CycleDelaySeconds))
		{
			PlayMechanismSound(Shot, Shot.CycleSound, 0.0f);
			++CycleSoundsPlayed;
		}
		Shot.PrevSinceShot = Since;
	}
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
	if (!ViewModel || !ViewModel->GetAttachParent())
	{
		return;
	}
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

	if (!IsWeaponUnlocked(CurrentWeapon))
	{
		return;
	}
	FJamWeapon& Weapon = Weapons[CurrentWeapon];

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
		// Its rounds leave with it (review 19.10: they stayed in Ammo until the reload's end); a reload
		// abandoned before the seat leaves the gun empty.
		Weapon.Ammo = 0;
	}
	if (!Weapon.ShellMesh && Crossed(PrevReloadTime, Time, Weapon.MagInSoundSeconds))
	{
		// The fresh magazine seated: the rounds count now, not at the reload's end - a switch after the seat keeps them.
		const int32 Moved = FMath::Min(Weapon.MagazineSize - Weapon.Ammo, Weapon.Reserve);
		Weapon.Ammo += Moved;
		Weapon.Reserve -= Moved;
		Play(Weapon.MagInSound);
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s magazine in, %d/%d"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString(), Weapon.Ammo, Weapon.Reserve);
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
	// Not on Visibility either: the pawn's look traces (RMB grab, F push, the grapple, the hull tools) see
	// through it, so RMB aims instead of taking a case or a dropped magazine.
	Body->SetCollisionResponseToChannel(ECC_Visibility, ECR_Ignore);
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
		for (int32 Gun = 0; Gun < FMath::Min(3, Guns->Weapons.Num()); ++Gun)
		{
			const FJamWeapon& Weapon = Guns->Weapons[Gun];
			for (const int32 Phase : { 1, 2 })
			{
				TArray<FVector> Place, Turn;
				Guns->TracePose(Gun, false, 0, LastKeyTime(Phase == 1 ? Weapon.StowPath : Weapon.DrawPath) + 0.4f, 0.01f, Place, Turn, Phase);
				for (int32 Index = 0; Index < Place.Num(); ++Index)
				{
					const float T = Index * 0.01f;
					const float Path = JamEvalCycle(Phase == 1 ? Weapon.StowPath : Weapon.DrawPath, T);
					// The bore's angle to the eye: from the grip, the gun's line against the way back to the camera.
					const FQuat Rotation = FRotator(Turn[Index].X, Turn[Index].Y, Turn[Index].Z).Quaternion() * Weapon.ViewOffset.GetRotation();
					const FVector Grip = Weapon.ViewOffset.GetTranslation() + Place[Index];
					const FVector Bore = Rotation.RotateVector(Gun == 0 ? -FVector::ForwardVector : FVector::ForwardVector);
					const float ToEye = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Bore, (-Grip).GetSafeNormal())));
					UE_LOG(LogMindKore, Log, TEXT("Pose trace: %s %s t=%.2f place=(%.3f,%.3f,%.3f) turn=(%.3f,%.3f,%.3f) part=(%.4f,0,0) bore_to_eye=%.1f"),
						Names[Gun], Phase == 1 ? TEXT("stow") : TEXT("draw"), T, Place[Index].X, Place[Index].Y, Place[Index].Z,
						Turn[Index].X, Turn[Index].Y, Turn[Index].Z, Path, ToEye);
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

	/**
	 * jam.Weapons.MagTrace: the magazines' pose on the gun every 1/60 s through the reload,
	 * "Mag trace: <gun> t=.. loc=(x,y,z) turn=<deg about ViewMesh Y> shown=<0|1>" - for the
	 * side-view renders and the check that it only slides while in its well.
	 */
	void RunMagTrace(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			return;
		}
		for (const int32 Gun : { 0, 2 })
		{
			const FJamWeapon& Weapon = Pawn->Guns->Weapons[Gun];
			for (int32 Frame = 0; Frame * (1.0f / 60.0f) <= Weapon.ReloadSeconds; ++Frame)
			{
				const float T = Frame / 60.0f;
				const FTransform Pose = UJamWeaponsComponent::GetMagPose(Weapon, T);
				const bool bShown = !(T >= Weapon.MagEjectSeconds && T < Weapon.MagInSeconds);
				const FVector Loc = Pose.GetLocation();
				UE_LOG(LogMindKore, Log, TEXT("Mag trace: %s t=%.4f loc=(%.4f,%.4f,%.4f) turn=%.4f shown=%d"), Gun ? TEXT("sv98") : TEXT("pistol"),
					// A turn about Y only: X goes to (cos a, 0, -sin a).
					T, Loc.X, Loc.Y, Loc.Z, FMath::RadiansToDegrees(FMath::Atan2(-Pose.GetUnitAxis(EAxis::X).Z, Pose.GetUnitAxis(EAxis::X).X)), bShown ? 1 : 0);
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Mag trace: done"));
	}

	FAutoConsoleCommandWithWorld GMagTrace(
		TEXT("jam.Weapons.MagTrace"),
		TEXT("Log the magazines' pose on the gun every 1/60 s through the reload."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunMagTrace));

	/**
	 * jam.Weapons.MagFrames: the magazine drop and insert of the USP and the SV-98 in first
	 * person at 60 fps (jam.Weapons.ReloadPoseAt; HighResShot, needs a real RHI):
	 * JamMag_<gun>_<ms>.png.
	 */
	void RunMagFrames(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		IConsoleVariable* ReloadAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.ReloadPoseAt"));
		if (!Pawn || !ReloadAt)
		{
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
		float At = 8.0f;
		for (const int32 Gun : { 0, 2 })
		{
			const FJamWeapon& Weapon = Pawn->Guns->Weapons[Gun];
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Hold(); });
			At += 1.0f;
			const float Spans[2][2] = { { Weapon.MagEjectSeconds - 0.18f, Weapon.MagEjectSeconds + 0.02f },
				{ Weapon.MagInSeconds - 0.02f, Weapon.MagInSoundSeconds + 0.07f } };
			for (const auto& Span : Spans)
			{
				for (int32 Frame = FMath::FloorToInt(Span[0] * 60.0f); Frame <= FMath::CeilToInt(Span[1] * 60.0f); ++Frame)
				{
					const float T = Frame / 60.0f;
					After(At, [Hold, ReloadAt, T]() { Hold(); ReloadAt->Set(T, ECVF_SetByCode); });
					After(At + 0.12f, [Controller, Gun, T]()
					{
						Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamMag_%s_%04d"), Gun ? TEXT("sv98") : TEXT("pistol"),
							FMath::RoundToInt(T * 1000.0f)));
					});
					At += 0.25f;
				}
			}
			After(At, [ReloadAt]() { ReloadAt->Set(-1.0f, ECVF_SetByCode); });
			At += 0.5f;
		}
		After(At, [Hold]() { Hold(); UE_LOG(LogMindKore, Log, TEXT("Mag frames: done")); });
	}

	FAutoConsoleCommandWithWorld GMagFrames(
		TEXT("jam.Weapons.MagFrames"),
		TEXT("The USP's and the SV-98's magazine drop and insert in first person at 60 fps (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunMagFrames));

	/**
	 * jam.Weapons.SwitchFrames: every gun put away and drawn, stills every 33 ms held with
	 * jam.Weapons.StowPoseAt / DrawPoseAt (HighResShot; needs a real RHI):
	 * JamSwitch_<gun>_<stow|draw>_<ms>.png.
	 */
	void RunSwitchFrames(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		IConsoleVariable* StowAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.StowPoseAt"));
		IConsoleVariable* DrawAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.DrawPoseAt"));
		if (!Pawn || !StowAt || !DrawAt)
		{
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
		float At = 8.0f;
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			After(At, [Pawn, Hold, Gun]() { Pawn->Guns->UnlockWeapon(Gun); Pawn->Guns->FinishSwitch(); Hold(); });
			At += 1.0f;
			const FJamWeapon& Weapon = Pawn->Guns->Weapons[Gun];
			for (const bool bDraw : { false, true })
			{
				const float End = LastKeyTime(bDraw ? Weapon.DrawPath : Weapon.StowPath) + (bDraw ? 0.3f : 0.0f);
				for (float T = 0.0f; T <= End + KINDA_SMALL_NUMBER; T += 1.0f / 30.0f)
				{
					After(At, [Hold, StowAt, DrawAt, bDraw, T]()
					{
						Hold();
						StowAt->Set(bDraw ? -1.0f : T, ECVF_SetByCode);
						DrawAt->Set(bDraw ? T : -1.0f, ECVF_SetByCode);
					});
					After(At + 0.12f, [Controller, Name = Names[Gun], bDraw, T]()
					{
						Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamSwitch_%s_%s_%04d"), Name,
							bDraw ? TEXT("draw") : TEXT("stow"), FMath::RoundToInt(T * 1000.0f)));
					});
					At += 0.25f;
				}
			}
			After(At, [StowAt, DrawAt]() { StowAt->Set(-1.0f, ECVF_SetByCode); DrawAt->Set(-1.0f, ECVF_SetByCode); });
			At += 0.5f;
		}
		After(At, [Hold]() { Hold(); UE_LOG(LogMindKore, Log, TEXT("Switch frames: done")); });
	}

	FAutoConsoleCommandWithWorld GSwitchFrames(
		TEXT("jam.Weapons.SwitchFrames"),
		TEXT("Every gun put away and drawn, stills every 33 ms (jam.Weapons.StowPoseAt / DrawPoseAt; HighResShot, needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunSwitchFrames));

	/**
	 * jam.Weapons.SwitchProbe: live switching with quick changes of mind, logged every frame -
	 * "Switch probe: t=.. stage=.. shown=.. current=.. ready=.. place=(..) turn=(..)" and the
	 * largest frame-to-frame jump of the pose (a pop would show as one). Pistol -> shotgun,
	 * back to the pistol during the put-away (0.2 s), the SV-98 (0.8 s), the shotgun during
	 * the SV-98's draw (1.6 s); a try to fire during a draw and after it; holster at once and
	 * draw (3.6 s, 4.2 s). Then the cases and the cycle sounds over switches (PASS / FAIL lines), the magazine
	 * counted at its seat, R waiting for the bolt; the last line "Switch probe: all done". Runs headless too.
	 */
	void RunSwitchProbe(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			return;
		}
		UJamWeaponsComponent* Guns = Pawn->Guns;
		for (int32 Gun = 0; Gun < Guns->Weapons.Num(); ++Gun)
		{
			Guns->UnlockWeapon(Gun);
		}
		Guns->SelectWeapon(0);
		Guns->FinishSwitch();
		struct FProbe
		{
			double Start = 0.0;
			FVector Place = FVector::ZeroVector;
			FVector Turn = FVector::ZeroVector;
			float MaxPlaceJump = 0.0f;
			float MaxTurnJump = 0.0f;
			int32 Frames = 0;
			int32 Cases = 0;
			int32 Ammo = 0;
			int32 Sounds = 0;
			int32 Reserve = 0;
			double ShotAt = 0.0;
		};
		TSharedRef<FProbe> Probe = MakeShared<FProbe>();
		Probe->Start = World->GetTimeSeconds();
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		FTimerHandle Every;
		Pawn->GetWorldTimerManager().SetTimer(Every, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Guns, Probe]()
		{
			const float T = float(Pawn->GetWorld()->GetTimeSeconds() - Probe->Start);
			if (T > 5.0f)
			{
				return;
			}
			const FVector Place = Guns->GetPosePlace();
			const FVector Turn = Guns->GetPoseTurn();
			if (Probe->Frames++ > 0)
			{
				Probe->MaxPlaceJump = FMath::Max(Probe->MaxPlaceJump, float((Place - Probe->Place).Size()));
				Probe->MaxTurnJump = FMath::Max(Probe->MaxTurnJump, float((Turn - Probe->Turn).GetAbsMax()));
			}
			Probe->Place = Place;
			Probe->Turn = Turn;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: t=%.3f shown=%d current=%d ready=%d place=(%.2f,%.2f,%.2f) turn=(%.2f,%.2f,%.2f)"), T,
				Guns->GetShownWeapon(), Guns->CurrentWeapon, Guns->IsWeaponReady() ? 1 : 0, Place.X, Place.Y, Place.Z, Turn.X, Turn.Y, Turn.Z);
		}), 0.001f, true);
		After(0.05f, [Guns]() { Guns->SelectWeapon(1); });
		After(0.25f, [Guns]() { Guns->SelectWeapon(0); });   // during the pistol's put-away: back
		After(0.8f, [Guns]() { Guns->SelectWeapon(2); });
		After(1.6f, [Guns]() { Guns->SelectWeapon(1); });    // during the SV-98's draw: away again, the shotgun
		After(2.0f, [Pawn, Guns]() { UE_LOG(LogMindKore, Log, TEXT("Switch probe: fire at 2.0 s (%s): %s"), *Guns->Describe(), Pawn->Fire() ? TEXT("fired") : TEXT("refused")); });
		After(3.2f, [Pawn, Guns]() { UE_LOG(LogMindKore, Log, TEXT("Switch probe: fire at 3.2 s (%s): %s"), *Guns->Describe(), Pawn->Fire() ? TEXT("fired") : TEXT("refused")); });
		After(3.6f, [Pawn]() { Pawn->SetHands(EJamHands::Held); });
		After(4.2f, [Pawn]() { Pawn->SetHands(EJamHands::Gun); });
		After(5.1f, [Pawn, Guns, Probe]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: done - %d frames, largest frame-to-frame jump %.2f cm, %.2f deg; fire after the draw: %s"),
				Probe->Frames, Probe->MaxPlaceJump, Probe->MaxTurnJump, Pawn->Fire() ? TEXT("fired") : TEXT("refused"));
		});
		// Back and forth with no shot (owner 19.10: a switch threw a case from the shotgun and the SV-98):
		// whole switches, quick changes of mind, unarmed and back - none throws a case.
		float At = 6.0f;
		After(At, [Guns, Probe]() { Probe->Cases = Guns->GetCasesThrown(); });
		int32 Switches = 0;
		for (const int32 Gun : { 1, 2, 0, 2, 1, 0, 1, 2, 0, 1, 2, 1 })
		{
			After(At += 1.4f, [Guns, Gun]() { Guns->SelectWeapon(Gun); });
			++Switches;
		}
		for (int32 Quick = 0; Quick < 6; ++Quick)
		{
			After(At += 0.15f, [Guns, Quick]() { Guns->SelectWeapon(Quick % 2 == 0 ? 2 : 1); });
			++Switches;
		}
		for (int32 Round = 0; Round < 3; ++Round)
		{
			After(At += 1.0f, [Pawn]() { Pawn->SetHands(EJamHands::Held); });
			After(At += 0.8f, [Pawn]() { Pawn->SetHands(EJamHands::Gun); });
			Switches += 2;
		}
		After(At += 1.5f, [Guns, Probe, Switches]()
		{
			const int32 Thrown = Guns->GetCasesThrown() - Probe->Cases;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: %d switches, no shot - %d case(s) thrown (expected 0): %s"), Switches, Thrown,
				Thrown == 0 ? TEXT("PASS") : TEXT("FAIL"));
		});
		// A cycle cut short: the shotgun fired and put away mid-pump (its case due at 0.26 s), drawn
		// back - the cycle froze while away and finishes on the draw-back: one case, one round spent.
		After(At += 0.1f, [Pawn, Guns, Probe]()
		{
			Probe->Cases = Guns->GetCasesThrown();
			Probe->Ammo = Guns->Weapons[1].Ammo;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: mid-pump shot (%s): %s"), *Guns->Describe(), Pawn->Fire() ? TEXT("fired") : TEXT("refused"));
		});
		After(At += 0.1f, [Guns]() { Guns->SelectWeapon(2); });
		After(At += 1.5f, [Guns]() { Guns->SelectWeapon(1); });
		After(At += 1.6f, [Pawn, Guns, Probe]()
		{
			const int32 Thrown = Guns->GetCasesThrown() - Probe->Cases;
			const int32 Ammo = Guns->Weapons[1].Ammo;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: cycle cut by a switch and drawn back - %d case(s) (expected 1), ammo %d -> %d (expected -1): %s"),
				Thrown, Probe->Ammo, Ammo, Thrown == 1 && Ammo == Probe->Ammo - 1 ? TEXT("PASS") : TEXT("FAIL"));
			Probe->Cases = Guns->GetCasesThrown();
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: shot after the draw-back: %s"), Pawn->Fire() ? TEXT("fired") : TEXT("refused"));
		});
		After(At += 0.6f, [Guns, Probe]()
		{
			const int32 Thrown = Guns->GetCasesThrown() - Probe->Cases;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: a whole pump after a shot - %d case(s) (expected 1): %s"), Thrown, Thrown == 1 ? TEXT("PASS") : TEXT("FAIL"));
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: cases done"));
		});
		// A change of mind mid-pump (review 19.10): the shotgun fired, the pistol picked 0.1 s later, the shotgun
		// back 0.1 s / 0.3 s after that. The pump, its sound and its case come as one: one of each.
		for (const float Back : { 0.1f, 0.3f })
		{
			After(At += 0.6f, [Pawn, Guns, Probe, Back]()
			{
				Probe->Cases = Guns->GetCasesThrown();
				Probe->Sounds = Guns->GetCycleSounds();
				UE_LOG(LogMindKore, Log, TEXT("Switch probe: shotgun shot, the pistol at +0.1 s, back %.1f s later (%s): %s"), Back, *Guns->Describe(),
					Pawn->Fire() ? TEXT("fired") : TEXT("refused"));
			});
			After(At + 0.1f, [Guns]() { Guns->SelectWeapon(0); });
			After(At + 0.1f + Back, [Guns]() { Guns->SelectWeapon(1); });
			After(At += 1.6f, [Guns, Probe, Back]()
			{
				const int32 Cases = Guns->GetCasesThrown() - Probe->Cases;
				const int32 Sounds = Guns->GetCycleSounds() - Probe->Sounds;
				UE_LOG(LogMindKore, Log, TEXT("Switch probe: return after %.1f s - %d case(s), %d pump sound(s) (expected 1, 1): %s"), Back, Cases, Sounds,
					Cases == 1 && Sounds == 1 ? TEXT("PASS") : TEXT("FAIL"));
			});
		}
		// The magazine counts when it seats (review 19.10): the SV-98 reloaded from 1 round, the pistol picked 1.4 s in -
		// after the seat (1.25 s), before the reload's end (1.7 s) - and the rifle back: a full magazine from the reserve.
		After(At += 0.5f, [Guns]() { Guns->SelectWeapon(2); });
		After(At += 1.5f, [Guns, Probe]()
		{
			FJamWeapon& Rifle = Guns->Weapons[2];
			Rifle.Ammo = 1;
			Probe->Reserve = Rifle.Reserve;
			Guns->Reload();
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: SV-98 reload from %s"), *Guns->Describe());
		});
		After(At += 1.4f, [Guns]() { Guns->SelectWeapon(0); });
		After(At += 0.8f, [Guns]() { Guns->SelectWeapon(2); });
		After(At += 1.5f, [Guns, Probe]()
		{
			const FJamWeapon& Rifle = Guns->Weapons[2];
			const bool bPass = Rifle.Ammo == Rifle.MagazineSize && Rifle.Reserve == Probe->Reserve - Rifle.MagazineSize;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: switched after the seat, before the end - %d/%d (expected %d/%d): %s"), Rifle.Ammo, Rifle.Reserve,
				Rifle.MagazineSize, Probe->Reserve - Rifle.MagazineSize, bPass ? TEXT("PASS") : TEXT("FAIL"));
		});
		// ...and switched after the old magazine dropped (0.5 s), before the new one seats: its rounds left with it.
		After(At += 0.2f, [Guns, Probe]()
		{
			FJamWeapon& Rifle = Guns->Weapons[2];
			Rifle.Ammo = 2;
			Probe->Reserve = Rifle.Reserve;
			Guns->Reload();
		});
		After(At += 0.8f, [Guns]() { UE_LOG(LogMindKore, Log, TEXT("Switch probe: 0.8 s into the reload, the old magazine out: %s"), *Guns->Describe()); Guns->SelectWeapon(0); });
		After(At += 0.8f, [Guns]() { Guns->SelectWeapon(2); });
		After(At += 1.5f, [Guns, Probe]()
		{
			const FJamWeapon& Rifle = Guns->Weapons[2];
			const bool bPass = Rifle.Ammo == 0 && Rifle.Reserve == Probe->Reserve;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: switched between the drop and the seat - %d/%d (expected 0/%d): %s"), Rifle.Ammo, Rifle.Reserve,
				Probe->Reserve, bPass ? TEXT("PASS") : TEXT("FAIL"));
		});
		// R right after a shot waits for the bolt (review 19.10): fired, R at 0.1 s - the reload starts when the cycle ends (1.25 s).
		After(At += 0.2f, [Pawn, Guns, Probe]()
		{
			Guns->Weapons[2].Ammo = 2;
			Probe->ShotAt = Pawn->GetWorld()->GetTimeSeconds();
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: SV-98 shot (%s): %s"), *Guns->Describe(), Pawn->Fire() ? TEXT("fired") : TEXT("refused"));
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		});
		After(At + 0.1f, [Guns]() { Guns->Reload(); });
		After(At + 0.2f, [Guns]()
		{
			const float Left = Guns->GetReloadRemaining();
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: 0.2 s after the shot, R at 0.1 s - reload %s (expected waiting): %s"),
				Left > 0.0f ? TEXT("running") : TEXT("waiting"), Left > 0.0f ? TEXT("FAIL") : TEXT("PASS"));
		});
		After(At += 1.4f, [Pawn, Guns, Probe]()
		{
			const FJamWeapon& Rifle = Guns->Weapons[2];
			const float Left = Guns->GetReloadRemaining();
			const double Now = Pawn->GetWorld()->GetTimeSeconds();
			const float Started = Left > 0.0f ? float(Now - (Rifle.ReloadSeconds - Left) - Probe->ShotAt) : -1.0f;
			const float Cycle = Rifle.GetActionCycleSeconds();
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: the reload started %.3f s after the shot (cycle %.2f s; expected at its end): %s"), Started, Cycle,
				Started >= Cycle - 0.001f && Started <= Cycle + 0.1f ? TEXT("PASS") : TEXT("FAIL"));
		});
		// The wheel with a hull tool in hand (review 19.10): it reels the strap and leaves the guns alone; without one
		// it steps the guns. Injected into the real input action, as the mouse would.
		const auto Wheel = [Pawn, Guns]()
		{
			const APlayerController* Controller = Cast<APlayerController>(Pawn->GetController());
			UEnhancedInputLocalPlayerSubsystem* Input = Controller ? ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()) : nullptr;
			UInputAction* Action = FindObject<UInputAction>(Pawn, TEXT("IA_ZG_Wheel"));
			if (Input && Action)
			{
				Input->InjectInputForAction(Action, FInputActionValue(1.0f), {}, {});
			}
			return Input && Action;
		};
		After(At += 2.0f, [Pawn, Guns, Probe, Wheel]()
		{
			Pawn->SetHands(EJamHands::Tool, EJamHullTool::Strap);
			Probe->Ammo = Guns->CurrentWeapon;
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: the strap in hand, the wheel turned (%s)"), Wheel() ? TEXT("injected") : TEXT("NO INPUT"));
		});
		After(At += 0.3f, [Pawn, Guns, Probe, Wheel]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: wheel with the strap - gun %d -> %d (expected unchanged): %s"), Probe->Ammo, Guns->CurrentWeapon,
				Guns->CurrentWeapon == Probe->Ammo ? TEXT("PASS") : TEXT("FAIL"));
			Pawn->SetHands(EJamHands::Gun);
			Wheel();
		});
		After(At += 0.3f, [Guns, Probe]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: wheel with no tool - gun %d -> %d (expected another): %s"), Probe->Ammo, Guns->CurrentWeapon,
				Guns->CurrentWeapon != Probe->Ammo ? TEXT("PASS") : TEXT("FAIL"));
		});
		// Spent cases and dropped magazines thrown in this probe: none blocks Visibility, the look traces' channel
		// (RMB grab, F push, the grapple, the hull tools), so RMB never picks one (review 19.10).
		After(At += 0.3f, [Pawn]()
		{
			int32 Debris = 0;
			int32 Visible = 0;
			for (TActorIterator<AStaticMeshActor> It(Pawn->GetWorld()); It; ++It)
			{
				const UStaticMeshComponent* Body = It->GetStaticMeshComponent();
				const FString Mesh = Body && Body->GetStaticMesh() ? Body->GetStaticMesh()->GetName() : FString();
				if (Body && Body->IsSimulatingPhysics() && (Mesh.StartsWith(TEXT("SM_Jam_Case")) || Mesh.StartsWith(TEXT("SM_Jam_Shell")) || Mesh.EndsWith(TEXT("_Mag"))))
				{
					++Debris;
					Visible += Body->GetCollisionResponseToChannel(ECC_Visibility) != ECR_Ignore ? 1 : 0;
				}
			}
			UE_LOG(LogMindKore, Log, TEXT("Switch probe: %d loose case(s)/magazine(s), %d on Visibility (expected 0): %s"), Debris, Visible,
				Debris > 0 && Visible == 0 ? TEXT("PASS") : TEXT("FAIL"));
		});
		After(At += 2.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Switch probe: all done")); });
	}

	FAutoConsoleCommandWithWorld GSwitchProbe(
		TEXT("jam.Weapons.SwitchProbe"),
		TEXT("Live switching with quick changes of mind and a holster/draw, logged every frame with the largest pose jump."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunSwitchProbe));

	FAutoConsoleCommandWithWorld GWeaponFrames(
		TEXT("jam.Weapons.Frames"),
		TEXT("Every gun's cycle and reload as stills every 33 ms (jam.Weapons.PoseAt / ReloadPoseAt; HighResShot, needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponFrames));

	FAutoConsoleCommandWithWorld GWeaponReloads(
		TEXT("jam.Weapons.Reloads"),
		TEXT("The reloads frame by frame (jam.Weapons.ReloadPoseAt), spent cases and dropped magazines live (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunWeaponReloads));
}
