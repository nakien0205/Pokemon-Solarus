#include "Battle/BattleEngine.h"
#include "Battle/BattleAbility.h"
#include "Battle/BattleBagItem.h"
#include "Battle/BattleCapture.h"
#include "Battle/BattleEffectExecutor.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleFaintOutcomeResolver.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleStatCalculator.h"
#include "Battle/BattleSwitching.h"
#include "Battle/BattleVolatile.h"
#include "Battle/BattleWildFlow.h"
#include "BattleEngineCheckpointState.h"
#include "BattleEngineCommon.h"
#include "BattleEngineEvents.h"
#include "BattleEngineQueueBoundary.h"
#include "BattleEngineSwitchPipeline.h"
#include "BattleEngineTriggerRuntime.h"
#include "BattleResolutionCommit.h"
#include "Math/NumericLimits.h"

namespace BattleEngineReplayPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;
}

using namespace BattleEngineReplayPrivate;

FBattleReplayInputs FBattleEngine::ExportReplayInputs() const
{
	FBattleReplayInputs Inputs;
	if (State.IsValid())
	{
		Inputs.Setup = State->Setup;
		Inputs.Decisions = State->SubmittedDecisions;
		Inputs.StatRefreshes = State->SubmittedStatRefreshes;
	}
	return Inputs;
}

TArray<FBattleRandomDraw> FBattleEngine::ExportRandomTrace() const
{
	TArray<FBattleRandomDraw> Trace;
	if (State.IsValid() && State->Random.IsValid())
	{
		for (const FBattleRandomDraw& Draw : State->Random->GetTrace())
		{
			Trace.Add(Draw);
		}
	}
	return Trace;
}

FBattleReplayRecord FBattleEngine::ExportReplayRecord() const
{
	FBattleReplayRecord Record;
	if (!State.IsValid())
	{
		return Record;
	}
	const TArray<FBattleRandomDraw> Trace = ExportRandomTrace();
	const bool bCreated = FBattleReplayRecord::TryCreate(
		FBattleReplayRecord::CurrentSchemaVersion,
		ExportReplayInputs(),
		State->Resolutions,
		Trace,
		GetSnapshot(),
		Record);
	ensure(bCreated);
	return Record;
}
