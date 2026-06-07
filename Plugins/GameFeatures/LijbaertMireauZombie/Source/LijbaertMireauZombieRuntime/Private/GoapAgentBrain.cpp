#include "GoapAgentBrain.h"
#include "GameFramework/Character.h"
#include "GoapAgentInterface.h"
#include "AIController.h"
#include "Common/InventoryComponent.h"
#include "Common/HealthComponent.h"
#include "Items/BaseItem.h"
#include "Items/Medkit.h"
#include "Items/Weapon.h"

UGoapAgentBrain::UGoapAgentBrain()
{
    PrimaryComponentTick.bCanEverTick = true;
}

void UGoapAgentBrain::BeginPlay()
{
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
    
    // Action Kiting Combat — flee always, shoot only if weapon available
    FGoapAction KiteAction;
    KiteAction.ActionName = TEXT("Action_Kiting");
    KiteAction.Cost = 1;
    KiteAction.Preconditions.Add(TEXT("ZombiesNearby"), 1);
    KiteAction.Effects.Add(TEXT("ZombiesNearby"), 0);
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
    
    FGoapState InitialState;
    InitialState.Add(TEXT("ZombiesNearby"), 0);
    InitialState.Add(TEXT("HasWeapon"), 0);
    InitialState.Add(TEXT("HasMedkit"), 0);
    InitialState.Add(TEXT("HouseExplored"), 0);
    InitialState.Add(TEXT("HasResources"), 0);
    InitialState.Add(TEXT("IsHealthy"), 1);
    
    InitializeAgent(Actions, Goals, InitialState);
    
    CurrentGoal.Empty();
    CurrentGoal.Add(TEXT("HasResources"), 1);
    VisualCurrentGoalName = TEXT("Search Houses for Loot");
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

    FGoapGoalStrategy BestStrategy;
    bool bFoundGoal = false;

    for (const FGoapGoalStrategy& Strategy : Strategies)
    {
        if (Strategy.DesirabilityScore <= 0.f) continue;

        // Skip goals that are already satisfied — no point pursuing them
        const int32* CurrentValue = CurrentState.Find(Strategy.GoalKey);
        if (CurrentValue && *CurrentValue == Strategy.TargetValue) continue;

        if (!bFoundGoal || Strategy.DesirabilityScore > BestStrategy.DesirabilityScore)
        {
            BestStrategy = Strategy;
            bFoundGoal = true;
        }
    }

    if (!bFoundGoal) return; // Every active goal is already met — stay on current goal

    CurrentGoal.Empty();
    CurrentGoal.Add(BestStrategy.GoalKey, BestStrategy.TargetValue);
    VisualCurrentGoalName = BestStrategy.VisualName;
}

void UGoapAgentBrain::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
    // Search for the perceptor so we can extract data like percievedZombes/items
    // Only need it once but in tick so we don't have problems with it not being constructed yet
    if (CachedPerceptor == nullptr)
    {
        if (AAIController* AIController = Cast<AAIController>(GetOwner()))
        {
            if (APawn* PossessedPawn = AIController->GetPawn())
            {
                CachedPerceptor = PossessedPawn->FindComponentByClass<UStudentPerceptor>();
                
                if (CachedPerceptor)
                {
                    UE_LOG(LogTemp, Log, TEXT("[%s] GOAP Brain successfully linked and cached StudentPerceptor!"), *GetOwner()->GetName());
                }
            }
        }
    }

    // When one is foundwe just run like normal
    if (CachedPerceptor != nullptr)
    {
        CalculateDesirability();
    }
    
    Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
    
    // If we are actively running an action, check if a higher-priority goal has emerged
    if (bIsExecutingAction)
    {
        // Find the best available goal and the desirability of the current goal
        float BestDesirability = 0.f;
        FString BestGoalKey;
        float CurrentGoalDesirability = 0.f;
        FString CurrentGoalKey;

        for (const auto& Pair : CurrentGoal)
        {
            CurrentGoalKey = Pair.Key;
        }

        for (const FGoapGoalStrategy& G : PossibleGoals)
        {
            if (G.DesirabilityScore > BestDesirability)
            {
                BestDesirability = G.DesirabilityScore;
                BestGoalKey = G.GoalKey;
            }
            if (G.GoalKey == CurrentGoalKey)
            {
                CurrentGoalDesirability = G.DesirabilityScore;
            }
        }

        // Interrupt only if a meaningfully better goal exists (5-point threshold avoids flapping)
        if (!BestGoalKey.IsEmpty() && BestGoalKey != CurrentGoalKey && BestDesirability > CurrentGoalDesirability + 5.f)
        {
            UE_LOG(LogTemp, Warning, TEXT("[%s] GOAP Brain: Priority interrupt! '%s'(%.0f) > '%s'(%.0f) — aborting plan"),
                *GetOwner()->GetName(), *BestGoalKey, BestDesirability, *CurrentGoalKey, CurrentGoalDesirability);
            AbortCurrentPlan();
            // Fall through so replanning happens this tick
        }
        else
        {
            return;
        }
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

    UE_LOG(LogTemp, Log, TEXT("[%s] GOAP Brain: Initiating Action -> '%s'"),
        *GetOwner()->GetName(), *CurrentlyRunningAction.ActionName);

    // Broadcast so the BT task knows to start executing
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
    bool bZombiesDetected = (CachedPerceptor->PerceivedZombies.Num() > 0);
    CurrentState.Add(TEXT("ZombiesNearby"), bZombiesDetected ? 1 : 0);
    
    APawn* Pawn = Cast<APawn>(CachedPerceptor->GetOwner());

    // Read health directly via the typed component
    float CurrentHealth = 100.f;
    if (Pawn)
    {
        if (UHealthComponent* HPComp = Pawn->FindComponentByClass<UHealthComponent>())
        {
            CurrentHealth = static_cast<float>(HPComp->GetHealth());
        }
    }

    // Read inventory directly — Cast to typed item classes to identify what we have
    bool bHasMedkit = false;
    bool bHasWeapon = false;
    if (Pawn)
    {
        if (UInventoryComponent* InvComp = Pawn->FindComponentByClass<UInventoryComponent>())
        {
            for (ABaseItem* Item : InvComp->GetInventory())
            {
                if (!Item) continue;
                if (Cast<AMedkit>(Item))  bHasMedkit = true;
                if (Cast<AWeapon>(Item))  bHasWeapon = true;
            }
        }
    }

    CurrentState.Add(TEXT("HasWeapon"), bHasWeapon ? 1 : 0);
    CurrentState.Add(TEXT("HasMedkit"), bHasMedkit ? 1 : 0);
    CurrentState.Add(TEXT("IsHealthy"), (CurrentHealth >= 3.0f) ? 1 : 0);

    
    for (FGoapGoalStrategy& Strategy : PossibleGoals)
    {
        // Dependency for: "Survive Threats"
        if (Strategy.GoalKey == TEXT("ZombiesNearby"))
        {
            // If zombies are in our perception field, keep this maxed out.
            // If they vanish, drop urgency to zero so we don't try to resolve a solved threat.
            Strategy.DesirabilityScore = bZombiesDetected ? 95.0f : 0.0f;
        }

        // Dependency for: "Heal Critical Injuries"
        else if (Strategy.GoalKey == TEXT("IsHealthy"))
        {
            // Health is on a 0-10 scale. Trigger healing below 30% of max (i.e. < 3) AND we have a medkit.
            if (CurrentHealth < 3.0f && bHasMedkit)
            {
                Strategy.DesirabilityScore = 100.0f;
            }
            else
            {
                Strategy.DesirabilityScore = 0.0f; // Reset if healthy or helpless
            }
        }

        // Dependency for: "Search Houses for Loot"
        else if (Strategy.GoalKey == TEXT("HasResources"))
        {
            if (!bZombiesDetected && CurrentHealth >= 3.0f)
            {
                Strategy.DesirabilityScore = 40.0f; // Baseline exploration
            }
            else if (bZombiesDetected && !bHasWeapon)
            {
                // Zombies nearby and no weapon — urgently search for one after fleeing
                Strategy.DesirabilityScore = 70.0f;
            }
            else
            {
                Strategy.DesirabilityScore = 10.0f; // Suppressed under duress
            }
        }
    }

    
    // Send your newly weighted goal vectors right back into your strategy sorter loop!
    ProcessHighestPriorityGoal(PossibleGoals);
}