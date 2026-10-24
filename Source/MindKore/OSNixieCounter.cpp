#include "OSNixieCounter.h"

#include "Components/MeshComponent.h"
#include "Components/PointLightComponent.h"
#include "HAL/IConsoleManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/World.h"
#include "MindKore.h"

namespace
{
	const FName DigitParameter(TEXT("SP_DigitIndex"));
	const FName EnabledParameter(TEXT("SP_CounterEnabled"));
	const FName FlickerPhaseParameter(TEXT("SP_FlickerPhase"));
	const FName PrevDigitParameter(TEXT("SP_PrevDigit"));
	const FName ChangeTimeParameter(TEXT("SP_ChangeTime"));
	const FName PowerTimeParameter(TEXT("SP_PowerTime"));

	int32 SafeDigit(int32 Value) { return Value >= 0 && Value <= 9 ? Value : 10; }
}

UOSNixieCounterComponent::UOSNixieCounterComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UOSNixieCounterComponent::BeginPlay()
{
	Super::BeginPlay();
	RefreshBinding();
}

void UOSNixieCounterComponent::OnRegister()
{
	Super::OnRegister();
	if (HasBegunPlay()) RefreshBinding();
}

void UOSNixieCounterComponent::OnUnregister()
{
	ReleaseBinding();
	Super::OnUnregister();
}

void UOSNixieCounterComponent::SetDigit(int32 Value)
{
	const int32 SafeValue = SafeDigit(Value);
	if (Digit == SafeValue) return;
	// A dark tube has nothing to fade; a lit one leaves its old cathode glowing for a few ms.
	PrevDigit = bCounterEnabled ? Digit : 10;
	Digit = SafeValue;
	ChangeTime = Now();
	ApplyState();
}

void UOSNixieCounterComponent::SetCounterEnabled(bool bEnabled)
{
	if (bCounterEnabled == bEnabled) return;
	bCounterEnabled = bEnabled;
	PowerTime = Now();
	ApplyState();
}

double UOSNixieCounterComponent::Now() const
{
	// The material's Time node is the same world clock (paused and dilated with the world).
	const UWorld* World = GetWorld();
	return World ? World->GetTimeSeconds() : 0.0;
}

void UOSNixieCounterComponent::ApplyState()
{
	// The glow for the gas: lit with a digit, its strength by os.Gas.NixieLamp (read when the digit changes).
	if (GasGlow)
	{
		static const IConsoleVariable* Strength = IConsoleManager::Get().FindConsoleVariable(TEXT("os.Gas.NixieLamp"));
		const float Scale = Strength ? Strength->GetFloat() : 1.0f;
		const bool bLit = bCounterEnabled && Digit < 10 && Scale > 0.0f;
		GasGlow->SetIntensity(bLit ? 2.0f * Scale : 0.0f);
		GasGlow->SetVisibility(bLit);
	}
	for (const FOSNixieMaterialBinding& Binding : Bindings)
	{
		Binding.Instance->SetScalarParameterValue(DigitParameter, float(Digit));
		Binding.Instance->SetScalarParameterValue(EnabledParameter, bCounterEnabled ? 1.0f : 0.0f);
		if (!Binding.bHasClocks) continue;
		Binding.Instance->SetScalarParameterValue(PrevDigitParameter, float(PrevDigit));
		Binding.Instance->SetScalarParameterValue(ChangeTimeParameter, float(ChangeTime));
		Binding.Instance->SetScalarParameterValue(PowerTimeParameter, float(PowerTime));
	}
}

int32 UOSNixieCounterComponent::RefreshBinding()
{
	return BindMesh(Cast<UMeshComponent>(TargetMesh.GetComponent(GetOwner())), MaterialSlots);
}

int32 UOSNixieCounterComponent::BindMesh(UMeshComponent* Mesh, const TArray<int32>& Slots)
{
	ReleaseBinding();
	TargetMesh.OverrideComponent = Mesh;
	MaterialSlots = Slots;
	Digit = SafeDigit(Digit);
	if (!IsValid(Mesh)) return 0;
	BoundMesh = Mesh;
	// The digit's glow on the smoke (v13 13b): 30 cm, orange, for the gas only - the world's renderer never sees it.
	if (GetWorld() && GetWorld()->IsGameWorld())
	{
		if (!GasGlow)
		{
			GasGlow = NewObject<UPointLightComponent>(GetOwner(), TEXT("NixieGasGlow"));
			GasGlow->SetMobility(EComponentMobility::Movable);
			GasGlow->bAffectsWorld = false;
			GasGlow->SetCastShadows(false);
			GasGlow->bUseInverseSquaredFalloff = false;
			GasGlow->LightFalloffExponent = 2.0f;
			GasGlow->SetIntensityUnits(ELightUnits::Unitless);
			GasGlow->SetAttenuationRadius(30.0f);
			GasGlow->SetLightColor(FLinearColor(1.0f, 0.35f, 0.08f));
			GasGlow->ComponentTags.Add(TEXT("GasOnly"));
			GasGlow->RegisterComponent();
		}
		GasGlow->AttachToComponent(Mesh, FAttachmentTransformRules::SnapToTargetNotIncludingScale);
	}
	for (const int32 Slot : MaterialSlots)
	{
		if (Slot < 0 || Slot >= Mesh->GetNumMaterials() || Bindings.ContainsByPredicate(
			[Slot](const FOSNixieMaterialBinding& Binding) { return Binding.Slot == Slot; })) continue;

		UMaterialInterface* Material = Mesh->GetMaterial(Slot);
		float Unused = 0.0f;
		if (!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(DigitParameter), Unused)
			|| !Material->GetScalarParameterValue(FMaterialParameterInfo(EnabledParameter), Unused))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Nixie %s: %s slot %d lacks the two Nixie scalar parameters; skipped."),
				*GetName(), *Mesh->GetName(), Slot);
			continue;
		}

		UMaterialInstanceDynamic* ExistingMID = Cast<UMaterialInstanceDynamic>(Material);
		if (ExistingMID && Cast<UOSNixieCounterComponent>(ExistingMID->GetOuter()))
		{
			UE_LOG(LogMindKore, Warning, TEXT("Nixie %s: %s slot %d already belongs to another counter; skipped."),
				*GetName(), *Mesh->GetName(), Slot);
			continue;
		}

		// Copy an existing MID's overrides onto a sibling, never parent a MID to a MID or mutate it.
		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(ExistingMID ? ExistingMID->Parent.Get() : Material, this);
		if (!Instance) continue;
		if (ExistingMID) Instance->CopyParameterOverrides(ExistingMID);
		if (Material->GetScalarParameterValue(FMaterialParameterInfo(FlickerPhaseParameter), Unused))
		{
			// Same phase across this display's slots and rebinds, different across live components.
			// The shader animates from Time; no runtime tick or shared material write is needed.
			const float Phase = float((GetUniqueID() * 2654435761u) & 0x00ffffffu) / 16384.0f;
			Instance->SetScalarParameterValue(FlickerPhaseParameter, Phase);
		}
		FOSNixieMaterialBinding& Binding = Bindings.AddDefaulted_GetRef();
		Binding.Slot = Slot;
		Binding.OriginalOverride = Mesh->OverrideMaterials.IsValidIndex(Slot) ? Mesh->OverrideMaterials[Slot].Get() : nullptr;
		Binding.Instance = Instance;
		Binding.bHasClocks = Material->GetScalarParameterValue(FMaterialParameterInfo(ChangeTimeParameter), Unused)
			&& Material->GetScalarParameterValue(FMaterialParameterInfo(PowerTimeParameter), Unused)
			&& Material->GetScalarParameterValue(FMaterialParameterInfo(PrevDigitParameter), Unused);
		Mesh->SetMaterial(Slot, Instance);
	}
	ApplyState();
	return Bindings.Num();
}

void UOSNixieCounterComponent::ReleaseBinding()
{
	if (IsValid(BoundMesh))
	{
		for (const FOSNixieMaterialBinding& Binding : Bindings)
		{
			if (BoundMesh->GetMaterial(Binding.Slot) == Binding.Instance)
				BoundMesh->SetMaterial(Binding.Slot, Binding.OriginalOverride);
		}
	}
	Bindings.Reset();
	BoundMesh = nullptr;
}

#if !UE_BUILD_SHIPPING
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "HAL/IConsoleManager.h"

namespace
{
	void RunNixieProbe(const TArray<FString>& Args, UWorld* World)
	{
		if (!World || !World->IsGameWorld())
		{
			UE_LOG(LogMindKore, Error, TEXT("Nixie probe: FAIL - run in PIE or a game world."));
			return;
		}
		const FString MaterialPath = Args.IsEmpty()
			? TEXT("/Game/OSArt/Materials/Instruments/Nixie/MI_OS_Nixie_RMB93.MI_OS_Nixie_RMB93") : Args[0];
		UMaterialInterface* Material = LoadObject<UMaterialInterface>(nullptr, *MaterialPath);
		float OriginalDigit = 0.0f, OriginalEnabled = 0.0f;
		if (!Material || !Material->GetScalarParameterValue(FMaterialParameterInfo(DigitParameter), OriginalDigit)
			|| !Material->GetScalarParameterValue(FMaterialParameterInfo(EnabledParameter), OriginalEnabled))
		{
			UE_LOG(LogMindKore, Error, TEXT("Nixie probe: FAIL - missing material/parameter contract: %s"), *MaterialPath);
			return;
		}
		float OriginalPhase = 0.0f;
		const bool bHasPhase = Material->GetScalarParameterValue(FMaterialParameterInfo(FlickerPhaseParameter), OriginalPhase);

		FActorSpawnParameters Spawn;
		Spawn.ObjectFlags |= RF_Transient;
		AActor* Owner = World->SpawnActor<AActor>(Spawn);
		if (!Owner)
		{
			UE_LOG(LogMindKore, Error, TEXT("Nixie probe: FAIL - could not spawn transient owner."));
			return;
		}
		// Two slots, no render data: the probe tests real component/MID state even under NullRHI.
		UStaticMesh* MeshAsset = NewObject<UStaticMesh>(Owner);
		MeshAsset->GetStaticMaterials().Add(FStaticMaterial(Material));
		MeshAsset->GetStaticMaterials().Add(FStaticMaterial(Material));
		auto MakeMesh = [&]()
		{
			UStaticMeshComponent* Mesh = NewObject<UStaticMeshComponent>(Owner);
			Mesh->SetStaticMesh(MeshAsset);
			Owner->AddInstanceComponent(Mesh);
			return Mesh;
		};
		UStaticMeshComponent* MeshA = MakeMesh();
		UStaticMeshComponent* MeshB = MakeMesh();
		Owner->SetRootComponent(MeshA);
		UOSNixieCounterComponent* A = NewObject<UOSNixieCounterComponent>(Owner);
		UOSNixieCounterComponent* B = NewObject<UOSNixieCounterComponent>(Owner);
		Owner->AddInstanceComponent(A);
		Owner->AddInstanceComponent(B);
		int32 Passed = 0, Failed = 0;
		auto Check = [&](bool bPass, const TCHAR* Label)
		{
			bPass ? ++Passed : ++Failed;
			UE_LOG(LogMindKore, Log, TEXT("Nixie probe: %s - %s"), bPass ? TEXT("PASS") : TEXT("FAIL"), Label);
		};
		auto Scalar = [](UMaterialInterface* MI, FName Parameter, float Expected)
		{
			float Value = -999.0f;
			return MI && MI->GetScalarParameterValue(FMaterialParameterInfo(Parameter), Value) && FMath::IsNearlyEqual(Value, Expected);
		};
		A->SetDigit(4);
		B->SetDigit(7);
		Check(A->BindMesh(MeshA, { 0, 1, 1, -1, 99 }) == 2 && B->BindMesh(MeshB, { 0 }) == 1,
			TEXT("valid unique slots only; state set before binding survives"));
		UMaterialInstanceDynamic* A0 = Cast<UMaterialInstanceDynamic>(MeshA->GetMaterial(0));
		UMaterialInstanceDynamic* A1 = Cast<UMaterialInstanceDynamic>(MeshA->GetMaterial(1));
		UMaterialInstanceDynamic* B0 = Cast<UMaterialInstanceDynamic>(MeshB->GetMaterial(0));
		Check(A0 && A1 && B0 && A0 != A1 && A0 != B0 && A1 != B0,
			TEXT("every target slot owns a different MID"));
		Check(Scalar(A0, DigitParameter, 4) && Scalar(A1, DigitParameter, 4) && Scalar(B0, DigitParameter, 7),
			TEXT("two displays have independent values"));
		float PhaseA = 0.0f, PhaseB = 0.0f;
		if (bHasPhase)
		{
			Check(A0 && B0 && A0->GetScalarParameterValue(FMaterialParameterInfo(FlickerPhaseParameter), PhaseA)
				&& B0->GetScalarParameterValue(FMaterialParameterInfo(FlickerPhaseParameter), PhaseB)
				&& !FMath::IsNearlyEqual(PhaseA, PhaseB) && Scalar(A1, FlickerPhaseParameter, PhaseA),
				TEXT("optional flicker phase differs per component, agrees across its slots"));
		}
		bool bAllDigits = true;
		for (int32 Digit = 0; Digit <= 10; ++Digit)
		{
			A->SetDigit(Digit);
			bAllDigits &= Scalar(A0, DigitParameter, float(Digit)) && Scalar(A1, DigitParameter, float(Digit));
		}
		Check(bAllDigits && Scalar(B0, DigitParameter, 7), TEXT("0..9 and explicit blank10; other display unchanged"));
		A->SetDigit(-1);
		Check(A->GetDigit() == 10 && Scalar(A0, DigitParameter, 10), TEXT("negative input blanks safely"));
		A->SetDigit(99);
		Check(A->GetDigit() == 10 && Scalar(A0, DigitParameter, 10), TEXT("overflow input blanks safely"));
		A->SetDigit(3);
		A->SetCounterEnabled(false);
		Check(Scalar(A0, EnabledParameter, 0) && Scalar(A1, EnabledParameter, 0) && Scalar(A0, DigitParameter, 3)
			&& Scalar(B0, EnabledParameter, 1), TEXT("power off retains selected digit without affecting another counter"));
		A->SetCounterEnabled(true);
		Check(Scalar(A0, EnabledParameter, 1) && Scalar(A0, DigitParameter, 3), TEXT("power on restores same digit"));
		const int32 Refreshed = A->RefreshBinding();
		UMaterialInstanceDynamic* RefreshedMID = Cast<UMaterialInstanceDynamic>(MeshA->GetMaterial(0));
		Check(Refreshed == 2 && RefreshedMID && RefreshedMID->Parent == Material,
			TEXT("refresh replaces owned MIDs without nesting dynamic parents"));
		if (bHasPhase) Check(Scalar(RefreshedMID, FlickerPhaseParameter, PhaseA), TEXT("flicker phase survives rebind"));
		Check(B->BindMesh(MeshA, { 0 }) == 0 && A->GetBoundMaterialCount() == 2,
			TEXT("another counter cannot take an owned slot"));
		Check(MeshB->GetMaterial(0) == Material, TEXT("rebind restores previous mesh material"));
		UMaterialInstanceDynamic* ExternalMID = UMaterialInstanceDynamic::Create(Material, Owner);
		ExternalMID->SetScalarParameterValue(DigitParameter, 6);
		ExternalMID->SetScalarParameterValue(EnabledParameter, 0);
		MeshA->SetMaterial(1, ExternalMID);
		A->ReleaseBinding();
		Check(MeshA->GetMaterial(0) == Material && MeshA->GetMaterial(1) == ExternalMID,
			TEXT("release restores owned slots and preserves external replacement"));
		B->BindMesh(MeshA, { 1 });
		UMaterialInstanceDynamic* CopiedMID = Cast<UMaterialInstanceDynamic>(MeshA->GetMaterial(1));
		Check(CopiedMID && CopiedMID != ExternalMID && CopiedMID->Parent == Material
			&& Scalar(CopiedMID, DigitParameter, 7) && Scalar(ExternalMID, DigitParameter, 6)
			&& Scalar(ExternalMID, EnabledParameter, 0), TEXT("existing dynamic material cloned without mutating source"));
		B->ReleaseBinding();
		Check(MeshA->GetMaterial(1) == ExternalMID, TEXT("original dynamic material restored"));
		Check(A->BindMesh(nullptr, { 0 }) == 0 && A->GetBoundMaterialCount() == 0, TEXT("null binding is safe"));
		A->TargetMesh.OverrideComponent = MeshA;
		A->MaterialSlots = { 0 };
		A->RegisterComponent();
		Check(A->GetBoundMaterialCount() == 1 && Scalar(MeshA->GetMaterial(0), DigitParameter, 3),
			TEXT("BeginPlay resolves configured component reference and saved state"));
		A->UnregisterComponent();
		Check(MeshA->GetMaterial(0) == Material && A->GetBoundMaterialCount() == 0, TEXT("unregister restores material"));
		A->RegisterComponent();
		Check(A->GetBoundMaterialCount() == 1, TEXT("reregister restores binding without Tick"));
		A->DestroyComponent();
		Check(MeshA->GetMaterial(0) == Material, TEXT("component destruction restores original material"));
		Check(!B->PrimaryComponentTick.bCanEverTick, TEXT("no component Tick"));
		{
			// v5 event clocks: one stamp per event, in world seconds, nothing per frame.
			const double T0 = World->GetTimeSeconds();
			B->BindMesh(MeshB, { 0 });
			B->SetCounterEnabled(true);
			B->SetDigit(5);
			B->SetDigit(2);
			const double Stamp = B->GetChangeTime();
			Check(B->GetPreviousDigit() == 5 && FMath::IsNearlyEqual(Stamp, T0, 1e-6),
				TEXT("digit change stamps world time and remembers the lit digit"));
			B->SetDigit(2);
			Check(B->GetChangeTime() == Stamp && B->GetPreviousDigit() == 5, TEXT("same digit again restarts nothing"));
			B->SetCounterEnabled(false);
			Check(FMath::IsNearlyEqual(B->GetPowerTime(), T0, 1e-6), TEXT("power switch stamps world time"));
			B->SetDigit(7);
			Check(B->GetPreviousDigit() == 10, TEXT("a change while dark leaves no afterglow"));
			B->SetCounterEnabled(true);
			float ChangeParam = 0.0f;
			UMaterialInstanceDynamic* BMID = Cast<UMaterialInstanceDynamic>(MeshB->GetMaterial(0));
			const bool bClocks = BMID && BMID->GetScalarParameterValue(FMaterialParameterInfo(ChangeTimeParameter), ChangeParam);
			if (bClocks)
			{
				Check(FMath::IsNearlyEqual(double(ChangeParam), B->GetChangeTime(), 1e-3) && Scalar(BMID, PrevDigitParameter, 10.0f)
					&& Scalar(BMID, PowerTimeParameter, float(B->GetPowerTime())), TEXT("clocks reach the bound material"));
				UE_LOG(LogMindKore, Log, TEXT("Nixie probe: clocks world=%.4f change=%.4f power=%.4f prev=%d"),
					T0, B->GetChangeTime(), B->GetPowerTime(), B->GetPreviousDigit());
			}
			else UE_LOG(LogMindKore, Log, TEXT("Nixie probe: material has no v5 clocks - transitions are not animated."));
		}
		Check(Scalar(Material, DigitParameter, OriginalDigit) && Scalar(Material, EnabledParameter, OriginalEnabled),
			TEXT("shared material defaults unchanged"));
		if (bHasPhase) Check(Scalar(Material, FlickerPhaseParameter, OriginalPhase), TEXT("shared flicker phase unchanged"));
		B->ReleaseBinding();
		Owner->Destroy();
		UE_LOG(LogMindKore, Log, TEXT("Nixie probe: %s (%d passed, %d failed). State/MID proof only; visuals require real RHI."),
			Failed == 0 ? TEXT("PASS") : TEXT("FAIL"), Passed, Failed);
	}

	FAutoConsoleCommandWithWorldAndArgs GNixieProbe(
		TEXT("os.Nixie.Probe"),
		TEXT("Per-instance Nixie MID, digit/OFF, binding and lifetime checks in PIE or -game (NullRHI supported). Optional material object path; default MI_OS_Nixie_RMB93. Does not change shared assets."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateStatic(&RunNixieProbe));
}
#endif
