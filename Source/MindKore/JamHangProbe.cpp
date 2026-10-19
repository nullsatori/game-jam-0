// jam.Hang.Probe: the arm (AJamZeroGPawn::UpdateArm) logged over time - a block
// edge caught in passing, swings let go with and without a pull, a kick while
// holding, a heavy loose body on the arm (Lvl_Jam_ZeroG); hanging under the
// cabin ceiling, a pull-up, a drop (Lvl_Jam_Ship).

#include "JamShip.h"
#include "JamZeroGPawn.h"
#include "MindKore.h"

#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "TimerManager.h"

namespace
{
	void After(AJamZeroGPawn* Pawn, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
	}

	/** jam.Hang.Probe shots: HighResShot frames at the marked moments (needs a real RHI). */
	bool bHangShots = false;

	/** A frame every Period for Seconds from now, when shots were asked for. */
	void Frames(AJamZeroGPawn* Pawn, const TCHAR* Stage, float Period, float Seconds)
	{
		if (!bHangShots)
		{
			return;
		}
		const FString Name = Stage;
		for (float Time = 0.0f; Time <= Seconds + 0.001f; Time += Period)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Name, Time]()
			{
				if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
				{
					UE_LOG(LogMindKore, Log, TEXT("Hang probe [%s frame +%.2f s]: arm %.1f cm, %.0f cm/s"), *Name, Time, Pawn->GetArmLengthCm(), Pawn->GetBodyVelocity().Size());
					Controller->ConsoleCommand(TEXT("HighResShot 1"));
				}
			}), FMath::Max(Time, 0.001f), false);
		}
	}

	void LogArm(const AJamZeroGPawn* Pawn, const FString& Stage, double Since)
	{
		const FVector Velocity = Pawn->GetBodyVelocity();
		const FVector Hand = Pawn->GetHandLocation();
		const FVector FromHand = Pawn->ShoulderLocation() - Hand;
		const FVector Along = FromHand.GetSafeNormal();
		const double Radial = FVector::DotProduct(Velocity, Along);
		UE_LOG(LogMindKore, Log, TEXT("Hang probe [%s +%.2f s]: eye-hand %s, arm %.1f cm (reach %.1f), velocity %s = %.0f cm/s (along the arm %.0f, across %.0f), %s%s"),
			*Stage, Pawn->GetWorld()->GetTimeSeconds() - Since, *FromHand.ToCompactString(), Pawn->GetArmLengthCm(), Pawn->GetArmRestCm(),
			*Velocity.ToCompactString(), Velocity.Size(), Radial, (Velocity - Along * Radial).Size(),
			Pawn->IsClinging() ? TEXT("holding on") : Pawn->GetGrabbed() ? TEXT("holding a body") : TEXT("hands free"),
			Pawn->IsInGravity() ? (Pawn->IsGrounded() ? TEXT(", gravity, grounded") : TEXT(", gravity, in the air")) : TEXT(""));
	}

	/** Log every Period seconds for Seconds from now. */
	void Sample(AJamZeroGPawn* Pawn, const FString& Stage, float Period, float Seconds)
	{
		const double Since = Pawn->GetWorld()->GetTimeSeconds();
		for (float Time = 0.0f; Time <= Seconds + 0.001f; Time += Period)
		{
			After(Pawn, FMath::Max(Time, 0.001f), [Pawn, Stage, Since]() { LogArm(Pawn, Stage, Since); });
		}
	}

	/** Thrust keys held (view frame: X forward, Z up) for Seconds from now, the view kept on the hand; then Done. The
	 * keys set here are read by the next tick, for the frame ahead: the frame the end falls in gets its share of
	 * the input, so the hold lasts the same time at any frame rate (whole frames were 11 of 12 at 30 FPS for 0.4 s,
	 * 57 of 57.6 at 144). Done runs once the last share has been read - the next stage starts with no frame between. */
	void HoldKeys(AJamZeroGPawn* Pawn, const FVector& Input, float Seconds, bool bLookAtHand, TFunction<void()> Done = nullptr)
	{
		const double End = Pawn->GetWorld()->GetTimeSeconds() + Seconds;
		const TSharedRef<FTimerHandle> Hold = MakeShared<FTimerHandle>();
		const auto Press = [Pawn, Hold, End, Input, bLookAtHand, Done = MoveTemp(Done)]()
		{
			const UWorld* World = Pawn->GetWorld();
			const double Share = (End - World->GetTimeSeconds()) / World->GetDeltaSeconds();
			if (Share < 1.0e-3)
			{
				if (Pawn->GetWorldTimerManager().IsTimerActive(*Hold))
				{
					// Clearing the timer frees this lambda's captures: keep the handle and what runs next.
					const TFunction<void()> Next = Done;
					const TSharedRef<FTimerHandle> Handle = Hold;
					Pawn->GetWorldTimerManager().ClearTimer(*Handle);
					if (Next)
					{
						Next();
					}
				}
				return;
			}
			if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()); Controller && bLookAtHand)
			{
				Controller->SetControlRotation((Pawn->GetHandLocation() - Pawn->ShoulderLocation()).Rotation());
			}
			Pawn->ProbeThrust(Input * float(FMath::Min(Share, 1.0)), false);
		};
		Pawn->GetWorldTimerManager().SetTimer(*Hold, FTimerDelegate::CreateWeakLambda(Pawn, Press), 0.0005f, true);
		Press();
	}

	void Look(AJamZeroGPawn* Pawn, const FVector& Direction)
	{
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			Controller->SetControlRotation(Direction.Rotation());
		}
	}

	void Place(AJamZeroGPawn* Pawn, const FVector& Location, const FVector& Velocity)
	{
		Pawn->Body->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(Velocity);
	}

	/** Zero-G, part 3: the heaviest loose body put ahead at rest, grabbed while passing at 300 cm/s. */
	void HeavyBody(AJamZeroGPawn* Pawn, FVector Home)
	{
		UPrimitiveComponent* Heaviest = nullptr;
		for (TActorIterator<AActor> It(Pawn->GetWorld()); It; ++It)
		{
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (Root && Root != Pawn->Body && Root->IsSimulatingPhysics() && (!Heaviest || Root->GetMass() > Heaviest->GetMass()))
			{
				Heaviest = Root;
			}
		}
		if (!Heaviest)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hang probe: no loose body"));
			return;
		}
		const float Radius = Heaviest->Bounds.SphereRadius;
		// Open space: the first spot above the start with nothing within the body's radius + 5 m.
		FCollisionQueryParams Clear(SCENE_QUERY_STAT(JamHangProbeClear), false, Pawn);
		Clear.AddIgnoredComponent(Heaviest);
		for (int32 Step = 1; Step <= 10; ++Step)
		{
			const FVector Spot = Home + FVector(0.0f, 0.0f, 600.0f * Step);
			if (!Pawn->GetWorld()->OverlapBlockingTestByChannel(Spot, FQuat::Identity, ECC_Pawn, FCollisionShape::MakeSphere(Radius + 500.0f), Clear))
			{
				Home = Spot;
				break;
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Hang probe [heavy]: open space at %s"), *Home.ToCompactString());
		Heaviest->SetWorldLocation(Home + FVector(Radius + 150.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Heaviest->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Heaviest->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Place(Pawn, Home, FVector(0.0f, 300.0f, 0.0f));
		Look(Pawn, FVector::ForwardVector);
		After(Pawn, 0.05f, [Pawn, Heaviest]()
		{
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHangProbe), false, Pawn);
			const FVector Eye = Pawn->ShoulderLocation();
			const bool bHit = Heaviest->LineTraceComponent(Hit, Eye, Eye + FVector(1000.0f, 0.0f, 0.0f), Params);
			const FVector Point = bHit ? FVector(Hit.ImpactPoint) : Heaviest->GetComponentLocation();
			const float Total = Pawn->Body->GetMass() + Heaviest->GetMass();
			const FVector Before = Pawn->GetBodyVelocity() * Pawn->Body->GetMass() + Heaviest->GetPhysicsLinearVelocity() * Heaviest->GetMass();
			const bool bGrabbed = Pawn->Grab(Heaviest, Point);
			UE_LOG(LogMindKore, Log, TEXT("Hang probe [heavy]: %s %s %.0f kg at %.0f cm, momentum %s kg cm/s, centre of mass %s cm/s"),
				bGrabbed ? TEXT("grabbed") : TEXT("NOT grabbed"), *GetNameSafe(Heaviest->GetOwner()), Heaviest->GetMass(),
				FVector::Dist(Eye, Point), *Before.ToCompactString(), *(Before / Total).ToCompactString());
			const double Since = Pawn->GetWorld()->GetTimeSeconds();
			for (float Time = 0.1f; Time <= 3.01f; Time += 0.1f)
			{
				After(Pawn, Time, [Pawn, Heaviest, Since, Total]()
				{
					const FVector Momentum = Pawn->GetBodyVelocity() * Pawn->Body->GetMass() + Heaviest->GetPhysicsLinearVelocity() * Heaviest->GetMass();
					LogArm(Pawn, TEXT("heavy"), Since);
					UE_LOG(LogMindKore, Log, TEXT("Hang probe [heavy]: body %s cm/s, momentum %s kg cm/s, centre of mass %s cm/s"),
						*Heaviest->GetPhysicsLinearVelocity().ToCompactString(), *Momentum.ToCompactString(), *(Momentum / Total).ToCompactString());
				});
			}
			After(Pawn, 3.1f, [Pawn]()
			{
				Pawn->Release();
				UE_LOG(LogMindKore, Log, TEXT("Hang probe: done"));
			});
		});
	}

	/** Zero-G: grab a block's edge while passing it; a pumped swing let go; the same with a pull mid-swing; a kick while holding; a heavy body. */
	void RunZeroG(AJamZeroGPawn* Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		const FVector Home = Pawn->GetActorLocation();
		UStaticMeshComponent* Block = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UStaticMeshComponent* Mesh = Cast<UStaticMeshComponent>(It->GetRootComponent());
			if (!Mesh || !Mesh->GetStaticMesh() || Mesh->IsSimulatingPhysics() || Mesh->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				continue;
			}
			const float Distance = FVector::Dist(Home, Mesh->GetComponentLocation());
			if (Mesh->Bounds.SphereRadius > 100.0f && Mesh->Bounds.SphereRadius < 600.0f && Distance > 500.0f && Distance < 2800.0f
				&& (!Block || Distance < FVector::Dist(Home, Block->GetComponentLocation())))
			{
				Block = Mesh;
			}
		}
		if (!Block)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hang probe: no static block 5-28 m away"));
			return;
		}
		// The block's +X+Y vertical edge, halfway up; the pawn 100 cm out along +X, passing at 300 cm/s along +Y.
		const FTransform Frame = Block->GetComponentTransform();
		const FBox Box = Block->GetStaticMesh()->GetBoundingBox();
		const FVector Edge = Frame.TransformPosition(FVector(Box.Max.X, Box.Max.Y, Box.GetCenter().Z));
		const FVector Out = Frame.TransformVectorNoScale(FVector::ForwardVector);
		const FVector Side = Frame.TransformVectorNoScale(FVector::RightVector);
		Place(Pawn, Edge + Out * 100.0f - Side * 30.0f, Side * 300.0f);
		Look(Pawn, -Out * 100.0f + Side * 30.0f); // at the edge
		UE_LOG(LogMindKore, Log, TEXT("Hang probe [edge]: %s, the edge at %s, out %s, passing along %s at 300 cm/s"),
			*GetNameSafe(Block->GetOwner()), *Edge.ToCompactString(), *Out.ToCompactString(), *Side.ToCompactString());
		After(Pawn, 0.1f, [Pawn, Block, Edge]()
		{
			const bool bHeld = Pawn->Cling(Block, Edge);
			UE_LOG(LogMindKore, Log, TEXT("Hang probe [edge]: %s"), bHeld ? TEXT("holding on") : TEXT("NOT holding"));
			Sample(Pawn, TEXT("catch"), 0.05f, 0.5f);
			Frames(Pawn, TEXT("catch"), 0.1f, 0.6f);
			Sample(Pawn, TEXT("swing"), 0.25f, 2.0f);
		});
		// From rest at arm length on the bisector of the free side, looking at the hand: D pumps the swing
		// for PumpSeconds; then (if PullSeconds > 0) W pulls in mid-swing; then let go.
		const FVector Bisector = (Out + Side).GetSafeNormal();
		const auto Fling = [Pawn, Block, Edge, Bisector](const FString& Label, float PumpSeconds, float PullSeconds)
		{
			Pawn->LetGoCling();
			Place(Pawn, Edge + Bisector * Pawn->ArmLengthCm, FVector::ZeroVector);
			Look(Pawn, -Bisector);
			UE_LOG(LogMindKore, Log, TEXT("Hang probe [%s]: %s, D %.2f s then W %.2f s, let go"), *Label,
				Pawn->Cling(Block, Edge) ? TEXT("holding on at rest") : TEXT("NOT holding"), PumpSeconds, PullSeconds);
			const auto LetGo = [Pawn, Label]()
			{
				LogArm(Pawn, Label + TEXT(" let go"), Pawn->GetWorld()->GetTimeSeconds());
				Pawn->LetGoCling();
				// The first frame after is the release speed: a pulled body reaches the block a few frames later.
				After(Pawn, 0.001f, [Pawn, Label]() { LogArm(Pawn, Label + TEXT(" released"), Pawn->GetWorld()->GetTimeSeconds()); });
				Sample(Pawn, Label + TEXT(" flying"), 0.25f, 0.5f);
			};
			HoldKeys(Pawn, FVector(0.0f, 1.0f, 0.0f), PumpSeconds, true, [Pawn, Label, PullSeconds, LetGo]()
			{
				if (PullSeconds <= 0.0f)
				{
					LetGo();
					return;
				}
				HoldKeys(Pawn, FVector(1.0f, 0.0f, 0.0f), PullSeconds, true, LetGo);
				Sample(Pawn, Label + TEXT(" W"), 0.025f, PullSeconds);
			});
			Sample(Pawn, Label + TEXT(" D"), 0.1f, PumpSeconds);
		};
		// Stage lengths in whole frames at 30, 60 and 144 FPS (and 0.1 s at 30 / 60), so every rate lets go at the same moment.
		After(Pawn, 2.3f, [Fling]() { Fling(TEXT("swing-release"), 0.5f, 0.0f); });
		After(Pawn, 3.6f, [Fling]() { Fling(TEXT("swing-pull-release"), 1.0f / 3.0f, 0.1f); });
		// A sniper-size kick (400 cm/s straight out) while holding on: the hold must come to rest (welding).
		After(Pawn, 4.9f, [Pawn, Block, Edge, Bisector]()
		{
			Place(Pawn, Edge + Bisector * Pawn->ArmLengthCm, Bisector * 400.0f);
			UE_LOG(LogMindKore, Log, TEXT("Hang probe [kick]: %s, 400 cm/s straight out"), Pawn->Cling(Block, Edge) ? TEXT("holding on") : TEXT("NOT holding"));
			Sample(Pawn, TEXT("kick"), 0.05f, 0.6f);
			After(Pawn, 0.7f, [Pawn]() { Pawn->LetGoCling(); });
		});
		After(Pawn, 5.8f, [Pawn, Home]() { HeavyBody(Pawn, Home); });
	}

	/** The ship: hang under the cabin ceiling, swinging; pull up; let go. */
	void RunCabin(AJamZeroGPawn* Pawn, const AJamGravityZone* Cabin)
	{
		UWorld* World = Pawn->GetWorld();
		FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHangProbe), false, Pawn);
		const FVector Centre = Cabin->GetActorLocation();
		FHitResult Up;
		FHitResult Down;
		const bool bUp = World->LineTraceSingleByChannel(Up, Centre, Centre + FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params);
		const bool bDown = World->LineTraceSingleByChannel(Down, Centre, Centre - FVector(0.0f, 0.0f, 1000.0f), ECC_Visibility, Params);
		if (!bUp || !bDown)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hang probe: no cabin ceiling or floor over %s"), *Centre.ToCompactString());
			return;
		}
		const float Eye = Pawn->EyeHeightCm;
		const float BodyZ = FMath::Max(float(Down.ImpactPoint.Z) + 60.0f, float(Up.ImpactPoint.Z) - Eye - 90.0f);
		UE_LOG(LogMindKore, Log, TEXT("Hang probe [cabin]: %s, floor Z %.0f, ceiling Z %.0f (%s), body put at Z %.0f, gravity %s"),
			*Cabin->GetName(), Down.ImpactPoint.Z, Up.ImpactPoint.Z, *GetNameSafe(Up.GetActor()), BodyZ, Pawn->IsInGravity() ? TEXT("on") : TEXT("off"));
		// Stand in the cabin first, so the eye has risen to its height (EyeRiseRate), then up under the ceiling.
		Place(Pawn, FVector(Centre.X, Centre.Y, Down.ImpactPoint.Z + 60.0f), FVector::ZeroVector);
		Look(Pawn, FVector(1.0f, 0.0f, -0.1f)); // ahead, a little down: the cabin swings past
		After(Pawn, 1.5f, [Pawn, Centre, BodyZ]() { Place(Pawn, FVector(Centre.X, Centre.Y, BodyZ), FVector::ZeroVector); });
		After(Pawn, 1.55f, [Pawn, Up]()
		{
			const FVector Hand(Pawn->ShoulderLocation().X, Pawn->ShoulderLocation().Y, Up.ImpactPoint.Z);
			const bool bHeld = Pawn->Cling(Up.GetComponent(), Hand);
			// A push along +X as the hand closes: the swing.
			Pawn->Body->SetPhysicsLinearVelocity(FVector(250.0f, 0.0f, 0.0f));
			UE_LOG(LogMindKore, Log, TEXT("Hang probe [cabin]: %s the ceiling, 250 cm/s along +X"), bHeld ? TEXT("holding") : TEXT("NOT holding"));
			Sample(Pawn, TEXT("hang"), 0.1f, 3.0f);
			Frames(Pawn, TEXT("hang"), 0.2f, 1.2f);
			After(Pawn, 3.2f, [Pawn]()
			{
				// Space (the view's up, toward the hand overhead) held 1 s.
				// The last line at the release of Space (a sample at +1.0 s raced it: 45.1 / 39.2 / 35.7 cm at 30 / 60 / 144 were the reach relaxing or not yet).
				HoldKeys(Pawn, FVector(0.0f, 0.0f, 1.0f), 1.0f, false, [Pawn]() { LogArm(Pawn, TEXT("pull-up Space released"), Pawn->GetWorld()->GetTimeSeconds()); });
				Sample(Pawn, TEXT("pull-up Space"), 0.1f, 0.9f);
				Frames(Pawn, TEXT("pull-up Space"), 0.2f, 0.4f);
			});
			After(Pawn, 4.3f, [Pawn]() { Sample(Pawn, TEXT("keys up"), 0.2f, 1.0f); });
			After(Pawn, 5.5f, [Pawn]()
			{
				Pawn->LetGoCling();
				Sample(Pawn, TEXT("fall"), 0.1f, 0.5f);
				After(Pawn, 0.6f, []() { UE_LOG(LogMindKore, Log, TEXT("Hang probe: done")); });
			});
		});
	}

	void RunHangProbe(const TArray<FString>& Args, UWorld* World)
	{
		bHangShots = Args.Contains(TEXT("shots"));
		if (bHangShots && !Args.Contains(TEXT("now")))
		{
			// Frames: let the renderer warm up, then a quarter of the speed so each HighResShot lands on its own moment.
			FTimerHandle Later;
			World->GetTimerManager().SetTimer(Later, FTimerDelegate::CreateWeakLambda(World, [World]()
			{
				if (APlayerController* Controller = World->GetFirstPlayerController())
				{
					Controller->ConsoleCommand(TEXT("slomo 0.25"));
				}
				RunHangProbe({ TEXT("shots"), TEXT("now") }, World);
			}), 4.0f, false);
			return;
		}
		const APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hang probe: no AJamZeroGPawn"));
			return;
		}
		// Hands free (a gun out would make RMB aim; the probe calls Cling and Grab itself).
		if (Pawn->GetGrabbed())
		{
			Pawn->Release();
		}
		Pawn->LetGoCling();
		// The biggest box gravity zone is the cabin (as jam.Ship.Probe picks it).
		const AJamGravityZone* Cabin = nullptr;
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			const FVector Extent = It->Zone->GetScaledBoxExtent();
			if (It->SphereRadiusCm <= 0.0f && (!Cabin || Extent.X * Extent.Y * Extent.Z > Cabin->Zone->GetScaledBoxExtent().X * Cabin->Zone->GetScaledBoxExtent().Y * Cabin->Zone->GetScaledBoxExtent().Z))
			{
				Cabin = *It;
			}
		}
		UE_LOG(LogMindKore, Log, TEXT("Hang probe: arm %.0f cm (pull-up to %.0f), %.0f rad/s, damping ratio %.2f; %s"), Pawn->ArmLengthCm, Pawn->ArmMinCm,
			Pawn->ArmStiffness, Pawn->ArmDampingRatio, Cabin ? TEXT("the cabin") : TEXT("zero-G"));
		if (Cabin)
		{
			RunCabin(Pawn, Cabin);
		}
		else
		{
			RunZeroG(Pawn);
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GHangProbe(
		TEXT("jam.Hang.Probe"),
		TEXT("The arm: on Lvl_Jam_ZeroG a block edge caught in passing, a pumped swing let go, the same with a pull mid-swing, a kick while holding, a heavy body grabbed in passing; on Lvl_Jam_Ship hanging under the cabin ceiling. Logs positions, velocities and the arm length over time; \"shots\" adds HighResShot frames of the catch / the hang (needs a real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHangProbe));
}
