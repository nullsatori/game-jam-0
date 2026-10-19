#include "JamGravity.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerStart.h"
#include "HAL/IConsoleManager.h"
#include "JamFlyer.h"
#include "JamShip.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "MKImpactDelivery.h"
#include "MKImpactTypes.h"
#include "MKPhysicsProp.h"
#include "MindKore.h"
#include "TimerManager.h"

DECLARE_CYCLE_STAT(TEXT("Jam body gravity"), STAT_JamBodyGravity, STATGROUP_Game);

namespace
{
	const FName NoGravityTag(TEXT("Jam.NoGravity"));

	/** Slower than this, cm/s and deg/s, with something within RestGapCm under its bounds along gravity, a body rests. */
	constexpr float RestSpeedCm = 3.0f;
	constexpr float RestSpinDeg = 10.0f;
	constexpr float RestGapCm = 2.0f;

	// ponytail: one set for every world (one game world runs at a time); per world if PIE ever runs two.
	/** Bodies found at rest last frame. At rest two frames running, a body is put to sleep: in one frame
	 *  the velocity can still be 0 under a hit's impulse that the physics applies only after this pass. */
	TSet<TWeakObjectPtr<UPrimitiveComponent>> GRestingLastFrame;

	/** The rule: of the zones containing Location, the higher Priority, then the smaller zone. */
	const AJamGravityZone* BestZone(const UWorld* World, const FVector& Location, bool bWalkOnly)
	{
		const AJamGravityZone* Best = nullptr;
		double BestVolume = 0.0;
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			if ((bWalkOnly && !It->bWalk) || !It->Contains(Location, 0.0f))
			{
				continue;
			}
			const double Volume = It->GetVolumeCm3();
			if (!Best || It->Priority > Best->Priority || (It->Priority == Best->Priority && Volume < BestVolume))
			{
				Best = *It;
				BestVolume = Volume;
			}
		}
		return Best;
	}

	/** What the pull costs on the game thread, for jam.Gravity.Cost and the probe. */
	struct FPullCost
	{
		double Seconds = 0.0;
		double MaxSeconds = 0.0;
		double QuerySeconds = 0.0;
		int64 Frames = 0;
		int64 Pushed = 0;
	};
	FPullCost GPullCost;

	void LogPullCost(const TCHAR* Label)
	{
		const double Frames = double(FMath::Max<int64>(GPullCost.Frames, 1));
		UE_LOG(LogMindKore, Log, TEXT("Gravity cost [%s]: %lld frames, pull avg %.1f us (overlap queries %.1f), max %.1f us, %.1f bodies pushed per frame"), Label,
			GPullCost.Frames, GPullCost.Seconds / Frames * 1e6, GPullCost.QuerySeconds / Frames * 1e6, GPullCost.MaxSeconds * 1e6, double(GPullCost.Pushed) / Frames);
		GPullCost = FPullCost();
	}

	FAutoConsoleCommand GGravityCost(
		TEXT("jam.Gravity.Cost"),
		TEXT("Log what the zones' pull on loose bodies (JamPullBodies) cost per frame since the last call, then start over."),
		FConsoleCommandDelegate::CreateLambda([]() { LogPullCost(TEXT("jam.Gravity.Cost")); }));
}

FJamGravity JamGravityAt(const UWorld* World, const FVector& Location)
{
	FJamGravity Gravity;
	Gravity.Zone = World ? BestZone(World, Location, /*bWalkOnly=*/true) : nullptr;
	if (Gravity.Zone)
	{
		Gravity.Acceleration = Gravity.Zone->GravityAt(Location) * Gravity.Zone->GravityCm;
	}
	return Gravity;
}

const AJamGravityZone* JamAirZoneAt(const UWorld* World, const FVector& Location)
{
	return World ? BestZone(World, Location, /*bWalkOnly=*/false) : nullptr;
}

void JamPullBodies(UWorld* World)
{
	SCOPE_CYCLE_COUNTER(STAT_JamBodyGravity);
	const double Start = FPlatformTime::Seconds();
	// A map with world gravity (the test room) already pulls bodies that have it on: the zone replaces it.
	const FVector WorldGravity(0.0f, 0.0f, World->GetGravityZ());
	TArray<FOverlapResult> Overlaps;
	TSet<TWeakObjectPtr<UPrimitiveComponent>> RestingNow;
	int32 Pushed = 0;
	for (TActorIterator<AJamGravityZone> Zone(World); Zone; ++Zone)
	{
		if (!Zone->bWalk || Zone->GravityCm <= 0.0f)
		{
			continue;
		}
		FVector Centre;
		const FCollisionShape Shape = Zone->GetQueryShape(Centre);
		Overlaps.Reset();
		const double QueryStart = FPlatformTime::Seconds();
		World->OverlapMultiByObjectType(Overlaps, Centre, FQuat::Identity, FCollisionObjectQueryParams(ECC_PhysicsBody), Shape);
		GPullCost.QuerySeconds += FPlatformTime::Seconds() - QueryStart;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			UPrimitiveComponent* Body = Overlap.GetComponent();
			const AActor* Owner = Body ? Body->GetOwner() : nullptr;
			if (!Body || !Body->IsSimulatingPhysics() || !Body->IsGravityEnabled() || !Body->RigidBodyIsAwake()
				|| (Owner && Owner->ActorHasTag(NoGravityTag)))
			{
				continue;
			}
			// Where zones overlap a body is pushed once, by the zone that wins at its centre.
			const FVector Location = Body->GetComponentLocation();
			const FJamGravity Gravity = JamGravityAt(World, Location);
			if (Gravity.Zone != *Zone)
			{
				continue;
			}
			// At rest on something: no push, and asleep from the second such frame. A push wakes a body
			// (Chaos), and a pushed body never gets slow enough for Chaos to sleep it, so the pull does
			// it: a hit or a grab wakes it.
			if (Body->GetPhysicsLinearVelocity().SizeSquared() < FMath::Square(RestSpeedCm)
				&& Body->GetPhysicsAngularVelocityInDegrees().SizeSquared() < FMath::Square(RestSpinDeg))
			{
				const FVector Down = Gravity.Acceleration.GetSafeNormal();
				const FVector Extent = Body->Bounds.BoxExtent;
				const float Support = FMath::Abs(Down.X) * Extent.X + FMath::Abs(Down.Y) * Extent.Y + FMath::Abs(Down.Z) * Extent.Z;
				FHitResult Ground;
				if (World->LineTraceSingleByChannel(Ground, Location, Location + Down * (Support + RestGapCm), ECC_PhysicsBody,
					FCollisionQueryParams(SCENE_QUERY_STAT(JamGravityRest), false, Owner)))
				{
					if (GRestingLastFrame.Contains(Body))
					{
						Body->PutRigidBodyToSleep();
					}
					else
					{
						RestingNow.Add(Body);
					}
					continue;
				}
			}
			Body->AddForce(Gravity.Acceleration - WorldGravity, NAME_None, /*bAccelChange=*/true);
			++Pushed;
		}
	}
	GRestingLastFrame = MoveTemp(RestingNow);
	const double Seconds = FPlatformTime::Seconds() - Start;
	GPullCost.Seconds += Seconds;
	GPullCost.MaxSeconds = FMath::Max(GPullCost.MaxSeconds, Seconds);
	++GPullCost.Frames;
	GPullCost.Pushed += Pushed;
}

FHitResult JamPredictFlight(const UWorld* World, const FVector& Start, const FVector& Velocity, float RadiusCm,
	float Seconds, float StepSeconds, float Restitution, int32 Bounces, const FCollisionQueryParams& Params,
	TArray<FJamFlightPoint>& OutPath, float& OutHitSeconds)
{
	// The physics steps the frame's delta, capped at 1/30 s (no substepping in this project).
	const float Step = FMath::Clamp(StepSeconds, 1.0f / 240.0f, 1.0f / 30.0f);
	const int32 StepsPerSweep = FMath::Max(1, FMath::RoundToInt(0.1f / Step));
	FHitResult First;
	OutHitSeconds = 0.0f;
	OutPath.Reset();
	OutPath.Add({ Start, 0.0f });
	FVector Position = Start;
	FVector Speed = Velocity;
	float Time = 0.0f;
	while (Time < Seconds)
	{
		// ponytail: one gravity per ~0.1 s chunk, from where it starts (a planetoid's down turns < 1 deg over it).
		const FVector Accel = JamGravityAt(World, Position).Acceleration;
		FVector Next = Position;
		FVector NextSpeed = Speed;
		float NextTime = Time;
		for (int32 Index = 0; Index < StepsPerSweep && NextTime < Seconds; ++Index)
		{
			NextSpeed += Accel * Step;
			Next += NextSpeed * Step;
			NextTime += Step;
		}
		if (Next.Equals(Position, 0.01))
		{
			break; // at rest in zero-G: nothing more to draw
		}
		FHitResult Hit;
		if (!World->SweepSingleByChannel(Hit, Position, Next, FQuat::Identity, ECC_PhysicsBody, FCollisionShape::MakeSphere(RadiusCm), Params))
		{
			Position = Next;
			Speed = NextSpeed;
			Time = NextTime;
			OutPath.Add({ Position, Time });
			continue;
		}
		// The contact, partway through the chunk: time and speed there by the fraction swept.
		Time = FMath::Lerp(Time, NextTime, Hit.Time);
		Speed = FMath::Lerp(Speed, NextSpeed, Hit.Time);
		Position = Hit.Location;
		OutPath.Add({ Position, Time });
		if (!First.bBlockingHit)
		{
			First = Hit;
			OutHitSeconds = Time;
		}
		if (Bounces-- <= 0)
		{
			break;
		}
		const FVector Into = Hit.ImpactNormal * FVector::DotProduct(Speed, Hit.ImpactNormal);
		Speed = (Speed - Into) - Into * Restitution;
		Position += Hit.ImpactNormal * 0.5f;
	}
	return First;
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJamZonePriorityTest, "Jam.Gravity.ZonePriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJamZonePriorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	// A big room with a small box in its corner, and a sphere over both.
	AJamGravityZone* Room = World->SpawnActor<AJamGravityZone>();
	Room->Zone->SetBoxExtent(FVector(1000, 1000, 500));
	AJamGravityZone* Nook = World->SpawnActor<AJamGravityZone>(FVector(800, 0, 0), FRotator::ZeroRotator);
	Nook->Zone->SetBoxExtent(FVector(100, 100, 100));
	Nook->GravityCm = 300.0f;
	AJamGravityZone* Ball = World->SpawnActor<AJamGravityZone>(FVector(0, 0, 3000), FRotator::ZeroRotator);
	Ball->SphereRadiusCm = 3600.0f;
	Ball->bRadial = true;
	Ball->GravityCm = 500.0f;
	TestTrue(TEXT("Tie: the smaller zone (the room inside the ball)"), JamGravityAt(World, FVector::ZeroVector).Zone == Room);
	TestTrue(TEXT("Tie: the smallest (the nook)"), JamGravityAt(World, FVector(800, 0, 0)).Zone == Nook);
	TestEqual(TEXT("The nook's strength"), JamGravityAt(World, FVector(800, 0, 0)).Acceleration, FVector(0, 0, -300));
	Ball->Priority = 1;
	TestTrue(TEXT("Priority beats size"), JamGravityAt(World, FVector(800, 0, 0)).Zone == Ball);
	TestTrue(TEXT("Radial: toward the ball's centre"), JamGravityAt(World, FVector(800, 0, 0)).Acceleration.GetSafeNormal().Equals(FVector(-800, 0, 3000).GetSafeNormal(), 1e-4));
	Ball->Priority = 0;
	Room->bWalk = false;
	TestTrue(TEXT("Gravity skips an air-only zone"), JamGravityAt(World, FVector::ZeroVector).Zone == Ball);
	TestTrue(TEXT("Air takes it"), JamAirZoneAt(World, FVector::ZeroVector) == Room);
	TestTrue(TEXT("Outside every zone: no gravity"), JamGravityAt(World, FVector(0, 0, -5000)).Acceleration.IsZero());
	// A box zone rolled 90 deg about X: its down is world +Y (world down without the option).
	Nook->SetActorRotation(FRotator(0, 0, 90));
	TestEqual(TEXT("Box: world down by default"), Nook->GravityAt(FVector(800, 0, 0)), FVector::DownVector);
	Nook->bPullAlongActorDown = true;
	TestTrue(TEXT("Box: along its actor's down"), Nook->GravityAt(FVector(800, 0, 0)).Equals(-Nook->GetActorUpVector(), 1e-4));
	TestTrue(TEXT("Rolled actor's down is not world down"), !(-Nook->GetActorUpVector()).Equals(FVector::DownVector, 0.1));
	World->DestroyWorld(false);
	return true;
}
#endif

// --- probe -------------------------------------------------------------------------------

namespace
{
	void After(UWorld* World, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		World->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(World, MoveTemp(Step)), Seconds, false);
	}

	AJamZeroGPawn* PlayerPawn(const UWorld* World)
	{
		const APlayerController* PlayerController = World->GetFirstPlayerController();
		return PlayerController ? Cast<AJamZeroGPawn>(PlayerController->GetPawn()) : nullptr;
	}

	void Teleport(AActor* Actor, const FVector& Location)
	{
		if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Actor->GetRootComponent()); Root && Root->IsSimulatingPhysics())
		{
			Root->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
			Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			return;
		}
		Actor->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	}

	/** The body's lowest point over what is under it along gravity, cm (the bounds' support; -1: nothing within 50 m). */
	float GapBelow(const UPrimitiveComponent* Body)
	{
		UWorld* World = Body->GetWorld();
		const FVector Location = Body->GetComponentLocation();
		const FJamGravity Gravity = JamGravityAt(World, Location);
		const FVector Down = Gravity.Zone ? Gravity.Acceleration.GetSafeNormal() : FVector::DownVector;
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamGravityGap), false, Body->GetOwner());
		FHitResult Hit;
		if (!World->LineTraceSingleByChannel(Hit, Location, Location + Down * 5000.0f, ECC_Visibility, Params))
		{
			return -1.0f;
		}
		const FVector Extent = Body->Bounds.BoxExtent;
		return Hit.Distance - (FMath::Abs(Down.X) * Extent.X + FMath::Abs(Down.Y) * Extent.Y + FMath::Abs(Down.Z) * Extent.Z);
	}

	FString Describe(const UPrimitiveComponent* Body)
	{
		return FString::Printf(TEXT("%s gap %.1f cm, speed %.1f cm/s, %s"), *GetNameSafe(Body->GetOwner()), GapBelow(Body),
			Body->GetPhysicsLinearVelocity().Size(), Body->RigidBodyIsAwake() ? TEXT("awake") : TEXT("asleep"));
	}

	/** A loose box (AMKPhysicsProp, the engine cube) of SizeCm and MassKg, with Tag if given. */
	AMKPhysicsProp* SpawnBox(UWorld* World, const FVector& Where, float SizeCm, float MassKg, FName Tag = NAME_None)
	{
		const FTransform Transform(FRotator::ZeroRotator, Where, FVector(SizeCm / 100.0f));
		AMKPhysicsProp* Box = World->SpawnActorDeferred<AMKPhysicsProp>(AMKPhysicsProp::StaticClass(), Transform, nullptr, nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		Box->Body->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
		Box->MassKg = MassKg;
		if (!Tag.IsNone())
		{
			Box->Tags.Add(Tag);
		}
		Box->FinishSpawning(Transform);
		return Box;
	}

	/**
	 * Hold a 10 kg box, aim PitchDeg up, throw; every frame compare the box with
	 * the prediction at the same time since the throw (JamPredictFlight from the
	 * same state as the pawn's own path), then let it land. Then Next.
	 */
	void ProbeThrow(UWorld* World, const FString Label, float PitchDeg, TFunction<void()> Next)
	{
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		const FRotator View(PitchDeg, Pawn->GetControlRotation().Yaw, 0.0f);
		World->GetFirstPlayerController()->SetControlRotation(View);
		AMKPhysicsProp* Box = SpawnBox(World, Pawn->Camera->GetComponentLocation() + View.Vector() * 150.0f, 30.0f, 10.0f);
		Pawn->Grab(Box->Body, Box->Body->GetComponentLocation());
		After(World, 1.2f, [World, Label, Next, Box]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(World);
			Pawn->ProbeAim(true); // the pawn's own path logs its first contact this frame
			After(World, 0.1f, [World, Label, Next, Box]()
			{
				AJamZeroGPawn* Pawn = PlayerPawn(World);
				Pawn->ProbeAim(false);
				UPrimitiveComponent* Held = Pawn->GetGrabbed();
				if (Held != Box->Body)
				{
					UE_LOG(LogMindKore, Warning, TEXT("Gravity probe [%s throw]: the box is not held"), *Label);
					Next();
					return;
				}
				// The pawn's DrawPrediction inputs: the held velocity plus the throw's impulse.
				const float MassKg = Held->GetMass();
				const FVector Direction = Pawn->GetControlRotation().Vector();
				const FVector Start = Held->GetComponentLocation();
				const FVector Velocity = Held->GetPhysicsLinearVelocity() + Direction * FMath::Min(Pawn->ThrowImpulse, MassKg * Pawn->MaxThrowSpeedCm) / MassKg;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(JamGravityProbe), false, Pawn);
				Params.AddIgnoredActor(Box);
				TSharedRef<TArray<FJamFlightPoint>> Path = MakeShared<TArray<FJamFlightPoint>>();
				float HitSeconds = 0.0f;
				const FHitResult Hit = JamPredictFlight(World, Start, Velocity, Held->Bounds.SphereRadius, Pawn->PredictSeconds,
					World->GetDeltaSeconds(), Pawn->PredictRestitution, 0, Params, *Path, HitSeconds);
				UE_LOG(LogMindKore, Log, TEXT("Gravity probe [%s throw]: from %s at %s cm/s (%.0f cm/s), gravity %s, step %.4f s; predicted contact %s at %s in %.2f s"),
					*Label, *Start.ToCompactString(), *Velocity.ToCompactString(), Velocity.Size(), *JamGravityAt(World, Start).Acceleration.ToCompactString(),
					World->GetDeltaSeconds(), Hit.bBlockingHit ? *GetNameSafe(Hit.GetActor()) : TEXT("none"), *FVector(Hit.Location).ToCompactString(), HitSeconds);
				Pawn->ThrowHeld();
				// Every frame until just before the predicted contact: the box against the path at the same time.
				const double Thrown = World->GetTimeSeconds();
				const float Until = Hit.bBlockingHit ? HitSeconds - 0.05f : 2.0f;
				TSharedRef<float> MaxDeviation = MakeShared<float>(0.0f);
				TSharedRef<int32> Samples = MakeShared<int32>(0);
				TSharedRef<FTimerHandle> Track = MakeShared<FTimerHandle>();
				World->GetTimerManager().SetTimer(*Track, FTimerDelegate::CreateWeakLambda(World, [World, Box, Path, Thrown, Until, MaxDeviation, Samples, Track, Label]()
				{
					const float Since = float(World->GetTimeSeconds() - Thrown);
					if (Since <= 0.0f)
					{
						return;
					}
					if (Since > Until || !IsValid(Box))
					{
						UE_LOG(LogMindKore, Log, TEXT("Gravity probe [%s throw]: flight vs prediction over %.2f s, %d frames: max deviation %.2f cm"),
							*Label, Until, *Samples, *MaxDeviation);
						// Last: clearing the timer destroys this lambda and what it captured.
						World->GetTimerManager().ClearTimer(*Track);
						return;
					}
					const TArray<FJamFlightPoint>& Points = *Path;
					for (int32 Index = 1; Index < Points.Num(); ++Index)
					{
						if (Points[Index].Seconds >= Since)
						{
							const float Alpha = (Since - Points[Index - 1].Seconds) / FMath::Max(Points[Index].Seconds - Points[Index - 1].Seconds, 1e-4f);
							// ponytail: linear between chunk ends (0.1 s apart: off the parabola by g dt^2 / 8 = 1.2 cm at most)
							const FVector Predicted = FMath::Lerp(Points[Index - 1].Location, Points[Index].Location, Alpha);
							*MaxDeviation = FMath::Max(*MaxDeviation, float(FVector::Dist(Predicted, Box->GetActorLocation())));
							++*Samples;
							break;
						}
					}
				}), 0.0005f, true);
				After(World, 3.0f, [World, Label, Next, Box]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Gravity probe [%s throw +3 s]: %s"), *Label, IsValid(Box) ? *Describe(Box->Body) : TEXT("box gone"));
					Next();
				});
			});
		});
	}

	struct FBodyStart
	{
		TWeakObjectPtr<UPrimitiveComponent> Body;
		FVector Location;
	};

	/** The bodies in walking zones at the start: where they are, how high, and whether the zones pull them. */
	TArray<FBodyStart> LogStartBodies(UWorld* World)
	{
		TArray<FBodyStart> Found;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (!Body || !Body->IsSimulatingPhysics() || Body->GetCollisionObjectType() != ECC_PhysicsBody || Cast<AJamFlyer>(*It))
			{
				continue;
			}
			const FJamGravity Gravity = JamGravityAt(World, Body->GetComponentLocation());
			if (!Gravity.Zone)
			{
				continue;
			}
			const bool bOut = It->ActorHasTag(NoGravityTag) || !Body->IsGravityEnabled();
			UE_LOG(LogMindKore, Log, TEXT("Gravity probe [start]: %s in %s, %s, tags [%s] - %s"), *It->GetActorNameOrLabel(), *Gravity.Zone->GetActorNameOrLabel(),
				*Describe(Body), *FString::JoinBy(It->Tags, TEXT(", "), [](const FName& Tag) { return Tag.ToString(); }), bOut ? TEXT("opted out") : TEXT("pulled"));
			Found.Add({ Body, Body->GetComponentLocation() });
		}
		return Found;
	}

	void ProbeCorpse(UWorld* World, TFunction<void()> Next)
	{
		AJamFlyerHabitat* Home = nullptr;
		for (TActorIterator<AJamFlyerHabitat> It(World); It && !Home; ++It)
		{
			Home = It->InnerRadiusCm > 0.0f ? *It : nullptr;
		}
		AJamFlyer* Flyer = Home && !Home->GetFlock().IsEmpty() ? Home->GetFlock()[0].Get() : nullptr;
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		if (!Flyer)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Gravity probe [corpse]: no flyer round a planetoid"));
			Next();
			return;
		}
		// 3 m up and 4 m aside of the pawn standing on the planetoid, killed by a round as a gun delivers it.
		const FVector Up = -JamGravityAt(World, Pawn->GetActorLocation()).Acceleration.GetSafeNormal();
		const FVector Side = FVector::CrossProduct(Up, FVector::ForwardVector).GetSafeNormal();
		Teleport(Flyer, Pawn->GetActorLocation() + Side * 400.0f + Up * 300.0f);
		FMKResolvedImpact Kill;
		Kill.ShotId = FGuid::NewGuid();
		Kill.Kind = EMKImpactKind::Round;
		Kill.DamageAmount = 1000.0f;
		Kill.Hit = FHitResult(Flyer, Flyer->Body, Flyer->GetActorLocation(), Up);
		Kill.ImpulseNewtonSeconds = -Up * 30.0f;
		Kill.bHasPhysicsImpulse = true;
		Kill.SourceActor = Pawn;
		MKDeliverResolvedImpact(World, Pawn, Kill);
		UE_LOG(LogMindKore, Log, TEXT("Gravity probe [corpse]: %s killed 3 m over the ground, state %s"), *Flyer->GetName(),
			Flyer->GetState() == EJamFlyerState::Dead ? TEXT("dead") : TEXT("ALIVE"));
		const TWeakObjectPtr<AJamFlyer> Weak = Flyer;
		const double Killed = World->GetTimeSeconds();
		// Poll until it sleeps (or 15 s); then a kick wakes it, and it must fall asleep again.
		TSharedRef<TFunction<void(int32)>> Wait = MakeShared<TFunction<void(int32)>>();
		*Wait = [World, Weak, Killed, Next, Wait](int32 Round)
		{
			AJamFlyer* Corpse = Weak.Get();
			if (!Corpse)
			{
				Next();
				return;
			}
			const bool bAsleep = !Corpse->Body->RigidBodyIsAwake();
			const float Since = float(World->GetTimeSeconds() - Killed);
			if (!bAsleep && Since < 15.0f * (Round + 1))
			{
				if (FMath::Fmod(Since, 2.0f) < 0.25f)
				{
					UE_LOG(LogMindKore, Log, TEXT("Gravity probe [corpse, %.1f s]: %s at %s"), Since, *Describe(Corpse->Body), *Corpse->GetActorLocation().ToCompactString());
				}
				After(World, 0.25f, [Wait, Round]() { (*Wait)(Round); });
				return;
			}
			UE_LOG(LogMindKore, Log, TEXT("Gravity probe [corpse %s]: %s at %.1f s"), Round == 0 ? TEXT("after death") : TEXT("after the kick"),
				*Describe(Corpse->Body), Since);
			if (Round == 0 && bAsleep)
			{
				// A hit: 300 cm/s up and aside.
				Corpse->Body->AddImpulse((-JamGravityAt(World, Corpse->GetActorLocation()).Acceleration.GetSafeNormal() + FVector(0.3f, 0.0f, 0.0f)) * 300.0f,
					NAME_None, /*bVelChange=*/true);
				After(World, 0.1f, [World, Weak, Wait]()
				{
					if (const AJamFlyer* Corpse = Weak.Get())
					{
						UE_LOG(LogMindKore, Log, TEXT("Gravity probe [corpse kicked +0.1 s]: %s"), *Describe(Corpse->Body));
					}
					(*Wait)(1);
				});
				return;
			}
			Next();
		};
		After(World, 0.5f, [Wait]() { (*Wait)(0); });
	}

	/** Load: Count boxes dropped in the cabin; the pull's cost while they fall, then once they sleep. */
	void ProbeLoad(UWorld* World, const FVector& Floor, int32 Count, TFunction<void()> Next)
	{
		LogPullCost(TEXT("before the load"));
		for (int32 Index = 0; Index < Count; ++Index)
		{
			SpawnBox(World, Floor + FVector((Index % 8) * 40.0f - 140.0f, (Index / 8 % 8) * 40.0f - 140.0f, 150.0f + (Index / 64) * 40.0f), 20.0f, 5.0f);
		}
		After(World, 3.0f, [World, Count, Next]()
		{
			LogPullCost(*FString::Printf(TEXT("%d boxes falling, 3 s"), Count));
			After(World, 5.0f, [World, Count, Next]()
			{
				LogPullCost(*FString::Printf(TEXT("%d boxes landed, next 5 s"), Count));
				Next();
			});
		});
	}

	/**
	 * jam.Gravity.Probe (Lvl_Jam_Ship): the bodies in walking zones at the start;
	 * in the cabin a pistol casing, an opted-out box and its control, a throw
	 * against its prediction, a load of boxes for the cost; on the planetoid a
	 * throw and a flyer corpse that has to fall asleep, wake on a kick and sleep
	 * again; the start bodies' drift. Ends with "Gravity probe: done".
	 */
	void RunGravityProbe(UWorld* World)
	{
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		if (!Pawn || !World->GetAuthGameMode<AJamShipGameMode>())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Gravity probe: needs AJamShipGameMode and the zero-G pawn"));
			return;
		}
		LogPullCost(TEXT("from the start to the probe"));
		const TArray<FBodyStart> StartBodies = LogStartBodies(World);
		if (!JamGravityAt(World, Pawn->GetActorLocation()).Zone)
		{
			for (TActorIterator<APlayerStart> Start(World); Start; ++Start)
			{
				if (Start->ActorHasTag(TEXT("Cabin")))
				{
					Teleport(Pawn, Start->GetActorLocation());
				}
			}
		}
		const FJamGravity Cabin = JamGravityAt(World, Pawn->GetActorLocation());
		UE_LOG(LogMindKore, Log, TEXT("Gravity probe [cabin]: pawn at %s in %s, gravity %s, air %s"), *Pawn->GetActorLocation().ToCompactString(),
			*GetNameSafe(Cabin.Zone), *Cabin.Acceleration.ToCompactString(), *GetNameSafe(JamAirZoneAt(World, Pawn->GetActorLocation())));
		// A pistol shot along the cabin: the casing must reach the floor.
		Pawn->Guns->UnlockWeapon(0);
		Pawn->Guns->SelectWeapon(0);
		Pawn->Guns->FinishSwitch();
		World->GetFirstPlayerController()->SetControlRotation(FRotator(-5.0f, 0.0f, 0.0f));
		TSet<AActor*> Before;
		for (TActorIterator<AStaticMeshActor> It(World); It; ++It)
		{
			Before.Add(*It);
		}
		Pawn->Fire();
		After(World, 0.4f, [World, Before, StartBodies]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(World);
			UStaticMeshComponent* Casing = nullptr;
			for (TActorIterator<AStaticMeshActor> It(World); It && !Casing; ++It)
			{
				Casing = !Before.Contains(*It) && It->GetStaticMeshComponent()->IsSimulatingPhysics() ? It->GetStaticMeshComponent() : nullptr;
			}
			UE_LOG(LogMindKore, Log, TEXT("Gravity probe [casing +0.4 s]: %s"), Casing ? *Describe(Casing) : TEXT("NO casing found"));
			// Two boxes 1.2 m over the floor beside the pawn: one tagged Jam.NoGravity, the other its control.
			const FVector Floor = Pawn->GetActorLocation() - FVector(0.0f, 0.0f, Pawn->Body->GetScaledSphereRadius());
			AMKPhysicsProp* Out = SpawnBox(World, Floor + FVector(150.0f, -70.0f, 120.0f), 30.0f, 10.0f, NoGravityTag);
			AMKPhysicsProp* Control = SpawnBox(World, Floor + FVector(150.0f, 70.0f, 120.0f), 30.0f, 10.0f);
			const FVector OutStart = Out->GetActorLocation();
			const TWeakObjectPtr<UStaticMeshComponent> WeakCasing = Casing;
			After(World, 2.5f, [World, WeakCasing, Out, Control, OutStart, Floor, StartBodies]()
			{
				if (const UStaticMeshComponent* Casing = WeakCasing.Get())
				{
					UE_LOG(LogMindKore, Log, TEXT("Gravity probe [casing +2.9 s]: %s - on the floor %s"), *Describe(Casing),
						FMath::Abs(GapBelow(Casing)) < 3.0f && Casing->GetPhysicsLinearVelocity().Size() < 5.0f ? TEXT("yes") : TEXT("NO"));
				}
				UE_LOG(LogMindKore, Log, TEXT("Gravity probe [opted out +2.5 s]: %s, moved %.2f cm - stays %s"), *Describe(Out->Body),
					FVector::Dist(Out->GetActorLocation(), OutStart), FVector::Dist(Out->GetActorLocation(), OutStart) < 1.0f ? TEXT("yes") : TEXT("NO"));
				UE_LOG(LogMindKore, Log, TEXT("Gravity probe [control +2.5 s]: %s - fell %s"), *Describe(Control->Body),
					FMath::Abs(GapBelow(Control->Body)) < 3.0f ? TEXT("yes") : TEXT("NO"));
				ProbeThrow(World, TEXT("cabin"), 20.0f, [World, Floor, StartBodies]()
				{
					ProbeLoad(World, Floor + FVector(-200.0f, 0.0f, 0.0f), 64, [World, StartBodies]()
					{
						// The planetoid: 3 m under the walking ball's top, fall, land, throw.
						AJamZeroGPawn* Pawn = PlayerPawn(World);
						const AJamGravityZone* Shell = nullptr;
						for (TActorIterator<AJamGravityZone> It(World); It; ++It)
						{
							Shell = It->bRadial && It->bWalk && It->SphereRadiusCm > 0.0f ? *It : Shell;
						}
						if (!Shell)
						{
							UE_LOG(LogMindKore, Warning, TEXT("Gravity probe: no planetoid"));
							return;
						}
						Teleport(Pawn, Shell->GetActorLocation() + FVector::UpVector * (Shell->SphereRadiusCm - 300.0f));
						After(World, 4.0f, [World, StartBodies]()
						{
							AJamZeroGPawn* Pawn = PlayerPawn(World);
							UE_LOG(LogMindKore, Log, TEXT("Gravity probe [on the planetoid]: pawn at %s, %s"), *Pawn->GetActorLocation().ToCompactString(),
								Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("NOT on the ground"));
							ProbeThrow(World, TEXT("planetoid"), 25.0f, [World, StartBodies]()
							{
								ProbeCorpse(World, [World, StartBodies]()
								{
									for (const FBodyStart& Start : StartBodies)
									{
										if (const UPrimitiveComponent* Body = Start.Body.Get())
										{
											UE_LOG(LogMindKore, Log, TEXT("Gravity probe [start bodies, end]: %s moved %.1f cm"), *Describe(Body),
												FVector::Dist(Body->GetComponentLocation(), Start.Location));
										}
									}
									LogPullCost(TEXT("the probe's rest"));
									UE_LOG(LogMindKore, Log, TEXT("Gravity probe: done"));
								});
							});
						});
					});
				});
			});
		});
	}

	/**
	 * jam.Gravity.Stills (Lvl_Jam_Ship, needs a real RHI): in the cabin the
	 * pawn holds a 10 kg box and aims (Left Shift: the predicted arc), then
	 * throws it; Shot (not HighResShot: it leaves out the debug lines of the
	 * arc) while aiming, in flight and after it lands.
	 */
	void RunGravityStills(UWorld* World)
	{
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		APlayerController* Controller = World->GetFirstPlayerController();
		if (!Pawn || !Controller)
		{
			return;
		}
		const FRotator View(12.0f, 0.0f, 0.0f);
		Controller->SetControlRotation(View);
		// Held low and to the right where it was taken, so the arc does not hide behind the box.
		Pawn->bHoldInFront = false;
		const FRotationMatrix Axes(View);
		AMKPhysicsProp* Box = SpawnBox(World, Pawn->Camera->GetComponentLocation() + Axes.GetUnitAxis(EAxis::X) * 110.0f
			+ Axes.GetUnitAxis(EAxis::Y) * 45.0f - Axes.GetUnitAxis(EAxis::Z) * 45.0f, 30.0f, 10.0f);
		Pawn->Grab(Box->Body, Box->Body->GetComponentLocation());
		After(World, 1.2f, [World]() { PlayerPawn(World)->ProbeAim(true); });
		After(World, 2.5f, [World, Controller]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Gravity stills: aiming, the arc drawn"));
			// Where the path's points land on the screen (the debug lines are drawn through them).
			AJamZeroGPawn* Pawn = PlayerPawn(World);
			if (UPrimitiveComponent* Held = Pawn->GetGrabbed())
			{
				const float MassKg = Held->GetMass();
				const FVector Velocity = Held->GetPhysicsLinearVelocity()
					+ Pawn->GetControlRotation().Vector() * FMath::Min(Pawn->ThrowImpulse, MassKg * Pawn->MaxThrowSpeedCm) / MassKg;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(JamGravityStills), false, Pawn);
				Params.AddIgnoredActor(Held->GetOwner());
				TArray<FJamFlightPoint> Path;
				float HitSeconds = 0.0f;
				JamPredictFlight(World, Held->GetComponentLocation(), Velocity, Held->Bounds.SphereRadius, Pawn->PredictSeconds,
					World->GetDeltaSeconds(), Pawn->PredictRestitution, 1, Params, Path, HitSeconds);
				for (const FJamFlightPoint& Point : Path)
				{
					FVector2D Screen(-1.0, -1.0);
					Controller->ProjectWorldLocationToScreen(Point.Location, Screen);
					UE_LOG(LogMindKore, Log, TEXT("Gravity stills: path %.2f s at %s, screen (%.0f, %.0f)"), Point.Seconds,
						*Point.Location.ToCompactString(), Screen.X, Screen.Y);
				}
			}
			Controller->ConsoleCommand(TEXT("Shot"));
		});
		After(World, 3.6f, [World]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(World);
			Pawn->ProbeAim(false);
			Pawn->ThrowHeld();
		});
		After(World, 3.75f, [World, Controller, Box]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Gravity stills: in flight, box at %s"), *Box->GetActorLocation().ToCompactString());
			Controller->ConsoleCommand(TEXT("Shot"));
		});
		After(World, 6.0f, [World, Controller, Box]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Gravity stills: landed, %s"), *Describe(Box->Body));
			Controller->ConsoleCommand(TEXT("Shot"));
		});
		After(World, 8.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Gravity stills: done")); });
	}

	FAutoConsoleCommandWithWorld GGravityStills(
		TEXT("jam.Gravity.Stills"),
		TEXT("Rendered stills of a throw in the cabin: aiming with the predicted arc, in flight, landed (Shot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunGravityStills));

	FAutoConsoleCommandWithWorld GGravityProbe(
		TEXT("jam.Gravity.Probe"),
		TEXT("Loose bodies in the gravity zones of Lvl_Jam_Ship: casing, opt-out, throws against their prediction, a corpse that sleeps, the cost."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunGravityProbe));
}
