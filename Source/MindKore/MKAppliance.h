#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MKInteractable.h"
#include "MKAppliance.generated.h"

class UAudioComponent;
class UMaterialInterface;
class USoundAttenuation;
class USoundBase;

/** What one named component of the appliance does while a state lasts. */
USTRUCT(BlueprintType)
struct MINDKORE_API FMKAppliancePart
{
	GENERATED_BODY()

	/** Name of a component on the appliance (the Blueprint variable name). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FName Component;

	/** Move to Pose (relative transform) over MoveSeconds; 0 = at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	bool bMove = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance", meta = (EditCondition = "bMove"))
	FTransform Pose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance", meta = (EditCondition = "bMove", ClampMin = "0.0"))
	float MoveSeconds = 0.0f;

	/** Constant turn while the state lasts, degrees per second (the plate). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FRotator SpinPerSecond = FRotator::ZeroRotator;

	/** Light components only. Negative = leave alone, 0 = off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	float LightIntensity = -1.0f;

	/** Primitive components only: material for slot 0. Null = leave alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	TObjectPtr<UMaterialInterface> Material = nullptr;

	/** Mesh components only: scalar parameter on its materials (emissive). None = leave alone. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FName ScalarParameter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	float ScalarValue = 0.0f;
};

USTRUCT(BlueprintType)
struct MINDKORE_API FMKApplianceState
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FName Name;

	/** Verb offered to the hand. Empty = the appliance offers nothing in this state. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FText Action;

	/** Not empty = busy: the verb is shown but refused with this physical reason. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FText BusyReason;

	/** 0 = waits for the hand; otherwise goes to NextOnTimer after this long. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance", meta = (ClampMin = "0.0"))
	float Seconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FName NextOnUse;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FName NextOnTimer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	TArray<FMKAppliancePart> Parts;

	/** Played once on entering. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Sound")
	TObjectPtr<USoundBase> EnterSound = nullptr;

	/** Plays while the state lasts. Null = silence. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Sound")
	TObjectPtr<USoundBase> LoopSound = nullptr;

	/** Noise for the AI. 0 = none. Placeholder values until the noise classes are tuned (DESIGN_INTERACTION_AND_PHYSICS 7.1). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Noise", meta = (ClampMin = "0.0"))
	float NoiseLoudness = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Noise")
	FName NoiseTag;

	/** 0 = once on entering; otherwise repeated at this period while the state lasts. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Noise", meta = (ClampMin = "0.0"))
	float NoisePeriod = 0.0f;
};

UENUM()
enum class EMKApplianceEvent : uint8
{
	Use,
	Timer,
};

/** What a state offers the hand: nothing, the verb, or the verb refused with the busy reason. */
MINDKORE_API FMKInteractionOffer MKApplianceOffer(const FMKApplianceState& State);

/** The whole transition rule. NAME_None = the state stays. A busy state ignores the hand; a state with no duration ignores the timer. */
MINDKORE_API FName MKApplianceNext(const FMKApplianceState& State, EMKApplianceEvent Event);

/**
 * An ordinary household thing that works the way it would in life (toilet,
 * microwave, television): DESIGN_INTERACTION_AND_PHYSICS 0.4. A small authored
 * cycle of states; Blueprint children add the components and fill States, no
 * graph logic. One verb per state, short press only; a busy state refuses
 * with a physical reason, so the noise cannot be spammed. Moving parts follow
 * authored poses, not physics. Nothing is saved: a loaded level starts in
 * InitialState, silently.
 */
UCLASS()
class MINDKORE_API AMKAppliance : public AActor, public IMKInteractable
{
	GENERATED_BODY()

public:
	AMKAppliance();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Appliance")
	TObjectPtr<USceneComponent> Root = nullptr;

	/** The one loop voice; one-shots are played at the actor's location. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MindKore|Appliance")
	TObjectPtr<UAudioComponent> LoopAudio = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	TArray<FMKApplianceState> States;

	/** State at level start. None = the first one. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance")
	FName InitialState;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Sound")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	/** Level of this appliance's loop and one-shots against the files' own level. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MindKore|Appliance|Sound", meta = (ClampMin = "0.0"))
	float Volume = 1.0f;

	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Appliance")
	FName CurrentState;

	/** Noises reported since BeginPlay: for tests and the PIE probe. */
	UPROPERTY(BlueprintReadOnly, Transient, Category = "MindKore|Appliance")
	int32 NoiseCount = 0;

	/** Same as the hand: for level script and the PIE probe. */
	UFUNCTION(BlueprintCallable, Category = "MindKore|Appliance")
	void Use(AActor* User);

	// IMKInteractable
	virtual FMKInteractionOffer QueryInteraction(const AActor* Hand) const override;
	virtual void CompleteInteraction(AActor* Hand) override;

	virtual void Tick(float DeltaSeconds) override;

protected:
	virtual void BeginPlay() override;

private:
	struct FMove
	{
		TWeakObjectPtr<USceneComponent> Part;
		FTransform From;
		FTransform To;
		float Seconds = 0.0f;
		float Elapsed = 0.0f;
	};
	struct FSpin
	{
		TWeakObjectPtr<USceneComponent> Part;
		FRotator PerSecond = FRotator::ZeroRotator;
	};

	const FMKApplianceState* Find(FName Name) const;
	/** bAtRest: level start - poses snap, no one-shot sound, no entry noise. */
	void Enter(const FMKApplianceState& State, bool bAtRest);
	void ReportNoise(const FMKApplianceState& State);

	TArray<FMove> Moves;
	TArray<FSpin> Spins;
	float StateElapsed = 0.0f;
	int32 NoisePeriods = 0;
};
