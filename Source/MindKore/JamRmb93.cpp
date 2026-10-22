// The RMB-93 «Рысь» (Docs/RecoilWeapons-Rmb93.md): the pump shotgun's slot with its own handling - a
// forward pump stroke, the spent hull dropped out of the bottom, round-by-round loading through the top
// window under a hinged lid (JamWeaponLid.cpp) and smoke out of the shroud's holes. Its probe is here too.

#include "JamWeapons.h"

#include "Engine/StaticMesh.h"
#include "Sound/SoundBase.h"
#include "UObject/UObjectGlobals.h"

#if !UE_BUILD_SHIPPING
#include "Components/StaticMeshComponent.h"
#include "Containers/Ticker.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "JamGravity.h"
#include "JamMuzzleFx.h"
#include "MindKore.h"
#endif

#define LOCTEXT_NAMESPACE "JamRmb93"

namespace
{
	const TCHAR* const Rmb93Meshes = TEXT("/Game/JamArt/Weapons/RMB93/Meshes/");

	UStaticMesh* Rmb93Mesh(const TCHAR* Name)
	{
		return LoadObject<UStaticMesh>(nullptr, *FString::Printf(TEXT("%sSM_Jam_RMB93_%s.SM_Jam_RMB93_%s"), Rmb93Meshes, Name, Name));
	}

	USoundBase* GunSound(const TCHAR* Name)
	{
		return LoadObject<USoundBase>(nullptr, *FString::Printf(TEXT("/Game/JamArt/Audio/Weapons/%s.%s"), Name, Name));
	}
}

FJamWeapon JamMakeRmb93(const FJamWeapon& PumpShotgun)
{
	// The pump gun's ballistics, sounds, sling and pace; the meshes from Scripts/jam_rmb93_assets.py: the body with the
	// counter in material slot 1 (the lid cut off it), the barrel and fore-end as the moving part, the lid on its hinge.
	FJamWeapon Gun = PumpShotgun;
	Gun.Name = LOCTEXT("Name", "Рысь");
	Gun.ViewMesh = Rmb93Mesh(TEXT("ViewBody"));
	Gun.ActionMesh = Rmb93Mesh(TEXT("Action"));
	Gun.ActionPivot = FVector::ZeroVector;
	Gun.NixieMaterialSlot = 1;

	// The pump runs forward: the shot's gas does nothing, the hand throws barrel and fore-end 5.5 cm forward off the
	// standing breech and pulls them home. The pump's curve and its sound as they are (the clicks at 0.26 and 0.40 s
	// are inviolable): the forward stop with a 8% bounce, home with a double knock. The gun barely leans with the
	// stroke (owner 22.10: the pump's big cant after every shot is too much) - the other way from the pump's, the
	// stroke being away from the eye: a few degrees onto its left side and half a centimetre out.
	Gun.ActionTravelCm = 5.5f;
	Gun.ActionBodyTurn = FRotator(-1.0f, -2.0f, -3.0f);
	Gun.ActionBodyOffsetCm = FVector(0.5f, -0.5f, 0.3f);
	// The fore-end into its forward stop (a twitch out, the muzzle down) and home (back toward the eye, the muzzle up).
	Gun.ShotKicks = { { 0.26f, FVector(20.0f, 0.0f, -4.0f), FRotator(-20.0f, -4.0f, -6.0f) },
		{ 0.4f, FVector(-20.0f, 0.0f, 5.0f), FRotator(24.0f, 5.0f, 8.0f) } };
	// 3.4 kg against the pump's 3.6: a touch quicker on its springs, the same weight to the turns.
	Gun.PoseSprings = { { 5.8f, 0.5f, 0.0f }, { 5.3f, 0.55f, 0.0f }, { 5.3f, 0.5f, 0.01f },
		{ 4.3f, 0.5f, 0.03f }, { 4.3f, 0.55f, 0.03f }, { 3.9f, 0.5f, 0.05f } };

	// The hull drops out of the bottom as the barrel opens the breech fully (its forward stop): from the gap between the
	// breech face and the barrel's end, lying along the bore, falling, tumbling end over end.
	Gun.EjectPort = FVector(2.8f, 0.0f, -0.75f);
	Gun.EjectSeconds = 0.26f;
	Gun.EjectVelocity = FVector(20.0f, -10.0f, -180.0f);
	Gun.bEjectAlongBore = true;

	// Round by round through the top window into the tube over the barrel (z 4.23, its mouth at x 9.2), 0.5 s a round.
	// One plane (y 0) between the folded stock's rails: the round comes down from above and behind, nose 49 deg down,
	// and puts its nose into the tube's mouth (0.12 s); the thumb presses its base down into the window, the round
	// turning level about its nose (0.2 s); a beat as the thumb comes behind it (0.22 s); pushed forward along the tube,
	// faster and faster, seated at 0.36 s with a 1.5% rebound. ShellInsert runs From -> To, ShellArc off that line (down):
	// along the window and the tube the two keep the round at the tube's height exactly (their keys in proportion).
	Gun.ShellFrom = FVector(-2.0f, 0.0f, 14.0f);
	Gun.ShellTo = FVector(9.4f, 0.0f, 4.23f);
	Gun.ShellArcCm = FVector(0.0f, 0.0f, -6.0f);
	Gun.ShellTiltDeg = 35.0f;
	Gun.ReloadSeconds = 0.5f;
	Gun.ShellSeatSeconds = 0.36f;
	Gun.ShellInsert = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 6.0f }, { 0.12f, 0.5307f, 0.0f, 0.0f }, { 0.16f, 0.449f, -1.37f, -1.37f },
		{ 0.2f, 0.4211f, 0.0f, 0.0f }, { 0.22f, 0.4211f, 0.0f, 0.0f }, { 0.36f, 1.0f, 7.0f, -1.0f }, { 0.375f, 0.985f, 0.0f, 0.0f },
		{ 0.39f, 1.0f, 0.0f, 0.0f } });
	Gun.ShellArc = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.12f, 0.104f, 0.0f, 0.0f }, { 0.16f, 0.551f, 10.5f, 10.5f },
		{ 0.2f, 0.9427f, 0.0f, 0.0f }, { 0.22f, 0.9427f, 0.0f, 0.0f }, { 0.36f, 0.0f, -11.4f, 1.628f }, { 0.375f, 0.0244f, 0.0f, 0.0f },
		{ 0.39f, 0.0f, 0.0f, 0.0f } });
	Gun.ShellTurn = JamMakeCycleCurve({ { 0.0f, 1.4f, 0.0f, 0.0f }, { 0.12f, 1.0f, -2.0f, 0.0f }, { 0.16f, 0.5f, -12.5f, -12.5f },
		{ 0.2f, 0.0f, -6.0f, 0.0f }, { 0.21f, -0.05f, 0.0f, 0.0f }, { 0.22f, 0.0f, 0.0f, 0.0f } });
	// The window to the eye: the muzzle down and to the left, rolled a little onto the left hand, drawn 6 cm back and 5 up.
	// The lead before the first round is 0.3 s: the palm strikes the latch at 0.14 s and the lid is open on its stop by 0.24 s.
	Gun.ReloadBodyTurn = FRotator(-14.0f, -12.0f, -18.0f);
	Gun.ReloadBodyOffsetCm = FVector(-6.0f, -5.0f, 5.0f);
	Gun.ReloadBody = JamMakeCycleCurve({ { 0.0f, 0.0f, 0.0f, 0.0f }, { 0.03f, 1.0f, 0.0f, 0.0f }, { 0.3f, 1.0f, 0.0f, 0.0f } });
	Gun.ReloadKicks.Reset();
	// Three beats a round, as the pump's: the nose into the tube, the base pressed down, the push home (forward).
	Gun.RoundKicks = { { 0.12f, FVector(0.0f, 0.0f, -10.0f), FRotator(0.0f, 0.0f, 15.0f) },
		{ 0.2f, FVector(0.0f, 0.0f, -30.0f), FRotator(-30.0f, 0.0f, 10.0f) },
		{ 0.36f, FVector(45.0f, 0.0f, 10.0f), FRotator(20.0f, 15.0f, -30.0f) } };

	// The lid over the window, hinged at its front on the receiver (in front of it the rear sight it leans on, open).
	FJamLid& Lid = Gun.Lid;
	Lid.Mesh = Rmb93Mesh(TEXT("Lid"));
	Lid.HingeCm = FVector(10.5f, 0.0f, 5.5f);
	Lid.LengthCm = 8.0f;
	// Open, it leans only 14 deg past upright: gravity holds it there against a swing back of ~190 deg/s at most. Off the
	// rear sight it comes back with a tenth of its ~1500 deg/s throw and stays there (a fifth tipped it over the top).
	Lid.OpenBounce = 0.1f;
	// The palm slaps the latch at the lid's rear edge: the gun knocked down, its muzzle dipping, a twist.
	Lid.StrikeSeconds = 0.14f;
	Lid.StrikeKicks = { { 0.14f, FVector(0.0f, 5.0f, -45.0f), FRotator(-40.0f, 10.0f, 20.0f) } };
	// The hand catches it 0.22 s before the end, lifts it a hair and presses it down, faster and faster, onto the
	// latch (0.1 s) - a knock and a 1.5% rebound - shut at 0.14 s.
	Lid.CloseSeconds = 0.22f;
	Lid.Close = JamMakeCycleCurve({ { 0.0f, 1.0f, 0.0f, 0.0f }, { 0.03f, 1.02f, 0.0f, 0.0f }, { 0.1f, 0.0f, -16.0f, 1.2f },
		{ 0.112f, 0.015f, 0.0f, 0.0f }, { 0.125f, 0.0f, -0.5f, 0.0f }, { 0.14f, 0.0f, 0.0f, 0.0f } });
	Lid.LatchSeconds = 0.1f;
	Lid.LatchKicks = { { 0.0f, FVector(0.0f, 0.0f, -10.0f), FRotator::ZeroRotator },
		{ 0.1f, FVector(0.0f, 0.0f, -40.0f), FRotator(-50.0f, -10.0f, -20.0f) } };
	// Borrowed sounds (KNOWN_ISSUES): the slide stop's lever for the latch let go, the shotgun's dry click higher for the
	// lid on a stop, the pistol's magazine seat lower for the latch snapping shut.
	Lid.StrikeSound = GunSound(TEXT("SW_Jam_SlideRelease_Pistol"));
	Lid.StrikeSoundLead = 0.0167f;
	Lid.StrikeSoundPitch = 0.85f;
	Lid.StopSound = GunSound(TEXT("SW_Jam_Empty_Shotgun"));
	Lid.StopSoundPitch = 1.25f;
	Lid.LatchSound = GunSound(TEXT("SW_Jam_MagIn_Pistol"));
	Lid.LatchSoundPitch = 0.9f;

	// The shroud's 16 holes (ActionMesh space, the shroud rides the barrel): four rings along it, two holes a side each, above and below the bore.
	FJamPortSmoke& Smoke = Gun.PortSmoke;
	Smoke.BoreZCm = 0.75f;
	for (const float X : { 38.15f, 41.05f, 43.95f, 46.85f })
	{
		for (const float Y : { 1.21f, -1.21f })
		{
			for (const float Z : { 1.25f, 0.25f })
			{
				Smoke.Ports.Add(FVector(X, Y, Z));
			}
		}
	}
	return Gun;
}

FJamGunAim JamMakeRmb93Aim(const FJamGunAim& PumpAim)
{
	// Along the top of the receiver to the front sight.
	FJamGunAim Aim = PumpAim;
	Aim.SightRearCm = FVector(11.5f, 0.0f, 7.4f);
	Aim.SightFrontCm = FVector(51.5f, 0.0f, 7.4f);
	return Aim;
}

#if !UE_BUILD_SHIPPING
namespace
{
	/**
	 * jam.Rmb93.Probe: the RMB-93 put in slot 2 of the player's guns and run through its handling on the real paths, on
	 * world time (its numbers are the same at any fixed frame rate: Scripts/jam_fps_sweep.ps1): a shot (the hull, the
	 * shroud's puff), a reload (the strike, the lid on its stop, held clear of the round, latched), a reload broken off
	 * by a shot (the lid left open), the lid in zero-G after a flick of the view and in gravity level and looking up,
	 * a reload that only shuts it, a held still. The inventory, the gravity scale and the view are put back after.
	 */
	struct FRmb93Probe
	{
		TWeakObjectPtr<UWorld> World;
		TWeakObjectPtr<APlayerController> Controller;
		TWeakObjectPtr<UJamWeaponsComponent> Guns;
		TWeakObjectPtr<UJamMuzzleFxComponent> Fx;
		TArray<FJamWeapon> SavedWeapons;
		TArray<FJamGunAim> SavedAims;
		int32 SavedCurrent = 0;
		float SavedGravityScale = 1.0f;
		FRotator SavedView = FRotator::ZeroRotator;
		FRotator Level = FRotator::ZeroRotator;
		/** Where Pitch turns from: the view as a turn starts (afloat, the view drifts off level - snapping it back jolts the lid). */
		FRotator PitchBase = FRotator::ZeroRotator;
		/** Stage 9 put the lid upright. */
		bool bLidPlaced = false;
		int32 Passed = 0;
		int32 Failed = 0;
		int32 Stage = 0;
		double StageStart = 0.0;
		// Per stage.
		int32 CasesBefore = 0;
		double ReloadStart = 0.0;
		/** The frames the strike and the latch fell between (they happen inside a frame, on the lid's own steps). */
		FVector2D StruckIn = FVector2D(-1.0f);
		FVector2D LatchedIn = FVector2D(-1.0f);
		/** The frames it knocked on its open stop between; the knocks before the reload. */
		FVector2D OpenIn = FVector2D(-1.0f);
		int32 KnocksBefore = 0;
		float PrevT = 0.0f;
		float LowWhileLoading = 1000.0f;
		float Low = 1000.0f;
		float High = -1000.0f;
		float Rest = 0.0f;
		FTSTicker::FDelegateHandle Ticker;

		void Check(bool bPass, const FString& Label)
		{
			bPass ? ++Passed : ++Failed;
			UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: %s - %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), *Label);
		}
		const FJamLid& Lid() const { return Guns->Weapons[1].Lid; }
		float Since() const { return float(World->GetTimeSeconds() - StageStart); }
		void Next()
		{
			++Stage;
			StageStart = World->GetTimeSeconds();
		}
		bool Shoot()
		{
			FVector Recoil;
			// Above the room: the probe hits nothing.
			const APawn* Pawn = Controller->GetPawn();
			return Guns->Fire(Pawn->GetActorLocation() + FVector(0.0f, 0.0f, 10000.0f), FVector::UpVector, Recoil);
		}
		/** The view pitched Degrees up from PitchBase, eased in over Seconds from the stage's start (smoothstep: no jump in the turn's speed). */
		void Pitch(float From, float To, float Seconds)
		{
			const float Share = FMath::SmoothStep(0.0f, 1.0f, Since() / Seconds);
			Controller->SetControlRotation(PitchBase + FRotator(FMath::Lerp(From, To, Share), 0.0f, 0.0f));
		}
		void Finish()
		{
			if (UJamWeaponsComponent* G = Guns.Get())
			{
				G->Holster(true);
				G->Weapons = SavedWeapons;
				G->Scope->Aims = SavedAims;
				G->CurrentWeapon = SavedCurrent;
				G->FinishSwitch();
			}
			JamSetGravityScale(SavedGravityScale);
			if (APlayerController* PC = Controller.Get())
			{
				PC->SetControlRotation(SavedView);
			}
			IConsoleManager::Get().ProcessUserConsoleInput(TEXT("jam.Rmb93.Cost"), *GLog, World.Get());
			UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: %s, %d passed, %d failed; the inventory, gravity and view put back"),
				Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Passed, Failed);
			UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: done"));
		}

		/** One frame; false when through. */
		bool Tick()
		{
			UJamWeaponsComponent* G = Guns.Get();
			if (!G || !World.IsValid() || !Controller.IsValid())
			{
				UE_LOG(LogMindKore, Error, TEXT("Rmb93 probe: FAIL - the pawn went away"));
				UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: done"));
				return false;
			}
			const FJamWeapon& Gun = G->Weapons[1];
			const FJamLid& L = Lid();
			const float Lead = Gun.ReloadBody.GetRichCurveConst()->GetLastKey().Time;
			switch (Stage)
			{
			case 0: // a shot: the hull out of the bottom, the shroud's puff
				if (Since() >= 0.2f)
				{
					CasesBefore = G->GetCasesThrown();
					const int32 Puffs = Fx.IsValid() ? Fx->GetPuffCount() : 0;
					Check(Shoot() && Gun.Ammo == 5, TEXT("a shot fires, 5 left"));
					Check(Fx.IsValid() && Fx->GetPuffCount() >= Puffs + Gun.PortSmoke.ShotPuffs,
						FString::Printf(TEXT("the shroud's holes puff at the shot (%d puffs)"), Fx.IsValid() ? Fx->GetPuffCount() - Puffs : -1));
					Next();
				}
				break;
			case 1: // the cycle through: the hull thrown; a reload of one round
				if (Since() >= Gun.GetActionCycleSeconds() + 0.1f)
				{
					Check(G->GetCasesThrown() == CasesBefore + 1, TEXT("the forward stroke throws one hull"));
					Check(Gun.PortSmoke.HeatAt(World->GetTimeSeconds()) > 0.5f, TEXT("the barrel is hot (the seep runs)"));
					G->Reload();
					ReloadStart = World->GetTimeSeconds();
					StruckIn = LatchedIn = FVector2D(-1.0f);
					OpenIn = FVector2D(-1.0f);
					KnocksBefore = G->GetLidKnocks();
					PrevT = 0.0f;
					LowWhileLoading = 1000.0f;
					Check(G->GetReloadRemaining() > 0.0f && L.State == EJamLidState::Latched, FString::Printf(TEXT("a reload of one round, %.2f s"), G->GetReloadRemaining()));
					Next();
				}
				break;
			case 2: // the lid through the reload
			{
				const float T = float(World->GetTimeSeconds() - ReloadStart);
				if (StruckIn.X < 0.0f && L.State != EJamLidState::Latched)
				{
					StruckIn = FVector2D(PrevT, T);
				}
				// Its first knock is on the open stop (it leaves the receiver on the kicker, no knock); a frame's last angle may
				// already be the bounce back, so the knock, not the angle, says when it got there.
				if (OpenIn.X < 0.0f && G->GetLidKnocks() > KnocksBefore)
				{
					OpenIn = FVector2D(PrevT, T);
				}
				if (T >= Lead && L.State == EJamLidState::Free)
				{
					LowWhileLoading = FMath::Min(LowWhileLoading, L.Angle);
				}
				if (StruckIn.X >= 0.0f && LatchedIn.X < 0.0f && L.State == EJamLidState::Latched)
				{
					LatchedIn = FVector2D(PrevT, T);
				}
				PrevT = T;
				if (G->GetReloadRemaining() <= 0.0f && T > 0.2f)
				{
					// The latch: the catch CloseSeconds before the end, shut at Close's last key.
					const float Length = 2.0f * Lead + Gun.ReloadSeconds;
					const float Latch = Length - L.CloseSeconds + L.Close.GetRichCurveConst()->GetLastKey().Time;
					UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: reload - struck in %.3f..%.3f s, on its stop in %.3f..%.3f s, %.1f deg at least while loading, latched in %.3f..%.3f s"),
						StruckIn.X, StruckIn.Y, OpenIn.X, OpenIn.Y, LowWhileLoading, LatchedIn.X, LatchedIn.Y);
					Check(StruckIn.X < L.StrikeSeconds && L.StrikeSeconds <= StruckIn.Y + 0.001f, TEXT("the palm strikes the latch on time"));
					Check(OpenIn.Y > L.StrikeSeconds && OpenIn.Y < Lead, TEXT("the lid flies open onto its stop before the first round"));
					Check(LowWhileLoading >= L.ClearDeg - 0.5f, TEXT("the lid stays clear of the round"));
					Check(LatchedIn.X < Latch && Latch <= LatchedIn.Y + 0.001f && L.Angle == 0.0f && Gun.Ammo == 6, TEXT("loaded and latched shut on time"));
					Next();
				}
				break;
			}
			case 3: // a reload broken off by a shot: the lid left open
				if (Since() >= 0.1f && G->GetReloadRemaining() <= 0.0f && Gun.Ammo == 6)
				{
					Check(Shoot(), TEXT("a second shot"));
				}
				else if (Since() >= Gun.GetActionCycleSeconds() + 0.25f && G->GetReloadRemaining() <= 0.0f && Gun.Ammo == 5)
				{
					G->Reload();
					ReloadStart = World->GetTimeSeconds();
				}
				else if (Gun.Ammo == 5 && G->GetReloadRemaining() > 0.0f && World->GetTimeSeconds() - ReloadStart >= Lead + 0.05f)
				{
					Check(Shoot() && G->GetReloadRemaining() <= 0.0f, TEXT("a shot breaks the loading off"));
					Next();
				}
				break;
			case 4: // left open, it swings free; zero-G from here
				if (Since() >= 0.5f)
				{
					// Not "on the sight": the forward stroke yanks the gun out, muzzle down, and throws an open lid shut
					// onto its kicker and back - where it is 0.5 s on depends on the frame the shot fell in.
					Check(L.State == EJamLidState::Free && L.Angle > 1.0f, FString::Printf(TEXT("the lid is left open and free (%.1f deg)"), L.Angle));
					JamSetGravityScale(0.0f);
					Next();
				}
				break;
			case 5: // zero-G: it floats to its spring's rest
				if (Since() >= 3.0f)
				{
					Rest = L.Angle;
					Check(FMath::Abs(Rest - L.SpringRestDeg) < 4.0f, FString::Printf(TEXT("zero-G: it floats near its spring's rest (%.1f deg)"), Rest));
					Low = 1000.0f;
					High = -1000.0f;
					PitchBase = Controller->GetControlRotation();
					Next();
				}
				break;
			case 6: // a flick of the view up and back down (0.12 s each way): the lid swings
				Pitch(0.0f, 15.0f, 0.12f);
				Low = FMath::Min(Low, L.Angle);
				High = FMath::Max(High, L.Angle);
				if (Since() >= 0.12f)
				{
					Next();
				}
				break;
			case 7:
				Pitch(15.0f, 0.0f, 0.12f);
				Low = FMath::Min(Low, L.Angle);
				High = FMath::Max(High, L.Angle);
				if (Since() >= 0.12f)
				{
					Next();
				}
				break;
			case 8:
				Low = FMath::Min(Low, L.Angle);
				High = FMath::Max(High, L.Angle);
				if (Since() >= 2.5f)
				{
					UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: zero-G flick - rest %.1f, swung %.1f..%.1f deg, %.1f deg 2.5 s after"), Rest, Low, High, L.Angle);
					Check(High - Low > 8.0f, TEXT("zero-G: the view's flick swings the lid"));
					Check(FMath::Abs(L.Angle - L.SpringRestDeg) < 6.0f, TEXT("zero-G: it settles back"));
					JamSetGravityScale(1.0f);
					Controller->SetControlRotation(Level);
					PitchBase = Level;
					bLidPlaced = false;
					Next();
				}
				break;
			case 9: // gravity, level: from upright it lies on the rear sight
				// Gravity on stands the pawn up: the view rises on an ease, slowing at ~1.8 g, and an open lid is thrown
				// over the top by it - honestly. So once the pawn stands, the lid is put upright, at rest, and let go.
				if (!bLidPlaced && Since() >= 1.5f)
				{
					FJamLid& Free = G->Weapons[1].Lid;
					Free.State = EJamLidState::Free;
					Free.Angle = Free.SpringRestDeg;
					Free.Speed = 0.0f;
					bLidPlaced = true;
				}
				else if (Since() >= 2.5f)
				{
					Check(L.Angle >= L.OpenStopDeg - 0.5f, FString::Printf(TEXT("gravity, level: from upright it lies on the rear sight (%.1f deg)"), L.Angle));
					Next();
				}
				break;
			case 10: // gravity, looking up: it falls back toward the receiver
				Pitch(0.0f, 45.0f, 0.3f);
				if (Since() >= 2.0f)
				{
					Check(L.Angle < L.ClearDeg, FString::Printf(TEXT("gravity, looking up: it falls back onto its kicker (%.1f deg)"), L.Angle));
					Controller->SetControlRotation(Level);
					// Full, the lid open: a reload only shuts it.
					G->Weapons[1].Ammo = G->Weapons[1].MagazineSize;
					G->Reload();
					Check(FMath::IsNearlyEqual(G->GetReloadRemaining(), 2.0f * Lead, 0.02f), FString::Printf(TEXT("full with the lid open, a reload only shuts it (%.2f s)"),
						G->GetReloadRemaining()));
					Next();
				}
				break;
			case 11:
				if (Since() >= 2.0f * Lead + 0.1f)
				{
					Check(L.State == EJamLidState::Latched && Gun.Ammo == Gun.MagazineSize, TEXT("...and it is latched"));
					IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.ReloadPoseAt"))->Set(0.6f);
					Next();
				}
				break;
			case 12: // a held still: the lid open in it
				if (Since() >= 0.1f)
				{
					const float Angle = FMath::RadiansToDegrees(G->LidInView->GetRelativeRotation().Quaternion().GetTwistAngle(FVector::RightVector));
					Check(Angle >= L.ClearDeg - 0.5f && L.State == EJamLidState::Latched, FString::Printf(TEXT("a held reload still shows it open (%.1f deg), the gun's own lid untouched"), Angle));
					IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.ReloadPoseAt"))->Set(-1.0f);
					Finish();
					return false;
				}
				break;
			default:
				return false;
			}
			return true;
		}
	};

	void RunRmb93Probe(UWorld* World)
	{
		APlayerController* PC = World ? World->GetFirstPlayerController() : nullptr;
		APawn* Pawn = PC ? PC->GetPawn() : nullptr;
		UJamWeaponsComponent* Guns = Pawn ? Pawn->FindComponentByClass<UJamWeaponsComponent>() : nullptr;
		if (!World || !World->IsGameWorld() || !Guns || !Guns->ViewModel || !Guns->LidInView || !Guns->Scope || !Guns->Weapons.IsValidIndex(1))
		{
			UE_LOG(LogMindKore, Error, TEXT("Rmb93 probe: FAIL - needs a game world with the player's guns (Lvl_Jam_TestRoom or Lvl_Jam_ZeroG)"));
			UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: done"));
			return;
		}
		const TSharedRef<FRmb93Probe> Probe = MakeShared<FRmb93Probe>();
		Probe->World = World;
		Probe->Controller = PC;
		Probe->Guns = Guns;
		Probe->Fx = Pawn->FindComponentByClass<UJamMuzzleFxComponent>();
		Probe->SavedWeapons = Guns->Weapons;
		Probe->SavedAims = Guns->Scope->Aims;
		Probe->SavedCurrent = Guns->CurrentWeapon;
		Probe->SavedGravityScale = JamGetGravityScale();
		Probe->SavedView = PC->GetControlRotation();
		Probe->Level = FRotator(0.0f, Probe->SavedView.Yaw, 0.0f);
		PC->SetControlRotation(Probe->Level);

		const FJamWeapon Rmb = JamMakeRmb93(GetDefault<UJamWeaponsComponent>()->Weapons[1]);
		Probe->Check(Rmb.ViewMesh && Rmb.ActionMesh && Rmb.Lid.Mesh,
			TEXT("the RMB-93's meshes load - the lid's too (SM_Jam_RMB93_Lid, Scripts/jam_rmb93_assets.py)"));
		Probe->Check(Rmb.PortSmoke.Ports.Num() == 16 && Rmb.ShellMesh && Rmb.bEjectAlongBore, TEXT("16 shroud holes, round-by-round, bottom ejection"));
		if (!Rmb.Lid.Mesh)
		{
			Probe->Finish();
			return;
		}
		Guns->ApplyPickupWeapon(1, Rmb);
		Guns->Scope->Aims[1] = JamMakeRmb93Aim(GetDefault<UJamScopeComponent>()->Aims[1]);
		Guns->Weapons[1].Reserve = 24;
		Guns->UnlockWeapon(1);
		Guns->FinishSwitch();
		Probe->StageStart = World->GetTimeSeconds();
		Probe->Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateLambda([Probe](float)
		{
			return Probe->Tick();
		}));
		UE_LOG(LogMindKore, Log, TEXT("Rmb93 probe: started (~20 s of world time)"));
	}

	FAutoConsoleCommandWithWorld GRmb93Probe(
		TEXT("jam.Rmb93.Probe"),
		TEXT("The RMB-93 through its handling on world time: shot, hull, shroud puff, reload with the lid, a broken-off reload, the lid in zero-G and gravity, a lid-only reload, a still. ~20 s; Lvl_Jam_TestRoom or Lvl_Jam_ZeroG, NullRHI fine; ends \"Rmb93 probe: done\"."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunRmb93Probe));
}
#endif

#undef LOCTEXT_NAMESPACE
