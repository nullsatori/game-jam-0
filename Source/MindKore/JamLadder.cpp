#include "JamLadder.h"
#include "Components/BoxComponent.h"
#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#endif

AJamLadder::AJamLadder()
{
	Target = CreateDefaultSubobject<UBoxComponent>(TEXT("Target"));
	SetRootComponent(Target);
	Target->SetBoxExtent(FVector(42, 55, 240));
	Target->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	Target->SetCanEverAffectNavigation(false);
}

bool AJamLadder::IsInReach(const FVector& Position) const
{
	const FVector Local = Target->GetComponentTransform().InverseTransformPosition(Position);
	const FVector Extent = Target->GetUnscaledBoxExtent();
	return FMath::Abs(Local.X) <= Extent.X && FMath::Abs(Local.Y) <= Extent.Y
		&& Local.Z >= Bottom.Z - 5 && Local.Z <= Top.Z + 5;
}

bool AJamLadder::CanEnter(const FVector& Position, const FVector& MoveDirection) const
{
	if (MoveDirection.IsNearlyZero() || !IsInReach(Position))
	{
		return false;
	}
	const FVector Local = GetActorTransform().InverseTransformPosition(Position);
	const FVector Move = GetActorTransform().InverseTransformVectorNoScale(MoveDirection);
	return Move.Y >= -0.1 && !(Local.Z >= Top.Z - 10 && Move.Z >= 0)
		&& !(Local.Z <= Bottom.Z + 10 && Move.Z <= 0);
}

#if WITH_DEV_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJamLadderEntryTest, "Jam.Ladder.MovementEntry",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FJamLadderEntryTest::RunTest(const FString& Parameters)
{
	UWorld* World = UWorld::CreateWorld(EWorldType::EditorPreview, false);
	AJamLadder* Ladder = World->SpawnActor<AJamLadder>();
	const FVector Top(0, 0, Ladder->Top.Z);
	const FVector Bottom(0, 0, Ladder->Bottom.Z);
	TestTrue(TEXT("Looking down descends"), Ladder->CanEnter(Top, FVector(0, 1, -1)));
	TestTrue(TEXT("Looking up ascends"), Ladder->CanEnter(Bottom, FVector(0, 1, 1)));
	TestTrue(TEXT("Falling onto ladder grabs"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, 0, -1)));
	TestTrue(TEXT("Jumping onto ladder grabs"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, 1, 1)));
	TestFalse(TEXT("No motion does not grab"), Ladder->CanEnter(FVector::ZeroVector, FVector::ZeroVector));
	TestFalse(TEXT("Moving away does not grab"), Ladder->CanEnter(FVector::ZeroVector, FVector(0, -1, 0)));
	TestFalse(TEXT("Distant landing does not pull player"), Ladder->CanEnter(Ladder->Top, FVector(0, 1, -1)));
	TestFalse(TEXT("At top upward motion stays free"), Ladder->CanEnter(Top, FVector(0, 1, 1)));
	World->DestroyWorld(false);
	return true;
}
#endif
