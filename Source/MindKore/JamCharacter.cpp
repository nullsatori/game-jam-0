#include "JamCharacter.h"

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
#include "JamGravity.h"
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

AJamCharacter::AJamCharacter(const FObjectInitializer& ObjectInitializer)
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

	// Every path below is MindKore's own content, copied with its reference
	// closure (CREDITS.md lists the sounds' donors).
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

void AJamCharacter::BeginPlay()
{
	Super::BeginPlay();
	if (BulletTimeMaterial)
	{
		BulletTimeMID = UMaterialInstanceDynamic::Create(BulletTimeMaterial, this);
		// Always in the camera's chain; its weight follows the slow-down (0 = off).
		Camera->PostProcessSettings.WeightedBlendables.Array.Add(FWeightedBlendable(0.0f, BulletTimeMID));
	}
}

void AJamCharacter::BuildInput()
{
	Mapping = NewObject<UInputMappingContext>(this, TEXT("JamMapping"));
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

void AJamCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
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
	Input->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AJamCharacter::Move);
	Input->BindAction(LookAction, ETriggerEvent::Triggered, this, &AJamCharacter::Look);
	Input->BindAction(JumpAction, ETriggerEvent::Started, this, &AJamCharacter::JumpPressed);
	Input->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);
	Input->BindActionValueLambda(CrouchAction, ETriggerEvent::Started, [this](const FInputActionValue&) { Crouch(); });
	Input->BindActionValueLambda(CrouchAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { UnCrouch(); });
	Input->BindAction(UseAction, ETriggerEvent::Started, this, &AJamCharacter::Use);
	Input->BindAction(PrimaryAction, ETriggerEvent::Started, this, &AJamCharacter::PrimaryPressed);
	Input->BindAction(PutAction, ETriggerEvent::Started, this, &AJamCharacter::PutDown);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Started, this, &AJamCharacter::BulletTimePressed);
	Input->BindAction(BulletTimeAction, ETriggerEvent::Completed, this, &AJamCharacter::BulletTimeReleased);
	Input->BindAction(CrowbarAction, ETriggerEvent::Started, this, &AJamCharacter::ToggleCrowbar);
}

void AJamCharacter::PawnClientRestart()
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

void AJamCharacter::Move(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	// The view's yaw in gravity space, back to the world: with gravity down this
	// is the plain control yaw; on the planetoid it walks along the surface.
	const FVector Gravity = GetCharacterMovement()->GetGravityDirection();
	const FRotator Yaw(0.0f, JamGravityRelativeRotation(GetControlRotation(), Gravity).Yaw, 0.0f);
	const FRotationMatrix Walk(JamGravityWorldRotation(Yaw, Gravity));
	AddMovementInput(Walk.GetUnitAxis(EAxis::X), Axis.Y);
	AddMovementInput(Walk.GetUnitAxis(EAxis::Y), Axis.X);
}

void AJamCharacter::Look(const FInputActionValue& Value)
{
	const FVector2D Axis = Value.Get<FVector2D>();
	AddControllerYawInput(Axis.X);
	AddControllerPitchInput(-Axis.Y);
}

void AJamCharacter::JumpPressed()
{
	Jump();
}

void AJamCharacter::Use()
{
	Interaction->Interact();
}

void AJamCharacter::PrimaryPressed()
{
	if (Carry->IsHolding())
	{
		Carry->Throw();
		return;
	}
	Fire();
}

void AJamCharacter::PutDown()
{
	Carry->Put();
}

void AJamCharacter::ToggleCrowbar()
{
	if (HasCrowbar())
	{
		Tags.Remove(MKFastenerCrowbarTag());
	}
	else
	{
		Tags.AddUnique(MKFastenerCrowbarTag());
	}
	UE_LOG(LogMindKore, Log, TEXT("Jam: %s %s the crowbar tag (%s)"), *GetName(),
		HasCrowbar() ? TEXT("got") : TEXT("dropped"), *MKFastenerCrowbarTag().ToString());
}

bool AJamCharacter::HasCrowbar() const
{
	return ActorHasTag(MKFastenerCrowbarTag());
}

void AJamCharacter::Fire()
{
	UWorld* World = GetWorld();
	if (!World || !Controller)
	{
		return;
	}
	// From the pawn's own camera, not the camera manager's cached view: the
	// same point in play, and right on the frame of a teleport (console smoke).
	const FVector Start = Camera->GetComponentLocation();
	const FVector Direction = GetControlRotation().Vector();

	for (USoundBase* Sound : ShotSounds)
	{
		UGameplayStatics::PlaySound2D(this, Sound);
	}

	// As UMKFirearmComponent traces: the Shot channel (ECC_GameTraceChannel2),
	// complex collision (the glass pane's shard triangles), physical material.
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamShot), /*bTraceComplex=*/true, this);
	Params.bReturnPhysicalMaterial = true;
	// Multi-trace and MKSelectResolvedHit, as UMKFirearmComponent::PerformImpactTrace:
	// hit-zone boxes only overlap Shot, so they arrive as touches in front of
	// the blocking hit and win on zone priority within the same actor.
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
	// Not in MindKore: a body made only of hit-zone boxes (AMKImpactDummy) has
	// nothing that blocks Shot, so its nearest zone stands in for the blocking
	// hit; without this the rule above always picks the wall behind the dummy.
	if (NearestZone != INDEX_NONE)
	{
		Candidates[NearestZone].bBlocking = true;
	}
	const int32 Selected = MKSelectResolvedHit(Candidates, /*SameActorToleranceCm=*/2.5f);
	if (Selected == INDEX_NONE)
	{
		UE_LOG(LogMindKore, Log, TEXT("Jam shot: no hit"));
		return;
	}
	const FHitResult& Hit = Hits[Selected];

	FMKImpactResolveParams ResolveParams;
	ResolveParams.ImpactData = ImpactData;
	FMKImpactFeedback Feedback;
	FMKResolvedImpact Resolved = MKResolveImpactFromHit(Hit, Direction, ResolveParams, Feedback);
	// Identity, damage and impulse are the caller's (UMKFirearmComponent::ResolveApplyAndFeedback).
	Resolved.ShotId = FGuid::NewGuid();
	Resolved.Kind = EMKImpactKind::Round;
	Resolved.SourceActor = this;
	Resolved.ImpactTimeSeconds = World->GetTimeSeconds();
	Resolved.RangeCm = FVector::Dist(GetActorLocation(), Hit.ImpactPoint);
	Resolved.DamageAmount = ShotDamage * Resolved.DamageMultiplier;
	Resolved.ImpulseNewtonSeconds = Resolved.ShotDirection * ShotImpulseNewtonSeconds;
	Resolved.bHasPhysicsImpulse = true;
	const bool bDelivered = MKDeliverResolvedImpact(this, this, Resolved);
	UE_LOG(LogMindKore, Log, TEXT("Jam shot: %s / %s, surface %d, zone %d, damage %.1f, %s"),
		*GetNameSafe(Hit.GetActor()), *GetNameSafe(Hit.GetComponent()), int32(Resolved.SurfaceKind),
		int32(Resolved.HitZone), Resolved.DamageAmount, bDelivered ? TEXT("delivered") : TEXT("no receiver"));

	if (Feedback.ImpactSound)
	{
		UGameplayStatics::PlaySoundAtLocation(this, Feedback.ImpactSound, Hit.ImpactPoint);
	}
	if (UMaterialInterface* DecalMaterial = Feedback.DecalMaterial ? Feedback.DecalMaterial.Get() : DefaultImpactDecal.Get())
	{
		UGameplayStatics::SpawnDecalAttached(DecalMaterial, FVector(4.0f, 6.0f, 6.0f), Hit.GetComponent(), NAME_None,
			Hit.ImpactPoint, (-Hit.ImpactNormal).Rotation(), EAttachLocation::KeepWorldPosition, 30.0f);
	}
}

void AJamCharacter::ReceiveResolvedImpact_Implementation(const FMKResolvedImpact& ResolvedImpact)
{
	Vitals->ApplyIncoming(ResolvedImpact.DamageAmount, ResolvedImpact.Kind, /*bBlocked=*/false, ResolvedImpact.ShotId);
}

void AJamCharacter::BulletTimePressed()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, true);
	}
}

void AJamCharacter::BulletTimeReleased()
{
	if (UMKTimeDomainSubsystem* Time = GetWorld()->GetSubsystem<UMKTimeDomainSubsystem>())
	{
		Time->SetRequested(this, false);
	}
}

void AJamCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateBulletTimePresentation();
}

void AJamCharacter::UpdateBulletTimePresentation()
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
