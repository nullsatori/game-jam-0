#include "JamZeroGPawn.h"

#include "Camera/CameraComponent.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/StaticMesh.h"
#include "DrawDebugHelpers.h"
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
#include "JamDockZone.h"
#include "JamOxygen.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MKFastener.h"
#include "MKGlassPane.h"
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
	Camera->SetFieldOfView(CameraFOV);

	// World space, not attached: the specks stay put while the camera flies
	// through them (UpdateDust wraps the ones left behind).
	Dust = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Dust"));
	Dust->SetupAttachment(Body);
	Dust->SetUsingAbsoluteLocation(true);
	Dust->SetUsingAbsoluteRotation(true);
	Dust->SetUsingAbsoluteScale(true);
	Dust->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Dust->SetCastShadow(false);
	Dust->SetCanEverAffectNavigation(false);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> DustShape(TEXT("/Engine/BasicShapes/Cube.Cube"));
	Dust->SetStaticMesh(DustShape.Object);

	Interaction = CreateDefaultSubobject<UMKInteractionComponent>(TEXT("Interaction"));
	Keyring = CreateDefaultSubobject<UMKKeyringComponent>(TEXT("Keyring"));
	Vitals = CreateDefaultSubobject<UMKVitalsComponent>(TEXT("Vitals"));
	Oxygen = CreateDefaultSubobject<UJamOxygenComponent>(TEXT("Oxygen"));

	// Guns as movement. Pistol: quick nudges (15 cm/s); shotgun: a shove and a
	// spray that scatters junk; sniper: one slow shot that throws you back 4 m/s.
	FJamWeapon Pistol;
	Pistol.Name = NSLOCTEXT("JamZeroG", "Pistol", "Пистолет");
	Pistol.Damage = 25.0f;
	Pistol.FireInterval = 0.15f;
	Pistol.MagazineSize = 12;
	Pistol.Reserve = 60;
	Pistol.ReloadSeconds = 1.2f;
	Pistol.SpreadDeg = 0.5f;
	Pistol.RecoilImpulse = 1500.0f;
	Pistol.HitImpulse = 3000.0f;
	Pistol.SoundPitch = 1.3f;
	Pistol.bAutomatic = true;
	FJamWeapon Shotgun;
	Shotgun.Name = NSLOCTEXT("JamZeroG", "Shotgun", "Дробовик");
	Shotgun.Damage = 12.0f;
	Shotgun.FireInterval = 0.8f;
	Shotgun.MagazineSize = 6;
	Shotgun.Reserve = 24;
	Shotgun.ReloadSeconds = 2.0f;
	Shotgun.Pellets = 8;
	Shotgun.SpreadDeg = 6.0f;
	Shotgun.RecoilImpulse = 12000.0f;
	Shotgun.HitImpulse = 4000.0f;
	Shotgun.RangeCm = 3000.0f;
	Shotgun.SoundPitch = 0.9f;
	FJamWeapon Sniper;
	Sniper.Name = NSLOCTEXT("JamZeroG", "Sniper", "Снапа");
	Sniper.Damage = 150.0f;
	Sniper.FireInterval = 1.2f;
	Sniper.MagazineSize = 4;
	Sniper.Reserve = 16;
	Sniper.ReloadSeconds = 2.5f;
	Sniper.RecoilImpulse = 40000.0f;
	Sniper.HitImpulse = 60000.0f;
	Sniper.RangeCm = 15000.0f;
	Sniper.SoundPitch = 0.7f;
	Weapons = { Pistol, Shotgun, Sniper };

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
	// A plate struck: the armour hit cue (CC0 donors, see CREDITS.md).
	static ConstructorHelpers::FObjectFinder<USoundBase> Crash(
		TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Armor.SC_MK_Hit_Armor"));
	CrashSound = Crash.Object;
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
	Camera->SetFieldOfView(CameraFOV);
	if (UMaterialInterface* Speck = DustMaterial.LoadSynchronous())
	{
		Dust->SetMaterial(0, Speck);
	}
	{
		FRandomStream Random(DustCount);
		const FVector Origin = Camera->GetComponentLocation();
		const FVector Scale(DustSpeckCm / 100.0f);
		TArray<FTransform> Specks;
		for (int32 Index = 0; Index < DustCount; ++Index)
		{
			const FVector Offset(Random.FRandRange(-0.5f, 0.5f), Random.FRandRange(-0.5f, 0.5f), Random.FRandRange(-0.5f, 0.5f));
			Specks.Emplace(FQuat::Identity, Origin + Offset * DustCubeCm, Scale);
		}
		Dust->AddInstances(Specks, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
	}
	BodyMaterial = NewObject<UPhysicalMaterial>(this, TEXT("ZeroGBodyMaterial"));
	BodyMaterial->Friction = BodyFriction;
	BodyMaterial->Restitution = BodyRestitution;
	Body->SetPhysMaterialOverride(BodyMaterial);
	for (FJamWeapon& Weapon : Weapons)
	{
		Weapon.Ammo = Weapon.MagazineSize;
	}
	Body->SetNotifyRigidBodyCollision(true);
	Body->OnComponentHit.AddDynamic(this, &AJamZeroGPawn::HandleBodyHit);
	if (BulletTimeMaterial)
	{
		BulletTimeMID = UMaterialInstanceDynamic::Create(BulletTimeMaterial, this);
		Camera->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0.0f, BulletTimeMID));
	}
	UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: %.0f kg, world gravity Z %.1f, FOV %.0f, %d dust specks"), *GetName(),
		Body->GetMass(), GetWorld()->GetGravityZ(), Camera->FieldOfView, Dust->GetInstanceCount());
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
	UInputAction* TetherAction = MakeAction(TEXT("IA_ZG_Tether"), EInputActionValueType::Boolean);
	UInputAction* ReloadAction = MakeAction(TEXT("IA_ZG_Reload"), EInputActionValueType::Boolean);
	UInputAction* WheelAction = MakeAction(TEXT("IA_ZG_Wheel"), EInputActionValueType::Axis1D);
	UInputAction* AimAction = MakeAction(TEXT("IA_ZG_Aim"), EInputActionValueType::Boolean);
	UInputAction* WeaponActions[3] = {
		MakeAction(TEXT("IA_ZG_Weapon1"), EInputActionValueType::Boolean),
		MakeAction(TEXT("IA_ZG_Weapon2"), EInputActionValueType::Boolean),
		MakeAction(TEXT("IA_ZG_Weapon3"), EInputActionValueType::Boolean) };

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
	Map(TetherAction, EKeys::T, false, false);
	Map(ReloadAction, EKeys::R, false, false);
	Map(WheelAction, EKeys::MouseWheelAxis, false, false);
	Map(AimAction, EKeys::LeftShift, false, false);
	Map(WeaponActions[0], EKeys::One, false, false);
	Map(WeaponActions[1], EKeys::Two, false, false);
	Map(WeaponActions[2], EKeys::Three, false, false);

	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Look);
	Input->BindAction(ThrustAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Thrust);
	Input->BindAction(LiftAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Lift);
	Input->BindActionValueLambda(StabiliseAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bStabilising = true; });
	Input->BindActionValueLambda(StabiliseAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bStabilising = false; });
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AJamZeroGPawn::Use);
	// Act on press. An automatic gun keeps firing while LMB is held; Fire's
	// interval gate makes the press frame (Started and Triggered) one shot.
	Input->BindAction(PrimaryAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PrimaryPressed);
	Input->BindActionValueLambda(PrimaryAction, ETriggerEvent::Triggered, [this](const FInputActionValue&)
	{
		const FJamWeapon* Weapon = GetWeapon();
		if (!GrabbedBody.IsValid() && Weapon && Weapon->bAutomatic)
		{
			Fire();
		}
	});
	Input->BindAction(PushAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PushPressed);
	// Left Shift held: the predicted paths are drawn.
	Input->BindActionValueLambda(AimAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bAiming = true; });
	Input->BindActionValueLambda(AimAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bAiming = false; });
	Input->BindAction(PutAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PutDown);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Started, this, &AJamZeroGPawn::BulletTimePressed);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Completed, this, &AJamZeroGPawn::BulletTimeReleased);
	Input->BindAction(CrowbarAction, ETriggerEvent::Started, this, &AJamZeroGPawn::ToggleCrowbar);
	Input->BindAction(TetherAction, ETriggerEvent::Started, this, &AJamZeroGPawn::ToggleTether);
	Input->BindAction(ReloadAction, ETriggerEvent::Started, this, &AJamZeroGPawn::Reload);
	Input->BindActionValueLambda(WheelAction, ETriggerEvent::Started, [this](const FInputActionValue& Value)
	{
		if (Weapons.Num() > 0)
		{
			SelectWeapon((CurrentWeapon + (Value.Get<float>() > 0.0f ? 1 : Weapons.Num() - 1)) % Weapons.Num());
		}
	});
	for (int32 Index = 0; Index < 3; ++Index)
	{
		Input->BindActionValueLambda(WeaponActions[Index], ETriggerEvent::Started, [this, Index](const FInputActionValue&) { SelectWeapon(Index); });
	}
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
	// Force mode, not the default acceleration mode: a force acts equally and
	// oppositely on both bodies whatever their masses.
	GrabConstraint->SetLinearDriveAccelerationMode(false);
	GrabConstraint->SetLinearDriveParams(GrabStiffness, GrabDamping, /*ForceLimit=*/0.0f);
	// The prop is component 1 (the child), the pawn component 2 (the parent):
	// the drive's position target is the child frame's position in the parent
	// frame. Frame 2 moves to the pawn's centre (its rotation is locked, so the
	// frame never turns); frame 1 stays at the grab point on the prop. The
	// target is then where the grab point should sit from the pawn's centre, in
	// body space (UpdateHoldTarget, every tick). The other way round the target
	// lives in the prop's spinning frame - seen in the probe as the part shoved
	// to the opposite side.
	// Mass conditioning rescales the two masses for stability: momentum is then
	// not conserved (the probe saw 45000 kg cm/s after a 30000 push with an
	// 800 kg part). Off, the pair moves as 100 + 800 kg.
	GrabConstraint->ConstraintInstance.DisableMassConditioning();
	GrabConstraint->SetConstrainedComponents(Target, NAME_None, Body, NAME_None);
	GrabOffsetLocal = Body->GetComponentTransform().InverseTransformVectorNoScale(Point - Body->GetComponentLocation());
	GrabConstraint->SetConstraintReferencePosition(EConstraintFrame::Frame2, FVector::ZeroVector);
	GrabbedBody = Target;
	UpdateHoldTarget();
	GrabCandidate = nullptr;
	Target->WakeAllRigidBodies();
	UE_LOG(LogMindKore, Log, TEXT("ZeroG grab: %s, %.0f kg"), *GetNameSafe(Target->GetOwner()), Target->GetMass());
	return true;
}

void AJamZeroGPawn::UpdateHoldTarget()
{
	if (!GrabConstraint)
	{
		return;
	}
	const FVector Target = bHoldInFront
		? Body->GetComponentTransform().InverseTransformVectorNoScale(
			ViewLocation() - Body->GetComponentLocation() + ViewDirection() * HoldDistanceCm)
		: GrabOffsetLocal;
	GrabConstraint->SetLinearPositionTarget(Target);
}

void AJamZeroGPawn::UpdateDust()
{
	// A speck left more than half a cube behind re-enters on the far side: the
	// cube follows the camera, each speck stays still in the world until then.
	const FVector Centre = Camera->GetComponentLocation();
	const double Half = DustCubeCm * 0.5;
	bool bAnyWrapped = false;
	for (int32 Index = 0; Index < Dust->GetInstanceCount(); ++Index)
	{
		FTransform Speck;
		Dust->GetInstanceTransform(Index, Speck, /*bWorldSpace=*/true);
		FVector Offset = Speck.GetLocation() - Centre;
		bool bWrapped = false;
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			if (Offset[Axis] > Half || Offset[Axis] < -Half)
			{
				Offset[Axis] = FMath::Wrap(Offset[Axis], -Half, Half);
				bWrapped = true;
			}
		}
		if (bWrapped)
		{
			Speck.SetLocation(Centre + Offset);
			Dust->UpdateInstanceTransform(Index, Speck, /*bWorldSpace=*/true, /*bMarkRenderStateDirty=*/false);
			bAnyWrapped = true;
		}
	}
	if (bAnyWrapped)
	{
		Dust->MarkRenderStateDirty();
	}
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

void AJamZeroGPawn::ToggleTether()
{
	if (TetheredBody.IsValid())
	{
		DetachTether();
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamTetherLook), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + ViewDirection() * TetherReachCm, ECC_Visibility, Params))
	{
		AttachTether(Hit.GetComponent());
	}
	else
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG tether: nothing within %.0f cm"), TetherReachCm);
	}
}

bool AJamZeroGPawn::AttachTether(UPrimitiveComponent* Target)
{
	if (!Target || !Target->IsSimulatingPhysics() || Target == Body)
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG tether: %s is not a loose body"), *GetNameSafe(Target ? Target->GetOwner() : nullptr));
		return false;
	}
	DetachTether();
	// A rope is a distance limit, not a spring: slack until TetherLengthCm, then
	// a soft spherical limit between the pawn's centre and the part's origin.
	TetherConstraint = NewObject<UPhysicsConstraintComponent>(this);
	TetherConstraint->SetupAttachment(Body);
	TetherConstraint->RegisterComponent();
	TetherConstraint->SetWorldLocation(Body->GetComponentLocation());
	TetherConstraint->SetLinearXLimit(LCM_Limited, TetherLengthCm);
	TetherConstraint->SetLinearYLimit(LCM_Limited, TetherLengthCm);
	TetherConstraint->SetLinearZLimit(LCM_Limited, TetherLengthCm);
	TetherConstraint->SetAngularSwing1Limit(ACM_Free, 0.0f);
	TetherConstraint->SetAngularSwing2Limit(ACM_Free, 0.0f);
	TetherConstraint->SetAngularTwistLimit(ACM_Free, 0.0f);
	FLinearConstraint& Limit = TetherConstraint->ConstraintInstance.ProfileInstance.LinearLimit;
	Limit.bSoftConstraint = TetherStiffness > 0.0f;
	Limit.Stiffness = TetherStiffness;
	Limit.Damping = TetherDamping;
	TetherConstraint->ConstraintInstance.DisableMassConditioning();
	TetherConstraint->SetConstrainedComponents(Body, NAME_None, Target, NAME_None);
	// Frame 2 from the pawn's position in the part's space to the part's origin.
	TetherConstraint->SetConstraintReferencePosition(EConstraintFrame::Frame2, FVector::ZeroVector);
	TetheredBody = Target;
	Target->WakeAllRigidBodies();
	UE_LOG(LogMindKore, Log, TEXT("ZeroG tether: on %s, %.0f kg, %.0f cm away, rope %.0f cm"), *GetNameSafe(Target->GetOwner()),
		Target->GetMass(), FVector::Dist(Body->GetComponentLocation(), Target->GetComponentLocation()), TetherLengthCm);
	return true;
}

void AJamZeroGPawn::DetachTether()
{
	if (TetherConstraint)
	{
		TetherConstraint->BreakConstraint();
		TetherConstraint->DestroyComponent();
		TetherConstraint = nullptr;
	}
	if (TetheredBody.IsValid())
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG tether: cut from %s"), *GetNameSafe(TetheredBody->GetOwner()));
	}
	TetheredBody = nullptr;
}

void AJamZeroGPawn::DrawTether() const
{
	const UPrimitiveComponent* Tethered = TetheredBody.Get();
	if (!Tethered)
	{
		return;
	}
	// ponytail: a debug line (Development builds), straight; a UCableComponent
	// (CableComponent plugin) if the rope must sag or ship in Shipping.
	const FVector From = Body->GetComponentLocation() - FVector(0.0f, 0.0f, 20.0f);
	const FVector To = Tethered->GetComponentLocation();
	const bool bTaut = FVector::Dist(From, To) >= TetherLengthCm * 0.97f;
	DrawDebugLine(GetWorld(), From, To, bTaut ? FColor(255, 150, 40) : FColor(150, 150, 150), false, -1.0f, 0, bTaut ? 3.0f : 1.5f);
}

void AJamZeroGPawn::HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherComp && OtherComp == GrabbedBody.Get())
	{
		return;
	}
	float SpeedCm = 0.0f;
	JamDeliverCollisionImpact(this, Body, Hit, NormalImpulse, CrashImpact, LastCrashSeconds, SpeedCm);
	if (SpeedCm < FeedbackMinSpeedCm || ShakeRemaining > ShakeSeconds * 0.5f)
	{
		return; // A touch, or the same crash still shaking.
	}
	if (CrashSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, CrashSound, Hit.ImpactPoint, FMath::Clamp(SpeedCm / 1000.0f, 0.2f, 1.0f));
	}
	ShakeRemaining = ShakeSeconds;
	ShakeAmplitude = ShakeCmPer1000 * SpeedCm / 1000.0f;
	if (SpeedCm >= CrashDamageMinSpeedCm)
	{
		const float Damage = (SpeedCm - CrashDamageMinSpeedCm) * CrashDamagePerCmS;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG crash: %s at %.0f cm/s, self damage %.1f"), *GetNameSafe(OtherActor), SpeedCm, Damage);
		Vitals->ApplyIncoming(Damage, EMKImpactKind::Blunt, /*bBlocked=*/false, FGuid());
	}
}

void AJamZeroGPawn::ProbeAim(bool bAim)
{
	bAiming = bAim;
	bLogNextPrediction = bAim;
}

void AJamZeroGPawn::DrawPrediction() const
{
	if (!bAiming)
	{
		return;
	}
	const FVector Direction = ViewDirection();
	if (UPrimitiveComponent* Held = GrabbedBody.Get())
	{
		// LMB would throw: where the prop goes.
		const float MassKg = FMath::Max(Held->GetMass(), 0.01f);
		const float Impulse = FMath::Min(ThrowImpulse, MassKg * MaxThrowSpeedCm);
		DrawPath(Held->GetComponentLocation(), Held->GetPhysicsLinearVelocity() + Direction * Impulse / MassKg,
			Held->Bounds.SphereRadius, Held->GetOwner(), FColor(120, 220, 255));
		return;
	}
	// RMB would push (orange, only with something in reach); LMB would recoil (yellow).
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamPushAim), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + Direction * PushReachCm, ECC_Visibility, Params))
	{
		DrawPath(Body->GetComponentLocation(), GetBodyVelocity() - Direction * PushImpulse / Body->GetMass(),
			Body->GetScaledSphereRadius(), nullptr, FColor(255, 140, 40));
	}
	if (const FJamWeapon* Weapon = GetWeapon(); Weapon && Weapon->RecoilImpulse > 0.0f)
	{
		DrawPath(Body->GetComponentLocation(), GetBodyVelocity() - Direction * Weapon->RecoilImpulse / Body->GetMass(),
			Body->GetScaledSphereRadius(), nullptr, FColor(255, 230, 90));
	}
}

void AJamZeroGPawn::DrawPath(const FVector& Start, const FVector& Velocity, float RadiusCm, const AActor* Ignored, const FColor& Color) const
{
	// ponytail: straight segments and one reflection, drawn with debug lines
	// (Development builds); no spin, no drag, the other body treated as static.
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamPredict), /*bTraceComplex=*/false, this);
	if (Ignored)
	{
		Params.AddIgnoredActor(Ignored);
	}
	FVector Position = Start;
	FVector Speed = Velocity;
	float Seconds = PredictSeconds;
	for (int32 Bounce = 0; Bounce < 2 && Speed.SizeSquared() > 1.0f; ++Bounce)
	{
		const FVector End = Position + Speed * Seconds;
		FHitResult Hit;
		const bool bHit = World->SweepSingleByChannel(Hit, Position, End, FQuat::Identity, ECC_PhysicsBody,
			FCollisionShape::MakeSphere(RadiusCm), Params);
		const FVector Stop = bHit ? FVector(Hit.Location) : End;
		// Dashes of 25 cm every 40 cm.
		const float Length = FVector::Dist(Position, Stop);
		const FVector Along = (Stop - Position).GetSafeNormal();
		for (float At = 0.0f; At < Length; At += 40.0f)
		{
			DrawDebugLine(World, Position + Along * At, Position + Along * FMath::Min(At + 25.0f, Length), Color, false, -1.0f, 0, 2.0f);
		}
		if (!bHit)
		{
			if (Bounce == 0 && bLogNextPrediction)
			{
				bLogNextPrediction = false;
				UE_LOG(LogMindKore, Log, TEXT("ZeroG prediction: from (%.0f, %.0f, %.0f) at (%.0f, %.0f, %.0f) cm/s, no contact within %.1f s, ends at (%.0f, %.0f, %.0f)"),
					Start.X, Start.Y, Start.Z, Velocity.X, Velocity.Y, Velocity.Z, PredictSeconds, End.X, End.Y, End.Z);
			}
			return;
		}
		if (Bounce == 0 && bLogNextPrediction)
		{
			bLogNextPrediction = false;
			UE_LOG(LogMindKore, Log, TEXT("ZeroG prediction: from (%.0f, %.0f, %.0f) at %.0f cm/s, first contact %s at (%.0f, %.0f, %.0f) in %.2f s"),
				Start.X, Start.Y, Start.Z, Velocity.Size(), *GetNameSafe(Hit.GetActor()), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
				Hit.ImpactPoint.Z, PredictSeconds * Hit.Time);
		}
		if (Bounce == 0)
		{
			DrawDebugSphere(World, Hit.ImpactPoint, 15.0f, 8, FColor(255, 60, 40), false, -1.0f, 0, 2.0f);
		}
		const FVector Normal = Hit.ImpactNormal;
		const FVector Into = Normal * FVector::DotProduct(Speed, Normal);
		Speed = (Speed - Into) - Into * PredictRestitution;
		Seconds *= 1.0f - Hit.Time;
		Position = FVector(Hit.Location) + Normal * 0.5f;
	}
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
	if (!bThrustersEnabled || DeltaSeconds <= 0.0f)
	{
		return;
	}
	if (bStabilising)
	{
		// X: deceleration proportional to speed up to a cap - firm when fast,
		// easing out near standstill, never overshooting it.
		const FVector Velocity = GetBodyVelocity();
		const float Speed = Velocity.Size();
		const float Change = FMath::Min(FMath::Min(StabiliserAccelCm, Speed * StabiliserRate) * DeltaSeconds, Speed);
		if (Speed > KINDA_SMALL_NUMBER)
		{
			Body->AddImpulse(-Velocity / Speed * Change, NAME_None, /*bVelChange=*/true);
		}
		Oxygen->Spend(Oxygen->StabiliserPerSecond * DeltaSeconds);
	}
	if (Input.IsNearlyZero())
	{
		return;
	}
	// Along the input direction only, up to ThrustMaxSpeedCm along it: the
	// other components of the velocity (a push, a recoil) are left alone, and
	// a faster burst along the same direction is not braked either.
	const FRotationMatrix View(GetControlRotation());
	const FVector Direction = (View.GetUnitAxis(EAxis::X) * Input.X + View.GetUnitAxis(EAxis::Y) * Input.Y
		+ View.GetUnitAxis(EAxis::Z) * Input.Z).GetSafeNormal();
	const float Along = FVector::DotProduct(GetBodyVelocity(), Direction);
	const float Add = FMath::Min(ThrustAccelCm * DeltaSeconds, ThrustMaxSpeedCm - Along);
	if (Add > 0.0f)
	{
		Body->AddImpulse(Direction * Add, NAME_None, /*bVelChange=*/true);
		Oxygen->Spend(Oxygen->ThrusterPerSecond * DeltaSeconds);
	}
}

void AJamZeroGPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (GrabConstraint && !GrabbedBody.IsValid())
	{
		Release();
	}
	if (TetherConstraint && !TetheredBody.IsValid())
	{
		DetachTether();
	}
	DrawTether();
	DrawPrediction();
	// Camera shake: a fading random offset of the lens; the body never turns.
	ShakeRemaining = FMath::Max(0.0f, ShakeRemaining - DeltaSeconds);
	Camera->SetRelativeLocation(ShakeRemaining > 0.0f
		? FMath::VRand() * ShakeAmplitude * (ShakeRemaining / FMath::Max(ShakeSeconds, 0.01f))
		: FVector::ZeroVector);
	RefreshGrabCandidate();
	UpdateHoldTarget();
	UpdateDust();
	ApplyThrusters(DeltaSeconds);
	if (ReloadEndSeconds > 0.0 && GetWorld()->GetTimeSeconds() >= ReloadEndSeconds && Weapons.IsValidIndex(CurrentWeapon))
	{
		FJamWeapon& Weapon = Weapons[CurrentWeapon];
		const int32 Moved = FMath::Min(Weapon.MagazineSize - Weapon.Ammo, Weapon.Reserve);
		Weapon.Ammo += Moved;
		Weapon.Reserve -= Moved;
		ReloadEndSeconds = 0.0;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG weapon: %s reloaded, %d/%d"), *Weapon.Name.ToString(), Weapon.Ammo, Weapon.Reserve);
	}
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

void AJamZeroGPawn::SelectWeapon(int32 Index)
{
	if (!Weapons.IsValidIndex(Index) || Index == CurrentWeapon)
	{
		return;
	}
	CurrentWeapon = Index;
	ReloadEndSeconds = 0.0; // A switch abandons a reload.
	UE_LOG(LogMindKore, Log, TEXT("ZeroG weapon: %s (%d/%d)"), *Weapons[Index].Name.ToString(), Weapons[Index].Ammo, Weapons[Index].Reserve);
}

float AJamZeroGPawn::GetReloadRemaining() const
{
	return FMath::Max(0.0f, float(ReloadEndSeconds - GetWorld()->GetTimeSeconds()));
}

void AJamZeroGPawn::Reload()
{
	FJamWeapon* Weapon = Weapons.IsValidIndex(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
	if (!Weapon || Weapon->Ammo >= Weapon->MagazineSize || Weapon->Reserve <= 0 || GetReloadRemaining() > 0.0f)
	{
		return;
	}
	ReloadEndSeconds = GetWorld()->GetTimeSeconds() + Weapon->ReloadSeconds;
	UE_LOG(LogMindKore, Log, TEXT("ZeroG weapon: reloading %s, %.1f s"), *Weapon->Name.ToString(), Weapon->ReloadSeconds);
}

bool AJamZeroGPawn::Fire()
{
	FJamWeapon* Weapon = Weapons.IsValidIndex(CurrentWeapon) ? &Weapons[CurrentWeapon] : nullptr;
	const double Now = GetWorld()->GetTimeSeconds();
	if (!Weapon || Now < NextShotSeconds || GetReloadRemaining() > 0.0f)
	{
		return false;
	}
	if (Weapon->Ammo <= 0)
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG weapon: %s is empty"), *Weapon->Name.ToString());
		Reload();
		return false;
	}
	--Weapon->Ammo;
	NextShotSeconds = Now + Weapon->FireInterval;
	const FVector Direction = ViewDirection();
	// One kick per shot, whatever the pellets do. Guns cost no oxygen.
	AddBodyImpulse(-Direction * Weapon->RecoilImpulse);
	for (USoundBase* Sound : ShotSounds)
	{
		UGameplayStatics::PlaySound2D(this, Sound, 1.0f, Weapon->SoundPitch);
	}
	for (int32 Pellet = 0; Pellet < Weapon->Pellets; ++Pellet)
	{
		FireRay(*Weapon, Weapon->SpreadDeg > 0.0f
			? FMath::VRandCone(Direction, FMath::DegreesToRadians(Weapon->SpreadDeg))
			: Direction);
	}
	UE_LOG(LogMindKore, Log, TEXT("ZeroG weapon: %s fired, %d pellet(s), recoil %.0f kg cm/s, %d/%d left"),
		*Weapon->Name.ToString(), Weapon->Pellets, Weapon->RecoilImpulse, Weapon->Ammo, Weapon->Reserve);
	return true;
}

void AJamZeroGPawn::FireRay(const FJamWeapon& Weapon, const FVector& Direction)
{
	UWorld* World = GetWorld();
	const FVector Start = ViewLocation();
	// ponytail: the same trace, hit choice and impact fill as AJamCharacter::Fire,
	// copied so the ground pawn stays untouched; merge into one free function if
	// both pawns outlive the jam.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamZeroGShot), /*bTraceComplex=*/true, this);
	Params.bReturnPhysicalMaterial = true;
	TArray<FHitResult> Hits;
	World->LineTraceMultiByChannel(Hits, Start, Start + Direction * Weapon.RangeCm, ECC_GameTraceChannel2, Params);
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
	Resolved.DamageAmount = Weapon.Damage * Resolved.DamageMultiplier;
	// N s for receivers that read it (another pawn); kg cm/s / 100.
	Resolved.ImpulseNewtonSeconds = Resolved.ShotDirection * Weapon.HitImpulse / 100.0f;
	Resolved.bHasPhysicsImpulse = true;
	const bool bDelivered = MKDeliverResolvedImpact(this, this, Resolved);
	// Every loose body takes the gun's push at the hit point (MindKore's props
	// ignore rounds; in zero-G the push is the point).
	UPrimitiveComponent* HitComponent = Hit.GetComponent();
	if (HitComponent && HitComponent->IsSimulatingPhysics() && HitComponent->GetOwner() != this)
	{
		HitComponent->AddImpulseAtLocation(Direction * Weapon.HitImpulse, Hit.ImpactPoint);
	}
	UE_LOG(LogMindKore, Verbose, TEXT("ZeroG shot: %s / %s, zone %d, damage %.1f, %s"), *GetNameSafe(Hit.GetActor()),
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
		UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [%s]: gravity Z %.1f, pawn velocity (%.0f, %.0f, %.0f) = %.0f cm/s, O2 %.1f s, body rotation %s, view %s"),
			Stage, Pawn->GetWorld()->GetGravityZ(), Velocity.X, Velocity.Y, Velocity.Z, Velocity.Size(), Pawn->Oxygen->Seconds,
			*Pawn->Body->GetComponentRotation().ToCompactString(), *Pawn->GetControlRotation().ToCompactString());
	}

	/**
	 * Thrusters: at rest, then sideways 300 cm/s, then forward thrust held for
	 * 1.5 s (along X only, up to ThrustMaxSpeedCm; Y untouched), then the
	 * stabiliser held for 3 s, sampled every half second.
	 */
	void ProbeThrusters(AJamZeroGPawn* Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		Pawn->Body->SetWorldLocation(FVector(-1500.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector(0.0f, 300.0f, 0.0f));
		const double Start = World->GetTimeSeconds();
		const TSharedRef<FTimerHandle> Steer = MakeShared<FTimerHandle>();
		World->GetTimerManager().SetTimer(*Steer, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Start, Steer]()
		{
			const double Elapsed = Pawn->GetWorld()->GetTimeSeconds() - Start;
			if (Elapsed >= 4.5)
			{
				// Stop steering, or the probe would overwrite the player's thrust input forever.
				Pawn->ProbeThrust(FVector::ZeroVector, false);
				Pawn->GetWorldTimerManager().ClearTimer(*Steer);
				return;
			}
			// The view faces +X: forward thrust for 1.5 s, then the stabiliser to 4.5 s.
			Pawn->ProbeThrust(Elapsed < 1.5 ? FVector(1.0f, 0.0f, 0.0f) : FVector::ZeroVector, Elapsed >= 1.5);
		}), 0.0005f, true);
		for (int32 Step = 1; Step <= 10; ++Step)
		{
			FTimerHandle Sample;
			World->GetTimerManager().SetTimer(Sample, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Step]()
			{
				const FVector Velocity = Pawn->GetBodyVelocity();
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [thrusters +%.1f s, %s]: velocity (%.0f, %.0f, %.0f) = %.0f cm/s, O2 %.1f s"),
					Step * 0.5f, Step <= 3 ? TEXT("forward") : Step <= 9 ? TEXT("stabiliser") : TEXT("free"),
					Velocity.X, Velocity.Y, Velocity.Z, Velocity.Size(), Pawn->Oxygen->Seconds);
			}), Step * 0.5f, false);
		}
	}

	/** The dock: one Ship.Part put inside the zone at rest; the count is logged before and after. */
	void ProbeDock(AJamZeroGPawn* Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		AJamDockZone* Dock = nullptr;
		AActor* Part = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Dock = Dock ? Dock : Cast<AJamDockZone>(*It);
			Part = Part ? Part : (It->ActorHasTag(TEXT("Ship.Part")) ? *It : nullptr);
		}
		UPrimitiveComponent* Root = Part ? Cast<UPrimitiveComponent>(Part->GetRootComponent()) : nullptr;
		if (!Dock || !Root)
		{
			UE_LOG(LogMindKore, Warning, TEXT("ZeroG probe: no dock zone or no Ship.Part"));
			return;
		}
		UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [dock]: %d/%d before; moving %s (%.0f kg) inside"), Dock->GetCount(),
			Dock->RequiredCount, *Part->GetName(), Root->GetMass());
		Root->SetWorldLocation(Dock->GetActorLocation() - FVector(0.0f, 0.0f, 500.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Root->SetPhysicsLinearVelocity(FVector::ZeroVector);
		FTimerHandle After;
		World->GetTimerManager().SetTimer(After, FTimerDelegate::CreateWeakLambda(Dock, [Dock, Root]()
		{
			UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [dock]: %d/%d after 1 s, part drifting at %.0f cm/s"), Dock->GetCount(),
				Dock->RequiredCount, Root->GetPhysicsLinearVelocity().Size());
		}), 1.0f, false);
	}

	/**
	 * Glass: a 4 kg canister at 200 cm/s (under the impulse threshold), then the
	 * heaviest small body (bounding radius < 70 cm) at 800 cm/s, both thrown at
	 * the pane; the pane's remaining area is logged after each. Then the pawn
	 * is flung at the anchor at 1000 cm/s for a crash.
	 */
	void ProbeCollisions(AJamZeroGPawn* Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		AMKGlassPane* Pane = nullptr;
		UPrimitiveComponent* Light = nullptr;
		UPrimitiveComponent* Heavy = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Pane = Pane ? Pane : Cast<AMKGlassPane>(*It);
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (!Root || Root == Pawn->Body || !Root->IsSimulatingPhysics())
			{
				continue;
			}
			if (It->IsA<AJamOxygenCanister>())
			{
				Light = Light ? Light : Root;
			}
			else if (Root->Bounds.SphereRadius < 70.0f && (!Heavy || Root->GetMass() > Heavy->GetMass()))
			{
				Heavy = Root;
			}
		}
		if (!Pane || !Light || !Heavy)
		{
			UE_LOG(LogMindKore, Warning, TEXT("ZeroG probe: collisions need a pane, a canister and a small heavy body"));
			return;
		}
		const FVector Target = Pane->GetActorLocation();
		const auto Throw = [Target](UPrimitiveComponent* Thing, float Speed)
		{
			Thing->SetWorldLocation(Target - FVector(0.0f, 300.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
			Thing->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
			Thing->SetPhysicsLinearVelocity(FVector(0.0f, Speed, 0.0f));
		};
		UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [glass]: pane %.0f cm2; light %s %.0f kg at 200 cm/s"), Pane->GetRemainingAreaCm2(),
			*GetNameSafe(Light->GetOwner()), Light->GetMass());
		Throw(Light, 200.0f);
		FTimerHandle AfterLight;
		World->GetTimerManager().SetTimer(AfterLight, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Pane, Heavy, Throw]()
		{
			UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [glass]: after the light one %.0f cm2; heavy %s %.0f kg at 800 cm/s"),
				Pane->GetRemainingAreaCm2(), *GetNameSafe(Heavy->GetOwner()), Heavy->GetMass());
			Throw(Heavy, 800.0f);
			FTimerHandle AfterHeavy;
			Pawn->GetWorld()->GetTimerManager().SetTimer(AfterHeavy, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Pane]()
			{
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [glass]: after the heavy one %.0f cm2"), Pane->GetRemainingAreaCm2());
				Pawn->Body->SetWorldLocation(FVector(-200.0f, 0.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
				Pawn->Body->SetPhysicsLinearVelocity(FVector(1000.0f, 0.0f, 0.0f));
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [crash]: pawn flung at the anchor at 1000 cm/s, health %.0f"), Pawn->Vitals->Health);
				FTimerHandle AfterCrash;
				Pawn->GetWorld()->GetTimerManager().SetTimer(AfterCrash, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
				{
					LogProbe(TEXT("after crash"), Pawn);
					UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [crash]: health %.0f"), Pawn->Vitals->Health);
					ProbeDock(Pawn);
					ProbeThrusters(Pawn);
				}), 1.0f, false);
			}), 2.0f, false);
		}), 2.5f, false);
	}

	/** The rope: the part 5 m to the side, the pawn pushes off; once the rope is taut the part follows. */
	void ProbeTether(AJamZeroGPawn* Pawn, UPrimitiveComponent* Part)
	{
		Pawn->Body->SetWorldLocation(FVector::ZeroVector, false, nullptr, ETeleportType::TeleportPhysics);
		Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Part->SetWorldLocation(FVector(0.0f, -500.0f, 0.0f), false, nullptr, ETeleportType::TeleportPhysics);
		Part->SetPhysicsLinearVelocity(FVector::ZeroVector);
		Part->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Pawn->AttachTether(Part);
		Pawn->Push();
		for (const float Seconds : { 1.0f, 3.0f, 5.0f })
		{
			FTimerHandle After;
			Pawn->GetWorld()->GetTimerManager().SetTimer(After, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Part, Seconds]()
			{
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [tether +%.0f s]: rope %.0f cm (length %.0f), pawn %.0f cm/s, part %.0f kg %.0f cm/s"),
					Seconds, FVector::Dist(Pawn->Body->GetComponentLocation(), Part->GetComponentLocation()), Pawn->TetherLengthCm,
					Pawn->GetBodyVelocity().Size(), Part->GetMass(), Part->GetPhysicsLinearVelocity().Size());
				if (Seconds >= 5.0f)
				{
					LogProbe(TEXT("tether end"), Pawn);
					Pawn->DetachTether();
					ProbeCollisions(Pawn);
				}
			}), Seconds, false);
		}
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
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [coupled push +%.1f s]: pawn %s cm/s, held %s %.0f kg %s cm/s (expected both near %.0f), momentum %.0f kg cm/s, joint stretch %.0f cm"),
					Seconds, *PawnVelocity.ToCompactString(), *GetNameSafe(Heaviest->GetOwner()), Heaviest->GetMass(), *PropVelocity.ToCompactString(), Expected,
					Momentum, FVector::Dist(Pawn->Body->GetComponentLocation(), Heaviest->GetComponentLocation()) - RestDistance);
				if (Seconds >= 3.0f)
				{
					Pawn->Release();
					ProbeTether(Pawn, Heaviest);
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
		// The coupled-push measurement keeps the held thing where it was grabbed.
		Pawn->bHoldInFront = false;
		LogProbe(TEXT("before"), Pawn);
		// One frame of aiming the push: the predicted path logs its first contact.
		Pawn->ProbeAim(true);
		Pawn->Tick(0.0f);
		Pawn->ProbeAim(false);
		Pawn->Push();
		FTimerHandle AfterPush;
		World->GetTimerManager().SetTimer(AfterPush, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
		{
			LogProbe(TEXT("after push"), Pawn);
			Pawn->Fire();
			FTimerHandle AfterShot;
			Pawn->GetWorld()->GetTimerManager().SetTimer(AfterShot, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
			{
				LogProbe(TEXT("after pistol"), Pawn);
				Pawn->SelectWeapon(2);
				Pawn->Fire();
				FTimerHandle AfterSniper;
				Pawn->GetWorld()->GetTimerManager().SetTimer(AfterSniper, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
				{
					LogProbe(TEXT("after sniper"), Pawn);
					Pawn->SelectWeapon(0);
					ProbeCoupledPush(Pawn);
				}), 0.25f, false);
			}), 0.25f, false);
		}), 0.25f, false);
	}

	FAutoConsoleCommandWithWorld GZeroGProbe(
		TEXT("jam.ZeroG.Probe"),
		TEXT("Zero-G pawn: push off what is in view, then shoot; logs the pawn velocity around each."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunZeroGProbe));
}
