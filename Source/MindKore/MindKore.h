#pragma once

#include "CoreMinimal.h"

// Project-wide log category. Use `UE_LOG(LogMindKore, ...)` in module code so
// project messages can be filtered apart from engine noise in the Output Log
// (and silenced/boosted per-category via `Log LogMindKore <verbosity>`).
DECLARE_LOG_CATEGORY_EXTERN(LogMindKore, Log, All);
