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
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "InputMappingContext.h"
#include "JamHullTools.h"
#include "JamLivingWall.h"
#include "JamWeapons.h"
#include "JamZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MindKore.h"
#include "TimerManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarScopeTrace(TEXT("jam.Scope.Trace"), 0,
		TEXT("Log the scope's aim springs every tick (Scope trace: t, place, turn, alpha) - for plotting the curves."));
}

UJamScopeComponent::UJamScopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// X, Y, Z, pitch, yaw, roll. In: after a short reaction, Z first and quickest (up), Y, then X (in) - the arc; all a little
	// under-damped (low gravity: it floats 8-12% past the eye and settles in about 0.8 s). The turns are
	// slower and floatier still: the muzzle comes round late.
	AimIn = { { 1.8f, 0.58f, 0.08f }, { 2.0f, 0.62f, 0.04f }, { 2.3f, 0.55f, 0.02f }, { 1.5f, 0.45f, 0.0f }, { 1.5f, 0.45f, 0.0f }, { 1.4f, 0.42f, 0.0f } };
	// Out: no wait, quicker, nearly critical - it drops away rather than floats.
	AimOut = { { 2.8f, 0.75f, 0.0f }, { 2.6f, 0.75f, 0.0f }, { 2.8f, 0.7f, 0.0f }, { 2.0f, 0.5f, 0.0f }, { 2.0f, 0.5f, 0.0f }, { 2.0f, 0.5f, 0.0f } };
	LensMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/Materials/Weapons/M_Jam_ScopeLens.M_Jam_ScopeLens")));
	ReticleMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/Materials/Weapons/M_Jam_ScopeReticle.M_Jam_ScopeReticle")));
	SightDotMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/JamArt/Materials/Weapons/M_Jam_SightDot.M_Jam_SightDot")));
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
	if (Weapon != ScopedWeapon || !bMoving)
	{
		return;
	}
	// The springs' pose; at the eye it is exactly the scope's axis on the camera's, the eyepiece
	// EyeReliefCm ahead, with the rest pose's turn (none on the SV-98).
	Rest.SetTranslation(Place);
	Rest.SetRotation(FRotator(Turn.X, Turn.Y, Turn.Z).Quaternion() * Rest.GetRotation());
	KickScale = FMath::Lerp(1.0f, AimKickScale, Alpha);
}

void UJamScopeComponent::StepMotion(float DeltaTime, bool bAim)
{
	const FVector RestPlace = Weapons->Weapons[ScopedWeapon].ViewOffset.GetTranslation();
	const FVector AimPlace = FVector(EyeReliefCm, 0.0f, 0.0f) - EyepieceCm;
	if (bAim != bAimTarget)
	{
		bAimTarget = bAim;
		SinceSwitch = 0.0f;
	}
	SinceSwitch += DeltaTime;
	const TArray<FJamAimSpring>& Springs = bAimTarget ? AimIn : AimOut;
	if (Springs.Num() < 6)
	{
		return;
	}
	// Each place channel heads for its end once its delay has passed, from the other end until then.
	FVector Goal;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		const bool bGo = SinceSwitch >= Springs[Axis].Delay;
		Goal[Axis] = (bGo == bAimTarget) ? AimPlace[Axis] : RestPlace[Axis];
	}
	// x'' = w^2 (goal - x) - 2 zeta w x', semi-implicit Euler in steps of at most 4 ms (stable at the 3 Hz ends).
	const int32 Steps = FMath::Clamp(FMath::CeilToInt(DeltaTime / 0.004f), 1, 32);
	const float Dt = DeltaTime / Steps;
	for (int32 Step = 0; Step < Steps; ++Step)
	{
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const float W = 2.0f * PI * Springs[Axis].Hz;
			PlaceVelocity[Axis] += (W * W * (Goal[Axis] - Place[Axis]) - 2.0f * Springs[Axis].Damping * W * PlaceVelocity[Axis]) * Dt;
			Place[Axis] += PlaceVelocity[Axis] * Dt;
		}
		// The turns chase the drag of the motion: the muzzle dips while rising, points back across, cants.
		const FVector TurnGoal(-PitchLag * PlaceVelocity.Z, -YawLag * PlaceVelocity.Y, RollLag * PlaceVelocity.Y);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FJamAimSpring& Spring = Springs[3 + Axis];
			const float W = 2.0f * PI * Spring.Hz;
			TurnVelocity[Axis] += (W * W * (TurnGoal[Axis] - Turn[Axis]) - 2.0f * Spring.Damping * W * TurnVelocity[Axis]) * Dt;
			Turn[Axis] += TurnVelocity[Axis] * Dt;
		}
	}
	// Settled: snap exactly onto the end (the eyepiece aligned to the micron), and at the hip stop.
	const FVector End = bAimTarget ? AimPlace : RestPlace;
	if (SinceSwitch > 0.2f && (Place - End).Size() < 0.01f && PlaceVelocity.Size() < 0.05f && Turn.Size() < 0.01f && TurnVelocity.Size() < 0.05f)
	{
		Place = End;
		PlaceVelocity = TurnVelocity = Turn = FVector::ZeroVector;
		bMoving = bAimTarget;
	}
	const FVector Way = AimPlace - RestPlace;
	Alpha = FMath::Clamp(FVector::DotProduct(Place - RestPlace, Way) / Way.SizeSquared(), 0.0f, 1.0f);
	if (!bMoving && !bAimTarget)
	{
		Alpha = 0.0f;
	}
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

	// The laser layer, a hair in front of the glass (toward the eye, -X).
	Reticle = NewObject<UStaticMeshComponent>(GetOwner(), TEXT("ScopeReticle"));
	Reticle->SetStaticMesh(Plane);
	Reticle->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Reticle->SetCastShadow(false);
	Reticle->SetOnlyOwnerSee(true);
	Reticle->SetupAttachment(Gun);
	Reticle->SetRelativeLocationAndRotation(EyepieceCm - FVector(0.02f, 0.0f, 0.0f), FRotator(90.0f, 0.0f, 0.0f));
	Reticle->SetRelativeScale3D(FVector(LensRadiusCm * 2.0f / 100.0f));
	if (UMaterialInterface* Material = ReticleMaterial.LoadSynchronous())
	{
		ReticleMID = UMaterialInstanceDynamic::Create(Material, this);
		ReticleMID->SetTextureParameterValue(TEXT("Scene"), Target);
		Reticle->SetMaterial(0, ReticleMID);
	}
	Reticle->RegisterComponent();

	static UStaticMesh* Sphere = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	UMaterialInterface* DotMaterial = SightDotMaterial.LoadSynchronous();
	for (const FVector& At : SightDotsCm)
	{
		UStaticMeshComponent* Dot = NewObject<UStaticMeshComponent>(GetOwner());
		Dot->SetStaticMesh(Sphere);
		Dot->SetMaterial(0, DotMaterial);
		Dot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Dot->SetCastShadow(false);
		Dot->SetOnlyOwnerSee(true);
		Dot->SetupAttachment(Weapons->ActionInView);
		Dot->SetRelativeLocation(At);
		Dot->SetRelativeScale3D(FVector(SightDotCm / 100.0f));
		Dot->SetVisibility(false);
		Dot->RegisterComponent();
		SightDots.Add(Dot);
	}

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
	// The body's velocity change this frame, in the camera's frame: the gun's inertia sways it the other way.
	const FVector BodyVelocity = GetOwner()->GetVelocity();
	const FVector BodyChange = Camera->GetComponentQuat().UnrotateVector(BodyVelocity - LastBodyVelocity);
	LastBodyVelocity = BodyVelocity;
	if (!bScopeInView)
	{
		// Another gun in hand: the rifle starts from its rest pose when it comes back.
		bMoving = bAimTarget = false;
		Alpha = 0.0f;
	}
	else
	{
		if (!bMoving && (bWants || BodyChange.Size() * SwayPerBodyVelocity > 0.5f))
		{
			bMoving = true;
			Place = Weapons->Weapons[ScopedWeapon].ViewOffset.GetTranslation();
			PlaceVelocity = Turn = TurnVelocity = FVector::ZeroVector;
		}
		if (bMoving)
		{
			PlaceVelocity -= (BodyChange * SwayPerBodyVelocity).GetClampedToMaxSize(MaxSwayCmPerSec);
			StepMotion(DeltaTime, bWants);
		}
	}
	if (CVarScopeTrace.GetValueOnGameThread() && bMoving)
	{
		UE_LOG(LogMindKore, Log, TEXT("Scope trace: t=%.4f aim=%d place=%.3f,%.3f,%.3f turn=%.3f,%.3f,%.3f alpha=%.4f"), GetWorld()->GetTimeSeconds(),
			bAimTarget ? 1 : 0, Place.X, Place.Y, Place.Z, Turn.X, Turn.Y, Turn.Z, Alpha);
	}
	Lens->SetVisibility(bScopeInView);
	Reticle->SetVisibility(bScopeInView);
	// The sight dots ride on the moving part of their gun (the USP's slide) while that part is in view.
	const UStaticMesh* Part = Weapons->ActionInView ? Weapons->ActionInView->GetStaticMesh() : nullptr;
	const bool bDotsInView = Part && Weapons->Weapons.IsValidIndex(SightDotWeapon) && Part == Weapons->Weapons[SightDotWeapon].ActionMesh;
	for (UStaticMeshComponent* Dot : SightDots)
	{
		Dot->SetVisibility(bDotsInView);
	}
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
		if (ReticleMID)
		{
			ReticleMID->SetVectorParameterValue(TEXT("Eye"), FLinearColor(Eye.Y * ShadowPerCm, -Eye.Z * ShadowPerCm, -Eye.X - EyeReliefCm, 0.0f));
		}
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
	 * cycle, +1.5 s), at the hip again, then the pistol with its sight dots (at rest
	 * and the slide held back). Every still is logged first with the
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
		// The reticle close: the same frame at twice the size.
		After(12.2f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 2 filename=JamScope_03b_aimed_x2")); });
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
		// The pistol's sight dots: at rest, then the slide held back (jam.Weapons.PoseAt) - they ride on it.
		IConsoleVariable* PoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Weapons.PoseAt"));
		After(16.5f, [Pawn, Hold, Centre]() { Pawn->Guns->UnlockWeapon(0); Hold(Centre); });
		After(17.5f, [Hold, Shot, Centre]() { Hold(Centre); Shot(TEXT("10_pistol_sights")); });
		After(17.7f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 2 filename=JamScope_10b_pistol_x2")); });
		After(18.0f, [PoseAt]() { if (PoseAt) { PoseAt->Set(0.035f, ECVF_SetByCode); } });
		After(18.3f, [Hold, Shot, Centre]() { Hold(Centre); Shot(TEXT("11_pistol_slide_back")); });
		After(18.6f, [PoseAt]() { if (PoseAt) { PoseAt->Set(-1.0f, ECVF_SetByCode); } });
		// Straight down the sight line: the pistol posed so the line through the rear dots' middle and the
		// front dot runs along the view, the rear sight 20 cm ahead - the front post must sit centred in the
		// notch. The rest pose comes back after the still.
		TSharedRef<FTransform> Rest = MakeShared<FTransform>();
		After(19.0f, [Pawn, Scope, Hold, Centre, Rest]()
		{
			Hold(Centre);
			FJamWeapon& Pistol = Pawn->Guns->Weapons[Scope->SightDotWeapon];
			*Rest = Pistol.ViewOffset;
			const TArray<FVector>& Dots = Scope->SightDotsCm;
			const FVector Rear = (Dots[0] + Dots[1]) * 0.5f;
			// Turned round first (the USP's muzzle is its -X), then the small tilt onto the line - one
			// rotation straight from -X to +X would pick an arbitrary axis and could roll it upside down.
			const FQuat Round = FRotator(0.0f, 180.0f, 0.0f).Quaternion();
			const FQuat Turn = FQuat::FindBetweenNormals(Round.RotateVector((Dots[2] - Rear).GetSafeNormal()), FVector::ForwardVector) * Round;
			Pistol.ViewOffset = FTransform(Turn, FVector(20.0f, 0.0f, 0.0f) - Turn.RotateVector(Rear));
			UE_LOG(LogMindKore, Log, TEXT("Scope shots: sight line - rear dots %s / %s, front %s, notch centre y -1.431, front post centre y -1.453 (SM_Jam_USP_Slide)"),
				*Dots[0].ToCompactString(), *Dots[1].ToCompactString(), *Dots[2].ToCompactString());
		});
		// The HUD off for it: its centre dot sits exactly on the front sight dot.
		After(19.3f, [Controller]() { if (Controller->MyHUD) { Controller->MyHUD->bShowHUD = false; } });
		After(19.4f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 2 filename=JamScope_12_pistol_sightline_x2")); });
		After(19.7f, [Controller]() { if (Controller->MyHUD) { Controller->MyHUD->bShowHUD = true; } });
		After(19.8f, [Pawn, Scope, Rest]() { Pawn->Guns->Weapons[Scope->SightDotWeapon].ViewOffset = *Rest; });
		After(20.2f, [Scope]() { UE_LOG(LogMindKore, Log, TEXT("Scope shots: done - %s"), *Scope->Describe()); });
	}

	FAutoConsoleCommandWithWorld GScopeShots(
		TEXT("jam.Scope.Shots"),
		TEXT("Stills of the SV-98's scope at a living wall ~20 m away: hip, raising, aimed (centre, edge), a shot through the bolt cycle, hip again, the pistol sight dots; GPU/frame time at the hip and aimed (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunScopeShots));

	/**
	 * jam.Scope.AimFrames: the raise and the lowering frame by frame (HighResShot; needs a real RHI). The
	 * zero-G pawn looks at the living wall nearest to 20 m with the SV-98; time runs at a tenth so the
	 * stills land every 33 ms of game time: 25 through the raise (0..0.8 s), 16 through the lowering
	 * (JamAim_in_<nn>_<ms>.png, JamAim_out_<nn>_<ms>.png). jam.Scope.Trace logs the springs every tick
	 * meanwhile, for the curves.
	 */
	void RunScopeAimFrames(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamZeroGPawn* Pawn = Controller ? Cast<AJamZeroGPawn>(Controller->GetPawn()) : nullptr;
		UJamScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UJamScopeComponent>() : nullptr;
		IConsoleVariable* Trace = IConsoleManager::Get().FindConsoleVariable(TEXT("jam.Scope.Trace"));
		if (!Scope || !Trace)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Scope aim frames: needs the AJamZeroGPawn with its scope"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f);
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			const float Error = FMath::Abs(FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation()) - 2000.0f);
			if (Error < Best)
			{
				Best = Error;
				Target = It->GetActorLocation();
			}
		}
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		const auto Still = [Controller, Scope](const TCHAR* Phase, int32 Index, float Seconds)
		{
			UE_LOG(LogMindKore, Log, TEXT("Scope aim frames: %s %02d at %.3f s - %s"), Phase, Index, Seconds, *Scope->Describe());
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=JamAim_%s_%02d_%03d"), Phase, Index, FMath::RoundToInt(Seconds * 1000.0f)));
		};
		// Long first wait: shaders may still compile. Timers count game time, slowed from 9 s on.
		After(8.0f, [Pawn, Controller, Target]()
		{
			Pawn->Guns->UnlockWeapon(2);
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		});
		After(9.0f, [World, Trace]() { UGameplayStatics::SetGlobalTimeDilation(World, 0.1f); Trace->Set(1, ECVF_SetByCode); });
		const float In = 9.2f;
		const float Out = In + 1.5f;
		After(In, [Scope]() { Scope->SetHeld(true); });
		for (int32 Index = 0; Index < 25; ++Index)
		{
			After(In + Index / 30.0f + 0.001f, [Still, Index]() { Still(TEXT("in"), Index, Index / 30.0f); });
		}
		After(Out, [Scope]() { Scope->SetHeld(false); });
		for (int32 Index = 0; Index < 16; ++Index)
		{
			After(Out + Index / 30.0f + 0.001f, [Still, Index]() { Still(TEXT("out"), Index, Index / 30.0f); });
		}
		After(Out + 1.0f, [World, Trace, Scope]()
		{
			Trace->Set(0, ECVF_SetByCode);
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
			UE_LOG(LogMindKore, Log, TEXT("Scope aim frames: done - %s"), *Scope->Describe());
		});
	}

	FAutoConsoleCommandWithWorld GScopeAimFrames(
		TEXT("jam.Scope.AimFrames"),
		TEXT("The SV-98's raise and lowering every 33 ms of game time (time at a tenth), with jam.Scope.Trace on (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunScopeAimFrames));
}
