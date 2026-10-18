#include "JamScope.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "JamHullTools.h"
#include "JamLivingWall.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MindKore.h"
#include "TimerManager.h"

UJamScopeComponent::UJamScopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	LensMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/Materials/Weapons/M_Jam_ScopeLens.M_Jam_ScopeLens")));
}

float UJamScopeComponent::GetCaptureFieldOfView() const
{
	// The eyepiece's half-angle as drawn: atan(R / relief) in the gun's own field
	// of view, carried into the world's - tan scales by tan(world/2) / tan(gun/2).
	// The picture in it spans Magnification times less of the world.
	const float WorldTan = FMath::Tan(FMath::DegreesToRadians(WorldFieldOfView * 0.5f));
	const float GunTan = FMath::Tan(FMath::DegreesToRadians(AimGunFieldOfView * 0.5f));
	const float LensTan = LensRadiusCm / EyeReliefCm * WorldTan / GunTan;
	return 2.0f * FMath::RadiansToDegrees(FMath::Atan(LensTan / Magnification));
}

float UJamScopeComponent::GetLookScale() const
{
	// Screen-matched: the world through the eyepiece moves Magnification times
	// faster on screen than the unzoomed world around it.
	return FMath::Lerp(1.0f, 1.0f / Magnification, Alpha);
}

void UJamScopeComponent::AimPose(int32 Weapon, FTransform& Rest, float& KickScale) const
{
	if (Weapon != ScopedWeapon || Alpha <= 0.0f)
	{
		return;
	}
	// At the eye: the scope's axis (parallel to the gun's X) on the camera's, the
	// eyepiece EyeReliefCm ahead.
	Rest.SetTranslation(FMath::Lerp(Rest.GetTranslation(), FVector(EyeReliefCm, 0.0f, 0.0f) - EyepieceCm, Alpha));
	Rest.SetRotation(FQuat::Slerp(Rest.GetRotation(), FQuat::Identity, Alpha));
	KickScale = FMath::Lerp(1.0f, AimKickScale, Alpha);
}

void UJamScopeComponent::BindInput(UEnhancedInputComponent& Input, UInputMappingContext& Mapping)
{
	if (!AimAction)
	{
		AimAction = NewObject<UInputAction>(this, TEXT("IA_Jam_Scope"));
		AimAction->ValueType = EInputActionValueType::Boolean;
		Mapping.MapKey(AimAction, ScopeKey);
	}
	Input.BindActionValueLambda(AimAction, ETriggerEvent::Started, [this](const FInputActionValue&) { bHeld = true; });
	Input.BindActionValueLambda(AimAction, ETriggerEvent::Completed, [this](const FInputActionValue&) { bHeld = false; });
}

bool UJamScopeComponent::Build()
{
	Weapons = GetOwner()->FindComponentByClass<UJamWeaponsComponent>();
	Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	UStaticMeshComponent* Gun = Weapons ? Weapons->ViewModel.Get() : nullptr;
	if (!Gun || !Camera || !Weapons->Weapons.IsValidIndex(ScopedWeapon))
	{
		return false;
	}
	// The bodycam's field, as the pawn set it on BeginPlay (JamApplyBodycamLens).
	WorldFieldOfView = Camera->FieldOfView;
	// The weapons component ticks first: it poses the gun with the last tick's
	// alpha (AimPose), and the lens then reads the gun where it is drawn this frame.
	AddTickPrerequisiteComponent(Weapons);

	Target = NewObject<UTextureRenderTarget2D>(this, TEXT("ScopeTarget"));
	Target->RenderTargetFormat = RTF_RGBA16f;
	Target->ClearColor = FLinearColor::Black;
	Target->InitAutoFormat(RenderTargetSize, RenderTargetSize);
	Target->UpdateResourceImmediate(true);

	Capture = NewObject<USceneCaptureComponent2D>(GetOwner(), TEXT("ScopeCapture"));
	Capture->SetupAttachment(Gun);
	Capture->SetRelativeLocation(ObjectiveCm);
	Capture->FOVAngle = GetCaptureFieldOfView();
	Capture->TextureTarget = Target;
	// Linear scene colour before post: shown as emissive, the main view's
	// exposure and tonemapper apply once - the same as to the world around it.
	Capture->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
	Capture->bCaptureEveryFrame = false;
	Capture->bCaptureOnMovement = false;
	// Keeps its view state (Lumen's caches, history) between aims.
	Capture->bAlwaysPersistRenderingState = true;
	Capture->RegisterComponent();

	static UStaticMesh* Plane = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane.Plane"));
	Lens = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ScopeLens"));
	Lens->SetStaticMesh(Plane);
	Lens->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Lens->SetCastShadow(false);
	Lens->SetOnlyOwnerSee(true);
	Lens->SetupAttachment(Gun);
	// The engine plane (100 cm, facing +Z) turned to face the eye (-X): its X is
	// the gun's up, its Y the gun's right - M_Jam_ScopeLens reads them so.
	Lens->SetRelativeLocationAndRotation(EyepieceCm, FRotator(90.0f, 0.0f, 0.0f));
	Lens->SetRelativeScale3D(FVector(LensRadiusCm * 2.0f / 100.0f));
	if (UMaterialInterface* Material = LensMaterial.LoadSynchronous())
	{
		LensMID = UMaterialInstanceDynamic::Create(Material, this);
		LensMID->SetTextureParameterValue(TEXT("Scene"), Target);
		Lens->SetMaterial(0, LensMID);
	}
	else
	{
		UE_LOG(LogMindKore, Warning, TEXT("Scope %s: no %s (Scripts/jam_scope.py)"), *GetNameSafe(GetOwner()), *LensMaterial.ToString());
	}
	Lens->RegisterComponent();

	// Everything the owner has now - the gun, its moving part, flash, smoke,
	// dust, the lens itself - stays out of the picture; tracers made later do not.
	// What hangs on the camera (the gun, its part, the lens, flash and smoke) is
	// drawn as first person: aimed, in the gun's own field of view.
	TArray<UPrimitiveComponent*> Primitives;
	GetOwner()->GetComponents(Primitives);
	for (UPrimitiveComponent* Primitive : Primitives)
	{
		Capture->HideComponent(Primitive);
		if (Primitive->IsAttachedTo(Camera))
		{
			Primitive->SetFirstPersonPrimitiveType(EFirstPersonPrimitiveType::FirstPerson);
		}
	}
	UE_LOG(LogMindKore, Log, TEXT("Scope %s: capture FOV %.2f deg (x%.1f), target %dx%d, %d owner primitives hidden, key %s"),
		*GetNameSafe(GetOwner()), Capture->FOVAngle, Magnification, RenderTargetSize, RenderTargetSize, Primitives.Num(), *ScopeKey.ToString());
	return true;
}

void UJamScopeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Capture && !Build())
	{
		return;
	}
	// The scope is on the rifle in view: up only while it is drawn, not with a
	// hull tool in hand and not during a reload.
	const UStaticMeshComponent* Gun = Weapons->ViewModel;
	const bool bScopeInView = Gun->GetStaticMesh() && Gun->GetStaticMesh() == Weapons->Weapons[ScopedWeapon].ViewMesh;
	const UJamHullToolsComponent* Tools = GetOwner()->FindComponentByClass<UJamHullToolsComponent>();
	const bool bWants = bHeld && bScopeInView && Weapons->CurrentWeapon == ScopedWeapon
		&& !(Tools && Tools->GetTool() != EJamHullTool::None) && Weapons->GetReloadRemaining() <= 0.0f;
	const float WasAlpha = Alpha;
	Progress = FMath::Clamp(Progress + (bWants ? DeltaTime : -DeltaTime) / AimSeconds, 0.0f, 1.0f);
	// Smootherstep: no speed at either end, fastest halfway.
	Alpha = Progress * Progress * Progress * (Progress * (Progress * 6.0f - 15.0f) + 10.0f);
	Lens->SetVisibility(bScopeInView);
	Capture->bCaptureEveryFrame = Alpha > 0.0f;
	if (Alpha > 0.0f || WasAlpha > 0.0f)
	{
		// The world keeps the bodycam's field (owner: no zoom outside the scope);
		// only the gun narrows its own, coming up large to the eye.
		Camera->SetEnableFirstPersonFieldOfView(Alpha > 0.0f);
		Camera->SetFirstPersonFieldOfView(FMath::Lerp(WorldFieldOfView, AimGunFieldOfView, Alpha));
	}
	if (LensMID && bScopeInView)
	{
		// The eye (the camera's origin) in the gun's frame, from the eyepiece: the
		// lens's x is the gun's right, its y the gun's down; z how far the eye is
		// off its relief, cm. At the hip the shadow covers the glass.
		const FVector Eye = Gun->GetRelativeTransform().InverseTransformPosition(FVector::ZeroVector) - EyepieceCm;
		LensMID->SetVectorParameterValue(TEXT("Eye"), FLinearColor(Eye.Y * ShadowPerCm, -Eye.Z * ShadowPerCm, -Eye.X - EyeReliefCm, 0.0f));
	}
}

FString UJamScopeComponent::Describe() const
{
	return FString::Printf(TEXT("aim %.2f, capture %s, capture FOV %.2f deg (x%.1f), target %dx%d, world FOV %.1f, gun FOV %.1f (%s), look scale %.3f"),
		Alpha, Capture && Capture->bCaptureEveryFrame ? TEXT("on") : TEXT("off"), Capture ? Capture->FOVAngle : 0.0f, Magnification,
		Target ? Target->SizeX : 0, Target ? Target->SizeY : 0, Camera ? Camera->FieldOfView : 0.0f,
		Camera ? Camera->FirstPersonFieldOfView : 0.0f, Camera && Camera->bEnableFirstPersonFieldOfView ? TEXT("on") : TEXT("off"), GetLookScale());
}

namespace
{
	/**
	 * jam.Scope.Shots: the SV-98's scope in stills (HighResShot; needs a real RHI;
	 * Lvl_Jam_Range). The zero-G pawn stands 19.5 m before the living wall nearest
	 * to 20 m away and looks at it: the rifle at the hip, raising, aimed at the
	 * wall's centre and at its top edge, a shot (+0.05, +0.15, +0.6 s in the bolt
	 * cycle, +1.5 s), then at the hip again. Every still is logged first with the
	 * scope's state; GPU and frame times are sampled at the hip and aimed.
	 */
	void RunScopeShots(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		UJamScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UJamScopeComponent>() : nullptr;
		if (!Scope)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Scope shots: needs the AJamZeroGPawn with its scope (made when input binds)"));
			return;
		}
		const AJamLivingWall* Wall = nullptr;
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			const float Error = FMath::Abs(FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation()) - 2000.0f);
			if (!Wall || Error < FMath::Abs(FVector::Dist(Wall->GetActorLocation(), Pawn->GetActorLocation()) - 2000.0f))
			{
				Wall = *It;
			}
		}
		if (!Wall)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Scope shots: no living wall"));
			return;
		}
		const FVector Centre = Wall->GetActorLocation();
		FVector Stand = Centre + Wall->GetFrontNormal() * 1950.0f;
		Stand.Z = Pawn->GetActorLocation().Z;
		const auto Hold = [Pawn, Controller, Stand](const FVector& At)
		{
			Pawn->Body->SetWorldLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation((At - Pawn->Camera->GetComponentLocation()).Rotation());
		};
		const auto Shot = [Pawn, Scope, Controller, Centre](const TCHAR* Name)
		{
			UE_LOG(LogMindKore, Log, TEXT("Scope shots: %s - %s; %s; %.0f cm to the wall"), Name, *Scope->Describe(), *Pawn->Guns->Describe(),
				FVector::Dist(Pawn->Camera->GetComponentLocation(), Centre));
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamScope_%s"), Name));
		};
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		// Ten GPU frame times 0.1 s apart and the mean frame time over that second.
		const auto Measure = [After, Scope](float At, const TCHAR* Name)
		{
			TSharedRef<TArray<double>> Gpu = MakeShared<TArray<double>>();
			TSharedRef<TPair<uint64, double>> Start = MakeShared<TPair<uint64, double>>();
			After(At, [Start]() { *Start = TPair<uint64, double>(GFrameCounter, FPlatformTime::Seconds()); });
			for (int32 Index = 1; Index <= 10; ++Index)
			{
				After(At + Index * 0.1f, [Gpu]() { Gpu->Add(FPlatformTime::ToMilliseconds(RHIGetGPUFrameCycles())); });
			}
			After(At + 1.05f, [Gpu, Start, Scope, Name]()
			{
				double Sum = 0.0;
				for (const double Each : *Gpu)
				{
					Sum += Each;
				}
				const double Frames = double(GFrameCounter - Start->Key);
				UE_LOG(LogMindKore, Log, TEXT("Scope cost [%s]: GPU %.2f ms (mean of %d), frame %.2f ms over %.0f frames; %s"), Name,
					Sum / FMath::Max(1, Gpu->Num()), Gpu->Num(), (FPlatformTime::Seconds() - Start->Value) * 1000.0 / FMath::Max(1.0, Frames), Frames, *Scope->Describe());
			});
		};
		const FVector Edge = Centre + FVector(0.0f, 0.0f, 200.0f); // the round wall's top edge, its label above
		Hold(Centre);
		// Long first wait: shaders may still compile.
		After(8.0f, [Pawn, Hold, Centre]() { Pawn->Guns->UnlockWeapon(2); Hold(Centre); });
		Measure(8.3f, TEXT("hip"));
		After(9.5f, [Hold, Shot, Centre]() { Hold(Centre); Shot(TEXT("01_hip")); });
		After(10.0f, [Scope, Hold, Centre]() { Hold(Centre); Scope->SetHeld(true); });
		After(10.15f, [Shot]() { Shot(TEXT("02_raising")); });
		Measure(10.6f, TEXT("aimed"));
		After(11.9f, [Hold, Centre]() { Hold(Centre); });
		After(12.0f, [Shot]() { Shot(TEXT("03_aimed_wall_centre")); });
		After(12.4f, [Hold, Edge]() { Hold(Edge); });
		After(12.6f, [Shot]() { Shot(TEXT("04_aimed_wall_edge")); });
		After(13.0f, [Pawn, Hold, Centre]() { Hold(Centre); Pawn->Fire(); });
		After(13.05f, [Shot]() { Shot(TEXT("05_shot_0050")); });
		After(13.15f, [Shot]() { Shot(TEXT("06_shot_0150")); });
		After(13.6f, [Shot]() { Shot(TEXT("07_bolt_0600")); });
		After(14.4f, [Hold, Centre]() { Hold(Centre); });
		After(14.5f, [Shot]() { Shot(TEXT("08_settled_1500")); });
		After(15.0f, [Scope]() { Scope->SetHeld(false); });
		After(16.0f, [Hold, Shot, Centre]() { Hold(Centre); Shot(TEXT("09_hip_again")); });
		After(16.5f, [Scope]() { UE_LOG(LogMindKore, Log, TEXT("Scope shots: done - %s"), *Scope->Describe()); });
	}

	FAutoConsoleCommandWithWorld GScopeShots(
		TEXT("jam.Scope.Shots"),
		TEXT("Stills of the SV-98's scope at a living wall ~20 m away: hip, raising, aimed (centre, edge), a shot through the bolt cycle, hip again; GPU/frame time at the hip and aimed (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunScopeShots));
}
