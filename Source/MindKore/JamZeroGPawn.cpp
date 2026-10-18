#include "JamZeroGPawn.h"

#include "JamLens.h"

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
#include "JamHullTools.h"
#include "JamOxygen.h"
#include "JamShip.h"
#include "JamLadder.h"
#include "JamWeapons.h"
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
	Guns = CreateDefaultSubobject<UJamWeaponsComponent>(TEXT("Guns"));
	HullTools = CreateDefaultSubobject<UJamHullToolsComponent>(TEXT("HullTools"));

	// A plate struck: the armour hit cue (CC0 donors, see CREDITS.md).
	static ConstructorHelpers::FObjectFinder<USoundBase> Crash(
		TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Armor.SC_MK_Hit_Armor"));
	CrashSound = Crash.Object;
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> BulletTime(
		TEXT("/Game/MindKore/PostProcess/M_MK_BulletTime.M_MK_BulletTime"));
	BulletTimeMaterial = BulletTime.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> TimeEntry(
		TEXT("/Game/MindKore/Audio/BulletTime/SW_MK_TimeEntry.SW_MK_TimeEntry"));
	BulletTimeEntrySound = TimeEntry.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> TimeExit(
		TEXT("/Game/MindKore/Audio/BulletTime/SW_MK_TimeExit.SW_MK_TimeExit"));
	BulletTimeExitSound = TimeExit.Object;
	// Flesh underfoot: MindKore's wet body hit and gristle crunch (recorded donors, CREDITS.md), pitched down when played.
	static ConstructorHelpers::FObjectFinder<USoundBase> FleshStep(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Torso.SC_MK_Hit_Torso"));
	FleshStepSound = FleshStep.Object;
	static ConstructorHelpers::FObjectFinder<USoundBase> FleshCrash(TEXT("/Game/MindKore/Audio/Impact/SC_MK_Hit_Limb.SC_MK_Hit_Limb"));
	FleshCrashSound = FleshCrash.Object;
}

namespace
{
	/** MK_Flesh (SurfaceType3, DefaultEngine.ini) from the hit, else from the component's first material
	 *  (the planetoid's complex collision and a physics contact may not report one). */
	bool IsFlesh(const FHitResult& Hit)
	{
		const UPhysicalMaterial* Material = Hit.PhysMaterial.Get();
		const UPrimitiveComponent* Component = Hit.GetComponent();
		if (!Material && Component && Component->GetMaterial(0))
		{
			Material = Component->GetMaterial(0)->GetPhysicalMaterial();
		}
		return UPhysicalMaterial::DetermineSurfaceType(Material) == SurfaceType3;
	}
}

void AJamZeroGPawn::BeginPlay()
{
	Super::BeginPlay();
	Body->SetMassOverrideInKg(NAME_None, BodyMassKg, true);
	JamApplyBodycamLens(*Camera, CameraFOV, bLensDistortion);
	if (UMaterialInterface* Speck = DustMaterial.LoadSynchronous())
	{
		Dust->SetMaterial(0, Speck);
	}
	{
		FRandomStream Random(DustCount);
		const FVector Origin = Camera->GetComponentLocation();
		TArray<FTransform> Specks;
		for (int32 Index = 0; Index < DustCount; ++Index)
		{
			const FVector Offset(Random.FRandRange(-0.5f, 0.5f), Random.FRandRange(-0.5f, 0.5f), Random.FRandRange(-0.5f, 0.5f));
			// Mostly fine dust, a few larger motes.
			const FVector Scale(DustSpeckCm / 100.0f * FMath::Lerp(0.6f, 1.3f, FMath::Square(Random.FRand())));
			Specks.Emplace(FQuat::Identity, Origin + Offset * DustCubeCm, Scale);
		}
		Dust->AddInstances(Specks, /*bShouldReturnIndices=*/false, /*bWorldSpace=*/true);
	}
	BodyMaterial = NewObject<UPhysicalMaterial>(this, TEXT("ZeroGBodyMaterial"));
	BodyMaterial->Friction = BodyFriction;
	BodyMaterial->Restitution = BodyRestitution;
	Body->SetPhysMaterialOverride(BodyMaterial);
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
	UInputAction* RollAction = MakeAction(TEXT("IA_ZG_Roll"), EInputActionValueType::Axis1D);
	UInputAction* GrappleAction = MakeAction(TEXT("IA_ZG_Grapple"), EInputActionValueType::Boolean);
	UInputAction* BulletTimeAction = MakeAction(TEXT("IA_ZG_BulletTime"), EInputActionValueType::Boolean);
	UInputAction* CrowbarAction = MakeAction(TEXT("IA_ZG_Crowbar"), EInputActionValueType::Boolean);
	UInputAction* StrapAction = MakeAction(TEXT("IA_ZG_Strap"), EInputActionValueType::Boolean);
	UInputAction* WelderAction = MakeAction(TEXT("IA_ZG_Welder"), EInputActionValueType::Boolean);
	UInputAction* ToolSecondaryAction = MakeAction(TEXT("IA_ZG_ToolSecondary"), EInputActionValueType::Boolean);
	UInputAction* ReelAction = MakeAction(TEXT("IA_ZG_Reel"), EInputActionValueType::Axis1D);
	UInputAction* GunKeyAction = MakeAction(TEXT("IA_ZG_GunKey"), EInputActionValueType::Boolean);
	UInputAction* AimAction = MakeAction(TEXT("IA_ZG_Aim"), EInputActionValueType::Boolean);
	UInputAction* JumpAction = MakeAction(TEXT("IA_ZG_Jump"), EInputActionValueType::Boolean);

	Map(LookAction, EKeys::Mouse2D, false, false);
	// WASD onto one 2D axis: D is +X, W is +Y (swizzled), A and S negated.
	Map(ThrustAction, EKeys::W, true, false);
	Map(ThrustAction, EKeys::S, true, true);
	Map(ThrustAction, EKeys::D, false, false);
	Map(ThrustAction, EKeys::A, false, true);
	Map(LiftAction, EKeys::SpaceBar, false, false);
	Map(LiftAction, EKeys::LeftControl, false, true);
	Map(StabiliseAction, EKeys::X, false, false);
	// Owner 17.10/18.10: RMB grabs/lets go/holds on, F interacts (else pushes), Q/E roll, V bullet time, Alt the grapple.
	Map(UseAction, EKeys::RightMouseButton, false, false);
	Map(PrimaryAction, EKeys::LeftMouseButton, false, false);
	Map(PushAction, EKeys::F, false, false);
	Map(RollAction, EKeys::E, false, false);
	Map(RollAction, EKeys::Q, false, true);
	Map(GrappleAction, EKeys::LeftAlt, false, false);
	Map(BulletTimeAction, EKeys::V, false, false);
	Map(CrowbarAction, EKeys::G, false, false);
	// Hull repair (Docs/HullRepair.md): 4 the strap (it took over the T tether), 5 the welder;
	// R and the wheel go to the tool in hand, 1/2/3 put it away (the guns keep their own bindings).
	Map(StrapAction, EKeys::Four, false, false);
	Map(WelderAction, EKeys::Five, false, false);
	Map(ToolSecondaryAction, EKeys::R, false, false);
	Map(ReelAction, EKeys::MouseWheelAxis, false, false);
	Map(GunKeyAction, EKeys::One, false, false);
	Map(GunKeyAction, EKeys::Two, false, false);
	Map(GunKeyAction, EKeys::Three, false, false);
	Map(AimAction, EKeys::LeftShift, false, false);
	// Space is also Lift: thrust up in zero-G, a jump (press) in a gravity zone.
	Map(JumpAction, EKeys::SpaceBar, false, false);

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
		const FJamWeapon* Weapon = Guns->GetWeapon();
		if (!GrabbedBody.IsValid() && HullTools->GetTool() == EJamHullTool::None && Weapon && Weapon->bAutomatic)
		{
			Fire();
		}
	});
	Input->BindActionValueLambda(PrimaryAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { HullTools->PrimaryReleased(); });
	Input->BindActionValueLambda(StrapAction, ETriggerEvent::Started, [this](const FInputActionValue&) { HullTools->SelectTool(EJamHullTool::Strap); });
	Input->BindActionValueLambda(WelderAction, ETriggerEvent::Started, [this](const FInputActionValue&) { HullTools->SelectTool(EJamHullTool::Welder); });
	Input->BindActionValueLambda(ToolSecondaryAction, ETriggerEvent::Started, [this](const FInputActionValue&) { HullTools->SecondaryPressed(); });
	Input->BindActionValueLambda(ReelAction, ETriggerEvent::Started, [this](const FInputActionValue& Value) { HullTools->Reel(Value.Get<float>() > 0.0f ? 1 : -1); });
	Input->BindActionValueLambda(GunKeyAction, ETriggerEvent::Started, [this](const FInputActionValue&) { HullTools->SelectTool(EJamHullTool::None); });
	Input->BindAction(PushAction, ETriggerEvent::Started, this, &AJamZeroGPawn::PushPressed);
	// Left Shift held: the predicted paths are drawn.
	Input->BindActionValueLambda(AimAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bAiming = true; });
	Input->BindActionValueLambda(AimAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bAiming = false; });
	Input->BindAction(RollAction, ETriggerEvent::Triggered, this, &AJamZeroGPawn::Roll);
	Input->BindAction(GrappleAction, ETriggerEvent::Started, this, &AJamZeroGPawn::ToggleGrapple);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Started, this, &AJamZeroGPawn::BulletTimePressed);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Completed, this, &AJamZeroGPawn::BulletTimeReleased);
	Input->BindAction(CrowbarAction, ETriggerEvent::Started, this, &AJamZeroGPawn::ToggleCrowbar);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AJamZeroGPawn::JumpPressed);
	Guns->BindInput(*Input, *Mapping);
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
	if (IsClimbingLadder())
	{
		return;
	}
	// RMB is the hands only (owner 18.10; interaction is F): holding - let go;
	// a loose body in reach is grabbed; a fixed one (the hull) is held on to.
	if (GrabbedBody.IsValid())
	{
		Release();
		return;
	}
	if (ClingConstraint)
	{
		LetGoCling();
		return;
	}
	if (UPrimitiveComponent* Candidate = GrabCandidate.Get())
	{
		Grab(Candidate, GrabCandidatePoint);
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamClingLook), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + ViewDirection() * GrabReachCm, ECC_Visibility, Params))
	{
		Cling(Hit.GetComponent());
	}
}

bool AJamZeroGPawn::Cling(UPrimitiveComponent* Target)
{
	if (!Target || Target->IsSimulatingPhysics() || ClingConstraint)
	{
		return false;
	}
	// The body locked to the world where it is: no drift from recoil or the torch.
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	ClingConstraint = NewObject<UPhysicsConstraintComponent>(this);
	ClingConstraint->SetupAttachment(Body);
	ClingConstraint->RegisterComponent();
	ClingConstraint->SetWorldLocation(Body->GetComponentLocation());
	ClingConstraint->SetLinearXLimit(LCM_Locked, 0.0f);
	ClingConstraint->SetLinearYLimit(LCM_Locked, 0.0f);
	ClingConstraint->SetLinearZLimit(LCM_Locked, 0.0f);
	ClingConstraint->SetAngularSwing1Limit(ACM_Free, 0.0f);
	ClingConstraint->SetAngularSwing2Limit(ACM_Free, 0.0f);
	ClingConstraint->SetAngularTwistLimit(ACM_Free, 0.0f);
	ClingConstraint->SetConstrainedComponents(Body, NAME_None, nullptr, NAME_None);
	UE_LOG(LogMindKore, Log, TEXT("ZeroG cling: holding on to %s"), *GetNameSafe(Target->GetOwner()));
	return true;
}

void AJamZeroGPawn::LetGoCling()
{
	if (ClingConstraint)
	{
		ClingConstraint->BreakConstraint();
		ClingConstraint->DestroyComponent();
		ClingConstraint = nullptr;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG cling: let go"));
	}
}

void AJamZeroGPawn::PrimaryPressed()
{
	if (GrabbedBody.IsValid())
	{
		ThrowHeld();
		return;
	}
	if (HullTools->GetTool() != EJamHullTool::None)
	{
		HullTools->PrimaryPressed();
		return;
	}
	Fire();
}

void AJamZeroGPawn::PushPressed()
{
	// F is the interaction first (a pickup, a door, a canister: owner 18.10); with nothing offered it pushes.
	if (Interaction->CurrentOffer.IsOffered())
	{
		Interaction->Interact();
		return;
	}
	// Holding something, F pushes off whatever else is in reach and the held
	// body comes along (the grab couples the masses). With nothing else in
	// reach the push goes into the held body itself: it is let go, and it and
	// the pawn get equal and opposite impulses (Vision/CurrentState.md).
	if (Push())
	{
		return;
	}
	if (UPrimitiveComponent* Held = GrabbedBody.Get())
	{
		const FVector Direction = ViewDirection();
		Release();
		Held->AddImpulse(Direction * PushImpulse);
		AddBodyImpulse(-Direction * PushImpulse);
		UE_LOG(LogMindKore, Log, TEXT("ZeroG push: the held %s (%.0f kg) pushed away, impulse %.0f kg cm/s"),
			*GetNameSafe(Held->GetOwner()), Held->GetMass(), PushImpulse);
	}
}

void AJamZeroGPawn::Roll(const FInputActionValue& Value)
{
	// Only free rotation turns the view about its forward axis; in a gravity
	// zone the controller keeps the view level and drops roll input.
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->AddRollInput(Value.Get<float>() * RollRateDegPerSec * GetWorld()->DeltaRealTimeSeconds);
	}
}

bool AJamZeroGPawn::HasGrapple() const
{
	const AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>();
	return !Mode || Mode->Tools.Contains(FName(TEXT("Grapple")));
}

void AJamZeroGPawn::ToggleGrapple()
{
	if (GrappleTarget.IsValid())
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple: let go of %s"), *GetNameSafe(GrappleTarget->GetOwner()));
		GrappleTarget = nullptr;
		return;
	}
	if (!HasGrapple())
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple: not taken yet"));
		return;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamGrapple), /*bTraceComplex=*/false, this);
	if (!GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + ViewDirection() * GrappleRangeCm, ECC_Visibility, Params) || !Hit.GetComponent())
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple: nothing within %.0f cm"), GrappleRangeCm);
		return;
	}
	GrappleTarget = Hit.GetComponent();
	GrappleLocalPoint = Hit.GetComponent()->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
	UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple: hooked %s at %.0f cm (%s)"), *GetNameSafe(Hit.GetActor()), Hit.Distance,
		Hit.GetComponent()->IsSimulatingPhysics() ? TEXT("loose, pulled both ways") : TEXT("fixed"));
}

void AJamZeroGPawn::UpdateGrapple()
{
	UPrimitiveComponent* Target = GrappleTarget.Get();
	if (!Target)
	{
		GrappleTarget = nullptr;
		return;
	}
	const FVector Point = Target->GetComponentTransform().TransformPosition(GrappleLocalPoint);
	const FVector Delta = Point - Body->GetComponentLocation();
	const float Distance = Delta.Size();
	if (Distance <= GrappleArriveCm)
	{
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple: arrived at %s"), *GetNameSafe(Target->GetOwner()));
		GrappleTarget = nullptr;
		return;
	}
	// Equal and opposite: the line pulls both ends.
	const FVector Pull = Delta / Distance * GrapplePull;
	Body->AddForce(Pull);
	if (Target->IsSimulatingPhysics())
	{
		Target->AddForceAtLocation(-Pull, Point);
	}
	DrawDebugLine(GetWorld(), ViewLocation() - FVector(0.0f, 0.0f, 20.0f), Point, FColor(120, 230, 255), false, 0.0f, 0, 1.5f);
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
	const float MinSpeedCm = IsInGravity() ? FMath::Max(FeedbackMinSpeedCm, GroundFeedbackMinSpeedCm) : FeedbackMinSpeedCm;
	if (SpeedCm < MinSpeedCm || ShakeRemaining > ShakeSeconds * 0.5f)
	{
		return; // A touch, or the same crash still shaking.
	}
	const bool bFlesh = IsFlesh(Hit);
	if (USoundBase* Sound = bFlesh && FleshCrashSound ? FleshCrashSound.Get() : CrashSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Hit.ImpactPoint, FMath::Clamp(SpeedCm / 1000.0f, 0.2f, 1.0f),
			bFlesh ? FMath::FRandRange(FleshPitchRange.X, FleshPitchRange.Y) : 1.0f);
	}
	ShakeRemaining = ShakeSeconds;
	ShakeAmplitude = ShakeCmPer1000 * SpeedCm / 1000.0f;
	// Landing on walkable ground in a gravity zone thumps but never hurts: a fall is how you arrive on the planetoid.
	const bool bLanding = IsInGravity()
		&& FVector::DotProduct(Hit.ImpactNormal, -GravityDirection) >= FMath::Cos(FMath::DegreesToRadians(MaxGroundAngleDeg));
	if (SpeedCm >= CrashDamageMinSpeedCm && !bLanding)
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
	if (const FJamWeapon* Weapon = Guns->GetWeapon(); Weapon && Weapon->RecoilImpulse > 0.0f)
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

// --- walking ------------------------------------------------------------------------

void AJamZeroGPawn::SetGravity(const FVector& Direction, float StrengthCm, bool bRadial)
{
	const bool bWas = IsInGravity();
	GravityDirection = StrengthCm > 0.0f ? Direction.GetSafeNormal() : FVector::ZeroVector;
	GravityStrengthCm = GravityDirection.IsZero() ? 0.0f : StrengthCm;
	bRadialGravity = bRadial;
	if (bWas != IsInGravity())
	{
		bGrounded = false;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: %s at %s, %.0f cm/s"), *GetName(), IsInGravity() ? TEXT("gravity on (walking)") : TEXT("gravity off (zero-G)"),
			*GetActorLocation().ToCompactString(), GetBodyVelocity().Size());
	}
}

bool AJamZeroGPawn::ProbeGround(FHitResult& OutHit) const
{
	// The body's sphere, a little thinner, swept a short way along gravity: a
	// blocking hit there that faces up enough is ground.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamGround), /*bTraceComplex=*/false, this);
	Params.bReturnPhysicalMaterial = true; // flesh underfoot squelches
	FCollisionResponseParams Response;
	Body->InitSweepCollisionParams(Params, Response);
	if (const UPrimitiveComponent* Held = GrabbedBody.Get())
	{
		Params.AddIgnoredActor(Held->GetOwner());
	}
	const FVector Start = Body->GetComponentLocation();
	const bool bHit = GetWorld()->SweepSingleByChannel(OutHit, Start, Start + GravityDirection * GroundProbeCm, FQuat::Identity,
		Body->GetCollisionObjectType(), FCollisionShape::MakeSphere(Body->GetScaledSphereRadius() - 2.0f), Params, Response);
	if (!bHit)
	{
		OutHit.ImpactNormal = -GravityDirection;
	}
	return bHit && FVector::DotProduct(OutHit.ImpactNormal, -GravityDirection) >= FMath::Cos(FMath::DegreesToRadians(MaxGroundAngleDeg));
}

void AJamZeroGPawn::PlayFleshStep(const FHitResult& Ground, float Volume)
{
	const bool bFlesh = IsFlesh(Ground);
	UE_LOG(LogMindKore, Verbose, TEXT("ZeroG pawn %s: step on %s (%s), volume %.2f"), *GetName(), *GetNameSafe(Ground.GetComponent()),
		bFlesh ? TEXT("flesh") : TEXT("not flesh"), Volume);
	if (FleshStepSound && bFlesh)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FleshStepSound, Ground.ImpactPoint, Volume, FMath::FRandRange(FleshPitchRange.X, FleshPitchRange.Y));
	}
}

void AJamZeroGPawn::Walk(float DeltaSeconds)
{
	const FVector Input = FVector(ThrustInput.X, ThrustInput.Y, 0.0f).GetClampedToMaxSize(1.0f); // Space and Ctrl (Z) do not walk
	ThrustInput = FVector::ZeroVector;
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}
	// Gravity as an acceleration on this body only; the world's stays 0.
	Body->AddForce(GravityDirection * GravityStrengthCm, NAME_None, /*bAccelChange=*/true);
	const FVector Up = -GravityDirection;
	const bool bJustJumped = GetWorld()->GetTimeSeconds() - LastJumpSeconds < 0.25;
	FHitResult GroundHit;
	// A touchdown is ground after a real flight, not a bump of the relief losing the probe for a frame.
	const bool bTouchdown = GetWorld()->GetTimeSeconds() - LastGroundedSeconds > 0.2;
	bGrounded = !bJustJumped && ProbeGround(GroundHit);
	const FVector Ground = bGrounded ? FVector(GroundHit.ImpactNormal) : Up;

	// The view's right lies in the ground plane (the view frame never rolls);
	// forward is right x up, so looking straight down still walks forward.
	const FVector Right = FVector::VectorPlaneProject(FRotationMatrix(GetControlRotation()).GetUnitAxis(EAxis::Y), Up).GetSafeNormal();
	const FVector Forward = FVector::CrossProduct(Right, Up);
	const FVector Wish = (Forward * Input.X + Right * Input.Y).GetClampedToMaxSize(1.0f) * WalkSpeedCm;

	const FVector Velocity = GetBodyVelocity();
	float Normal = FVector::DotProduct(Velocity, Up);
	FVector Tangent = Velocity - Up * Normal;
	if (bGrounded)
	{
		// On the ground the walk runs in the ground's plane: up a slope the wish
		// climbs along it (in the gravity plane the climb was cancelled every
		// frame and a 25-degree slope took ~25 cm/s, jam.Ship.Caves); on level
		// ground this is the gravity plane.
		const FVector Slope = FVector::VectorPlaneProject(Wish, Ground).GetSafeNormal() * Wish.Size();
		Normal = FVector::DotProduct(Velocity, Ground);
		Tangent = Velocity - Ground * Normal;
		const float Accel = Wish.IsNearlyZero() ? WalkBrakingCm : WalkAccelCm;
		Tangent += (Slope - Tangent).GetClampedToMaxSize(Accel * DeltaSeconds);
		// Stuck to the ground: no bounce off the floor or off the curve of the planetoid.
		Body->SetPhysicsLinearVelocity(Tangent + Ground * FMath::Min(Normal, 0.0f));
		// Steps: one on touchdown (louder), then one every StepCm walked.
		StepTravelCm += float(Tangent.Size()) * DeltaSeconds;
		if (bTouchdown || StepTravelCm >= StepCm)
		{
			PlayFleshStep(GroundHit, bTouchdown ? StepVolume * 2.0f : StepVolume);
			StepTravelCm = 0.0f;
		}
		LastGroundedSeconds = GetWorld()->GetTimeSeconds();
		return;
	}
	if (!Wish.IsNearlyZero())
	{
		// Air control: adds toward the input up to the walking speed, never brakes.
		const FVector Direction = Wish.GetSafeNormal();
		const float Add = FMath::Min(AirAccelCm * DeltaSeconds, WalkSpeedCm - FVector::DotProduct(Tangent, Direction));
		Tangent += Direction * FMath::Max(Add, 0.0f);
	}
	Body->SetPhysicsLinearVelocity(Tangent + Up * Normal);
}

void AJamZeroGPawn::BeginLadderClimb(AJamLadder* Ladder)
{
	ActiveLadder = Ladder;
	if (!bRestoreLadderRadius)
	{
		LadderOriginalRadiusCm = Body->GetUnscaledSphereRadius();
	}
	bRestoreLadderRadius = true;
	Body->SetSphereRadius(FMath::Min(LadderOriginalRadiusCm, 34.0f));
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	bGrounded = false;
}

void AJamZeroGPawn::EndLadderClimb()
{
	ActiveLadder = nullptr;
	LadderCooldownUntil = GetWorld()->GetTimeSeconds() + 0.3;
	RestoreLadderBodyRadius();
}

void AJamZeroGPawn::RestoreLadderBodyRadius()
{
	if (!bRestoreLadderRadius)
	{
		return;
	}
	const FCollisionShape Shape = FCollisionShape::MakeSphere(LadderOriginalRadiusCm * Body->GetComponentScale().GetAbsMax());
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderStandUp), false, this);
	const auto Blocked = [&](const FVector& Position)
	{
		return GetWorld()->OverlapBlockingTestByChannel(Position, GetActorQuat(), Body->GetCollisionObjectType(), Shape, Params);
	};
	if (Blocked(GetActorLocation()))
	{
		// Stand up from the landing without expanding into the floor or hatch edge.
		const FVector Up = IsInGravity() ? -GravityDirection : FVector::UpVector;
		const FVector Raised = GetActorLocation() + Up * (LadderOriginalRadiusCm - Body->GetUnscaledSphereRadius() + 0.5f);
		if (Blocked(Raised))
		{
			return;
		}
		FHitResult Hit;
		SetActorLocation(Raised, true, &Hit, ETeleportType::TeleportPhysics);
		if (Hit.bBlockingHit)
		{
			return;
		}
	}
	Body->SetSphereRadius(LadderOriginalRadiusCm);
	bRestoreLadderRadius = false;
}

void AJamZeroGPawn::ClimbLadder(float DeltaSeconds)
{
	const FVector Input = ThrustInput;
	ThrustInput = FVector::ZeroVector;
	if (!IsValid(ActiveLadder))
	{
		EndLadderClimb();
		return;
	}
	const FVector Wish = (ViewDirection() * Input.X + Camera->GetRightVector() * Input.Y).GetClampedToMaxSize(1.0f);
	const FVector Velocity = Wish * ActiveLadder->ClimbSpeedCm;
	const FVector Delta = Velocity * DeltaSeconds;
	FHitResult Hit;
	SetActorLocation(GetActorLocation() + Delta, true, &Hit, ETeleportType::TeleportPhysics);
	if (Hit.bBlockingHit && !Hit.bStartPenetrating)
	{
		// Slide along rungs while preserving the vertical part of the input.
		const FVector Slide = FVector::VectorPlaneProject(Delta * (1.0f - Hit.Time), Hit.Normal);
		SetActorLocation(GetActorLocation() + Slide, true, nullptr, ETeleportType::TeleportPhysics);
	}
	const FVector LocalWish = ActiveLadder->GetActorTransform().InverseTransformVectorNoScale(Wish);
	if (!ActiveLadder->IsInReach(GetActorLocation()) || LocalWish.Y < -0.1f)
	{
		EndLadderClimb();
		Body->SetPhysicsLinearVelocity(Velocity);
	}
	else
	{
		Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	}
}

void AJamZeroGPawn::JumpPressed()
{
	if (IsClimbingLadder())
	{
		const FVector Away = ActiveLadder->GetActorTransform().TransformVectorNoScale(FVector(0, -1, 0));
		const FVector Up = IsInGravity() ? -GravityDirection : FVector::UpVector;
		JumpedFromLadder = ActiveLadder;
		EndLadderClimb();
		Body->SetPhysicsLinearVelocity(Away * JumpSpeedCm * 0.65f + ViewDirection() * JumpSpeedCm * 0.45f + Up * JumpSpeedCm * 0.55f);
		LastJumpSeconds = GetWorld()->GetTimeSeconds();
		return;
	}
	if (!IsInGravity())
	{
		return; // Zero-G: Space is the up thruster (Lift).
	}
	const FVector Up = -GravityDirection;
	const FVector Velocity = GetBodyVelocity();
	if (bGrounded)
	{
		Body->SetPhysicsLinearVelocity(Velocity - Up * FVector::DotProduct(Velocity, Up) + Up * JumpSpeedCm);
		LastJumpSeconds = GetWorld()->GetTimeSeconds();
		bGrounded = false;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: jump at %.0f cm/s"), *GetName(), JumpSpeedCm);
	}
	else if (bRadialGravity)
	{
		// In the air over a planetoid: the jet leap that leaves its gravity.
		Body->SetPhysicsLinearVelocity(Velocity + Up * JetExitSpeedCm);
		LastJumpSeconds = GetWorld()->GetTimeSeconds();
		UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: jet exit at %.0f cm/s"), *GetName(), JetExitSpeedCm);
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
	UpdateGrapple();
	DrawPrediction();
	// The camera: at eye height over the body in gravity, at its centre in zero-G
	// (eased both ways), plus the shake - a fading random world-space offset.
	EyeOffset = FMath::VInterpTo(EyeOffset, -GravityDirection * EyeHeightCm, DeltaSeconds, EyeRiseRate);
	ShakeRemaining = FMath::Max(0.0f, ShakeRemaining - DeltaSeconds);
	const FVector Shake = ShakeRemaining > 0.0f
		? FMath::VRand() * ShakeAmplitude * (ShakeRemaining / FMath::Max(ShakeSeconds, 0.01f))
		: FVector::ZeroVector;
	Camera->SetRelativeLocation(Body->GetComponentTransform().InverseTransformVectorNoScale(EyeOffset + Shake));
	RefreshGrabCandidate();
	UpdateHoldTarget();
	UpdateDust();
	if (!IsClimbingLadder() && !ClingConstraint && GetWorld()->GetTimeSeconds() >= LadderCooldownUntil
		&& !GrabbedBody.IsValid() && !TetheredBody.IsValid() && !GrappleTarget.IsValid())
	{
		FVector Move = ViewDirection() * ThrustInput.X + Camera->GetRightVector() * ThrustInput.Y;
		if (Move.IsNearlyZero() && !bGrounded)
		{
			Move = GetBodyVelocity().GetSafeNormal();
		}
		for (TActorIterator<AJamLadder> It(GetWorld()); It; ++It)
		{
			if (JumpedFromLadder.Get() == *It)
			{
				if (It->IsInReach(GetActorLocation()))
				{
					continue;
				}
				JumpedFromLadder.Reset();
			}
			if (It->CanEnter(GetActorLocation(), Move))
			{
				BeginLadderClimb(*It);
				break;
			}
		}
	}
	if (IsClimbingLadder())
	{
		ClimbLadder(DeltaSeconds);
	}
	else if (IsInGravity())
	{
		Walk(DeltaSeconds);
	}
	else
	{
		ApplyThrusters(DeltaSeconds);
	}
	if (!IsClimbingLadder())
	{
		RestoreLadderBodyRadius();
	}
	// The cap is for zero-G: in a zone gravity bounds the speed, and the jet exit must beat it.
	if (MaxSpeedCm > 0.0f && !IsInGravity())
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

bool AJamZeroGPawn::Fire()
{
	FVector Recoil;
	if (!Guns->Fire(ViewLocation(), ViewDirection(), Recoil))
	{
		return false;
	}
	AddBodyImpulse(Recoil);
	// The kick is felt in the camera too, not only in the speed readout.
	const float Kick = RecoilShakeCmPer10000 * Recoil.Size() / 10000.0f;
	ShakeAmplitude = ShakeRemaining > 0.0f ? FMath::Max(ShakeAmplitude, Kick) : Kick;
	ShakeRemaining = ShakeSeconds;
	return true;
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
				Pawn->Guns->SelectWeapon(2);
				Pawn->Fire();
				FTimerHandle AfterSniper;
				Pawn->GetWorld()->GetTimerManager().SetTimer(AfterSniper, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
				{
					LogProbe(TEXT("after sniper"), Pawn);
					Pawn->Guns->SelectWeapon(0);
					ProbeCoupledPush(Pawn);
				}), 0.25f, false);
			}), 0.25f, false);
		}), 0.25f, false);
	}

	/**
	 * jam.ZeroG.Grapple: hook the nearest fixed block, then a light loose prop,
	 * logging both ends' velocities after a second each; then roll the view
	 * 45 degrees through the controller's roll input (free rotation keeps it).
	 */
	void RunZeroGGrapple(UWorld* World)
	{
		AJamZeroGPawn* Pawn = nullptr;
		for (TActorIterator<AJamZeroGPawn> It(World); It; ++It)
		{
			Pawn = *It;
			break;
		}
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Pawn || !PlayerController)
		{
			return;
		}
		const FVector From = Pawn->GetActorLocation();
		UPrimitiveComponent* Fixed = nullptr;
		UPrimitiveComponent* Loose = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (!Root || *It == Pawn || Root->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				continue;
			}
			const float Distance = FVector::Dist(From, Root->GetComponentLocation());
			if (Distance < 500.0f || Distance > 2800.0f)
			{
				continue;
			}
			if (!Root->IsSimulatingPhysics() && Root->Bounds.SphereRadius > 150.0f && Root->Bounds.SphereRadius < 600.0f
				&& (!Fixed || Distance < FVector::Dist(From, Fixed->GetComponentLocation())))
			{
				Fixed = Root;
			}
			if (Root->IsSimulatingPhysics() && Root->GetMass() < 30.0f
				&& (!Loose || Distance < FVector::Dist(From, Loose->GetComponentLocation())))
			{
				Loose = Root;
			}
		}
		const auto HookAt = [Pawn, PlayerController](UPrimitiveComponent* Target, const TCHAR* Label)
		{
			if (!Target)
			{
				UE_LOG(LogMindKore, Warning, TEXT("ZeroG grapple probe [%s]: no target in range"), Label);
				return;
			}
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Target->SetPhysicsLinearVelocity(FVector::ZeroVector);
			PlayerController->SetControlRotation((Target->GetComponentLocation() - Pawn->Camera->GetComponentLocation()).Rotation());
			Pawn->ToggleGrapple();
			const FString Name = Label;
			FTimerHandle Later;
			Pawn->GetWorld()->GetTimerManager().SetTimer(Later, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Target, Name]()
			{
				UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple probe [%s +1 s]: hooked %s, pawn %.0f cm/s toward it %.0f, target %s %.1f kg %.0f cm/s, distance %.0f cm"),
					*Name, Pawn->GetGrappled() ? TEXT("yes") : TEXT("no"), Pawn->GetBodyVelocity().Size(),
					FVector::DotProduct(Pawn->GetBodyVelocity(), (Target->GetComponentLocation() - Pawn->GetActorLocation()).GetSafeNormal()),
					*GetNameSafe(Target->GetOwner()), Target->IsSimulatingPhysics() ? Target->GetMass() : 0.0f, Target->GetPhysicsLinearVelocity().Size(),
					FVector::Dist(Pawn->GetActorLocation(), Target->GetComponentLocation()));
				if (Pawn->GetGrappled())
				{
					Pawn->ToggleGrapple();
				}
			}), 1.0f, false);
		};
		HookAt(Fixed, TEXT("fixed block"));
		FTimerHandle Second;
		World->GetTimerManager().SetTimer(Second, FTimerDelegate::CreateWeakLambda(Pawn, [HookAt, Loose]() { HookAt(Loose, TEXT("light loose prop")); }), 1.5f, false);
		FTimerHandle Third;
		World->GetTimerManager().SetTimer(Third, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, PlayerController]()
		{
			const float Before = PlayerController->GetControlRotation().Roll;
			PlayerController->AddRollInput(45.0f);
			FTimerHandle Next;
			Pawn->GetWorld()->GetTimerManager().SetTimer(Next, FTimerDelegate::CreateWeakLambda(Pawn, [PlayerController, Before]()
			{
				UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple probe [roll]: control roll %.1f -> %.1f after a 45 deg roll input"), Before, PlayerController->GetControlRotation().Roll);
			}), 0.1f, false);
		}), 3.2f, false);
	}

	FAutoConsoleCommandWithWorld GZeroGGrapple(
		TEXT("jam.ZeroG.Grapple"),
		TEXT("Zero-G pawn: hook a fixed block, then a light loose prop (logging both ends after 1 s), then a 45 deg roll input."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunZeroGGrapple));

	FAutoConsoleCommandWithWorld GZeroGProbe(
		TEXT("jam.ZeroG.Probe"),
		TEXT("Zero-G pawn: push off what is in view, then shoot; logs the pawn velocity around each."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunZeroGProbe));
}
