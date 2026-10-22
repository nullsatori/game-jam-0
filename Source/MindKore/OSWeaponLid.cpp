// A gun's hinged loading lid and its perforated shroud's smoke - the RMB-93's (OSRmb93.cpp,
// Docs/RecoilWeapons-Rmb93.md). Members of UOSWeaponsComponent (OSWeapons.h), apart from
// OSWeaponParts.cpp: the lid is a body on its hinge, not a curve.

#include "OSWeapons.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformTime.h"
#include "OSGravity.h"
#include "OSMuzzleFx.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"

namespace
{
	/** The lid's steps: its kicker rings at 6 Hz, its knocks are on a stop - small steps keep both the same at any frame rate. */
	constexpr float LidStepSeconds = 1.0f / 240.0f;

	/** Rounds a held reload pose loads (os.Weapons.ReloadPoseAt), as OSWeaponParts.cpp's. */
	constexpr int32 LidPoseShells = 2;

	float LastKey(const FRuntimeFloatCurve& Curve)
	{
		const FRichCurve* Rich = Curve.GetRichCurveConst();
		return Rich && Rich->GetNumKeys() > 0 ? Rich->GetLastKey().Time : 0.0f;
	}

	bool LidCrossed(float Before, float Now, float At)
	{
		return Before < At && Now >= At;
	}

	/** What the lid feels in the gun's frame. */
	struct FLidPull
	{
		/** Gravity less the hinge's own acceleration, cm/s^2. */
		FVector Accel = FVector::ZeroVector;
		/** The gun's spin and its change, rad/s and rad/s^2. */
		FVector Spin = FVector::ZeroVector;
		FVector SpinRate = FVector::ZeroVector;
	};

	/** What a run of the lid did: its hardest knock on a stop (deg/s), the strike and the latch. */
	struct FLidRun
	{
		float Knock = 0.0f;
		bool bStrikeSound = false;
		bool bLatchSound = false;
		bool bStruck = false;
		bool bLatched = false;
	};

	/** The lid free on its hinge for Dt (semi-implicit Euler); returns how fast it hit a stop, deg/s (0: none). */
	float SwingLid(FOSLid& Lid, const FLidPull& Pull, float Dt)
	{
		const float Theta = FMath::DegreesToRadians(Lid.Angle);
		float Speed = FMath::DegreesToRadians(Lid.Speed);
		// The plate from the hinge (its rear edge up: positive about Y), its centre half-way, I = m L^2 / 3: a push F on
		// its centre turns it by (L/2 d x F) / I = 3 / (2 L) (d x F). The gun turning under it turns it back, its spin flings it out.
		const FVector D(-FMath::Cos(Theta), 0.0f, FMath::Sin(Theta));
		const float Spring = 2.0f * PI * Lid.SpringHz;
		float Accel = 1.5f / Lid.LengthCm * FVector::CrossProduct(D, Pull.Accel).Y
			- Pull.SpinRate.Y
			- (Pull.Spin | D) * FVector::CrossProduct(D, Pull.Spin).Y
			- Spring * (Spring * (Theta - FMath::DegreesToRadians(Lid.SpringRestDeg)) + 2.0f * Lid.Damping * Speed);
		// The kicker leaf under it, over its first KickerDeg.
		if (const float Kicker = FMath::DegreesToRadians(Lid.KickerDeg); Theta < Kicker)
		{
			Accel += FMath::Square(2.0f * PI * Lid.KickerHz) * (Kicker - Theta);
		}
		Speed += Accel * Dt;
		float Angle = Lid.Angle + FMath::RadiansToDegrees(Speed * Dt);
		Lid.Speed = FMath::RadiansToDegrees(Speed);
		// The stops: the rear sight open, the receiver shut - or, held up in a reload, the round and the hand.
		float Knock = 0.0f;
		const float Floor = Lid.bHeldClear ? Lid.ClearDeg : 0.0f;
		if (Angle > Lid.OpenStopDeg)
		{
			Angle = Lid.OpenStopDeg;
			if (Lid.Speed > 0.0f)
			{
				Knock = Lid.Speed;
				Lid.Speed *= -Lid.OpenBounce;
			}
		}
		else if (Angle < Floor)
		{
			Angle = Floor;
			if (Lid.Speed < 0.0f)
			{
				Knock = -Lid.Speed;
				Lid.Speed *= -Lid.ShutBounce;
			}
		}
		Lid.Angle = Angle;
		return Knock;
	}

	/**
	 * Lid over reload time Reload0..Reload1 (< 0: no reload) and DeltaTime, in steps of at most LidStepSeconds, its
	 * events on the step they fall in: the palm on the latch at StrikeSeconds (bStrike, latched), a flick up at the
	 * first round when it lies below ClearDeg, the hand's catch CloseSeconds before ReloadLength and the press shut
	 * along Close (latched at its last key); free, it swings (SwingLid). A reload broken off with the lid open
	 * (bCatchPending): the hand catches it as shot time Shot0..Shot1 reaches CatchAtShot - the pump stroke through.
	 */
	void RunLid(FOSLid& Lid, float Reload0, float Reload1, float Lead, float ReloadLength, bool bStrike, float Shot0, float Shot1,
		float CatchAtShot, const FLidPull& Pull, float DeltaTime, FLidRun& Out)
	{
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / LidStepSeconds), 1, 2048);
		const float Dt = DeltaTime / Steps;
		const float CloseEnd = LastKey(Lid.Close);
		// The thumb's flick: what the kicker gives it.
		const float Flick = Lid.KickerDeg * 2.0f * PI * Lid.KickerHz;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float R0 = Reload1 >= 0.0f ? FMath::Lerp(Reload0, Reload1, float(Step) / Steps) : -1.0f;
			const float R1 = Reload1 >= 0.0f ? FMath::Lerp(Reload0, Reload1, float(Step + 1) / Steps) : -1.0f;
			if (R1 >= 0.0f)
			{
				if (bStrike && Lid.State == EOSLidState::Latched)
				{
					Out.bStrikeSound |= LidCrossed(R0, R1, Lid.StrikeSeconds - Lid.StrikeSoundLead);
					if (LidCrossed(R0, R1, Lid.StrikeSeconds))
					{
						Lid.State = EOSLidState::Free;
						Lid.Speed = 0.0f; // the kicker throws it
						Out.bStruck = true;
					}
				}
				if (Lid.State == EOSLidState::Free && Lid.Angle < Lid.ClearDeg && LidCrossed(R0, R1, Lead))
				{
					Lid.Speed = FMath::Max(Lid.Speed, Flick);
				}
				if (Lid.State == EOSLidState::Free && LidCrossed(R0, R1, ReloadLength - Lid.CloseSeconds))
				{
					Lid.State = EOSLidState::Hand;
					Lid.CatchAngle = Lid.Angle;
					Lid.SinceCatch = 0.0f;
				}
				Lid.bHeldClear |= Lid.State == EOSLidState::Free && Lid.Angle >= Lid.ClearDeg;
			}
			else
			{
				Lid.bHeldClear = false;
				if (Lid.bCatchPending && Lid.State == EOSLidState::Free
					&& FMath::Lerp(Shot0, Shot1, float(Step + 1) / Steps) >= CatchAtShot)
				{
					Lid.State = EOSLidState::Hand;
					Lid.CatchAngle = Lid.Angle;
					Lid.SinceCatch = 0.0f;
				}
			}
			if (Lid.State != EOSLidState::Free)
			{
				Lid.bCatchPending = false;
			}
			if (Lid.State == EOSLidState::Hand)
			{
				// The hand's press runs to its end even when the reload is broken off under it.
				const float Before = Lid.SinceCatch;
				Lid.SinceCatch += Dt;
				Out.bLatchSound |= LidCrossed(Before, Lid.SinceCatch, Lid.LatchSeconds - Lid.LatchSoundLead);
				const float Angle = Lid.CatchAngle * OSEvalCycle(Lid.Close, Lid.SinceCatch);
				Lid.Speed = (Angle - Lid.Angle) / Dt;
				Lid.Angle = Angle;
				if (Lid.SinceCatch >= CloseEnd)
				{
					Lid.State = EOSLidState::Latched;
					Lid.Angle = Lid.Speed = 0.0f;
					Lid.bHeldClear = false;
					Out.bLatched = true;
				}
			}
			else if (Lid.State == EOSLidState::Free)
			{
				Out.Knock = FMath::Max(Out.Knock, SwingLid(Lid, Pull, Dt));
			}
		}
	}

#if !UE_BUILD_SHIPPING
	/** What the lid and the shroud's smoke cost on the game thread (os.Rmb93.Cost). */
	struct FRmb93Cost
	{
		double Seconds = 0.0;
		double MaxSeconds = 0.0;
		int64 Frames = 0;
	};
	FRmb93Cost GRmb93Cost;

	FAutoConsoleCommand GRmb93CostCommand(
		TEXT("os.Rmb93.Cost"),
		TEXT("Log what the RMB-93's lid and shroud smoke (UpdateLid + UpdatePortSmoke) cost per frame with it in view since the last call, then start over."),
		FConsoleCommandDelegate::CreateLambda([]()
		{
			const double Frames = double(FMath::Max<int64>(GRmb93Cost.Frames, 1));
			UE_LOG(LogMindKore, Log, TEXT("Rmb93 cost: %lld frames, lid + shroud smoke avg %.2f us, max %.1f us"), GRmb93Cost.Frames,
				GRmb93Cost.Seconds / Frames * 1e6, GRmb93Cost.MaxSeconds * 1e6);
			GRmb93Cost = FRmb93Cost();
		}));

	/** Times one frame's lid and smoke (both calls add to the same frame: the lid starts it). */
	struct FRmb93CostScope
	{
		double Start = FPlatformTime::Seconds();
		bool bNewFrame = false;
		explicit FRmb93CostScope(bool bInNewFrame) : bNewFrame(bInNewFrame) {}
		~FRmb93CostScope()
		{
			const double Spent = FPlatformTime::Seconds() - Start;
			GRmb93Cost.Seconds += Spent;
			GRmb93Cost.MaxSeconds = FMath::Max(GRmb93Cost.MaxSeconds, Spent);
			GRmb93Cost.Frames += bNewFrame ? 1 : 0;
		}
	};
#endif
}

const FOSLid* UOSWeaponsComponent::GetShownLid() const
{
	return Weapons.IsValidIndex(ShownWeapon) && Weapons[ShownWeapon].Lid.Mesh ? &Weapons[ShownWeapon].Lid : nullptr;
}

FVector UOSWeaponsComponent::GetGunGravity()
{
	// A zone's gravity changes only at its edge: read four times a second, not every frame - and at once when the menu's
	// scale changes (otherwise the lid swings a quarter second in the old gravity).
	const double Now = GetWorld()->GetTimeSeconds();
	if (GunGravitySeconds < 0.0 || Now < GunGravitySeconds || Now - GunGravitySeconds >= 0.25 || GunGravityScale != OSGetGravityScale())
	{
		GunGravitySeconds = Now;
		GunGravityScale = OSGetGravityScale();
		// The test room's character walks in the world's gravity; the zero-G pawn is in the zones' (or none).
		GunGravity = Cast<ACharacter>(GetOwner())
			? FVector(0.0f, 0.0f, GetWorld()->GetGravityZ() * OSGetGravityScale())
			: OSGravityAt(GetWorld(), GetOwner()->GetActorLocation()).Acceleration;
	}
	return GunGravity;
}

void UOSWeaponsComponent::UpdateLid(FOSWeapon& Weapon, const FTransform& GunWorld, float DeltaTime)
{
	FOSLid& Lid = Weapon.Lid;
	if (!Lid.Mesh || !LidInView || DeltaTime <= 0.0f)
	{
		bLidShot = false;
		return;
	}
#if !UE_BUILD_SHIPPING
	FRmb93CostScope Cost(true);
#endif
	const float Lead = LastKey(Weapon.ReloadBody);
	const FVector Gravity = GetGunGravity();
	static IConsoleVariable* ReloadPoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Weapons.ReloadPoseAt"));
	if (const float HeldAt = ReloadPoseAt ? ReloadPoseAt->GetFloat() : -1.0f; HeldAt >= 0.0f)
	{
		// A still: the held reload from its start with the gun still in this gravity - the same every frame.
		FOSLid Still = Lid;
		Still.State = EOSLidState::Latched;
		Still.Angle = Still.Speed = 0.0f;
		Still.bHeldClear = false;
		Still.bCatchPending = false;
		FLidPull Pull;
		Pull.Accel = GunWorld.GetRotation().UnrotateVector(Gravity);
		FLidRun Run;
		RunLid(Still, 0.0f, HeldAt, Lead, 2.0f * Lead + LidPoseShells * Weapon.ReloadSeconds, true, -1.0f, -1.0f, 0.0f, Pull, HeldAt, Run);
		LidInView->SetRelativeLocationAndRotation(Lid.HingeCm, FQuat(FVector::RightVector, FMath::DegreesToRadians(Still.Angle)));
		LidTrackFrames = 0;
		return;
	}

	// The gun's motion by differences over the last ticks: the velocity a tick changes by is exact whatever the tick's length, so
	// the lid takes the same knock at any frame rate. Started afresh for a gun just shown or a jump (a teleport).
	const FVector Hinge = GunWorld.TransformPosition(Lid.HingeCm);
	const FQuat Rotation = GunWorld.GetRotation();
	if (LidTrackWeapon != ShownWeapon || FVector::DistSquared(Hinge, LidPrevHinge) > FMath::Square(100.0f))
	{
		LidTrackFrames = 0;
	}
	FVector Velocity = FVector::ZeroVector;
	FVector Spin = FVector::ZeroVector;
	FVector Accel = FVector::ZeroVector;
	FVector SpinRate = FVector::ZeroVector;
	if (LidTrackFrames >= 1)
	{
		Velocity = (Hinge - LidPrevHinge) / DeltaTime;
		FQuat Turn = Rotation * LidPrevRotation.Inverse();
		Turn.EnforceShortestArcWith(FQuat::Identity);
		Spin = Turn.GetRotationAxis() * (Turn.GetAngle() / DeltaTime);
		if (LidTrackFrames >= 2)
		{
			Accel = (Velocity - LidPrevVelocity) / DeltaTime;
			SpinRate = (Spin - LidPrevSpin) / DeltaTime;
		}
	}
	LidTrackWeapon = ShownWeapon;
	LidTrackFrames = FMath::Min(LidTrackFrames + 1, 2);
	LidPrevHinge = Hinge;
	LidPrevRotation = Rotation;
	LidPrevVelocity = Velocity;
	LidPrevSpin = Spin;
	FLidPull Pull;
	Pull.Accel = Rotation.UnrotateVector(Gravity - Lid.InertiaScale * Accel);
	Pull.Spin = Rotation.UnrotateVector(Spin) * FMath::Sqrt(Lid.InertiaScale);
	Pull.SpinRate = Rotation.UnrotateVector(SpinRate) * Lid.InertiaScale;

	// The drawn kick, as the jump it gives an open lid.
	if (bLidShot && Lid.State == EOSLidState::Free)
	{
		Lid.Speed += Lid.ShotSpeedDeg;
	}
	bLidShot = false;
	// Live reload time; a new reload from its start, so the strike is never stepped over.
	const float Reload = ReloadEndSeconds > 0.0 ? float(GetWorld()->GetTimeSeconds() - ReloadStartSeconds) : -1.0f;
	float From = LidPrevReload;
	if (Reload >= 0.0f && (From < 0.0f || Reload < From))
	{
		From = ReloadFromSeconds;
	}
	// Broken off (a shot, a switch) with the lid open: the hand shuts it once the pump stroke is through - left standing,
	// it read as broken (owner 22.10). A new reload takes it over instead.
	if (Reload < 0.0f && LidPrevReload >= 0.0f && Lid.State == EOSLidState::Free)
	{
		Lid.bCatchPending = true;
	}
	else if (Reload >= 0.0f)
	{
		Lid.bCatchPending = false;
	}
	LidPrevReload = Reload;
	FLidRun Run;
	RunLid(Lid, From, Reload, Lead, float(ReloadEndSeconds - ReloadStartSeconds), bReloadStrikesLid, FMath::Max(Weapon.SinceShot - DeltaTime, 0.0f),
		Weapon.SinceShot, Weapon.GetActionCycleSeconds(), Pull, DeltaTime, Run);

	if (Run.bStrikeSound && Lid.StrikeSound)
	{
		PlayGunSound(Lid.StrikeSound, 1.0f, Lid.StrikeSoundPitch);
	}
	if (Run.bStruck)
	{
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s lid struck open"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString());
	}
	if (Run.bLatchSound && Lid.LatchSound)
	{
		PlayGunSound(Lid.LatchSound, 1.0f, Lid.LatchSoundPitch);
	}
	if (Run.bLatched)
	{
		UE_LOG(LogMindKore, Log, TEXT("Weapon %s: %s lid latched"), *GetNameSafe(GetOwner()), *Weapon.Name.ToString());
	}
	// A knock that is a knock (not the lid resting on its stop), louder the harder; at most one sound per 40 ms.
	if (Run.Knock >= 0.05f * Lid.StopSoundFullDegPerSec)
	{
		++LidKnocks;
		const double Now = GetWorld()->GetTimeSeconds();
		if (Lid.StopSound && (Now - LidLastKnockSeconds >= 0.04 || Now < LidLastKnockSeconds))
		{
			PlayGunSound(Lid.StopSound, FMath::Clamp(Run.Knock / Lid.StopSoundFullDegPerSec, 0.15f, 1.0f), Lid.StopSoundPitch);
			LidLastKnockSeconds = Now;
		}
	}
	LidInView->SetRelativeLocationAndRotation(Lid.HingeCm, FQuat(FVector::RightVector, FMath::DegreesToRadians(Lid.Angle)));
}

void UOSWeaponsComponent::UpdatePortSmoke(FOSWeapon& Weapon, float DeltaTime)
{
	FOSPortSmoke& Smoke = Weapon.PortSmoke;
	UOSMuzzleFxComponent* Fx = MuzzleFx.Get();
	if (Smoke.Ports.IsEmpty() || !Fx || !ActionInView || DeltaTime <= 0.0f)
	{
		return;
	}
#if !UE_BUILD_SHIPPING
	FRmb93CostScope Cost(!Weapon.Lid.Mesh);
#endif
	// The seep goes as the heat: puffs per second, the fraction carried to the next frame (the same count at any rate).
	const float Heat = FMath::Min(Smoke.HeatAt(GetWorld()->GetTimeSeconds()), Smoke.MaxHeat);
	if (Heat < 0.02f)
	{
		Smoke.SeepCarry = 0.0f;
		return;
	}
	Smoke.SeepCarry += Smoke.SeepPerSecond * Heat * DeltaTime;
	if (const int32 Count = FMath::FloorToInt(Smoke.SeepCarry); Count > 0)
	{
		Smoke.SeepCarry -= Count;
		Fx->PortPuffs(Smoke, *ActionInView, Count, Smoke.SeepThrowCm, DeltaTime);
	}
}

void UOSWeaponsComponent::PortSmokeShot(FOSWeapon& Weapon)
{
	FOSPortSmoke& Smoke = Weapon.PortSmoke;
	if (Smoke.Ports.IsEmpty())
	{
		return;
	}
	const double Now = GetWorld()->GetTimeSeconds();
	Smoke.Heat = FMath::Min(Smoke.HeatAt(Now) + Smoke.HeatPerShot, Smoke.MaxHeat);
	Smoke.HeatTime = Now;
	if (UOSMuzzleFxComponent* Fx = MuzzleFx.Get(); Fx && ActionInView && ActionInView->GetStaticMesh())
	{
		Fx->PortPuffs(Smoke, *ActionInView, Smoke.ShotPuffs, Smoke.ShotThrowCm, 0.0f);
	}
}
