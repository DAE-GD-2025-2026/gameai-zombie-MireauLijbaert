#include "GoapPlanner.h"

TArray<FGoapAction> GoapPlanner::Plan(const TArray<FGoapAction>& AvailableActions, 
                                      const FGoapState& CurrentState, 
                                      const FGoapState& Goal)
{
    TArray<FGoapAction> EmptyPlan;

    TArray<FGoapNode*> OpenList;
    TArray<FGoapNode*> ClosedList;

    FGoapNode* StartNode = new FGoapNode();
    StartNode->State = CurrentState;
    StartNode->GCost = 0;
    StartNode->HCost = CalculateHeuristic(CurrentState, Goal);
    StartNode->Parent = nullptr;

    OpenList.Add(StartNode);
    FGoapNode* GoalNode = nullptr;

    while (OpenList.Num() > 0)
    {
        OpenList.Sort([](const FGoapNode& A, const FGoapNode& B) {
            return A.GetFCost() < B.GetFCost();
        });

        FGoapNode* CurrentNode = OpenList[0];
        OpenList.RemoveAt(0);
        ClosedList.Add(CurrentNode);

        if (GoapHelpers::IsStateMatch(CurrentNode->State, Goal))
        {
            GoalNode = CurrentNode;
            break;
        }

        for (const FGoapAction& Action : AvailableActions)
        {
            if (GoapHelpers::IsStateMatch(CurrentNode->State, Action.Preconditions))
            {
                FGoapState SimulatedState = CurrentNode->State;
                for (const auto& Effect : Action.Effects)
                {
                    // Calling .Add() here to match your header function name!
                    SimulatedState.Add(Effect.Key, Effect.Value);
                }

                bool bAlreadyEvaluated = false;
                for (const FGoapNode* ClosedNode : ClosedList)
                {
                    if (ClosedNode->State == SimulatedState)
                    {
                        bAlreadyEvaluated = true;
                        break;
                    }
                }
                if (bAlreadyEvaluated) continue;

                int32 NewGCost = CurrentNode->GCost + Action.Cost;
                
                FGoapNode** ExistingOpenNode = OpenList.FindByPredicate([&SimulatedState](const FGoapNode* Node) {
                    return Node->State == SimulatedState;
                });

                if (ExistingOpenNode)
                {
                    if (NewGCost < (*ExistingOpenNode)->GCost)
                    {
                        (*ExistingOpenNode)->GCost = NewGCost;
                        (*ExistingOpenNode)->Parent = CurrentNode;
                        (*ExistingOpenNode)->Action = Action;
                    }
                }
                else
                {
                    FGoapNode* NeighborNode = new FGoapNode();
                    NeighborNode->State = SimulatedState;
                    NeighborNode->GCost = NewGCost;
                    NeighborNode->HCost = CalculateHeuristic(SimulatedState, Goal);
                    NeighborNode->Parent = CurrentNode;
                    NeighborNode->Action = Action;

                    OpenList.Add(NeighborNode);
                }
            }
        }
    }

    TArray<FGoapAction> FinalPlan;
    if (GoalNode != nullptr)
    {
        FinalPlan = BuildPlan(GoalNode);
    }

    for (FGoapNode* Node : OpenList)   delete Node;
    for (FGoapNode* Node : ClosedList) delete Node;

    return FinalPlan;
}

TArray<FGoapAction> GoapPlanner::BuildPlan(FGoapNode* GoalNode)
{
    TArray<FGoapAction> BackwardPlan;
    FGoapNode* Current = GoalNode;

    while (Current != nullptr && !Current->Action.ActionName.IsEmpty())
    {
        BackwardPlan.Add(Current->Action);
        Current = Current->Parent;
    }

    TArray<FGoapAction> ForwardPlan;
    for (int32 i = BackwardPlan.Num() - 1; i >= 0; --i)
    {
        ForwardPlan.Add(BackwardPlan[i]);
    }

    return ForwardPlan;
}

int32 GoapPlanner::CalculateHeuristic(const FGoapState& CurrentState, const FGoapState& Goal)
{
    int32 UnmetConditions = 0;
    for (const auto& GoalCondition : Goal)
    {
        const int32* CurrentVal = CurrentState.Find(GoalCondition.Key);
        if (!CurrentVal || *CurrentVal != GoalCondition.Value)
        {
            UnmetConditions++;
        }
    }
    return UnmetConditions;
}