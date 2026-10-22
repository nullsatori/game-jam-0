#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "OSNixieCounter.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;

USTRUCT()
struct FOSNixieMaterialBinding
{
	GENERATED_BODY()

	int32 Slot = INDEX_NONE;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> OriginalOverride = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Instance = nullptr;
	/** The material has the v5 event clocks (SP_PrevDigit, SP_ChangeTime, SP_PowerTime). */
	bool bHasClocks = false;
};

/**
 * One digit on a weapon or world prop. Owns its material instances; values change by event, never by Tick.
 * Each change also stamps the world time into the material (v5): the shader plays the ignition, the old
 * digit's afterglow and the power-on stutter from Time - stamp, so they look the same at any frame rate.
 */
UCLASS(ClassGroup = (OS), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UOSNixieCounterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UOSNixieCounterComponent();

	/** Select this actor's display mesh. An empty reference uses the actor's root, if it is a mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Nixie", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.MeshComponent"))
	FComponentReference TargetMesh;

	/** Slots must already have a Nixie material exposing SP_DigitIndex and SP_CounterEnabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "OS|Nixie")
	TArray<int32> MaterialSlots = { 0 };

	/** 0..9 are digits, 10 is blank. Any other input becomes blank, never a misleading clamped digit. */
	UFUNCTION(BlueprintCallable, Category = "OS|Nixie")
	void SetDigit(int32 Value);

	/** Power off preserves the selected digit for the next power on. */
	UFUNCTION(BlueprintCallable, Category = "OS|Nixie")
	void SetCounterEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "OS|Nixie")
	int32 GetDigit() const { return Digit; }

	UFUNCTION(BlueprintPure, Category = "OS|Nixie")
	bool IsCounterEnabled() const { return bCounterEnabled; }

	/** Runtime mesh binding (for spawned parts). Call again after replacing a mesh/material. Returns bound slot count. */
	UFUNCTION(BlueprintCallable, Category = "OS|Nixie")
	int32 BindMesh(UMeshComponent* Mesh, const TArray<int32>& Slots);

	/** Resolve TargetMesh and MaterialSlots. BeginPlay calls this automatically. */
	UFUNCTION(BlueprintCallable, Category = "OS|Nixie")
	int32 RefreshBinding();

	/** Restore only slots still owned by this counter; never overwrite another system's replacement material. */
	UFUNCTION(BlueprintCallable, Category = "OS|Nixie")
	void ReleaseBinding();

	UFUNCTION(BlueprintPure, Category = "OS|Nixie")
	int32 GetBoundMaterialCount() const { return Bindings.Num(); }

	/** The digit lit before the last change (10 when the tube was dark); its afterglow fades in the shader. */
	UFUNCTION(BlueprintPure, Category = "OS|Nixie")
	int32 GetPreviousDigit() const { return PrevDigit; }

	/** World seconds of the last digit change / power switch; far in the past until the first one. */
	double GetChangeTime() const { return ChangeTime; }
	double GetPowerTime() const { return PowerTime; }

protected:
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OS|Nixie", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "10"))
	int32 Digit = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "OS|Nixie", meta = (AllowPrivateAccess = "true"))
	bool bCounterEnabled = true;

	int32 PrevDigit = 10;
	double ChangeTime = -1000.0;
	double PowerTime = -1000.0;

	UPROPERTY(Transient) TObjectPtr<UMeshComponent> BoundMesh = nullptr;
	UPROPERTY(Transient) TArray<FOSNixieMaterialBinding> Bindings;

	void ApplyState();
	double Now() const;
};
