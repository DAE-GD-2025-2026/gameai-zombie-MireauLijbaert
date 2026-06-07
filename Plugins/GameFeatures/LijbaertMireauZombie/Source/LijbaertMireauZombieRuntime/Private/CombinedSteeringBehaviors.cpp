#include "CombinedSteeringBehaviors.h"
#include <algorithm>

//****************
//BLENDED STEERING

BlendedSteering::BlendedSteering(const std::vector<WeightedBehavior>& WeightedBehaviors)
    : WeightedBehaviors(WeightedBehaviors) {}

SteeringOutput BlendedSteering::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
    SteeringOutput Blended = {};
    float TotalWeight = 0.f;

    for (auto& Behavior : WeightedBehaviors)
    {
        // Call once and store — avoids double-ticking stateful behaviors like Wander
        SteeringOutput Out = Behavior.pBehavior->CalculateSteering(DeltaT, Agent);
        Blended.LinearVelocity  += Out.LinearVelocity  * Behavior.Weight;
        Blended.AngularVelocity += Out.AngularVelocity * Behavior.Weight;
        TotalWeight += Behavior.Weight;
    }

    if (TotalWeight > 0.f)
    {
        Blended.LinearVelocity  /= TotalWeight;
        Blended.AngularVelocity /= TotalWeight;
    }

    return Blended;
}

float* BlendedSteering::GetWeight(ISteeringBehavior* const SteeringBehavior)
{
    auto it = std::find_if(WeightedBehaviors.begin(), WeightedBehaviors.end(),
        [SteeringBehavior](const WeightedBehavior& Elem) { return Elem.pBehavior == SteeringBehavior; });

    return (it != WeightedBehaviors.end()) ? &it->Weight : nullptr;
}

//*****************
//PRIORITY STEERING

SteeringOutput PrioritySteering::CalculateSteering(float DeltaT, ASteeringAgent& Agent)
{
    SteeringOutput Steering = {};

    for (ISteeringBehavior* const pBehavior : m_PriorityBehaviors)
    {
        Steering = pBehavior->CalculateSteering(DeltaT, Agent);
        if (Steering.IsValid)
            break;
    }

    return Steering;
}
