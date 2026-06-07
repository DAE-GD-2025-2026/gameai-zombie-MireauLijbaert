#include "GoapAgentBrain.h"
#include "GameFramework/Character.h"
#include "GoapAgentInterface.h"
#include "AIController.h"

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
    
    // If we are actively running an action, pass control off to the AI Controller / Character loop
    if (bIsExecutingAction)
    {
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

    // Read Health via reflection
    float CurrentHealth = 100.f;
    if (Pawn)
    {
        if (UActorComponent* HPComp = Pawn->GetComponentByClass(
            UClass::TryFindTypeSlow<UClass>(TEXT("/Script/GameAI_Zombie.HealthComponent"))))
        {
            // GetHealth() is a BlueprintPure UFUNCTION, so we can call it by name
            struct { int32 ReturnValue; } HealthResult;
            UFunction* GetHealthFunc = HPComp->FindFunction(FName("GetHealth"));
            if (GetHealthFunc)
            {
                HPComp->ProcessEvent(GetHealthFunc, &HealthResult);
                CurrentHealth = (float)HealthResult.ReturnValue;
            }
        }
    }

    // Read Inventory via reflection
    bool bHasMedkit = false;
    bool bHasWeapon = false;
    if (Pawn)
    {
        if (UActorComponent* InvComp = Pawn->GetComponentByClass(
            UClass::TryFindTypeSlow<UClass>(TEXT("/Script/GameAI_Zombie.InventoryComponent"))))
        {
            struct { TArray<UObject*> ReturnValue; } InvResult;
            UFunction* GetInvFunc = InvComp->FindFunction(FName("GetInventory"));
            if (GetInvFunc)
            {
                InvComp->ProcessEvent(GetInvFunc, &InvResult);
                for (UObject* Item : InvResult.ReturnValue)
                {
                    if (!Item) continue;
                    FString ClassName = Item->GetClass()->GetName();
                    if (ClassName.Contains(TEXT("Medkit")))  bHasMedkit = true;
                    if (ClassName.Contains(TEXT("Pistol")) || 
                        ClassName.Contains(TEXT("Shotgun"))) bHasWeapon = true;
                }
            }
        }
    }

    CurrentState.Add(TEXT("HasWeapon"), bHasWeapon ? 1 : 0);
    CurrentState.Add(TEXT("HasMedkit"), bHasMedkit ? 1 : 0);

    
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
            // If our health drops below 30% AND we possess a healing item,
            // make this the single most important task in existence.
            if (CurrentHealth < 30.0f && bHasMedkit)
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
            // Baseline exploration task. If we are completely safe from zombies
            // and don't need emergency medical care, this stays active.
            if (!bZombiesDetected && CurrentHealth >= 30.0f)
            {
                Strategy.DesirabilityScore = 40.0f;
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