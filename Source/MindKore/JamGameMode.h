#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/HUD.h"
#include "JamGameMode.generated.h"

/** The starter's game mode: AJamCharacter and AJamHUD. Config: GlobalDefaultGameMode. */
UCLASS()
class MINDKORE_API AJamGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AJamGameMode();
};

/**
 * Debug-grade HUD: a crosshair dot, the interaction offer under it (verb, and
 * the reason when refused) and a state line - health/armour, carrying,
 * crowbar tag, bullet time.
 */
UCLASS()
class MINDKORE_API AJamHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};

/**
 * The zero-G mode: AJamZeroGPawn and AJamZeroGHUD. Set it as the GameMode
 * override of a zero-G map whose WorldSettings set global gravity to 0. On
 * StartPlay every AMKPhysicsProp loses its damping and gets a small random
 * drift and spin (seeded, the same every run), so the junk floats.
 */
UCLASS()
class MINDKORE_API AJamZeroGGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	AJamZeroGGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG", meta = (ClampMin = "0.0"))
	float InitialDriftMaxCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG", meta = (ClampMin = "0.0"))
	float InitialSpinMaxDeg = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Jam|ZeroG")
	int32 DriftSeed = 1337;

	virtual void StartPlay() override;
};

/** Zero-G HUD: reticle, interaction offer, O2 bar, speed, held mass, push cooldown, health. */
UCLASS()
class MINDKORE_API AJamZeroGHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
