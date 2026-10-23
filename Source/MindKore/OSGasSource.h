// A gas source for the level (v11 stage 4, Docs/GasSystem.md): a component on any actor lets out UOSGasSubsystem's
// parcels from a point, a segment, a ring, a disc or a box - a burst or a steady flow - by a preset or by hand.

#pragma once

#include "Components/SceneComponent.h"
#include "CoreMinimal.h"
#include "OSGas.h"
#include "OSGasSource.generated.h"

/** What a source lets out (its recipe, look, mode and amount): the six presets, or Custom (the component's own). */
UENUM(BlueprintType)
enum class EOSGasPreset : uint8
{
	Custom,
	/** A muzzle's blast: a quick burst with a vortex ring. */
	Muzzle,
	/** A port or a vent hole: a small puff out of it. */
	Port,
	/** Welding fumes: a thin warm rising flow. */
	Weld,
	/** A hull breach: in a vacuum a straight jet out, in air the room's air drawn to the hole as well. */
	Breach,
	/** A thruster's short cold cone. */
	Rcs,
	/** Dust: heavy, it settles in gravity and hangs in zero-G. */
	Dust,
};

UENUM(BlueprintType)
enum class EOSGasShape : uint8
{
	Point,
	/** Along the component's X, Size.X long. */
	Segment,
	/** A circle of radius Size.Y in the component's YZ plane. */
	Ring,
	/** A filled circle of radius Size.Y in the YZ plane. */
	Disc,
	/** A box of half-extents Size. */
	Box,
};

UENUM(BlueprintType)
enum class EOSGasMode : uint8
{
	/** Count parcels at once (Burst()). */
	Burst,
	/** Rate parcels a second until Stop(). */
	Continuous,
};

/** One preset: what it lets out and how. */
struct FOSGasPresetDef
{
	FOSGasRecipe Recipe;
	FOSGasLook Look;
	EOSGasMode Mode = EOSGasMode::Burst;
	float Rate = 0.0f;
	int32 Count = 0;
};

/** A preset's definition (Custom: an empty one). */
MINDKORE_API FOSGasPresetDef OSGasPreset(EOSGasPreset Preset);
/** A preset's name for the console (muzzle, port, weld, breach, rcs, dust); Custom for none. */
MINDKORE_API EOSGasPreset OSGasPresetByName(const FString& Name);

UCLASS(ClassGroup = (OS), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UOSGasSourceComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UOSGasSourceComponent();

	/**
	 * Picking a preset in the editor fills the recipe, the look, the mode and the amount below (they can be tuned after);
	 * BeginPlay leaves them as they are. Custom changes nothing.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	EOSGasPreset Preset = EOSGasPreset::Custom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	EOSGasShape Shape = EOSGasShape::Point;

	/** The shape's size, cm (see EOSGasShape). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	FVector Size = FVector(20.0f, 20.0f, 20.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	EOSGasMode Mode = EOSGasMode::Burst;

	/** Continuous: parcels a second. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0.0"))
	float Rate = 20.0f;

	/** Burst: parcels at once. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas", meta = (ClampMin = "0"))
	int32 Count = 30;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	FOSGasRecipe Recipe;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	FOSGasLook Look;

	/** The gas leaves with the component's velocity (it moves with it while let out); off: from where it is, still. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	bool bCarried = true;

	/** BeginPlay: a burst at once, or the flow started. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Gas")
	bool bAutoStart = true;

	/** Count parcels now (whatever the mode). */
	UFUNCTION(BlueprintCallable, Category = "OS|Gas")
	void Burst();

	/** Starts (or changes) the flow, parcels a second; 0 stops it. */
	UFUNCTION(BlueprintCallable, Category = "OS|Gas")
	void SetRate(float PerSecond);

	UFUNCTION(BlueprintCallable, Category = "OS|Gas")
	void Stop();

	/** Applies a preset now (its recipe, look, mode and amount). */
	UFUNCTION(BlueprintCallable, Category = "OS|Gas")
	void ApplyPreset(EOSGasPreset NewPreset);

	virtual void BeginPlay() override;
#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
#endif
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/** Count parcels at points of the shape (each its own Emit: the shape's spread), within the budget. */
	void Let(int32 Number, bool bContinuous);
	FVector PointOnShape() const;

	bool bFlowing = false;
	float Carry = 0.0f;
	float BreachFlowAge = 0.0f;
	FVector LastLocation = FVector::ZeroVector;
	/** Its own cloud (rule 3h: a source writes only into its own). */
	int32 Cloud = INDEX_NONE;
	uint32 CloudSerial = 0;
};
