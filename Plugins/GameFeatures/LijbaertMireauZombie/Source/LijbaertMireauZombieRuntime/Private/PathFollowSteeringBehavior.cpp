#include "PathFollowSteeringBehavior.h"
#include "SteeringAgent.h"

PathFollow::PathFollow()
{
	pSeek = new Seek();
	pArrive = new Arrive();
	pArrive->SetTargetRadius(10.0f);
}

PathFollow::~PathFollow()
{
	delete pArrive;
	delete pSeek;
}

void PathFollow::SetPath(std::vector<FVector2D>& path)
{
	pathVec = path;
	currentPathIndex = -1;
	bPathFinished = false;
	GotoNextPathPoint();
}

SteeringOutput PathFollow::CalculateSteering(float DeltaTime, ASteeringAgent& Agent)
{
	// Path is done — return zero so the perceptor detects arrival and fires bActionFinished
	if (bPathFinished)
		return SteeringOutput{};

	if (currentPathIndex < static_cast<int>(pathVec.size()))
	{
		float agentRadius = Agent.GetCapsuleRadius();
		FVector2D ToPathPoint{pathVec[currentPathIndex] - Agent.GetPosition()};

		if (ToPathPoint.SizeSquared() < agentRadius * agentRadius)
		{
			GotoNextPathPoint();
		}
	}

	if (pCurrentSteering != nullptr)
	{
		return pCurrentSteering->CalculateSteering(DeltaTime, Agent);
	}
	return SteeringOutput{};
}

void PathFollow::GotoNextPathPoint()
{
	++currentPathIndex;

	// Reached the end — stop here instead of looping back to the start
	if (currentPathIndex >= static_cast<int>(pathVec.size()))
	{
		bPathFinished = true;
		pCurrentSteering = nullptr;
		return;
	}

	if (currentPathIndex == static_cast<int>(pathVec.size()) - 1)
	{
		// Last waypoint — use Arrive so we slow to a clean stop
		FTargetData PathTarget{pathVec[currentPathIndex]};
		pArrive->SetTarget(PathTarget);
		pCurrentSteering = pArrive;
	}
	else
	{
		FTargetData PathTarget{pathVec[currentPathIndex]};
		pSeek->SetTarget(PathTarget);
		pCurrentSteering = pSeek;
	}
}
