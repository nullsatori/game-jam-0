#include "JamZeroGPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "HAL/IConsoleManager.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "JamOxygen.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MKFastener.h"
#include "MKImpactDelivery.h"
#include "MKInteractionComponent.h"
#include "MKKeyring.h"
#include "MKPhysicsPropComponent.h"
#include "MKTimeDomainSubsystem.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "PhysicsEngine/PhysicsConstraintComponent.h"
#include "Sound/SoundBase.h"
#include "TimerManager.h"
#include "UObject/ConstructorHelpers.h"

AJamZeroGPawn::AJamZeroGPawn()
{
	PrimaryActorTick.bCanEverTick = true;
	// The body never turns; the camera follows the control rotation alone.
	bUseControllerRotationYaw = false;
	bUseControllerRotationPitch = false;
	bUseControllerRotationRoll = false;

	Body = CreateDefaultSubobject<USphereComponent>(TEXT("Body"));
	SetRootComponent(Body);
	Body->InitSphereRadius(40.0f);
	Body->SetCollisionProfileName(TEXT("Pawn"));
	Body->SetSimulatePhysics(true);
	Body->SetEnableGravity(false);
	Body->SetLinearDamping(0.0f);
	Body->SetAngularDamping(0.0f);
	Body->BodyInstance.bOverrideMass = true;
	Body->BodyInstance.SetMassOverride(BodyMassKg);
	Body->BodyInstance.bUseCCD = true;
	Body->BodyInstance.DOFMode = EDOFMode::SixDOF;
	Body->BodyInstance.bLockXRotation = true;
	Body->BodyInstance.bLockYRotation = true;
	Body->BodyInstance.bLockZRotation = true;
	Body->SetCanEverAffectNavigation(false);

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(Body);
	Camera->bUsePawnControlRotation = true;

	Interaction = CreateDefaultSubobject<UMKInteractionComponent>(TEXT("Interaction"));
	Keyring = CreateDefaultSubobject<UMKKeyringComponent>(TEXT("Keyring"));
	Vitals = CreateDefaultSubobject<UMKVitalsComponent>(TEXT("Vitals"));
	Oxygen = CreateDefaultSubobject<UJamOxygenComponent>(TEXT("Oxygen"));

	// Same MindKore content as AJamCharacter (CREDITS.md lists the sound donors).
	static ConstructorHelpers::FObjectFinder<UMKImpactDataAsset> DefaultImpactData(
		TEXT("/Game/MindKore/Impacts/DA_Impact_Default.DA_Impact_Default"));
	ImpactData = DefaultImpactData.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> BlastTransient(
		TEXT("/Game/MindKore/Audio/SW_MK_Blast_Transient.SW_MK_Blast_Transient"));
	static ConstructorHelpers::FObjectFinder<USoundBase> BlastTail(
		TEXT("/Game/MindKore/Audio/SW_MK_Blast_Tail.SW_MK_Blast_Tail"));
	for (USoundBase* Sound : { BlastTransient.Object, BlastTail.Object })
	{
		if (Sound)
		{
			ShotSounds.Add(Sound);
		}
	}
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Decal(
		TEXT("/Game/MindKore/Impacts/Decals/M_Decal_Default.M_Decal_Default"));
	DefaultImpactDecal = Decal.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BulletTime(
		TEXT("/Game/MindKore/PostProcess/M_MK_BulletTime.M_MK_BulletTime"));
	BulletTimeMaterial = BulletTime.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> TimeEntry(
		TEXT("/Game/MindKore/Audio/BulletTime/SW_MK_TimeEntry.SW_MK_TimeEntry"));
	BulletTimeEntrySound = TimeEntry.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> TimeExit(
		TEXT("/Game/MindKore/Audio/BulletTime/SW_MK_TimeExit.SW_MK_TimeExit"));
	BulletTimeExitSound = TimeExit.Object;
}

void AJamZeroGPawn::BeginPlay()
{
	Super::BeginPlay();
	Body->SetMassOverrideInKg(NAME_None, BodyMassKg, true);
	BodyMaterial = NewObject<UPhysicalMaterial>(this, TEXT("ZeroGBodyMaterial"));
	BodyMaterial->Friction = BodyFriction;
	BodyMaterial->Restitution = BodyRestitution;
	Body->SetPhysMaterialOverride(BodyMaterial);
	if (BulletTimeMaterial)
	{
		BulletTimeMID = UMaterialInstanceDynamic::Create(BulletTimeMaterial, this);
		Camera->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0.0f, BulletTimeMID));
	}
	UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: %.0f kg, world gravity Z %.1f"), *GetName(), Body->GetMass(),
		GetWorld()->GetGravityZ());
}

// --- input -------------------------------------------------------------------

void AJamZeroGPawn::BuildInput()
{
	Mapping = NewObject<UInputMappingContext>(this, TEXT("JamZeroGMapping"));
}

void AJamZeroGPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogMindKore, Error, TEXT("%s: input component is not an UEnhancedInputComponent (Config/DefaultInput.ini)"), *GetName());
		return;
	}
	BuildInput();
	Actions.Reset();
	const auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		Actions.Add(Action);
		return Action;
	};
	const auto Map = [this](UInputAction* Action, const FKey& Key, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& KeyMapping = Mapping->MapKey(Action, Key);
		if (bSwizzle)
		{
			KeyMapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		}
		if (bNegate)
		{
			KeyMapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
	};

	UInputAction* LookAction = MakeAction(TEXT("IA_ZG_Look"), EInputActionValueType::Axis2D);
	UInputAction* ThrustAction = MakeAction(TEXT("IA_ZG_Thrust"), EInputActionValueType::Axis2D);
	UInputAction* LiftAction = MakeAction(TEXT("IA_ZG_Lift"), EInputActionValueType::Axis1D);
	UInputAction* StabiliseAction = MakeAction(TEXT("IA_ZG_Stabilise"), EInputActionValueType::Boolean);
	UInputAction* UseAction = MakeAction(TEXT("IA_ZG_Use"), EInputActionValueType::Boolean);
	UInputAction* PrimaryAction = MakeAction(TEXT("IA_ZG_Primary"), EInputActionValueType::Boolean);
	UInputAction* PushAction = MakeAction(TEXT("IA_ZG_Push"), EInputActionValueType::Boolean);
	UInputAction* PutAction = MakeAction(TEXT("IA_ZG_Put"), EInputActionValueType::Boolean);
	UInputAction* BulletTimeAction = MakeAction(TEXT("IA_ZG_BulletTime"), EInputActionValueType::Boolean);
	UInputAction* CrowbarAction = MakeAction(TEXT("IA_ZG_Crowbar"), EInputActionValueType::Boolean);

	Map(LookAction, EKeys::Mouse2D, false, false);
	// WASD onto one 2D axis: D is +X, W is +Y (swizzled), A and S negated.
	Map(ThrustAction, EKeys::W, true, false);
	Map(ThrustAction, EKeys::S, true, true);
	Map(ThrustAction, EKeys::D, false, false);
	Map(ThrustAction, EKeys::A, false, true);
	Map(LiftAction, EKeys::SpaceBar, false, false);
	Map(LiftAction, EKeys::LeftControl, false, true);
	Map(StabiliseAction, EKeys::X, false, false);
	Map(UseAction, EKeys::E, false, false);
	Map(PrimaryAction, EKeys::LeftMouseButton, false, false);
	Map(PushAction, EKeys::RightMouseButton, false, false);
	Map(PutAction, EKeys::F, false, false);
	Map(BulletTimeAction, EKeys::Q, false, false);
	Map(CrowbarAction, EKeys::G, false, false);

	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Look);
	Input->BindAction(ThrustAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Thrust);
	Input->BindAction(LiftAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Lift);
	Input->BindActionValueLambda(StabiliseAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bStabilising = true; });
	Input->BindActionValueLambda(StabiliseAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bStabilising = false; });
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AJamZeroGPawn::Use);
	Input->BindAction(PrimaryAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PrimaryPressed);
	Input->BindAction(PushAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PushPressed);
	Input->BindAction(PutAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PutDown);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Started, this, &AJamZeroGPawn::BulletTimePressed);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Completed, this, &AJamZeroGPawn::BulletTimeReleased);
	Input->BindAction(CrowbarAction, ETriggerEvent::Started, this, &AJamZeroGPawn::ToggleCrowbar);
}

void AJamZeroGPawn::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr;
	if (Subsystem && Mapping)
	{
		Subsystem->AddMappingContext(Mapping, 0);
	}
}

void AJamZeroGPawn::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void AJamZeroGPawn::Thrust(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	ThrustInput.X += Axis.Y;
	ThrustInput.Y += Axis.X;
}

void AJamZeroGPawn::Lift(const FInputActionValue& Value)
{
	ThrustInput.Z += Value.Get<float>();
}

void AJamZeroGPawn::Use()
{
	// Holding: E lets go. Otherwise what the object offers (a canister, a door)
	// comes first; a loose body that offers nothing is grabbed.
	if (GrabbedBody.IsValid())
	{
		Release();
		return;
	}
	if (Interaction->CurrentOffer.IsOffered())
	{
		Interaction->Interact();
		return;
	}
	if (UPrimitiveComponent* Candidate = GrabCandidate.Get())
	{
		Grab(Candidate, GrabCandidatePoint);
	}
}

void AJamZeroGPawn::PrimaryPressed()
{
	if (GrabbedBody.IsValid())
	{
		ThrowHeld();
		return;
	}
	Fire();
}

void AJamZeroGPawn::PushPressed()
{
	if (GrabbedBody.IsValid())
	{
		Release();
		return;
	}
	Push();
}

void AJamZeroGPawn::PutDown()
{
	Release();
}

void AJamZeroGPawn::ToggleCrowbar()
{
	if (HasCrowbar())
	{
		Tags.Remove(MKFastenerCrowbarTag());
	}
	else
	{
		Tags.AddUnique(MKFastenerCrowbarTag());
	}
	UE_LOG(LogMindKore, Log, TEXT("Jam: %s %s the crowbar tag"), *GetName(), HasCrowbar() ? TEXT("got") : TEXT("dropped"));
}

bool AJamZeroGPawn::HasCrowbar() const
{
	return ActorHasTag(MKFastenerCrowbarTag());
}

void AJamZeroGPawn::BulletTimePressed()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, true);
	}
}

void AJamZeroGPawn::BulletTimeReleased()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, false);
	}
}

// --- motion -------------------------------------------------------------------

FVector AJamZeroGPawn::ViewLocation() const
{
	return Camera->GetComponentLocation();
}

FVector AJamZeroGPawn::ViewDirection() const
{
	return GetControlRotation().Vector();
}

FVector AJamZeroGPawn::GetBodyVelocity() const
{
	return Body->GetPhysicsLinearVelocity();
}

void AJamZeroGPawn::AddBodyImpulse(const FVector& Impulse)
{
	Body->AddImpulse(Impulse, NAME_None, /*bVelChange=*/false);
}

bool AJamZeroGPawn::Push()
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	if (Now - LastPushSeconds < PushCooldownSeconds)
	{
		return false;
	}
	const FVector Direction = ViewDirection();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamPush), /*bTraceComplex=*/false, this);
	if (const UPrimitiveComponent* Held = GrabbedBody.Get())
	{
		Params.AddIgnoredActor(Held->GetOwner());
	}
	FHitResult Hit;
	if (!World->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + Direction * PushReachCm, ECC_Visibility, Params))
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG push: nothing within %.0f cm"), PushReachCm);
		return false;
	}
	LastPushSeconds = Now;
	UPrimitiveComponent* Other = Hit.GetComponent();
	// Equal and opposite: what the hand gives the other body it takes from
	// the pawn. A static thing absorbs it all, so all of it is the pawn's.
	if (Other && Other->IsSimulatingPhysics())
	{
		Other->AddImpulseAtLocation(Direction * PushImpulse, Hit.ImpactPoint);
	}
	AddBodyImpulse(-Direction * PushImpulse);
	UE_LOG(LogMindKore, Log, TEXT("ZeroG push: off %s (%s, %.0f kg), impulse %.0f kg cm/s"), *GetNameSafe(Hit.GetActor()),
		Other && Other->IsSimulatingPhysics() ? TEXT("moving body") : TEXT("static"),
		Other && Other->IsSimulatingPhysics() ? Other->GetMass() : 0.0f, PushImpulse);
	return true;
}

void AJamZeroGPawn::RefreshGrabCandidate()
{
	GrabCandidate = nullptr;
	if (GrabbedBody.IsValid())
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamGrabLook), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + ViewDirection() * GrabReachCm, ECC_Visibility, Params)
		&& Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics())
	{
		GrabCandidate = Hit.GetComponent();
		GrabCandidatePoint = Hit.ImpactPoint;
	}
}

bool AJamZeroGPawn::Grab(UPrimitiveComponent* Target, FVector Point)
{
	if (GrabbedBody.IsValid() || !Target || !Target->IsSimulatingPhysics() || Target == Body)
	{
		return false;
	}
	if (Target->GetMass() > MaxGrabMassKg)
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grab: %s refused, %.0f kg is over %.0f kg"), *GetNameSafe(Target->GetOwner()),
			Target->GetMass(), MaxGrabMassKg);
		return false;
	}
	// One joint between the two bodies at the grab point: a spring on the
	// position, nothing on the rotation. Both ends feel it, so momentum is
	// shared - not a handle that moves the prop and leaves the pawn alone.
	GrabConstraint = NewObject<UPhysicsConstraintComponent>(this);
	GrabConstraint->SetupAttachment(Body);
	GrabConstraint->RegisterComponent();
	GrabConstraint->SetWorldLocation(Point);
	GrabConstraint->SetDisableCollision(true);
	GrabConstraint->SetLinearXLimit(LCM_Free, 0.0f);
	GrabConstraint->SetLinearYLimit(LCM_Free, 0.0f);
	GrabConstraint->SetLinearZLimit(LCM_Free, 0.0f);
	GrabConstraint->SetAngularSwing1Limit(ACM_Free, 0.0f);
	GrabConstraint->SetAngularSwing2Limit(ACM_Free, 0.0f);
	GrabConstraint->SetAngularTwistLimit(ACM_Free, 0.0f);
	GrabConstraint->SetLinearPositionDrive(true, true, true);
	GrabConstraint->SetLinearVelocityDrive(true, true, true);
	GrabConstraint->SetLinearDriveParams(GrabStiffness, GrabDamping, /*ForceLimit=*/0.0f);
	GrabConstraint->SetConstrainedComponents(Body, NAME_None, Target, NAME_None);
	GrabbedBody = Target;
	GrabCandidate = nullptr;
	Target->WakeAllRigidBodies();
	UE_LOG(LogMindKore, Log, TEXT("ZeroG grab: %s, %.0f kg"), *GetNameSafe(Target->GetOwner()), Target->GetMass());
	return true;
}

void AJamZeroGPawn::Release()
{
	if (GrabConstraint)
	{
		GrabConstraint->BreakConstraint();
		GrabConstraint->DestroyComponent();
		GrabConstraint = nullptr;
	}
	if (GrabbedBody.IsValid())
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grab: let go of %s"), *GetNameSafe(GrabbedBody->GetOwner()));
	}
	GrabbedBody = nullptr;
}

void AJamZeroGPawn::ThrowHeld()
{
	UPrimitiveComponent* PropBody = GrabbedBody.Get();
	if (!PropBody)
	{
		return;
	}
	const FVector Direction = ViewDirection();
	const float MassKg = PropBody->GetMass();
	// Equal and opposite; a light prop gets less than ThrowImpulse so it does not exceed MaxThrowSpeedCm.
	const float Impulse = FMath::Min(ThrowImpulse, MassKg * MaxThrowSpeedCm);
	Release();
	PropBody->AddImpulse(Direction * Impulse);
	AddBodyImpulse(-Direction * Impulse);
	UE_LOG(LogMindKore, Log, TEXT("ZeroG throw: %s %.1f kg, impulse %.0f kg cm/s (prop +%.0f cm/s, pawn -%.0f cm/s)"),
		*GetNameSafe(PropBody->GetOwner()), MassKg, Impulse, Impulse / FMath::Max(MassKg, 0.01f), Impulse / Body->GetMass());
}

void AJamZeroGPawn::ApplyThrusters(float DeltaSeconds)
{
	const FVector Input = ThrustInput.GetClampedToMaxSize(1.0f);
	ThrustInput = FVector::ZeroVector;
	if (!bThrustersEnabled || Oxygen->IsEmpty() || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (!Input.IsNearlyZero())
	{
		const FRotationMatrix View(GetControlRotation());
		const FVector Direction = View.GetUnitAxis(EAxis::X) * Input.X + View.GetUnitAxis(EAxis::Y) * Input.Y
			+ View.GetUnitAxis(EAxis::Z) * Input.Z;
		Body->AddImpulse(Direction * ThrustAccelCm * DeltaSeconds, NAME_None, /*bVelChange=*/true);
		Oxygen->Spend(Oxygen->ThrusterPerSecond * DeltaSeconds);
	}
	if (bStabilising)
	{
		const FVector Velocity = GetBodyVelocity();
		const FVector Change = -Velocity.GetClampedToMaxSize(StabiliserAccelCm * DeltaSeconds);
		Body->AddImpulse(Change, NAME_None, /*bVelChange=*/true);
		Oxygen->Spend(Oxygen->StabiliserPerSecond * DeltaSeconds);
	}
}

void AJamZeroGPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GrabConstraint && !GrabbedBody.IsValid())
	{
		Release();
	}
	RefreshGrabCandidate();
	ApplyThrusters(DeltaSeconds);
	if (MaxSpeedCm > 0.0f)
	{
		const FVector Velocity = GetBodyVelocity();
		if (Velocity.SizeSquared() > FMath::Square(MaxSpeedCm))
		{
			Body->SetPhysicsLinearVelocity(Velocity.GetClampedToMaxSize(MaxSpeedCm));
		}
	}
	UpdateBulletTimePresentation();
}

float AJamZeroGPawn::GetHeldMassKg() const
{
	const UPrimitiveComponent* Held = GrabbedBody.Get();
	return Held ? Held->GetMass() : 0.0f;
}

float AJamZeroGPawn::GetPushCooldownRemaining() const
{
	return FMath::Max(0.0f, float(LastPushSeconds + PushCooldownSeconds - GetWorld()->GetTimeSeconds()));
}

// --- shot ----------------------------------------------------------------------

void AJamZeroGPawn::Fire()
{
	UWorld* World = GetWorld();
	const FVector Start = ViewLocation();
	const FVector Direction = ViewDirection();
	AddBodyImpulse(-Direction * ShotRecoilImpulse);
	Oxygen->Spend(Oxygen->ShotCost);
	for (USoundBase* Sound : ShotSounds)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}

	// ponytail: the same trace, hit choice and impact fill as AJamCharacter::Fire,
	// copied so the ground pawn stays untouched; merge into one free function if
	// both pawns outlive the jam.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamZeroGShot), /*bTraceComplex=*/true, this);
	Params.bReturnPhysicalMaterial = true;
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, Start + Direction * ShotRangeCm, ECC_GameTraceChannel2, Params);
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
	if (NearestZone != INDEX_NONE)
	{
		Candidates[NearestZone].bBlocking = true;
	}
	const int32 Selected = MKSelectResolvedHit(Candidates, /*SameActorToleranceCm=*/2.5f);
	if (Selected == INDEX_NONE)
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG shot: no hit"));
		return;
	}
	const FHitResult& Hit = Hits[Selected];
	FMKImpactResolveParams ResolveParams;
	ResolveParams.ImpactData = ImpactData;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, ResolveParams, Feedback);
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Round;
	Resolved.SourceActor = this;
	Resolved.ImpactTimeSeconds = World->GetTimeSeconds();
	Resolved.RangeCm = FVector::Dist(GetActorLocation(), Hit.ImpactPoint);
	Resolved.DamageAmount = ShotDamage * Resolved.DamageMultiplier;
	Resolved.ImpulseNewtonSeconds = Resolved.ShotDirection * ShotImpulseNewtonSeconds;
	Resolved.bHasPhysicsImpulse = true;
	const bool bDelivered = MKDeliverResolvedImpact(this, this, Resolved);
	// A round pushes loose junk here (MindKore's props ignore rounds): zero-G fun.
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (!bDelivered && HitComponent && HitComponent->IsSimulatingPhysics())
	{
		HitComponent->AddImpulseAtLocation(Resolved.ImpulseNewtonSeconds * 100.0, Hit.ImpactPoint);
	}
	UE_LOG(LogMindKore, Log, TEXT("ZeroG shot: %s / %s, zone %d, damage %.1f, %s"), *GetNameSafe(Hit.GetActor()),
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
}

void AJamZeroGPawn::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	Vitals->ApplyIncoming(ResolvedImpact.DamageAmount, ResolvedImpact.Kind, /*bBlocked=*/false, ResolvedImpact.ShotId);
	if (ResolvedImpact.bHasPhysicsImpulse)
	{
		// N s to kg cm/s, as MKPhysicsBridge::ApplyPointImpulse converts.
		Body->AddImpulseAtLocation(ResolvedImpact.ImpulseNewtonSeconds * 100.0, ResolvedImpact.Hit.ImpactPoint);
	}
}

// --- bullet time presentation ---------------------------------------------------

void AJamZeroGPawn::UpdateBulletTimePresentation()
{
	// ponytail: copy of AJamCharacter::UpdateBulletTimePresentation (itself a port
	// of MindKore's MKCameraBulletTime.cpp); a shared component if both pawns stay.
	const UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>();
	const bool bMine = Time && Time->GetOperator() == this;
	const bool bSlowed = bMine && Time->IsSlowed();
	const float Alpha = bMine ? Time->GetPresentationAlpha() : 0.0f;
	if (bSlowed != bBulletTimeWasSlowed)
	{
		if (USoundBase* Sound = bSlowed ? BulletTimeEntrySound.Get() : BulletTimeExitSound.Get())
		{
			UGameplayStatics::SpawnSound2D(this, Sound, 0.65f);
		}
		bBulletTimeWasSlowed = bSlowed;
	}
	if (!BulletTimeMID)
	{
		return;
	}
	BulletTimeMID->SetScalarParameterValue(TEXT("Amount"), Alpha);
	BulletTimeMID->SetScalarParameterValue(TEXT("EntryAge"), Time ? Time->GetEntryAge() : 100.0f);
	BulletTimeMID->SetScalarParameterValue(TEXT("ArcVisible"), 0.0f);
	int32 Width = 0;
	int32 Height = 0;
	if (const APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->GetViewportSize(Width, Height);
	}
	BulletTimeMID->SetScalarParameterValue(TEXT("Aspect"), Height > 0 ? float(Width) / Height : 1.0f);
	for (FWeightedBlendable& Blendable : Camera->PostProcessSettings.WeightedBlendables.Array)
	{
		if (Blendable.Object == BulletTimeMID)
		{
			Blendable.Weight = Alpha > 0.0f ? 1.0f : 0.0f;
		}
	}
}

// --- console probe ----------------------------------------------------------------

namespace
{
	void LogProbe(const TCHAR* Stage, const AJamZeroGPawn* Pawn)
	{
		const FVector Velocity = Pawn->GetBodyVelocity();
		UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [%s]: gravity Z %.1f, pawn velocity (%.0f, %.0f, %.0f) = %.0f cm/s, O2 %.1f s"),
			Stage, Pawn->GetWorld()->GetGravityZ(), Velocity.X, Velocity.Y, Velocity.Z, Velocity.Size(), Pawn->Oxygen->Seconds);
	}

	/**
	 * Back to the start at rest, the heaviest loose prop brought beside the pawn
	 * and grabbed, then a push off the anchor: pawn and prop should leave
	 * together at about PushImpulse / (pawn mass + prop mass).
	 */
	void ProbeCoupledPush(AJamZeroGPawn* Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		UPrimitiveComponent* Heaviest = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (Root && Root != Pawn->Body && Root->IsSimulatingPhysics() && (!Heaviest || Root->GetMass() > Heaviest->GetMass()))
			{
				Heaviest = Root;
			}
		}
		if (!Heaviest)
		{
			UE_LOG(LogMindKore, Warning, TEXT("ZeroG probe: no loose body to grab"));
			return;
		}
		Pawn->Body->SetWorldLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Heaviest->SetWorldLocation(FVector(-300.0f, -300.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Heaviest->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Heaviest->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Pawn->Grab(Heaviest, Heaviest->GetComponentLocation());
		Pawn->Push();
		const float Expected = Pawn->PushImpulse / (Pawn->Body->GetMass() + Heaviest->GetMass());
		const float RestDistance = FVector::Dist(Pawn->Body->GetComponentLocation(), Heaviest->GetComponentLocation());
		// Sampled at 0.5, 1, 2 and 3 s; the last one lets go.
		for (const float Seconds : { 0.5f, 1.0f, 2.0f, 3.0f })
		{
			FTimerHandle After;
			World->GetTimerManager().SetTimer(After, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Heaviest, Expected, RestDistance, Seconds]()
			{
				const FVector PawnVelocity = Pawn->GetBodyVelocity();
				const FVector PropVelocity = Heaviest->GetPhysicsLinearVelocity();
				const float Momentum = (PawnVelocity * Pawn->Body->GetMass() + PropVelocity * Heaviest->GetMass()).Size();
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [coupled push +%.1f s]: pawn %.0f cm/s, held %s %.0f kg %.0f cm/s (expected both near %.0f), momentum %.0f kg cm/s, joint stretch %.0f cm"),
					Seconds, PawnVelocity.Size(), *GetNameSafe(Heaviest->GetOwner()), Heaviest->GetMass(), PropVelocity.Size(), Expected,
					Momentum, FVector::Dist(Pawn->Body->GetComponentLocation(), Heaviest->GetComponentLocation()) - RestDistance);
				if (Seconds >= 3.0f)
				{
					Pawn->Release();
				}
			}), Seconds, false);
		}
	}

	/** jam.ZeroG.Probe: push off what is in view, then shoot, logging the pawn's velocity around each. */
	void RunZeroGProbe(UWorld* World)
	{
		AJamZeroGPawn* Pawn = nullptr;
		for (TActorIterator<AJamZeroGPawn> It(World); It; ++It)
		{
			Pawn = *It;
			break;
		}
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("ZeroG probe: no AJamZeroGPawn in %s"), *World->GetMapName());
			return;
		}
		LogProbe(TEXT("before"), Pawn);
		Pawn->Push();
		FTimerHandle AfterPush;
		World->GetTimerManager().SetTimer(AfterPush, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
		{
			LogProbe(TEXT("after push"), Pawn);
			Pawn->Fire();
			FTimerHandle AfterShot;
			Pawn->GetWorld()->GetTimerManager().SetTimer(AfterShot, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
			{
				LogProbe(TEXT("after shot"), Pawn);
				ProbeCoupledPush(Pawn);
			}), 0.25f, false);
		}), 0.25f, false);
	}

	FAutoConsoleCommandWithWorld GZeroGProbe(
		TEXT("jam.ZeroG.Probe"),
		TEXT("Zero-G pawn: push off what is in view, then shoot; logs the pawn velocity around each."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunZeroGProbe));
}
