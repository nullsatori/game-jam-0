#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameFramework/Pawn.h"
#include "MKTimeDomainState.h"
#include "MKTimeDomainSubsystem.generated.h"

class APawn;
struct FMKResolvedImpact;

/** Sole world-dilation writer. Physical player and world share the Chaos clock. */
UCLASS()
class MINDKORE_API UMKTimeDomainSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void OnWorldEndPlay(UWorld& InWorld) override;
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual TStatId GetStatId() const override;

	UFUNCTION(BlueprintCallable, Category="MindKore|BulletTime")
	bool SetRequested(APawn* Player, bool bRequested);
	UFUNCTION(BlueprintCallable, Category="MindKore|BulletTime")
	void CancelForOwner(APawn* Player);
	/** Explicit fixture/checkpoint supply; no automatic recharge economy. */
	UFUNCTION(BlueprintCallable, Category="MindKore|BulletTime")
	void RefillResource();
	UFUNCTION(BlueprintPure, Category="MindKore|BulletTime")
	bool IsSlowed() const { return Clock.IsSlowed(); }
	UFUNCTION(BlueprintPure, Category="MindKore|BulletTime")
	float GetPresentationAlpha() const { return Clock.Alpha; }
	UFUNCTION(BlueprintPure, Category="MindKore|BulletTime")
	float GetRemainingSeconds() const { return Clock.Remaining; }
	APawn* GetOperator() const { return Operator.Get(); }
	float GetEntryAge() const { return EntryAge; }
	void NotifyDecisiveImpact(const FMKResolvedImpact& Impact);

protected:
	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
private:
	FMKTimeDomainState Clock;
	TWeakObjectPtr<APawn> Operator;
	float BaselineDilation = 1.0f;
	double LastRealSeconds = 0.0;
	bool bOwnsDilation = false;
	float EntryAge = 100.0f;
	void RestoreWorld();
};
