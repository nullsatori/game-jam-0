#include "OSHullTools.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "OSShipParts.h"
#include "OSWeldProjector.h"
#include "OSZeroGPawn.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "MindKore.h"
#include "UObject/ConstructorHelpers.h"

#define LOCTEXT_NAMESPACE "OSHullTools"

UOSHullToolsComponent::UOSHullToolsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	ThreadMaterial = TSoftObjectPtr<UMaterialInterface>(FSoftObjectPath(TEXT("/Game/OSArt/Materials/Hull/M_OS_StrapLaser.M_OS_StrapLaser")));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cylinder(TEXT("/Engine/BasicShapes/Cylinder.Cylinder"));
	ThreadMesh = Cylinder.Object;
}

UPrimitiveComponent* UOSHullToolsComponent::OwnerBody() const
{
	const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner());
	return Pawn ? Pawn->Body.Get() : nullptr;
}

bool UOSHullToolsComponent::TraceView(float Reach, FHitResult& OutHit) const
{
	const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}
	// From the eye without the camera shake, like the pawn's own aim (a shot or a bite must not move the torch).
	const FVector From = Pawn->ShoulderLocation();
	const FVector To = From + Pawn->GetControlRotation().Vector() * Reach;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(OSHullTool), /*bTraceComplex=*/false, Pawn);
	return GetWorld()->LineTraceSingleByChannel(OutHit, From, To, ECC_Visibility, Params) && OutHit.GetComponent();
}

EOSHullTool UOSHullToolsComponent::GetTool() const
{
	const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner());
	return Pawn && Pawn->GetHands() == EOSHands::Tool ? Tool : EOSHullTool::None;
}

void UOSHullToolsComponent::TakeOut(EOSHullTool NewTool)
{
	if (NewTool != EOSHullTool::None)
	{
		Tool = NewTool;
	}
	bTorchOn = false;
	WeldStatus = FText::GetEmpty();
	UE_LOG(LogMindKore, Log, TEXT("Hull tools: %s in hand"), Tool == EOSHullTool::Strap ? TEXT("the strap") : TEXT("the welder"));
}

void UOSHullToolsComponent::PutAway()
{
	bTorchOn = false;
	WeldStatus = FText::GetEmpty();
	UE_LOG(LogMindKore, Log, TEXT("Hull tools: nothing in hand"));
}

// --- strap -------------------------------------------------------------------------

int32 UOSHullToolsComponent::AddLine(UPrimitiveComponent* A, const FVector& PointA, UPrimitiveComponent* B, const FVector& PointB, bool bBelt)
{
	if (!A || !B || A == B)
	{
		return INDEX_NONE;
	}
	FOSStrapLine& Line = Lines.AddDefaulted_GetRef();
	Line.A = A;
	Line.B = B;
	Line.LocalA = A->GetComponentTransform().InverseTransformPosition(PointA);
	Line.LocalB = B->GetComponentTransform().InverseTransformPosition(PointB);
	Line.bBelt = bBelt;
	Line.LengthCm = FMath::Max(MinLengthCm, float(FVector::Dist(PointA, PointB)));
	UE_LOG(LogMindKore, Log, TEXT("Strap: line %d %s -> %s, %.0f cm%s"), Lines.Num() - 1, *GetNameSafe(A->GetOwner()), *GetNameSafe(B->GetOwner()),
		Line.LengthCm, bBelt ? TEXT(" (on the belt)") : TEXT(""));
	return Lines.Num() - 1;
}

void UOSHullToolsComponent::SetLineLength(int32 Index, float LengthCm)
{
	if (Lines.IsValidIndex(Index))
	{
		Lines[Index].LengthCm = FMath::Max(MinLengthCm, LengthCm);
	}
}

void UOSHullToolsComponent::CutLine(int32 Index)
{
	if (Lines.IsValidIndex(Index))
	{
		UE_LOG(LogMindKore, Log, TEXT("Strap: line %d cut"), Index);
		Lines.RemoveAt(Index);
	}
}

void UOSHullToolsComponent::CutAll()
{
	UE_LOG(LogMindKore, Log, TEXT("Strap: all %d line(s) cut"), Lines.Num());
	Lines.Reset();
}

void UOSHullToolsComponent::PrimaryPressed()
{
	if (GetTool() == EOSHullTool::Welder)
	{
		bTorchOn = true;
		return;
	}
	if (GetTool() != EOSHullTool::Strap)
	{
		return;
	}
	FHitResult Hit;
	if (!TraceView(StrapReachCm, Hit))
	{
		UE_LOG(LogMindKore, Log, TEXT("Strap: nothing within %.0f cm"), StrapReachCm);
		return;
	}
	UPrimitiveComponent* Body = OwnerBody();
	// A line still on the belt: its belt end goes to what was clicked.
	if (FOSStrapLine* Towing = Lines.FindByPredicate([](const FOSStrapLine& Line) { return Line.bBelt; }))
	{
		if (Hit.GetComponent() == Towing->A.Get())
		{
			return; // the same body again: keep towing
		}
		Towing->B = Hit.GetComponent();
		Towing->LocalB = Hit.GetComponent()->GetComponentTransform().InverseTransformPosition(Hit.ImpactPoint);
		Towing->bBelt = false;
		if (const UPrimitiveComponent* A = Towing->A.Get())
		{
			Towing->LengthCm = FMath::Max(MinLengthCm, float(FVector::Dist(A->GetComponentTransform().TransformPosition(Towing->LocalA), Hit.ImpactPoint)));
		}
		UE_LOG(LogMindKore, Log, TEXT("Strap: line %s -> %s, %.0f cm, the hands free"), *GetNameSafe(Towing->A.IsValid() ? Towing->A->GetOwner() : nullptr),
			*GetNameSafe(Hit.GetActor()), Towing->LengthCm);
		return;
	}
	if (Lines.Num() >= MaxLines)
	{
		UE_LOG(LogMindKore, Log, TEXT("Strap: %d lines already (R cuts one)"), Lines.Num());
		return;
	}
	if (Body)
	{
		AddLine(Hit.GetComponent(), Hit.ImpactPoint, Body, Body->GetComponentLocation(), /*bBelt=*/true);
	}
}

void UOSHullToolsComponent::Reel(int32 Step)
{
	if (GetTool() != EOSHullTool::Strap || Lines.IsEmpty())
	{
		return;
	}
	FOSStrapLine& Line = Lines.Last();
	Line.LengthCm = FMath::Max(MinLengthCm, Line.LengthCm - Step * ReelStepCm);
	UE_LOG(LogMindKore, Verbose, TEXT("Strap: line %d reeled to %.0f cm"), Lines.Num() - 1, Line.LengthCm);
}

void UOSHullToolsComponent::SecondaryPressed()
{
	if (GetTool() == EOSHullTool::Welder)
	{
		bCutting = !bCutting;
		UE_LOG(LogMindKore, Log, TEXT("Welder: %s"), bCutting ? TEXT("cut") : TEXT("weld"));
		return;
	}
	if (GetTool() != EOSHullTool::Strap || Lines.IsEmpty())
	{
		return;
	}
	// The line nearest the crosshair ray; none close to it - the last one.
	const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner());
	const FVector From = Pawn->ShoulderLocation();
	const FVector Direction = Pawn->GetControlRotation().Vector();
	int32 Nearest = Lines.Num() - 1;
	float Best = 60.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FOSStrapLine& Line = Lines[Index];
		if (!Line.A.IsValid() || !Line.B.IsValid())
		{
			continue;
		}
		const FVector PA = Line.A->GetComponentTransform().TransformPosition(Line.LocalA);
		const FVector PB = Line.B->GetComponentTransform().TransformPosition(Line.LocalB);
		FVector OnRay;
		FVector OnLine;
		FMath::SegmentDistToSegmentSafe(From, From + Direction * 3000.0f, PA, PB, OnRay, OnLine);
		const float Distance = float(FVector::Dist(OnRay, OnLine));
		if (Distance < Best)
		{
			Best = Distance;
			Nearest = Index;
		}
	}
	CutLine(Nearest);
}

void UOSHullToolsComponent::ApplyLines(float DeltaSeconds)
{
	Lines.RemoveAll([](const FOSStrapLine& Line) { return !Line.A.IsValid() || !Line.B.IsValid(); });
	for (FOSStrapLine& Line : Lines)
	{
		UPrimitiveComponent* A = Line.A.Get();
		UPrimitiveComponent* B = Line.B.Get();
		const FVector PA = A->GetComponentTransform().TransformPosition(Line.LocalA);
		const FVector PB = B->GetComponentTransform().TransformPosition(Line.LocalB);
		const FVector Delta = PB - PA;
		const float Distance = float(Delta.Size());
		Line.Tension = 0.0f;
		if (Distance <= Line.LengthCm || Distance < KINDA_SMALL_NUMBER)
		{
			continue; // slack
		}
		const FVector Along = Delta / Distance;
		// Opening speed of the two ends along the line (damps the pull, no bounce).
		const FVector VA = A->IsSimulatingPhysics() ? A->GetPhysicsLinearVelocityAtPoint(PA) : FVector::ZeroVector;
		const FVector VB = B->IsSimulatingPhysics() ? B->GetPhysicsLinearVelocityAtPoint(PB) : FVector::ZeroVector;
		const float Opening = float(FVector::DotProduct(VB - VA, Along));
		const float Pull = FMath::Clamp(Stiffness * (Distance - Line.LengthCm) + Damping * Opening, 0.0f, MaxForce);
		Line.Tension = MaxForce > 0.0f ? Pull / MaxForce : 0.0f;
		// Equal and opposite; a static end anchors.
		if (A->IsSimulatingPhysics())
		{
			A->AddForceAtLocation(Along * Pull, PA);
		}
		if (B->IsSimulatingPhysics())
		{
			B->AddForceAtLocation(-Along * Pull, PB);
		}
	}
}

void UOSHullToolsComponent::DrawLines()
{
	UMaterialInterface* Look = ThreadMaterial.LoadSynchronous();
	if (!ThreadMesh || !Look)
	{
		return;
	}
	const FBox Bounds = ThreadMesh->GetBoundingBox();
	const FVector MeshSize = Bounds.GetSize();
	const float PixelAngle = UOSWeldProjectorComponent::ViewPixelAngle(Cast<APawn>(GetOwner()));
	for (int32 Index = 0; Index < FMath::Max(Lines.Num(), Threads.Num()); ++Index)
	{
		if (!Lines.IsValidIndex(Index))
		{
			Threads[Index]->SetVisibility(false);
			continue;
		}
		if (!Threads.IsValidIndex(Index))
		{
			UStaticMeshComponent* Thread = NewObject<UStaticMeshComponent>(GetOwner());
			Thread->SetStaticMesh(ThreadMesh);
			Thread->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Thread->SetGenerateOverlapEvents(false);
			Thread->SetCanEverAffectNavigation(false);
			Thread->SetCastShadow(false);
			Thread->bReceivesDecals = false;
			UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Look, this);
			Instance->SetScalarParameterValue(TEXT("ZMin"), float(Bounds.Min.Z));
			Instance->SetScalarParameterValue(TEXT("ZSpan"), float(FMath::Max(MeshSize.Z, 1.0)));
			Instance->SetScalarParameterValue(TEXT("RadiusCm"), ThreadRadiusCm);
			if (const AOSZeroGPawn* Pawn = Cast<AOSZeroGPawn>(GetOwner()); Pawn && Pawn->WeldProjector)
			{
				// One place for the colours: the projector's.
				Instance->SetVectorParameterValue(TEXT("Halo"), Pawn->WeldProjector->HaloColour);
				Instance->SetVectorParameterValue(TEXT("Core"), Pawn->WeldProjector->CoreColour);
			}
			Thread->SetMaterial(0, Instance);
			Thread->RegisterComponent();
			Threads.Add(Thread);
			ThreadLooks.Add(Instance);
		}
		const FOSStrapLine& Line = Lines[Index];
		const FVector PA = Line.A->GetComponentTransform().TransformPosition(Line.LocalA);
		const FVector PB = Line.B->GetComponentTransform().TransformPosition(Line.LocalB) - (Line.bBelt ? FVector(0.0f, 0.0f, 20.0f) : FVector::ZeroVector);
		const float Length = FMath::Max(float(FVector::Dist(PA, PB)), 1.0f);
		// The mesh's local Z runs from A to B; its bounds' centre goes on the middle of the line.
		const FQuat Turn = FRotationMatrix::MakeFromZ(PB - PA).ToQuat();
		const FVector Scale(2.0f * ThreadRadiusCm / FMath::Max(MeshSize.X, 1.0), 2.0f * ThreadRadiusCm / FMath::Max(MeshSize.Y, 1.0), Length / FMath::Max(MeshSize.Z, 1.0));
		Threads[Index]->SetWorldTransform(FTransform(Turn, (PA + PB) * 0.5f - Turn.RotateVector(Bounds.GetCenter() * Scale), Scale));
		Threads[Index]->SetVisibility(true);
		UMaterialInstanceDynamic* Instance = ThreadLooks[Index];
		Instance->SetScalarParameterValue(TEXT("Length"), Length);
		Instance->SetScalarParameterValue(TEXT("Tension"), Line.Tension > 0.0f ? Line.Tension : -1.0f);
		Instance->SetScalarParameterValue(TEXT("PixelAngle"), PixelAngle);
	}
}

// --- welder ------------------------------------------------------------------------

void UOSHullToolsComponent::PrimaryHeld(float DeltaSeconds)
{
	if (GetTool() != EOSHullTool::Welder || !bTorchOn)
	{
		return;
	}
	FHitResult Hit;
	if (!TraceView(WeldReachCm, Hit))
	{
		WeldStatus = LOCTEXT("TooFar", "Горелка: ближе");
		return;
	}
	// The breach whose seam is nearest the torch point.
	AOSPlateSocket* Nearest = nullptr;
	double Best = 400.0 * 400.0;
	for (TActorIterator<AOSPlateSocket> Socket(GetWorld()); Socket; ++Socket)
	{
		const double Distance = FVector::DistSquared(Socket->GetActorLocation(), Hit.ImpactPoint);
		if (Distance < Best)
		{
			Best = Distance;
			Nearest = *Socket;
		}
	}
	if (!Nearest)
	{
		WeldStatus = LOCTEXT("NoBreach", "Горелка: нечего варить");
		return;
	}
	switch (Nearest->Weld(Hit.ImpactPoint, DeltaSeconds, bCutting))
	{
	case EOSWeldResult::Welded:
		WeldStatus = LOCTEXT("Welding", "Шов ложится");
		break;
	case EOSWeldResult::Cut:
		WeldStatus = LOCTEXT("Cutting", "Режу шов");
		break;
	case EOSWeldResult::Gap:
		WeldStatus = LOCTEXT("Gap", "Зазор: дожми лист");
		break;
	case EOSWeldResult::Junk:
		WeldStatus = FText::Format(LOCTEXT("Junk", "В корпусе мусор: {0}"), Nearest->JunkCount());
		break;
	default:
		WeldStatus = LOCTEXT("OffSeam", "Веди по краю пробоины");
		break;
	}
}

void UOSHullToolsComponent::PrimaryReleased()
{
	bTorchOn = false;
	WeldStatus = FText::GetEmpty();
}

void UOSHullToolsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyLines(DeltaTime);
	DrawLines();
	PrimaryHeld(DeltaTime);
}

#undef LOCTEXT_NAMESPACE
