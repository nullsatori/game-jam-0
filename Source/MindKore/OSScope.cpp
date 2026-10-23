#include "OSScope.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicRHI.h"
#include "Engine/StaticMesh.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/HUD.h"
#include "GameFramework/PlayerController.h"
#include "OSLivingWall.h"
#include "OSWeapons.h"
#include "OSZeroGPawn.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "MindKore.h"
#include "TimerManager.h"

namespace
{
	TAutoConsoleVariable<int32> CVarScopeTrace(TEXT("os.Scope.Trace"), 0,
		TEXT("Log the scope's aim springs every tick (Scope trace: t, place, turn, alpha) - for plotting the curves."));
}

UOSScopeComponent::UOSScopeComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// Per channel: X, Y, Z, pitch, yaw, roll; { Hz, damping, delay s }. Owner 18.10: «насчет анимаций делай
	// все по-богатому, по взрослому». Raising: a dip (anticipation), then an arc - the channels staggered -
	// under-damped (low gravity: it floats past the eye and settles slowly), the turns floatier still and
	// lagging; aimed it breathes. Lowering: a beat later, heavier and lazier than the raise.
	FOSGunAim Pistol;
	// The USP (muzzle -X): the middle of its rear dots and the front dot (SightDotsCm), at arm's length -
	// 50 cm (owner 18.10: «прицел пистолета пиздец близко»), the gun small under the target; light and
	// quick, punched out, a wrist roll as it swings in.
	Pistol.SightRearCm = FVector(3.6f, -1.431f, 5.82f);
	Pistol.SightFrontCm = FVector(-13.303f, -1.4534f, 6.134f);
	Pistol.EyeCm = 50.0f;
	// The dot just over the front blade's top (its 1.8 mm insert sits mid-blade at the sight line).
	Pistol.ZeroOverFrontCm = 0.35f;
	Pistol.GunFieldOfView = 100.0f;
	Pistol.KickScale = 0.55f;
	Pistol.In = { { 3.0f, 0.55f, 0.08f }, { 3.2f, 0.6f, 0.06f }, { 3.4f, 0.5f, 0.0f }, { 2.6f, 0.45f, 0.0f }, { 2.6f, 0.45f, 0.0f }, { 2.2f, 0.36f, 0.0f } };
	Pistol.Out = { { 2.4f, 0.68f, 0.03f }, { 2.2f, 0.7f, 0.05f }, { 2.6f, 0.62f, 0.0f }, { 2.0f, 0.45f, 0.0f }, { 2.0f, 0.45f, 0.0f }, { 1.8f, 0.42f, 0.0f } };
	Pistol.LagDegPerCmPerSec = FVector(0.06f, 0.03f, 0.09f);
	Pistol.AnticipationCm = 0.8f;
	Pistol.AnticipationSeconds = 0.06f;
	Pistol.LookLag = 0.012f;
	Pistol.BreathDeg = 0.25f;
	FOSGunAim PumpGun;
	// The pump gun: along the rib, the receiver's top rear edge to the bead (Scripts/blender/os_guns.py:
	// receiver top 1.25, bead top 1.35 over the bore; origin 4.5 under it). The eye 45 cm behind the
	// receiver's rear, the gun's field 85: picked from stills of 16-65 cm and 65-90 deg (owner 19.10:
	// «приклад поменьше» - at 28 cm / 75 the stock filled the bottom of the frame); the butt stays in
	// front of the eye (NearClipPlane 2, Config/DefaultEngine.ini). Heavy: a later start, a slower,
	// bigger arc, a slow settle.
	// The eye a little over the rib (1.2 cm at the receiver): the rib rises to the bead as a shotgun is pointed,
	// and the comb stays under the receiver instead of covering it.
	PumpGun.SightRearCm = FVector(-9.9f, 0.0f, 7.05f);
	PumpGun.SightFrontCm = FVector(43.1f, 0.0f, 5.85f);
	PumpGun.EyeCm = 45.0f;
	PumpGun.GunFieldOfView = 85.0f;
	PumpGun.KickScale = 0.45f;
	// Across first, then forward, up last: the stock comes in under the eye and rises to the shoulder.
	PumpGun.In = { { 1.3f, 0.55f, 0.08f }, { 1.6f, 0.6f, 0.05f }, { 1.4f, 0.5f, 0.14f }, { 1.1f, 0.42f, 0.0f }, { 1.1f, 0.45f, 0.0f }, { 1.0f, 0.4f, 0.0f } };
	PumpGun.Out = { { 1.3f, 0.7f, 0.06f }, { 1.2f, 0.72f, 0.1f }, { 1.5f, 0.62f, 0.0f }, { 1.1f, 0.45f, 0.0f }, { 1.1f, 0.45f, 0.0f }, { 1.0f, 0.42f, 0.0f } };
	PumpGun.LagDegPerCmPerSec = FVector(0.08f, 0.03f, 0.04f);
	PumpGun.AnticipationCm = 1.6f;
	PumpGun.AnticipationSeconds = 0.12f;
	PumpGun.LookLag = 0.025f;
	PumpGun.BreathDeg = 0.2f;
	FOSGunAim Sniper;
	// The SV-98: the eyepiece on the view axis 14 cm ahead, the objective behind it (the scope).
	Sniper.SightRearCm = EyepieceCm;
	Sniper.SightFrontCm = ObjectiveCm;
	Sniper.EyeCm = 14.0f;
	Sniper.ZeroCm = 10000.0f; // the scope zeroed at 100 m
	// 96: the eyepiece ~44% of the frame (owner 18.10: «прицел можно помельче, чтобы и оружие было видно нормально»).
	Sniper.GunFieldOfView = 96.0f;
	Sniper.KickScale = 0.25f;
	Sniper.In = { { 1.8f, 0.58f, 0.1f }, { 2.0f, 0.62f, 0.06f }, { 2.3f, 0.55f, 0.0f }, { 1.5f, 0.45f, 0.0f }, { 1.5f, 0.45f, 0.0f }, { 1.4f, 0.42f, 0.0f } };
	Sniper.Out = { { 1.7f, 0.7f, 0.05f }, { 1.6f, 0.72f, 0.08f }, { 1.9f, 0.62f, 0.0f }, { 1.4f, 0.45f, 0.0f }, { 1.4f, 0.45f, 0.0f }, { 1.3f, 0.42f, 0.0f } };
	Sniper.LagDegPerCmPerSec = FVector(0.1f, 0.04f, 0.05f);
	Sniper.AnticipationCm = 1.2f;
	Sniper.AnticipationSeconds = 0.09f;
	Sniper.LookLag = 0.02f;
	Sniper.BreathDeg = 0.12f;
	// The attachment menu's pose (Docs/Attachments.md, «Поза меню»). The pistol's fitted on the PC's stills 22.10
	// (os.Attach.Stills): its left side flat to the eye, right of the projected menu, the magazine's foot in the frame.
	Pistol.Menu.OffsetCm = FVector(40.0f, 12.0f, -6.0f);
	Pistol.Menu.Turn = FRotator(-5.0f, -35.0f, -10.0f);
	Pistol.Menu.GunFieldOfView = 95.0f;
	Pistol.Menu.In = { { 3.2f, 0.55f, 0.05f }, { 3.0f, 0.6f, 0.03f }, { 3.4f, 0.5f, 0.0f }, { 2.4f, 0.42f, 0.0f }, { 2.4f, 0.42f, 0.0f }, { 2.0f, 0.38f, 0.0f } };
	Pistol.Menu.AnticipationCm = 0.6f;
	Pistol.Menu.AnticipationSeconds = 0.05f;
	Pistol.Menu.RowKickMove = FVector(-6.0f, 0.0f, 0.0f);
	Pistol.Menu.RowKickTurn = FRotator(0.0f, 0.0f, 15.0f);
	PumpGun.Menu.OffsetCm = FVector(20.0f, 13.0f, -9.0f);
	PumpGun.Menu.Turn = FRotator(-8.0f, -18.0f, -40.0f);
	PumpGun.Menu.GunFieldOfView = 85.0f;
	PumpGun.Menu.In = { { 1.6f, 0.55f, 0.08f }, { 1.5f, 0.6f, 0.05f }, { 1.5f, 0.5f, 0.1f }, { 1.1f, 0.42f, 0.0f }, { 1.1f, 0.42f, 0.0f }, { 0.9f, 0.38f, 0.0f } };
	PumpGun.Menu.AnticipationCm = 1.2f;
	PumpGun.Menu.AnticipationSeconds = 0.1f;
	PumpGun.Menu.RowKickMove = FVector(-4.0f, 0.0f, 0.0f);
	PumpGun.Menu.RowKickTurn = FRotator(0.0f, 0.0f, 10.0f);
	Sniper.Menu.OffsetCm = FVector(18.0f, 14.0f, -12.0f);
	Sniper.Menu.Turn = FRotator(-6.0f, -14.0f, -30.0f);
	Sniper.Menu.GunFieldOfView = 90.0f;
	Sniper.Menu.In = { { 1.8f, 0.58f, 0.08f }, { 1.8f, 0.62f, 0.05f }, { 2.0f, 0.55f, 0.0f }, { 1.4f, 0.45f, 0.0f }, { 1.4f, 0.45f, 0.0f }, { 1.2f, 0.4f, 0.0f } };
	Sniper.Menu.AnticipationCm = 1.0f;
	Sniper.Menu.AnticipationSeconds = 0.08f;
	Sniper.Menu.RowKickMove = FVector(-3.0f, 0.0f, 0.0f);
	Sniper.Menu.RowKickTurn = FRotator(0.0f, 0.0f, 8.0f);
	Aims = { Pistol, PumpGun, Sniper };
	LensMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Weapons/M_OS_ScopeLens.M_OS_ScopeLens")));
	ReticleMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Weapons/M_OS_ScopeReticle.M_OS_ScopeReticle")));
	SightDotMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Weapons/M_OS_SightDot.M_OS_SightDot")));
}

float UOSScopeComponent::GetCaptureFieldOfView() const
{
	// The eyepiece's half-angle as drawn: atan(R / relief) in the gun's own field
	// of view, carried into the world's - tan scales by tan(world/2) / tan(gun/2).
	// The picture in it spans Magnification times less of the world.
	const float WorldTan = FMath::Tan(FMath::DegreesToRadians(Camera->FieldOfView * 0.5f));
	const FOSGunAim& Aim = Aims[ScopedWeapon];
	const float GunTan = FMath::Tan(FMath::DegreesToRadians(Aim.GunFieldOfView * 0.5f));
	const float LensTan = LensRadiusCm / Aim.EyeCm * WorldTan / GunTan;
	return 2.0f * FMath::RadiansToDegrees(FMath::Atan(LensTan / Magnification));
}

float UOSScopeComponent::GetLookScale() const
{
	// Screen-matched: the world through the eyepiece moves Magnification times
	// faster on screen than the unzoomed world around it.
	return GetMotionWeapon() == ScopedWeapon ? FMath::Lerp(1.0f, 1.0f / Magnification, Alpha) : 1.0f;
}

void UOSScopeComponent::AimPose(int32 Weapon, FTransform& Rest, float& KickScale) const
{
	if (Weapon != MotionWeapon || !bMoving)
	{
		return;
	}
	// The springs' pose; settled at the eye it is exactly the sight line on the view axis.
	Rest.SetTranslation(Place);
	Rest.SetRotation(FRotator(Turn.X, Turn.Y, Turn.Z).Quaternion() * Rest.GetRotation());
	KickScale = FMath::Lerp(1.0f, Aims[Weapon].KickScale, Alpha);
}

void UOSScopeComponent::AimEnd(FVector& OutPlace, FVector& OutTurn) const
{
	const FOSGunAim& Aim = Aims[MotionWeapon];
	const FQuat RestTurn = Weapons->Weapons[MotionWeapon].ViewOffset.GetRotation();
	// The least turn that lays the sight line (carried by the rest turn) along the view; the rear sight EyeCm ahead.
	const FQuat Delta = FQuat::FindBetweenNormals(RestTurn.RotateVector((Aim.SightFrontCm - Aim.SightRearCm).GetSafeNormal()), FVector::ForwardVector);
	OutPlace = FVector(Aim.EyeCm, 0.0f, 0.0f) - (Delta * RestTurn).RotateVector(Aim.SightRearCm);
	const FRotator Turned = Delta.Rotator();
	OutTurn = FVector(Turned.Pitch, Turned.Yaw, Turned.Roll);
}

void UOSScopeComponent::MenuEnd(FVector& OutPlace, FVector& OutTurn) const
{
	const FOSGunMenuPose& Menu = Aims[MotionWeapon].Menu;
	OutPlace = Menu.OffsetCm;
	OutTurn = FVector(Menu.Turn.Pitch, Menu.Turn.Yaw, Menu.Turn.Roll);
}

void UOSScopeComponent::MenuPose(int32 Weapon, FTransform& Rest, float& KickScale) const
{
	// The springs' pose is AimPose's for both ends; in the menu's pose no shot is fired, so the kick is the hip's.
	if (Weapon == MotionWeapon && bMoving && MenuAlpha > 0.0f)
	{
		KickScale = FMath::Lerp(KickScale, 1.0f, MenuAlpha);
	}
}

void UOSScopeComponent::StepMotion(float DeltaTime, ETarget Wanted)
{
	const FOSGunAim& Aim = Aims[MotionWeapon];
	const FVector RestPlace = Weapons->Weapons[MotionWeapon].ViewOffset.GetTranslation();
	FVector AimPlace, AimTurn, MenuPlace, MenuTurn;
	AimEnd(AimPlace, AimTurn);
	MenuEnd(MenuPlace, MenuTurn);
	if (Wanted != Heading)
	{
		LeftTarget = Heading;
		Heading = Wanted;
		SinceSwitch = 0.0f;
	}
	bAimTarget = Heading == ETarget::Aim;
	SinceSwitch += DeltaTime;
	const auto EndOf = [&](ETarget Of, FVector& OutPlace, FVector& OutTurn)
	{
		OutPlace = Of == ETarget::Aim ? AimPlace : Of == ETarget::Menu ? MenuPlace : RestPlace;
		OutTurn = Of == ETarget::Aim ? AimTurn : Of == ETarget::Menu ? MenuTurn : FVector::ZeroVector;
	};
	FVector End, EndTurn, From, FromTurn;
	EndOf(Heading, End, EndTurn);
	EndOf(LeftTarget, From, FromTurn);
	// The springs of the way: into the aim or the menu their In; back to the hip the Out of where it was (the menu's, or the aim's when it has none).
	const TArray<FOSAimSpring>& MenuIn = Aim.Menu.In.Num() >= 6 ? Aim.Menu.In : Aim.In;
	const TArray<FOSAimSpring>& MenuOut = Aim.Menu.Out.Num() >= 6 ? Aim.Menu.Out : Aim.Out;
	const TArray<FOSAimSpring>& Springs = Heading == ETarget::Aim ? Aim.In : Heading == ETarget::Menu ? MenuIn
		: (LeftTarget == ETarget::Menu ? MenuOut : Aim.Out);
	if (Springs.Num() < 6)
	{
		return;
	}
	// Each channel heads for its end once its delay has passed, from the other end until then.
	FVector Goal;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		Goal[Axis] = SinceSwitch >= Springs[Axis].Delay ? End[Axis] : From[Axis];
	}
	// Anticipation: a raise (or the turn into the menu) starts with a dip.
	const float DipCm = Heading == ETarget::Menu ? Aim.Menu.AnticipationCm : Aim.AnticipationCm;
	const float DipSeconds = Heading == ETarget::Menu ? Aim.Menu.AnticipationSeconds : Aim.AnticipationSeconds;
	if (Heading != ETarget::Rest && SinceSwitch < DipSeconds)
	{
		Goal.Z = FMath::Min(Goal.Z, Place.Z) - DipCm;
	}
	// Aimed, it breathes: two slow, unrelated sines (pitch, yaw).
	const float Now = GetWorld()->GetTimeSeconds();
	const FVector Breath = bAimTarget ? Aim.BreathDeg * FVector(FMath::Sin(2.0f * PI * 0.23f * Now), 0.6f * FMath::Sin(2.0f * PI * 0.15f * Now + 1.3f), 0.0f)
		: FVector::ZeroVector;
	const FVector TurnEnd = SinceSwitch >= Springs[3].Delay ? EndTurn : FromTurn;
	// x'' = w^2 (goal - x) - 2 zeta w x', semi-implicit Euler in steps of at most 4 ms (stable at the 4.4 Hz ends).
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
		// The turns chase the aimed turn plus the drag of the motion: the muzzle dips while rising, points back across, cants.
		const FVector Lag = Aim.LagDegPerCmPerSec;
		const FVector TurnGoal = TurnEnd + Breath + LookLagTurn + FVector(-Lag.X * PlaceVelocity.Z, -Lag.Y * PlaceVelocity.Y, Lag.Z * PlaceVelocity.Y);
		for (int32 Axis = 0; Axis < 3; ++Axis)
		{
			const FOSAimSpring& Spring = Springs[3 + Axis];
			const float W = 2.0f * PI * Spring.Hz;
			TurnVelocity[Axis] += (W * W * (TurnGoal[Axis] - Turn[Axis]) - 2.0f * Spring.Damping * W * TurnVelocity[Axis]) * Dt;
			Turn[Axis] += TurnVelocity[Axis] * Dt;
		}
	}
	// Settled: snap exactly onto the end (the sight line on the axis to the micron), and at the hip stop.
	if (Heading == ETarget::Rest && SinceSwitch > 0.2f && (Place - End).Size() < 0.01f && PlaceVelocity.Size() < 0.05f && (Turn - EndTurn).Size() < 0.01f
		&& TurnVelocity.Size() < 0.05f && LookLagTurn.Size() < 0.01f)
	{
		Place = End;
		Turn = EndTurn;
		PlaceVelocity = TurnVelocity = FVector::ZeroVector;
		bMoving = false;
	}
	// How far along each way the gun is: the aim's share fades as the menu's grows (the two exclude each other).
	const FVector Way = AimPlace - RestPlace;
	const FVector MenuWay = MenuPlace - RestPlace;
	MenuAlpha = MenuWay.SizeSquared() > 0.01f ? FMath::Clamp(FVector::DotProduct(Place - RestPlace, MenuWay) / MenuWay.SizeSquared(), 0.0f, 1.0f) : 0.0f;
	if (Heading != ETarget::Menu && LeftTarget != ETarget::Menu)
	{
		MenuAlpha = 0.0f;
	}
	Alpha = FMath::Clamp(FVector::DotProduct(Place - RestPlace, Way) / Way.SizeSquared(), 0.0f, 1.0f) * (1.0f - MenuAlpha);
	if (!bMoving && Heading == ETarget::Rest)
	{
		Alpha = MenuAlpha = 0.0f;
	}
}

bool UOSScopeComponent::Build()
{
	Weapons = GetOwner()->FindComponentByClass<UOSWeaponsComponent>();
	Camera = GetOwner()->FindComponentByClass<UCameraComponent>();
	UStaticMeshComponent* Gun = Weapons ? Weapons->ViewModel.Get() : nullptr;
	if (!Gun || !Camera || !Weapons->Weapons.IsValidIndex(ScopedWeapon))
	{
		return false;
	}
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
	// the gun's up, its Y the gun's right - M_OS_ScopeLens reads them so.
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
		UE_LOG(LogMindKore, Warning, TEXT("Scope %s: no %s (Scripts/os_scope.py)"), *GetNameSafe(GetOwner()), *LensMaterial.ToString());
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
	for (int32 Index = 0; Index < SightDotsCm.Num(); ++Index)
	{
		const FVector& At = SightDotsCm[Index];
		UStaticMeshComponent* Dot = NewObject<UStaticMeshComponent>(GetOwner());
		Dot->SetStaticMesh(Sphere);
		const bool bFront = Index == SightDotsCm.Num() - 1;
		if (bFront && DotMaterial)
		{
			UMaterialInstanceDynamic* Bright = UMaterialInstanceDynamic::Create(DotMaterial, Dot);
			Bright->SetScalarParameterValue(TEXT("Glow"), FrontSightDotGlow);
			Dot->SetMaterial(0, Bright);
		}
		else
		{
			Dot->SetMaterial(0, DotMaterial);
		}
		Dot->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Dot->SetCastShadow(false);
		Dot->SetOnlyOwnerSee(true);
		Dot->SetupAttachment(Weapons->ActionInView);
		Dot->SetRelativeLocation(At);
		Dot->SetRelativeScale3D((bFront ? FrontSightDotCm : FVector(SightDotCm)) / 100.0f);
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
	UE_LOG(LogMindKore, Log, TEXT("Scope %s: capture FOV %.2f deg (x%.1f), target %dx%d, %d owner primitives hidden"),
		*GetNameSafe(GetOwner()), Capture->FOVAngle, Magnification, RenderTargetSize, RenderTargetSize, Primitives.Num());
	return true;
}

void UOSScopeComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (!Capture && !Build())
	{
		return;
	}
	// The gun shown in view, which during a switch is still the old one on its way out (review 19.10: reading
	// CurrentWeapon dropped the aim and the gun's field of view in one frame). Aim with it only while it is the
	// one wanted - not on its way out, not with the hands on something else (a hull tool), not during a reload; going away it lowers on
	// its Out springs. The scope (lens, capture) belongs to the SV-98 alone.
	const UStaticMeshComponent* Gun = Weapons->ViewModel;
	const int32 Shown = Weapons->GetShownWeapon();
	const bool bGunInView = Weapons->Weapons.IsValidIndex(Shown) && Aims.IsValidIndex(Shown);
	const bool bScopeInView = bGunInView && Shown == ScopedWeapon;
	// The attachment menu's pose (Tab held, a module on its way, a magazine being swapped) wins over the aim.
	const bool bMenu = bMenuHeld && bGunInView && Shown == Weapons->CurrentWeapon && Weapons->GetWeapon();
	const bool bWants = !bMenu && (bHeld || bHeldByHands) && bGunInView && Shown == Weapons->CurrentWeapon
		&& Weapons->GetWeapon() && Weapons->GetReloadRemaining() <= 0.0f;
	const ETarget Wanted = bMenu ? ETarget::Menu : bWants ? ETarget::Aim : ETarget::Rest;
	const float WasMenuAlpha = MenuAlpha;
	const float WasAlpha = Alpha;
	// The body's velocity change this frame, in the camera's frame: the gun's inertia sways it the other way.
	const FVector BodyVelocity = GetOwner()->GetVelocity();
	const FVector BodyChange = Camera->GetComponentQuat().UnrotateVector(BodyVelocity - LastBodyVelocity);
	LastBodyVelocity = BodyVelocity;
	// The view's turn rate, in its own frame: the gun lags behind it (yaw and pitch back, a roll into a yaw).
	const FQuat View = Camera->GetComponentQuat();
	const FRotator Turned = (LastView.Inverse() * View).Rotator();
	LastView = View;
	const float LookLag = Aims.IsValidIndex(Shown) ? Aims[Shown].LookLag : 0.0f;
	LookLagTurn = DeltaTime > 0.0f
		? (FVector(-Turned.Pitch, -Turned.Yaw, -0.5f * Turned.Yaw) * (LookLag / DeltaTime)).BoundToCube(4.0f)
		: FVector::ZeroVector;
	if (!bGunInView || Shown != MotionWeapon)
	{
		// No gun, or another one: it starts from its rest pose.
		bMoving = bAimTarget = false;
		Heading = LeftTarget = ETarget::Rest;
		Alpha = MenuAlpha = 0.0f;
		MotionWeapon = bGunInView ? Shown : INDEX_NONE;
	}
	if (bGunInView)
	{
		if (!bMoving && (bWants || bMenu || BodyChange.Size() * SwayPerBodyVelocity > 0.5f || LookLagTurn.Size() > 0.2f))
		{
			bMoving = true;
			Place = Weapons->Weapons[Shown].ViewOffset.GetTranslation();
			PlaceVelocity = Turn = TurnVelocity = FVector::ZeroVector;
		}
		if (bMoving)
		{
			PlaceVelocity -= (BodyChange * SwayPerBodyVelocity).GetClampedToMaxSize(MaxSwayCmPerSec);
			StepMotion(DeltaTime, Wanted);
		}
	}
	if (CVarScopeTrace.GetValueOnGameThread() && bMoving)
	{
		UE_LOG(LogMindKore, Log, TEXT("Scope trace: gun=%d t=%.4f aim=%d place=%.3f,%.3f,%.3f turn=%.3f,%.3f,%.3f alpha=%.4f"), MotionWeapon, GetWorld()->GetTimeSeconds(),
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
	Capture->bCaptureEveryFrame = bScopeInView && Alpha > 0.01f; // not for a draw's overshoot toward the eye
	if (Capture->bCaptureEveryFrame)
	{
		// The bodycam's field and the gun's aim are live (the debug menu edits them): the eyepiece follows.
		Capture->FOVAngle = GetCaptureFieldOfView();
	}
	if (Alpha > 0.0f || WasAlpha > 0.0f || MenuAlpha > 0.0f || WasMenuAlpha > 0.0f)
	{
		// The world keeps the bodycam's field (owner: no zoom outside the scope);
		// only the gun narrows its own, coming up large to the eye - or in the menu's pose.
		const float WorldFieldOfView = Camera->FieldOfView;
		const float AimField = Aims.IsValidIndex(MotionWeapon) ? Aims[MotionWeapon].GunFieldOfView : WorldFieldOfView;
		const float MenuField = Aims.IsValidIndex(MotionWeapon) ? Aims[MotionWeapon].Menu.GunFieldOfView : WorldFieldOfView;
		Camera->SetEnableFirstPersonFieldOfView(Alpha > 0.0f || MenuAlpha > 0.0f);
		Camera->SetFirstPersonFieldOfView(WorldFieldOfView + (AimField - WorldFieldOfView) * Alpha + (MenuField - WorldFieldOfView) * MenuAlpha);
	}
	if (LensMID && bScopeInView)
	{
		// The eye (the camera's origin) in the gun's frame, from the eyepiece: the
		// lens's x is the gun's right, its y the gun's down; z how far the eye is
		// off its relief, cm. At the hip the shadow covers the glass.
		const FVector Eye = Gun->GetRelativeTransform().InverseTransformPosition(FVector::ZeroVector) - EyepieceCm;
		LensMID->SetVectorParameterValue(TEXT("Eye"), FLinearColor(Eye.Y * ShadowPerCm, -Eye.Z * ShadowPerCm, -Eye.X - Aims[ScopedWeapon].EyeCm, 0.0f));
		if (ReticleMID)
		{
			ReticleMID->SetVectorParameterValue(TEXT("Eye"), FLinearColor(Eye.Y * ShadowPerCm, -Eye.Z * ShadowPerCm, -Eye.X - Aims[ScopedWeapon].EyeCm, 0.0f));
		}
	}
}

FString UOSScopeComponent::Describe() const
{
	return FString::Printf(TEXT("gun %d, aim %.2f, capture %s, capture FOV %.2f deg (x%.1f), target %dx%d, world FOV %.1f, gun FOV %.1f (%s), look scale %.3f"),
		GetMotionWeapon(), Alpha, Capture && Capture->bCaptureEveryFrame ? TEXT("on") : TEXT("off"), Capture ? Capture->FOVAngle : 0.0f, Magnification,
		Target ? Target->SizeX : 0, Target ? Target->SizeY : 0, Camera ? Camera->FieldOfView : 0.0f,
		Camera ? Camera->FirstPersonFieldOfView : 0.0f, Camera && Camera->bEnableFirstPersonFieldOfView ? TEXT("on") : TEXT("off"), GetLookScale());
}

namespace
{
	/**
	 * os.Scope.Shots: the SV-98's scope in stills (HighResShot; needs a real RHI;
	 * Lvl_OS_Range). The zero-G pawn stands 19.5 m before the living wall nearest
	 * to 20 m away and looks at it: the rifle at the hip, raising, aimed at the
	 * wall's centre and at its top edge, a shot (+0.05, +0.15, +0.6 s in the bolt
	 * cycle, +1.5 s), at the hip again, then the pistol with its sight dots (at rest
	 * and the slide held back). Every still is logged first with the
	 * scope's state; GPU and frame times are sampled at the hip and aimed.
	 */
	void RunScopeShots(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		UOSScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UOSScopeComponent>() : nullptr;
		if (!Scope)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Scope shots: needs the AOSZeroGPawn with its scope (made when input binds)"));
			return;
		}
		const AOSLivingWall* Wall = nullptr;
		for (TActorIterator<AOSLivingWall> It(World); It; ++It)
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
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSScope_%s"), Name));
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
		After(12.2f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 2 filename=OSScope_03b_aimed_x2")); });
		After(12.4f, [Hold, Edge]() { Hold(Edge); });
		After(12.6f, [Shot]() { Shot(TEXT("04_aimed_wall_edge")); });
		// Into the dark (space above the wall), then back on the wall for the shot.
		After(12.65f, [Hold, Centre]() { Hold(Centre + FVector(0.0f, 0.0f, 2500.0f)); });
		After(12.8f, [Shot]() { Shot(TEXT("04b_aimed_dark")); });
		After(13.0f, [Pawn, Hold, Centre]() { Hold(Centre); Pawn->Fire(); });
		After(13.05f, [Shot]() { Shot(TEXT("05_shot_0050")); });
		After(13.15f, [Shot]() { Shot(TEXT("06_shot_0150")); });
		After(13.6f, [Shot]() { Shot(TEXT("07_bolt_0600")); });
		After(14.4f, [Hold, Centre]() { Hold(Centre); });
		After(14.5f, [Shot]() { Shot(TEXT("08_settled_1500")); });
		After(15.0f, [Scope]() { Scope->SetHeld(false); });
		After(16.0f, [Hold, Shot, Centre]() { Hold(Centre); Shot(TEXT("09_hip_again")); });
		// The pistol's sight dots: at rest, then the slide held back (os.Weapons.PoseAt) - they ride on it.
		IConsoleVariable* PoseAt = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Weapons.PoseAt"));
		After(16.5f, [Pawn, Hold, Centre]() { Pawn->Guns->UnlockWeapon(0); Hold(Centre); });
		After(17.5f, [Hold, Shot, Centre]() { Hold(Centre); Shot(TEXT("10_pistol_sights")); });
		After(17.7f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 2 filename=OSScope_10b_pistol_x2")); });
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
			FOSWeapon& Pistol = Pawn->Guns->Weapons[Scope->SightDotWeapon];
			*Rest = Pistol.ViewOffset;
			const TArray<FVector>& Dots = Scope->SightDotsCm;
			const FVector Rear = (Dots[0] + Dots[1]) * 0.5f;
			// Turned round first (the USP's muzzle is its -X), then the small tilt onto the line - one
			// rotation straight from -X to +X would pick an arbitrary axis and could roll it upside down.
			const FQuat Round = FRotator(0.0f, 180.0f, 0.0f).Quaternion();
			const FQuat Turn = FQuat::FindBetweenNormals(Round.RotateVector((Dots[2] - Rear).GetSafeNormal()), FVector::ForwardVector) * Round;
			Pistol.ViewOffset = FTransform(Turn, FVector(20.0f, 0.0f, 0.0f) - Turn.RotateVector(Rear));
			UE_LOG(LogMindKore, Log, TEXT("Scope shots: sight line - rear dots %s / %s, front %s, notch centre y -1.431, front post centre y -1.453 (SM_OS_USP_Slide)"),
				*Dots[0].ToCompactString(), *Dots[1].ToCompactString(), *Dots[2].ToCompactString());
		});
		// The HUD off for it: its centre dot sits exactly on the front sight dot.
		After(19.3f, [Controller]() { if (Controller->MyHUD) { Controller->MyHUD->bShowHUD = false; } });
		After(19.4f, [Controller]() { Controller->ConsoleCommand(TEXT("HighResShot 2 filename=OSScope_12_pistol_sightline_x2")); });
		After(19.7f, [Controller]() { if (Controller->MyHUD) { Controller->MyHUD->bShowHUD = true; } });
		After(19.8f, [Pawn, Scope, Rest]() { Pawn->Guns->Weapons[Scope->SightDotWeapon].ViewOffset = *Rest; });
		After(20.2f, [Scope]() { UE_LOG(LogMindKore, Log, TEXT("Scope shots: done - %s"), *Scope->Describe()); });
	}

	FAutoConsoleCommandWithWorld GScopeShots(
		TEXT("os.Scope.Shots"),
		TEXT("Stills of the SV-98's scope at a living wall ~20 m away: hip, raising, aimed (centre, edge), a shot through the bolt cycle, hip again, the pistol sight dots; GPU/frame time at the hip and aimed (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunScopeShots));

	/**
	 * os.Scope.AimFrames: every gun's raise, a shot in the aimed pose and the lowering, frame by frame
	 * (HighResShot; needs a real RHI). The zero-G pawn stands before the living wall nearest to 20 m;
	 * time runs at a tenth so the stills land every 33 ms of game time. For each gun (pistol, pump gun,
	 * SV-98): 25 stills through the raise (0..0.8 s), a shot at 1.3 s and 18 stills after it, 16 through
	 * the lowering (OSAim_<gun>_<phase>_<nn>_<ms>.png). Then the SV-98 aimed under a 60 deg/s turn of the
	 * view (look), 4 s of aimed idle at full speed (the breath), going unarmed (holster) and drawn again
	 * (draw). os.Scope.Trace logs the springs throughout.
	 */
	void RunScopeAimFrames(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		UOSScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UOSScopeComponent>() : nullptr;
		IConsoleVariable* Trace = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Scope.Trace"));
		if (!Scope || !Trace)
		{
			UE_LOG(LogMindKore, Warning, TEXT("Scope aim frames: needs the AOSZeroGPawn with its scope"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f);
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<AOSLivingWall> It(World); It; ++It)
		{
			const float Error = FMath::Abs(FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation()) - 2000.0f);
			if (Error < Best)
			{
				Best = Error;
				Target = It->GetActorLocation();
			}
		}
		const FVector Stand = Pawn->GetActorLocation();
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		const auto Still = [Controller, Scope](int32 Gun, const TCHAR* Phase, int32 Index, float Seconds)
		{
			UE_LOG(LogMindKore, Log, TEXT("Scope aim frames: gun %d %s %02d at %.3f s - %s"), Gun, Phase, Index, Seconds, *Scope->Describe());
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSAim_%d_%s_%02d_%03d"), Gun, Phase, Index, FMath::RoundToInt(Seconds * 1000.0f)));
		};
		// Long first wait: shaders may still compile. Timers count game time, slowed from 9 s on.
		After(9.0f, [World, Trace]() { UGameplayStatics::SetGlobalTimeDilation(World, 0.1f); Trace->Set(1, ECVF_SetByCode); });
		float At = 9.2f;
		for (int32 Gun = 0; Gun < 3; ++Gun)
		{
			// Back on the spot, the gun in hand, facing the wall; half a second to let the body's sway settle.
			After(At, [Pawn, Controller, Target, Stand, Gun]()
			{
				Pawn->Body->SetWorldLocation(Stand, false, nullptr, ETeleportType::TeleportPhysics);
				Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
				Pawn->Guns->UnlockWeapon(Gun);
				Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
			});
			const float In = At + 2.0f; // the draw settled first
			After(In, [Scope]() { Scope->SetHeld(true); });
			for (int32 Index = 0; Index < 25; ++Index)
			{
				After(In + Index / 30.0f + 0.001f, [Still, Gun, Index]() { Still(Gun, TEXT("in"), Index, Index / 30.0f); });
			}
			const float Shot = In + 1.3f;
			After(Shot, [Pawn]() { Pawn->Fire(); });
			for (int32 Index = 0; Index < 18; ++Index)
			{
				After(Shot + Index / 30.0f + 0.001f, [Still, Gun, Index]() { Still(Gun, TEXT("shot"), Index, Index / 30.0f); });
			}
			const float Out = Shot + 1.4f;
			After(Out, [Scope]() { Scope->SetHeld(false); });
			for (int32 Index = 0; Index < 16; ++Index)
			{
				After(Out + Index / 30.0f + 0.001f, [Still, Gun, Index]() { Still(Gun, TEXT("out"), Index, Index / 30.0f); });
			}
			At = Out + 1.0f;
		}
		// The SV-98 again: aimed, the view swung 60 deg/s for 0.3 s (the gun lags), then 4 s of aimed idle at
		// full speed (the breath, in the trace), then unarmed (the drop away) and drawn again (the rise).
		After(At, [Scope]() { Scope->SetHeld(true); });
		const float Look = At + 1.5f;
		for (int32 Tick = 0; Tick < 18; ++Tick)
		{
			After(Look + Tick / 60.0f, [Controller]() { Controller->SetControlRotation(Controller->GetControlRotation() + FRotator(0.0f, 1.0f, 0.0f)); });
		}
		for (int32 Index = 0; Index < 18; ++Index)
		{
			After(Look + Index / 30.0f + 0.001f, [Still, Index]() { Still(2, TEXT("look"), Index, Index / 30.0f); });
		}
		const float Idle = Look + 1.0f;
		After(Idle, [World]() { UGameplayStatics::SetGlobalTimeDilation(World, 1.0f); });
		After(Idle + 4.0f, [World]() { UGameplayStatics::SetGlobalTimeDilation(World, 0.1f); });
		const float Away = Idle + 4.2f;
		After(Away, [Scope, Pawn]() { Scope->SetHeld(false); Pawn->SetHands(EOSHands::Held); });
		for (int32 Index = 0; Index < 12; ++Index)
		{
			After(Away + Index / 30.0f + 0.001f, [Still, Index]() { Still(2, TEXT("holster"), Index, Index / 30.0f); });
		}
		const float Draw = Away + 0.6f;
		After(Draw, [Pawn]() { Pawn->SetHands(EOSHands::Gun); });
		for (int32 Index = 0; Index < 24; ++Index)
		{
			After(Draw + Index / 30.0f + 0.001f, [Still, Index]() { Still(2, TEXT("draw"), Index, Index / 30.0f); });
		}
		At = Draw + 1.0f;
		After(At, [World, Trace, Scope]()
		{
			Trace->Set(0, ECVF_SetByCode);
			UGameplayStatics::SetGlobalTimeDilation(World, 1.0f);
			UE_LOG(LogMindKore, Log, TEXT("Scope aim frames: done - %s"), *Scope->Describe());
		});
	}

	FAutoConsoleCommandWithWorld GScopeAimFrames(
		TEXT("os.Scope.AimFrames"),
		TEXT("Every gun's raise, a shot aimed and the lowering every 33 ms of game time (time at a tenth), os.Scope.Trace on (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunScopeAimFrames));

	/**
	 * os.Guns.RmbProbe: the RMB rule, logged step by step (no stills; NullRHI is fine). Needs a loose body
	 * (a simulating actor) and a fixed surface within 50 m in the map (Lvl_OS_ZeroG). The zero-G pawn: with a gun out
	 * and nothing in reach RMB aims (alpha rises, RMB up lowers it); with a gun out and the body in reach RMB
	 * takes it and the gun goes away at once (unarmed); RMB again lets go and the last gun comes back with a
	 * draw that holds fire until it settles; with a hull tool in hand RMB holds on to the fixed surface and
	 * lets go; holding, a gun selected lets the body go and is drawn.
	 */
	void RunRmbProbe(UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		UOSScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UOSScopeComponent>() : nullptr;
		AActor* Loose = nullptr;
		float LooseDist = TNumericLimits<float>::Max();
		for (TActorIterator<AActor> It(World); It && Pawn; ++It)
		{
			const UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(It->GetRootComponent());
			if (!Root || *It == Pawn || Root->GetCollisionEnabled() == ECollisionEnabled::NoCollision)
			{
				continue;
			}
			const float Dist = FVector::Dist(It->GetActorLocation(), Pawn->GetActorLocation());
			if (Root->IsSimulatingPhysics() && Root->GetMass() < 200.0f && Dist < LooseDist)
			{
				Loose = *It;
				LooseDist = Dist;
			}
		}
		if (!Scope || !Loose)
		{
			UE_LOG(LogMindKore, Warning, TEXT("RMB probe: needs the AOSZeroGPawn and a loose body"));
			return;
		}
		const auto After = [Pawn](float Seconds, TFunction<void()> Step)
		{
			FTimerHandle Handle;
			Pawn->GetWorldTimerManager().SetTimer(Handle, FTimerDelegate::CreateWeakLambda(Pawn, MoveTemp(Step)), Seconds, false);
		};
		// Stand Distance short of a point, looking at it, still.
		const auto Face = [Pawn, Controller](const FVector& Point, const FVector& From, float Distance)
		{
			const FVector Dir = (Point - From).GetSafeNormal();
			Pawn->Body->SetWorldLocation(Point - Dir * Distance, false, nullptr, ETeleportType::TeleportPhysics);
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation(Dir.Rotation());
		};
		const auto State = [Pawn, Scope](const TCHAR* Step)
		{
			static const TCHAR* Names[] = { TEXT("none"), TEXT("aim"), TEXT("grab"), TEXT("let go"), TEXT("cling") };
			UE_LOG(LogMindKore, Log, TEXT("RMB probe [%s]: RMB would %s; %s; holding %.0f kg, clinging %d, aim %.2f (gun %d)"), Step,
				Names[int32(Pawn->GetRmbAction())], *Pawn->Guns->Describe(), Pawn->GetHeldMassKg(), Pawn->IsClinging() ? 1 : 0,
				Scope->GetAimAlpha(), Scope->GetMotionWeapon());
		};
		// A loose body freezes in place for the probe: it does not drift off between the steps.
		const auto Pin = [Loose]() { if (UPrimitiveComponent* Root = Cast<UPrimitiveComponent>(Loose->GetRootComponent())) { Root->SetPhysicsLinearVelocity(FVector::ZeroVector); Root->SetPhysicsAngularVelocityInDegrees(FVector::ZeroVector); } };
		// 1. A gun out, nothing in reach: RMB aims.
		After(4.0f, [Pawn, Controller]() { Pawn->Guns->UnlockWeapon(0); Controller->SetControlRotation(FRotator(85.0f, 0.0f, 0.0f)); }); // up, into space
		After(4.5f, [Pawn, State]() { State(TEXT("gun out, nothing in reach")); Pawn->Use(); });
		After(5.3f, [State]() { State(TEXT("RMB held 0.8 s")); });
		After(5.4f, [Scope]() { Scope->SetAimHeld(false); });
		After(6.2f, [State]() { State(TEXT("RMB up 0.8 s")); });
		// 2. A gun out, the loose body in reach: RMB takes it, the gun goes away.
		After(6.5f, [Loose, Face, Pin, Pawn]() { Pin(); Face(Loose->GetActorLocation(), Pawn->GetActorLocation(), 120.0f); });
		After(6.7f, [Pawn, State]() { State(TEXT("gun out, body in reach")); Pawn->Use(); });
		After(6.8f, [State]() { State(TEXT("after RMB")); });
		// 3. Unarmed, RMB again: let go - the last gun comes back with a draw, no shot until it settles.
		After(7.2f, [Pawn, State]() { Pawn->Use(); State(TEXT("holding, RMB again: let go")); });
		After(7.25f, [Pawn, State]() { const bool bFired = Pawn->Fire(); State(bFired ? TEXT("fire during the draw: FIRED") : TEXT("fire during the draw: refused")); });
		After(8.1f, [Pawn, State]() { State(Pawn->Guns->IsDrawing() ? TEXT("0.9 s after: still drawing") : TEXT("0.9 s after: draw settled")); });
		// Holding on to the hull needs free hands or a hull tool (a gun out aims): the strap in hand.
		After(8.2f, [Pawn]() { Pawn->PressSlot(Pawn->GetToolSlot(EOSHullTool::Strap)); });
		// The nearest fixed surface from where the probe started: rays out six ways, the first static hit.
		After(8.3f, [World, Face, Pawn, Home = Pawn->GetActorLocation()]()
		{
			FCollisionQueryParams Params(SCENE_QUERY_STAT(OSRmbProbe), false, Pawn);
			FHitResult Nearest;
			for (const FVector& Way : { FVector::ForwardVector, -FVector::ForwardVector, FVector::RightVector, -FVector::RightVector, FVector::UpVector, -FVector::UpVector })
			{
				FHitResult Hit;
				if (World->LineTraceSingleByChannel(Hit, Home, Home + Way * 5000.0f, ECC_Visibility, Params) && Hit.GetComponent()
					&& !Hit.GetComponent()->IsSimulatingPhysics() && (!Nearest.bBlockingHit || Hit.Distance < Nearest.Distance))
				{
					Nearest = Hit;
				}
			}
			if (Nearest.bBlockingHit)
			{
				UE_LOG(LogMindKore, Log, TEXT("RMB probe: the fixed surface - %s"), *GetNameSafe(Nearest.GetActor()));
				Face(Nearest.ImpactPoint, Nearest.ImpactPoint + Nearest.ImpactNormal * 200.0f, 120.0f);
			}
		});
		After(8.5f, [Pawn, State]() { State(TEXT("strap in hand, the wall ahead")); Pawn->Use(); State(TEXT("strap in hand, after RMB on the wall")); });
		After(8.8f, [Pawn, State]() { Pawn->Use(); State(TEXT("clinging, RMB again")); Pawn->PressSlot(Pawn->GetToolSlot(EOSHullTool::Strap)); });
		// 4. A gun out, the body taken (unarmed), then a gun selected (2): the body is let go, the gun drawn.
		After(9.1f, [Loose, Face, Pin, Pawn]() { Pin(); Face(Loose->GetActorLocation(), Pawn->GetActorLocation(), 120.0f); });
		After(9.3f, [Pawn, State]() { Pawn->Use(); State(TEXT("gun out, body taken again")); Pawn->PressSlot(1); });
		After(9.5f, [State]() { State(TEXT("gun 2 selected while holding")); UE_LOG(LogMindKore, Log, TEXT("RMB probe: done")); });
	}

	FAutoConsoleCommandWithWorld GRmbProbe(
		TEXT("os.Guns.RmbProbe"),
		TEXT("The RMB rule step by step: aim with a gun out, grab (unarmed) with a loose body in reach, the hands when unarmed, a gun drawn while holding (Lvl_OS_ZeroG)."),
		FConsoleCommandWithWorldDelegate::CreateStatic(&RunRmbProbe));

	/**
	 * os.Scope.AimStills <gun> <eye:fov> [<eye:fov> ...]: one gun aimed at the living wall nearest to
	 * 20 m with each eye distance (cm) and gun field of view in turn, a settled still of each
	 * (OSAimStill_<gun>_<eye>_<fov>.png) - for choosing them by eye. Needs a real RHI.
	 */
	void RunAimStills(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		UOSScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UOSScopeComponent>() : nullptr;
		const int32 Gun = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 1;
		if (!Scope || !Scope->Aims.IsValidIndex(Gun))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Aim stills: needs the AOSZeroGPawn and a gun with an aim"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f);
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<AOSLivingWall> It(World); It; ++It)
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
		After(6.0f, [Pawn, Controller, Target, Scope, Gun]()
		{
			Pawn->Guns->UnlockWeapon(Gun);
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
			Scope->SetHeld(true);
		});
		float At = 8.0f;
		for (int32 Index = 1; Index < Args.Num(); ++Index)
		{
			FString Eye, Fov;
			if (!Args[Index].Split(TEXT(":"), &Eye, &Fov))
			{
				continue;
			}
			const float EyeCm = FCString::Atof(*Eye);
			const float FieldOfView = FCString::Atof(*Fov);
			After(At, [Scope, Gun, EyeCm, FieldOfView]() { Scope->Aims[Gun].EyeCm = EyeCm; Scope->Aims[Gun].GunFieldOfView = FieldOfView; });
			After(At + 2.5f, [Controller, Scope, Gun, EyeCm, FieldOfView]()
			{
				UE_LOG(LogMindKore, Log, TEXT("Aim stills: gun %d eye %.0f cm, gun FOV %.0f - %s"), Gun, EyeCm, FieldOfView, *Scope->Describe());
				Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSAimStill_%d_%02.0f_%02.0f"), Gun, EyeCm, FieldOfView));
			});
			At += 3.0f;
		}
		After(At, []() { UE_LOG(LogMindKore, Log, TEXT("Aim stills: done")); });
	}

	/**
	 * os.Scope.SwitchStills [Gun] [To]: a switch while aimed (review 19.10: the gun snapped from the eye to the hip and
	 * its field of view 100 -> 134 in one frame). Gun (default 2, the SV-98) raised to the eye at the living wall nearest
	 * to 20 m and settled; then gun To (default 0) selected with the aim still held. Stills of the aimed frame and 0, 0.05,
	 * 0.15, 0.3 s after the switch (OSScopeSwitch_<aimed|ms>.png); every tick from 0.1 s before to 0.6 s after it
	 * logged - "Scope switch: t=.. shown=.. current=.. place=(..) - <the scope's state>". Needs a real RHI.
	 */
	void RunScopeSwitchStills(const TArray<FString>& Args, UWorld* World)
	{
		APlayerController* Controller = World->GetFirstPlayerController();
		AOSZeroGPawn* Pawn = Controller ? Cast<AOSZeroGPawn>(Controller->GetPawn()) : nullptr;
		UOSScopeComponent* Scope = Pawn ? Pawn->FindComponentByClass<UOSScopeComponent>() : nullptr;
		const int32 Gun = Args.Num() > 0 ? FCString::Atoi(*Args[0]) : 2;
		const int32 To = Args.Num() > 1 ? FCString::Atoi(*Args[1]) : 0;
		if (!Scope || !Pawn->Guns->Weapons.IsValidIndex(Gun) || !Pawn->Guns->Weapons.IsValidIndex(To))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Scope switch: needs the AOSZeroGPawn and two guns"));
			return;
		}
		FVector Target = Pawn->GetActorLocation() + FVector(2000.0f, 0.0f, 0.0f);
		float Best = TNumericLimits<float>::Max();
		for (TActorIterator<AOSLivingWall> It(World); It; ++It)
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
		const auto Still = [Controller, Scope](const FString& Name)
		{
			UE_LOG(LogMindKore, Log, TEXT("Scope switch: still %s - %s"), *Name, *Scope->Describe());
			Controller->ConsoleCommand(FString::Printf(TEXT("HighResShot 1 filename=OSScopeSwitch_%s"), *Name));
		};
		After(6.0f, [Pawn, Controller, Target, Scope, Gun, To]()
		{
			Pawn->Guns->UnlockWeapon(To);
			Pawn->Guns->UnlockWeapon(Gun);
			Pawn->Guns->FinishSwitch();
			Pawn->Body->SetPhysicsLinearVelocity(FVector::ZeroVector);
			Controller->SetControlRotation((Target - Pawn->Camera->GetComponentLocation()).Rotation());
			Scope->SetHeld(true);
		});
		After(8.5f, [Still]() { Still(TEXT("aimed")); });
		TSharedRef<double> SwitchAt = MakeShared<double>(0.0);
		After(9.0f, [Pawn, Still, SwitchAt, To]()
		{
			*SwitchAt = Pawn->GetWorld()->GetTimeSeconds();
			Pawn->Guns->SelectWeapon(To);
			Still(TEXT("0000"));
		});
		for (const float Late : { 0.05f, 0.15f, 0.3f })
		{
			After(9.0f + Late, [Still, Late]() { Still(FString::Printf(TEXT("%04d"), FMath::RoundToInt(Late * 1000.0f))); });
		}
		After(8.9f, [Pawn, Scope, SwitchAt]()
		{
			FTimerHandle Every;
			Pawn->GetWorldTimerManager().SetTimer(Every, FTimerDelegate::CreateWeakLambda(Pawn, [Pawn, Scope, SwitchAt]()
			{
				const double Now = Pawn->GetWorld()->GetTimeSeconds();
				const float T = *SwitchAt > 0.0 ? float(Now - *SwitchAt) : -0.1f;
				if (T > 0.6f)
				{
					return;
				}
				UE_LOG(LogMindKore, Log, TEXT("Scope switch: t=%.3f shown=%d current=%d place=%s - %s"), T, Pawn->Guns->GetShownWeapon(), Pawn->Guns->CurrentWeapon,
					*Pawn->Guns->ViewModel->GetRelativeLocation().ToCompactString(), *Scope->Describe());
			}), 0.001f, true);
		});
		After(10.0f, [Scope]() { Scope->SetHeld(false); UE_LOG(LogMindKore, Log, TEXT("Scope switch: done")); });
	}

	FAutoConsoleCommandWithWorldAndArgs GScopeSwitchStills(
		TEXT("os.Scope.SwitchStills"),
		TEXT("os.Scope.SwitchStills [Gun] [To]: aimed with Gun (2), then To (0) selected with the aim held - stills and a per-tick log of the aim and the gun's field of view (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunScopeSwitchStills));

	FAutoConsoleCommandWithWorldAndArgs GAimStills(
		TEXT("os.Scope.AimStills"),
		TEXT("os.Scope.AimStills <gun> <eye:fov> ...: a settled aimed still for each eye distance / gun field of view (HighResShot; needs a real RHI)."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunAimStills));
}
