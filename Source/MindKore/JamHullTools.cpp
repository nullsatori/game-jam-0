#include "JamHullTools.h"

#include "Camera/CameraComponent.h"
#include "Components/SphereComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "JamShipParts.h"
#include "JamZeroGPawn.h"
#include "MindKore.h"

#define LOCTEXT_NAMESPACE "JamHullTools"

UJamHullToolsComponent::UJamHullToolsComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

UPrimitiveComponent* UJamHullToolsComponent::OwnerBody() const
{
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(GetOwner());
	return Pawn ? Pawn->Body.Get() : nullptr;
}

bool UJamHullToolsComponent::TraceView(float Reach, FHitResult& OutHit) const
{
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(GetOwner());
	if (!Pawn)
	{
		return false;
	}
	const FVector From = Pawn->Camera->GetComponentLocation();
	const FVector To = From + Pawn->GetControlRotation().Vector() * Reach;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(JamHullTool), /*bTraceComplex=*/false, Pawn);
	return GetWorld()->LineTraceSingleByChannel(OutHit, From, To, ECC_Visibility, Params) && OutHit.GetComponent();
}

void UJamHullToolsComponent::SelectTool(EJamHullTool NewTool)
{
	Tool = Tool == NewTool ? EJamHullTool::None : NewTool;
	bTorchOn = false;
	WeldStatus = FText::GetEmpty();
	UE_LOG(LogMindKore, Log, TEXT("Hull tools: %s in hand"), Tool == EJamHullTool::Strap ? TEXT("the strap") : Tool == EJamHullTool::Welder ? TEXT("the welder") : TEXT("nothing"));
}

// --- strap -------------------------------------------------------------------------

int32 UJamHullToolsComponent::AddLine(UPrimitiveComponent* A, const FVector& PointA, UPrimitiveComponent* B, const FVector& PointB, bool bBelt)
{
	if (!A || !B || A == B)
	{
		return INDEX_NONE;
	}
	FJamStrapLine& Line = Lines.AddDefaulted_GetRef();
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

void UJamHullToolsComponent::SetLineLength(int32 Index, float LengthCm)
{
	if (Lines.IsValidIndex(Index))
	{
		Lines[Index].LengthCm = FMath::Max(MinLengthCm, LengthCm);
	}
}

void UJamHullToolsComponent::CutLine(int32 Index)
{
	if (Lines.IsValidIndex(Index))
	{
		UE_LOG(LogMindKore, Log, TEXT("Strap: line %d cut"), Index);
		Lines.RemoveAt(Index);
	}
}

void UJamHullToolsComponent::CutAll()
{
	UE_LOG(LogMindKore, Log, TEXT("Strap: all %d line(s) cut"), Lines.Num());
	Lines.Reset();
}

void UJamHullToolsComponent::PrimaryPressed()
{
	if (Tool == EJamHullTool::Welder)
	{
		bTorchOn = true;
		return;
	}
	if (Tool != EJamHullTool::Strap)
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
	if (FJamStrapLine* Towing = Lines.FindByPredicate([](const FJamStrapLine& Line) { return Line.bBelt; }))
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

void UJamHullToolsComponent::Reel(int32 Step)
{
	if (Tool != EJamHullTool::Strap || Lines.IsEmpty())
	{
		return;
	}
	FJamStrapLine& Line = Lines.Last();
	Line.LengthCm = FMath::Max(MinLengthCm, Line.LengthCm - Step * ReelStepCm);
	UE_LOG(LogMindKore, Verbose, TEXT("Strap: line %d reeled to %.0f cm"), Lines.Num() - 1, Line.LengthCm);
}

void UJamHullToolsComponent::SecondaryPressed()
{
	if (Tool == EJamHullTool::Welder)
	{
		bCutting = !bCutting;
		UE_LOG(LogMindKore, Log, TEXT("Welder: %s"), bCutting ? TEXT("cut") : TEXT("weld"));
		return;
	}
	if (Tool != EJamHullTool::Strap || Lines.IsEmpty())
	{
		return;
	}
	// The line nearest the crosshair ray; none close to it - the last one.
	const AJamZeroGPawn* Pawn = Cast<AJamZeroGPawn>(GetOwner());
	const FVector From = Pawn->Camera->GetComponentLocation();
	const FVector Direction = Pawn->GetControlRotation().Vector();
	int32 Nearest = Lines.Num() - 1;
	float Best = 60.0f;
	for (int32 Index = 0; Index < Lines.Num(); ++Index)
	{
		const FJamStrapLine& Line = Lines[Index];
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

void UJamHullToolsComponent::ApplyLines(float DeltaSeconds)
{
	Lines.RemoveAll([](const FJamStrapLine& Line) { return !Line.A.IsValid() || !Line.B.IsValid(); });
	for (FJamStrapLine& Line : Lines)
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

void UJamHullToolsComponent::DrawLines() const
{
	// ponytail: debug lines (Development builds), straight; a UCableComponent if the lines must sag or ship in Shipping.
	for (const FJamStrapLine& Line : Lines)
	{
		const FVector PA = Line.A->GetComponentTransform().TransformPosition(Line.LocalA);
		const FVector PB = Line.B->GetComponentTransform().TransformPosition(Line.LocalB) - (Line.bBelt ? FVector(0.0f, 0.0f, 20.0f) : FVector::ZeroVector);
		// Slack white, taut yellow, at the limit red.
		const FColor Colour = Line.Tension <= 0.0f ? FColor(200, 200, 200) : Line.Tension < 0.95f ? FColor(255, 210, 60) : FColor(255, 60, 40);
		DrawDebugLine(GetWorld(), PA, PB, Colour, false, -1.0f, 0, Line.Tension > 0.0f ? 2.5f : 1.2f);
	}
}

// --- welder ------------------------------------------------------------------------

void UJamHullToolsComponent::PrimaryHeld(float DeltaSeconds)
{
	if (Tool != EJamHullTool::Welder || !bTorchOn)
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
	AJamPlateSocket* Nearest = nullptr;
	double Best = 400.0 * 400.0;
	for (TActorIterator<AJamPlateSocket> Socket(GetWorld()); Socket; ++Socket)
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
	case EJamWeldResult::Welded:
		WeldStatus = LOCTEXT("Welding", "Шов ложится");
		break;
	case EJamWeldResult::Cut:
		WeldStatus = LOCTEXT("Cutting", "Режу шов");
		break;
	case EJamWeldResult::Gap:
		WeldStatus = LOCTEXT("Gap", "Зазор: дожми лист");
		break;
	case EJamWeldResult::Junk:
		WeldStatus = FText::Format(LOCTEXT("Junk", "В корпусе мусор: {0}"), Nearest->JunkCount());
		break;
	default:
		WeldStatus = LOCTEXT("OffSeam", "Веди по краю пробоины");
		break;
	}
}

void UJamHullToolsComponent::PrimaryReleased()
{
	bTorchOn = false;
	WeldStatus = FText::GetEmpty();
}

void UJamHullToolsComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	ApplyLines(DeltaTime);
	DrawLines();
	PrimaryHeld(DeltaTime);
}

#undef LOCTEXT_NAMESPACE
