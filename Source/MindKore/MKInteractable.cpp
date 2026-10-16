#include "MKInteractable.h"

#include "MindKore.h"
#include "Perception/AISense_Hearing.h"

void MKReportMechanismNoise(AActor* Source, const FVector& Location, float Loudness, FName Tag)
{
	if (!Source)
	{
		return;
	}
	UAISense_Hearing::ReportNoiseEvent(Source, Location, Loudness, Source, /*MaxRange=*/0.0f, Tag);
	UE_LOG(LogMindKore, Log, TEXT("Noise %s from %s at (%.0f, %.0f, %.0f) loudness %.2f"),
		*Tag.ToString(), *Source->GetName(), Location.X, Location.Y, Location.Z, Loudness);
}
