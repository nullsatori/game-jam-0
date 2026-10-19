#include "JamShip.h"

#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/BoxComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Components/LightComponent.h"
#include "Engine/DirectionalLight.h"
#include "EngineUtils.h"
#include "HAL/IConsoleManager.h"
#include "JamDockZone.h"
#include "JamHullTools.h"
#include "JamGravity.h"
#include "JamLivingWall.h"
#include "JamOxygen.h"
#include "JamPickups.h"
#include "JamShipParts.h"
#include "JamWeapons.h"
#include "JamWeldProjector.h"
#include "JamZeroGPawn.h"
#include "MKDoor.h"
#include "MKKeyring.h"
#include "MKVitals.h"
#include "MindKore.h"
#include "TimerManager.h"
#include "GameFramework/PlayerStart.h"
#include "GameFramework/Volume.h"
#if WITH_EDITOR
#include "Editor.h"
#include "ActorFactories/ActorFactory.h"
#include "Builders/CylinderBuilder.h"
#include "Components/BrushComponent.h"
#include "Engine/TriggerVolume.h"
#include "ScopedTransaction.h"
#include "BSPOps.h"
#include "Model.h"
#endif
#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#endif

#define LOCTEXT_NAMESPACE "JamShip"

// --- controller ----------------------------------------------------------------------

void AJamShipController::UpdateRotation(float DeltaTime)
{
	// The pawn's gravity; outside every zone the frame keeps the last one.
	const AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(GetPawn());
	const bool bInGravity = Floater && Floater->IsInGravity();
	// The view kicks since the last frame (AddViewKick): spent here, in this frame's write.
	const FRotator Kick(PendingViewKick.Pitch, PendingViewKick.Yaw, 0.0f);
	PendingViewKick = FRotator::ZeroRotator;
	// Only the view turns, never the physics body. Teleporting the sphere's
	// rotation every frame against its rotation lock left it spinning at
	// 10^5..10^6 deg/s, and on touchdown friction turned that spin into a
	// sideways kick (7.5 m/s in jam.Ship.Land); it also turned the grab's
	// body-space hold target away from the view.
	if (Floater && !bInGravity && Floater->bFreeBodyRotation)
	{
		// Compose in body space: after a flip, yaw still turns around the
		// player's own up axis. Euler pitch/yaw accumulation cannot do this.
		// The kick after the mouse, in the view's own frame: a rolled view kicks toward its own up.
		const FQuat View = (GetControlRotation().Quaternion() * RotationInput.Quaternion() * Kick.Quaternion()).GetNormalized();
		LastWrittenRotation = View.Rotator();
		SetControlRotation(LastWrittenRotation);
		bWasFreeBodyRotation = true;
		return;
	}
	if (bWasFreeBodyRotation)
	{
		// Leaving free rotation (landing, or entering the cabin): build the
		// upright frame for the target gravity at once, keeping where the view
		// looks, and fade the leftover roll out as a world-space offset. Easing
		// the gravity itself from the free pose meant turning through up to 180
		// degrees with FindBetweenNormals, whose axis is arbitrary near opposite
		// vectors: an upside-down landing nodded the view 60 degrees in pitch on
		// its way upright (jam.Ship.Land 0 180).
		const FQuat ExitView = GetControlRotation().Quaternion();
		const FVector Gravity = bInGravity ? Floater->GetGravityDirection()
			: (SmoothedGravity.IsNearlyZero() ? FVector::DownVector : SmoothedGravity);
		const FVector Up = -Gravity;
		const FVector Forward = ExitView.GetForwardVector();
		FVector Ahead = FVector::VectorPlaneProject(Forward, Up);
		if (!Ahead.Normalize(0.05f))
		{
			// Looking straight along gravity: the view's own up (or down) gives the heading.
			Ahead = FVector::VectorPlaneProject(FVector::DotProduct(Forward, Up) < 0.0f ? ExitView.GetUpVector() : -ExitView.GetUpVector(), Up).GetSafeNormal();
		}
		GravityFrame = FRotationMatrix::MakeFromXZ(Ahead, Up).ToQuat();
		const float Pitch = FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(FVector::DotProduct(Forward, Up), -1.0f, 1.0f)));
		RelativeView = FRotator(FMath::Clamp(Pitch, -89.0f, 89.0f), 0.0f, 0.0f);
		SmoothedGravity = Gravity;
		RecoveryOffset = (ExitView * (GravityFrame * RelativeView.Quaternion()).Inverse()).GetNormalized();
		if (RecoveryOffset.W < 0.0f)
		{
			RecoveryOffset = -RecoveryOffset; // the short way round: GetAngle stays within 180 degrees
		}
		LastWrittenRotation = GetControlRotation();
		bWasFreeBodyRotation = false;
	}
	if (SmoothedGravity.IsNearlyZero())
	{
		SmoothedGravity = bInGravity ? Floater->GetGravityDirection() : FVector::DownVector;
		GravityFrame = FQuat::FindBetweenNormals(FVector::DownVector, SmoothedGravity);
	}
	else if (bInGravity)
	{
		// The view's "down" eases toward the pawn's gravity instead of snapping:
		// landing on the planetoid turns the world under you over ~0.75 s.
		const FVector TargetGravity = Floater->GetGravityDirection();
		const float Alpha = 1.0f - FMath::Exp(-GravityAlignRate * DeltaTime);
		const FQuat Turn = FQuat::Slerp(FQuat::Identity, FQuat::FindBetweenNormals(SmoothedGravity, TargetGravity), Alpha);
		const FVector Next = Turn.RotateVector(SmoothedGravity).GetSafeNormal(UE_SMALL_NUMBER, TargetGravity);
		// The view's frame is carried along with gravity (parallel transport),
		// all in quaternions: walking around a sphere keeps the view's pitch and
		// yaw relative to the local up, with no gimbal at the "equator".
		GravityFrame = FQuat::FindBetweenNormals(SmoothedGravity, Next) * GravityFrame;
		GravityFrame.Normalize();
		SmoothedGravity = Next;
	}

	// Someone else set the view (possession, a teleport, a probe): take it as the new one.
	const FRotator Control = GetControlRotation();
	if (!Control.Equals(LastWrittenRotation, 0.01f))
	{
		const FRotator Local = (GravityFrame.Inverse() * Control.Quaternion()).Rotator();
		RelativeView = FRotator(Local.Pitch, Local.Yaw, 0.0f);
	}

	bWasFreeBodyRotation = false;
	FRotator View = RelativeView;
	// The kick turns the upright view like the mouse does; the roll left over from free rotation fades on its own below.
	FRotator DeltaRotation(RotationInput + Kick);
	if (PlayerCameraManager)
	{
		PlayerCameraManager->ProcessViewRotation(DeltaTime, View, DeltaRotation);
	}
	else
	{
		View += DeltaRotation;
		View.Pitch = FMath::Clamp(FRotator::NormalizeAxis(View.Pitch), -89.0f, 89.0f);
	}
	View.Roll = 0.0f;
	RelativeView = View;
	// The roll left over from free rotation fades at the gravity easing rate,
	// but never faster than RecoveryMaxDegPerSec: a 180-degree flip would
	// otherwise start at 720 deg/s.
	if (const float Angle = RecoveryOffset.GetAngle(); Angle > UE_KINDA_SMALL_NUMBER)
	{
		const float Step = FMath::Min(Angle * (1.0f - FMath::Exp(-GravityAlignRate * DeltaTime)),
			FMath::DegreesToRadians(RecoveryMaxDegPerSec) * DeltaTime);
		RecoveryOffset = FQuat::Slerp(RecoveryOffset, FQuat::Identity, Step / Angle).GetNormalized();
	}
	LastWrittenRotation = (RecoveryOffset * GravityFrame * View.Quaternion()).Rotator();
	SetControlRotation(LastWrittenRotation);
	if (APawn* const ViewPawn = GetPawnOrSpectator())
	{
		ViewPawn->FaceRotation(LastWrittenRotation, DeltaTime);
	}
}

// --- gravity zone ----------------------------------------------------------------------

AJamGravityZone::AJamGravityZone()
{
	Zone = CreateDefaultSubobject<UBoxComponent>(TEXT("Zone"));
	SetRootComponent(Zone);
	Zone->SetBoxExtent(FVector(400.0f, 250.0f, 150.0f));
	Zone->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Zone->SetCanEverAffectNavigation(false);
}

void AJamGravityZone::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	Zone->SetVisibility(!IsValid(BoundaryVolume));
}

#if WITH_EDITOR
void AJamGravityZone::CreateEditableBoundary()
{
	if (!GEditor || !GetWorld() || GetWorld()->IsGameWorld())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("CreateBoundary", "Create cabin boundary"));
	if (!IsValid(BoundaryVolume))
	{
		UActorFactory* Factory = GEditor->FindActorFactoryForActorClass(ATriggerVolume::StaticClass());
		AVolume* Volume = Factory ? Cast<AVolume>(Factory->CreateActor(nullptr, GetLevel(), Zone->GetComponentTransform())) : nullptr;
		if (!Volume)
		{
			return;
		}
		Volume->SetActorLabel(GetActorLabel() + TEXT("_Boundary"));
		const FVector Extent = Zone->GetUnscaledBoxExtent().ComponentMax(FVector(1));
		if (bEllipticalCylinder)
		{
			UCylinderBuilder* Builder = NewObject<UCylinderBuilder>(Volume, NAME_None, RF_Transactional);
			Volume->BrushBuilder = Builder;
			Builder->Sides = 32;
			Builder->OuterRadius = 100;
			Builder->Z = 200;
			Builder->Hollow = false;
			Builder->Build(GetWorld(), Volume);
		}
		Volume->SetActorTransform(Zone->GetComponentTransform());
		Volume->SetActorScale3D(Zone->GetComponentScale() * Extent / 100.0f);
		Volume->MarkPackageDirty();
		Modify();
		BoundaryVolume = Volume;
		Zone->SetVisibility(false);
	}
	RebuildBoundaryCollision();
	GEditor->SelectNone(false, true);
	GEditor->SelectActor(BoundaryVolume, true, true);
	GEditor->RedrawLevelEditingViewports();
}

void AJamGravityZone::RebuildBoundaryCollision()
{
	if (!IsValid(BoundaryVolume) || !BoundaryVolume->Brush || GetWorld()->IsGameWorld())
	{
		return;
	}
	const FScopedTransaction Transaction(LOCTEXT("RebuildBoundary", "Rebuild cabin boundary collision"));
	BoundaryVolume->Modify();
	BoundaryVolume->Brush->Modify();
	// Inward-wound brush faces produce no collision hulls; preserve the shape and orient them outward.
	double SignedVolume = 0.0;
	for (const FPoly& Poly : BoundaryVolume->Brush->Polys->Element)
	{
		for (int32 I = 1; I + 1 < Poly.Vertices.Num(); ++I)
		{
			SignedVolume += FVector::DotProduct(FVector(Poly.Vertices[0]), FVector::CrossProduct(FVector(Poly.Vertices[I]), FVector(Poly.Vertices[I + 1])));
		}
	}
	for (FPoly& Poly : BoundaryVolume->Brush->Polys->Element)
	{
		Poly.PolyFlags &= ~PF_NotSolid;
		if (Poly.Vertices.Num() >= 3)
		{
			Poly.Base = Poly.Vertices[0];
			Poly.Normal = FVector3f::ZeroVector;
			Poly.CalcNormal(true);
			if (SignedVolume < 0.0)
			{
				Poly.Reverse();
			}
		}
	}
	// Use the same full geometry/collision rebuild as UE's brush editing tools.
	FBSPOps::csgPrepMovingBrush(BoundaryVolume);
	BoundaryVolume->MarkPackageDirty();
}
#endif

bool AJamGravityZone::Contains(const FVector& Location, float InsetCm) const
{
	if (IsValid(BoundaryVolume))
	{
		return BoundaryVolume->EncompassesPoint(Location, FMath::Max(0.0f, -InsetCm));
	}
	if (SphereRadiusCm > 0.0f)
	{
		return FVector::Dist(Location, GetActorLocation()) <= SphereRadiusCm - InsetCm;
	}
	const FVector Local = Zone->GetComponentTransform().InverseTransformPositionNoScale(Location);
	const FVector Extent = Zone->GetScaledBoxExtent() - FVector(InsetCm);
	if (bEllipticalCylinder)
	{
		return Extent.X > 0.0f && Extent.Y > 0.0f && Extent.Z >= 0.0f
			&& FMath::Abs(Local.Z) <= Extent.Z
			&& FMath::Square(Local.X / Extent.X) + FMath::Square(Local.Y / Extent.Y) <= 1.0f;
	}
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y && FMath::Abs(Local.Z) <= Extent.Z;
}

FVector AJamGravityZone::GravityAt(const FVector& Location) const
{
	return bRadial ? (GetActorLocation() - Location).GetSafeNormal(UE_SMALL_NUMBER, FVector::DownVector) : FVector::DownVector;
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJamOvalZoneTest, "Jam.Gravity.OvalZone",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJamOvalZoneTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	AJamGravityZone* Oval = World->SpawnActor<AJamGravityZone>();
	Oval->Zone->SetBoxExtent(FVector(300, 200, 150));
	Oval->bEllipticalCylinder = true;
	TestTrue(TEXT("Centre has air"), Oval->Contains(FVector::ZeroVector, 0));
	TestTrue(TEXT("Floor edge stays inside"), Oval->Contains(FVector(300, 0, -150), 0));
	TestFalse(TEXT("Box corner is outside oval"), Oval->Contains(FVector(290, 190, 0), 0));
	TestFalse(TEXT("Above ceiling is outside"), Oval->Contains(FVector(0, 0, 151), 0));
	TestFalse(TEXT("Inset shrinks boundary"), Oval->Contains(FVector(300, 0, 0), 1));
	Oval->SetActorLocationAndRotation(FVector(1000, 500, 50), FRotator(0, 90, 0));
	Oval->SetActorScale3D(FVector(2, 1, 1));
	TestTrue(TEXT("Rotated scaled long axis"), Oval->Contains(FVector(1000, 1050, 50), 0));
	TestFalse(TEXT("Rotated short axis"), Oval->Contains(FVector(1210, 500, 50), 0));
	Oval->bEllipticalCylinder = false;
	TestTrue(TEXT("Existing box unchanged"), Oval->Contains(Oval->GetActorTransform().TransformPosition(FVector(290, 190, 0)), 0));
	Oval->SphereRadiusCm = 100;
	TestFalse(TEXT("Sphere takes precedence"), Oval->Contains(FVector(1000, 650, 50), 0));
	World->DestroyWorld(false);
	return true;
}
#endif

// --- game mode ---------------------------------------------------------------------------

AJamShipGameMode::AJamShipGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	// The pawn is the zero-G mode's (AJamZeroGPawn): it walks in the zones.
	HUDClass = AJamShipHUD::StaticClass();
	PlayerControllerClass = AJamShipController::StaticClass();
}

void AJamShipGameMode::StartPlay()
{
	Super::StartPlay();
	// What the map holds, so a hand-edited map keeps «Груз n/N» right.
	CargoTotal = 0;
	for (TActorIterator<AJamCargo> It(GetWorld()); It; ++It)
	{
		++CargoTotal;
	}
	UE_LOG(LogMindKore, Log, TEXT("Ship: %d cargo in %s"), CargoTotal, *GetWorld()->GetMapName());
}

void AJamShipGameMode::SetPlayerDefaults(APawn* PlayerPawn)
{
	Super::SetPlayerDefaults(PlayerPawn);
	AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(PlayerPawn);
	if (!Pawn)
	{
		return;
	}
	Pawn->Vitals->OnDepleted.RemoveAll(this);
	Pawn->Vitals->OnDepleted.AddUObject(this, &AJamShipGameMode::HandleDeath, TWeakObjectPtr<AJamZeroGPawn>(Pawn));
}

void AJamShipGameMode::HandleDeath(TWeakObjectPtr<AJamZeroGPawn> WeakPawn)
{
	AJamZeroGPawn* Pawn = WeakPawn.Get();
	if (!Pawn || bEnded)
	{
		return;
	}
	UE_LOG(LogMindKore, Warning, TEXT("Ship: %s died (O2 %.0f s); waking in the cabin in %.0f s"), *Pawn->GetName(), Pawn->Oxygen->Seconds, RespawnDelaySeconds);
	// The body goes limp at once: no torch, hook, hold or aim left running on it.
	Pawn->LetGoOfEverything();
	if (APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController()))
	{
		Pawn->DisableInput(PlayerController);
	}
	FTimerHandle Wake;
	GetWorldTimerManager().SetTimer(Wake, FTimerDelegate::CreateWeakLambda(this, [this, WeakPawn]()
	{
		if (AJamZeroGPawn* Dead = WeakPawn.Get(); Dead && !bEnded)
		{
			RespawnInCabin(Dead);
		}
	}), FMath::Max(RespawnDelaySeconds, 0.01f), false);
}

void AJamShipGameMode::Toast(const FText& Text)
{
	ToastText = Text;
	ToastUntil = GetWorld()->GetTimeSeconds() + ToastSeconds;
}

FText AJamShipGameMode::GetToast() const
{
	return GetWorld()->GetTimeSeconds() < ToastUntil ? ToastText : FText::GetEmpty();
}

const AJamGravityZone* AJamShipGameMode::ZoneAt(const FVector& Location, float InsetCm, bool bWalkOnly) const
{
	for (TActorIterator<AJamGravityZone> It(GetWorld()); It; ++It)
	{
		if ((It->bWalk || !bWalkOnly) && It->Contains(Location, InsetCm))
		{
			return *It;
		}
	}
	return nullptr;
}

const AJamGravityZone* AJamShipGameMode::ZoneOf(const APawn* Pawn) const
{
	return Pawn ? ZoneAt(Pawn->GetActorLocation(), 0.0f, /*bWalkOnly=*/false) : nullptr;
}

void AJamShipGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	APlayerController* PlayerController = GetWorld()->GetFirstPlayerController();
	APawn* Pawn = PlayerController ? PlayerController->GetPawn() : nullptr;
	if (!Pawn || bEnded)
	{
		return;
	}
	const FVector Location = Pawn->GetActorLocation();
	// Air: any breathable zone (the planetoid's air shell is wider than its walking shell).
	const AJamGravityZone* Air = ZoneAt(Location, 0.0f, /*bWalkOnly=*/false);
	if (UJamOxygenComponent* Oxygen = Pawn->FindComponentByClass<UJamOxygenComponent>())
	{
		Oxygen->SetBreathable(Air && Air->bBreathable, Air ? Air->RefillPerSecond : 0.0f);
	}
	// Gravity: the walking zone's, or none (zero-G).
	if (AJamZeroGPawn* Floater = Cast<AJamZeroGPawn>(Pawn))
	{
		const AJamGravityZone* Zone = ZoneAt(Location, 0.0f);
		Floater->SetGravity(Zone ? Zone->GravityAt(Location) : FVector::ZeroVector, Zone ? Zone->GravityCm : 0.0f, Zone && Zone->bRadial);
	}
}

void AJamShipGameMode::EndGame()
{
	if (bEnded)
	{
		return;
	}
	bEnded = true;
	// TODO (boss): the planetoid wakes here and grabs the ship; for now the run simply ends.
	OnLaunchAttempt.Broadcast();
	UE_LOG(LogMindKore, Log, TEXT("Ship: launch - Улетел, cargo %d/%d; restarting in %.0f s"), Cargo, CargoTotal, EndRestartSeconds);
	Toast(FText::Format(LOCTEXT("Flew", "Улетел! Груз {0}/{1}"), Cargo, CargoTotal));
	ToastUntil = GetWorld()->GetTimeSeconds() + EndRestartSeconds + 1.0;
	if (APlayerController* PlayerController = GetWorld()->GetFirstPlayerController())
	{
		FTimerHandle Restart;
		GetWorldTimerManager().SetTimer(Restart, FTimerDelegate::CreateWeakLambda(PlayerController, [PlayerController]()
		{
			PlayerController->RestartLevel();
		}), FMath::Max(EndRestartSeconds, 0.01f), false);
	}
}


void AJamShipGameMode::RespawnInCabin(AJamZeroGPawn* Pawn)
{
	if (!Pawn)
	{
		return;
	}
	// The same pawn, so whatever it carries stays by construction; only where it is and its health and air change.
	const APlayerStart* Start = nullptr;
	const APlayerStart* Any = nullptr;
	for (TActorIterator<APlayerStart> It(GetWorld()); It && !Start; ++It)
	{
		Any = Any ? Any : *It;
		Start = It->ActorHasTag(TEXT("Cabin")) ? *It : nullptr;
	}
	if (!Start)
	{
		UE_LOG(LogMindKore, Warning, TEXT("Ship: no PlayerStart tagged Cabin in %s; waking at %s"), *GetWorld()->GetMapName(), *GetNameSafe(Any));
		Start = Any;
	}
	const FTransform At = Start ? Start->GetActorTransform() : Pawn->GetActorTransform();
	Pawn->ReviveAt(At.GetLocation());
	if (APlayerController* PlayerController = Cast<APlayerController>(Pawn->GetController()))
	{
		PlayerController->SetControlRotation(FRotator(0.0f, At.Rotator().Yaw, 0.0f));
		Pawn->EnableInput(PlayerController);
	}
	UE_LOG(LogMindKore, Log, TEXT("Ship: %s woke at %s (%s), guns %s"), *Pawn->GetName(), *Pawn->GetActorLocation().ToCompactString(),
		*GetNameSafe(Start), *Pawn->Guns->Describe());
	Toast(LOCTEXT("Respawn", "Очнулся в каюте. Швы и уборка на месте"));
}

// --- HUD ------------------------------------------------------------------------------------

void AJamShipHUD::DrawHUD()
{
	Super::DrawHUD(); // the zero-G HUD: reticle, action, O2, HP, speed, the gun
	if (!Canvas)
	{
		return;
	}
	UFont* Font = GEngine->GetMediumFont();
	const APawn* Pawn = GetOwningPawn();
	const AJamShipGameMode* Mode = GetWorld()->GetAuthGameMode<AJamShipGameMode>();

	// The three lamps, mirrored; cargo; the air here.
	float Y = 24.0f;
	for (TActorIterator<AJamShipPanel> Panel(GetWorld()); Panel; ++Panel)
	{
		const FLinearColor Red(1.0f, 0.35f, 0.3f);
		const FLinearColor Green(0.4f, 1.0f, 0.5f);
		DrawText(FString::Printf(TEXT("Корпус: %d/%d"), Panel->HullDone(), Panel->HullSockets.Num()), Panel->IsHullDone() ? Green : Red, 24.0f, Y, Font);
		DrawText(FString::Printf(TEXT("Топливо: %d/%d"), Panel->FuelDone(), Panel->FuelNeeded()), Panel->IsFuelDone() ? Green : Red, 24.0f, Y + 20.0f, Font);
		DrawText(FString::Printf(TEXT("Навигация: %d/%d"), Panel->NavDone(), Panel->NavNeeded()), Panel->IsNavDone() ? Green : Red, 24.0f, Y + 40.0f, Font);
		Y += 60.0f;
		break;
	}
	if (Mode)
	{
		DrawText(FString::Printf(TEXT("Груз %d/%d"), Mode->Cargo, Mode->CargoTotal), FLinearColor(1.0f, 0.85f, 0.4f), 24.0f, Y, Font);
		if (const AJamGravityZone* Zone = Mode->ZoneOf(Pawn); Zone && !Zone->HudText.IsEmpty())
		{
			DrawText(Zone->HudText.ToString(), FLinearColor(1.0f, 0.75f, 0.55f), 24.0f, Y + 20.0f, Font);
		}
		// Hull repair: the tool in hand, the breach nearby, holding on.
		if (const AJamZeroGPawn* Player = Cast<AJamZeroGPawn>(Pawn))
		{
			const FLinearColor Tool(0.75f, 0.9f, 1.0f);
			float Line = Canvas->ClipY - 140.0f;
			const UJamHullToolsComponent* Tools = Player->HullTools;
			if (Tools->GetTool() == EJamHullTool::Strap)
			{
				DrawText(FString::Printf(TEXT("[%d] Стяжка: линий %d/%d   ЛКМ — зацепить, колесо — подмотка, R — срезать"), Player->GetToolSlot(EJamHullTool::Strap) + 1,
					Tools->GetLines().Num(), Tools->MaxLines), Tool, 24.0f, Line, Font);
				Line -= 20.0f;
			}
			else if (Tools->GetTool() == EJamHullTool::Welder)
			{
				DrawText(FString::Printf(TEXT("[%d] Сварка: %s   ЛКМ — горелка, R — варить/резать   %s"), Player->GetToolSlot(EJamHullTool::Welder) + 1,
					Tools->IsCutting() ? TEXT("режу") : TEXT("варю"),
					*Tools->GetWeldStatus().ToString()), Tool, 24.0f, Line, Font);
				Line -= 20.0f;
			}
			if (Player->IsClinging())
			{
				DrawText(TEXT("Держусь (ПКМ — отпустить; WASD, Space/Ctrl — подтянуться, раскачаться)"), Tool, 24.0f, Line, Font);
				Line -= 20.0f;
			}
			const AJamPlateSocket* Near = nullptr;
			double Best = 500.0 * 500.0;
			for (TActorIterator<AJamPlateSocket> Socket(GetWorld()); Socket; ++Socket)
			{
				const double Distance = FVector::DistSquared(Socket->GetActorLocation(), Player->GetActorLocation());
				if (Distance < Best && (Socket->GetPlate() || Socket->GetSeamFraction() > 0.0f))
				{
					Best = Distance;
					Near = *Socket;
				}
			}
			if (Near)
			{
				const bool bSealed = Near->bFilled;
				DrawText(FString::Printf(TEXT("Пробоина: перекрытие %.0f%%  шов %.0f%%%s"), Near->GetCoverage() * 100.0f, Near->GetSeamFraction() * 100.0f,
					bSealed ? TEXT("  — заварена") : Near->IsTacked() ? TEXT("  — прихвачен") : TEXT("")),
					bSealed ? FLinearColor(0.4f, 1.0f, 0.5f) : FLinearColor(1.0f, 0.8f, 0.4f), 24.0f, Line, Font);
			}
		}
		const FText Toast = Mode->GetToast();
		if (!Toast.IsEmpty())
		{
			float Width = 0.0f;
			float Height = 0.0f;
			const float Scale = Mode->bEnded ? 2.0f : 1.2f;
			GetTextSize(Toast.ToString(), Width, Height, Font, Scale);
			DrawText(Toast.ToString(), FLinearColor::White, (Canvas->ClipX - Width) * 0.5f, Canvas->ClipY * 0.25f, Font, Scale);
		}
	}
}

// --- console probes ----------------------------------------------------------------------------

namespace
{
	/** Run Step after Seconds of game time (the probe's chain of steps). */
	void After(AJamShipGameMode* Mode, float Seconds, TFunction<void()> Step)
	{
		FTimerHandle Handle;
		Mode->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Mode, MoveTemp(Step)), Seconds, false);
	}

	AJamZeroGPawn* PlayerPawn(const UWorld* World)
	{
		const APlayerController* PlayerController = World->GetFirstPlayerController();
		return PlayerController ? Cast<AJamZeroGPawn>(PlayerController->GetPawn()) : nullptr;
	}

	/** The pawn's view in its gravity space (world down in zero-G): roll should stay 0. */
	FRotator GravityView(const AJamZeroGPawn* Pawn)
	{
		const FVector Gravity = Pawn->IsInGravity() ? Pawn->GetGravityDirection() : FVector::DownVector;
		return JamGravityRelativeRotation(Pawn->GetControlRotation(), Gravity);
	}

	void LogState(const TCHAR* Stage, const AJamZeroGPawn* Pawn)
	{
		if (!Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe [%s]: no AJamZeroGPawn"), Stage);
			return;
		}
		const FVector Eye = Pawn->Camera->GetComponentLocation() - Pawn->Body->GetComponentLocation();
		const FVector Up = Pawn->IsInGravity() ? -Pawn->GetGravityDirection() : FVector::UpVector;
		const AJamShipController* Controller = Cast<AJamShipController>(Pawn->GetController());
		const FRotator View = GravityView(Pawn);
		UE_LOG(LogMindKore, Log, TEXT("Ship probe [%s]: %s at %s, %s, speed %.0f cm/s, eye %.0f cm over the body (. up %.3f), view gravity . gravity %.3f, view pitch %.1f roll %.1f in gravity space, suit %s, guns %s, O2 %.0f, HP %.0f"),
			Stage, *Pawn->GetName(), *Pawn->GetActorLocation().ToCompactString(),
			Pawn->IsInGravity() ? (Pawn->IsGrounded() ? TEXT("walking, on the ground") : TEXT("walking, in the air")) : TEXT("zero-G"),
			Pawn->GetBodyVelocity().Size(), Eye.Size(), FVector::DotProduct(Eye.GetSafeNormal(), Up),
			Controller && Pawn->IsInGravity() ? FVector::DotProduct(Controller->GetViewGravity(), Pawn->GetGravityDirection()) : -2.0f,
			View.Pitch, View.Roll, MKHolderHasKey(Pawn, MKJamSuitKey()) ? TEXT("yes") : TEXT("no"), *Pawn->Guns->Describe(),
			Pawn->Oxygen->Seconds, Pawn->Vitals->Health);
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

	TArray<AActor*> Tagged(UWorld* World, FName Tag)
	{
		TArray<AActor*> Found;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(Tag))
			{
				Found.Add(*It);
			}
		}
		return Found;
	}

	/** Hold scripted walking/thrust input (view frame: X forward) every frame for Seconds. */
	void HoldInput(AJamShipGameMode* Mode, const FVector& Input, float Seconds)
	{
		const double End = Mode->GetWorld()->GetTimeSeconds() + Seconds;
		const TSharedRef<FTimerHandle> Hold = MakeShared<FTimerHandle>();
		Mode->GetWorldTimerManager().SetTimer(*Hold, FTimerDelegate::CreateWeakLambda(Mode, [Mode, Hold, End, Input]()
		{
			if (Mode->GetWorld()->GetTimeSeconds() > End)
			{
				Mode->GetWorldTimerManager().ClearTimer(*Hold);
				return;
			}
			if (AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld()))
			{
				Pawn->ProbeThrust(Input, false);
			}
		}), 0.0005f, true);
	}

	void LogPanel(UWorld* World, const TCHAR* Stage)
	{
		for (TActorIterator<AJamShipPanel> Panel(World); Panel; ++Panel)
		{
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [%s]: hull %d/%d %s, fuel %d/%d %s, navigation %d/%d %s"), Stage,
				Panel->HullDone(), Panel->HullSockets.Num(), Panel->IsHullDone() ? TEXT("green") : TEXT("red"),
				Panel->FuelDone(), Panel->FuelNeeded(), Panel->IsFuelDone() ? TEXT("green") : TEXT("red"),
				Panel->NavDone(), Panel->NavNeeded(), Panel->IsNavDone() ? TEXT("green") : TEXT("red"));
		}
	}

	/** The walking shell of the planetoid (the radial walking zone), or null. */
	const AJamGravityZone* PlanetShell(UWorld* World)
	{
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			if (It->bRadial && It->bWalk && It->SphereRadiusCm > 0.0f)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** (c) Every part into place; one tank out again (the fuel lamp and the lever must go), back in, then the lever. */
	void ProbeFinish(AJamShipGameMode* Mode)
	{
		UWorld* World = Mode->GetWorld();
		AJamShipPanel* Panel = nullptr;
		for (TActorIterator<AJamShipPanel> It(World); It; ++It)
		{
			Panel = *It;
		}
		if (!Panel)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: no panel"));
			return;
		}
		TArray<AActor*> Plates = Tagged(World, TEXT("Hull.Plate"));
		int32 Next = 0;
		for (const AJamPlateSocket* Socket : Panel->HullSockets)
		{
			if (Socket && !Socket->bFilled && Plates.IsValidIndex(Next))
			{
				Teleport(Plates[Next++], Socket->GetActorLocation());
			}
		}
		TArray<AActor*> Fuel = Tagged(World, TEXT("Ship.Fuel"));
		for (int32 Index = 0; Panel->FuelDock && Index < Fuel.Num(); ++Index)
		{
			Teleport(Fuel[Index], Panel->FuelDock->GetActorLocation() + FVector(0.0f, (Index - 0.5f) * 200.0f, 0.0f));
		}
		for (AActor* Nav : Tagged(World, TEXT("Ship.Nav")))
		{
			if (Panel->NavDock)
			{
				Teleport(Nav, Panel->NavDock->GetActorLocation());
			}
		}
		After(Mode, 2.0f, [Mode, Panel, Fuel]()
		{
			LogPanel(Mode->GetWorld(), TEXT("all parts in"));
			if (Fuel.IsEmpty())
			{
				Panel->CompleteInteraction(PlayerPawn(Mode->GetWorld()));
				UE_LOG(LogMindKore, Log, TEXT("Ship probe [end]: ended %s"), Mode->bEnded ? TEXT("yes") : TEXT("no"));
				return;
			}
			// The goals are live: a tank pushed back out takes the fuel lamp and the lever with it.
			const FVector Docked = Fuel[0]->GetActorLocation();
			Teleport(Fuel[0], Docked + FVector(0.0f, 0.0f, 3000.0f));
			After(Mode, 1.0f, [Mode, Panel, Fuel, Docked]()
			{
				LogPanel(Mode->GetWorld(), TEXT("a tank pushed out"));
				Panel->CompleteInteraction(PlayerPawn(Mode->GetWorld()));
				UE_LOG(LogMindKore, Log, TEXT("Ship probe [lever, a tank out]: ended %s"), Mode->bEnded ? TEXT("yes") : TEXT("no"));
				Teleport(Fuel[0], Docked);
				After(Mode, 1.0f, [Mode, Panel]()
				{
					LogPanel(Mode->GetWorld(), TEXT("the tank back in"));
					Panel->CompleteInteraction(PlayerPawn(Mode->GetWorld()));
					UE_LOG(LogMindKore, Log, TEXT("Ship probe [end]: ended %s"), Mode->bEnded ? TEXT("yes") : TEXT("no"));
				});
			});
		});
	}

	/** (b) A plate at a socket with junk in the hull, then with the hull cleaned. */
	void ProbePlate(AJamShipGameMode* Mode)
	{
		UWorld* World = Mode->GetWorld();
		AJamPlateSocket* Socket = nullptr;
		for (TActorIterator<AJamPlateSocket> It(World); It; ++It)
		{
			Socket = *It;
			break;
		}
		TArray<AActor*> Plates = Tagged(World, TEXT("Hull.Plate"));
		if (!Socket || Plates.IsEmpty())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: no socket or no plate"));
			return;
		}
		AActor* Plate = Plates[0];
		UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: %s to %s with junk in the hull: %d"), *Plate->GetName(), *Socket->GetName(),
			Socket->CleanZone ? Socket->CleanZone->GetCount() : -1);
		Teleport(Plate, Socket->GetActorLocation());
		After(Mode, 0.5f, [Mode, Socket, Plate]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: with junk - fitted %s"), Socket->bFilled ? TEXT("yes") : TEXT("no"));
			// Clear the hull: every foreign thing 150 m away.
			int32 Moved = 0;
			for (AActor* Junk : Tagged(Mode->GetWorld(), TEXT("Junk.Foreign")))
			{
				Teleport(Junk, FVector(0.0f, 0.0f, 15000.0f) + FVector(Moved++ * 300.0f, 0.0f, 0.0f));
			}
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: %d junk props moved out of the hull"), Moved);
			After(Mode, 0.5f, [Mode, Socket, Plate]()
			{
				Teleport(Plate, Socket->GetActorLocation());
				After(Mode, 0.5f, [Mode, Socket]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Ship probe [plate]: clean hull (%d) - fitted %s"),
						Socket->CleanZone ? Socket->CleanZone->GetCount() : -1, Socket->bFilled ? TEXT("yes") : TEXT("no"));
					ProbeFinish(Mode);
				});
			});
		});
	}

	/** (a3) Out of the airlock into zero-G, the pistol, then down onto the planetoid. */
	void ProbeOutAndPlanet(AJamShipGameMode* Mode, const FVector& Outside)
	{
		Teleport(PlayerPawn(Mode->GetWorld()), Outside);
		After(Mode, 0.5f, [Mode]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("out of the airlock"), Pawn);
			for (TActorIterator<AJamWeaponPickup> Pickup(Mode->GetWorld()); Pickup; ++Pickup)
			{
				if (Pickup->WeaponIndex == 0)
				{
					Pickup->CompleteInteraction(Pawn);
				}
			}
			Pawn->Fire();
			LogState(TEXT("pistol taken, one shot"), Pawn);
			for (TActorIterator<AJamToolPickup> Tool(Mode->GetWorld()); Tool; ++Tool)
			{
				Tool->CompleteInteraction(Pawn);
			}
			UE_LOG(LogMindKore, Log, TEXT("Ship probe [tool pickups taken]: the grapple %s"), Mode->Tools.Contains(MKJamGrappleTool()) ? TEXT("unlocked") : TEXT("NOT unlocked"));
			const AJamGravityZone* Shell = PlanetShell(Mode->GetWorld());
			if (!Shell)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Ship probe: no planetoid shell"));
				ProbePlate(Mode);
				return;
			}
			// 5 m over the top of the surface: falls, lands, the view turns to the local up.
			Teleport(Pawn, Shell->GetActorLocation() + FVector::UpVector * (Shell->SphereRadiusCm - 300.0f));
			After(Mode, 3.0f, [Mode]()
			{
				LogState(TEXT("on the planetoid"), PlayerPawn(Mode->GetWorld()));
				ProbePlate(Mode);
			});
		});
	}

	/**
	 * jam.Ship.Probe: (a) standing in the cabin, walking, stopping and a jump,
	 * the suit, both airlock doors, out into zero-G, the pistol, landing on the
	 * planetoid; (b) a plate refused and fitted; (c) every part in and the lever.
	 */
	void RunShipProbe(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		// The box zones (cabin, airlock): the cabin is the biggest; outside = 2 m
		// past the lowest X of them all (beyond the outer door).
		const AJamGravityZone* Cabin = nullptr;
		double MinX = TNumericLimits<double>::Max();
		for (TActorIterator<AJamGravityZone> It(World); It; ++It)
		{
			if (It->SphereRadiusCm > 0.0f)
			{
				continue;
			}
			const FVector Extent = It->Zone->GetScaledBoxExtent();
			MinX = FMath::Min(MinX, It->GetActorLocation().X - Extent.X);
			if (!Cabin || Extent.X * Extent.Y * Extent.Z > Cabin->Zone->GetScaledBoxExtent().X * Cabin->Zone->GetScaledBoxExtent().Y * Cabin->Zone->GetScaledBoxExtent().Z)
			{
				Cabin = *It;
			}
		}
		if (!Mode || !Pawn || !Cabin)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship probe: needs AJamShipGameMode, an AJamZeroGPawn and a box gravity zone"));
			return;
		}
		LogPanel(World, TEXT("start"));
		const FVector Outside(MinX - 200.0, Cabin->GetActorLocation().Y, Cabin->GetActorLocation().Z);
		// The back of the cabin, facing the panel (+X): room for a short walk.
		Teleport(Pawn, FVector(Cabin->GetActorLocation().X - Cabin->Zone->GetScaledBoxExtent().X + 60.0, 0.0, Cabin->GetActorLocation().Z));
		World->GetFirstPlayerController()->SetControlRotation(FRotator::ZeroRotator);
		After(Mode, 2.0f, [Mode, Outside]()
		{
			LogState(TEXT("standing, 2 s"), PlayerPawn(Mode->GetWorld()));
			HoldInput(Mode, FVector(1.0f, 0.0f, 0.0f), 0.8f);
			After(Mode, 0.4f, [Mode]() { LogState(TEXT("walking +0.4 s"), PlayerPawn(Mode->GetWorld())); });
			After(Mode, 0.8f, [Mode]() { LogState(TEXT("walking +0.8 s"), PlayerPawn(Mode->GetWorld())); });
			After(Mode, 1.4f, [Mode, Outside]()
			{
				AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
				LogState(TEXT("released +0.6 s"), Pawn);
				Pawn->JumpPressed();
				After(Mode, 0.15f, [Mode]() { LogState(TEXT("jump +0.15 s"), PlayerPawn(Mode->GetWorld())); });
				After(Mode, 1.5f, [Mode, Outside]()
				{
					AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
					LogState(TEXT("jump +1.5 s"), Pawn);
					for (TActorIterator<AJamSuitLocker> Locker(Mode->GetWorld()); Locker; ++Locker)
					{
						Locker->CompleteInteraction(Pawn);
					}
					// The doors by X: the inner one (cabin side) has the larger X.
					TArray<AMKDoor*> Doors;
					for (TActorIterator<AMKDoor> Door(Mode->GetWorld()); Door; ++Door)
					{
						Doors.Add(*Door);
					}
					Doors.Sort([](const AMKDoor& A, const AMKDoor& B) { return A.GetActorLocation().X > B.GetActorLocation().X; });
					for (AMKDoor* Door : Doors)
					{
						// A key lock takes one E to turn the key and another to open.
						const bool bKeyLock = Door->IsKeyLockEngaged();
						Door->CompleteInteraction(Pawn);
						if (bKeyLock)
						{
							Door->CompleteInteraction(Pawn);
						}
					}
					LogState(TEXT("suit taken"), Pawn);
					After(Mode, 1.5f, [Mode, Doors, Outside]()
					{
						for (const AMKDoor* Door : Doors)
						{
							UE_LOG(LogMindKore, Log, TEXT("Ship probe [airlock]: %s at x %.0f passable %s, key lock %s"), *Door->GetName(),
								Door->GetActorLocation().X, Door->IsPassable() ? TEXT("yes") : TEXT("no"), Door->IsKeyLockEngaged() ? TEXT("engaged") : TEXT("turned"));
						}
						ProbeOutAndPlanet(Mode, Outside);
					});
				});
			});
		});
	}

	void LogWhere(UWorld* World, FName Tag)
	{
		for (AActor* Actor : Tagged(World, Tag))
		{
			UE_LOG(LogMindKore, Log, TEXT("Where %s: %s at %s, %.0f cm/s"), *Tag.ToString(), *Actor->GetName(),
				*Actor->GetActorLocation().ToCompactString(), Actor->GetVelocity().Size());
		}
	}

	/** jam.Ship.Where [Tag] [Seconds]: where every actor with that tag is and how fast it moves; again after Seconds. */
	void RunShipWhere(const TArray<FString>& Args, UWorld* World)
	{
		const FName Tag(Args.Num() > 0 ? *Args[0] : TEXT("Junk.Foreign"));
		LogWhere(World, Tag);
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		if (Args.Num() > 1 && Mode)
		{
			After(Mode, FCString::Atof(*Args[1]), [World, Tag]() { LogWhere(World, Tag); });
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GShipWhere(
		TEXT("jam.Ship.Where"),
		TEXT("Logs location and speed of every actor with the given tag (default Junk.Foreign)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShipWhere));

	/**
	 * jam.Ship.Shots: rendered stills for a look check (needs a real RHI, not
	 * -nullrhi). Teleports the player (BugItGo) to each viewpoint, waits for the
	 * frame to settle, takes a HighResShot; the last one looks at the strongest
	 * directional light's star. Files land in Saved/Screenshots in this order.
	 */
	void RunShipShots(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Mode || !PlayerController)
		{
			return;
		}
		FRotator ToStar(-20.0f, 0.0f, 0.0f);
		float Brightest = -1.0f;
		for (TActorIterator<ADirectionalLight> Light(World); Light; ++Light)
		{
			const float Intensity = Light->GetLightComponent() ? Light->GetLightComponent()->Intensity : 0.0f;
			if (Intensity > Brightest)
			{
				Brightest = Intensity;
				ToStar = (-Light->GetActorForwardVector()).Rotation();
			}
		}
		struct FView { const TCHAR* Name; FVector Where; FRotator Look; };
		const TArray<FView> Views = {
			{ TEXT("cabin_panel"), FVector(1700.0f, 0.0f, -40.0f), FRotator(-8.0f, 0.0f, 0.0f) },
			{ TEXT("cabin_back"), FVector(1900.0f, 100.0f, -40.0f), FRotator(-5.0f, 190.0f, 0.0f) },
			{ TEXT("ship_outside"), FVector(600.0f, 1800.0f, 400.0f), FRotator(-10.0f, -100.0f, 0.0f) },
			{ TEXT("to_planetoid"), FVector(0.0f, 3500.0f, 300.0f), FRotator(-5.0f, 90.0f, 0.0f) },
			{ TEXT("to_star"), FVector(-1500.0f, -1500.0f, 1500.0f), ToStar },
			// The star map puts the galactic centre toward +X on the horizon (Scripts/jam_sky.py).
			{ TEXT("to_galactic_core"), FVector(2600.0f, -600.0f, 200.0f), FRotator(5.0f, 0.0f, 0.0f) },
		};
		for (int32 Index = 0; Index < Views.Num(); ++Index)
		{
			const FView View = Views[Index];
			After(Mode, 1.0f + Index * 4.0f, [PlayerController, View]()
			{
				PlayerController->ConsoleCommand(FString::Printf(TEXT("BugItGo %f %f %f %f %f 0"), View.Where.X, View.Where.Y, View.Where.Z,
					View.Look.Pitch, View.Look.Yaw));
			});
			After(Mode, 3.5f + Index * 4.0f, [PlayerController, View, Index]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Ship shots: %d %s at %s looking %s"), Index, View.Name, *View.Where.ToCompactString(), *View.Look.ToCompactString());
				LogState(View.Name, Cast<AJamZeroGPawn>(PlayerController->GetPawn()));
				PlayerController->ConsoleCommand(TEXT("HighResShot 1"));
			});
		}
	}

	FAutoConsoleCommandWithWorld GShipShots(
		TEXT("jam.Ship.Shots"),
		TEXT("Rendered stills of Lvl_Jam_Ship from six viewpoints (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipShots));

	/**
	 * jam.Ship.Lap: put the player on top of the planetoid and walk straight
	 * ahead for 35 s (scripted forward input every frame); every 3 s log the
	 * angle travelled around the centre, the camera's offset against the local
	 * up, and the view's roll in gravity space (should stay ~0).
	 */
	void RunShipLap(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		if (!Mode || !Shell || !Pawn)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship lap: needs the ship mode, a planetoid walking shell and the pawn"));
			return;
		}
		const FVector Centre = Shell->GetActorLocation();
		const FVector Start = Centre + FVector::UpVector * (Shell->SphereRadiusCm - 600.0f);
		Teleport(Pawn, Start);
		World->GetFirstPlayerController()->SetControlRotation(FRotator(-10.0f, 0.0f, 0.0f));
		After(Mode, 2.0f, [Mode]() { HoldInput(Mode, FVector(1.0f, 0.0f, 0.0f), 35.0f); });
		for (int32 Step = 0; Step <= 12; ++Step)
		{
			After(Mode, 2.0f + Step * 3.0f, [Mode, Centre, Start, Step]()
			{
				const AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld());
				if (!Now)
				{
					return;
				}
				const FVector Up = (Now->GetActorLocation() - Centre).GetSafeNormal();
				const float Angle = FMath::RadiansToDegrees(FMath::Acos(FVector::DotProduct(Up, (Start - Centre).GetSafeNormal())));
				const FVector Eye = (Now->Camera->GetComponentLocation() - Now->Body->GetComponentLocation()).GetSafeNormal();
				const FRotator Relative = GravityView(Now);
				const AJamShipController* Controller = Cast<AJamShipController>(Now->GetController());
				UE_LOG(LogMindKore, Log, TEXT("Ship lap +%2d s: %.0f deg from the start, height %.0f cm, %s, speed %.0f cm/s, eye offset . local up %.3f, view roll in gravity space %.1f, pitch %.1f, view gravity . gravity %.3f"),
					Step * 3, Angle, FVector::Dist(Now->GetActorLocation(), Centre), Now->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"),
					Now->GetBodyVelocity().Size(), FVector::DotProduct(Eye, Up), Relative.Roll, Relative.Pitch,
					Controller ? FVector::DotProduct(Controller->GetViewGravity(), Now->GetGravityDirection()) : -2.0f);
			});
		}
	}

	FAutoConsoleCommandWithWorld GShipLap(
		TEXT("jam.Ship.Lap"),
		TEXT("Walk the player straight ahead around the planetoid for 35 s, logging angle, up vectors and view roll."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipLap));

	/** jam.Ship.JetExit: on the planetoid, jump and Space again in the air; logs leaving its gravity. */
	void RunJetExit(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		if (!Mode || !Shell || !Pawn)
		{
			return;
		}
		Teleport(Pawn, Shell->GetActorLocation() + FVector::UpVector * (Shell->SphereRadiusCm - 650.0f));
		After(Mode, 2.0f, [Mode]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("jet: before the jump"), Pawn);
			Pawn->JumpPressed();
		});
		After(Mode, 2.4f, [Mode]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("jet: before the second press"), Pawn);
			Pawn->JumpPressed();
		});
		After(Mode, 4.0f, [Mode]() { LogState(TEXT("jet: +1.6 s after"), PlayerPawn(Mode->GetWorld())); });
	}

	FAutoConsoleCommandWithWorld GJetExit(
		TEXT("jam.Ship.JetExit"),
		TEXT("On the planetoid: jump, then Space in the air - the jet leap that leaves its gravity; logs the state."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunJetExit));

	/**
	 * jam.Ship.Land [Pitch] [Roll] [Speed]: start outside the planetoid's shell
	 * with a free (rolled) view, fall onto the surface at Speed and log every
	 * frame-ish for 4 s: grounded, radial and tangential speed, the body's
	 * angular velocity and its up against the local up, the view's roll/pitch
	 * in gravity space and how far the view turned since the last sample.
	 */
	void RunShipLand(const TArray<FString>& Args, UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Mode || !Shell || !Pawn || !PlayerController)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Ship land: needs the ship mode, the planetoid shell and the pawn"));
			return;
		}
		const float Pitch = Args.Num() > 0 ? FCString::Atof(*Args[0]) : -60.0f;
		const float Roll = Args.Num() > 1 ? FCString::Atof(*Args[1]) : 150.0f;
		const float Speed = Args.Num() > 2 ? FCString::Atof(*Args[2]) : 600.0f;
		const FVector Centre = Shell->GetActorLocation();
		const FVector Start = Centre + FVector::UpVector * (Shell->SphereRadiusCm + 300.0f);
		Teleport(Pawn, Start);
		PlayerController->SetControlRotation(FRotator(Pitch, 30.0f, Roll));
		After(Mode, 0.5f, [Mode, Centre, Speed]()
		{
			if (AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld()))
			{
				Now->Body->SetPhysicsLinearVelocity((Centre - Now->GetActorLocation()).GetSafeNormal() * Speed);
			}
		});
		TSharedRef<FQuat> LastView = MakeShared<FQuat>(FQuat::Identity);
		for (int32 Step = 0; Step <= 120; ++Step)
		{
			After(Mode, 0.5f + Step * 0.033f, [Mode, Centre, Step, LastView]()
			{
				const AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld());
				if (!Now)
				{
					return;
				}
				const FVector Up = (Now->GetActorLocation() - Centre).GetSafeNormal();
				const FVector Velocity = Now->GetBodyVelocity();
				const float Radial = FVector::DotProduct(Velocity, Up);
				const FQuat View = Now->GetControlRotation().Quaternion();
				const float Turned = Step == 0 ? 0.0f : FMath::RadiansToDegrees(View.AngularDistance(*LastView));
				*LastView = View;
				const FRotator Relative = GravityView(Now);
				UE_LOG(LogMindKore, Log, TEXT("Ship land %3d: %s %s, height %.0f, radial %.0f, tangential %.0f cm/s, body spin %.1f deg/s, body up . local up %.3f, view pitch %.1f roll %.1f, view turned %.2f deg"),
					Step, Now->IsInGravity() ? TEXT("gravity") : TEXT("zero-G "), Now->IsGrounded() ? TEXT("ground") : TEXT("air   "),
					FVector::Dist(Now->GetActorLocation(), Centre), Radial, (Velocity - Radial * Up).Size(),
					Now->Body->GetPhysicsAngularVelocityInDegrees().Size(), FVector::DotProduct(Now->Body->GetUpVector(), Up),
					Relative.Pitch, Relative.Roll, Turned);
			});
		}
	}

	FAutoConsoleCommandWithWorldAndArgs GShipLand(
		TEXT("jam.Ship.Land"),
		TEXT("jam.Ship.Land [Pitch] [Roll] [Speed]: fall onto the planetoid from a free, rolled view and log the landing every ~33 ms."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShipLand));

	/**
	 * jam.Hull.Probe [Index]: hull repair on Socket_Breach<Index> (the first by default), logic only.
	 * A plate 40 cm off the hole, the torch tried (junk inside: refused); a strap from the opposite
	 * wall through the hole pulls it on (gaps and coverage logged); the junk is removed; the whole
	 * contour welded with the pawn holding the plate (tack, seal, the panel; the tack must make it let go); half cut (unsealed, still tacked), all cut (loose),
	 * welded again; cling to the hull and fire the sniper; then the suit on, a strap line wall - plate
	 * and one on the belt, the torch lit and the hook in, and death: the torch, the hook and the belt
	 * line go at once, the input stops; the same pawn wakes standing in the cabin with the suit, the
	 * guns as they were (only the sniper unlocked), the wall - plate line and the seam;
	 * last a second plate 40 cm off the next breach: the torch must find a gap. With "shots" as the
	 * second argument: HighResShot stills of the strapped plate and of the fresh seam (needs a real RHI).
	 */
	void RunHullProbe(const TArray<FString>& Args, UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		TArray<AJamPlateSocket*> Sockets;
		for (TActorIterator<AJamPlateSocket> It(World); It; ++It)
		{
			Sockets.Add(*It);
		}
		Sockets.Sort([](const AJamPlateSocket& A, const AJamPlateSocket& B) { return A.GetName() < B.GetName(); });
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		const bool bShots = Args.Num() > 1 && Args[1] == TEXT("shots");
		AActor* Plate = nullptr;
		AActor* Spare = nullptr;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->ActorHasTag(TEXT("Hull.Plate")))
			{
				(Plate ? Spare : Plate) = *It;
			}
		}
		if (!Mode || !Pawn || !Sockets.IsValidIndex(Index) || !Plate)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hull probe: needs the ship map, the pawn, a breach %d and a Hull.Plate"), Index);
			return;
		}
		AJamPlateSocket* Socket = Sockets[Index];
		const TWeakObjectPtr<AJamPlateSocket> WeakSocket(Socket);
		const FTransform Frame = Socket->GetActorTransform();
		const FVector Out = Frame.GetUnitAxis(EAxis::Z);
		UPrimitiveComponent* PlateBody = Cast<UPrimitiveComponent>(Plate->GetRootComponent());
		// Plate 40 cm off the outer face, square to the hole; the pawn 3 m out.
		PlateBody->SetWorldLocationAndRotation(Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, Socket->SurfaceZ + 10.0f + 40.0f)), Frame.GetRotation(),
			false, nullptr, ETeleportType::TeleportPhysics);
		PlateBody->SetPhysicsLinearVelocity(FVector::ZeroVector);
		PlateBody->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		Teleport(Pawn, Socket->GetActorLocation() + Out * 300.0f + Frame.GetUnitAxis(EAxis::X) * 150.0f);
		if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
		{
			Controller->SetControlRotation((Socket->GetActorLocation() - Pawn->GetActorLocation()).Rotation());
		}
		UE_LOG(LogMindKore, Log, TEXT("Hull probe: %s, out %s, plate %s %.0f kg, junk inside %d"), *Socket->GetName(), *Out.ToCompactString(),
			*Plate->GetName(), PlateBody->GetMass(), Socket->JunkCount());
		const auto Contour = [WeakSocket](float Step)
		{
			// Torch points along the hole edge, on the plate top.
			TArray<FVector> Points;
			const AJamPlateSocket* S = WeakSocket.Get();
			const FVector2D H = S->HoleHalfCm;
			const FVector2D Corners[4] = { { -H.X, -H.Y }, { H.X, -H.Y }, { H.X, H.Y }, { -H.X, H.Y } };
			for (int32 Side = 0; Side < 4; ++Side)
			{
				const FVector2D From = Corners[Side];
				const FVector2D To = Corners[(Side + 1) % 4];
				const int32 Count = FMath::CeilToInt32(float((To - From).Size()) / Step);
				for (int32 K = 0; K < Count; ++K)
				{
					const FVector2D P = From + (To - From) * (float(K) / Count);
					Points.Add(S->GetActorTransform().TransformPositionNoScale(FVector(P.X, P.Y, S->SurfaceZ + 20.0f)));
				}
			}
			return Points;
		};
		const auto Report = [WeakSocket, PlateBody](const TCHAR* Stage)
		{
			const AJamPlateSocket* S = WeakSocket.Get();
			int32 Hull = 0;
			for (TActorIterator<AJamShipPanel> Panel(S->GetWorld()); Panel; ++Panel)
			{
				Hull = Panel->HullDone();
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [%s]: seam %.0f%%, coverage %.0f%%, plate over the hole %s, tacked %s, sealed %s, plate simulating %s, panel hull %d"),
				Stage, S->GetSeamFraction() * 100.0f, S->GetCoverage() * 100.0f, *GetNameSafe(S->GetPlate()), S->IsTacked() ? TEXT("yes") : TEXT("no"),
				S->bFilled ? TEXT("yes") : TEXT("no"), PlateBody->IsSimulatingPhysics() ? TEXT("yes") : TEXT("no"), Hull);
		};
		After(Mode, 0.5f, [WeakSocket, Contour]()
		{
			const TArray<FVector> Points = Contour(10.0f);
			const EJamWeldResult Result = WeakSocket->Weld(Points[5], 0.05f, false);
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [torch, plate 40 cm off, junk inside]: %s"),
				Result == EJamWeldResult::Junk ? TEXT("refused: junk") : Result == EJamWeldResult::Gap ? TEXT("refused: gap") : Result == EJamWeldResult::Welded ? TEXT("WELDED") : TEXT("off the seam"));
		});
		After(Mode, 0.6f, [Mode, Pawn, WeakSocket, PlateBody, Out, Report]()
		{
			// The anchor: the hull wall across the ship, straight in through the hole.
			const FVector From = WeakSocket->GetActorLocation() - Out * 30.0f;
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullProbe), false, Pawn);
			Params.AddIgnoredActor(PlateBody->GetOwner());
			if (!Mode->GetWorld()->LineTraceSingleByChannel(Hit, From, From - Out * 1200.0f, ECC_Visibility, Params) || !Hit.GetComponent())
			{
				UE_LOG(LogMindKore, Warning, TEXT("Hull probe: no wall across the ship to anchor the strap"));
				return;
			}
			const FVector OnPlate = PlateBody->GetComponentLocation() - Out * 10.0f;
			const int32 Line = Pawn->HullTools->AddLine(Hit.GetComponent(), Hit.ImpactPoint, PlateBody, OnPlate);
			Pawn->HullTools->SetLineLength(Line, float(FVector::Dist(Hit.ImpactPoint, OnPlate)) - 80.0f);
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [strap]: %s (%s) to the plate, %.0f cm, reeled 80 cm in"), *GetNameSafe(Hit.GetActor()),
				Hit.GetComponent()->IsSimulatingPhysics() ? TEXT("loose") : TEXT("fixed"), FVector::Dist(Hit.ImpactPoint, OnPlate));
		});
		After(Mode, 4.0f, [WeakSocket, PlateBody, Contour, Report]()
		{
			const FVector Local = WeakSocket->GetActorTransform().InverseTransformPositionNoScale(PlateBody->GetComponentLocation());
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [strapped +3.4 s]: plate centre at %s in the breach frame (flush: Z %.0f), speed %.0f cm/s"),
				*Local.ToCompactString(), WeakSocket->SurfaceZ + 10.0f, PlateBody->GetPhysicsLinearVelocity().Size());
			const TArray<FVector> Points = Contour(10.0f);
			const EJamWeldResult Result = WeakSocket->Weld(Points[5], 0.05f, false);
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [torch, strapped, junk inside]: %s"), Result == EJamWeldResult::Junk ? TEXT("refused: junk") : TEXT("NOT refused"));
			Report(TEXT("strapped"));
		});
		if (bShots)
		{
			After(Mode, 3.8f, [Pawn]() { Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(TEXT("Shot")); });
		}
		After(Mode, 4.2f, [Mode]()
		{
			int32 Removed = 0;
			for (TActorIterator<AActor> It(Mode->GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(TEXT("Junk.Foreign")))
				{
					It->Destroy();
					++Removed;
				}
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull probe: %d junk removed from the hull"), Removed);
		});
		After(Mode, 4.8f, [Pawn, PlateBody, WeakSocket, Contour, Report]()
		{
			// Holding the plate while its seam tacks it: the pawn must let go by itself once it stops simulating.
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [held while welding]: grabbed %s"), Pawn->Grab(PlateBody, PlateBody->GetComponentLocation()) ? TEXT("yes") : TEXT("no"));
			int32 Welded = 0;
			int32 Gaps = 0;
			for (const FVector& Point : Contour(5.0f))
			{
				for (int32 Pass = 0; Pass < 4; ++Pass)
				{
					const EJamWeldResult Result = WeakSocket->Weld(Point, 0.05f, false);
					Welded += Result == EJamWeldResult::Welded ? 1 : 0;
					Gaps += Result == EJamWeldResult::Gap ? 1 : 0;
				}
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [welded along the contour]: %d welded torch steps, %d on a gap, junk %d"), Welded, Gaps, WeakSocket->JunkCount());
			Report(TEXT("welded"));
		});
		After(Mode, 4.9f, [Pawn, PlateBody]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [tacked +0.1 s]: plate simulating %s, the pawn holds %s, speed %.0f cm/s"),
				PlateBody->IsSimulatingPhysics() ? TEXT("yes") : TEXT("no"), *GetNameSafe(Pawn->GetGrabbed() ? Pawn->GetGrabbed()->GetOwner() : nullptr), Pawn->GetBodyVelocity().Size());
		});
		if (bShots)
		{
			After(Mode, 5.0f, [Pawn]() { Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(TEXT("Shot")); });
		}
		After(Mode, 5.2f, [Pawn, Report]()
		{
			Pawn->HullTools->CutAll();
			Report(TEXT("strap cut"));
		});
		After(Mode, 6.5f, [WeakSocket, Contour, Report]()
		{
			const TArray<FVector> Points = Contour(5.0f);
			for (int32 K = 0; K < Points.Num() / 2; ++K)
			{
				for (int32 Pass = 0; Pass < 4; ++Pass)
				{
					WeakSocket->Weld(Points[K], 0.05f, true);
				}
			}
			Report(TEXT("half cut, 1.3 s after the strap went"));
		});
		After(Mode, 6.8f, [WeakSocket, Contour, Report]()
		{
			for (const FVector& Point : Contour(5.0f))
			{
				for (int32 Pass = 0; Pass < 4; ++Pass)
				{
					WeakSocket->Weld(Point, 0.05f, true);
				}
			}
			Report(TEXT("all cut"));
		});
		After(Mode, 7.0f, [Pawn, WeakSocket, PlateBody, Out]()
		{
			// Back on the hole for the re-weld: the strap again.
			FHitResult Hit;
			const FVector From = WeakSocket->GetActorLocation() - Out * 30.0f;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullProbe), false, Pawn);
			Params.AddIgnoredActor(PlateBody->GetOwner());
			if (Pawn->GetWorld()->LineTraceSingleByChannel(Hit, From, From - Out * 1200.0f, ECC_Visibility, Params))
			{
				const FVector OnPlate = PlateBody->GetComponentLocation() - Out * 10.0f;
				const int32 Line = Pawn->HullTools->AddLine(Hit.GetComponent(), Hit.ImpactPoint, PlateBody, OnPlate);
				Pawn->HullTools->SetLineLength(Line, float(FVector::Dist(Hit.ImpactPoint, OnPlate)) - 80.0f);
			}
		});
		After(Mode, 9.0f, [WeakSocket, Contour, Report]()
		{
			for (const FVector& Point : Contour(5.0f))
			{
				for (int32 Pass = 0; Pass < 4; ++Pass)
				{
					WeakSocket->Weld(Point, 0.05f, false);
				}
			}
			Report(TEXT("welded again"));
		});
		After(Mode, 9.3f, [Pawn, WeakSocket, Out]()
		{
			// Cling to the hull beside the patch and fire the sniper; then let go and fire again.
			Pawn->HullTools->CutAll();
			Teleport(Pawn, WeakSocket->GetActorLocation() + Out * 120.0f + WeakSocket->GetActorForwardVector() * 300.0f);
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullProbe), false, Pawn);
			const FVector From = Pawn->Body->GetComponentLocation();
			const bool bHull = Pawn->GetWorld()->LineTraceSingleByChannel(Hit, From, From - Out * 300.0f, ECC_Visibility, Params);
			const bool bClung = bHull && Pawn->Cling(Hit.GetComponent(), Hit.ImpactPoint);
			Pawn->Guns->UnlockWeapon(2);
			Pawn->Guns->FinishSwitch(); // no draw: the probe shoots at once
			if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
			{
				Controller->SetControlRotation((-Out).Rotation());
			}
			Pawn->Fire();
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [cling]: on %s %s, sniper fired"), *GetNameSafe(bHull ? Hit.GetActor() : nullptr), bClung ? TEXT("held") : TEXT("NOT held"));
		});
		After(Mode, 9.8f, [Pawn]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [cling +0.5 s]: speed %.0f cm/s"), Pawn->GetBodyVelocity().Size());
			Pawn->LetGoCling();
		});
		After(Mode, 11.1f, [Pawn]()
		{
			Pawn->Fire();
		});
		After(Mode, 11.6f, [Mode, Pawn, WeakSocket, PlateBody, Out]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [let go, sniper +0.5 s]: speed %.0f cm/s"), Pawn->GetBodyVelocity().Size());
			// What death must keep and what it must drop: the suit, a strap line wall - plate and one on the belt,
			// the torch lit, the hook in the hull.
			for (TActorIterator<AJamSuitLocker> Locker(Mode->GetWorld()); Locker; ++Locker)
			{
				Locker->CompleteInteraction(Pawn);
			}
			FHitResult Hit;
			const FVector From = WeakSocket->GetActorLocation() - Out * 30.0f;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullProbe), false, Pawn);
			Params.AddIgnoredActor(PlateBody->GetOwner());
			if (Mode->GetWorld()->LineTraceSingleByChannel(Hit, From, From - Out * 1200.0f, ECC_Visibility, Params))
			{
				Pawn->HullTools->AddLine(Hit.GetComponent(), Hit.ImpactPoint, PlateBody, PlateBody->GetComponentLocation());
				Pawn->HullTools->AddLine(Hit.GetComponent(), Hit.ImpactPoint, Pawn->Body, Pawn->Body->GetComponentLocation(), /*bBelt=*/true);
			}
			Pawn->SetHands(EJamHands::Tool, EJamHullTool::Welder);
			Pawn->HullTools->PrimaryPressed();
			Mode->Tools.AddUnique(MKJamGrappleTool());
			Pawn->ToggleGrapple();
		});
		const auto Kept = [Pawn]()
		{
			int32 Belt = 0;
			for (const FJamStrapLine& Line : Pawn->HullTools->GetLines())
			{
				Belt += Line.bBelt ? 1 : 0;
			}
			return FString::Printf(TEXT("suit %s, pistol %s, shotgun %s, sniper %s, strap lines %d (on the belt %d), torch '%s', hook %s, input %s"),
				MKHolderHasKey(Pawn, MKJamSuitKey()) ? TEXT("yes") : TEXT("NO"), Pawn->Guns->IsWeaponUnlocked(0) ? TEXT("unlocked") : TEXT("locked"),
				Pawn->Guns->IsWeaponUnlocked(1) ? TEXT("unlocked") : TEXT("locked"), Pawn->Guns->IsWeaponUnlocked(2) ? TEXT("unlocked") : TEXT("locked"),
				Pawn->HullTools->GetLines().Num(), Belt, *Pawn->HullTools->GetWeldStatus().ToString(), Pawn->GetGrappled() ? TEXT("in") : TEXT("off"),
				Pawn->InputEnabled() ? TEXT("on") : TEXT("off"));
		};
		After(Mode, 11.7f, [Pawn, Kept]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [before death]: %s"), *Kept());
			Pawn->Vitals->ApplyIncoming(100000.0f, EMKImpactKind::Blunt, /*bBlocked=*/false, FGuid());
		});
		After(Mode, 11.8f, [Pawn, Kept]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [dead +0.1 s]: HP %.0f, %s"), Pawn->Vitals->Health, *Kept());
		});
		After(Mode, 16.0f, [Mode, Pawn, Kept, Report]()
		{
			const AJamZeroGPawn* Now = PlayerPawn(Mode->GetWorld());
			const AJamGravityZone* Zone = Now ? Mode->ZoneOf(Now) : nullptr;
			const AActor* Start = nullptr;
			for (TActorIterator<APlayerStart> It(Mode->GetWorld()); It; ++It)
			{
				Start = It->ActorHasTag(TEXT("Cabin")) ? *It : Start;
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [after death +4.3 s]: pawn %s (%s), at %s, zone %s, %s, %.0f cm from the Cabin start, HP %.0f, O2 %.0f, %s"),
				*GetNameSafe(Now), Now == Pawn ? TEXT("the same") : TEXT("ANOTHER"), Now ? *Now->GetActorLocation().ToCompactString() : TEXT("-"),
				Zone ? *Zone->GetName() : TEXT("none"), Now && Now->IsGrounded() ? TEXT("standing") : TEXT("NOT standing"),
				Start && Now ? FVector::Dist(Start->GetActorLocation(), Now->GetActorLocation()) : -1.0, Pawn->Vitals->Health, Pawn->Oxygen->Seconds, *Kept());
			Report(TEXT("after the respawn"));
		});
		if (!Spare)
		{
			return;
		}
		const TWeakObjectPtr<AJamPlateSocket> Next(Sockets[(Index + 1) % Sockets.Num()]);
		After(Mode, 16.2f, [Next, Spare]()
		{
			UPrimitiveComponent* Body = Cast<UPrimitiveComponent>(Spare->GetRootComponent());
			const FTransform At = Next->GetActorTransform();
			Body->SetWorldLocationAndRotation(At.TransformPositionNoScale(FVector(0.0f, 0.0f, Next->SurfaceZ + 10.0f + 40.0f)), At.GetRotation(),
				false, nullptr, ETeleportType::TeleportPhysics);
			Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Body->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector);
		});
		After(Mode, 16.6f, [Next]()
		{
			const FVector Torch = Next->GetActorTransform().TransformPositionNoScale(FVector(0.0f, -Next->HoleHalfCm.Y, Next->SurfaceZ + 20.0f));
			const EJamWeldResult Result = Next->Weld(Torch, 0.05f, false);
			UE_LOG(LogMindKore, Log, TEXT("Hull probe [%s, plate 40 cm off, no junk]: %s, seam %.0f%%"), *Next->GetName(),
				Result == EJamWeldResult::Gap ? TEXT("refused: gap") : Result == EJamWeldResult::Welded ? TEXT("WELDED") : Result == EJamWeldResult::Junk ? TEXT("refused: junk") : TEXT("off the seam"),
				Next->GetSeamFraction() * 100.0f);
		});
	}

	FAutoConsoleCommandWithWorldAndArgs GHullProbe(
		TEXT("jam.Hull.Probe"),
		TEXT("jam.Hull.Probe [Index]: hull repair logic on one breach - strap, junk, weld, tack, seal, cut, cling, death and the revival in the cabin (what it keeps and drops)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHullProbe));

	/**
	 * jam.Hull.Stills [Index]: rendered stills (real RHI) of the suit projector's weld frame on
	 * Socket_Breach<Index>, from 2 m and 8 m, in five states: a plate strapped with a gap (one edge
	 * lifted, a taut and a slack line, junk inside), the plate flush with junk inside (a line at its
	 * limit), tacked, half welded, sealed. The plate is held still for the stills (physics off), so
	 * each state is posed, not raced to; the weld goes through AJamPlateSocket::Weld as the torch
	 * does. Junk is removed after the second state: one breach a run. Last, ProfileGPU with the
	 * frame lit and with the view turned away.
	 */
	void RunHullStills(const TArray<FString>& Args, UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		AJamZeroGPawn* Pawn = PlayerPawn(World);
		TArray<AJamPlateSocket*> Sockets;
		for (TActorIterator<AJamPlateSocket> It(World); It; ++It)
		{
			Sockets.Add(*It);
		}
		Sockets.Sort([](const AJamPlateSocket& A, const AJamPlateSocket& B) { return A.GetName() < B.GetName(); });
		TArray<AActor*> Plates = Tagged(World, TEXT("Hull.Plate"));
		Plates.Sort([](const AActor& A, const AActor& B) { return A.GetName() < B.GetName(); });
		const int32 Index = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 0;
		if (!Mode || !Pawn || !Sockets.IsValidIndex(Index) || Plates.IsEmpty())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Hull stills: needs the ship map, the pawn, a breach %d and a Hull.Plate"), Index);
			return;
		}
		const TWeakObjectPtr<AJamPlateSocket> WeakSocket(Sockets[Index]);
		const FTransform Frame = Sockets[Index]->GetActorTransform();
		const FVector Out = Frame.GetUnitAxis(EAxis::Z);
		const float SurfaceZ = Sockets[Index]->SurfaceZ;
		UPrimitiveComponent* PlateBody = Cast<UPrimitiveComponent>(Plates[Index % Plates.Num()]->GetRootComponent());
		PlateBody->SetSimulatePhysics(false);
		UE_LOG(LogMindKore, Log, TEXT("Hull stills: %s, out %s, plate %s, junk inside %d"), *Sockets[Index]->GetName(), *Out.ToCompactString(),
			*PlateBody->GetOwner()->GetName(), Sockets[Index]->JunkCount());

		// The plate: flush, or turned about its edge at -Y so the edge at +Y stands LiftCm off the hull.
		const auto PosePlate = [Frame, SurfaceZ, PlateBody](float LiftCm)
		{
			const float Angle = FMath::Atan2(LiftCm, 300.0f);
			const FQuat Turn(FVector::XAxisVector, Angle);
			const FVector Pivot(0.0f, -150.0f, SurfaceZ);
			const FVector Centre = Pivot + Turn.RotateVector(FVector(0.0f, 150.0f, 10.0f));
			// Which way the turn lifts is checked in the log by the gaps it leaves.
			PlateBody->SetWorldLocationAndRotation(Frame.TransformPositionNoScale(Centre), Frame.GetRotation() * Turn, false, nullptr, ETeleportType::TeleportPhysics);
		};
		// The eye Distance from the breach centre, 35 degrees off its normal, looking at it.
		const auto Place = [Pawn, Frame, Out, SurfaceZ](float Distance)
		{
			const FVector Centre = Frame.TransformPositionNoScale(FVector(0.0f, 0.0f, SurfaceZ));
			// Along the hull (X): across it the ship's other faces stand in the way of the far views.
			const FVector Eye = Centre + (Out * FMath::Cos(FMath::DegreesToRadians(35.0f)) + Frame.GetUnitAxis(EAxis::X) * FMath::Sin(FMath::DegreesToRadians(35.0f))) * Distance;
			const FVector CameraOffset = Pawn->Camera->GetComponentLocation() - Pawn->GetActorLocation();
			Teleport(Pawn, Eye - CameraOffset);
			if (APlayerController* Controller = Cast<APlayerController>(Pawn->GetController()))
			{
				Controller->SetControlRotation((Centre - Eye).Rotation());
			}
		};
		const auto Shot = [Pawn, WeakSocket](const FString& Name)
		{
			const UJamWeldProjectorComponent* Projector = Pawn->WeldProjector;
			const AJamPlateSocket* S = WeakSocket.Get();
			UE_LOG(LogMindKore, Log, TEXT("Hull stills [%s]: projector %s (core %s) on %s, %.0f cd, star %.2f lux, %d boxes, picture [%s]; seam %.0f%%, coverage %.0f%%, tacked %s, sealed %s, junk %d, eye %.0f cm"),
				*Name, Projector->IsLit() ? TEXT("lit") : TEXT("DARK"), Projector->IsCoreOn() ? TEXT("on") : TEXT("off"), *GetNameSafe(Projector->GetTarget()), Projector->Candelas, Projector->StarLux,
				Projector->GetBoxCount(), *FString::Join(Projector->GetPictureLines(), TEXT(" | ")), S->GetSeamFraction() * 100.0f, S->GetCoverage() * 100.0f,
				S->IsTacked() ? TEXT("yes") : TEXT("no"), S->bFilled ? TEXT("yes") : TEXT("no"), S->JunkCount(),
				FVector::Dist(Pawn->Camera->GetComponentLocation(), S->GetActorLocation()));
			Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(TEXT("HighResShot 1"));
		};
		// A line from the hull outer face beside the breach (Along cm from its centre on X) to a point on the plate.
		const auto Strap = [Pawn, Frame, SurfaceZ, PlateBody](float Along, const FVector& OnPlateLocal, float StretchCm)
		{
			const FVector Above = Frame.TransformPositionNoScale(FVector(Along, 0.0f, SurfaceZ + 50.0f));
			FHitResult Hit;
			FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullStills), false, Pawn);
			Params.AddIgnoredActor(PlateBody->GetOwner());
			if (!Pawn->GetWorld()->LineTraceSingleByChannel(Hit, Above, Above - Frame.GetUnitAxis(EAxis::Z) * 100.0f, ECC_Visibility, Params) || !Hit.GetComponent())
			{
				UE_LOG(LogMindKore, Warning, TEXT("Hull stills: no hull under %s to anchor a line"), *Above.ToCompactString());
				return;
			}
			const FVector OnPlate = PlateBody->GetComponentTransform().TransformPositionNoScale(OnPlateLocal);
			const int32 Line = Pawn->HullTools->AddLine(Hit.GetComponent(), Hit.ImpactPoint, PlateBody, OnPlate);
			Pawn->HullTools->SetLineLength(Line, float(FVector::Dist(Hit.ImpactPoint, OnPlate)) - StretchCm);
		};
		const auto WeldTo = [WeakSocket, Frame, SurfaceZ](float Fraction)
		{
			// The torch along the contour, in the socket order of pieces, up to Fraction of it.
			AJamPlateSocket* S = WeakSocket.Get();
			const TArray<FVector2D>& Centres = S->GetSegmentCentres();
			const int32 Count = FMath::RoundToInt32(Centres.Num() * Fraction);
			int32 Welded = 0;
			for (int32 Piece = 0; Piece < Count; ++Piece)
			{
				const FVector Point = Frame.TransformPositionNoScale(FVector(Centres[Piece].X, Centres[Piece].Y, SurfaceZ + 20.0f));
				for (int32 Pass = 0; Pass < 4; ++Pass)
				{
					Welded += S->Weld(Point, 0.05f, false) == EJamWeldResult::Welded ? 1 : 0;
				}
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull stills: welded to %.0f%% of the contour, %d torch steps took, seam %.0f%%"), Fraction * 100.0f, Welded, S->GetSeamFraction() * 100.0f);
		};
		const auto Gaps = [WeakSocket](const TCHAR* Stage)
		{
			const AJamPlateSocket* S = WeakSocket.Get();
			int32 Count = 0;
			FString Runs;
			const TArray<float>& Gap = S->GetGaps();
			const TArray<FVector2D>& Normals = S->GetSegmentNormals();
			for (int32 Piece = 0; Piece < Gap.Num(); ++Piece)
			{
				if (FMath::Abs(Gap[Piece]) > S->MaxGapCm)
				{
					++Count;
					if (Piece == 0 || FMath::Abs(Gap[Piece - 1]) <= S->MaxGapCm)
					{
						Runs += FString::Printf(TEXT(" from %d (normal %s)"), Piece, *Normals[Piece].ToString());
					}
				}
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull stills [%s]: %d of %d pieces on a gap:%s"), Stage, Count, Gap.Num(), *Runs);
		};

		if (Args.Num() > 1 && Args[1] == TEXT("plates"))
		{
			// Plates: the frame with empty hands, then with a plate on the belt strap - far and unaligned, near
			// and misaligned, a plate too small, and the fitting one aligned over the hole.
			UPrimitiveComponent* Small = Cast<UPrimitiveComponent>(Plates[(Index + 1) % Plates.Num()]->GetRootComponent());
			if (Small == PlateBody)
			{
				UE_LOG(LogMindKore, Warning, TEXT("Hull stills: plates mode needs two Hull.Plate bodies"));
				return;
			}
			Small->SetSimulatePhysics(false);
			const auto Pose = [Frame, SurfaceZ](UPrimitiveComponent* Body, const FVector& Local, float YawDeg)
			{
				// Local: the plate's centre in the breach frame; its 20 cm turned flat to the hull, YawDeg about the normal.
				const FQuat Turn(FVector::ZAxisVector, FMath::DegreesToRadians(YawDeg));
				Body->SetWorldLocationAndRotation(Frame.TransformPositionNoScale(Local + FVector(0.0f, 0.0f, SurfaceZ)), Frame.GetRotation() * Turn,
					false, nullptr, ETeleportType::TeleportPhysics);
			};
			// On a slack strap from the hull beside the breach: framed as a plate on its way, and nothing pulls the pawn.
			const auto Belt = [Pawn, Frame, SurfaceZ](UPrimitiveComponent* Body)
			{
				Pawn->HullTools->CutAll();
				const FVector Above = Frame.TransformPositionNoScale(FVector(300.0f, 0.0f, SurfaceZ + 50.0f));
				FHitResult Hit;
				FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullStills), false, Pawn);
				Params.AddIgnoredActor(Body->GetOwner());
				if (Pawn->GetWorld()->LineTraceSingleByChannel(Hit, Above, Above - Frame.GetUnitAxis(EAxis::Z) * 100.0f, ECC_Visibility, Params) && Hit.GetComponent())
				{
					const int32 Line = Pawn->HullTools->AddLine(Hit.GetComponent(), Hit.ImpactPoint, Body, Body->GetComponentLocation());
					Pawn->HullTools->SetLineLength(Line, float(FVector::Dist(Hit.ImpactPoint, Body->GetComponentLocation())) + 20.0f);
				}
			};
			const auto Report = [Pawn](const TCHAR* Name)
			{
				UE_LOG(LogMindKore, Log, TEXT("Hull plates [%s]: projector %s, picture [%s]"), Name, Pawn->WeldProjector->IsLit() ? TEXT("lit") : TEXT("DARK"),
					*FString::Join(Pawn->WeldProjector->GetPictureLines(), TEXT(" | ")));
				Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(TEXT("HighResShot 1"));
			};
			After(Mode, 0.5f, [Mode, Pose, PlateBody, Small, Place]()
			{
				for (TActorIterator<AActor> It(Mode->GetWorld()); It; ++It)
				{
					if (It->ActorHasTag(TEXT("Junk.Foreign")))
					{
						It->Destroy();
					}
				}
				Pose(PlateBody, FVector(0.0f, 0.0f, 3000.0f), 0.0f);
				// The small one: 2 x 2 m, too small for the 2.5 m hole.
				Small->SetWorldScale3D(Small->GetComponentScale() * FVector(2.0f / 3.0f, 2.0f / 3.0f, 1.0f));
				Pose(Small, FVector(0.0f, 0.0f, 3400.0f), 0.0f);
				Place(300.0f);
			});
			After(Mode, 1.8f, [Place]() { Place(300.0f); });
			After(Mode, 3.0f, [Report]() { Report(TEXT("empty hands, 3 m")); });
			After(Mode, 3.3f, [Pose, Belt, PlateBody, Place]() { Pose(PlateBody, FVector(60.0f, -40.0f, 260.0f), 20.0f); Belt(PlateBody); Place(450.0f); });
			After(Mode, 5.0f, [Report]() { Report(TEXT("fitting plate on the strap, far: 2.5 m off the hull, turned 20 deg, eye 4.5 m")); });
			After(Mode, 5.3f, [Pose, PlateBody, Place]() { Pose(PlateBody, FVector(45.0f, 30.0f, 40.0f), 12.0f); Place(250.0f); });
			After(Mode, 7.0f, [Report]() { Report(TEXT("fitting plate near but misaligned: 45/30 cm off, turned 12 deg, eye 2.5 m")); });
			After(Mode, 7.3f, [Pose, Belt, PlateBody, Small, Place]() { Pose(PlateBody, FVector(0.0f, 0.0f, 3000.0f), 0.0f); Pose(Small, FVector(0.0f, 0.0f, 30.0f), 0.0f); Belt(Small); Place(250.0f); });
			After(Mode, 9.0f, [Report]() { Report(TEXT("too small plate (2 x 2 m) over the hole, eye 2.5 m")); });
			After(Mode, 9.3f, [Pose, Belt, PlateBody, Small, Place]() { Pose(Small, FVector(0.0f, 0.0f, 3400.0f), 0.0f); Pose(PlateBody, FVector(5.0f, -4.0f, 11.0f), 2.0f); Belt(PlateBody); Place(250.0f); });
			After(Mode, 11.0f, [Report]() { Report(TEXT("fitting plate aligned and covering, near, eye 2.5 m")); });
			After(Mode, 11.3f, [Place]() { Place(600.0f); });
			After(Mode, 12.5f, [Report]() { Report(TEXT("fitting plate aligned and covering, eye 6 m")); });
			After(Mode, 13.0f, []() { UE_LOG(LogMindKore, Log, TEXT("Hull stills: done")); });
			return;
		}

		if (Args.Num() > 1 && Args[1] == TEXT("strips"))
		{
			// Strips: the frame half welded from 3 m; its appear held every 33 ms, then a second of its tremble.
			After(Mode, 0.5f, [Mode, Pawn, PosePlate, Place]()
			{
				for (TActorIterator<AActor> It(Mode->GetWorld()); It; ++It)
				{
					if (It->ActorHasTag(TEXT("Junk.Foreign")))
					{
						It->Destroy();
					}
				}
				PosePlate(0.0f);
				Place(300.0f);
			});
			After(Mode, 1.0f, [WeldTo]() { WeldTo(0.5f); });
			After(Mode, 1.3f, [Place]() { Place(300.0f); });
			After(Mode, 3.5f, [Pawn]() { Pawn->WeldProjector->HoldClock(0.0); Pawn->WeldProjector->Replay(); });
			const FString Tag = FString::Printf(TEXT("s%d"), Index);
			float At = 3.7f;
			for (int32 Frame30 = 0; Frame30 < 30; ++Frame30, At += 0.2f)
			{
				After(Mode, At, [Pawn, Frame30]() { Pawn->WeldProjector->HoldClock(Frame30 / 30.0); });
				After(Mode, At + 0.07f, [Pawn, Tag, Frame30]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Hull strips [appear %s +%d ms]: %s"), *Tag, FMath::RoundToInt32(Frame30 * 1000.0f / 30.0f),
						*FString::Join(Pawn->WeldProjector->GetPictureLines(), TEXT(" | ")));
					Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=WeldAppear_%s_%04d"), *Tag, FMath::RoundToInt32(Frame30 * 1000.0f / 30.0f)));
				});
			}
			for (int32 Frame30 = 0; Frame30 < 30; ++Frame30, At += 0.2f)
			{
				After(Mode, At, [Pawn, Frame30]() { Pawn->WeldProjector->HoldClock(2.5 + Frame30 / 30.0); });
				After(Mode, At + 0.07f, [Pawn, Tag, Frame30]()
				{
					Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=WeldTremble_%s_%04d"), *Tag, FMath::RoundToInt32(Frame30 * 1000.0f / 30.0f)));
				});
			}
			After(Mode, At + 0.3f, [Pawn]() { Pawn->WeldProjector->HoldClock(-1.0); UE_LOG(LogMindKore, Log, TEXT("Hull stills: done")); });
			return;
		}

		// 1: strapped with a gap - the +Y edge 12 cm off, a taut line to it and a slack one, junk inside.
		After(Mode, 0.5f, [PosePlate, Place, Strap]()
		{
			PosePlate(12.0f);
			Strap(280.0f, FVector(0.0f, 140.0f, 10.0f), 10.0f);
			Strap(-280.0f, FVector(-140.0f, 0.0f, 10.0f), -40.0f);
			Place(200.0f);
		});
		// Again: at 0.5 s the pawn may still stand in the cabin gravity, its camera elsewhere on the body.
		After(Mode, 1.8f, [Place]() { Place(200.0f); });
		After(Mode, 2.6f, [Gaps]() { Gaps(TEXT("gap")); });
		After(Mode, 2.7f, [Shot]() { Shot(TEXT("gap, 2 m")); });
		After(Mode, 3.2f, [Place]() { Place(800.0f); });
		After(Mode, 3.9f, [Shot]() { Shot(TEXT("gap, 8 m")); });
		// 2: flush, junk inside, a line at its limit.
		After(Mode, 4.4f, [Pawn, PosePlate, Place, Strap]()
		{
			Pawn->HullTools->CutAll();
			PosePlate(0.0f);
			Strap(280.0f, FVector(140.0f, 0.0f, 10.0f), 22.0f);
			Place(200.0f);
		});
		After(Mode, 5.4f, [Gaps]() { Gaps(TEXT("junk")); });
		After(Mode, 5.5f, [Shot]() { Shot(TEXT("junk inside, 2 m")); });
		After(Mode, 6.0f, [Place]() { Place(800.0f); });
		After(Mode, 6.7f, [Shot]() { Shot(TEXT("junk inside, 8 m")); });
		// 3: junk out, tacked (30%), one slack line left.
		After(Mode, 7.2f, [Mode, Pawn, Strap, Place]()
		{
			int32 Removed = 0;
			for (TActorIterator<AActor> It(Mode->GetWorld()); It; ++It)
			{
				if (It->ActorHasTag(TEXT("Junk.Foreign")))
				{
					It->Destroy();
					++Removed;
				}
			}
			UE_LOG(LogMindKore, Log, TEXT("Hull stills: %d junk removed"), Removed);
			Pawn->HullTools->CutAll();
			Strap(-280.0f, FVector(-140.0f, 0.0f, 10.0f), -40.0f);
			Place(200.0f);
		});
		After(Mode, 7.6f, [WeldTo]() { WeldTo(0.3f); });
		After(Mode, 9.4f, [Shot]() { Shot(TEXT("tacked, 2 m")); });
		After(Mode, 9.9f, [Place]() { Place(800.0f); });
		After(Mode, 10.6f, [Shot]() { Shot(TEXT("tacked, 8 m")); });
		// 4: half welded, the last bead still hot at 2 m.
		After(Mode, 11.0f, [Place]() { Place(200.0f); });
		After(Mode, 11.6f, [WeldTo]() { WeldTo(0.5f); });
		After(Mode, 11.9f, [Shot]() { Shot(TEXT("half welded, 2 m")); });
		After(Mode, 12.4f, [Place]() { Place(800.0f); });
		After(Mode, 13.1f, [Shot]() { Shot(TEXT("half welded, 8 m")); });
		// 5: sealed, cold: the ring beats.
		After(Mode, 13.5f, [Place, WeldTo]() { WeldTo(1.0f); Place(200.0f); });
		After(Mode, 13.75f, [Shot]() { Shot(TEXT("sealed, its pulse +0.25 s, 2 m")); });
		After(Mode, 15.2f, [Shot]() { Shot(TEXT("sealed, 2 m")); });
		After(Mode, 15.7f, [Place]() { Place(800.0f); });
		After(Mode, 16.4f, [Shot]() { Shot(TEXT("sealed, 8 m")); });
		// The cost: ProfileGPU with the frame lit at 2 m, then with the view turned away (the projector dark).
		After(Mode, 17.0f, [Place]() { Place(200.0f); });
		After(Mode, 18.0f, [Pawn]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull stills: ProfileGPU, projector %s"), Pawn->WeldProjector->IsLit() ? TEXT("lit") : TEXT("dark"));
			Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(TEXT("ProfileGPU"));
		});
		After(Mode, 19.0f, [Pawn, Out]()
		{
			Cast<APlayerController>(Pawn->GetController())->SetControlRotation(Out.Rotation());
		});
		After(Mode, 20.5f, [Pawn]()
		{
			UE_LOG(LogMindKore, Log, TEXT("Hull stills: ProfileGPU, projector %s"), Pawn->WeldProjector->IsLit() ? TEXT("lit") : TEXT("dark"));
			Cast<APlayerController>(Pawn->GetController())->ConsoleCommand(TEXT("ProfileGPU"));
		});
		After(Mode, 21.5f, []() { UE_LOG(LogMindKore, Log, TEXT("Hull stills: done")); });
	}

	FAutoConsoleCommandWithWorldAndArgs GHullStills(
		TEXT("jam.Hull.Stills"),
		TEXT("jam.Hull.Stills [Index]: rendered stills of the suit projector weld frame on one breach - strapped with a gap, junk inside, tacked, half welded, sealed - from 2 m and 8 m, then ProfileGPU lit and dark (HighResShot; real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunHullStills));

	/** jam.Ship.PlanetShots: two stills standing on the planetoid (top and side). */
	void RunPlanetShots(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		APlayerController* PlayerController = World->GetFirstPlayerController();
		if (!Mode || !Shell || !PlayerController)
		{
			return;
		}
		const FVector Centre = Shell->GetActorLocation();
		const float Radius = Shell->SphereRadiusCm - 650.0f;
		const FVector Spots[2] = { Centre + FVector::UpVector * Radius, Centre - FVector::YAxisVector * Radius };
		for (int32 Index = 0; Index < 2; ++Index)
		{
			const FVector Spot = Spots[Index];
			After(Mode, 1.0f + Index * 6.0f, [PlayerController, Spot]()
			{
				Teleport(PlayerController->GetPawn(), Spot);
				PlayerController->SetControlRotation(FRotator(-5.0f, 0.0f, 0.0f));
			});
			After(Mode, 5.5f + Index * 6.0f, [PlayerController, Index]()
			{
				LogState(Index == 0 ? TEXT("planet shot top") : TEXT("planet shot side"), Cast<AJamZeroGPawn>(PlayerController->GetPawn()));
				PlayerController->ConsoleCommand(TEXT("HighResShot 1"));
			});
		}
	}

	FAutoConsoleCommandWithWorld GPlanetShots(
		TEXT("jam.Ship.PlanetShots"),
		TEXT("Two HighResShot stills standing on the planetoid, top and side (needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunPlanetShots));

	/** The first living wall with this tag (crater membranes: Jam.Crater, cave plugs: Jam.CavePlug), or the named one. */
	AJamLivingWall* TaggedWall(UWorld* World, FName Tag, const FString& Name = FString())
	{
		for (TActorIterator<AJamLivingWall> It(World); It; ++It)
		{
			if (Name.IsEmpty() ? It->ActorHasTag(Tag) : It->GetName() == Name)
			{
				return *It;
			}
		}
		return nullptr;
	}

	/** Take the shotgun, look at Target through the gravity-framed controller, fire. */
	void ShotgunAt(AJamZeroGPawn* Pawn, const FVector& Target)
	{
		Pawn->Guns->UnlockWeapon(1);
		Pawn->Guns->FinishSwitch();
		Pawn->GetController()->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
		if (!Pawn->Fire())
		{
			UE_LOG(LogMindKore, Warning, TEXT("Caves probe: %s did not fire"), *Pawn->Guns->Describe());
		}
	}

	/** Signed height of the pawn over a wall's plane along its front normal, cm. */
	float OverPlane(const AJamLivingWall* Wall, const AJamZeroGPawn* Pawn)
	{
		return FVector::DotProduct(Pawn->GetActorLocation() - Wall->GetActorLocation(), Wall->GetFrontNormal());
	}

	/**
	 * (b) A cave plug: the pawn 5 m in front on the tunnel floor walks at it -
	 * the whole plug stops it; then a shotgun blast straight ahead at eye level
	 * and the same walk: through the hole before it closes.
	 */
	void ProbePlug(AJamShipGameMode* Mode, AJamLivingWall* Plug)
	{
		const FVector Front = Plug->GetFrontNormal();
		const FVector Up = Plug->GetActorUpVector();
		const FVector Centre = Plug->GetActorLocation();
		const FVector Start = Centre + Front * 500.0f - Up * 130.0f;
		const auto Walk = [Mode, Plug, Start, Front, Centre](const TCHAR* Label, bool bShoot, TFunction<void()> Next)
		{
			Teleport(PlayerPawn(Mode->GetWorld()), Start);
			const FString Name = Label;
			After(Mode, 1.2f, [Mode, Plug, Front, Centre, Name, bShoot, Next]()
			{
				AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
				// As a player would: straight ahead at eye level.
				const FVector Eye = Pawn->Camera->GetComponentLocation();
				const FVector Aim = Eye - Front * FVector::DotProduct(Eye - Centre, Front);
				LogState(*FString::Printf(TEXT("plug %s: start"), *Name), Pawn);
				if (bShoot)
				{
					ShotgunAt(Pawn, Aim);
				}
				Pawn->GetController()->SetControlRotation((Aim - Pawn->Camera->GetComponentLocation()).Rotation());
				const float HP = Pawn->Vitals->Health;
				After(Mode, 0.15f, [Mode, Plug, Name, HP, Next]()
				{
					UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug %s]: open cells %d, clear radius at the hole %.0f cm; walking at it for 3 s"),
						*Name, Plug->GetOpenCellCount(), Plug->ClearRadiusAt(Plug->GetLastWoundCentre()));
					HoldInput(Mode, FVector(1.0f, 0.0f, 0.0f), 3.0f);
					for (int32 Step = 1; Step <= 6; ++Step)
					{
						After(Mode, Step * 0.5f, [Mode, Plug, Name, Step]()
						{
							const AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
							const FVector Local = Plug->GetActorTransform().InverseTransformPosition(Pawn->GetActorLocation());
							UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug %s +%.1f s]: %.0f cm in front, %.0f cm across, %.0f cm over the axis, %s, speed %.0f cm/s"), *Name, Step * 0.5f,
								Local.Y, Local.X, Local.Z, Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"), Pawn->GetBodyVelocity().Size());
						});
					}
					After(Mode, 3.2f, [Mode, Plug, Name, HP, Next]()
					{
						const AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
						const float Over = OverPlane(Plug, Pawn);
						UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug %s]: through %s - the pawn is %.0f cm %s the plug, %s, HP %.0f -> %.0f, open cells %d"),
							*Name, Over < -(Plug->GetHalfDepthCm() + Pawn->Body->GetScaledSphereRadius()) ? TEXT("yes") : TEXT("no"),
							FMath::Abs(Over), Over < 0.0f ? TEXT("behind") : TEXT("in front of"),
							Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"), HP, Pawn->Vitals->Health, Plug->GetOpenCellCount());
						Next();
					});
				});
			});
		};
		UE_LOG(LogMindKore, Log, TEXT("Caves probe [plug]: %s at %s, front %s, up %s"), *Plug->GetName(), *Centre.ToCompactString(),
			*Front.ToCompactString(), *Up.ToCompactString());
		Walk(TEXT("control, no shot"), false, [Mode, Walk]()
		{
			// The weave must be whole again before the real try (the bump may have torn a cell).
			After(Mode, 3.0f, [Walk]() { Walk(TEXT("after a shotgun blast"), true, []() { UE_LOG(LogMindKore, Log, TEXT("Caves probe: done")); }); });
		});
	}

	/**
	 * (a) A crater: the pawn 2.5 m over the membrane in radial gravity, the
	 * shotgun straight down; logs the height over the membrane's plane every
	 * 0.25 s for 5 s: does it drop through the hole and land in the tunnel below?
	 */
	void ProbeCrater(AJamShipGameMode* Mode, AJamLivingWall* Crater, TFunction<void()> Next)
	{
		const FVector Up = Crater->GetFrontNormal();
		const FVector Centre = Crater->GetActorLocation();
		AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
		UE_LOG(LogMindKore, Log, TEXT("Caves probe [crater]: %s at %s, up %s (radial up there %s)"), *Crater->GetName(), *Centre.ToCompactString(),
			*Up.ToCompactString(), *(Centre - PlanetShell(Mode->GetWorld())->GetActorLocation()).GetSafeNormal().ToCompactString());
		Teleport(Pawn, Centre + Up * 250.0f);
		After(Mode, 0.25f, [Mode, Crater, Centre]()
		{
			AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
			LogState(TEXT("crater: over the membrane"), Pawn);
			ShotgunAt(Pawn, Centre);
		});
		const TSharedRef<float> Lowest = MakeShared<float>(TNumericLimits<float>::Max());
		for (int32 Step = 1; Step <= 20; ++Step)
		{
			After(Mode, 0.25f + Step * 0.25f, [Mode, Crater, Step, Lowest, Next]()
			{
				const AJamZeroGPawn* Pawn = PlayerPawn(Mode->GetWorld());
				const float Over = OverPlane(Crater, Pawn);
				*Lowest = FMath::Min(*Lowest, Over);
				UE_LOG(LogMindKore, Log, TEXT("Caves probe [crater +%.2f s]: %.0f cm %s the membrane, %s, speed %.0f cm/s, open cells %d"), Step * 0.25f,
					FMath::Abs(Over), Over >= 0.0f ? TEXT("over") : TEXT("under"), Pawn->IsInGravity() ? (Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air")) : TEXT("zero-G"),
					Pawn->GetBodyVelocity().Size(), Crater->GetOpenCellCount());
				if (Step == 20)
				{
					const bool bThrough = *Lowest < -(Crater->GetHalfDepthCm() + Pawn->Body->GetScaledSphereRadius());
					UE_LOG(LogMindKore, Log, TEXT("Caves probe [crater]: dropped through %s - lowest %.0f cm under the membrane, now %s, HP %.0f, membrane open cells %d"),
						bThrough ? TEXT("yes") : TEXT("no"), -*Lowest, Pawn->IsGrounded() ? TEXT("on the ground") : TEXT("in the air"), Pawn->Vitals->Health,
						Crater->GetOpenCellCount());
					Next();
				}
			});
		}
	}

	/** jam.Ship.Caves [Crater] [Plug]: a crater membrane opened by the shotgun and the drop through it; a cave plug walked into, then shot and walked through. */
	void RunShipCaves(const TArray<FString>& Args, UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		AJamLivingWall* Crater = TaggedWall(World, TEXT("Jam.Crater"), Args.Num() > 0 ? Args[0] : FString());
		AJamLivingWall* Plug = TaggedWall(World, TEXT("Jam.CavePlug"), Args.Num() > 1 ? Args[1] : FString());
		if (!Mode || !PlayerPawn(World) || !Crater || !Plug || !PlanetShell(World))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Caves probe: needs the ship mode, the pawn, the planetoid shell, a Jam.Crater and a Jam.CavePlug wall"));
			return;
		}
		ProbeCrater(Mode, Crater, [Mode, Plug]() { After(Mode, 1.0f, [Mode, Plug]() { ProbePlug(Mode, Plug); }); });
	}

	FAutoConsoleCommandWithWorldAndArgs GShipCaves(
		TEXT("jam.Ship.Caves"),
		TEXT("jam.Ship.Caves [Crater] [Plug]: shotgun a crater membrane and drop through; walk at a cave plug, then shoot it and walk through."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunShipCaves));

	/**
	 * jam.Ship.CaveShots: rendered stills (real RHI) - the planet from afar,
	 * standing on the relief, a crater membrane before and 0.35 s after a blast
	 * from its rim, a cave plug from 6 m down its tunnel.
	 */
	void RunCaveShots(UWorld* World)
	{
		AJamShipGameMode* Mode = World->GetAuthGameMode<AJamShipGameMode>();
		const AJamGravityZone* Shell = PlanetShell(World);
		APlayerController* Controller = World->GetFirstPlayerController();
		AJamLivingWall* Crater = TaggedWall(World, TEXT("Jam.Crater"));
		AJamLivingWall* Plug = TaggedWall(World, TEXT("Jam.CavePlug"));
		if (!Mode || !Shell || !Controller || !Crater || !Plug)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Cave shots: needs the ship mode, the planetoid, a crater and a plug"));
			return;
		}
		const FVector Centre = Shell->GetActorLocation();
		const auto Place = [Controller](const FVector& Where, const FVector& Target)
		{
			Teleport(Controller->GetPawn(), Where);
			Controller->SetControlRotation((Target - Where).Rotation());
		};
		const auto Shot = [Controller](const TCHAR* Name)
		{
			LogState(Name, Cast<AJamZeroGPawn>(Controller->GetPawn()));
			UE_LOG(LogMindKore, Log, TEXT("Cave shots: %s"), Name);
			Controller->ConsoleCommand(TEXT("HighResShot 1"));
		};
		const auto Look = [Controller](const FVector& Target)
		{
			if (const APawn* Pawn = Controller->GetPawn())
			{
				Controller->SetControlRotation((Target - Cast<AJamZeroGPawn>(Pawn)->Camera->GetComponentLocation()).Rotation());
			}
		};
		// 0: from afar (zero-G, 160 m out toward the ship side).
		const FVector Far = Centre + FVector(-0.3f, -1.0f, 0.35f).GetSafeNormal() * 25000.0f;
		After(Mode, 1.0f, [Place, Far, Centre]() { Place(Far, Centre); });
		After(Mode, 9.0f, [Place, Far, Centre, Shot]() { Place(Far, Centre); Shot(TEXT("planet from afar")); });
		// 1: standing on the relief at the top, looking along +X.
		const FVector Top = Centre + FVector::UpVector * (Shell->SphereRadiusCm - 600.0f);
		After(Mode, 10.0f, [Place, Top]() { Place(Top, Top + FVector(1000.0f, 0.0f, -300.0f)); });
		After(Mode, 14.0f, [Shot]() { Shot(TEXT("on the relief")); });
		// 2, 3: a crater from its rim (8.5 m out), before and after a blast.
		const FVector Up = Crater->GetFrontNormal();
		const FVector Side = FVector::CrossProduct(Up, FVector::RightVector).GetSafeNormal();
		const FVector Rim = Crater->GetActorLocation() + Side * 850.0f + Up * 500.0f;
		const FVector Membrane = Crater->GetActorLocation();
		After(Mode, 15.0f, [Place, Rim, Membrane]() { Place(Rim, Membrane); });
		After(Mode, 19.0f, [Look, Membrane]() { Look(Membrane); });
		After(Mode, 19.3f, [Shot]() { Shot(TEXT("crater, whole")); });
		After(Mode, 21.0f, [Controller, Membrane]() { ShotgunAt(Cast<AJamZeroGPawn>(Controller->GetPawn()), Membrane); });
		After(Mode, 21.05f, [Look, Membrane]() { Look(Membrane); });
		After(Mode, 21.35f, [Shot]() { Shot(TEXT("crater, +0.35 s after the blast")); });
		// 4: a cave plug from 6 m down its tunnel.
		const FVector PlugFront = Plug->GetFrontNormal();
		const FVector PlugUp = Plug->GetActorUpVector();
		const FVector Stand = Plug->GetActorLocation() + PlugFront * 600.0f - PlugUp * 130.0f;
		const FVector PlugCentre = Plug->GetActorLocation();
		After(Mode, 24.0f, [Place, Stand, PlugCentre]() { Place(Stand, PlugCentre); });
		After(Mode, 27.5f, [Look, PlugCentre]() { Look(PlugCentre); });
		After(Mode, 28.0f, [Shot]() { Shot(TEXT("cave plug")); });
	}

	FAutoConsoleCommandWithWorld GCaveShots(
		TEXT("jam.Ship.CaveShots"),
		TEXT("Rendered stills of the planetoid: from afar, on the relief, a crater membrane before/after the shotgun, a cave plug (HighResShot; real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunCaveShots));

	FAutoConsoleCommandWithWorld GShipProbe(
		TEXT("jam.Ship.Probe"),
		TEXT("Ship: standing, walking and jumping in the cabin, the suit and the airlock, zero-G outside, the planetoid; a plate; every part in, the lever."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunShipProbe));
}

#undef LOCTEXT_NAMESPACE
