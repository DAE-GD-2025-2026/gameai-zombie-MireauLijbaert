#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Village/House/House.h"

UBTTask_ExecuteGoapAction::UBTTask_ExecuteGoapAction()
{
    NodeName = "Execute GOAP Action";
    bNotifyTick = true;
}

EBTNodeResult::Type UBTTask_ExecuteGoapAction::ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    if (!AIC) { UE_LOG(LogTemp, Error, TEXT("BTTask: No AIC")); return EBTNodeResult::Failed; }

    APawn* ControlledPawn = AIC->GetPawn();
    if (!ControlledPawn) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Pawn")); return EBTNodeResult::Failed; }

    UGoapAgentBrain* Brain = AIC->FindComponentByClass<UGoapAgentBrain>();
    if (!Brain) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Brain found on AIC")); return EBTNodeResult::Failed; }
    if (Brain->GetCurrentPlan().Num() == 0)
    {
        // Brain is still replanning (e.g. right after completing the last action) — stay alive and wait
        return EBTNodeResult::InProgress;
    }

    UStudentPerceptor* Perceptor = ControlledPawn->FindComponentByClass<UStudentPerceptor>();
    if (!Perceptor) { UE_LOG(LogTemp, Error, TEXT("BTTask: No Perceptor found on Pawn")); return EBTNodeResult::Failed; }

    FGoapAction CurrentAction = Brain->GetCurrentPlan()[0];
    UE_LOG(LogTemp, Warning, TEXT("BTTask: Trying to execute action '%s'"), *CurrentAction.ActionName);
    
    if (CurrentAction.ActionName == TEXT("Action_Kiting"))
    {
        AActor* Threat = Perceptor->GetHighestThreatZombie();
        if (!Threat) return EBTNodeResult::Failed;
        Perceptor->ActivateFleeFrom(Threat->GetActorLocation());
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_SearchHouse"))
    {
        AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
        if (HouseActor)
        {
            AHouse* House = Cast<AHouse>(HouseActor);
            FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
            Perceptor->ActivateNavigationTarget(Target);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("BTTask: NavMesh unavailable — activating wander directly"));
            Perceptor->ActivateWanderMode(); // NavMesh unavailable — fall straight through to wander
            
            // No house spotted — pathfind to a clear point to exit any house we may still be in,
            // then TickTask will hand off to the wander steering behavior once we arrive.
            // UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(ControlledPawn->GetWorld());
            // FNavLocation ExitPoint;
            // if (NavSys && NavSys->GetRandomReachablePointInRadius(ControlledPawn->GetActorLocation(), 2000.f, ExitPoint))
            // {
            //     UE_LOG(LogTemp, Warning, TEXT("BTTask: NavMesh exit query OK — navigating to exit point"));
            //     Perceptor->ActivateNavigationTarget(ExitPoint.Location);
            // }
            // else
            // {
            //     
            // }
        }
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        // Reset state from previous action
        Perceptor->BeginLootSearch();
        return EBTNodeResult::InProgress;
    }
    if (CurrentAction.ActionName == TEXT("Action_UseMedkit"))
    {
        // No movement needed — instant use
        // Just complete it immediately
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return EBTNodeResult::Succeeded;
    }
   
    return EBTNodeResult::Failed;
}

void UBTTask_ExecuteGoapAction::TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
    AAIController* AIC = OwnerComp.GetAIOwner();
    if (!AIC) return;
    APawn* ControlledPawn = AIC->GetPawn();
    if (!ControlledPawn) return;

    UGoapAgentBrain* Brain = AIC->FindComponentByClass<UGoapAgentBrain>();
    UStudentPerceptor* Perceptor = ControlledPawn->FindComponentByClass<UStudentPerceptor>();
    if (!Brain || !Perceptor || Brain->GetCurrentPlan().Num() == 0) return;

    const FGoapAction& CurrentAction = Brain->GetCurrentPlan()[0];

    // SearchHouse: exit house → wander → spot house → navigate to center → rotate 360° to scan
    if (CurrentAction.ActionName == TEXT("Action_SearchHouse"))
    {
        // State: wandering in open world — check each tick if a house comes into view
        if (Perceptor->IsWandering())
        {
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            return;
        }

        if (!Perceptor->IsCurrentActionFinished())
        {
            // State: rotation in progress — just wait
            if (Perceptor->IsRotatingSearch())
                return;

            // State: navigating — redirect if a house is spotted mid-path
            if (Perceptor->IsNavigating())
            {
                if (Perceptor->GetCurrentTargetHouse() == nullptr)
                {
                    AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
                    if (HouseActor)
                    {
                        AHouse* House = Cast<AHouse>(HouseActor);
                        FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                        Perceptor->ActivateNavigationTarget(Target);
                    }
                }
                return;
            }

            // State: idle (None) — plan just became ready after ExecuteTask returned early.
            // Set up initial movement exactly as ExecuteTask would have.
            UE_LOG(LogTemp, Warning, TEXT("BTTask: SearchHouse — plan ready, initializing movement"));
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            else
            {
                UNavigationSystemV1* NavSys = FNavigationSystem::GetCurrent<UNavigationSystemV1>(ControlledPawn->GetWorld());
                FNavLocation ExitPoint;
                if (NavSys && NavSys->GetRandomReachablePointInRadius(ControlledPawn->GetActorLocation(), 2000.f, ExitPoint))
                {
                    UE_LOG(LogTemp, Warning, TEXT("BTTask: NavMesh exit query OK — navigating to exit point"));
                    Perceptor->ActivateNavigationTarget(ExitPoint.Location);
                }
                else
                {
                    UE_LOG(LogTemp, Warning, TEXT("BTTask: NavMesh unavailable — activating wander directly"));
                    Perceptor->ActivateWanderMode();
                }
            }
            return;
        }

        // Something finished — figure out which phase just ended
        if (Perceptor->IsHouseRotationDone())
        {
            // Full 360° scan done — mark explored and move to looting
            Perceptor->MarkCurrentHouseExplored();
            Brain->CompleteCurrentAction();
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        }
        else if (Perceptor->GetCurrentTargetHouse() != nullptr)
        {
            // Arrived at the house center — start rotating to scan with the FOV cone
            Perceptor->ActivateRotationSearch();
        }
        else
        {
            // Arrived at exit navigation point — now safely outside, start wander
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                UE_LOG(LogTemp, Warning, TEXT("BTTask: Arrived at exit point, house spotted — navigating to it"));
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("BTTask: Arrived at exit point, no house — activating wander"));
                Perceptor->ActivateWanderMode();
            }
        }
        return;
    }

    // PickupLoot: three-phase loop — scan KnownItems → navigate → grab → repeat
    if (CurrentAction.ActionName == TEXT("Action_PickupLoot"))
    {
        // Shared finish: complete the action and reset GOAP cycle states so the brain
        // immediately replans to search the next house instead of idling.
        auto FinishLooting = [&](const TCHAR* Reason)
        {
            UE_LOG(LogTemp, Log, TEXT("BTTask: Looting done (%s) — resetting for next house"), Reason);
            // CompleteCurrentAction applies the action's effects (HasResources=1), so reset AFTER
            // so our zeros overwrite the effect before FindNewPlan runs on the next brain tick
            Brain->CompleteCurrentAction();
            Brain->GetCurrentState().Add(TEXT("HouseExplored"), 0);
            Brain->GetCurrentState().Add(TEXT("HasResources"), 0);
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        };

        // Phase 2: still traveling to an item — wait
        if (Perceptor->IsNavigating()) return;

        // Phase 3: arrived — grab the item we walked to
        if (Perceptor->IsCurrentActionFinished())
        {
            UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
            AActor* LootActor = Perceptor->GetNearestUsefulLoot();

            if (Inv && LootActor)
            {
                ABaseItem* Item = Cast<ABaseItem>(LootActor);
                if (Item)
                {
                    const TArray<ABaseItem*>& Slots = Inv->GetInventory();
                    bool bInventoryFull = true;
                    for (int32 i = 0; i < Slots.Num(); ++i)
                    {
                        if (Slots[i] == nullptr)
                        {
                            Inv->GrabItem(i, Item);
                            Perceptor->PerceivedLoot.Remove(LootActor);
                            Perceptor->KnownItems.Remove(LootActor);
                            UE_LOG(LogTemp, Log, TEXT("BTTask: Grabbed '%s' into slot %d"), *Item->GetName(), i);
                            bInventoryFull = false;
                            break;
                        }
                    }

                    if (bInventoryFull)
                    {
                        FinishLooting(TEXT("inventory full"));
                        return;
                    }
                }
            }

            // Item grabbed (or invalid) — reset to Phase 1 to look for the next one
            Perceptor->BeginLootSearch();
            return;
        }

        // Phase 1: find the next item in memory and navigate to it
        AActor* Loot = Perceptor->GetNearestUsefulLoot();
        if (Loot)
        {
            Perceptor->ActivateNavigationTarget(Loot->GetActorLocation());
        }
        else
        {
            FinishLooting(TEXT("house fully looted"));
        }
        return;
    }

    // Default: all other actions finish when perceptor signals done
    if (Perceptor->IsCurrentActionFinished())
    {
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
    }
}