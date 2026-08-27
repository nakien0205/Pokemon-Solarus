#if WITH_DEV_AUTOMATION_TESTS

#include "BattleAtomicCheckpointTestFaults.h"

namespace BattleAtomicCheckpointTestFaultsPrivate
{
	using namespace BattleAtomicCheckpointTestCommonPrivate;

bool TryMakeFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<uint32> Results,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure )
{
		TUniquePtr<FFaultBattleRandom> Fault =
			MakeUnique<FFaultBattleRandom>(
				MoveTemp(Results),
				Mode,
				SuccessfulDrawsBeforeFailure);
		OutRandom = Fault.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Fault);
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

bool TryMakeStrictFaultEngine(
		const FAtomicWildScenario& Scenario,
		TArray<FBattleExpectedRandomDraw> ExpectedDraws,
		const EFaultRandomMode Mode,
		TUniquePtr<FBattleEngine>& OutEngine,
		FFaultBattleRandom*& OutRandom,
		const int32 SuccessfulDrawsBeforeFailure )
{
		TUniquePtr<FFaultBattleRandom> Fault =
			MakeUnique<FFaultBattleRandom>(
				MoveTemp(ExpectedDraws),
				Mode,
				SuccessfulDrawsBeforeFailure);
		OutRandom = Fault.Get();
		TUniquePtr<IBattleRandom> Random = MoveTemp(Fault);
		return TryMakeEngine(Scenario, MoveTemp(Random), OutEngine);
	}

bool TryMakeActionStartStaleEngine(
		const FAtomicWildScenario& Scenario,
		TUniquePtr<FBattleEngine>& OutEngine,
		FActionStartStaleRandom*& OutRandom,
		TArray<uint32> Results )
{
		TUniquePtr<FActionStartStaleRandom> Random =
			MakeUnique<FActionStartStaleRandom>(MoveTemp(Results));
		OutRandom = Random.Get();
		TUniquePtr<IBattleRandom> Base = MoveTemp(Random);
		return TryMakeEngine(Scenario, MoveTemp(Base), OutEngine);
	}
}

#endif
