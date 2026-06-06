#include "GoapAgentBrain.h"
#include "GameFramework/Character.h"
#include "GoapAgentInterface.h"

UGoapAgentBrain::UGoapAgentBrain()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UGoapAgentBrain::InitializeAgent(const TArray<FGoapAction>& CustomActions, const FGoapState& InitialState)
{
    AvailableActions = CustomActions;
    CurrentState = InitialState;
    CurrentPlan.Empty();
    bIsExecutingAction = false;
    
    UE_LOG(LogTemp, Log, TEXT("[%s] GOAP Agent Brain Initialized with %d actions."), *GetOwner()->GetName(), AvailableActions.Num());
}

void UGoapAgentBrain::ProcessHighestPriorityGoal(const TArray<FGoapGoalStrategy>& Strategies)
{
    if (bIsExecutingAction || Strategies.Num() == 0) return;
    
    FGoapGoalStrategy BestStrategy = Strategies[0];
    for (int32 i = 1; i < Strategies.Num(); ++i)
    {
        if (Strategies[i].DesirabilityScore > BestStrategy.DesirabilityScore)
        {
            BestStrategy = Strategies[i];
        }
    }

    // Apply the chosen goal
    CurrentGoal.Empty();
    CurrentGoal.Add(BestStrategy.GoalKey, BestStrategy.TargetValue);
    VisualCurrentGoalName = BestStrategy.VisualName;
}

void UGoapAgentBrain::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    // If we are actively running an action, pass control off to the AI Controller / Character loop
    if (bIsExecutingAction)
    {
        ActionDurationTimer -= DeltaTime;

        IGoapAgentInterface* GoapInterface = Cast<IGoapAgentInterface>(GetOwner());
        bool bCharacterIsFinished = (ActionDurationTimer <= 0.0f);

        // If the timer is up, OR the character explicitly tells us they finished early via code
        if (GoapInterface && GoapInterface->IsActionFinished(CurrentlyRunningAction.ActionName))
        {
            bCharacterIsFinished = true;
        }

        if (bCharacterIsFinished)
        {
            CompleteCurrentAction();
        }
        return;
    }
   
    if (PlanningCooldownTimer > 0.0f)
    {
        PlanningCooldownTimer -= DeltaTime;
        return;
    }

    if (CurrentPlan.Num() == 0)
    {   
        FindNewPlan();
    }
    else
    {
        StartNextPlanStep();
    }
    
    
}

void UGoapAgentBrain::FindNewPlan()
{
    if (CurrentGoal.StateMap.Num() == 0) return;

    bool bGoalAlreadySatisfied = true;
    for (const auto& GoalPair : CurrentGoal)
    {
        const int32* CurrentValue = CurrentState.Find(GoalPair.Key);
        if (!CurrentValue || *CurrentValue != GoalPair.Value)
        {
            bGoalAlreadySatisfied = false;
            break;
        }
    }
    
    if (bGoalAlreadySatisfied)
    {
        PlanningCooldownTimer = PlanningCooldownDuration; 
        VisualCurrentActionName = TEXT("Idling (Goal Met)");
        return; 
    }

    // Call the planner to construct a sequence of actions
    CurrentPlan = GoapPlanner::Plan(AvailableActions, CurrentState, CurrentGoal);

    if (CurrentPlan.Num() > 0)
    {
        PlanningCooldownTimer = 0.0f;
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("[%s] GOAP Brain: Failed to find valid plan! Cooldown applied."), *GetOwner()->GetName());
        PlanningCooldownTimer = PlanningCooldownDuration;
        VisualCurrentActionName = TEXT("No Valid Plan");
    }
}

void UGoapAgentBrain::StartNextPlanStep()
{
    if (CurrentPlan.Num() == 0) return;

    CurrentlyRunningAction = CurrentPlan[0];
    bIsExecutingAction = true;
    VisualCurrentActionName = CurrentlyRunningAction.ActionName;

    UE_LOG(LogTemp, Log, TEXT("[%s] GOAP Brain: Initiating Action -> '%s'"), *GetOwner()->GetName(), *CurrentlyRunningAction.ActionName);
    
    if (GetOwner()->GetClass()->ImplementsInterface(UGoapAgentInterface::StaticClass()))
    {
        IGoapAgentInterface* GoapInterface = Cast<IGoapAgentInterface>(GetOwner());
        if (GoapInterface)
        {
            // Ask the character how long this specific action takes
            ActionDurationTimer = GoapInterface->GetActionDuration(CurrentlyRunningAction.ActionName);
        
            // Tell the character to start physically performing it (e.g., play reload animation, play eating sound)
            bool bStarted = GoapInterface->ExecutePhysicalAction(CurrentlyRunningAction.ActionName);
        
            if (bStarted)
            {
                bIsExecutingAction = true;
                OnActionStarted.Broadcast(CurrentlyRunningAction.ActionName);
                return;
            }
        }
    }

    // Fallback safety if the character doesn't implement the interface or fails to execute
    UE_LOG(LogTemp, Warning, TEXT("[%s] Brain: Owner cannot execute action '%s'"), *GetOwner()->GetName(), *CurrentlyRunningAction.ActionName);
    AbortCurrentPlan();
    
    // Broadcast cleanly to either C++ Listeners, Behavior Trees, or Blueprints
    OnActionStarted.Broadcast(CurrentlyRunningAction.ActionName);
}

void UGoapAgentBrain::CompleteCurrentAction()
{
    if (!bIsExecutingAction) return;

    UE_LOG(LogTemp, Log, TEXT("[%s] GOAP Brain: Completed Action -> '%s'"), *GetOwner()->GetName(), *CurrentlyRunningAction.ActionName);

    // Apply action effects cleanly directly onto our internal state
    for (const auto& Effect : CurrentlyRunningAction.Effects)
    {
        CurrentState.Add(Effect.Key, Effect.Value);
    }

    bIsExecutingAction = false;
    OnActionCompleted.Broadcast(CurrentlyRunningAction.ActionName);

    // Advance the plan array
    if (CurrentPlan.Num() > 0)
    {
        CurrentPlan.RemoveAt(0);
    }
    
    VisualCurrentActionName = TEXT("Calculating next action...");
}

void UGoapAgentBrain::AbortCurrentPlan()
{
    UE_LOG(LogTemp, Warning, TEXT("[%s] GOAP Brain: Plan explicitly aborted!"), *GetOwner()->GetName());
    CurrentPlan.Empty();
    bIsExecutingAction = false;
    VisualCurrentActionName = TEXT("Aborted / Re-planning...");
    PlanningCooldownTimer = 0.1f; // Quick recalculation frame jump
}