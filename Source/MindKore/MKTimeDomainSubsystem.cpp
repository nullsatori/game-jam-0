#include "MKTimeDomainSubsystem.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "MKImpactTypes.h"

bool UMKTimeDomainSubsystem::DoesSupportWorldType(EWorldType::Type Type) const
{
	return Type == EWorldType::Game || Type == EWorldType::PIE;
}

void UMKTimeDomainSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	LastRealSeconds = GetWorld()->GetRealTimeSeconds();
}

TStatId UMKTimeDomainSubsystem::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UMKTimeDomainSubsystem, STATGROUP_Tickables);
}

bool UMKTimeDomainSubsystem::SetRequested(APawn* Player, bool bRequested)
{
	if (!Player || Player->GetWorld() != GetWorld()) return false;
	if (!bRequested)
	{
		if (Operator.Get() == Player) { Clock.Release(); RestoreWorld(); }
		return false;
	}
	if (!Player->IsLocallyControlled() || !Player->IsPlayerControlled()
		|| UGameplayStatics::IsGamePaused(GetWorld())) return false;
	if (Operator.IsValid() && Operator.Get() != Player) return false;
	Operator = Player;
	if (!Clock.Press()) return false;
	EntryAge = 0.0f;
	BaselineDilation = GetWorld()->GetWorldSettings()->TimeDilation;
	bOwnsDilation = true;
	LastRealSeconds = GetWorld()->GetRealTimeSeconds();
	return true;
}

void UMKTimeDomainSubsystem::RestoreWorld()
{
	if (!bOwnsDilation) return;
	if (GetWorld() && GetWorld()->GetWorldSettings())
		GetWorld()->GetWorldSettings()->SetTimeDilation(BaselineDilation);
	bOwnsDilation = false;
}

void UMKTimeDomainSubsystem::CancelForOwner(APawn* Player)
{
	if (Operator.Get() != Player) return;
	Clock.Exit(EMKSlowExit::OwnerLost);
	Clock.Release();
	RestoreWorld();
	Operator.Reset();
}

void UMKTimeDomainSubsystem::NotifyDecisiveImpact(const FMKResolvedImpact& Impact)
{
	if (!Clock.IsSlowed() || !Impact.ShotId.IsValid() || !Operator.IsValid()
		|| Impact.SourceActor.Get() != Operator.Get()) return;
	Clock.Exit(EMKSlowExit::DecisiveImpact);
	RestoreWorld();
}

void UMKTimeDomainSubsystem::RefillResource()
{
	if (Clock.Phase == EMKSlowPhase::Idle) Clock.Remaining = 3.0f;
}

void UMKTimeDomainSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
	const double Now = GetWorld()->GetRealTimeSeconds();
	const float RealDelta = FMath::Max(0.0, Now - LastRealSeconds);
	LastRealSeconds = Now;
	if (Clock.IsSlowed() && (!Operator.IsValid() || !Operator->IsPlayerControlled()))
	{
		Clock.Exit(EMKSlowExit::OwnerLost);
		Clock.Release();
		Operator.Reset();
	}
	Clock.Advance(RealDelta, UGameplayStatics::IsGamePaused(GetWorld()));
	if (!UGameplayStatics::IsGamePaused(GetWorld())) EntryAge += RealDelta;
	if (Clock.IsSlowed())
		GetWorld()->GetWorldSettings()->SetTimeDilation(BaselineDilation * FMath::Lerp(1.0f, 0.2f, Clock.Alpha));
	if (!Clock.IsSlowed()) RestoreWorld();
}

void UMKTimeDomainSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
	RestoreWorld();
	Clock = FMKTimeDomainState();
	Operator.Reset();
	Super::OnWorldEndPlay(InWorld);
}

void UMKTimeDomainSubsystem::Deinitialize()
{
	RestoreWorld();
	Super::Deinitialize();
}
