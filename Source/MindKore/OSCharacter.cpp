#include "OSCharacter.h"

#include "OSLens.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "InputModifiers.h"
#include "OSGravity.h"
#include "OSScope.h"
#include "OSWeapons.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MKCarryComponent.h"
#include "MKFastener.h"
#include "MKImpactDelivery.h"
#include "MKInteractionComponent.h"
#include "MKKeyring.h"
#include "MKTimeDomainSubsystem.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "Sound/SoundBase.h"
#include "UObject/ConstructorHelpers.h"

AOSCharacter::AOSCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	PrimaryActorTick.bCanEverTick = true;

	GetCapsuleComponent()->InitCapsuleSize(34.0f, 88.0f);
	BaseEyeHeight = 64.0f;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(GetCapsuleComponent());
	Camera->SetRelativeLocation(FVector(0.0f, 0.0f, BaseEyeHeight));
	Camera->bUsePawnControlRotation = true;

	UCharacterMovementComponent* Movement = GetCharacterMovement();
	// Crouch is the engine's: on the ground the capsule shrinks from the top;
	// in the air it shrinks around its centre, so a crouch pressed after the
	// jump lifts the feet by (88 - 40) cm - the crouch-jump onto a ledge.
	Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
	Movement->SetCrouchedHalfHeight(40.0f);
	Movement->AirControl = 0.35f;

	Interaction = CreateDefaultSubobject<UMKInteractionComponent>(TEXT("Interaction"));
	Carry = CreateDefaultSubobject<UMKCarryComponent>(TEXT("Carry"));
	Keyring = CreateDefaultSubobject<UMKKeyringComponent>(TEXT("Keyring"));
	Vitals = CreateDefaultSubobject<UMKVitalsComponent>(TEXT("Vitals"));
	Guns = CreateDefaultSubobject<UOSWeaponsComponent>(TEXT("Guns"));

	// Every path below is MindKore's own content, copied with its reference
	// closure (CREDITS.md lists the sounds' donors).
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

void AOSCharacter::BeginPlay()
{
	Super::BeginPlay();
	OSApplyBodycamLens(*Camera, CameraFOV, bLensDistortion);
	if (BulletTimeMaterial)
	{
		BulletTimeMID = UMaterialInstanceDynamic::Create(BulletTimeMaterial, this);
		// Always in the camera's chain; its weight follows the slow-down (0 = off).
		Camera->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0.0f, BulletTimeMID));
	}
}

void AOSCharacter::BuildInput()
{
	Mapping = NewObject<UInputMappingContext>(this, TEXT("OSMapping"));
	const auto MakeAction = [this](const TCHAR* Name, EInputActionValueType Type)
	{
		UInputAction* Action = NewObject<UInputAction>(this, Name);
		Action->ValueType = Type;
		return Action;
	};
	MoveAction = MakeAction(TEXT("IA_Move"), EInputActionValueType::Axis2D);
	LookAction = MakeAction(TEXT("IA_Look"), EInputActionValueType::Axis2D);
	JumpAction = MakeAction(TEXT("IA_Jump"), EInputActionValueType::Boolean);
	CrouchAction = MakeAction(TEXT("IA_Crouch"), EInputActionValueType::Boolean);
	UseAction = MakeAction(TEXT("IA_Use"), EInputActionValueType::Boolean);
	PrimaryAction = MakeAction(TEXT("IA_Primary"), EInputActionValueType::Boolean);
	PutAction = MakeAction(TEXT("IA_Put"), EInputActionValueType::Boolean);
	BulletTimeAction = MakeAction(TEXT("IA_BulletTime"), EInputActionValueType::Boolean);
	CrowbarAction = MakeAction(TEXT("IA_Crowbar"), EInputActionValueType::Boolean);

	// WASD onto one 2D axis: D is +X as read, W is +Y (swizzled), A and S negated.
	const auto MapMove = [this](const FKey& Key, bool bSwizzle, bool bNegate)
	{
		FEnhancedActionKeyMapping& KeyMapping = Mapping->MapKey(MoveAction, Key);
		if (bSwizzle)
		{
			KeyMapping.Modifiers.Add(NewObject<UInputModifierSwizzleAxis>(this));
		}
		if (bNegate)
		{
			KeyMapping.Modifiers.Add(NewObject<UInputModifierNegate>(this));
		}
	};
	MapMove(EKeys::W, true, false);
	MapMove(EKeys::S, true, true);
	MapMove(EKeys::D, false, false);
	MapMove(EKeys::A, false, true);
	Mapping->MapKey(LookAction, EKeys::Mouse2D);
	Mapping->MapKey(JumpAction, EKeys::SpaceBar);
	Mapping->MapKey(CrouchAction, EKeys::C);
	Mapping->MapKey(CrouchAction, EKeys::LeftControl);
	Mapping->MapKey(UseAction, EKeys::E);
	Mapping->MapKey(PrimaryAction, EKeys::LeftMouseButton);
	Mapping->MapKey(PutAction, EKeys::RightMouseButton);
	Mapping->MapKey(PutAction, EKeys::F);
	Mapping->MapKey(BulletTimeAction, EKeys::Q);
	Mapping->MapKey(CrowbarAction, EKeys::G);
}

void AOSCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);
	if (!Mapping)
	{
		BuildInput();
	}
	UEnhancedInputComponent* Input = Cast<UEnhancedInputComponent>(PlayerInputComponent);
	if (!Input)
	{
		UE_LOG(LogMindKore, Error, TEXT("%s: input component is not an UEnhancedInputComponent (Config/DefaultInput.ini)"), *GetName());
		return;
	}
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AOSCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AOSCharacter::Look);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AOSCharacter::JumpPressed);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindActionValueLambda(CrouchAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Crouch(); });
	Input->BindActionValueLambda(CrouchAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { UnCrouch(); });
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AOSCharacter::Use);
	Input->BindAction(PrimaryAction, ETriggerEvent::Started, this, &AOSCharacter::PrimaryPressed);
	// A bAutomatic gun (none by default) keeps firing while LMB is held; Fire's interval gate makes the press frame one shot.
	Input->BindActionValueLambda(PrimaryAction, ETriggerEvent::Triggered, [this](const FInputActionValue&)
	{
		const FOSWeapon* Weapon = Guns->GetWeapon();
		if (!Carry->IsHolding() && Weapon && Weapon->bAutomatic)
		{
			Fire();
		}
	});
	Input->BindAction(PutAction, ETriggerEvent::Started, this, &AOSCharacter::PutDown);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Started, this, &AOSCharacter::BulletTimePressed);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Completed, this, &AOSCharacter::BulletTimeReleased);
	Input->BindAction(CrowbarAction, ETriggerEvent::Started, this, &AOSCharacter::ToggleCrowbar);
	// The guns' keys, bound by each pawn (the zero-G pawn routes its own, AOSZeroGPawn::Route): R reload, 1..N select,
	// the wheel steps through the unlocked guns, MMB aims. The mapping holds the actions.
	const auto Bind = [this, Input](const FString& Name, EInputActionValueType Type, const FKey& Key, ETriggerEvent Event, TFunction<void(const FInputActionValue&)> Do)
	{
		UInputAction* Action = NewObject<UInputAction>(this, *Name);
		Action->ValueType = Type;
		Mapping->MapKey(Action, Key);
		Input->BindActionValueLambda(Action, Event, MoveTemp(Do));
		return Action;
	};
	Bind(TEXT("IA_Reload"), EInputActionValueType::Boolean, EKeys::R, ETriggerEvent::Started, [this](const FInputActionValue&) { Guns->Reload(); });
	Bind(TEXT("IA_Wheel"), EInputActionValueType::Axis1D, EKeys::MouseWheelAxis, ETriggerEvent::Started,
		[this](const FInputActionValue& Value) { Guns->SelectStep(Value.Get<float>() > 0.0f ? 1 : -1); });
	static const FKey Digits[] = { EKeys::One, EKeys::Two, EKeys::Three, EKeys::Four, EKeys::Five, EKeys::Six, EKeys::Seven, EKeys::Eight, EKeys::Nine };
	for (int32 Index = 0; Index < FMath::Min(Guns->Weapons.Num(), int32(UE_ARRAY_COUNT(Digits))); ++Index)
	{
		Bind(FString::Printf(TEXT("IA_Weapon%d"), Index + 1), EInputActionValueType::Boolean, Digits[Index], ETriggerEvent::Started,
			[this, Index](const FInputActionValue&) { Guns->SelectWeapon(Index); });
	}
	UInputAction* Aim = Bind(TEXT("IA_Scope"), EInputActionValueType::Boolean, EKeys::MiddleMouseButton, ETriggerEvent::Started,
		[this](const FInputActionValue&) { Guns->Scope->SetHeld(true); });
	Input->BindActionValueLambda(Aim, ETriggerEvent::Completed, [this](const FInputActionValue&) { Guns->Scope->SetHeld(false); });
}

void AOSCharacter::PawnClientRestart()
{
	Super::PawnClientRestart();
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	const ULocalPlayer* LocalPlayer = PlayerController ? PlayerController->GetLocalPlayer() : nullptr;
	if (UEnhancedInputLocalPlayerSubsystem* Subsystem = LocalPlayer ? LocalPlayer->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>() : nullptr)
	{
		if (!Mapping)
		{
			BuildInput();
		}
		Subsystem->AddMappingContext(Mapping, 0);
	}
}

void AOSCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	// The view's yaw in gravity space, back to the world: with gravity down this
	// is the plain control yaw; on the planetoid it walks along the surface.
	const FVector Gravity = GetCharacterMovement()->GetGravityDirection();
	const FRotator Yaw(0.0f, OSGravityRelativeRotation(GetControlRotation(), Gravity).Yaw, 0.0f);
	const FRotationMatrix Walk(OSGravityWorldRotation(Yaw, Gravity));
	AddMovementInput(Walk.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Walk.GetUnitAxis(EAxis::Y), Axis.X);
}

void AOSCharacter::Look(const FInputActionValue& Value)
{
	// Slower through the scope, as much as it magnifies (UOSScopeComponent::GetLookScale).
	const FVector2D Axis = Value.Get<FVector2D>() * (Guns->Scope ? Guns->Scope->GetLookScale() : 1.0f);
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void AOSCharacter::JumpPressed()
{
	Jump();
}

void AOSCharacter::Use()
{
	Interaction->Interact();
}

void AOSCharacter::PrimaryPressed()
{
	if (Carry->IsHolding())
	{
		Carry->Throw();
		return;
	}
	Fire();
}

void AOSCharacter::PutDown()
{
	Carry->Put();
}

void AOSCharacter::ToggleCrowbar()
{
	if (HasCrowbar())
	{
		Tags.Remove(MKFastenerCrowbarTag());
	}
	else
	{
		Tags.AddUnique(MKFastenerCrowbarTag());
	}
	UE_LOG(LogMindKore, Log, TEXT("OS: %s %s the crowbar tag (%s)"), *GetName(),
		HasCrowbar() ? TEXT("got") : TEXT("dropped"), *MKFastenerCrowbarTag().ToString());
}

bool AOSCharacter::HasCrowbar() const
{
	return ActorHasTag(MKFastenerCrowbarTag());
}

bool AOSCharacter::Fire()
{
	// From the pawn's own camera, not the camera manager's cached view: the
	// same point in play, and right on the frame of a teleport (console smoke).
	FVector Recoil;
	if (!Guns->Fire(Camera->GetComponentLocation(), GetControlRotation().Vector(), Recoil))
	{
		return false;
	}
	UCharacterMovementComponent* Movement = GetCharacterMovement();
	FVector Kick = Recoil / FMath::Max(Movement->Mass, 1.0f) * RecoilFraction;
	if (Movement->IsMovingOnGround())
	{
		// Along the ground only: the kick slides you back, it never lifts you off.
		Kick = FVector::VectorPlaneProject(Kick, Movement->GetGravityDirection());
	}
	Movement->Velocity += Kick;
	return true;
}

void AOSCharacter::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	Vitals->ApplyIncoming(ResolvedImpact.DamageAmount, ResolvedImpact.Kind, /*bBlocked=*/false, ResolvedImpact.ShotId);
}

void AOSCharacter::BulletTimePressed()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, true);
	}
}

void AOSCharacter::BulletTimeReleased()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, false);
	}
}

void AOSCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBulletTimePresentation();
}

void AOSCharacter::UpdateBulletTimePresentation()
{
	// Ported from UMKCameraComponent::AdvanceBulletTimePresentation
	// (MKCameraBulletTime.cpp): entry/exit cue and the post-process MID. Cut:
	// the ambience sound mix and the trajectory arc (ArcA..C), which belong to
	// MindKore's revolver; ArcVisible stays 0.
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
