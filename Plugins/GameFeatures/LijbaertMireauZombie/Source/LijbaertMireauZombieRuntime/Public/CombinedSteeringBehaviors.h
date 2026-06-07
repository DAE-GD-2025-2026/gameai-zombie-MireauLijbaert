#pragma once
#include <vector>
#include "SteeringBehaviors.h"

//****************
//BLENDED STEERING
class BlendedSteering final : public ISteeringBehavior
{
public:
    struct WeightedBehavior
    {
        ISteeringBehavior* pBehavior = nullptr;
        float Weight = 0.f;

        WeightedBehavior(ISteeringBehavior* const pBehavior, float Weight)
            : pBehavior(pBehavior), Weight(Weight) {}
    };

    BlendedSteering(const std::vector<WeightedBehavior>& WeightedBehaviors);

    void AddBehaviour(const WeightedBehavior& WeightedBehavior) { WeightedBehaviors.push_back(WeightedBehavior); }
    virtual SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;

    float* GetWeight(ISteeringBehavior* const SteeringBehavior);
    std::vector<WeightedBehavior>& GetWeightedBehaviorsRef() { return WeightedBehaviors; }

private:
    std::vector<WeightedBehavior> WeightedBehaviors = {};
};

//*****************
//PRIORITY STEERING
class PrioritySteering final : public ISteeringBehavior
{
public:
    PrioritySteering(const std::vector<ISteeringBehavior*>& priorityBehaviors)
        : m_PriorityBehaviors(priorityBehaviors) {}

    void AddBehaviour(ISteeringBehavior* const pBehavior) { m_PriorityBehaviors.push_back(pBehavior); }
    SteeringOutput CalculateSteering(float DeltaT, ASteeringAgent& Agent) override;

private:
    std::vector<ISteeringBehavior*> m_PriorityBehaviors = {};
};
