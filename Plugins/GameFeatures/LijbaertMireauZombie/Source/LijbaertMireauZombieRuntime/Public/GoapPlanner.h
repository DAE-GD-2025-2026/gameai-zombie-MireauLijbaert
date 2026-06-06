#pragma once

#include "CoreMinimal.h"
#include "Containers/Map.h"
#include "Containers/Array.h"
#include "GoapPlanner.generated.h"

// Goap brain from research project
// Scoring structure for goal importance
USTRUCT(BlueprintType)
struct FGoapGoalStrategy
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GOAP")
    FString GoalKey;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GOAP")
    int32 TargetValue = 1;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GOAP")
    float DesirabilityScore = 0.0f;

    UPROPERTY(BlueprintReadWrite, EditAnywhere, Category = "GOAP")
    FString VisualName;
};

USTRUCT(BlueprintType)
struct FGoapState
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    TMap<FString, int32> StateMap;

    
    void Add(const FString& Key, int32 Value) { StateMap.Add(Key, Value); }
    const int32* Find(const FString& Key) const { return StateMap.Find(Key); }
    
    // Helper to empty the goals
    void Empty() { StateMap.Empty(); }
    // Helper to check how many states are still in
    int32 Num() const { return StateMap.Num(); }
    
    bool operator==(const FGoapState& Other) const
    {
        if (StateMap.Num() != Other.StateMap.Num()) return false;
        for (const auto& Pair : StateMap)
        {
            const int32* OtherVal = Other.StateMap.Find(Pair.Key);
            if (!OtherVal || *OtherVal != Pair.Value) return false;
        }
        return true;
    }

    auto begin() const { return StateMap.begin(); }
    auto end() const { return StateMap.end(); }
};

USTRUCT(BlueprintType)
struct FGoapAction
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FString ActionName;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    int32 Cost = 1;
    
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GOAP")
    FString TargetObjectTag;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FGoapState Preconditions;

    UPROPERTY(BlueprintReadWrite, EditAnywhere)
    FGoapState Effects;
};

struct FGoapNode
{
    FGoapState State;
    int32 GCost = 0; 
    int32 HCost = 0; 
    FGoapNode* Parent = nullptr;
    FGoapAction Action;

    int32 GetFCost() const { return GCost + HCost; }
};

class LIJBAERTMIREAUZOMBIERUNTIME_API GoapHelpers
{
public:
    static bool IsStateMatch(const FGoapState& CurrentState, const FGoapState& RequiredState)
    {
        for (const auto& Condition : RequiredState)
        {
            const int32* CurrentVal = CurrentState.Find(Condition.Key);
            if (!CurrentVal || *CurrentVal != Condition.Value)
            {
                return false;
            }
        }
        return true;
    }
};

class LIJBAERTMIREAUZOMBIERUNTIME_API GoapPlanner
{
public:
    static TArray<FGoapAction> Plan(const TArray<FGoapAction>& AvailableActions, 
                                    const FGoapState& CurrentState, 
                                    const FGoapState& Goal);

private:
    static TArray<FGoapAction> BuildPlan(FGoapNode* GoalNode);
    static int32 CalculateHeuristic(const FGoapState& CurrentState, const FGoapState& Goal);
};
