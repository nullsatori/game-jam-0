#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "JamNixieCounter.generated.h"

class UMaterialInstanceDynamic;
class UMaterialInterface;
class UMeshComponent;

USTRUCT()
struct FJamNixieMaterialBinding
{
	GENERATED_BODY()

	int32 Slot = INDEX_NONE;
	UPROPERTY(Transient) TObjectPtr<UMaterialInterface> OriginalOverride = nullptr;
	UPROPERTY(Transient) TObjectPtr<UMaterialInstanceDynamic> Instance = nullptr;
};

/** One digit on a weapon or world prop. Owns its material instances; values change by event, never by Tick. */
UCLASS(ClassGroup = (Jam), meta = (BlueprintSpawnableComponent))
class MINDKORE_API UJamNixieCounterComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UJamNixieCounterComponent();

	/** Select this actor's display mesh. An empty reference uses the actor's root, if it is a mesh. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Nixie", meta = (UseComponentPicker, AllowedClasses = "/Script/Engine.MeshComponent"))
	FComponentReference TargetMesh;

	/** Slots must already have a Nixie material exposing SP_DigitIndex and SP_CounterEnabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|Nixie")
	TArray<int32> MaterialSlots = { 0 };

	/** 0..9 are digits, 10 is blank. Any other input becomes blank, never a misleading clamped digit. */
	UFUNCTION(BlueprintCallable, Category = "Jam|Nixie")
	void SetDigit(int32 Value);

	/** Power off preserves the selected digit for the next power on. */
	UFUNCTION(BlueprintCallable, Category = "Jam|Nixie")
	void SetCounterEnabled(bool bEnabled);

	UFUNCTION(BlueprintPure, Category = "Jam|Nixie")
	int32 GetDigit() const { return Digit; }

	UFUNCTION(BlueprintPure, Category = "Jam|Nixie")
	bool IsCounterEnabled() const { return bCounterEnabled; }

	/** Runtime mesh binding (for spawned parts). Call again after replacing a mesh/material. Returns bound slot count. */
	UFUNCTION(BlueprintCallable, Category = "Jam|Nixie")
	int32 BindMesh(UMeshComponent* Mesh, const TArray<int32>& Slots);

	/** Resolve TargetMesh and MaterialSlots. BeginPlay calls this automatically. */
	UFUNCTION(BlueprintCallable, Category = "Jam|Nixie")
	int32 RefreshBinding();

	/** Restore only slots still owned by this counter; never overwrite another system's replacement material. */
	UFUNCTION(BlueprintCallable, Category = "Jam|Nixie")
	void ReleaseBinding();

	UFUNCTION(BlueprintPure, Category = "Jam|Nixie")
	int32 GetBoundMaterialCount() const { return Bindings.Num(); }

protected:
	virtual void BeginPlay() override;
	virtual void OnRegister() override;
	virtual void OnUnregister() override;

private:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jam|Nixie", meta = (AllowPrivateAccess = "true", ClampMin = "0", ClampMax = "10"))
	int32 Digit = 10;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Jam|Nixie", meta = (AllowPrivateAccess = "true"))
	bool bCounterEnabled = true;

	UPROPERTY(Transient) TObjectPtr<UMeshComponent> BoundMesh = nullptr;
	UPROPERTY(Transient) TArray<FJamNixieMaterialBinding> Bindings;

	void ApplyState();
};
