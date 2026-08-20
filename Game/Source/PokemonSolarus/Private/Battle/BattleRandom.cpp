#include "Battle/BattleRandom.h"

FSeededBattleRandom::FSeededBattleRandom(const uint64 InSeed)
	: InitialSeed(InSeed)
	, State(InSeed)
{
}

bool FSeededBattleRandom::TryDrawUniform(
	const uint32 InclusiveMinimum,
	const uint32 InclusiveMaximum,
	const FBattleRandomContext& Context,
	FBattleRandomDraw& OutDraw)
{
	OutDraw = FBattleRandomDraw();
	if (InclusiveMinimum > InclusiveMaximum || !Context.IsValid() || NextCallOrdinal == 0)
	{
		return false;
	}

	const uint64 Bound = static_cast<uint64>(InclusiveMaximum)
		- static_cast<uint64>(InclusiveMinimum)
		+ 1ULL;
	const uint64 RejectionThreshold = (0ULL - Bound) % Bound;

	uint64 RawValue = 0;
	do
	{
		RawValue = NextRawValue();
	}
	while (RawValue < RejectionThreshold);

	FBattleRandomDraw Draw;
	Draw.InclusiveMinimum = InclusiveMinimum;
	Draw.InclusiveMaximum = InclusiveMaximum;
	Draw.Bound = Bound;
	Draw.RawValue = RawValue;
	Draw.Result = InclusiveMinimum + static_cast<uint32>(RawValue % Bound);
	Draw.CallOrdinal = NextCallOrdinal;
	Draw.BattleId = Context.BattleId;
	Draw.TurnId = Context.TurnId;
	Draw.ActionId = Context.ActionId;
	Draw.ResolutionId = Context.ResolutionId;
	Draw.RulePurpose = Context.RulePurpose;

	Trace.Add(Draw);
	OutDraw = Draw;
	++NextCallOrdinal;
	return true;
}

TConstArrayView<FBattleRandomDraw> FSeededBattleRandom::GetTrace() const
{
	return Trace;
}

uint64 FSeededBattleRandom::NextRawValue()
{
	State += 0x9E3779B97F4A7C15ULL;
	uint64 Mixed = State;
	Mixed = (Mixed ^ (Mixed >> 30U)) * 0xBF58476D1CE4E5B9ULL;
	Mixed = (Mixed ^ (Mixed >> 27U)) * 0x94D049BB133111EBULL;
	return Mixed ^ (Mixed >> 31U);
}
