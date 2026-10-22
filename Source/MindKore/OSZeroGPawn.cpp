#include "OSZeroGPawn.h"

#include "OSLens.h"

#include "Camera/CameraComponent.h"
#include "Components/BoxComponent.h"
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
#include "OSDockZone.h"
#include "OSHullTools.h"
#include "OSOxygen.h"
#include "OSPickups.h"
#include "OSShip.h"
#include "OSScope.h"
#include "OSLadder.h"
#include "OSWeapons.h"
#include "OSWeldProjector.h"
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

AOSZeroGPawn::AOSZeroGPawn()
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
	Oxygen = CreateDefaultSubobject<UOSOxygenComponent>(TEXT("Oxygen"));
	Guns = CreateDefaultSubobject<UOSWeaponsComponent>(TEXT("Guns"));
	// Every map runs the ship's mode: the guns come from their pickups.
	Guns->bStartWithAllWeapons = false;
	HullTools = CreateDefaultSubobject<UOSHullToolsComponent>(TEXT("HullTools"));
	WeldProjector = CreateDefaultSubobject<UOSWeldProjectorComponent>(TEXT("WeldProjector"));
	WeldProjector->SetupAttachment(Camera);
	// Under the lens, as MindKore's projector sits under the bodycam's.
	WeldProjector->SetRelativeLocation(FVector(0.0f, 0.0f, -6.0f));

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
	TAutoConsoleVariable<int32> CVarZeroGTrace(TEXT("os.ZeroG.Trace"), 0,
		TEXT("1: the player's pawn logs a line every tick (mode, body, velocity, eye, arm, contacts and shakes so far) and every body hit."));

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

void AOSZeroGPawn::BeginPlay()
{
	Super::BeginPlay();
	Body->SetMassOverrideInKg(NAME_None, BodyMassKg, true);
	OSApplyBodycamLens(*Camera, CameraFOV, bLensDistortion);
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
	Body->OnComponentHit.AddDynamic(this, &AOSZeroGPawn::HandleBodyHit);
	if (BulletTimeMaterial)
	{
		BulletTimeMID = UMaterialInstanceDynamic::Create(BulletTimeMaterial, this);
		Camera->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0.0f, BulletTimeMID));
	}
	UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: %.0f kg, world gravity Z %.1f, FOV %.0f, %d dust specks"), *GetName(),
		Body->GetMass(), GetWorld()->GetGravityZ(), Camera->FieldOfView, Dust->GetInstanceCount());
}

// --- input -------------------------------------------------------------------

void AOSZeroGPawn::BuildInput()
{
	Mapping = NewObject<UInputMappingContext>(this, TEXT("OSZeroGMapping"));
}

void AOSZeroGPawn::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
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
	UInputAction* PrimaryAction = MakeAction(TEXT("IA_ZG_Primary"), EInputActionValueType::Boolean);
	UInputAction* SecondaryAction = MakeAction(TEXT("IA_ZG_Secondary"), EInputActionValueType::Boolean);
	UInputAction* MiddleAction = MakeAction(TEXT("IA_ZG_Middle"), EInputActionValueType::Boolean);
	UInputAction* WheelAction = MakeAction(TEXT("IA_ZG_Wheel"), EInputActionValueType::Axis1D);
	UInputAction* ReloadAction = MakeAction(TEXT("IA_ZG_Reload"), EInputActionValueType::Boolean);
	UInputAction* UseAction = MakeAction(TEXT("IA_ZG_Use"), EInputActionValueType::Boolean);
	UInputAction* RollAction = MakeAction(TEXT("IA_ZG_Roll"), EInputActionValueType::Axis1D);
	UInputAction* GrappleAction = MakeAction(TEXT("IA_ZG_Grapple"), EInputActionValueType::Boolean);
	UInputAction* BulletTimeAction = MakeAction(TEXT("IA_ZG_BulletTime"), EInputActionValueType::Boolean);
	UInputAction* CrowbarAction = MakeAction(TEXT("IA_ZG_Crowbar"), EInputActionValueType::Boolean);
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
	// The hands' keys, each bound once, here: what they do is Route's table (owner 17-19.10). F interacts
	// (else pushes), Q/E roll, V bullet time, Alt the grapple.
	Map(PrimaryAction, EKeys::LeftMouseButton, false, false);
	Map(SecondaryAction, EKeys::RightMouseButton, false, false);
	Map(MiddleAction, EKeys::MiddleMouseButton, false, false);
	Map(WheelAction, EKeys::MouseWheelAxis, false, false);
	Map(ReloadAction, EKeys::R, false, false);
	Map(UseAction, EKeys::F, false, false);
	Map(RollAction, EKeys::E, false, false);
	Map(RollAction, EKeys::Q, false, true);
	Map(GrappleAction, EKeys::LeftAlt, false, false);
	Map(BulletTimeAction, EKeys::V, false, false);
	Map(CrowbarAction, EKeys::G, false, false);
	Map(AimAction, EKeys::LeftShift, false, false);
	// Space is also Lift: thrust up in zero-G, a jump (press) in a gravity zone.
	Map(JumpAction, EKeys::SpaceBar, false, false);

	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AOSZeroGPawn::Look);
	Input->BindAction(ThrustAction, ETriggerEvent::Triggered, this, &AOSZeroGPawn::Thrust);
	Input->BindAction(LiftAction, ETriggerEvent::Triggered, this, &AOSZeroGPawn::Lift);
	Input->BindActionValueLambda(StabiliseAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bStabilising = true; });
	Input->BindActionValueLambda(StabiliseAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bStabilising = false; });
	// Act on press. An automatic gun keeps firing while LMB is held; Fire's
	// interval gate makes the press frame (Started and Triggered) one shot.
	Input->BindActionValueLambda(PrimaryAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Press(EOSHandKey::Primary); });
	Input->BindActionValueLambda(PrimaryAction, ETriggerEvent::Triggered, [this](const FInputActionValue&) { Press(EOSHandKey::PrimaryHeld); });
	Input->BindActionValueLambda(SecondaryAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Press(EOSHandKey::Secondary); });
	Input->BindActionValueLambda(WheelAction, ETriggerEvent::Started, [this](const FInputActionValue& Value) { Press(EOSHandKey::Wheel, Value.Get<float>()); });
	Input->BindActionValueLambda(ReloadAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Press(EOSHandKey::Reload); });
	// Releases end a hold whatever is in hand: the torch, the RMB aim. MMB holds the aim the same way;
	// the gun rises to the eye only while one is drawn (UOSScopeComponent::TickComponent).
	Input->BindActionValueLambda(PrimaryAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { HullTools->PrimaryReleased(); });
	Input->BindActionValueLambda(SecondaryAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { Guns->Scope->SetAimHeld(false); });
	Input->BindActionValueLambda(MiddleAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Guns->Scope->SetHeld(true); });
	Input->BindActionValueLambda(MiddleAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { Guns->Scope->SetHeld(false); });
	// The digits from the lists: the guns 1..N in Guns->Weapons order, then the strap and the welder (GetToolSlot).
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine, EKeys::Zero };
	const int32 Slots = GetToolSlot(EOSHullTool::Welder) + 1;
	if (Slots > int32(UE_ARRAY_COUNT(Digits)))
	{
		UE_LOG(LogMindKore, Error, TEXT("%s: %d guns and 2 tools need %d digit keys, there are %d"), *GetName(), Guns->Weapons.Num(), Slots, int32(UE_ARRAY_COUNT(Digits)));
	}
	for (int32 Slot = 0; Slot < FMath::Min(Slots, int32(UE_ARRAY_COUNT(Digits))); ++Slot)
	{
		UInputAction* SlotAction = MakeAction(*FString::Printf(TEXT("IA_ZG_Slot%d"), Slot + 1), EInputActionValueType::Boolean);
		Map(SlotAction, Digits[Slot], false, false);
		Input->BindActionValueLambda(SlotAction, ETriggerEvent::Started, [this, Slot](const FInputActionValue&) { PressSlot(Slot); });
	}
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AOSZeroGPawn::PushPressed);
	// Left Shift held: the predicted paths are drawn.
	Input->BindActionValueLambda(AimAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bAiming = true; });
	Input->BindActionValueLambda(AimAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bAiming = false; });
	Input->BindAction(RollAction, ETriggerEvent::Triggered, this, &AOSZeroGPawn::Roll);
	Input->BindAction(GrappleAction, ETriggerEvent::Started, this, &AOSZeroGPawn::ToggleGrapple);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Started, this, &AOSZeroGPawn::BulletTimePressed);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Completed, this, &AOSZeroGPawn::BulletTimeReleased);
	Input->BindAction(CrowbarAction, ETriggerEvent::Started, this, &AOSZeroGPawn::ToggleCrowbar);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AOSZeroGPawn::JumpPressed);
}

void AOSZeroGPawn::PawnClientRestart()
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

void AOSZeroGPawn::Look(const FInputActionValue& Value)
{
	// Slower through the scope, as much as it magnifies (UOSScopeComponent::GetLookScale).
	const FVector2D Axis = Value.Get<FVector2D>() * LookSensitivity * (Guns->Scope ? Guns->Scope->GetLookScale() : 1.0f);
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void AOSZeroGPawn::Thrust(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	ThrustInput.X += Axis.Y;
	ThrustInput.Y += Axis.X;
}

void AOSZeroGPawn::Lift(const FInputActionValue& Value)
{
	ThrustInput.Z += Value.Get<float>();
}

// --- movement mode -------------------------------------------------------------

const FOSMoveRules& AOSZeroGPawn::Allows() const
{
	// One row per EOSMoveMode:               fire   use    grapple jump   thrusters
	static const FOSMoveRules Rules[] = {
		/* Float: Space is the up thruster */  { true, true,  true,  false, true },
		/* Walk */                              { true, true,  true,  true,  false },
		/* Ladder: Space pushes off it */       { true, false, false, true,  false },
		/* Hang: the keys are the arm */        { true, true,  true,  false, false },
	};
	return Rules[uint8(MoveMode)];
}

void AOSZeroGPawn::SetMode(EOSMoveMode NewMode)
{
	if (NewMode == MoveMode)
	{
		return;
	}
	const EOSMoveMode OldMode = MoveMode;
	switch (OldMode)
	{
	case EOSMoveMode::Walk:
		bGrounded = false;
		break;
	case EOSMoveMode::Ladder:
		UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: off %s"), *GetName(), *GetNameSafe(ActiveLadder));
		ActiveLadder = nullptr;
		bLadderMounting = false;
		LadderCooldownUntil = GetWorld()->GetTimeSeconds() + 0.3;
		RestoreLadderBodyRadius();
		break;
	case EOSMoveMode::Hang:
		HandOverArmCarry();
		ClingTarget = nullptr;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG cling: let go at %.0f cm/s"), GetBodyVelocity().Size());
		break;
	default:
		break;
	}
	MoveMode = NewMode;
	UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: %s -> %s"), *GetName(), *UEnum::GetDisplayValueAsText(OldMode).ToString(),
		*UEnum::GetDisplayValueAsText(NewMode).ToString());
	if (NewMode == EOSMoveMode::Walk)
	{
		bGrounded = false;
	}
}

// --- hands -----------------------------------------------------------------------

EOSHands AOSZeroGPawn::GetHands() const
{
	return Hands == EOSHands::Gun && !Guns->IsWeaponUnlocked(Guns->CurrentWeapon) ? EOSHands::Empty : Hands;
}

void AOSZeroGPawn::SetHands(EOSHands NewHands, EOSHullTool NewTool)
{
	if (NewHands == EOSHands::Empty)
	{
		NewHands = EOSHands::Gun; // free hands want the gun; with none picked up GetHands reads Empty
	}
	if (NewHands == Hands && (NewHands != EOSHands::Tool || NewTool == EOSHullTool::None || NewTool == HullTools->GetLastTool()))
	{
		return;
	}
	const EOSHands OldHands = Hands;
	switch (OldHands)
	{
	case EOSHands::Gun:
		// The reload is abandoned either way; a grab takes the gun out of view this frame, a tool lets it go away on its path.
		Guns->Holster(/*bAtOnce=*/NewHands == EOSHands::Held);
		break;
	case EOSHands::Tool:
		HullTools->PutAway();
		break;
	case EOSHands::Held:
		if (GrabbedBody.IsValid())
		{
			UE_LOG(LogMindKore, Log, TEXT("ZeroG grab: let go of %s"), *GetNameSafe(GrabbedBody->GetOwner()));
		}
		if (!ClingTarget.IsValid()) // holding on, the arm is on the hold, not on the body
		{
			// Dropped, not handed over: the steps leave out the held body's own gravity (and the floor under the
			// pawn), so here it is the frame's sag, not a velocity - handed over, a throw left its preview path
			// (os.Gravity.Probe, cabin / planetoid: 3.75 / 8.16 cm off, 1.36 / 4.93 before the steps).
			ArmCarry = FVector::ZeroVector;
		}
		GrabbedBody = nullptr;
		break;
	default:
		break;
	}
	if (NewHands == EOSHands::Held)
	{
		HandsBeforeHeld = OldHands;
	}
	Hands = NewHands;
	UE_LOG(LogMindKore, Log, TEXT("ZeroG hands: %s -> %s"), *UEnum::GetDisplayValueAsText(OldHands).ToString(), *UEnum::GetDisplayValueAsText(NewHands).ToString());
	switch (NewHands)
	{
	case EOSHands::Gun:
		if (OldHands == EOSHands::Held)
		{
			Guns->Rearm(); // back from unarmed: the last gun, drawn (owner 19.10); from a tool its put-away just runs back
		}
		break;
	case EOSHands::Tool:
		HullTools->TakeOut(NewTool);
		break;
	default:
		break;
	}
}

void AOSZeroGPawn::TakeTool(EOSHullTool Tool)
{
	SetHands(HullTools->GetTool() == Tool ? EOSHands::Gun : EOSHands::Tool, Tool);
}

EOSHandAction AOSZeroGPawn::Route(EOSHandKey Key) const
{
	using A = EOSHandAction;
	// One row per EOSHands, one column per EOSHandKey (Docs/ZeroG.md, «Руки»):
	//                     LMB              LMB held     RMB        wheel      R
	static const A Table[4][5] = {
		/* Gun */          { A::Fire,        A::AutoFire, A::Aim,    A::NextGun, A::Reload },
		/* Tool */         { A::ToolPrimary, A::None,     A::Hands,  A::Reel,    A::ToolSecondary },
		/* Held */         { A::Throw,       A::None,     A::Hands,  A::NextGun, A::None },
		/* Empty */        { A::None,        A::None,     A::Hands,  A::None,    A::None },
	};
	const A Action = Table[uint8(GetHands())][uint8(Key)];
	// The move mode's say (Allows): a ladder takes RMB (the hands are on the rungs).
	if ((Action == A::Fire || Action == A::AutoFire) && !Allows().bFire)
	{
		return A::None;
	}
	if ((Action == A::Aim || Action == A::Hands) && !Allows().bUse)
	{
		return A::None;
	}
	return Action;
}

void AOSZeroGPawn::Press(EOSHandKey Key, float Value)
{
	const int32 Step = Value > 0.0f ? 1 : -1;
	switch (Route(Key))
	{
	case EOSHandAction::Fire:
		Fire();
		break;
	case EOSHandAction::AutoFire:
		if (const FOSWeapon* Weapon = Guns->GetWeapon(); Weapon && Weapon->bAutomatic)
		{
			Fire();
		}
		break;
	case EOSHandAction::Reload:
		Guns->Reload();
		break;
	case EOSHandAction::NextGun:
		Guns->SelectStep(Step);
		if (Guns->IsWeaponUnlocked(Guns->CurrentWeapon))
		{
			SetHands(EOSHands::Gun); // holding a body: it is let go, the gun drawn
		}
		break;
	case EOSHandAction::Aim:
	case EOSHandAction::Hands:
		Use();
		break;
	case EOSHandAction::Throw:
		ThrowHeld();
		break;
	case EOSHandAction::ToolPrimary:
		HullTools->PrimaryPressed();
		break;
	case EOSHandAction::ToolSecondary:
		HullTools->SecondaryPressed();
		break;
	case EOSHandAction::Reel:
		HullTools->Reel(Step);
		break;
	default:
		break;
	}
}

void AOSZeroGPawn::PressSlot(int32 Slot)
{
	if (Slot >= Guns->Weapons.Num())
	{
		TakeTool(Slot == GetToolSlot(EOSHullTool::Strap) ? EOSHullTool::Strap : EOSHullTool::Welder);
		return;
	}
	// Any gun's key puts a hull tool away; a gun picked up is then the one in hand, from whatever was there.
	if (GetHands() == EOSHands::Tool)
	{
		SetHands(EOSHands::Gun);
	}
	if (Guns->IsWeaponUnlocked(Slot))
	{
		Guns->SelectWeapon(Slot);
		SetHands(EOSHands::Gun);
	}
}

void AOSZeroGPawn::Use()
{
	if (!Allows().bUse)
	{
		return;
	}
	// Owner 18.10: «прицеливание ставим на пкм тоже, хватать объекты можно но в таком
	// случае игрок переходит в Unarmed». A gun in hand: RMB grabs a loose body in reach
	// (Grab puts the gun away), otherwise it aims while held. Unarmed or a hull tool in
	// hand: RMB is the hands - holding, let go; a loose body in reach is grabbed; a fixed
	// one (the hull) is held on to. Interaction stays on F.
	if (GetRmbAction() == EOSRmbAction::Aim)
	{
		Guns->Scope->SetAimHeld(true);
		UE_LOG(LogMindKore, Log, TEXT("ZeroG RMB: aim"));
		return;
	}
	if (GrabbedBody.IsValid())
	{
		Release();
		return;
	}
	if (IsClinging())
	{
		LetGoCling();
		return;
	}
	if (UPrimitiveComponent* Candidate = GrabCandidate.Get())
	{
		Grab(Candidate, GrabCandidatePoint);
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSClingLook), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + ViewDirection() * GrabReachCm, ECC_Visibility, Params))
	{
		Cling(Hit.GetComponent(), Hit.ImpactPoint);
	}
}

bool AOSZeroGPawn::Cling(UPrimitiveComponent* Target, const FVector& Point)
{
	if (!Target || Target->IsSimulatingPhysics() || IsClinging())
	{
		return false;
	}
	// The hand takes the point; the velocity is kept - the arm catches it (UpdateArm).
	ArmCarry = FVector::ZeroVector; // a held body's arm ends here (the arm goes to the hold); dropped as on a release
	SetMode(EOSMoveMode::Hang);
	ClingTarget = Target;
	HandLocal = Target->GetComponentTransform().InverseTransformPosition(Point);
	// The held surface's normal at the hand, on the eye's side (UpdateArm keeps the head off its plane).
	const FVector Eye = ShoulderLocation();
	const FVector Into = (Point - Eye).GetSafeNormal();
	FHitResult Surface;
	FCollisionQueryParams SurfaceParams(SCENE_QUERY_STAT(OSClingNormal), false, this);
	const FVector Normal = Target->LineTraceComponent(Surface, Eye - Into * 5.0f, Point + Into * 10.0f, SurfaceParams) ? FVector(Surface.ImpactNormal) : -Into;
	HoldNormalLocal = Target->GetComponentTransform().InverseTransformVectorNoScale(Normal);
	ArmRestCm = FMath::Max(ArmMinCm, float(FVector::Dist(ShoulderLocation(), Point)));
	UE_LOG(LogMindKore, Log, TEXT("ZeroG cling: holding on to %s, the hand %.0f cm from the eye, coming in at %.0f cm/s"),
		*GetNameSafe(Target->GetOwner()), ArmRestCm, GetBodyVelocity().Size());
	return true;
}

void AOSZeroGPawn::LetGoCling()
{
	if (MoveMode == EOSMoveMode::Hang)
	{
		SetMode(FreeMode()); // the exit lets go of the hold
	}
}

FVector AOSZeroGPawn::ShoulderLocation() const
{
	return Body->GetComponentLocation() + EyeShownOffset;
}

void AOSZeroGPawn::UpdateEye(float DeltaSeconds)
{
	// The camera at eye height over the body in gravity, at its centre in zero-G, eased both ways.
	EyeOffset = FMath::VInterpTo(EyeOffset, -GravityDirection * EyeHeightCm, DeltaSeconds, EyeRiseRate);
	// It has no collision of its own and rides up to 110 cm over the body, so the body never meets the
	// ceiling the head is in (review #44): a sphere swept from the body's centre to the eye stops it. Under a
	// lower ceiling the eye drops at once, and rises back eased (EyeRiseRate) - an uneven ceiling or the
	// edge of a duct does not shake it. The sphere is wider by this frame's move: the sweep runs before
	// physics moves the body (walking at 450 cm/s under a slab's edge the camera came 10 cm off it, not 15).
	const float Want = float(EyeOffset.Size());
	float Drop = 0.0f;
	if (Want > 1.0f)
	{
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSEyeClear), false, this);
		if (const UPrimitiveComponent* Held = GrabbedBody.Get())
		{
			Params.AddIgnoredComponent(Held);
		}
		const FVector From = Body->GetComponentLocation();
		FHitResult Hit;
		const float Radius = EyeRadiusCm + float(GetBodyVelocity().Size()) * DeltaSeconds;
		if (GetWorld()->SweepSingleByChannel(Hit, From, From + EyeOffset, FQuat::Identity, ECC_Camera, FCollisionShape::MakeSphere(Radius), Params))
		{
			Drop = Want - Hit.Distance;
		}
	}
	EyeDropCm = Drop >= EyeDropCm ? Drop : FMath::FInterpTo(EyeDropCm, Drop, DeltaSeconds, EyeRiseRate);
	EyeShownOffset = Want > 1.0f ? EyeOffset * (FMath::Max(0.0f, Want - EyeDropCm) / Want) : EyeOffset;
}

float AOSZeroGPawn::GetArmLengthCm() const
{
	return float(FVector::Dist(ShoulderLocation(), GetHandLocation()));
}

FVector AOSZeroGPawn::GetHandLocation() const
{
	const UPrimitiveComponent* Target = ClingTarget.IsValid() ? ClingTarget.Get() : GrabbedBody.Get();
	return Target ? Target->GetComponentTransform().TransformPosition(HandLocal) : ShoulderLocation();
}

void AOSZeroGPawn::UpdateArm(float DeltaSeconds)
{
	// Held: the arm is on a held body. Holding on as well, it is on the hold (the keys are the arm, not the
	// thrusters; the head keeps clear of the plane) - steering the hold "to the view" swung the pawn round it.
	UPrimitiveComponent* Held = ClingTarget.IsValid() ? nullptr : GrabbedBody.Get();
	UPrimitiveComponent* Target = ClingTarget.IsValid() ? ClingTarget.Get() : Held;
	if (!Target || DeltaSeconds <= 0.0f)
	{
		return;
	}
	const FVector Hand = Target->GetComponentTransform().TransformPosition(HandLocal);
	const FVector Start = ShoulderLocation() - Hand;
	if (Start.Size() < KINDA_SMALL_NUMBER)
	{
		return;
	}
	// The pawn is one end, the hold (static, infinite mass) or the held body the
	// other. Mu is their reduced mass: the springs below are accelerations of the
	// relative motion, so a 4 kg can and an 800 kg part feel the same arm, and
	// the impulse Mu x change is split equal and opposite.
	const bool bMoving = Target->IsSimulatingPhysics();
	const float PawnMass = Body->GetMass();
	const float Mu = bMoving ? PawnMass * Target->GetMass() / (PawnMass + Target->GetMass()) : PawnMass;
	const FVector Measured = GetBodyVelocity() - (bMoving ? Target->GetPhysicsLinearVelocityAtPoint(Hand) : FVector::ZeroVector);

	// On a fixed hold the thrust keys are the arm, not the thrusters: toward the
	// hand pulls the reach in, across it pumps the swing.
	FVector Wish = FVector::ZeroVector;
	if (!Held)
	{
		const FVector Input = ThrustInput.GetClampedToMaxSize(1.0f);
		ThrustInput = FVector::ZeroVector;
		const FRotationMatrix View(GetControlRotation());
		Wish = View.GetUnitAxis(EAxis::X) * Input.X + View.GetUnitAxis(EAxis::Y) * Input.Y + View.GetUnitAxis(EAxis::Z) * Input.Z;
	}
	const float RestTarget = Held ? HoldRestCm : ArmLengthCm;
	FVector HoldNormal = FVector::ZeroVector;
	bool bBlocked = false;
	if (!Held)
	{
		if (IsInGravity())
		{
			// In gravity the eye rides EyeHeightCm over the body, out of the body's collision: the head keeps
			// HeadClearanceCm off the held surface's plane - the reach is not pulled shorter than puts the eye
			// there, and the cushion below stops a swing onto the plane. In zero-G the eye is the body's centre
			// and the body's own sphere keeps it off the hold (and a plane would stop a swing round an edge).
			HoldNormal = Target->GetComponentTransform().TransformVectorNoScale(HoldNormalLocal);
		}
		// While the body rests against something on its way to the hand (the held door, the ground at the
		// feet) the reach does not shorten - neither pulled in nor relaxed below what it was: the spring
		// pressed the body into the contact every tick, the contact threw it back and the hard catch shook the
		// camera - the hold jittered on doors, walls and the planetoid (os.Head.Probe: 60 shakes a second).
		// Not shortening, it also holds the body from sliding toward a hand down a slope; lengthening (keys
		// away, relaxing back out after a pull) still works and takes the body off the contact.
		FCollisionQueryParams Params(SCENE_QUERY_STAT(OSArmBlock), false, this);
		FCollisionResponseParams Response;
		Body->InitSweepCollisionParams(Params, Response);
		const FVector From = Body->GetComponentLocation();
		const FVector N = Start.GetUnsafeNormal();
		FHitResult Block;
		bBlocked = GetWorld()->SweepSingleByChannel(Block, From, From - N * ArmBlockProbeCm, FQuat::Identity, Body->GetCollisionObjectType(),
			FCollisionShape::MakeSphere(Body->GetScaledSphereRadius()), Params, Response)
			&& FVector::DotProduct(Block.ImpactNormal, N) > 0.3f;
	}
	// On a fixed hold Tick adds gravity after this (Hang): the steps carry it too, to fall and catch within the frame.
	const FVector Gravity = ClingTarget.IsValid() ? GravityDirection * GravityStrengthCm : FVector::ZeroVector;

	// The relative motion in steps of at most 1/240 s, so the arm is the same at any frame rate. One implicit
	// step a frame was stable but damped more the longer the frame; the reach's 20 cm lead and the pump were
	// read once a frame too: at 30 / 60 / 144 FPS a pull mid-swing let go at 272 / 298 / 319 cm/s, a hang
	// stretched the arm 5.6 / 3.6 / 2.6 cm.
	const int32 Steps = FMath::Max(1, FMath::CeilToInt(DeltaSeconds * 240.0f - 0.001f));
	const float Dt = DeltaSeconds / float(Steps);
	const float K = FMath::Square(ArmStiffness);
	const float C = 2.0f * ArmDampingRatio * ArmStiffness;
	const float Ks = FMath::Square(HoldSteerStiffness);
	const float Cs = 2.0f * HoldSteerStiffness;
	// Resting against something (bBlocked), the reach stays at least what it and the arm were when the sweep looked.
	const float BlockedRest = FMath::Max(ArmRestCm, float(Start.Size()));
	FVector Offset = Start;
	FVector Relative = Measured + ArmCarry / Mu;
	float Tension = 0.0f;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		// N points from the hand to the eye.
		const float Length = float(Offset.Size());
		const FVector N = Offset / Length;
		const float Radial = FVector::DotProduct(Relative, N);
		const FVector Across = Relative - N * Radial;
		const float Toward = -FVector::DotProduct(Wish, N);
		if (Toward > 0.1f)
		{
			// The reach leads the eye by at most 20 cm: pulling against a wall stores no spring to fire you off it later.
			ArmRestCm = FMath::Max3(ArmMinCm, ArmRestCm - Toward * ArmPullCmS * Dt, FMath::Min(ArmRestCm, Length - 20.0f));
		}
		else if (Toward < -0.1f && ArmRestCm < RestTarget)
		{
			ArmRestCm = FMath::Min(RestTarget, ArmRestCm - Toward * ArmPullCmS * Dt);
		}
		else
		{
			ArmRestCm = FMath::FInterpConstantTo(ArmRestCm, RestTarget, Dt, ArmRelaxCmS);
		}
		const float Height = float(FVector::DotProduct(Offset, HoldNormal));
		const float Facing = float(FVector::DotProduct(N, HoldNormal));
		if (Facing > KINDA_SMALL_NUMBER)
		{
			ArmRestCm = FMath::Max(ArmRestCm, FMath::Min(HeadClearanceCm / Facing, FMath::Max(ArmLengthCm, ArmMinCm)));
		}
		if (bBlocked)
		{
			ArmRestCm = FMath::Max(ArmRestCm, BlockedRest);
		}

		// The reach: a damped spring along N, implicit Euler on the velocity
		// (v' = (v - dt k x) / (1 + dt c + dt^2 k)).
		const float NewRadial = (Radial - Dt * K * (Length - ArmRestCm)) / (1.0f + Dt * C + Dt * Dt * K);
		FVector Change = N * (NewRadial - Radial);
		Tension = FMath::Max(Tension, FMath::Abs(NewRadial - Radial) / Dt);

		if (Held)
		{
			// The hand steers the held body across the arm: to the view, or where it
			// was grabbed. The same implicit spring, critically damped, on the
			// across-the-arm error of the eye's offset from the hand.
			const FVector Want = -(bHoldInFront ? ViewDirection() : HoldDirection) * Length;
			const FVector Error = FVector::VectorPlaneProject(Offset - Want, N);
			Change += (Across - Dt * Ks * Error) / (1.0f + Dt * Cs + Dt * Dt * Ks) - Across;
		}
		else if (!Wish.IsNearlyZero())
		{
			// Pump: like the thrusters, up to SwingMaxSpeedCm along the input, never braking.
			const FVector Pump = FVector::VectorPlaneProject(Wish, N);
			const float PumpSize = Pump.Size();
			if (PumpSize > 0.1f)
			{
				const FVector Direction = Pump / PumpSize;
				const float Add = FMath::Min(SwingAccelCm * PumpSize * Dt, SwingMaxSpeedCm - FVector::DotProduct(Across, Direction));
				Change += Direction * FMath::Max(Add, 0.0f);
			}
		}
		else
		{
			// Hands still: a big swing lasts, a small one settles to rest (a steady weld).
			const float Speed = Across.Size();
			if (Speed > KINDA_SMALL_NUMBER)
			{
				Change -= Across / Speed * FMath::Min(FMath::Min(SwingDampAccelCm, Speed * SwingDampRate) * Dt, Speed);
			}
		}

		if (!HoldNormal.IsZero())
		{
			// The head's cushion: the eye closes on the held plane no faster than its gap over HeadClearanceCm
			// shuts in HeadCushionSeconds - it eases onto the clearance instead of swinging or springing into the
			// ceiling, and a hold taken closer eases it back out.
			const float Along = float(FVector::DotProduct(Relative + Change, HoldNormal));
			const float Least = (HeadClearanceCm - Height) / HeadCushionSeconds;
			if (Along < Least)
			{
				Change += HoldNormal * (Least - Along);
			}
		}
		Relative += Change + Gravity * Dt;
		Offset += Relative * Dt;
	}

	// Chaos moves the body in a straight line on the velocity it ends the frame with: given the chord, it lands
	// where the steps did. What the steps' end velocity has beyond the chord is carried into the next tick.
	// Exact for the pawn on a hold with one physics step a frame and no contact; a contact, the MaxSpeedCm cap,
	// a frame longer than MaxPhysicsDeltaTime (1/30 s) or a held body's spin leave that frame's carry a little off.
	const FVector Chord = (Offset - Start) / DeltaSeconds;
	ArmCarry = (Relative - Chord) * Mu;
	const FVector Impulse = (Chord - Gravity * DeltaSeconds - Measured) * Mu;
	// Every tick, not a single kick: past AddBodyImpulse's launch rule, which would read a frame's share.
	Body->AddImpulse(Impulse, NAME_None, /*bVelChange=*/false);
	if (bMoving)
	{
		Target->AddImpulseAtLocation(-Impulse, Hand);
	}
	// A hard catch tugs the camera (the crash shake, small).
	if (Tension > ArmTugMinAccelCm)
	{
		KickCamera(ArmTugCmPer1000 * Tension / 1000.0f);
	}
}

void AOSZeroGPawn::HandOverArmCarry()
{
	// The hand lets go of a hold mid-frame of the arm's steps: the velocity they ended with, not the frame's
	// chord - at once, so a respawn's zero velocity right after still wins. (A held body's carry is dropped: SetHands.)
	Body->SetPhysicsLinearVelocity(ArmCarry / Body->GetMass(), /*bAddToCurrent=*/true);
	ArmCarry = FVector::ZeroVector;
}

EOSRmbAction AOSZeroGPawn::GetRmbAction() const
{
	if (!Allows().bUse)
	{
		return EOSRmbAction::None;
	}
	if (GetHands() == EOSHands::Gun && !IsClinging())
	{
		return GrabCandidate.IsValid() ? EOSRmbAction::Grab : EOSRmbAction::Aim;
	}
	if (GrabbedBody.IsValid() || IsClinging())
	{
		return EOSRmbAction::LetGo;
	}
	return GrabCandidate.IsValid() ? EOSRmbAction::Grab : EOSRmbAction::Cling;
}

FString AOSZeroGPawn::GetRmbPrompt() const
{
	if (GetRmbAction() != EOSRmbAction::Grab)
	{
		return FString();
	}
	return GetHands() == EOSHands::Gun ? TEXT("[ПКМ] взять (оружие убрать)") : TEXT("[ПКМ] взять");
}

void AOSZeroGPawn::PushPressed()
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

void AOSZeroGPawn::Roll(const FInputActionValue& Value)
{
	// Only free rotation turns the view about its forward axis; in a gravity
	// zone the controller keeps the view level and drops roll input.
	if (APlayerController* PlayerController = Cast<APlayerController>(GetController()))
	{
		PlayerController->AddRollInput(Value.Get<float>() * RollRateDegPerSec * GetWorld()->DeltaRealTimeSeconds);
	}
}

bool AOSZeroGPawn::HasGrapple() const
{
	const AOSShipGameMode* Mode = GetWorld()->GetAuthGameMode<AOSShipGameMode>();
	return !Mode || Mode->Tools.Contains(OSGrappleTool());
}

void AOSZeroGPawn::ToggleGrapple()
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
	if (!Allows().bGrapple)
	{
		// The ladder holds the body still: a hook would only tug at it (and a hooked body does not take a ladder).
		UE_LOG(LogMindKore, Log, TEXT("ZeroG grapple: not from the ladder"));
		return;
	}
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGrapple), /*bTraceComplex=*/false, this);
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

void AOSZeroGPawn::UpdateGrapple()
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

void AOSZeroGPawn::ToggleCrowbar()
{
	if (HasCrowbar())
	{
		Tags.Remove(MKFastenerCrowbarTag());
	}
	else
	{
		Tags.AddUnique(MKFastenerCrowbarTag());
	}
	UE_LOG(LogMindKore, Log, TEXT("OS: %s %s the crowbar tag"), *GetName(), HasCrowbar() ? TEXT("got") : TEXT("dropped"));
}

bool AOSZeroGPawn::HasCrowbar() const
{
	return ActorHasTag(MKFastenerCrowbarTag());
}

void AOSZeroGPawn::BulletTimePressed()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, true);
	}
}

void AOSZeroGPawn::BulletTimeReleased()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, false);
	}
}

// --- motion -------------------------------------------------------------------

FVector AOSZeroGPawn::ViewLocation() const
{
	// The eye without the shake: the shake moves only the camera, never what the view aims or holds.
	return ShoulderLocation();
}

FVector AOSZeroGPawn::ViewDirection() const
{
	return GetControlRotation().Vector();
}

FVector AOSZeroGPawn::GetBodyVelocity() const
{
	return Body->GetPhysicsLinearVelocity();
}

void AOSZeroGPawn::AddBodyImpulse(const FVector& Impulse)
{
	Body->AddImpulse(Impulse, NAME_None, /*bVelChange=*/false);
	// Walking, a kick up past the threshold leaves the ground as a jump does: Walk would
	// otherwise see the floor within GroundProbeCm next frame and clip the upward speed
	// (an SV-98 shot at the feet lifted the body only below ~70 FPS).
	const float Up = MoveMode == EOSMoveMode::Walk ? float(FVector::DotProduct(Impulse, -GravityDirection)) / Body->GetMass() : 0.0f;
	if (Up > LaunchThresholdCm)
	{
		LastJumpSeconds = GetWorld()->GetTimeSeconds();
		bGrounded = false;
		UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: launched off the ground at %.0f cm/s"), *GetName(), Up);
	}
}

bool AOSZeroGPawn::IsStrapPulling() const
{
	return HullTools->GetLines().ContainsByPredicate([this](const FOSStrapLine& Line)
	{
		return Line.Tension > 0.0f && (Line.A.Get() == Body || Line.B.Get() == Body);
	});
}

bool AOSZeroGPawn::Push()
{
	UWorld* World = GetWorld();
	const double Now = World->GetTimeSeconds();
	if (Now - LastPushSeconds < PushCooldownSeconds)
	{
		return false;
	}
	const FVector Direction = ViewDirection();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSPush), /*bTraceComplex=*/false, this);
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

void AOSZeroGPawn::RefreshGrabCandidate()
{
	GrabCandidate = nullptr;
	if (GrabbedBody.IsValid())
	{
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGrabLook), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + ViewDirection() * GrabReachCm, ECC_Visibility, Params)
		&& Hit.GetComponent() && Hit.GetComponent()->IsSimulatingPhysics())
	{
		GrabCandidate = Hit.GetComponent();
		GrabCandidatePoint = Hit.ImpactPoint;
	}
}

bool AOSZeroGPawn::Grab(UPrimitiveComponent* Target, FVector Point)
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
	// The hand on the grab point, the pair on the arm (UpdateArm): equal and
	// opposite impulses, so momentum is shared - not a handle that moves the
	// prop and leaves the pawn alone. Nothing on the rotation: the prop turns
	// freely about the hand.
	GrabbedBody = Target;
	// Holding on (only a probe gets here: RMB lets go of the hold first), the arm stays on the hold - its hand
	// and reach are kept, and the body is only in hand. They were taken over: the hand jumped on the ceiling, the
	// reach to the body's 90 cm, and os.Hands.Probe's hang swung at up to 550 cm/s.
	if (!IsClinging())
	{
		HandLocal = Target->GetComponentTransform().InverseTransformPosition(Point);
		HoldDirection = (Point - ShoulderLocation()).GetSafeNormal();
		ArmRestCm = FMath::Max(ArmMinCm, float(FVector::Dist(ShoulderLocation(), Point)));
		HoldRestCm = bHoldInFront ? HoldDistanceCm : ArmRestCm;
	}
	GrabCandidate = nullptr;
	Target->WakeAllRigidBodies();
	UE_LOG(LogMindKore, Log, TEXT("ZeroG grab: %s, %.0f kg"), *GetNameSafe(Target->GetOwner()), Target->GetMass());
	// Both hands on it (owner 18.10: «хватать объекты можно но в таком случае игрок переходит в Unarmed»).
	SetHands(EOSHands::Held);
	return true;
}

void AOSZeroGPawn::UpdateDust()
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

void AOSZeroGPawn::Release()
{
	// Hands free (leaving Held lets the body go): back to what was in hand, the gun drawn (owner 19.10).
	if (Hands == EOSHands::Held)
	{
		SetHands(HandsBeforeHeld);
	}
}

void AOSZeroGPawn::ToggleTether()
{
	if (TetheredBody.IsValid())
	{
		DetachTether();
		return;
	}
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSTetherLook), /*bTraceComplex=*/false, this);
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

bool AOSZeroGPawn::AttachTether(UPrimitiveComponent* Target)
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

void AOSZeroGPawn::DetachTether()
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

void AOSZeroGPawn::DrawTether() const
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

void AOSZeroGPawn::HandleBodyHit(UPrimitiveComponent* HitComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
	FVector NormalImpulse, const FHitResult& Hit)
{
	if (OtherComp && OtherComp == GrabbedBody.Get())
	{
		return;
	}
	++ProbeContacts;
	ProbeContactImpulse += float(NormalImpulse.Size());
	UE_CLOG(CVarZeroGTrace.GetValueOnGameThread() > 0, LogMindKore, Log, TEXT("ZeroG trace: hit %s, normal %s, impulse %s"),
		*GetNameSafe(OtherComp), *FVector(Hit.ImpactNormal).ToCompactString(), *NormalImpulse.ToCompactString());
	float SpeedCm = 0.0f;
	OSDeliverCollisionImpact(this, Body, Hit, NormalImpulse, CrashImpact, LastCrashSeconds, SpeedCm);
	const float MinSpeedCm = IsInGravity() ? FMath::Max(FeedbackMinSpeedCm, GroundFeedbackMinSpeedCm) : FeedbackMinSpeedCm;
	const double Now = GetWorld()->GetTimeSeconds();
	// The same crash's later contacts: timed from the last crash, not from the shake - a shot or a bite shakes too.
	if (SpeedCm < MinSpeedCm || Now - LastThumpSeconds < ShakeSeconds * 0.5f)
	{
		return; // A touch, or the same crash again.
	}
	LastThumpSeconds = Now;
	const bool bFlesh = IsFlesh(Hit);
	UE_LOG(LogMindKore, Verbose, TEXT("ZeroG pawn %s: crash thump on %s at %.0f cm/s (%s)"), *GetName(), *GetNameSafe(OtherActor), SpeedCm, bFlesh ? TEXT("flesh") : TEXT("not flesh"));
	if (USoundBase* Sound = bFlesh && FleshCrashSound ? FleshCrashSound.Get() : CrashSound.Get())
	{
		UGameplayStatics::PlaySoundAtLocation(this, Sound, Hit.ImpactPoint, FMath::Clamp(SpeedCm / 1000.0f, 0.2f, 1.0f),
			bFlesh ? FMath::FRandRange(FleshPitchRange.X, FleshPitchRange.Y) : 1.0f);
	}
	ShakeRemaining = ShakeSeconds;
	ShakeAmplitude = ShakeCmPer1000 * SpeedCm / 1000.0f;
	++ProbeShakes;
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

void AOSZeroGPawn::ProbeAim(bool bAim)
{
	bAiming = bAim;
	bLogNextPrediction = bAim;
}

void AOSZeroGPawn::DrawPrediction() const
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
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSPushAim), /*bTraceComplex=*/false, this);
	FHitResult Hit;
	if (GetWorld()->LineTraceSingleByChannel(Hit, ViewLocation(), ViewLocation() + Direction * PushReachCm, ECC_Visibility, Params))
	{
		DrawPath(Body->GetComponentLocation(), GetBodyVelocity() - Direction * PushImpulse / Body->GetMass(),
			Body->GetScaledSphereRadius(), nullptr, FColor(255, 140, 40));
	}
	if (const FOSWeapon* Weapon = Guns->GetWeapon(); Weapon && Weapon->RecoilImpulse > 0.0f)
	{
		DrawPath(Body->GetComponentLocation(), GetBodyVelocity() - Direction * Weapon->RecoilImpulse / Body->GetMass(),
			Body->GetScaledSphereRadius(), nullptr, FColor(255, 230, 90));
	}
}

void AOSZeroGPawn::DrawPath(const FVector& Start, const FVector& Velocity, float RadiusCm, const AActor* Ignored, const FColor& Color) const
{
	// ponytail: an arc under the zones' gravity (OSPredictFlight, stepped at this
	// frame's delta as the physics steps) and one reflection, drawn with debug lines
	// (Development builds); no spin, no drag, the other body treated as static.
	UWorld* World = GetWorld();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSPredict), /*bTraceComplex=*/false, this);
	if (Ignored)
	{
		Params.AddIgnoredActor(Ignored);
	}
	TArray<FOSFlightPoint> Path;
	float HitSeconds = 0.0f;
	const FHitResult Hit = OSPredictFlight(World, Start, Velocity, RadiusCm, PredictSeconds, World->GetDeltaSeconds(),
		PredictRestitution, /*Bounces=*/1, Params, Path, HitSeconds);
	// Dashes of 25 cm every 40 cm, along the whole path.
	float Run = 0.0f;
	for (int32 Index = 1; Index < Path.Num(); ++Index)
	{
		const FVector From = Path[Index - 1].Location;
		const float Length = FVector::Dist(From, Path[Index].Location);
		const FVector Along = (Path[Index].Location - From).GetSafeNormal();
		for (float At = 0.0f; At < Length;)
		{
			const float Phase = FMath::Fmod(Run + At, 40.0f);
			const float Step = FMath::Min(Phase < 25.0f ? 25.0f - Phase : 40.0f - Phase, Length - At);
			if (Phase < 25.0f)
			{
				DrawDebugLine(World, From + Along * At, From + Along * (At + Step), Color, false, -1.0f, 0, 2.0f);
			}
			At += Step;
		}
		Run += Length;
	}
	if (bLogNextPrediction)
	{
		bLogNextPrediction = false;
		const FVector End = Path.Last().Location;
		if (Hit.bBlockingHit)
		{
			UE_LOG(LogMindKore, Log, TEXT("ZeroG prediction: from (%.0f, %.0f, %.0f) at %.0f cm/s, first contact %s at (%.0f, %.0f, %.0f) in %.2f s"),
				Start.X, Start.Y, Start.Z, Velocity.Size(), *GetNameSafe(Hit.GetActor()), Hit.ImpactPoint.X, Hit.ImpactPoint.Y,
				Hit.ImpactPoint.Z, HitSeconds);
		}
		else
		{
			UE_LOG(LogMindKore, Log, TEXT("ZeroG prediction: from (%.0f, %.0f, %.0f) at (%.0f, %.0f, %.0f) cm/s, no contact within %.1f s, ends at (%.0f, %.0f, %.0f)"),
				Start.X, Start.Y, Start.Z, Velocity.X, Velocity.Y, Velocity.Z, PredictSeconds, End.X, End.Y, End.Z);
		}
	}
	if (Hit.bBlockingHit)
	{
		DrawDebugSphere(World, Hit.ImpactPoint, 15.0f, 8, FColor(255, 60, 40), false, -1.0f, 0, 2.0f);
	}
}

void AOSZeroGPawn::ThrowHeld()
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

void AOSZeroGPawn::ApplyThrusters(float DeltaSeconds)
{
	const FVector Input = ThrustInput.GetClampedToMaxSize(1.0f);
	ThrustInput = FVector::ZeroVector;
	if (!bThrustersEnabled || !Allows().bThrusters || DeltaSeconds <= 0.0f)
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

void AOSZeroGPawn::SetGravity(const FVector& Direction, float StrengthCm, bool bRadial)
{
	const bool bWas = IsInGravity();
	GravityDirection = StrengthCm > 0.0f ? Direction.GetSafeNormal() : FVector::ZeroVector;
	GravityStrengthCm = GravityDirection.IsZero() ? 0.0f : StrengthCm;
	bRadialGravity = bRadial;
	if (bWas != IsInGravity())
	{
		bGrounded = false;
		// Free hands follow the zone; a hold or a ladder is left by its own rules.
		if (MoveMode == EOSMoveMode::Float || MoveMode == EOSMoveMode::Walk)
		{
			SetMode(FreeMode());
		}
		UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: %s at %s, %.0f cm/s"), *GetName(), IsInGravity() ? TEXT("gravity on (walking)") : TEXT("gravity off (zero-G)"),
			*GetActorLocation().ToCompactString(), GetBodyVelocity().Size());
	}
}

bool AOSZeroGPawn::ProbeGround(FHitResult& OutHit) const
{
	// The body's sphere, a little thinner, swept a short way along gravity: a
	// blocking hit there that faces up enough is ground.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSGround), /*bTraceComplex=*/false, this);
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

void AOSZeroGPawn::PlayFleshStep(const FHitResult& Ground, float Volume)
{
	const bool bFlesh = IsFlesh(Ground);
	UE_LOG(LogMindKore, Verbose, TEXT("ZeroG pawn %s: step on %s (%s), volume %.2f"), *GetName(), *GetNameSafe(Ground.GetComponent()),
		bFlesh ? TEXT("flesh") : TEXT("not flesh"), Volume);
	if (FleshStepSound && bFlesh)
	{
		UGameplayStatics::PlaySoundAtLocation(this, FleshStepSound, Ground.ImpactPoint, Volume, FMath::FRandRange(FleshPitchRange.X, FleshPitchRange.Y));
	}
}

void AOSZeroGPawn::Walk(float DeltaSeconds)
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
	// Pulled from outside (the hook, a taut strap line): the ground neither brakes the body nor
	// holds it down, as in the air - the hook's 1500 cm/s^2 never beat the 4000 brake and the snap.
	const bool bPulled = GrappleTarget.IsValid() || IsStrapPulling();
	if (bGrounded && bPulled)
	{
		LastGroundedSeconds = GetWorld()->GetTimeSeconds();
	}
	if (bGrounded && !bPulled)
	{
		// On the ground the walk runs in the ground's plane: up a slope the wish
		// climbs along it (in the gravity plane the climb was cancelled every
		// frame and a 25-degree slope took ~25 cm/s, os.Ship.Caves); on level
		// ground this is the gravity plane.
		const FVector Slope = FVector::VectorPlaneProject(Wish, Ground).GetSafeNormal() * Wish.Size();
		Normal = FVector::DotProduct(Velocity, Ground);
		Tangent = Velocity - Ground * Normal;
		const float Accel = Wish.IsNearlyZero() ? WalkBrakingCm : WalkAccelCm;
		Tangent += (Slope - Tangent).GetClampedToMaxSize(Accel * DeltaSeconds);
		// Stuck to the ground: no bounce off the floor or off the curve of the planetoid.
		Body->SetPhysicsLinearVelocity(Tangent + Ground * FMath::Min(Normal, 0.0f));
		// Steps: one on touchdown (louder), then one every StepCm walked. The ground probe finds the
		// ground before the body meets it, so the touchdown step waits for the contact (the fall into
		// the ground stopped, at most 0.1 s): a hard landing's crash plays then, and replaces the step.
		const double Now = GetWorld()->GetTimeSeconds();
		StepTravelCm += float(Tangent.Size()) * DeltaSeconds;
		if (bTouchdown)
		{
			TouchdownSeconds = Now;
			bTouchdownStepPending = true;
			StepTravelCm = 0.0f;
		}
		if (bTouchdownStepPending)
		{
			if (Normal > -100.0f || Now - TouchdownSeconds >= 0.1)
			{
				bTouchdownStepPending = false;
				if (Now - LastThumpSeconds > 0.1)
				{
					PlayFleshStep(GroundHit, StepVolume * 2.0f);
				}
				else
				{
					UE_LOG(LogMindKore, Verbose, TEXT("ZeroG pawn %s: touchdown step skipped, the crash played %.3f s ago"), *GetName(), Now - LastThumpSeconds);
				}
			}
		}
		else if (StepTravelCm >= StepCm)
		{
			PlayFleshStep(GroundHit, StepVolume);
			StepTravelCm = 0.0f;
		}
		LastGroundedSeconds = Now;
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

void AOSZeroGPawn::BeginLadderClimb(AOSLadder* Ladder)
{
	// Entering Ladder: what the mode needs the ladder for (SetMode leaves Walk).
	SetMode(EOSMoveMode::Ladder);
	ActiveLadder = Ladder;
	if (!bRestoreLadderRadius)
	{
		LadderOriginalRadiusCm = Body->GetUnscaledSphereRadius();
	}
	bRestoreLadderRadius = true;
	Body->SetSphereRadius(FMath::Min(LadderOriginalRadiusCm, 34.0f));
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	bGrounded = false;
	bLadderMounting = false;
	LadderTravelCm = 0;
	// Source keeps the climber against the ladder: the face is where the body, swept into it, meets
	// the rungs (or the wall behind). Grabbed further out, the climb would hang under a hatch's lip.
	const FTransform& Frame = Ladder->GetActorTransform();
	const FVector From = GetActorLocation();
	FCollisionQueryParams Params(SCENE_QUERY_STAT(LadderFace), false, this);
	FCollisionResponseParams Response;
	Body->InitSweepCollisionParams(Params, Response);
	FHitResult Face;
	const bool bFace = GetWorld()->SweepSingleByChannel(Face, From, From + Frame.GetUnitAxis(EAxis::Y) * Ladder->Target->GetUnscaledBoxExtent().Y * 2.0f,
		FQuat::Identity, Body->GetCollisionObjectType(), FCollisionShape::MakeSphere(Body->GetScaledSphereRadius()), Params, Response);
	LadderFaceY = Frame.InverseTransformPosition(bFace && !Face.bStartPenetrating ? Face.Location : From).Y - (bFace ? 1.0f : 0.0f);
	PlayRung(Ladder->RungVolume * 1.5f); // the grab
	UE_LOG(LogMindKore, Log, TEXT("ZeroG pawn %s: on %s, face at local Y %.0f (%s)"), *GetName(), *Ladder->GetName(), LadderFaceY,
		bFace ? *GetNameSafe(Face.GetComponent()) : TEXT("nothing in reach"));
}

void AOSZeroGPawn::PlayRung(float Volume)
{
	if (ActiveLadder && ActiveLadder->RungSound)
	{
		UE_LOG(LogMindKore, Verbose, TEXT("ZeroG pawn %s: rung, volume %.2f"), *GetName(), Volume);
		UGameplayStatics::PlaySoundAtLocation(this, ActiveLadder->RungSound, GetActorLocation(), Volume,
			FMath::FRandRange(ActiveLadder->RungPitchRange.X, ActiveLadder->RungPitchRange.Y));
	}
}

void AOSZeroGPawn::EndLadderClimb()
{
	if (MoveMode == EOSMoveMode::Ladder)
	{
		SetMode(FreeMode()); // the exit: off the ladder, the cooldown, the body's radius back
	}
}

void AOSZeroGPawn::RestoreLadderBodyRadius()
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

void AOSZeroGPawn::ClimbLadder(float DeltaSeconds)
{
	const FVector Input = ThrustInput;
	ThrustInput = FVector::ZeroVector;
	// Zero-G: the ladder is ignored (ApplyThrusters takes over; RMB clings to the rungs as to any fixed surface).
	if (!IsValid(ActiveLadder) || !IsInGravity())
	{
		EndLadderClimb();
		return;
	}
	// No gravity on the rungs and no sliding: the body moves by sweeps only, its velocity stays zero.
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	if (DeltaSeconds <= 0.0f)
	{
		return;
	}
	const FTransform& Frame = ActiveLadder->GetActorTransform();
	const FVector Landing = Frame.TransformPosition(ActiveLadder->Top);
	const FVector Start = GetActorLocation();
	const float Speed = ActiveLadder->ClimbSpeedCm;
	float Climb = 0.0f;
	FVector Velocity;
	if (bLadderMounting)
	{
		// Over the lip onto the landing at the climb speed, whatever the keys do: no hanging on the edge.
		Velocity = (Landing - Start).GetSafeNormal() * Speed;
	}
	else
	{
		// Source (CS:GO/CS2): W climbs where the view pitches, up within the dead zone, S the reverse;
		// A/D along the rungs. The push of the view into the ladder is not motion: no drift off its face.
		Climb = Input.X * ActiveLadder->ClimbSign(ViewDirection());
		const float Side = Input.Y * FMath::Sign(FVector::DotProduct(Camera->GetRightVector(), Frame.GetUnitAxis(EAxis::X)));
		Velocity = Frame.TransformVectorNoScale(FVector(Side, 0.0f, Climb).GetClampedToMaxSize(1.0f)) * Speed;
	}
	FVector Delta = bLadderMounting ? (Landing - Start).GetClampedToMaxSize(Speed * DeltaSeconds) : Velocity * DeltaSeconds;
	if (!bLadderMounting && !Velocity.IsNearlyZero())
	{
		// Moving, the body closes on the ladder's face (at most the climb speed): no drift while still.
		const FVector From = Frame.InverseTransformPosition(Start);
		Delta += Frame.GetUnitAxis(EAxis::Y) * FMath::Clamp(LadderFaceY - float(From.Y), -Speed * DeltaSeconds, Speed * DeltaSeconds);
		// No higher than Top.Z, where the mount takes over (a long frame would overshoot and drop back).
		const float Rise = FVector::DotProduct(Delta, Frame.GetUnitAxis(EAxis::Z));
		Delta -= Frame.GetUnitAxis(EAxis::Z) * FMath::Max(0.0f, Rise - FMath::Max(0.0f, ActiveLadder->Top.Z - float(From.Z)));
	}
	FHitResult Hit;
	SetActorLocation(Start + Delta, true, &Hit, ETeleportType::TeleportPhysics);
	if (Hit.bBlockingHit)
	{
		UE_LOG(LogMindKore, VeryVerbose, TEXT("ZeroG pawn %s: ladder move blocked by %s at %s, normal %s%s"), *GetName(), *GetNameSafe(Hit.GetComponent()),
			*Frame.InverseTransformPosition(Hit.ImpactPoint).ToCompactString(), *Frame.InverseTransformVectorNoScale(Hit.ImpactNormal).ToCompactString(),
			Hit.bStartPenetrating ? TEXT(", starting inside") : TEXT(""));
	}
	if (Hit.bBlockingHit && !Hit.bStartPenetrating)
	{
		const FVector Here = Frame.InverseTransformPosition(GetActorLocation());
		if (FVector::DotProduct(Hit.Normal, Frame.GetUnitAxis(EAxis::Y)) < -0.5f)
		{
			// Against the rungs: the face is no deeper than here (a grab at the top finds the rails, behind them), 1 cm clear.
			LadderFaceY = FMath::Min(LadderFaceY, float(Here.Y) - 1.0f);
		}
		FVector Rest = Delta * (1.0f - Hit.Time);
		if (FVector::DotProduct(Hit.Normal, Frame.GetUnitAxis(EAxis::Z)) < -0.3f)
		{
			// A lip overhead (the hatch narrower than the shaft below): the rest of the step goes toward the
			// ladder's axis at its face, where the hatch is open, instead of into the lip.
			const FVector ToAxis = Frame.TransformVectorNoScale(FVector(-Here.X, LadderFaceY - Here.Y, 0.0f));
			Rest = ToAxis.GetClampedToMaxSize(Rest.Size());
		}
		// Slide along rungs while preserving the vertical part of the input.
		const FVector Slide = FVector::VectorPlaneProject(Rest, Hit.Normal);
		SetActorLocation(GetActorLocation() + Slide, true, nullptr, ETeleportType::TeleportPhysics);
	}
	const float Moved = FVector::Dist(GetActorLocation(), Start);
	LadderTravelCm += Moved;
	if (LadderTravelCm >= ActiveLadder->RungStepCm)
	{
		PlayRung(ActiveLadder->RungVolume);
		LadderTravelCm = 0.0f;
	}
	if (bLadderMounting)
	{
		// On the landing, or held by something on the way (less than a fifth of the step made): walking resumes.
		if (FVector::Dist(GetActorLocation(), Landing) < 1.0f || Moved < 0.2f * Delta.Size())
		{
			EndLadderClimb();
		}
		return;
	}
	const FVector Local = Frame.InverseTransformPosition(GetActorLocation());
	if (Climb > 0.0f && Local.Z >= ActiveLadder->Top.Z - 1.0f)
	{
		bLadderMounting = true;
		return;
	}
	// Down (or not climbing up) at the foot with a floor under the body: off, standing. A floor, not a
	// rung's 45-degree bevel (walkable to ProbeGround): off it the body would creep down the rung.
	FHitResult Ground;
	const bool bAtFoot = Climb <= 0.0f && Local.Z <= ActiveLadder->Bottom.Z + 30.0f && ProbeGround(Ground)
		&& FVector::DotProduct(Ground.ImpactNormal, -GravityDirection) > 0.9f;
	if (bAtFoot || !ActiveLadder->IsInReach(GetActorLocation()))
	{
		EndLadderClimb();
		// Off the side keeps the strafe; on the floor the walk takes over from rest.
		Body->SetPhysicsLinearVelocity(bAtFoot ? FVector::ZeroVector : Velocity);
	}
}

void AOSZeroGPawn::JumpPressed()
{
	if (!Allows().bJump)
	{
		return; // Zero-G: Space is the up thruster (Lift); holding on, it is the arm (UpdateArm), never a jump or the jet exit.
	}
	if (MoveMode == EOSMoveMode::Ladder)
	{
		const FVector Away = ActiveLadder->GetActorTransform().TransformVectorNoScale(FVector(0, -1, 0));
		const FVector Up = IsInGravity() ? -GravityDirection : FVector::UpVector;
		JumpedFromLadder = ActiveLadder;
		EndLadderClimb();
		// Source pushes off along the ladder's face normal; a modest push here (~250 cm/s out, ~150 up), the view does not add.
		Body->SetPhysicsLinearVelocity(Away * JumpSpeedCm * 0.6f + Up * JumpSpeedCm * 0.35f);
		LastJumpSeconds = GetWorld()->GetTimeSeconds();
		return;
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

void AOSZeroGPawn::LetGoOfEverything()
{
	if (GrabbedBody.IsValid())
	{
		Release();
	}
	LetGoCling();
	DetachTether();
	GrappleTarget = nullptr;
	if (IsClimbingLadder())
	{
		EndLadderClimb();
	}
	// The torch: its LMB release never arrives once the input is off.
	HullTools->PrimaryReleased();
	const TArray<FOSStrapLine>& Lines = HullTools->GetLines();
	for (int32 Index = Lines.Num() - 1; Index >= 0; --Index)
	{
		if (Lines[Index].bBelt)
		{
			HullTools->CutLine(Index);
		}
	}
	if (Guns->Scope)
	{
		Guns->Scope->SetHeld(false);
		Guns->Scope->SetAimHeld(false);
	}
	BulletTimeReleased();
	bAiming = false;
	bStabilising = false;
	ThrustInput = FVector::ZeroVector;
}

void AOSZeroGPawn::ReviveAt(const FVector& Location)
{
	LetGoOfEverything();
	Body->SetWorldLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
	Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
	Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
	bGrounded = false;
	ShakeRemaining = 0.0f;
	Vitals->Revive();
	Oxygen->Refill(Oxygen->CapacitySeconds);
}

void AOSZeroGPawn::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	// A held or roped body that stops simulating (a plate tacked by its seam) would hang the pawn on the world: let go.
	if (GrabbedBody.IsStale() || (GrabbedBody.IsValid() && !GrabbedBody->IsSimulatingPhysics()))
	{
		Release();
	}
	if (ClingTarget.IsStale())
	{
		LetGoCling();
	}
	if (TetherConstraint && (!TetheredBody.IsValid() || !TetheredBody->IsSimulatingPhysics()))
	{
		DetachTether();
	}
	DrawTether();
	UpdateGrapple();
	DrawPrediction();
	// The camera: at eye height over the body in gravity, at its centre in zero-G
	// (eased both ways), kept off geometry (UpdateEye), plus the shake - a fading
	// random world-space offset.
	UpdateEye(DeltaSeconds);
	ShakeRemaining = FMath::Max(0.0f, ShakeRemaining - DeltaSeconds);
	const FVector Shake = ShakeRemaining > 0.0f
		? FMath::VRand() * ShakeAmplitude * (ShakeRemaining / FMath::Max(ShakeSeconds, 0.01f))
		: FVector::ZeroVector;
	Camera->SetRelativeLocation(Body->GetComponentTransform().InverseTransformVectorNoScale(EyeShownOffset + Shake));
	// The bodycam's field follows CameraFOV live (the debug menu edits it). The pawn is its only writer; the
	// scope narrows only the gun's own first-person field and reads this one from the camera.
	if (Camera->FieldOfView != CameraFOV)
	{
		Camera->SetFieldOfView(CameraFOV);
	}
	RefreshGrabCandidate();
	// The hand: on the hold (Hang) or on a held loose body (in any mode).
	UpdateArm(DeltaSeconds);
	UpdateDust();
	if (MoveMode == EOSMoveMode::Walk && GetWorld()->GetTimeSeconds() >= LadderCooldownUntil
		&& !GrabbedBody.IsValid() && !TetheredBody.IsValid() && !GrappleTarget.IsValid())
	{
		// The ground under the body now, as Walk will find it this tick: bGrounded is the last tick's,
		// and stepping off a hatch's lip into the well the first tick in the air is the grab's. At
		// 60 FPS (7.5 cm a tick at the walk) the stale flag let the body pass the ladder's axis first.
		FHitResult Ground;
		const bool bOnGround = GetWorld()->GetTimeSeconds() - LastJumpSeconds >= 0.25 && ProbeGround(Ground);
		FVector Move = ViewDirection() * ThrustInput.X + Camera->GetRightVector() * ThrustInput.Y;
		if (Move.IsNearlyZero() && !bOnGround)
		{
			Move = GetBodyVelocity().GetSafeNormal();
		}
		for (TActorIterator<AOSLadder> It(GetWorld()); It; ++It)
		{
			if (JumpedFromLadder.Get() == *It)
			{
				if (It->IsInReach(GetActorLocation()))
				{
					continue;
				}
				JumpedFromLadder.Reset();
			}
			if (It->CanEnter(GetActorLocation(), Move, bOnGround))
			{
				BeginLadderClimb(*It);
				break;
			}
		}
	}
	switch (MoveMode)
	{
	case EOSMoveMode::Float:
		ApplyThrusters(DeltaSeconds);
		break;
	case EOSMoveMode::Walk:
		Walk(DeltaSeconds);
		break;
	case EOSMoveMode::Ladder:
		ClimbLadder(DeltaSeconds);
		break;
	case EOSMoveMode::Hang:
		// The arm (UpdateArm) holds the body and took the keys; gravity still pulls, nothing walks,
		// brakes or snaps it to the ground (Walk used to, and fought the arm with the feet on the floor).
		ThrustInput = FVector::ZeroVector;
		Body->AddForce(GravityDirection * GravityStrengthCm, NAME_None, /*bAccelChange=*/true);
		break;
	}
	if (MoveMode != EOSMoveMode::Ladder)
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
	UE_CLOG(CVarZeroGTrace.GetValueOnGameThread() > 0, LogMindKore, Log,
		TEXT("ZeroG trace: t %.3f dt %.4f %s gravity %.0f%s body %s v %s = %.1f cm/s, eye %s, arm %.1f (reach %.1f), contacts %d (%.0f kg cm/s), shakes %d"),
		GetWorld()->GetTimeSeconds(), DeltaSeconds, *UEnum::GetDisplayValueAsText(MoveMode).ToString(), GravityStrengthCm, bGrounded ? TEXT(" grounded") : TEXT(""),
		*Body->GetComponentLocation().ToCompactString(), *GetBodyVelocity().ToCompactString(), GetBodyVelocity().Size(),
		*(ShoulderLocation() - Body->GetComponentLocation()).ToCompactString(), GetArmLengthCm(), ArmRestCm, ProbeContacts, ProbeContactImpulse, ProbeShakes);
}

float AOSZeroGPawn::GetHeldMassKg() const
{
	const UPrimitiveComponent* Held = GrabbedBody.Get();
	return Held ? Held->GetMass() : 0.0f;
}

float AOSZeroGPawn::GetPushCooldownRemaining() const
{
	return FMath::Max(0.0f, float(LastPushSeconds + PushCooldownSeconds - GetWorld()->GetTimeSeconds()));
}

// --- shot ----------------------------------------------------------------------

void AOSZeroGPawn::KickCamera(float AmplitudeCm)
{
	ShakeAmplitude = ShakeRemaining > 0.0f ? FMath::Max(ShakeAmplitude, AmplitudeCm) : AmplitudeCm;
	ShakeRemaining = ShakeSeconds;
	++ProbeShakes;
}

bool AOSZeroGPawn::Fire()
{
	FVector Recoil;
	if (!Allows().bFire || !Guns->Fire(ViewLocation(), ViewDirection(), Recoil))
	{
		return false;
	}
	AddBodyImpulse(Recoil);
	// The kick is felt in the camera too, not only in the speed readout.
	KickCamera(RecoilShakeCmPer10000 * Recoil.Size() / 10000.0f);
	return true;
}

void AOSZeroGPawn::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	Vitals->ApplyIncoming(ResolvedImpact.DamageAmount, ResolvedImpact.Kind, /*bBlocked=*/false, ResolvedImpact.ShotId);
	if (ResolvedImpact.bHasPhysicsImpulse)
	{
		// N s to kg cm/s, as MKPhysicsBridge::ApplyPointImpulse converts; at the body's centre (its rotation is locked).
		AddBodyImpulse(ResolvedImpact.ImpulseNewtonSeconds * 100.0);
	}
}

// --- bullet time presentation ---------------------------------------------------

void AOSZeroGPawn::UpdateBulletTimePresentation()
{
	// ponytail: copy of AOSCharacter::UpdateBulletTimePresentation (itself a port
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
	void LogProbe(const TCHAR* Stage, const AOSZeroGPawn* Pawn)
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
	void ProbeThrusters(AOSZeroGPawn* Pawn)
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
	void ProbeDock(AOSZeroGPawn* Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		AOSDockZone* Dock = nullptr;
		AActor* Part = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			Dock = Dock ? Dock : Cast<AOSDockZone>(*It);
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
	 * is flung at the anchor at 1000 cm/s for a crash, the camera kept shaking (a bite's shake) all the way.
	 */
	void ProbeCollisions(AOSZeroGPawn* Pawn)
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
			if (It->IsA<AOSOxygenCanister>())
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
				UE_LOG(LogMindKore, Log, TEXT("ZeroG probe [crash]: pawn flung at the anchor at 1000 cm/s, health %.0f, the camera kept shaking (a bite's shake must not cancel the crash)"), Pawn->Vitals->Health);
				const TSharedRef<FTimerHandle> Shaking = MakeShared<FTimerHandle>();
				const double ShakeUntil = Pawn->GetWorld()->GetTimeSeconds() + 0.6;
				Pawn->GetWorld()->GetTimerManager().SetTimer(*Shaking, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Shaking, ShakeUntil]()
				{
					if (Pawn->GetWorld()->GetTimeSeconds() > ShakeUntil)
					{
						Pawn->GetWorld()->GetTimerManager().ClearTimer(*Shaking);
						return;
					}
					Pawn->KickCamera(0.5f);
				}), 0.02f, true);
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
	void ProbeTether(AOSZeroGPawn* Pawn, UPrimitiveComponent* Part)
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
	void ProbeCoupledPush(AOSZeroGPawn* Pawn)
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

	/** os.ZeroG.Probe: push off what is in view, then shoot, logging the pawn's velocity around each. */
	void RunZeroGProbe(UWorld* World)
	{
		AOSZeroGPawn* Pawn = nullptr;
		for (TActorIterator<AOSZeroGPawn> It(World); It; ++It)
		{
			Pawn = *It;
			break;
		}
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("ZeroG probe: no AOSZeroGPawn in %s"), *World->GetMapName());
			return;
		}
		// The coupled-push measurement keeps the held thing where it was grabbed.
		Pawn->bHoldInFront = false;
		// The ship's mode locks the guns until their pickups; the probe takes them itself.
		Pawn->Guns->UnlockWeapon(2);
		Pawn->Guns->UnlockWeapon(0);
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
			Pawn->Guns->FinishSwitch(); // no draw: the probe shoots at once
			Pawn->Fire();
			FTimerHandle AfterShot;
			Pawn->GetWorld()->GetTimerManager().SetTimer(AfterShot, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn]()
			{
				LogProbe(TEXT("after pistol"), Pawn);
				Pawn->Guns->SelectWeapon(2);
				Pawn->Guns->FinishSwitch();
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
	 * os.ZeroG.Grapple: hook the nearest fixed block, then a light loose prop,
	 * logging both ends' velocities after a second each; then roll the view
	 * 45 degrees through the controller's roll input (free rotation keeps it).
	 */
	void RunZeroGGrapple(UWorld* World)
	{
		AOSZeroGPawn* Pawn = nullptr;
		for (TActorIterator<AOSZeroGPawn> It(World); It; ++It)
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
		TEXT("os.ZeroG.Grapple"),
		TEXT("Zero-G pawn: hook a fixed block, then a light loose prop (logging both ends after 1 s), then a 45 deg roll input."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunZeroGGrapple));

	FAutoConsoleCommandWithWorld GZeroGProbe(
		TEXT("os.ZeroG.Probe"),
		TEXT("Zero-G pawn: push off what is in view, then shoot; logs the pawn velocity around each."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunZeroGProbe));
}
