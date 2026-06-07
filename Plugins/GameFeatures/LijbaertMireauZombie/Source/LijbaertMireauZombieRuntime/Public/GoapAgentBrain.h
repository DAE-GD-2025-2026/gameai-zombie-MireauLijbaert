#pragma once

// Ported from research project but pruned all unnecesarry code that doesn't make sense for the zombie game
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GoapPlanner.h"
#include "GoapAgentBrain.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FGoapActionSignature, const FString&, ActionName);

UCLASS( ClassGroup=(Custom), meta=(BlueprintSpawnableComponent) )
class LIJBAERTMIREAUZOMBIERUNTIME_API UGoapAgentBrain : public UActorComponent
{
    GENERATED_BODY()

public: 
    UGoapAgentBrain();
    
    virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

    // Initializes actions, goals and base world-state states dynamically
    void InitializeAgent(const TArray<FGoapAction>& CustomActions, const TArray<FGoapGoalStrategy>& Goals, const FGoapState& InitialState);

    // Determines the active goal based on highest desirability
    void ProcessHighestPriorityGoal(const TArray<FGoapGoalStrategy>& Strategies);

    // External triggers to tell the brain when an action succeeded or failed
    UFUNCTION(BlueprintCallable, Category = "GOAP")
    void CompleteCurrentAction();

    UFUNCTION(BlueprintCallable, Category = "GOAP")
    void AbortCurrentPlan();

    // Getters for AI Controllers and States
    FGoapState& GetCurrentState() { return CurrentState; }
    const TArray<FGoapAction>& GetCurrentPlan() const { return CurrentPlan; }

    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    FString VisualCurrentGoalName;

    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    FString VisualCurrentActionName;
    
    UPROPERTY(BlueprintAssignable, Category = "GOAP|Events")
    FGoapActionSignature OnActionStarted;

    UPROPERTY(BlueprintAssignable, Category = "GOAP|Events")
    FGoapActionSignature OnActionCompleted;

protected:
    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    TArray<FGoapAction> AvailableActions;
    
    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    TArray<FGoapGoalStrategy> PossibleGoals;
    

    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    FGoapState CurrentState;

    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    FGoapState CurrentGoal;

    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    TArray<FGoapAction> CurrentPlan;

    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GOAP")
    float PlanningCooldownDuration = 0.5f;

    float PlanningCooldownTimer = 0.0f;
    
    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    bool bIsExecutingAction = false;

    UPROPERTY(BlueprintReadOnly, Category = "GOAP")
    FGoapAction CurrentlyRunningAction;
    
    float ActionDurationTimer{0};

private:
    void FindNewPlan();
    void StartNextPlanStep();
    void SetupGoalsAndActions();
    void CalculateDesirability();
};