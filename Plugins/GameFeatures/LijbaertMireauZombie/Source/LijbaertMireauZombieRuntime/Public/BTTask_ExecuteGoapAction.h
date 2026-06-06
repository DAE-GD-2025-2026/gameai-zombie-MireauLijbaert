#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ExecuteGoapAction.generated.h"

UCLASS()
class LIJBAERTMIREAUZOMBIERUNTIME_API UBTTask_ExecuteGoapAction : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ExecuteGoapAction();

	// This runs the moment the Behavior Tree hits this node
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// This ticks every frame while the node is active
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};