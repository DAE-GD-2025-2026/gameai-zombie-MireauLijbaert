#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "GoapAgentInterface.generated.h"

UINTERFACE(MinimalAPI, Blueprintable)
class UGoapAgentInterface : public UInterface
{
	GENERATED_BODY()
};

class LIJBAERTMIREAUZOMBIERUNTIME_API IGoapAgentInterface
{
	GENERATED_BODY()

public:
	virtual float GetActionDuration(const FString& ActionName) = 0;
	virtual bool ExecutePhysicalAction(const FString& ActionName) = 0;
	virtual bool IsActionFinished(const FString& ActionName) = 0;
	
};