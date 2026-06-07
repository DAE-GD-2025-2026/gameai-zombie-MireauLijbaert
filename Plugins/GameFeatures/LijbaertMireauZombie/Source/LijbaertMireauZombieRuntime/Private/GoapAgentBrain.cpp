#include "GoapAgentBrain.h"
#include "GameFramework/Character.h"
#include "GoapAgentInterface.h"

UGoapAgentBrain::UGoapAgentBrain()
{
    PrimaryComponentTick.bCanEverTick = true;
    SetupGoalsAndActions();
}

void UGoapAgentBrain::SetupGoalsAndActions()
{
    TArray<FGoapAction> Actions;
    TArray<FGoapGoalStrategy> Goals;

    // ---Define goals---
    
    // Priority Goal: Survival
    FGoapGoalStrategy DefendGoal;
    DefendGoal.GoalKey = TEXT("ZombiesNearby");
    DefendGoal.TargetValue = 0;
    DefendGoal.DesirabilityScore = 95.0f; // Extremely high urgency!
    DefendGoal.VisualName = TEXT("Survive Threats");
    Goals.Add(DefendGoal);

    // Baseline Goal: Looting Houses 
    FGoapGoalStrategy LootGoal;
    LootGoal.GoalKey = TEXT("HasResources");
    LootGoal.TargetValue = 1;
    LootGoal.DesirabilityScore = 40.0f; // Default task when safe
    LootGoal.VisualName = TEXT("Search Houses for Loot");
    Goals.Add(LootGoal);

    // Emergency Goal: Healing 
    FGoapGoalStrategy CriticalHealGoal;
    CriticalHealGoal.GoalKey = TEXT("IsHealthy");
    CriticalHealGoal.TargetValue = 1;
    CriticalHealGoal.DesirabilityScore = 0.0f; // Dynamic, We will update this via sensors
    CriticalHealGoal.VisualName = TEXT("Heal Critical Injuries");
    Goals.Add(CriticalHealGoal);

    // ---Define actions---
    
    // Action Kiting Combat
    FGoapAction KiteAction;
    KiteAction.ActionName = TEXT("Action_Kiting");
    KiteAction.Cost = 1;
    KiteAction.Preconditions.Add(TEXT("ZombiesNearby"), 1);
    KiteAction.Preconditions.Add(TEXT("HasWeapon"), 1);
    KiteAction.Effects.Add(TEXT("ZombiesNearby"), 0); // Fleeing/killing eliminates threat
    Actions.Add(KiteAction);

    // Action Search House
    FGoapAction SearchHouseAction;
    SearchHouseAction.ActionName = TEXT("Action_SearchHouse");
    SearchHouseAction.Cost = 3;
    SearchHouseAction.Preconditions.Add(TEXT("ZombiesNearby"), 0);
    SearchHouseAction.Effects.Add(TEXT("HouseExplored"), 1);
    Actions.Add(SearchHouseAction);

    // Action Loot Item
    FGoapAction PickupAction;
    PickupAction.ActionName = TEXT("Action_PickupLoot");
    PickupAction.Cost = 1;
    PickupAction.Preconditions.Add(TEXT("HouseExplored"), 1);
    PickupAction.Effects.Add(TEXT("HasResources"), 1);
    Actions.Add(PickupAction);

    // Action Inject Medkit
    FGoapAction HealAction;
    HealAction.ActionName = TEXT("Action_UseMedkit");
    HealAction.Cost = 2;
    HealAction.Preconditions.Add(TEXT("HasMedkit"), 1);
    HealAction.Effects.Add(TEXT("IsHealthy"), 1);
    Actions.Add(HealAction);
    
    FGoapState InitialState; // Start blank
    InitializeAgent(Actions, Goals, InitialState);
    
}

void UGoapAgentBrain::InitializeAgent(const TArray<FGoapAction>& CustomActions, const TArray<FGoapGoalStrategy>& Goals, const FGoapState& InitialState)
{
    AvailableActions = CustomActions;
    CurrentState = InitialState;
    PossibleGoals = Goals;
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

// Specific to my goals
void UGoapAgentBrain::CalculateDesirability()
{
   // if (!Perceptor) return;

  
    //bool bZombiesDetected = (Perceptor->PerceivedZombies.Num() > 0);
    //CurrentState.Add(TEXT("ZombiesNearby"), bZombiesDetected ? 1 : 0);

    // Grab status parameters from the teacher's pawn or components
    // float CurrentHealth = ... Get Health ...
    // bool bHasMedkit = ... Get Inventory Medkit Count ...
    // bool bHasWeapon = ... Get Weapon Equipped ...

    // Mock parameters for compilation safety until connected:
    // float CurrentHealth = 100.0f; 
    // bool bHasMedkit = false;
    // bool bHasWeapon = true;
    //
    // CurrentState.Add(TEXT("HasWeapon"), bHasWeapon ? 1 : 0);
    // CurrentState.Add(TEXT("HasMedkit"), bHasMedkit ? 1 : 0);
    //
    //
    // for (FGoapGoalStrategy& Strategy : PossibleGoals)
    // {
    //     // Dependency for: "Survive Threats"
    //     if (Strategy.GoalKey == TEXT("ZombiesNearby"))
    //     {
    //         // If zombies are in our perception field, keep this maxed out.
    //         // If they vanish, drop urgency to zero so we don't try to resolve a solved threat.
    //         Strategy.DesirabilityScore = bZombiesDetected ? 95.0f : 0.0f;
    //     }
    //
    //     // Dependency for: "Heal Critical Injuries"
    //     else if (Strategy.GoalKey == TEXT("IsHealthy"))
    //     {
    //         // If our health drops below 30% AND we possess a healing item,
    //         // make this the single most important task in existence.
    //         if (CurrentHealth < 30.0f && bHasMedkit)
    //         {
    //             Strategy.DesirabilityScore = 100.0f; 
    //         }
    //         else
    //         {
    //             Strategy.DesirabilityScore = 0.0f; // Reset if healthy or helpless
    //         }
    //     }
    //
    //     // Dependency for: "Search Houses for Loot"
    //     else if (Strategy.GoalKey == TEXT("HasResources"))
    //     {
    //         // Baseline exploration task. If we are completely safe from zombies
    //         // and don't need emergency medical care, this stays active.
    //         if (!bZombiesDetected && CurrentHealth >= 30.0f)
    //         {
    //             Strategy.DesirabilityScore = 40.0f;
    //         }
    //         else
    //         {
    //             Strategy.DesirabilityScore = 10.0f; // Suppressed under duress
    //         }
    //     }
    // }
    //
    //
    // // Send your newly weighted goal vectors right back into your strategy sorter loop!
    // ProcessHighestPriorityGoal(PossibleGoals);
}