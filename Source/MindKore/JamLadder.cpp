#include "JamLadder.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "Engine/OverlapResult.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "JamZeroGPawn.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#endif

AJamLadder::AJamLadder()
{
	Target = CreateDefaultSubobject<UBoxComponent>(TEXT("Target"));
	SetRootComponent(Target);
	Target->SetBoxExtent(FVector(42, 55, 240));
	Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Target->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<USoundBase> Rung(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Armor.SC_MK_Hit_Armor"));
	RungSound = Rung.Object;
}

bool AJamLadder::IsInReach(const FVector& Position) const
{
	const FVector Local = Target->GetComponentTransform().InverseTransformPosition(Position);
	const FVector Extent = Target->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y
		&& Local.Z >= Bottom.Z - 20 && Local.Z <= Top.Z + 5;
}

float AJamLadder::ClimbSign(const FVector& ViewDirection) const
{
	const FVector Local = GetActorTransform().InverseTransformVectorNoScale(ViewDirection);
	return Local.Z >= -FMath::Sin(FMath::DegreesToRadians(ClimbDownPitchDeg)) * Local.Size() ? 1.0f : -1.0f;
}

bool AJamLadder::CanEnter(const FVector& Position, const FVector& MoveDirection, bool bOnGround) const
{
	if (MoveDirection.IsNearlyZero() || !IsInReach(Position))
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Position);
	const FVector Move = GetActorTransform().InverseTransformVectorNoScale(MoveDirection);
	const float Rise = ClimbSign(MoveDirection);
	if (bOnGround && (Rise < 0 || Move.Y <= 0.05))
	{
		return false;
	}
	return Move.Y >= -0.1 && !(Local.Z >= Top.Z - 10 && Rise > 0) && !(Local.Z <= Bottom.Z + 10 && Rise < 0);
}

// --- console probe ----------------------------------------------------------------

namespace
{
	struct FLadderProbeStep
	{
		const TCHAR* Name;
		float Pitch;               // view pitch, degrees
		FVector Input;             // X forward (W/S), Y right (D/A)
		float Seconds;             // the step's length, or its limit with an Until
		int32 Until = 0;           // 0 time, 1 until on the ladder, 2 until off it, 3 until up at Top.Z (the mount starts)
		bool bJump = false;        // Space at the start
		bool bTowardAxis = false;  // yaw toward the ladder from where the pawn is at the step's start, else along its face
		bool bShot = false;        // HighResShot at the end (with the "shots" argument)
	};

	/**
	 * jam.Ladder.Probe [shots]: the first AJamLadder, the player put on its Bottom
	 * landing, then scripted keys and view: grab, climb up looking up, release,
	 * down looking down, S both ways, the dead zone, strafe, over the top, back
	 * into the well and down to the floor, grab again, jump off, strafe off.
	 * Every 0.2 s: position in the ladder's frame (X along the rungs, Y out of
	 * the face toward the ladder, Z up), the speed from that position's change,
	 * the body's physics velocity, on/off. "shots": HighResShot stills of the
	 * climb and the mount (needs a real RHI). "trace": a line every frame - the mode,
	 * the position and velocity in the ladder's frame, the ground, IsInReach/CanEnter,
	 * the body's contacts. Run it at a fixed rate too (-UseFixedTimeStep -FPS=60).
	 */
	void RunLadderProbe(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		TActorIterator<AJamLadder> Found(World);
		AJamLadder* Ladder = Found ? *Found : nullptr;
		if (!Pawn || !Ladder)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ladder probe: needs the player's AJamZeroGPawn and an AJamLadder"));
			return;
		}
		const bool bShots = Args.Contains(TEXT("shots"));
		const bool bTrace = Args.Contains(TEXT("trace"));
		const FTransform Frame = Ladder->GetActorTransform();
		UE_LOG(LogMindKore, Log, TEXT("Ladder probe: %s at %s rotation %s, reach box %s, Top %s (world %s), Bottom %s (world %s), %.0f cm/s, dead zone %.0f deg, rung every %.0f cm"),
			*Ladder->GetName(), *Frame.GetLocation().ToCompactString(), *Frame.Rotator().ToCompactString(),
			*Ladder->Target->GetUnscaledBoxExtent().ToCompactString(), *Ladder->Top.ToCompactString(),
			*Frame.TransformPosition(Ladder->Top).ToCompactString(), *Ladder->Bottom.ToCompactString(),
			*Frame.TransformPosition(Ladder->Bottom).ToCompactString(), Ladder->ClimbSpeedCm, Ladder->ClimbDownPitchDeg, Ladder->RungStepCm);
		Pawn->Body->SetWorldLocation(Frame.TransformPosition(Ladder->Bottom), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);

		const FVector W(1, 0, 0), S(-1, 0, 0), D(0, 1, 0), A(0, -1, 0), None(0, 0, 0);
		static TArray<FLadderProbeStep> Steps;
		Steps = {
			{ TEXT("grab: W, view level"), 0, W, 2.0f, 1 },
			{ TEXT("climb up: W, view 30 up"), 30, W, 0.7f },
			{ TEXT("keys released"), 30, None, 0.4f },
			{ TEXT("climb down: W, view 40 down"), -40, W, 0.3f, 0, false, false, true },
			{ TEXT("S, view 30 up: down"), 30, S, 0.2f, 0, false, false, true },
			{ TEXT("S, view 40 down: up"), -40, S, 0.3f },
			{ TEXT("dead zone: W, view 10 down: up"), -10, W, 0.3f },
			{ TEXT("strafe D"), 0, D, 0.1f },
			{ TEXT("strafe A, to the side under the hatch"), 0, A, 0.2f },
			{ TEXT("to the lip: W, view level"), 0, W, 3.0f, 3 },
			{ TEXT("over the lip: W, view level"), 0, W, 0.12f, 0, false, false, true },
			{ TEXT("over the top: W, view level"), 0, W, 6.0f, 2 },
			{ TEXT("on the landing"), 0, None, 1.0f, 0, false, false, true },
			{ TEXT("looking down the well"), -50, None, 0.6f, 0, false, true, true },
			{ TEXT("into the well: W, view 60 down"), -60, W, 3.0f, 1, false, true },
			{ TEXT("down to the floor: W, view 60 down"), -60, W, 8.0f, 2 },
			{ TEXT("standing at the foot"), 0, None, 1.0f },
			{ TEXT("grab again: W, view level"), 0, W, 2.0f, 1 },
			{ TEXT("climb: W, view 30 up"), 30, W, 0.5f },
			{ TEXT("jump off: Space"), 0, None, 1.5f, 0, true },
			{ TEXT("grab again: W, view level"), 0, W, 3.0f, 1 },
			{ TEXT("climb: W, view 30 up"), 30, W, 0.4f },
			{ TEXT("strafe off: D"), 0, D, 2.0f, 2 },
			{ TEXT("after the strafe off"), 0, None, 1.0f },
		};
		struct FState
		{
			int32 Step = -1;
			double StepStart = 0;
			double LastSample = 0;
			FVector LastLocal = FVector::ZeroVector;
			double HoldUntil = 0; // a still being taken: the view held, no keys
			uint64 LastFrame = 0;
			FVector LastInput = FVector::ZeroVector;
			int32 YawStep = -1;
			double Yaw = 0;
		};
		const TSharedRef<FState> State = MakeShared<FState>();
		State->StepStart = World->GetTimeSeconds();
		const TSharedRef<FTimerHandle> Handle = MakeShared<FTimerHandle>();
		const TWeakObjectPtr<AJamLadder> WeakLadder = Ladder;
		World->GetTimerManager().SetTimer(*Handle, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Controller, WeakLadder, State, Handle, bShots, bTrace]()
		{
			UWorld* World = Pawn->GetWorld();
			const AJamLadder* Ladder = WeakLadder.Get();
			if (!Ladder)
			{
				World->GetTimerManager().ClearTimer(*Handle);
				return;
			}
			const FTransform& Frame = Ladder->GetActorTransform();
			const double Now = World->GetTimeSeconds();
			const FVector Local = Frame.InverseTransformPosition(Pawn->GetActorLocation());
			if (bTrace && State->LastFrame != GFrameCounter)
			{
				State->LastFrame = GFrameCounter;
				const FVector View = Controller->GetControlRotation().Vector();
				const FVector Right = FRotationMatrix(Controller->GetControlRotation()).GetUnitAxis(EAxis::Y);
				const FVector Move = View * State->LastInput.X + Right * State->LastInput.Y;
				const FVector Velocity = Frame.InverseTransformVectorNoScale(Pawn->GetBodyVelocity());
				const float Radius = Pawn->Body->GetScaledSphereRadius();
				FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderTrace), false, Pawn);
				FHitResult Ground;
				const bool bGround = World->SweepSingleByChannel(Ground, Pawn->GetActorLocation(), Pawn->GetActorLocation() + Pawn->GetGravityDirection() * Pawn->GroundProbeCm,
					FQuat::Identity, Pawn->Body->GetCollisionObjectType(), FCollisionShape::MakeSphere(Radius - 2.0f), Params);
				// The ground as the pawn's ladder check sees it next tick (ProbeGround from here, outside the 0.25 s after a jump); "grounded" is the flag Walk left.
				const bool bFloor = bGround && FVector::DotProduct(Ground.ImpactNormal, -Pawn->GetGravityDirection()) >= FMath::Cos(FMath::DegreesToRadians(Pawn->MaxGroundAngleDeg));
				TArray<FOverlapResult> Touching;
				World->OverlapMultiByChannel(Touching, Pawn->GetActorLocation(), FQuat::Identity, Pawn->Body->GetCollisionObjectType(),
					FCollisionShape::MakeSphere(Radius + 1.0f), Params);
				FString Contacts;
				for (const FOverlapResult& Overlap : Touching)
				{
					FMTDResult Mtd;
					const bool bIn = Overlap.Component.IsValid() && Overlap.Component->ComputePenetration(Mtd, FCollisionShape::MakeSphere(Radius + 1.0f), Pawn->GetActorLocation(), FQuat::Identity);
					Contacts += FString::Printf(TEXT(" %s%s"), *GetNameSafe(Overlap.Component.Get()),
						bIn ? *FString::Printf(TEXT("[n %s, %.1f]"), *Frame.InverseTransformVectorNoScale(Mtd.Direction).ToCompactString(), Mtd.Distance - 1.0f) : TEXT(""));
				}
				UE_LOG(LogMindKore, Log, TEXT("Ladder trace f%llu dt %.4f [%s]: mode %d, local (%.1f, %.1f, %.1f), vel (%.0f, %.0f, %.0f), %s, reach %d, enter next tick %d (move local %s), radius %.0f, ground sweep %s%s, touching:%s"),
					GFrameCounter, World->GetDeltaSeconds(), State->Step >= 0 ? Steps[State->Step].Name : TEXT("settle"), int32(Pawn->GetMoveMode()),
					Local.X, Local.Y, Local.Z, Velocity.X, Velocity.Y, Velocity.Z, Pawn->IsGrounded() ? TEXT("grounded") : TEXT("air"),
					Ladder->IsInReach(Pawn->GetActorLocation()), Ladder->CanEnter(Pawn->GetActorLocation(), Move, bFloor),
					*Frame.InverseTransformVectorNoScale(Move).ToCompactString(), Radius,
					bGround ? *GetNameSafe(Ground.GetComponent()) : TEXT("none"),
					bGround ? *FString::Printf(TEXT(" n %s t %.2f%s"), *Frame.InverseTransformVectorNoScale(Ground.ImpactNormal).ToCompactString(), Ground.Time, Ground.bStartPenetrating ? TEXT(" inside") : TEXT("")) : TEXT(""),
					*Contacts);
			}
			const auto Sample = [&](const TCHAR* Note)
			{
				const double Since = FMath::Max(Now - State->LastSample, 1e-3);
				const FVector Moved = Frame.TransformVectorNoScale(Local - State->LastLocal) / Since;
				const FVector Physics = Frame.InverseTransformVectorNoScale(Pawn->GetBodyVelocity());
				UE_LOG(LogMindKore, Log, TEXT("Ladder probe [%s +%.2f s]%s: local (%.0f, %.0f, %.0f), moving %.0f cm/s (local %.0f, %.0f, %.0f), body velocity local (%.0f, %.0f, %.0f), %s, %s, gravity %s"),
					State->Step >= 0 ? Steps[State->Step].Name : TEXT("settle"), Now - State->StepStart, Note,
					Local.X, Local.Y, Local.Z, Moved.Size(), (Local - State->LastLocal).X / Since, (Local - State->LastLocal).Y / Since,
					(Local - State->LastLocal).Z / Since, Physics.X, Physics.Y, Physics.Z,
					Pawn->IsClimbingLadder() ? TEXT("ON the ladder") : TEXT("off"), Pawn->IsGrounded() ? TEXT("grounded") : TEXT("not grounded"),
					*Pawn->GetGravityDirection().ToCompactString());
				State->LastSample = Now;
				State->LastLocal = Local;
			};
			if (State->Step < 0)
			{
				if (Now - State->StepStart < 1.5)
				{
					return; // settle on the bottom landing
				}
				Sample(TEXT(" start"));
				State->Step = 0;
				State->StepStart = Now;
				if (Steps[0].bJump) { Pawn->JumpPressed(); }
			}
			if (Now < State->HoldUntil)
			{
				Pawn->ProbeThrust(FVector::ZeroVector, false);
				return;
			}
			const FLadderProbeStep& Step = Steps[State->Step];
			const double Elapsed = Now - State->StepStart;
			const bool bDone = Elapsed >= Step.Seconds || (Step.Until == 1 && Pawn->IsClimbingLadder()) || (Step.Until == 2 && !Pawn->IsClimbingLadder())
				|| (Step.Until == 3 && Local.Z >= Ladder->Top.Z - 1);
			if (bDone)
			{
				Sample(Step.Until == 0 ? TEXT(" end") : Elapsed >= Step.Seconds ? TEXT(" end: TIMEOUT") : TEXT(" end: reached"));
				if (bShots && Step.bShot)
				{
					UE_LOG(LogMindKore, Log, TEXT("Ladder probe: HighResShot at [%s]"), Step.Name);
					Controller->ConsoleCommand(TEXT("HighResShot 1"));
					State->HoldUntil = Now + 0.3;
				}
				if (++State->Step >= Steps.Num())
				{
					Pawn->ProbeThrust(FVector::ZeroVector, false);
					World->GetTimerManager().ClearTimer(*Handle);
					UE_LOG(LogMindKore, Log, TEXT("Ladder probe: done"));
					return;
				}
				State->StepStart = FMath::Max(Now, State->HoldUntil);
				if (Steps[State->Step].bJump)
				{
					Pawn->JumpPressed();
					Sample(TEXT(" Space pressed"));
				}
				return;
			}
			if (Now - State->LastSample >= 0.2)
			{
				Sample(TEXT(""));
			}
			if (State->YawStep != State->Step)
			{
				// Turned once as the step starts, as a player turns to the hole and walks: re-aimed every
				// tick, the yaw flipped 180 degrees as the body passed over the axis, and W led away from
				// the ladder; at 30 FPS no tick fell between leaving the lip and the axis.
				State->YawStep = State->Step;
				const FVector Axis = Frame.GetLocation() - Pawn->GetActorLocation();
				State->Yaw = (Step.bTowardAxis ? FVector(Axis.X, Axis.Y, 0).GetSafeNormal() : Frame.GetUnitAxis(EAxis::Y)).Rotation().Yaw;
			}
			Controller->SetControlRotation(FRotator(Step.Pitch, State->Yaw, 0));
			Pawn->ProbeThrust(Step.Input, false);
			State->LastInput = Step.Input;
		}), 0.0005f, true);
	}

	FAutoConsoleCommandWithWorldAndArgs GLadderProbe(
		TEXT("jam.Ladder.Probe"),
		TEXT("The first ladder: grab, climb by the view, S, dead zone, strafe, over the top, down to the floor, jump off, strafe off; logs position and speed. 'shots': stills (needs a real RHI); 'trace': a line every frame (mode, ground, CanEnter, contacts)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunLadderProbe));
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJamLadderEntryTest, "Jam.Ladder.MovementEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJamLadderEntryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	AJamLadder* Ladder = World->SpawnActor<AJamLadder>();
	const FVector Top(0, 0, Ladder->Top.Z);
	const FVector Bottom(0, 0, Ladder->Bottom.Z);
	TestTrue(TEXT("Looking down descends"), Ladder->CanEnter(Top, FVector(0, 1, -1)));
	TestTrue(TEXT("Looking up ascends"), Ladder->CanEnter(Bottom, FVector(0, 1, 1)));
	TestTrue(TEXT("Level at the foot ascends (dead zone)"), Ladder->CanEnter(Bottom, FVector(0, 1, 0), true));
	TestFalse(TEXT("Looking down on a floor does not grab"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, 1, -1), true));
	TestFalse(TEXT("Strafing past the foot does not grab"), Ladder->CanEnter(Bottom, FVector(1, 0, 0), true));
	TestTrue(TEXT("Falling onto ladder grabs"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, 0, -1)));
	TestTrue(TEXT("Jumping onto ladder grabs"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, 1, 1)));
	TestFalse(TEXT("No motion does not grab"), Ladder->CanEnter(FVector::ZeroVector, FVector::ZeroVector));
	TestFalse(TEXT("Moving away does not grab"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, -1, 0)));
	TestFalse(TEXT("Distant landing does not pull player"), Ladder->CanEnter(Ladder->Top, FVector(0, 1, -1)));
	TestFalse(TEXT("At top upward motion stays free"), Ladder->CanEnter(Top, FVector(0, 1, 1)));
	TestEqual(TEXT("Level view climbs up"), Ladder->ClimbSign(FVector(0, 1, 0)), 1.0f);
	TestEqual(TEXT("10 deg down climbs up"), Ladder->ClimbSign(FRotator(-10, 90, 0).Vector()), 1.0f);
	TestEqual(TEXT("30 deg down climbs down"), Ladder->ClimbSign(FRotator(-30, 90, 0).Vector()), -1.0f);
	World->DestroyWorld(false);
	return true;
}
#endif
