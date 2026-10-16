#include "MKPhysicsBridge.h"
#include "PhysicsEngine/BodyInstance.h"

// Jam lift: AngularDrive cut (PhysicsControl plugin not enabled), see MKPhysicsBridge.h.
bool MKPhysicsBridge::ApplyPointImpulse(FBodyInstance& Body, const FVector& NewtonSeconds, const FVector& WorldPointCm)
{
	if (!Body.IsInstanceSimulatingPhysics() || NewtonSeconds.ContainsNaN() || WorldPointCm.ContainsNaN()
		|| NewtonSeconds.IsNearlyZero()) return false;
	Body.WakeInstance();
	Body.AddImpulseAtPosition(NewtonSeconds * 100.0, WorldPointCm);
	return true;
}
