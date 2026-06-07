#include "BTTask_ExecuteGoapAction.h"
#include "AIController.h"
#include "GoapAgentBrain.h"
#include "NavigationSystem.h"
#include "Kismet/GameplayStatics.h"
#include "Navigation/PathFollowingComponent.h"
#include "Common/InventoryComponent.h"
#include "Items/BaseItem.h"
#include "Items/ItemType.h"
#include "Items/Weapon.h"
#include "Items/Medkit.h"
#include "Village/House/House.h"
#include "Survivor/SurvivorPawn.h"

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

    // Always reset external rotation control at the start of each action
    Perceptor->bExternalRotationControl = false;
    KitingShootTimer = 0.f;

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
            // No house in sight yet — wander until perception spots one
            Perceptor->ActivateWanderMode();
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
        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (SurvivorPawn && Inv)
        {
            const TArray<ABaseItem*>& Slots = Inv->GetInventory();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (AMedkit* Medkit = Cast<AMedkit>(Slots[i]))
                {
                    Medkit->UseItem(*SurvivorPawn);
                    Inv->RemoveItem(i);
                    UE_LOG(LogTemp, Log, TEXT("BTTask: Used and consumed medkit '%s' from slot %d"), *Medkit->GetName(), i);
                    break;
                }
            }
        }
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

    // Opportunistic pickup: grab any useful item within touching range every tick,
    // regardless of current action. Clears items that block movement when they spawn nearby.
    {
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (Inv)
        {
            const float GrabRadiusSq = 150.f * 150.f;
            FVector MyLoc = ControlledPawn->GetActorLocation();

            for (int32 i = Perceptor->KnownItems.Num() - 1; i >= 0; --i)
            {
                AActor* LootActor = Perceptor->KnownItems[i];
                if (!IsValid(LootActor)) { Perceptor->KnownItems.RemoveAt(i); continue; }

                ABaseItem* Item = Cast<ABaseItem>(LootActor);
                if (!Item || Item->GetItemType() == EItemType::Garbage) continue;
                if (FVector::DistSquared(MyLoc, LootActor->GetActorLocation()) > GrabRadiusSq) continue;

                const TArray<ABaseItem*>& Slots = Inv->GetInventory();
                for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
                {
                    if (Slots[Slot] == nullptr)
                    {
                        Inv->GrabItem(Slot, Item);
                        Perceptor->PerceivedLoot.Remove(LootActor);
                        Perceptor->KnownItems.RemoveAt(i);
                        UE_LOG(LogTemp, Log, TEXT("BTTask: Opportunistic pickup '%s' into slot %d"), *Item->GetName(), Slot);
                        break;
                    }
                }
                break; // One item per tick to avoid array modification issues
            }
        }
    }

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
            // Mirror what ExecuteTask would have done.
            AActor* HouseActor = Perceptor->GetNearestUnexploredHouse();
            if (HouseActor)
            {
                AHouse* House = Cast<AHouse>(HouseActor);
                FVector Target = House ? House->GetBounds().Origin : HouseActor->GetActorLocation();
                Perceptor->ActivateNavigationTarget(Target);
            }
            else
            {
                Perceptor->ActivateWanderMode();
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

    // UseMedkit: instant use — handles the post-interrupt case where ExecuteTask never re-ran
    if (CurrentAction.ActionName == TEXT("Action_UseMedkit"))
    {
        ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
        UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
        if (SurvivorPawn && Inv)
        {
            const TArray<ABaseItem*>& Slots = Inv->GetInventory();
            for (int32 i = 0; i < Slots.Num(); ++i)
            {
                if (AMedkit* Medkit = Cast<AMedkit>(Slots[i]))
                {
                    Medkit->UseItem(*SurvivorPawn);
                    Inv->RemoveItem(i);
                    UE_LOG(LogTemp, Log, TEXT("BTTask: Used and consumed medkit '%s' from slot %d (TickTask path)"), *Medkit->GetName(), i);
                    break;
                }
            }
        }
        Brain->CompleteCurrentAction();
        FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
        return;
    }

    // Kiting: flee from zombie, face it and shoot periodically
    if (CurrentAction.ActionName == TEXT("Action_Kiting"))
    {
        // If flee hasn't been started yet (plan was interrupted and re-routed here without
        // ExecuteTask running), kick it off now
        if (!Perceptor->IsFleeing())
        {
            AActor* InitThreat = Perceptor->GetHighestThreatZombie();
            if (InitThreat)
            {
                Perceptor->ActivateFleeFrom(InitThreat->GetActorLocation());
                UE_LOG(LogTemp, Log, TEXT("BTTask: Kiting — re-initializing flee from TickTask"));
            }
        }

        // Check whether we have a usable weapon
        bool bHasUsableWeapon = false;
        {
            UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
            if (Inv)
            {
                for (ABaseItem* Item : Inv->GetInventory())
                {
                    AWeapon* W = Cast<AWeapon>(Item);
                    if (W && W->GetValue() > 0) { bHasUsableWeapon = true; break; }
                }
            }
        }

        // Only take rotation control when we can actually shoot — otherwise let movement
        // direction drive facing (survivor looks where they're running, not back at threat)
        Perceptor->bExternalRotationControl = bHasUsableWeapon;

        AActor* Threat = Perceptor->GetHighestThreatZombie();
        if (Threat && bHasUsableWeapon)
        {
            // Face toward the zombie so UseItem fires along the forward vector
            FVector ToThreat = (Threat->GetActorLocation() - ControlledPawn->GetActorLocation()).GetSafeNormal();
            ControlledPawn->SetActorRotation(FRotator(0.f, ToThreat.ToOrientationRotator().Yaw, 0.f));

            // Shoot on cooldown
            KitingShootTimer -= DeltaSeconds;
            if (KitingShootTimer <= 0.f)
            {
                ASurvivorPawn* SurvivorPawn = Cast<ASurvivorPawn>(ControlledPawn);
                UInventoryComponent* Inv = ControlledPawn->FindComponentByClass<UInventoryComponent>();
                if (SurvivorPawn && Inv)
                {
                    for (ABaseItem* Item : Inv->GetInventory())
                    {
                        AWeapon* Weapon = Cast<AWeapon>(Item);
                        if (Weapon && Weapon->GetValue() > 0)
                        {
                            Weapon->UseItem(*SurvivorPawn);
                            UE_LOG(LogTemp, Log, TEXT("BTTask: Kiting — fired %s"), *Weapon->GetName());
                            break;
                        }
                    }
                }
                KitingShootTimer = 0.5f; // one shot attempt every 0.5 seconds
            }
        }

        // Complete when flee behavior loses the zombie
        if (Perceptor->IsCurrentActionFinished())
        {
            Perceptor->bExternalRotationControl = false;
            Brain->CompleteCurrentAction();
            FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
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