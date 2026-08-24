#if WITH_DEV_AUTOMATION_TESTS

#include "Algo/Count.h"
#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleDataTableRows.h"
#include "Battle/BattleEngine.h"
#include "Battle/BattleReplay.h"
#include "Battle/BattleEffectExecutor.h"
#include "Battle/BattleState.h"
#include "BattleTestFactories.h"
#include "Engine/DataTable.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectGlobals.h"

class FBattleC05BEngineFixture
{
public:
	static bool SetCurrentEffectExecutionState(
		FBattleEngine& Engine,
		const EBattleLockedEffectExecutionState State)
	{
		if (!Engine.State.IsValid()
			|| !Engine.State->LockedActions.IsValidIndex(Engine.State->CurrentLockedActionIndex))
		{
			return false;
		}

		Engine.State->LockedActions[Engine.State->CurrentLockedActionIndex].EffectExecutionState = State;
		return true;
	}

	static int32 GetCurrentLockedActionIndex(const FBattleEngine& Engine)
	{
		return Engine.State.IsValid() ? Engine.State->CurrentLockedActionIndex : INDEX_NONE;
	}

	static bool SetBattlerHpForExecution(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const int32 CurrentHP)
	{
		if (!Engine.State.IsValid())
		{
			return false;
		}
		FBattleBattlerState* Battler = Engine.State->FindMutableBattler(BattlerId);
		if (Battler == nullptr || CurrentHP <= 0 || CurrentHP > Battler->PermanentStats.MaxHP)
		{
			return false;
		}
		Battler->CurrentHP = CurrentHP;
		Battler->bFainted = false;
		Battler->bFaintTransitionPending = false;
		return true;
	}

	static bool SeedDuplicateStatusAndCappedAttack(
		FBattleEngine& Engine,
		const FBattlerId BattlerId,
		const FConditionId StatusId)
	{
		if (!Engine.State.IsValid() || !StatusId.IsValid())
		{
			return false;
		}
		FBattleBattlerState* Battler = Engine.State->FindMutableBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		Battler->MajorStatusId = StatusId;
		const FBattleStatStageChangeResult StageResult = Battler->Stages.ApplyChange(
			EBattleStat::Attack,
			6);
		return StageResult.Outcome == EBattleStatStageChangeOutcome::Applied
			&& StageResult.NewStage == 6;
	}

	static bool GetFaintTransitionFacts(
		const FBattleEngine& Engine,
		const FBattlerId BattlerId,
		bool& bOutFainted,
		bool& bOutPending)
	{
		bOutFainted = false;
		bOutPending = false;
		if (!Engine.State.IsValid())
		{
			return false;
		}
		const FBattleBattlerState* Battler = Engine.State->FindBattler(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		bOutFainted = Battler->bFainted;
		bOutPending = Battler->bFaintTransitionPending;
		return true;
	}
};

namespace BattleEffectExecutorTests
{
	using BattleTest::MakeActiveSlotId;
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;
	using BattleTest::MakePartySlotId;

	constexpr uint64 BattleValue = 5505;
	constexpr uint64 PlayerTrainerValue = 1;
	constexpr uint64 OpponentTrainerValue = 2;
	constexpr uint64 PlayerLeftBattlerValue = 11;
	constexpr uint64 PlayerRightBattlerValue = 12;
	constexpr uint64 OpponentLeftBattlerValue = 21;
	constexpr uint64 OpponentRightBattlerValue = 22;

	const TCHAR* MoveName = TEXT("Move.C05B.Test");
	const TCHAR* SpeciesName = TEXT("Species.C05B.Test");
	const TCHAR* AbilityName = TEXT("Ability.C05B.Test");
	const TCHAR* ItemName = TEXT("Item.C05B.Test");
	const TCHAR* MajorConditionName = TEXT("Condition.C05B.Major");
	const TCHAR* VolatileConditionName = TEXT("Condition.C05B.Volatile");
	const TCHAR* WeatherConditionName = TEXT("Condition.C05B.Weather");
	const TCHAR* SideConditionName = TEXT("Condition.C05B.Side");

	struct FExpectedDraw
	{
		uint32 Minimum = 0;
		uint32 Maximum = 0;
		uint32 Result = 0;
		FDefinitionId RulePurpose;
	};

	class FStrictScriptedRandom final : public IBattleRandom
	{
	public:
		explicit FStrictScriptedRandom(TArray<FExpectedDraw> InExpectedDraws)
			: ExpectedDraws(MoveTemp(InExpectedDraws))
		{
		}

		virtual bool TryDrawUniform(
			const uint32 InclusiveMinimum,
			const uint32 InclusiveMaximum,
			const FBattleRandomContext& Context,
			FBattleRandomDraw& OutDraw) override
		{
			OutDraw = FBattleRandomDraw();
			if (bMismatch || !ExpectedDraws.IsValidIndex(NextExpectedIndex))
			{
				bMismatch = true;
				Mismatch = TEXT("An unexpected extra RNG draw was requested");
				return false;
			}

			const FExpectedDraw& Expected = ExpectedDraws[NextExpectedIndex];
			if (!Context.IsValid()
				|| InclusiveMinimum != Expected.Minimum
				|| InclusiveMaximum != Expected.Maximum
				|| Context.RulePurpose != Expected.RulePurpose
				|| Expected.Result < InclusiveMinimum
				|| Expected.Result > InclusiveMaximum)
			{
				bMismatch = true;
				Mismatch = FString::Printf(
					TEXT("RNG draw %d differed: got U[%u,%u] purpose %s, expected U[%u,%u] purpose %s result %u"),
					NextExpectedIndex,
					InclusiveMinimum,
					InclusiveMaximum,
					*Context.RulePurpose.GetName().ToString(),
					Expected.Minimum,
					Expected.Maximum,
					*Expected.RulePurpose.GetName().ToString(),
					Expected.Result);
				return false;
			}

			++NextExpectedIndex;
			OutDraw.InclusiveMinimum = InclusiveMinimum;
			OutDraw.InclusiveMaximum = InclusiveMaximum;
			OutDraw.Bound = static_cast<uint64>(InclusiveMaximum)
				- static_cast<uint64>(InclusiveMinimum) + 1;
			OutDraw.RawValue = Expected.Result;
			OutDraw.Result = Expected.Result;
			OutDraw.CallOrdinal = static_cast<uint64>(Trace.Num() + 1);
			OutDraw.BattleId = Context.BattleId;
			OutDraw.TurnId = Context.TurnId;
			OutDraw.ActionId = Context.ActionId;
			OutDraw.ResolutionId = Context.ResolutionId;
			OutDraw.RulePurpose = Context.RulePurpose;
			Trace.Add(OutDraw);
			return true;
		}

		virtual TConstArrayView<FBattleRandomDraw> GetTrace() const override
		{
			return Trace;
		}

		[[nodiscard]] bool IsExact() const
		{
			return !bMismatch && NextExpectedIndex == ExpectedDraws.Num();
		}

		[[nodiscard]] const FString& GetMismatch() const
		{
			return Mismatch;
		}

	private:
		TArray<FExpectedDraw> ExpectedDraws;
		int32 NextExpectedIndex = 0;
		bool bMismatch = false;
		FString Mismatch;
		TArray<FBattleRandomDraw> Trace;
	};

	FBattleEffectHookResult MakeHookResult(
		const EBattleEffectExecutionOutcome Outcome = EBattleEffectExecutionOutcome::Applied,
		const TCHAR* RuleName = TEXT("Rule.C05B.Test"))
	{
		FBattleEffectHookResult Result;
		Result.Outcome = Outcome;
		Result.RuleId = MakeDefinitionId<FDefinitionId>(RuleName);
		return Result;
	}

	FBattleResolvedTarget MakeBattlerTarget(
		const EBattleSide Side,
		const EBattlePosition Position,
		const uint64 BattlerValue)
	{
		FBattleBattlerTarget Battler;
		Battler.ActiveSlotId = MakeActiveSlotId(Side, Position);
		Battler.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		FBattleResolvedTarget Target;
		const bool bCreated = FBattleResolvedTarget::TryCreateBattler(Battler, Target);
		check(bCreated);
		return Target;
	}

	FBattleResolvedTarget MakeSideTarget(const EBattleSide Side)
	{
		FBattleResolvedTarget Target;
		const bool bCreated = FBattleResolvedTarget::TryCreateSide(Side, Target);
		check(bCreated);
		return Target;
	}

	FBattleResolvedTarget MakeFieldTarget()
	{
		return FBattleResolvedTarget::CreateField();
	}

	FBattleMoveEffectDescriptor MakeEffect(
		const int32 Order,
		const EBattleMoveEffectKind Kind,
		const EBattleEffectTarget Target)
	{
		FBattleMoveEffectDescriptor Effect;
		Effect.Order = Order;
		Effect.Kind = Kind;
		Effect.Target = Target;
		return Effect;
	}

	FBattleMoveDefinition MakeDamagingMove(
		const EBattleTargetClass TargetClass = EBattleTargetClass::SelectedOpponent,
		const EBattleEffectTarget EffectTarget = EBattleEffectTarget::ResolvedTarget)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Physical;
		Move.Power = 40;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 10;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 0;
		Move.TargetClass = TargetClass;
		Move.Flags = EBattleMoveFlags::NeverCritical;
		Move.Effects.Add(MakeEffect(0, EBattleMoveEffectKind::Damage, EffectTarget));
		return Move;
	}

	FBattleMoveDefinition MakeStatusMove(
		const EBattleTargetClass TargetClass,
		TArray<FBattleMoveEffectDescriptor> Effects)
	{
		FBattleMoveDefinition Move;
		Move.Id = MakeDefinitionId<FMoveId>(MoveName);
		Move.Type = EPokemonType::Normal;
		Move.Category = EBattleMoveCategory::Status;
		Move.Power = 0;
		Move.bAlwaysHits = true;
		Move.Accuracy = 0;
		Move.bUsesPP = true;
		Move.BasePP = 10;
		Move.bAllowsPPBoosts = true;
		Move.Priority = 0;
		Move.TargetClass = TargetClass;
		Move.Effects = MoveTemp(Effects);
		return Move;
	}

	FBattleEffectExecutionRequest MakeRequest(
		const FBattleMoveDefinition& Move,
		TArray<FBattleResolvedTarget> Targets,
		const uint64 ResolutionValue = 1)
	{
		FBattleEffectExecutionRequest Request;
		Request.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Request.TurnId = MakeNumericId<FTurnId>(1);
		Request.ActionId = MakeNumericId<FActionId>(1);
		Request.ResolutionId = MakeNumericId<FResolutionId>(ResolutionValue);
		Request.UserBattlerId = MakeNumericId<FBattlerId>(PlayerLeftBattlerValue);
		Request.UserSlotId = MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left);
		Request.Move = &Move;
		Request.Targets = MoveTemp(Targets);
		return Request;
	}

	struct FMockHp
	{
		int32 Current = 100;
		int32 Maximum = 100;
	};

	class FMockExecutionContext final : public IBattleEffectExecutionContext
	{
	public:
		FMockExecutionContext()
		{
			HpByBattler.Add(PlayerLeftBattlerValue, {100, 100});
			HpByBattler.Add(PlayerRightBattlerValue, {100, 100});
			HpByBattler.Add(OpponentLeftBattlerValue, {100, 100});
			HpByBattler.Add(OpponentRightBattlerValue, {100, 100});
		}

		virtual bool PrevalidateRequest(const FBattleEffectExecutionRequest& Request) const override
		{
			(void)Request;
			return bPrevalidateRequest;
		}

		virtual FBattleEffectHookResult CheckReachability(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("Reachability"), Target));
			return GateResult(ReachabilityOutcomes, Target);
		}

		virtual FBattleEffectHookResult CheckProtection(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("Protection"), Target));
			return GateResult(ProtectionOutcomes, Target);
		}

		virtual FBattleEffectHookResult CheckTryHit(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("TryHit"), Target));
			return GateResult(TryHitOutcomes, Target);
		}

		virtual FBattleEffectHookResult CheckMoveImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("MoveImmunity"), Target));
			return GateResult(MoveImmunityOutcomes, Target);
		}

		virtual FBattleEffectHookResult CheckAbilityImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("AbilityImmunity"), Target));
			return GateResult(AbilityImmunityOutcomes, Target);
		}

		virtual FBattleEffectHookResult CheckItemImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("ItemImmunity"), Target));
			return GateResult(ItemImmunityOutcomes, Target);
		}

		virtual FBattleEffectHookResult ApplyProtectionBreaking(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("ProtectionBreaking"), Target));
			return GateResult(ProtectionBreakingOutcomes, Target);
		}

		virtual bool TryBuildAccuracyInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleAccuracyCheckInput& OutInput) override
		{
			Calls.Add(GateCall(TEXT("Accuracy"), Target));
			OutInput = FBattleAccuracyCheckInput();
			OutInput.bAlwaysHits = Move.bAlwaysHits;
			OutInput.BaseAccuracy = Move.Accuracy;
			return true;
		}

		virtual bool TryBuildCriticalInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleCriticalCheckInput& OutInput) override
		{
			Calls.Add(GateCall(TEXT("Critical"), Target));
			OutInput = FBattleCriticalCheckInput();
			OutInput.Mode = EnumHasAllFlags(Move.Flags, EBattleMoveFlags::AlwaysCritical)
				? EBattleCriticalCheckMode::Always
				: EnumHasAllFlags(Move.Flags, EBattleMoveFlags::NeverCritical)
					? EBattleCriticalCheckMode::Never
					: EBattleCriticalCheckMode::Standard;
			OutInput.BaseStage = 1;
			return true;
		}

		virtual bool TryBuildDamageInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			const bool bSpreadAcrossMultipleTargets,
			FBattleFinalDamageInput& OutInput) override
		{
			Calls.Add(GateCall(TEXT("TypeImmunity"), Target));
			OutInput = FBattleFinalDamageInput();
			OutInput.AttackerLevel = 50;
			OutInput.AttackerStats = {100, 100, 100, 100, 100, 100};
			OutInput.DefenderStats = {100, 100, 100, 100, 100, 100};
			OutInput.MoveCategory = Move.Category;
			OutInput.MovePower = Move.Power;
			OutInput.bBypassTypeImmunity = EnumHasAllFlags(Move.Flags, EBattleMoveFlags::TypelessDamage);
			OutInput.bSpreadAcrossMultipleTargets = bSpreadAcrossMultipleTargets;
			OutInput.TypeEffectiveness = TypeImmunityTargets.Contains(TargetBattlerValue(Target))
				? FBattleTypeEffectiveness{0, 1}
				: FBattleTypeEffectiveness{1, 1};
			return true;
		}

		virtual bool IsSourceAbleToContinue() const override
		{
			const FMockHp* Hp = HpByBattler.Find(PlayerLeftBattlerValue);
			return bSourceCanContinue && Hp != nullptr && Hp->Current > 0;
		}

		virtual bool IsTargetAbleToContinue(const FBattleResolvedTarget& Target) const override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return true;
			}
			const FMockHp* Hp = HpByBattler.Find(TargetBattlerValue(Target));
			return Hp != nullptr && Hp->Current > 0;
		}

		virtual FBattleEffectHookResult CheckEffectEligibility(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(FString::Printf(TEXT("Eligibility:%d:%llu"), Effect.Order, TargetBattlerValue(Target)));
			if (const EBattleEffectExecutionOutcome* Outcome = EligibilityOutcomes.Find(Effect.Order))
			{
				return MakeHookResult(*Outcome, TEXT("Rule.C05B.Eligibility"));
			}
			return MakeHookResult();
		}

		virtual bool TryGetHp(
			const FBattleResolvedTarget& Target,
			int32& OutCurrentHP,
			int32& OutMaximumHP) const override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return false;
			}
			const FMockHp* Hp = HpByBattler.Find(TargetBattlerValue(Target));
			if (Hp == nullptr)
			{
				return false;
			}
			OutCurrentHP = Hp->Current;
			OutMaximumHP = Hp->Maximum;
			return true;
		}

		virtual FBattleEffectHookResult ApplyHpDelta(
			const FBattleResolvedTarget& Target,
			const int32 RequestedDelta) override
		{
			Calls.Add(FString::Printf(TEXT("HP:%d:%llu"), RequestedDelta, TargetBattlerValue(Target)));
			if (ForcedHpDeltaOutcome.IsSet())
			{
				return MakeHookResult(
					ForcedHpDeltaOutcome.GetValue(),
					TEXT("Rule.C05B.ForcedHpDelta"));
			}
			FBattleEffectHookResult Result = MakeHookResult();
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				Result.Outcome = EBattleEffectExecutionOutcome::Invalid;
				return Result;
			}

			FMockHp* Hp = HpByBattler.Find(TargetBattlerValue(Target));
			if (Hp == nullptr)
			{
				Result.Outcome = EBattleEffectExecutionOutcome::Invalid;
				return Result;
			}

			const int32 Before = Hp->Current;
			const int64 Requested = static_cast<int64>(Before) + RequestedDelta;
			const int32 After = static_cast<int32>(FMath::Clamp<int64>(Requested, 0, Hp->Maximum));
			Result.NumericBefore = Before;
			Result.NumericAfter = After;
			Result.NumericDelta = After - Before;
			Result.bStateMutated = After != Before;
			Result.bCapped = static_cast<int64>(After) != Requested;
			if (!Result.bStateMutated)
			{
				Result.Outcome = RequestedDelta > 0
					? EBattleEffectExecutionOutcome::Capped
					: EBattleEffectExecutionOutcome::Failed;
			}
			Hp->Current = After;
			return Result;
		}

		virtual FBattleEffectHookResult ApplyNonHpEffect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			Calls.Add(FString::Printf(TEXT("Effect:%d:%d"), Effect.Order, static_cast<int32>(Effect.Kind)));
			AppliedEffectOrders.Add(Effect.Order);
			if (const EBattleEffectExecutionOutcome* Forced = ApplicationOutcomes.Find(Effect.Order))
			{
				return MakeHookResult(*Forced, TEXT("Rule.C05B.Application"));
			}

			FBattleEffectHookResult Result = MakeHookResult();
			switch (Effect.Kind)
			{
			case EBattleMoveEffectKind::ApplyCondition:
				Result.bStateMutated = true;
				break;
			case EBattleMoveEffectKind::ModifyStatStage:
			{
				const int32 Before = AttackStage;
				const int32 Requested = Before + Effect.MagnitudeNumerator;
				AttackStage = FMath::Clamp(Requested, -6, 6);
				Result.NumericBefore = Before;
				Result.NumericAfter = AttackStage;
				Result.NumericDelta = AttackStage - Before;
				Result.bStateMutated = AttackStage != Before;
				Result.bCapped = AttackStage != Requested;
				if (!Result.bStateMutated)
				{
					Result.Outcome = EBattleEffectExecutionOutcome::Capped;
				}
				break;
			}
			case EBattleMoveEffectKind::SetFieldCondition:
			case EBattleMoveEffectKind::SetSideCondition:
			case EBattleMoveEffectKind::RemoveCondition:
				Result.bStateMutated = true;
				break;
			case EBattleMoveEffectKind::Switch:
			case EBattleMoveEffectKind::ChangeItem:
			case EBattleMoveEffectKind::Charge:
			case EBattleMoveEffectKind::Recharge:
			case EBattleMoveEffectKind::Protect:
			case EBattleMoveEffectKind::SemiInvulnerability:
				Result.Outcome = EBattleEffectExecutionOutcome::Deferred;
				break;
			default:
				Result.Outcome = EBattleEffectExecutionOutcome::Failed;
				break;
			}
			return Result;
		}

		virtual void RunImmediateUpdate(const FBattleResolvedTarget& Target) override
		{
			Calls.Add(GateCall(TEXT("ImmediateUpdate"), Target));
			++ImmediateUpdateCount;
		}

		virtual bool TryBuildEventTarget(
			const FBattleResolvedTarget& Target,
			FBattleEventTarget& OutTarget) const override
		{
			OutTarget = FBattleEventTarget();
			switch (Target.GetKind())
			{
			case EBattleResolvedTargetKind::Battler:
				OutTarget.BattlerId = Target.GetBattler().BattlerId;
				OutTarget.ActiveSlotId = Target.GetBattler().ActiveSlotId;
				return true;
			case EBattleResolvedTargetKind::Side:
				OutTarget.Side = Target.GetSide();
				OutTarget.bHasSide = true;
				return true;
			case EBattleResolvedTargetKind::Field:
				OutTarget.bField = true;
				return true;
			default:
				return false;
			}
		}

		void SetHp(const uint64 BattlerValue, const int32 Current, const int32 Maximum)
		{
			HpByBattler.Add(BattlerValue, {Current, Maximum});
		}

		[[nodiscard]] int32 GetHp(const uint64 BattlerValue) const
		{
			const FMockHp* Hp = HpByBattler.Find(BattlerValue);
			return Hp != nullptr ? Hp->Current : INDEX_NONE;
		}

		TMap<uint64, EBattleEffectExecutionOutcome> ReachabilityOutcomes;
		TMap<uint64, EBattleEffectExecutionOutcome> ProtectionOutcomes;
		TMap<uint64, EBattleEffectExecutionOutcome> TryHitOutcomes;
		TSet<uint64> TypeImmunityTargets;
		TMap<uint64, EBattleEffectExecutionOutcome> MoveImmunityOutcomes;
		TMap<uint64, EBattleEffectExecutionOutcome> AbilityImmunityOutcomes;
		TMap<uint64, EBattleEffectExecutionOutcome> ItemImmunityOutcomes;
		TMap<uint64, EBattleEffectExecutionOutcome> ProtectionBreakingOutcomes;
		TMap<int32, EBattleEffectExecutionOutcome> EligibilityOutcomes;
		TMap<int32, EBattleEffectExecutionOutcome> ApplicationOutcomes;
		TArray<FString> Calls;
		TArray<int32> AppliedEffectOrders;
		int32 AttackStage = 0;
		int32 ImmediateUpdateCount = 0;
		bool bSourceCanContinue = true;
		bool bPrevalidateRequest = true;
		TOptional<EBattleEffectExecutionOutcome> ForcedHpDeltaOutcome;

	private:
		[[nodiscard]] static uint64 TargetBattlerValue(const FBattleResolvedTarget& Target)
		{
			return Target.GetKind() == EBattleResolvedTargetKind::Battler
				? Target.GetBattler().BattlerId.GetValue()
				: 0;
		}

		[[nodiscard]] static FString GateCall(
			const TCHAR* Name,
			const FBattleResolvedTarget& Target)
		{
			return FString::Printf(TEXT("%s:%llu"), Name, TargetBattlerValue(Target));
		}

		[[nodiscard]] static FBattleEffectHookResult GateResult(
			const TMap<uint64, EBattleEffectExecutionOutcome>& Outcomes,
			const FBattleResolvedTarget& Target)
		{
			if (const EBattleEffectExecutionOutcome* Outcome = Outcomes.Find(TargetBattlerValue(Target)))
			{
				return MakeHookResult(*Outcome, TEXT("Rule.C05B.Gate"));
			}
			return MakeHookResult();
		}

		TMap<uint64, FMockHp> HpByBattler;
	};

	TArray<FBattleTypeChartEntry> MakeNeutralTypeChart()
	{
		TArray<FBattleTypeChartEntry> Entries;
		Entries.Reserve(FBattleTypeChart::EntryCount);
		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				Entries.Add(
					{
						static_cast<EPokemonType>(AttackingIndex),
						static_cast<EPokemonType>(DefendingIndex),
						1,
						1
					});
			}
		}
		return Entries;
	}

	FBattleDefinitionCatalogInput MakeCatalogInput(const FBattleMoveDefinition& Move)
	{
		FBattleDefinitionCatalogInput Input;
		Input.TypeChartEntries = MakeNeutralTypeChart();
		Input.Moves.Add(Move);
		Input.Abilities.Add({MakeDefinitionId<FAbilityId>(AbilityName)});
		Input.Items.Add({MakeDefinitionId<FItemId>(ItemName), EBattleItemKind::Held});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(MajorConditionName), EBattleConditionKind::MajorStatus});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(VolatileConditionName), EBattleConditionKind::Volatile});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(WeatherConditionName), EBattleConditionKind::Weather});
		Input.Conditions.Add(
			{MakeDefinitionId<FConditionId>(SideConditionName), EBattleConditionKind::SideCondition});

		FBattleSpeciesFormDefinition Species;
		Species.Id = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Species.PrimaryType = EPokemonType::Normal;
		Species.BaseStats = {80, 80, 80, 80, 80, 80};
		Species.CatchRate = 45;
		Species.AbilityChoices.Add(MakeDefinitionId<FAbilityId>(AbilityName));
		Input.SpeciesForms.Add(Species);
		return Input;
	}

	bool TryMakeCatalog(
		const FBattleMoveDefinition& Move,
		FBattleDefinitionCatalog& OutCatalog,
		TArray<FBattleCatalogDiagnostic>& OutDiagnostics)
	{
		return FBattleDefinitionCatalog::TryCreate(
			MakeCatalogInput(Move),
			OutCatalog,
			OutDiagnostics);
	}

	template <typename RowType>
	UDataTable* MakeTransientTable()
	{
		UDataTable* Table = NewObject<UDataTable>(GetTransientPackage());
		check(Table != nullptr);
		Table->RowStruct = RowType::StaticStruct();
		return Table;
	}

	FName PokemonTypeName(const int32 TypeIndex)
	{
		static const FName Names[] =
		{
			TEXT("Normal"), TEXT("Fire"), TEXT("Water"), TEXT("Electric"),
			TEXT("Grass"), TEXT("Ice"), TEXT("Fighting"), TEXT("Poison"),
			TEXT("Ground"), TEXT("Flying"), TEXT("Psychic"), TEXT("Bug"),
			TEXT("Rock"), TEXT("Ghost"), TEXT("Dragon"), TEXT("Dark"),
			TEXT("Steel"), TEXT("Fairy")
		};
		check(TypeIndex >= 0 && TypeIndex < UE_ARRAY_COUNT(Names));
		return Names[TypeIndex];
	}

	bool TryBuildRemoveConditionAdapterCatalog(
		FBattleDefinitionCatalog& OutCatalog,
		TArray<FBattleCatalogDiagnostic>& OutDiagnostics,
		const bool bIncludeTypelessDamageFlag = false)
	{
		UDataTable* SpeciesForms = MakeTransientTable<FBattleSpeciesFormTableRow>();
		UDataTable* Natures = MakeTransientTable<FBattleNatureTableRow>();
		UDataTable* Moves = MakeTransientTable<FBattleMoveTableRow>();
		UDataTable* Abilities = MakeTransientTable<FBattleAbilityTableRow>();
		UDataTable* Items = MakeTransientTable<FBattleItemTableRow>();
		UDataTable* Conditions = MakeTransientTable<FBattleConditionTableRow>();
		UDataTable* TypeChart = MakeTransientTable<FBattleTypeChartTableRow>();

		FBattleAbilityTableRow Ability;
		Abilities->AddRow(FName(AbilityName), Ability);

		FBattleSpeciesFormTableRow Species;
		Species.PrimaryType = FName(TEXT("Normal"));
		Species.BaseHP = 80;
		Species.BaseAttack = 80;
		Species.BaseDefense = 80;
		Species.BaseSpecialAttack = 80;
		Species.BaseSpecialDefense = 80;
		Species.BaseSpeed = 80;
		Species.CatchRate = 45;
		Species.AbilityIds.Add(FName(AbilityName));
		SpeciesForms->AddRow(FName(SpeciesName), Species);

		FBattleConditionTableRow Weather;
		Weather.Kind = FName(TEXT("Weather"));
		Conditions->AddRow(FName(WeatherConditionName), Weather);

		FBattleMoveTableRow ClearWeather;
		ClearWeather.Type = FName(TEXT("Normal"));
		ClearWeather.Category = FName(TEXT("Status"));
		ClearWeather.bAlwaysHits = true;
		ClearWeather.BasePP = 10;
		ClearWeather.Priority = 0;
		ClearWeather.TargetClass = FName(TEXT("Field"));
		if (bIncludeTypelessDamageFlag)
		{
			ClearWeather.Flags.Add(FName(TEXT("TypelessDamage")));
		}
		FBattleMoveEffectTableRow Removal;
		Removal.Kind = FName(TEXT("RemoveCondition"));
		Removal.Target = FName(TEXT("Field"));
		Removal.ConditionId = FName(WeatherConditionName);
		ClearWeather.Effects.Add(Removal);
		Moves->AddRow(FName(TEXT("Move.C05B.ClearWeather")), ClearWeather);

		for (int32 AttackingIndex = 0; AttackingIndex < FBattleTypeChart::TypeCount; ++AttackingIndex)
		{
			FBattleTypeChartTableRow Row;
			for (int32 DefendingIndex = 0; DefendingIndex < FBattleTypeChart::TypeCount; ++DefendingIndex)
			{
				FBattleTypeChartCellTableRow Cell;
				Cell.DefendingType = PokemonTypeName(DefendingIndex);
				Cell.Numerator = 1;
				Cell.Denominator = 1;
				Row.Entries.Add(Cell);
			}
			TypeChart->AddRow(PokemonTypeName(AttackingIndex), Row);
		}

		const FBattleDataTableSet Tables =
		{
			SpeciesForms,
			Natures,
			Moves,
			Abilities,
			Items,
			Conditions,
			TypeChart
		};
		return FBattleDataTableAdapter::BuildCatalog(Tables, OutCatalog, OutDiagnostics);
	}

	int32 CountExecutionEvents(
		const TConstArrayView<FBattleEffectExecutionEvent> Events,
		const EBattleEventType Type)
	{
		return static_cast<int32>(Algo::CountIf(
			Events,
			[Type](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == Type;
			}));
	}

	int32 CountResolutionEvents(
		const TConstArrayView<FBattleResolution> Resolutions,
		const EBattleEventType Type)
	{
		int32 Count = 0;
		for (const FBattleResolution& Resolution : Resolutions)
		{
			Count += static_cast<int32>(Algo::CountIf(
				Resolution.GetEvents(),
				[Type](const FBattleEvent& Event)
				{
					return Event.GetType() == Type;
				}));
		}
		return Count;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BValidationTest,
		"PokemonSolarus.Battle.C05B.Validation.DescriptorsChanceRemovalMultiHit",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BValidationTest::RunTest(const FString& Parameters)
	{
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;

		FBattleMoveDefinition PrimaryMove = MakeDamagingMove();
		TestTrue(
			TEXT("A 1/1 primary descriptor validates"),
			TryMakeCatalog(PrimaryMove, Catalog, Diagnostics));
		TestTrue(TEXT("The valid primary descriptor has no diagnostics"), Diagnostics.IsEmpty());

		FBattleMoveDefinition IndependentMove = MakeDamagingMove();
		FBattleMoveEffectDescriptor Independent = MakeEffect(
			1,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		Independent.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		Independent.ChanceNumerator = 100;
		Independent.ChanceDenominator = 100;
		IndependentMove.Effects.Add(Independent);
		Diagnostics.Reset();
		TestTrue(
			TEXT("An explicit independent 100/100 descriptor validates"),
			TryMakeCatalog(IndependentMove, Catalog, Diagnostics));

		FBattleMoveDefinition SharedChanceShape = IndependentMove;
		SharedChanceShape.Effects[1].ChanceNumerator = 1;
		SharedChanceShape.Effects[1].ChanceDenominator = 2;
		Diagnostics.Reset();
		TestFalse(
			TEXT("An unfrozen non-percentage chance shape is rejected"),
			TryMakeCatalog(SharedChanceShape, Catalog, Diagnostics));
		TestTrue(
			TEXT("The rejected chance produces an InvalidRange diagnostic"),
			Diagnostics.ContainsByPredicate(
				[](const FBattleCatalogDiagnostic& Diagnostic)
				{
					return Diagnostic.Code == EBattleCatalogDiagnosticCode::InvalidRange
						&& Diagnostic.Field == FName(TEXT("Effects.Chance"));
				}));

		FBattleMoveDefinition SecondaryDamage = MakeDamagingMove();
		SecondaryDamage.Effects[0].ChanceNumerator = 100;
		SecondaryDamage.Effects[0].ChanceDenominator = 100;
		Diagnostics.Reset();
		TestFalse(
			TEXT("Damage cannot be encoded as a secondary descriptor"),
			TryMakeCatalog(SecondaryDamage, Catalog, Diagnostics));

		FBattleMoveDefinition UserDamage = MakeDamagingMove(
			EBattleTargetClass::Self,
			EBattleEffectTarget::User);
		Diagnostics.Reset();
		TestFalse(
			TEXT("Direct Damage cannot target User"),
			TryMakeCatalog(UserDamage, Catalog, Diagnostics));

		FBattleMoveDefinition FieldDamage = MakeDamagingMove();
		FieldDamage.TargetClass = EBattleTargetClass::Field;
		Diagnostics.Reset();
		TestFalse(
			TEXT("A damaging move cannot use a field target class"),
			TryMakeCatalog(FieldDamage, Catalog, Diagnostics));

		for (const EBattleMoveEffectKind Kind :
			TArray<EBattleMoveEffectKind>{EBattleMoveEffectKind::Drain, EBattleMoveEffectKind::Recoil})
		{
			FBattleMoveDefinition BadLinkedTarget = MakeDamagingMove();
			FBattleMoveEffectDescriptor Linked = MakeEffect(
				1,
				Kind,
				EBattleEffectTarget::ResolvedTarget);
			Linked.MagnitudeNumerator = 1;
			Linked.MagnitudeDenominator = 2;
			BadLinkedTarget.Effects.Add(Linked);
			Diagnostics.Reset();
			TestFalse(
				TEXT("Drain and recoil must route to User"),
				TryMakeCatalog(BadLinkedTarget, Catalog, Diagnostics));
		}

		FBattleMoveDefinition PerHitDamage = MakeDamagingMove();
		PerHitDamage.Effects[0].Flags = EBattleMoveEffectFlags::PerHit;
		Diagnostics.Reset();
		TestFalse(
			TEXT("Damage cannot carry the PerHit secondary flag"),
			TryMakeCatalog(PerHitDamage, Catalog, Diagnostics));

		FBattleMoveDefinition PrimaryPerHit = MakeDamagingMove();
		FBattleMoveEffectDescriptor PrimaryPerHitCondition = MakeEffect(
			1,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		PrimaryPerHitCondition.ConditionId = MakeDefinitionId<FConditionId>(VolatileConditionName);
		PrimaryPerHitCondition.Flags = EBattleMoveEffectFlags::PerHit;
		PrimaryPerHit.Effects.Add(PrimaryPerHitCondition);
		Diagnostics.Reset();
		TestFalse(
			TEXT("A PerHit effect must be an independent secondary"),
			TryMakeCatalog(PrimaryPerHit, Catalog, Diagnostics));

		FBattleMoveEffectDescriptor RemoveMajor = MakeEffect(
			0,
			EBattleMoveEffectKind::RemoveCondition,
			EBattleEffectTarget::ResolvedTarget);
		RemoveMajor.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		FBattleMoveDefinition RemoveMajorMove = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{RemoveMajor});
		Diagnostics.Reset();
		TestTrue(
			TEXT("Removing a battler-family condition from a battler target validates"),
			TryMakeCatalog(RemoveMajorMove, Catalog, Diagnostics));

		FBattleMoveEffectDescriptor RemoveWeather = RemoveMajor;
		RemoveWeather.ConditionId = MakeDefinitionId<FConditionId>(WeatherConditionName);
		FBattleMoveDefinition BadRemovalMove = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{RemoveWeather});
		Diagnostics.Reset();
		TestFalse(
			TEXT("Removing a field-family condition from a battler target is rejected"),
			TryMakeCatalog(BadRemovalMove, Catalog, Diagnostics));
		TestTrue(
			TEXT("The removal-family mismatch names Effects.Target"),
			Diagnostics.ContainsByPredicate(
				[](const FBattleCatalogDiagnostic& Diagnostic)
				{
					return Diagnostic.Code == EBattleCatalogDiagnosticCode::IncompatibleEffect
						&& Diagnostic.Field == FName(TEXT("Effects.Target"));
				}));

		auto MakeMultiHitMove = [](const int32 Minimum, const int32 Maximum)
		{
			FBattleMoveDefinition Move = MakeDamagingMove();
			Move.Effects[0].Order = 1;
			FBattleMoveEffectDescriptor MultiHit = MakeEffect(
				0,
				EBattleMoveEffectKind::MultiHit,
				EBattleEffectTarget::ResolvedTarget);
			MultiHit.MinimumCount = Minimum;
			MultiHit.MaximumCount = Maximum;
			Move.Effects.Insert(MultiHit, 0);
			return Move;
		};

		for (const TPair<int32, int32>& Count : TArray<TPair<int32, int32>>{{2, 2}, {5, 5}, {2, 5}})
		{
			Diagnostics.Reset();
			TestTrue(
				TEXT("Each approved fixed/ranged multi-hit boundary validates"),
				TryMakeCatalog(MakeMultiHitMove(Count.Key, Count.Value), Catalog, Diagnostics));
		}

		Diagnostics.Reset();
		TestFalse(
			TEXT("An unfrozen 3..5 multi-hit range is rejected"),
			TryMakeCatalog(MakeMultiHitMove(3, 5), Catalog, Diagnostics));

		FBattleMoveDefinition SecondaryMultiHit = MakeMultiHitMove(2, 5);
		SecondaryMultiHit.Effects[0].ChanceNumerator = 100;
		SecondaryMultiHit.Effects[0].ChanceDenominator = 100;
		Diagnostics.Reset();
		TestFalse(
			TEXT("A secondary multi-hit descriptor is rejected"),
			TryMakeCatalog(SecondaryMultiHit, Catalog, Diagnostics));

		FBattleMoveDefinition MultiHitAfterDamage = MakeMultiHitMove(2, 2);
		MultiHitAfterDamage.Effects[0].Order = 2;
		MultiHitAfterDamage.Effects[1].Order = 1;
		MultiHitAfterDamage.Effects.Swap(0, 1);
		Diagnostics.Reset();
		TestFalse(
			TEXT("A multi-hit descriptor after damage is rejected"),
			TryMakeCatalog(MultiHitAfterDamage, Catalog, Diagnostics));

		FBattleMoveDefinition SpreadMultiHit = MakeMultiHitMove(2, 5);
		SpreadMultiHit.TargetClass = EBattleTargetClass::FixedSpreadSet;
		SpreadMultiHit.Effects[0].Target = EBattleEffectTarget::AllResolvedTargets;
		SpreadMultiHit.Effects[1].Target = EBattleEffectTarget::AllResolvedTargets;
		Diagnostics.Reset();
		TestFalse(
			TEXT("Spread plus multi-hit is rejected until combined count semantics are frozen"),
			TryMakeCatalog(SpreadMultiHit, Catalog, Diagnostics));

		FBattleMoveEffectDescriptor OversizedPercentageHeal = MakeEffect(
			0,
			EBattleMoveEffectKind::Heal,
			EBattleEffectTarget::User);
		OversizedPercentageHeal.ChanceNumerator = 100;
		OversizedPercentageHeal.ChanceDenominator = 100;
		OversizedPercentageHeal.MagnitudeNumerator = 3;
		OversizedPercentageHeal.MagnitudeDenominator = 2;
		const FBattleMoveDefinition OversizedPercentageHealMove = MakeStatusMove(
			EBattleTargetClass::Self,
			{OversizedPercentageHeal});
		Diagnostics.Reset();
		TestFalse(
			TEXT("A percentage magnitude greater than its whole is rejected before execution"),
			TryMakeCatalog(OversizedPercentageHealMove, Catalog, Diagnostics));
		TestTrue(
			TEXT("The oversized percentage names Effects.Magnitude"),
			Diagnostics.ContainsByPredicate(
				[](const FBattleCatalogDiagnostic& Diagnostic)
				{
					return Diagnostic.Code == EBattleCatalogDiagnosticCode::InvalidRange
						&& Diagnostic.Field == FName(TEXT("Effects.Magnitude"));
				}));

		FBattleMoveDefinition AuthoredTypelessMove = MakeDamagingMove();
		AuthoredTypelessMove.Flags |= EBattleMoveFlags::TypelessDamage;
		Diagnostics.Reset();
		TestFalse(
			TEXT("An authored move cannot opt into engine-owned typeless damage"),
			TryMakeCatalog(AuthoredTypelessMove, Catalog, Diagnostics));
		TestTrue(
			TEXT("The authored typeless flag is reported as incompatible"),
			Diagnostics.ContainsByPredicate(
				[](const FBattleCatalogDiagnostic& Diagnostic)
				{
					return Diagnostic.Code == EBattleCatalogDiagnosticCode::IncompatibleEffect
						&& Diagnostic.Field == FName(TEXT("Flags"));
				}));

		Diagnostics.Reset();
		TestTrue(
			TEXT("The Unreal Data Table adapter recognizes RemoveCondition"),
			TryBuildRemoveConditionAdapterCatalog(Catalog, Diagnostics));
		const FBattleMoveDefinition* AdaptedRemoval = Catalog.FindMove(
			MakeDefinitionId<FMoveId>(TEXT("Move.C05B.ClearWeather")));
		TestNotNull(TEXT("The adapted removal move exists"), AdaptedRemoval);
		if (AdaptedRemoval != nullptr)
		{
			TestEqual(TEXT("The adapter preserves one removal descriptor"), AdaptedRemoval->Effects.Num(), 1);
			if (AdaptedRemoval->Effects.Num() == 1)
			{
				TestEqual(
					TEXT("RemoveCondition maps to its appended effect kind"),
					AdaptedRemoval->Effects[0].Kind,
					EBattleMoveEffectKind::RemoveCondition);
			}
		}
		Diagnostics.Reset();
		TestFalse(
			TEXT("The Unreal Data Table adapter rejects the reserved TypelessDamage flag"),
			TryBuildRemoveConditionAdapterCatalog(Catalog, Diagnostics, true));
		TestTrue(
			TEXT("The reserved authored flag is an invalid Flags value"),
			Diagnostics.ContainsByPredicate(
				[](const FBattleCatalogDiagnostic& Diagnostic)
				{
					return Diagnostic.Code == EBattleCatalogDiagnosticCode::InvalidAuthoredValue
						&& Diagnostic.Field == FName(TEXT("Flags"));
				}));

		auto AssertDirectInvalidDefinitionNoDraw = [this](
			const TCHAR* Shape,
			const FBattleMoveDefinition& InvalidMove,
			TArray<FBattleResolvedTarget> Targets,
			const uint64 ResolutionValue)
		{
			FMockExecutionContext Context;
			FStrictScriptedRandom Random({});
			FBattleEffectExecutionResult Result;
			EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
			const bool bExecuted = FBattleEffectExecutor::TryExecute(
				MakeRequest(InvalidMove, MoveTemp(Targets), ResolutionValue),
				Context,
				Random,
				Result,
				Error);
			const FString Prefix = FString::Printf(TEXT("Direct validation of %s"), Shape);
			TestFalse(Prefix + TEXT(" rejects the complete request"), bExecuted);
			TestEqual(
				Prefix + TEXT(" reports InvalidMoveDefinition"),
				Error,
				EBattleEffectExecutorError::InvalidMoveDefinition);
			TestTrue(Prefix + TEXT(" consumes no RNG"), Random.IsExact());
			TestTrue(Prefix + TEXT(" reaches no dynamic hook"), Context.Calls.IsEmpty());
			TestFalse(Prefix + TEXT(" leaves no valid result"), Result.bValid);
			TestTrue(Prefix + TEXT(" emits no event"), Result.Events.IsEmpty());
			TestTrue(
				Prefix + TEXT(" emits no completed-hit metadata"),
				Result.CompletedHitsPerDamageTarget.IsEmpty());
		};

		AssertDirectInvalidDefinitionNoDraw(
			TEXT("a 100/100 Heal percentage greater than one whole"),
			OversizedPercentageHealMove,
			{MakeBattlerTarget(
				EBattleSide::Player,
				EBattlePosition::Left,
				PlayerLeftBattlerValue)},
			98);
		AssertDirectInvalidDefinitionNoDraw(
			TEXT("an authored known-type move carrying TypelessDamage"),
			AuthoredTypelessMove,
			{MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue)},
			99);

		FBattleMoveEffectDescriptor FieldSecondaryHeal = MakeEffect(
			0,
			EBattleMoveEffectKind::Heal,
			EBattleEffectTarget::Field);
		FieldSecondaryHeal.ChanceNumerator = 100;
		FieldSecondaryHeal.ChanceDenominator = 100;
		FieldSecondaryHeal.MagnitudeNumerator = 1;
		FieldSecondaryHeal.MagnitudeDenominator = 2;
		const FBattleMoveDefinition FieldSecondaryHealMove = MakeStatusMove(
			EBattleTargetClass::Field,
			{FieldSecondaryHeal});
		AssertDirectInvalidDefinitionNoDraw(
			TEXT("a 100/100 Heal targeting Field"),
			FieldSecondaryHealMove,
			{MakeFieldTarget()},
			100);

		FBattleMoveEffectDescriptor BadConditionTarget = MakeEffect(
			0,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::Field);
		BadConditionTarget.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		const FBattleMoveDefinition BadConditionTargetMove = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{BadConditionTarget});
		AssertDirectInvalidDefinitionNoDraw(
			TEXT("an ApplyCondition field target on a battler-targeted move"),
			BadConditionTargetMove,
			{MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue)},
			101);

		FBattleMoveEffectDescriptor ZeroMagnitudeHeal = MakeEffect(
			0,
			EBattleMoveEffectKind::Heal,
			EBattleEffectTarget::User);
		ZeroMagnitudeHeal.MagnitudeNumerator = 0;
		const FBattleMoveDefinition ZeroMagnitudeHealMove = MakeStatusMove(
			EBattleTargetClass::Self,
			{ZeroMagnitudeHeal});
		AssertDirectInvalidDefinitionNoDraw(
			TEXT("a zero-magnitude Heal payload"),
			ZeroMagnitudeHealMove,
			{MakeBattlerTarget(
				EBattleSide::Player,
				EBattlePosition::Left,
				PlayerLeftBattlerValue)},
			102);

		const FBattleMoveEffectDescriptor MissingConditionReference = MakeEffect(
			0,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		const FBattleMoveDefinition MissingConditionReferenceMove = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{MissingConditionReference});
		AssertDirectInvalidDefinitionNoDraw(
			TEXT("an ApplyCondition descriptor with no ConditionId"),
			MissingConditionReferenceMove,
			{MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue)},
			103);

		const FBattleMoveDefinition PreflightMove = MakeDamagingMove();
		FMockExecutionContext PreflightContext;
		PreflightContext.bPrevalidateRequest = false;
		FStrictScriptedRandom PreflightRandom({});
		FBattleEffectExecutionResult PreflightResult;
		EBattleEffectExecutorError PreflightError = EBattleEffectExecutorError::None;
		TestFalse(
			TEXT("Context-owned preflight rejection fails the complete request"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(
					PreflightMove,
					{MakeBattlerTarget(
						EBattleSide::Opponent,
						EBattlePosition::Left,
						OpponentLeftBattlerValue)},
					99),
				PreflightContext,
				PreflightRandom,
				PreflightResult,
				PreflightError));
		TestEqual(
			TEXT("Preflight rejection is typed as an invalid request"),
			PreflightError,
			EBattleEffectExecutorError::InvalidRequest);
		TestTrue(TEXT("Preflight rejection consumes no RNG"), PreflightRandom.IsExact());
		TestTrue(TEXT("Preflight rejection reaches no gate"), PreflightContext.Calls.IsEmpty());
		TestTrue(TEXT("Preflight rejection applies no descriptor"), PreflightContext.AppliedEffectOrders.IsEmpty());
		TestEqual(
			TEXT("Preflight rejection mutates no HP"),
			PreflightContext.GetHp(OpponentLeftBattlerValue),
			100);
		TestTrue(TEXT("Preflight rejection emits no effect event"), PreflightResult.Events.IsEmpty());

		const FBattleMoveDefinition PreventedDamageMove = MakeDamagingMove();
		FMockExecutionContext PreventedDamageContext;
		PreventedDamageContext.ForcedHpDeltaOutcome = EBattleEffectExecutionOutcome::Prevented;
		FStrictScriptedRandom PreventedDamageRandom(
			{{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}});
		FBattleEffectExecutionResult PreventedDamageResult;
		EBattleEffectExecutorError PreventedDamageError = EBattleEffectExecutorError::None;
		TestFalse(
			TEXT("Direct damage rejects a Prevented ApplyHpDelta hook result"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(
					PreventedDamageMove,
					{MakeBattlerTarget(
						EBattleSide::Opponent,
						EBattlePosition::Left,
						OpponentLeftBattlerValue)},
					104),
				PreventedDamageContext,
				PreventedDamageRandom,
				PreventedDamageResult,
				PreventedDamageError));
		TestEqual(
			TEXT("A non-applied direct damage mutation result is an invalid hook result"),
			PreventedDamageError,
			EBattleEffectExecutorError::InvalidHookResult);
		TestTrue(
			TEXT("The invalid direct damage hook is reached after its exact damage draw"),
			PreventedDamageRandom.IsExact());
		TestFalse(TEXT("The failed direct damage result is not valid"), PreventedDamageResult.bValid);
		TestEqual(
			TEXT("The failed direct damage hook leaves mock HP unchanged"),
			PreventedDamageContext.GetHp(OpponentLeftBattlerValue),
			100);
		TestEqual(
			TEXT("The failed direct damage hook emits no damage or HP mutation event"),
			CountExecutionEvents(PreventedDamageResult.Events, EBattleEventType::Damage)
				+ CountExecutionEvents(PreventedDamageResult.Events, EBattleEventType::HPChanged),
			0);
		TestEqual(
			TEXT("The failed direct damage hook runs no immediate update"),
			PreventedDamageContext.ImmediateUpdateCount,
			0);
		TestTrue(
			TEXT("The failed direct damage hook publishes no completed-hit count"),
			PreventedDamageResult.CompletedHitsPerDamageTarget.IsEmpty());
		TestFalse(
			TEXT("The failed direct damage hook publishes no final hit-count metadata"),
			PreventedDamageResult.Events.ContainsByPredicate(
				[](const FBattleEffectExecutionEvent& Event)
				{
					return Event.HitCount.IsSet();
				}));

		return true;
	}

	FBattleTrainerSetup MakeTrainer(
		const uint64 TrainerValue,
		const EBattleSide Side,
		const EBattleTrainerRole Role,
		const EBattleDecisionController Controller)
	{
		FBattleTrainerSetup Trainer;
		Trainer.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Trainer.Side = Side;
		Trainer.Role = Role;
		Trainer.Controller = Controller;
		Trainer.SelectorProfileId = MakeDefinitionId<FDefinitionId>(
			Role == EBattleTrainerRole::Player
				? TEXT("Selector.C05B.Player")
				: TEXT("Selector.C05B.Opponent"));
		return Trainer;
	}

	FBattlePartyEntrySetup MakePartyEntry(
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const int32 PartyIndex,
		const int32 Speed,
		const int32 CurrentPP,
		const int32 CurrentHP = 200)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		Entry.BattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		Entry.SourcePokemonId = MakeNumericId<FSourcePokemonId>(1000 + BattlerValue);
		Entry.PartySlotId = MakePartySlotId(PartyIndex);
		Entry.SpeciesFormId = MakeDefinitionId<FSpeciesFormId>(SpeciesName);
		Entry.Level = 50;
		Entry.Stats = {200, 100, 100, 100, 100, Speed};
		Entry.CurrentHP = CurrentHP;
		Entry.AbilityId = MakeDefinitionId<FAbilityId>(AbilityName);
		Entry.Moves.Add(
			{
				0,
				MakeDefinitionId<FMoveId>(MoveName),
				CurrentPP,
				10
			});
		return Entry;
	}

	FBattleSetup MakeSingleSetup(const int32 CurrentPP = 3)
	{
		FBattleSetupInput Input;
		Input.BattleId = MakeNumericId<FBattleId>(BattleValue);
		Input.SettingsReference = {MakeDefinitionId<FDefinitionId>(TEXT("Settings.C05B")), 1};
		Input.CatalogReference = {MakeDefinitionId<FDefinitionId>(TEXT("Catalog.C05B")), 1};
		Input.EncounterKind = EBattleEncounterKind::Trainer;
		Input.Format = EBattleFormat::Single;
		Input.CaptureCapacity = {2, 100};
		Input.Policies.bBagAllowed = false;
		Input.Trainers.Add(MakeTrainer(
			PlayerTrainerValue,
			EBattleSide::Player,
			EBattleTrainerRole::Player,
			EBattleDecisionController::Human));
		Input.Trainers.Add(MakeTrainer(
			OpponentTrainerValue,
			EBattleSide::Opponent,
			EBattleTrainerRole::Opponent,
			EBattleDecisionController::EnemyAI));
		Input.PartyEntries.Add(MakePartyEntry(
			PlayerTrainerValue,
			PlayerLeftBattlerValue,
			0,
			200,
			CurrentPP));
		Input.PartyEntries.Add(MakePartyEntry(
			OpponentTrainerValue,
			OpponentLeftBattlerValue,
			0,
			100,
			CurrentPP));
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Player, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(PlayerTrainerValue),
				MakeNumericId<FBattlerId>(PlayerLeftBattlerValue)
			});
		Input.StartingActive.Add(
			{
				MakeActiveSlotId(EBattleSide::Opponent, EBattlePosition::Left),
				MakeNumericId<FTrainerId>(OpponentTrainerValue),
				MakeNumericId<FBattlerId>(OpponentLeftBattlerValue)
			});
		Input.ObedienceInputs.Add(
			{
				MakeNumericId<FBattlerId>(PlayerLeftBattlerValue),
				true,
				20,
				8
			});

		FBattleSetup Setup;
		EBattleSetupValidationError Error = EBattleSetupValidationError::None;
		const bool bCreated = FBattleSetup::TryCreate(Input, Setup, Error);
		check(bCreated);
		return Setup;
	}

	TUniquePtr<FBattleEngine> MakeEngine(
		const FBattleMoveDefinition& Move,
		TUniquePtr<IBattleRandom>&& Random,
		const int32 CurrentPP = 3)
	{
		FBattleDefinitionCatalog Catalog;
		TArray<FBattleCatalogDiagnostic> Diagnostics;
		const bool bCatalogCreated = TryMakeCatalog(Move, Catalog, Diagnostics);
		check(bCatalogCreated);

		TUniquePtr<FBattleEngine> Engine;
		FBattleRejection Rejection;
		const bool bEngineCreated = FBattleEngine::TryCreate(
			MakeSingleSetup(CurrentPP),
			Catalog,
			MoveTemp(Random),
			Engine,
			Rejection);
		check(bEngineCreated);
		return Engine;
	}

	bool LockAllFights(FBattleEngine& Engine)
	{
		FBattleRejection Rejection;
		if (Engine.GetSnapshot().GetPhase() == EBattlePhase::Setup
			&& !Engine.TryBeginActionDecisionSequence(Rejection))
		{
			return false;
		}

		int32 Guard = 0;
		while (Engine.GetPendingDecision().IsSet() && Guard++ < 4)
		{
			const FBattleDecisionRequest Request = Engine.GetPendingDecision().GetValue();
			FBattleDecision Decision;
			const FMoveId MoveId = MakeDefinitionId<FMoveId>(MoveName);
			bool bCreated = false;
			if (Request.GetAutomaticallyTargetedMoveIds().Contains(MoveId))
			{
				bCreated = FBattleDecision::TryCreateAutomaticallyTargetedFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MoveId,
					Decision);
			}
			else
			{
				const FBattleMoveTargetOption* Target = Request.GetLegalMoveTargets().FindByPredicate(
					[MoveId](const FBattleMoveTargetOption& Option)
					{
						return Option.MoveId == MoveId;
					});
				bCreated = Target != nullptr && FBattleDecision::TryCreateFight(
					Request.GetStateVersion(),
					Request.GetDecisionOwnerTrainerId(),
					Request.GetActingBattlerId(),
					MoveId,
					Target->ActiveSlotId,
					Decision);
			}
			if (!bCreated || !Engine.SubmitDecision(Decision).WasAccepted())
			{
				return false;
			}
		}

		return Guard < 4 && Engine.GetSnapshot().GetPhase() == EBattlePhase::Locked;
	}

	bool PrepareFirstMove(FBattleEngine& Engine)
	{
		return LockAllFights(Engine)
			&& Engine.BeginNextLockedAction().WasAccepted()
			&& Engine.CommitCurrentMoveAfterPreMoveGates().WasAccepted()
			&& Engine.ResolveCurrentMoveTargets().WasAccepted();
	}

	const FBattleMoveSlotSetup* FindMoveSlot(
		const FBattleSnapshot& Snapshot,
		const uint64 BattlerValue)
	{
		const FBattlePartyEntrySetup* Battler = Snapshot.FindBattler(
			MakeNumericId<FBattlerId>(BattlerValue));
		return Battler != nullptr && !Battler->Moves.IsEmpty() ? &Battler->Moves[0] : nullptr;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BEnginePipelineTest,
		"PokemonSolarus.Battle.C05B.Integration.EnginePipelineAndSingleExecution",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05BEnginePipelineTest::RunTest(const FString& Parameters)
	{
		FBattleMoveDefinition Move = MakeDamagingMove();
		TUniquePtr<FStrictScriptedRandom> Random = MakeUnique<FStrictScriptedRandom>(
			TArray<FExpectedDraw>{{
				0,
				15,
				0,
				FBattleEffectExecutor::GetDamageRandomRulePurpose()
			}});
		FStrictScriptedRandom* RandomView = Random.Get();
		TUniquePtr<IBattleRandom> RandomOwner = MoveTemp(Random);
		TUniquePtr<FBattleEngine> Engine = MakeEngine(Move, MoveTemp(RandomOwner));

		const FBattleSnapshot InitialSnapshot = Engine->GetSnapshot();
		const FBattleResolution BeforeLock = Engine->ExecuteCurrentMoveEffects();
		TestFalse(TEXT("Effect execution is rejected before action lock"), BeforeLock.WasAccepted());
		TestEqual(TEXT("Premature execution consumes no RNG"), RandomView->GetTrace().Num(), 0);
		TestEqual(
			TEXT("Premature execution leaves target HP unchanged"),
			Engine->GetSnapshot().FindBattler(MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP,
			InitialSnapshot.FindBattler(MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP);

		TestTrue(TEXT("The two Singles Fight choices lock"), LockAllFights(*Engine));
		const FBattleResolution BeforeStart = Engine->ExecuteCurrentMoveEffects();
		TestFalse(TEXT("Effect execution is rejected before action start"), BeforeStart.WasAccepted());
		TestTrue(TEXT("The first locked action starts"), Engine->BeginNextLockedAction().WasAccepted());
		const FBattleResolution BeforeCommit = Engine->ExecuteCurrentMoveEffects();
		TestFalse(TEXT("Effect execution is rejected before move commitment"), BeforeCommit.WasAccepted());

		const FBattleResolution Committed = Engine->CommitCurrentMoveAfterPreMoveGates();
		TestTrue(TEXT("The move commits once"), Committed.WasAccepted());
		const FBattleMoveSlotSetup* AfterCommitSlot = FindMoveSlot(
			Engine->GetSnapshot(),
			PlayerLeftBattlerValue);
		TestNotNull(TEXT("The committed move slot remains visible"), AfterCommitSlot);
		if (AfterCommitSlot != nullptr)
		{
			TestEqual(TEXT("PP is consumed exactly at commitment"), AfterCommitSlot->CurrentPP, 2);
		}
		TestFalse(
			TEXT("A duplicate move commitment is rejected"),
			Engine->CommitCurrentMoveAfterPreMoveGates().WasAccepted());
		const FBattleMoveSlotSetup* AfterDuplicateCommit = FindMoveSlot(
			Engine->GetSnapshot(),
			PlayerLeftBattlerValue);
		if (AfterDuplicateCommit != nullptr)
		{
			TestEqual(TEXT("Duplicate commitment consumes no second PP"), AfterDuplicateCommit->CurrentPP, 2);
		}

		const FBattleResolution BeforeTargeting = Engine->ExecuteCurrentMoveEffects();
		TestFalse(TEXT("Effect execution is rejected before target resolution"), BeforeTargeting.WasAccepted());
		TestTrue(TEXT("C04B target resolution succeeds"), Engine->ResolveCurrentMoveTargets().WasAccepted());
		TestTrue(
			TEXT("The fixture keeps the production-adapter target above the faint threshold"),
			FBattleC05BEngineFixture::SetBattlerHpForExecution(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftBattlerValue),
				200));

		TestTrue(
			TEXT("The fixture can expose the synchronous re-entrant guard"),
			FBattleC05BEngineFixture::SetCurrentEffectExecutionState(
				*Engine,
				EBattleLockedEffectExecutionState::Executing));
		const int32 HPBeforeReentrant = Engine->GetSnapshot().FindBattler(
			MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP;
		const FBattleResolution Reentrant = Engine->ExecuteCurrentMoveEffects();
		TestFalse(TEXT("Re-entrant effect execution is rejected"), Reentrant.WasAccepted());
		TestEqual(TEXT("Re-entrant rejection consumes no RNG"), RandomView->GetTrace().Num(), 0);
		TestEqual(
			TEXT("Re-entrant rejection mutates no HP"),
			Engine->GetSnapshot().FindBattler(MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP,
			HPBeforeReentrant);
		TestTrue(
			TEXT("The fixture restores Pending for the real synchronous call"),
			FBattleC05BEngineFixture::SetCurrentEffectExecutionState(
				*Engine,
				EBattleLockedEffectExecutionState::Pending));

		const int32 ActionIndexBefore = FBattleC05BEngineFixture::GetCurrentLockedActionIndex(*Engine);
		const FBattleResolution Executed = Engine->ExecuteCurrentMoveEffects();
		TestTrue(TEXT("The committed targeted action executes"), Executed.WasAccepted());
		TestEqual(
			TEXT("Successful execution advances the locked action index once"),
			FBattleC05BEngineFixture::GetCurrentLockedActionIndex(*Engine),
			ActionIndexBefore + 1);
		TestTrue(TEXT("The one expected effect RNG draw was consumed"), RandomView->IsExact());
		if (!RandomView->GetMismatch().IsEmpty())
		{
			AddError(RandomView->GetMismatch());
		}

		const FBattleSnapshot AfterExecution = Engine->GetSnapshot();
		const int32 HPAfterExecution = AfterExecution.FindBattler(
			MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP;
		TestTrue(TEXT("The target lost HP"), HPAfterExecution < HPBeforeReentrant);
		bool bFainted = false;
		bool bFaintTransitionPending = false;
		TestTrue(
			TEXT("The fixture can inspect production faint-transition facts"),
			FBattleC05BEngineFixture::GetFaintTransitionFacts(
				*Engine,
				MakeNumericId<FBattlerId>(OpponentLeftBattlerValue),
				bFainted,
				bFaintTransitionPending));
		TestFalse(TEXT("The surviving C05B target is not fainted"), bFainted);
		TestFalse(TEXT("The surviving C05B target has no pending C05C transition"), bFaintTransitionPending);
		TestFalse(
			TEXT("The public snapshot keeps the surviving target active"),
			AfterExecution.FindObservedBattler(
				MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->bFainted);
		TestEqual(
			TEXT("C05B emits no Fainted event"),
			static_cast<int32>(Algo::CountIf(
				Executed.GetEvents(),
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::Fainted;
				})),
			0);
		TestEqual(
			TEXT("C05B emits no removal or replacement event"),
			static_cast<int32>(Algo::CountIf(
				Executed.GetEvents(),
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::Removed
						|| Event.GetType() == EBattleEventType::ReplacementRequired
						|| Event.GetType() == EBattleEventType::OpponentRemovalCheckpoint
						|| Event.GetType() == EBattleEventType::BattleEnded;
				})),
			0);
		const FBattleMoveSlotSetup* AfterExecutionSlot = FindMoveSlot(
			AfterExecution,
			PlayerLeftBattlerValue);
		if (AfterExecutionSlot != nullptr)
		{
			TestEqual(TEXT("Effect execution does not consume PP again"), AfterExecutionSlot->CurrentPP, 2);
		}

		const FBattleResolution Duplicate = Engine->ExecuteCurrentMoveEffects();
		TestFalse(TEXT("A completed action cannot execute twice"), Duplicate.WasAccepted());
		TestEqual(
			TEXT("Duplicate execution cannot advance the action index"),
			FBattleC05BEngineFixture::GetCurrentLockedActionIndex(*Engine),
			ActionIndexBefore + 1);
		TestEqual(
			TEXT("Duplicate execution cannot apply HP again"),
			Engine->GetSnapshot().FindBattler(MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP,
			HPAfterExecution);
		TestEqual(TEXT("Duplicate execution consumes no RNG"), RandomView->GetTrace().Num(), 1);

		const FBattleReplayRecord Record = Engine->ExportReplayRecord();
		TestEqual(
			TEXT("Exactly one PP-consumption event exists"),
			CountResolutionEvents(Record.GetResolutions(), EBattleEventType::PPConsumed),
			1);
		TestEqual(
			TEXT("Exactly one damage event exists"),
			CountResolutionEvents(Record.GetResolutions(), EBattleEventType::Damage),
			1);
		TestEqual(
			TEXT("Exactly one action-completion event came from C05B"),
			CountResolutionEvents(Record.GetResolutions(), EBattleEventType::ActionCompleted),
			1);
		TestEqual(
			TEXT("The replay contains no C05C faint event"),
			CountResolutionEvents(Record.GetResolutions(), EBattleEventType::Fainted),
			0);
		TestEqual(
			TEXT("The replay contains no C05C removal event"),
			CountResolutionEvents(Record.GetResolutions(), EBattleEventType::Removed),
			0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BGateOrderTest,
		"PokemonSolarus.Battle.C05B.Order.ReachabilityProtectImmunityAccuracySpread",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BGateOrderTest::RunTest(const FString& Parameters)
	{
		const TArray<FBattleResolvedTarget> SpreadTargets =
		{
			MakeBattlerTarget(
				EBattleSide::Player,
				EBattlePosition::Right,
				PlayerRightBattlerValue),
			MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue),
			MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentRightBattlerValue)
		};
		FBattleMoveDefinition Move = MakeDamagingMove(
			EBattleTargetClass::FixedSpreadSet,
			EBattleEffectTarget::AllResolvedTargets);

		FMockExecutionContext TerminalContext;
		TerminalContext.ReachabilityOutcomes.Add(
			PlayerRightBattlerValue,
			EBattleEffectExecutionOutcome::Unreachable);
		TerminalContext.ProtectionOutcomes.Add(
			OpponentLeftBattlerValue,
			EBattleEffectExecutionOutcome::Protected);
		TerminalContext.TypeImmunityTargets.Add(OpponentRightBattlerValue);
		FStrictScriptedRandom NoRandom({});
		FBattleEffectExecutionResult TerminalResult;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		const bool bTerminalExecuted = FBattleEffectExecutor::TryExecute(
			MakeRequest(Move, SpreadTargets),
			TerminalContext,
			NoRandom,
			TerminalResult,
			Error);
		TestTrue(TEXT("Terminal spread gates execute successfully"), bTerminalExecuted);
		TestTrue(TEXT("Terminal gates consume no RNG"), NoRandom.IsExact());
		const TArray<FString> ExpectedTerminalCalls =
		{
			TEXT("Reachability:12"),
			TEXT("Reachability:21"),
			TEXT("Protection:21"),
			TEXT("Reachability:22"),
			TEXT("Protection:22"),
			TEXT("TryHit:22"),
			TEXT("TypeImmunity:22")
		};
		TestTrue(
			TEXT("Spread targets stop at their first terminal gate in stored target order"),
			TerminalContext.Calls == ExpectedTerminalCalls);
		TestEqual(
			TEXT("The unreachable target emits one typed event"),
			CountExecutionEvents(TerminalResult.Events, EBattleEventType::Unreachable),
			1);
		TestEqual(
			TEXT("The protected target emits one typed event"),
			CountExecutionEvents(TerminalResult.Events, EBattleEventType::Protected),
			1);
		TestEqual(
			TEXT("The type-immune target emits one typed event"),
			CountExecutionEvents(TerminalResult.Events, EBattleEventType::Immunity),
			1);

		Move.bAlwaysHits = false;
		Move.Accuracy = 50;
		FMockExecutionContext RuleContext;
		RuleContext.TryHitOutcomes.Add(
			PlayerRightBattlerValue,
			EBattleEffectExecutionOutcome::Blocked);
		RuleContext.AbilityImmunityOutcomes.Add(
			OpponentLeftBattlerValue,
			EBattleEffectExecutionOutcome::Immune);
		FStrictScriptedRandom MissRandom(
			{{0, 99, 99, FBattleEffectExecutor::GetAccuracyRulePurpose()}});
		FBattleEffectExecutionResult RuleResult;
		Error = EBattleEffectExecutorError::None;
		const bool bRuleExecuted = FBattleEffectExecutor::TryExecute(
			MakeRequest(Move, SpreadTargets, 2),
			RuleContext,
			MissRandom,
			RuleResult,
			Error);
		TestTrue(TEXT("TryHit, hook-immunity, and miss targets execute"), bRuleExecuted);
		TestTrue(TEXT("Only the reached miss consumes its one accuracy draw"), MissRandom.IsExact());
		const TArray<FString> ExpectedRuleCalls =
		{
			TEXT("Reachability:12"),
			TEXT("Protection:12"),
			TEXT("TryHit:12"),
			TEXT("Reachability:21"),
			TEXT("Protection:21"),
			TEXT("TryHit:21"),
			TEXT("TypeImmunity:21"),
			TEXT("MoveImmunity:21"),
			TEXT("AbilityImmunity:21"),
			TEXT("Reachability:22"),
			TEXT("Protection:22"),
			TEXT("TryHit:22"),
			TEXT("TypeImmunity:22"),
			TEXT("MoveImmunity:22"),
			TEXT("AbilityImmunity:22"),
			TEXT("ItemImmunity:22"),
			TEXT("Accuracy:22")
		};
		TestTrue(
			TEXT("Each later gate is reached only when every prior gate applied"),
			RuleContext.Calls == ExpectedRuleCalls);
		TestEqual(
			TEXT("TryHit blocking emits a typed blocked event"),
			CountExecutionEvents(RuleResult.Events, EBattleEventType::EffectBlocked),
			1);
		TestEqual(
			TEXT("Ability immunity emits immunity before accuracy"),
			CountExecutionEvents(RuleResult.Events, EBattleEventType::Immunity),
			1);
		TestEqual(
			TEXT("The reached numeric accuracy emits one check"),
			CountExecutionEvents(RuleResult.Events, EBattleEventType::AccuracyChecked),
			1);
		TestEqual(
			TEXT("The failed check emits one miss"),
			CountExecutionEvents(RuleResult.Events, EBattleEventType::Missed),
			1);

		FBattleMoveDefinition HitMove = MakeDamagingMove();
		HitMove.bAlwaysHits = false;
		HitMove.Accuracy = 100;
		FMockExecutionContext HitContext;
		FStrictScriptedRandom HitRandom(
			{
				{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			});
		FBattleEffectExecutionResult HitResult;
		Error = EBattleEffectExecutorError::None;
		const bool bHitExecuted = FBattleEffectExecutor::TryExecute(
			MakeRequest(
				HitMove,
				{MakeBattlerTarget(
					EBattleSide::Opponent,
					EBattlePosition::Left,
					OpponentLeftBattlerValue)},
				3),
			HitContext,
			HitRandom,
			HitResult,
			Error);
		TestTrue(TEXT("A fully reached numeric hit executes"), bHitExecuted);
		TestTrue(TEXT("The hit consumes accuracy then damage RNG exactly"), HitRandom.IsExact());
		const int32 AccuracyIndex = HitContext.Calls.Find(TEXT("Accuracy:21"));
		const int32 BreakIndex = HitContext.Calls.Find(TEXT("ProtectionBreaking:21"));
		const int32 CriticalIndex = HitContext.Calls.Find(TEXT("Critical:21"));
		TestTrue(
			TEXT("Protection breaking occurs after successful accuracy"),
			AccuracyIndex != INDEX_NONE && BreakIndex > AccuracyIndex);
		TestTrue(
			TEXT("Critical resolution occurs after protection breaking"),
			CriticalIndex > BreakIndex);

		FBattleMoveDefinition ScopedSpreadMove = MakeDamagingMove(
			EBattleTargetClass::FixedSpreadSet,
			EBattleEffectTarget::AllResolvedTargets);
		FBattleMoveEffectDescriptor UserDrop = MakeEffect(
			1,
			EBattleMoveEffectKind::ModifyStatStage,
			EBattleEffectTarget::User);
		UserDrop.ChanceNumerator = 100;
		UserDrop.ChanceDenominator = 100;
		UserDrop.Stat = EBattleStat::Attack;
		UserDrop.MagnitudeNumerator = -1;
		ScopedSpreadMove.Effects.Add(UserDrop);
		const TArray<FBattleResolvedTarget> ScopedSpreadTargets =
		{
			MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Left,
				OpponentLeftBattlerValue),
			MakeBattlerTarget(
				EBattleSide::Opponent,
				EBattlePosition::Right,
				OpponentRightBattlerValue)
		};
		FMockExecutionContext ScopedSpreadContext;
		FStrictScriptedRandom ScopedSpreadRandom(
			{
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}
			});
		FBattleEffectExecutionResult ScopedSpreadResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A spread move with one user-scoped secondary executes"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(ScopedSpreadMove, ScopedSpreadTargets, 4),
				ScopedSpreadContext,
				ScopedSpreadRandom,
				ScopedSpreadResult,
				Error));
		TestTrue(
			TEXT("Two damage rolls and one user-secondary chance draw are consumed exactly"),
			ScopedSpreadRandom.IsExact());
		TestTrue(
			TEXT("The user-scoped descriptor applies once for the whole spread action"),
			ScopedSpreadContext.AppliedEffectOrders == TArray<int32>({1}));
		TestEqual(
			TEXT("The user loses exactly one Attack stage"),
			ScopedSpreadContext.AttackStage,
			-1);
		TestEqual(
			TEXT("The spread action emits one user stat mutation"),
			CountExecutionEvents(ScopedSpreadResult.Events, EBattleEventType::StatStageChanged),
			1);
		TestEqual(
			TEXT("The action-scoped secondary has one independent chance draw"),
			static_cast<int32>(Algo::CountIf(
				ScopedSpreadRandom.GetTrace(),
				[](const FBattleRandomDraw& Draw)
				{
					return Draw.RulePurpose == FBattleEffectExecutor::GetSecondaryChanceRulePurpose();
				})),
			1);
		const int32 SecondSpreadDamageIndex = ScopedSpreadContext.Calls.IndexOfByPredicate(
			[](const FString& Call)
			{
				return Call.StartsWith(TEXT("HP:-")) && Call.EndsWith(TEXT(":22"));
			});
		const int32 UserDropIndex = ScopedSpreadContext.Calls.Find(
			FString::Printf(
				TEXT("Effect:1:%d"),
				static_cast<int32>(EBattleMoveEffectKind::ModifyStatStage)));
		TestTrue(
			TEXT("The user-scoped secondary runs only after the final spread target takes damage"),
			SecondSpreadDamageIndex != INDEX_NONE && UserDropIndex > SecondSpreadDamageIndex);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BChanceTest,
		"PokemonSolarus.Battle.C05B.Chance.PrimaryAndIndependentSecondaries",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BChanceTest::RunTest(const FString& Parameters)
	{
		FBattleMoveEffectDescriptor Primary = MakeEffect(
			0,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		Primary.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		FBattleMoveEffectDescriptor FailedSecondary = MakeEffect(
			1,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		FailedSecondary.ConditionId = MakeDefinitionId<FConditionId>(VolatileConditionName);
		FailedSecondary.ChanceNumerator = 50;
		FailedSecondary.ChanceDenominator = 100;
		FBattleMoveEffectDescriptor CertainSecondary = FailedSecondary;
		CertainSecondary.Order = 2;
		CertainSecondary.ChanceNumerator = 100;
		const FBattleMoveDefinition Move = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{Primary, FailedSecondary, CertainSecondary});

		FMockExecutionContext Context;
		FStrictScriptedRandom Random(
			{
				{0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()},
				{0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}
			});
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		const bool bExecuted = FBattleEffectExecutor::TryExecute(
			MakeRequest(
				Move,
				{MakeBattlerTarget(
					EBattleSide::Opponent,
					EBattlePosition::Left,
					OpponentLeftBattlerValue)}),
			Context,
			Random,
			Result,
			Error);
		TestTrue(TEXT("The primary and independent secondaries execute"), bExecuted);
		TestTrue(TEXT("Only the two x/100 descriptors consume chance draws"), Random.IsExact());
		TestTrue(
			TEXT("The primary and explicit 100/100 secondary apply in descriptor order"),
			Context.AppliedEffectOrders == TArray<int32>({0, 2}));
		TestEqual(
			TEXT("Both independent descriptors emit RandomCheck"),
			CountExecutionEvents(Result.Events, EBattleEventType::RandomCheck),
			2);
		TestEqual(
			TEXT("Only two condition mutations are reported"),
			CountExecutionEvents(Result.Events, EBattleEventType::StatusChanged),
			2);

		TArray<EBattleEffectExecutionOutcome> ChanceOutcomes;
		for (const FBattleEffectExecutionEvent& Event : Result.Events)
		{
			if (Event.Type == EBattleEventType::RandomCheck)
			{
				ChanceOutcomes.Add(Event.Outcome);
			}
		}
		TestEqual(TEXT("Two chance outcomes are available"), ChanceOutcomes.Num(), 2);
		if (ChanceOutcomes.Num() == 2)
		{
			TestEqual(
				TEXT("The 50/100 draw fails independently"),
				ChanceOutcomes[0],
				EBattleEffectExecutionOutcome::ChanceFailed);
			TestEqual(
				TEXT("The explicit 100/100 draw still succeeds after consuming RNG"),
				ChanceOutcomes[1],
				EBattleEffectExecutionOutcome::Applied);
		}
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BConditionAndStageTest,
		"PokemonSolarus.Battle.C05B.Conditions.StatusVolatileStatAndStageCap",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BConditionAndStageTest::RunTest(const FString& Parameters)
	{
		FBattleMoveEffectDescriptor Major = MakeEffect(
			0,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		Major.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		FBattleMoveEffectDescriptor Volatile = Major;
		Volatile.Order = 1;
		Volatile.ConditionId = MakeDefinitionId<FConditionId>(VolatileConditionName);
		FBattleMoveEffectDescriptor Prevented = Volatile;
		Prevented.Order = 2;
		FBattleMoveEffectDescriptor Stage = MakeEffect(
			3,
			EBattleMoveEffectKind::ModifyStatStage,
			EBattleEffectTarget::ResolvedTarget);
		Stage.Stat = EBattleStat::Attack;
		Stage.MagnitudeNumerator = 2;
		FBattleMoveEffectDescriptor CappedStage = Stage;
		CappedStage.Order = 4;
		const FBattleMoveDefinition Move = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{Major, Volatile, Prevented, Stage, CappedStage});

		FMockExecutionContext Context;
		Context.AttackStage = 5;
		Context.ApplicationOutcomes.Add(2, EBattleEffectExecutionOutcome::Prevented);
		FStrictScriptedRandom Random({});
		FBattleEffectExecutionResult Result;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		const bool bExecuted = FBattleEffectExecutor::TryExecute(
			MakeRequest(
				Move,
				{MakeBattlerTarget(
					EBattleSide::Opponent,
					EBattlePosition::Left,
					OpponentLeftBattlerValue)}),
			Context,
			Random,
			Result,
			Error);
		TestTrue(TEXT("Condition and stage hook results execute"), bExecuted);
		TestTrue(TEXT("Primary condition and stage hooks consume no RNG"), Random.IsExact());
		TestEqual(TEXT("The stat stage clamps at +6"), Context.AttackStage, 6);
		TestEqual(
			TEXT("Only the two applied condition hooks emit StatusChanged"),
			CountExecutionEvents(Result.Events, EBattleEventType::StatusChanged),
			2);
		TestEqual(
			TEXT("Only the partial +5 to +6 mutation emits StatStageChanged"),
			CountExecutionEvents(Result.Events, EBattleEventType::StatStageChanged),
			1);
		TestEqual(
			TEXT("The prevented condition emits a typed prevented event"),
			CountExecutionEvents(Result.Events, EBattleEventType::EffectPrevented),
			1);
		TestEqual(
			TEXT("The partial clamp and no-op cap each emit typed cap feedback"),
			CountExecutionEvents(Result.Events, EBattleEventType::EffectCapped),
			2);

		FBattleMoveEffectDescriptor DuplicateSecondary = MakeEffect(
			0,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		DuplicateSecondary.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		DuplicateSecondary.ChanceNumerator = 50;
		DuplicateSecondary.ChanceDenominator = 100;
		FBattleMoveEffectDescriptor CappedSecondary = MakeEffect(
			1,
			EBattleMoveEffectKind::ModifyStatStage,
			EBattleEffectTarget::ResolvedTarget);
		CappedSecondary.Stat = EBattleStat::Attack;
		CappedSecondary.MagnitudeNumerator = 1;
		CappedSecondary.ChanceNumerator = 50;
		CappedSecondary.ChanceDenominator = 100;
		const FBattleMoveDefinition ProductionMove = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{DuplicateSecondary, CappedSecondary});
		TUniquePtr<FStrictScriptedRandom> ProductionRandom =
			MakeUnique<FStrictScriptedRandom>(TArray<FExpectedDraw>{});
		FStrictScriptedRandom* ProductionRandomView = ProductionRandom.Get();
		TUniquePtr<IBattleRandom> ProductionRandomOwner = MoveTemp(ProductionRandom);
		TUniquePtr<FBattleEngine> ProductionEngine = MakeEngine(
			ProductionMove,
			MoveTemp(ProductionRandomOwner));
		TestTrue(
			TEXT("The fixture seeds duplicate status and capped Attack in production state"),
			FBattleC05BEngineFixture::SeedDuplicateStatusAndCappedAttack(
				*ProductionEngine,
				MakeNumericId<FBattlerId>(OpponentLeftBattlerValue),
				MakeDefinitionId<FConditionId>(MajorConditionName)));
		TestTrue(TEXT("The production engine reaches C05B"), PrepareFirstMove(*ProductionEngine));
		const FBattleResolution ProductionResolution = ProductionEngine->ExecuteCurrentMoveEffects();
		TestTrue(TEXT("Ineligible production secondaries resolve without mutation"), ProductionResolution.WasAccepted());
		TestTrue(
			TEXT("Duplicate/capped eligibility consumes no secondary chance draw"),
			ProductionRandomView->IsExact());
		TestEqual(
			TEXT("Duplicate status reports failure"),
			static_cast<int32>(Algo::CountIf(
				ProductionResolution.GetEvents(),
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::EffectFailed;
				})),
			1);
		TestEqual(
			TEXT("Capped stage reports its cap"),
			static_cast<int32>(Algo::CountIf(
				ProductionResolution.GetEvents(),
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::EffectCapped;
				})),
			1);
		TestEqual(
			TEXT("Ineligible secondaries emit no RandomCheck or false mutation"),
			static_cast<int32>(Algo::CountIf(
				ProductionResolution.GetEvents(),
				[](const FBattleEvent& Event)
				{
					return Event.GetType() == EBattleEventType::RandomCheck
						|| Event.GetType() == EBattleEventType::StatusChanged
						|| Event.GetType() == EBattleEventType::StatStageChanged;
				})),
			0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BRecoveryTest,
		"PokemonSolarus.Battle.C05B.Recovery.HealDrainRecoilAndSelfDamage",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BRecoveryTest::RunTest(const FString& Parameters)
	{
		const FBattleResolvedTarget UserTarget = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleResolvedTarget OpponentTarget = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);

		FBattleMoveEffectDescriptor FixedHeal = MakeEffect(
			0,
			EBattleMoveEffectKind::Heal,
			EBattleEffectTarget::User);
		FixedHeal.MagnitudeNumerator = 30;
		FixedHeal.MagnitudeDenominator = 1;
		FixedHeal.Flags = EBattleMoveEffectFlags::MinimumOne;
		const FBattleMoveDefinition FixedHealMove = MakeStatusMove(
			EBattleTargetClass::Self,
			{FixedHeal});
		FMockExecutionContext FixedHealContext;
		FixedHealContext.SetHp(PlayerLeftBattlerValue, 90, 100);
		FStrictScriptedRandom NoHealRandom({});
		FBattleEffectExecutionResult FixedHealResult;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Fixed healing executes"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(FixedHealMove, {UserTarget}),
				FixedHealContext,
				NoHealRandom,
				FixedHealResult,
				Error));
		TestEqual(TEXT("Fixed healing caps at Max HP"), FixedHealContext.GetHp(PlayerLeftBattlerValue), 100);
		TestEqual(
			TEXT("A capped positive heal still reports one healing mutation"),
			CountExecutionEvents(FixedHealResult.Events, EBattleEventType::Healing),
			1);
		TestEqual(
			TEXT("A capped positive heal reports its cap"),
			CountExecutionEvents(FixedHealResult.Events, EBattleEventType::EffectCapped),
			1);

		FBattleMoveEffectDescriptor PercentageHeal = FixedHeal;
		PercentageHeal.MagnitudeNumerator = 1;
		PercentageHeal.MagnitudeDenominator = 2;
		const FBattleMoveDefinition PercentageHealMove = MakeStatusMove(
			EBattleTargetClass::Self,
			{PercentageHeal});
		FMockExecutionContext PercentageHealContext;
		PercentageHealContext.SetHp(PlayerLeftBattlerValue, 20, 101);
		FStrictScriptedRandom NoPercentageRandom({});
		FBattleEffectExecutionResult PercentageHealResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Base-Max-HP percentage healing executes"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(PercentageHealMove, {UserTarget}, 2),
				PercentageHealContext,
				NoPercentageRandom,
				PercentageHealResult,
				Error));
		TestEqual(
			TEXT("Half of odd Base Max HP rounds half up"),
			PercentageHealContext.GetHp(PlayerLeftBattlerValue),
			71);

		FBattleMoveDefinition LinkedMove = MakeDamagingMove();
		FBattleMoveEffectDescriptor Drain = MakeEffect(
			1,
			EBattleMoveEffectKind::Drain,
			EBattleEffectTarget::User);
		Drain.MagnitudeNumerator = 1;
		Drain.MagnitudeDenominator = 2;
		Drain.Flags = EBattleMoveEffectFlags::MinimumOne;
		FBattleMoveEffectDescriptor Recoil = MakeEffect(
			2,
			EBattleMoveEffectKind::Recoil,
			EBattleEffectTarget::User);
		Recoil.MagnitudeNumerator = 1;
		Recoil.MagnitudeDenominator = 3;
		Recoil.Flags = EBattleMoveEffectFlags::UsesActualDamage
			| EBattleMoveEffectFlags::MinimumOne;
		LinkedMove.Effects.Add(Drain);
		LinkedMove.Effects.Add(Recoil);
		FMockExecutionContext LinkedContext;
		LinkedContext.SetHp(PlayerLeftBattlerValue, 50, 101);
		LinkedContext.SetHp(OpponentLeftBattlerValue, 5, 100);
		FStrictScriptedRandom LinkedRandom(
			{{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}});
		FBattleEffectExecutionResult LinkedResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Damage-linked drain and recoil execute after target mutation"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(LinkedMove, {OpponentTarget}, 3),
				LinkedContext,
				LinkedRandom,
				LinkedResult,
				Error));
		TestTrue(TEXT("The linked move consumes only its damage draw"), LinkedRandom.IsExact());
		TestEqual(TEXT("Actual target HP removal is five"), LinkedResult.TotalActualDamage, 5);
		TestEqual(TEXT("The target reaches zero before linked effects"), LinkedContext.GetHp(OpponentLeftBattlerValue), 0);
		TestEqual(
			TEXT("Drain half-up heals three and one-third recoil half-up removes two"),
			LinkedContext.GetHp(PlayerLeftBattlerValue),
			51);
		const int32 TargetHpEventIndex = LinkedResult.Events.IndexOfByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::HPChanged
					&& !Event.Targets.IsEmpty()
					&& Event.Targets[0].BattlerId.GetValue() == OpponentLeftBattlerValue;
			});
		const int32 HealingEventIndex = LinkedResult.Events.IndexOfByPredicate(
			[](const FBattleEffectExecutionEvent& Event)
			{
				return Event.Type == EBattleEventType::Healing;
			});
		TestTrue(
			TEXT("Target HP mutation precedes linked drain"),
			TargetHpEventIndex != INDEX_NONE && HealingEventIndex > TargetHpEventIndex);

		FBattleMoveEffectDescriptor FixedSelfDamage = MakeEffect(
			1,
			EBattleMoveEffectKind::Recoil,
			EBattleEffectTarget::User);
		FixedSelfDamage.MagnitudeNumerator = 7;
		FixedSelfDamage.MagnitudeDenominator = 1;
		FBattleMoveDefinition FixedSelfMove = MakeDamagingMove();
		FixedSelfMove.Effects.Add(FixedSelfDamage);
		FMockExecutionContext FixedSelfContext;
		FixedSelfContext.SetHp(PlayerLeftBattlerValue, 50, 101);
		FStrictScriptedRandom FixedSelfRandom(
			{{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}});
		FBattleEffectExecutionResult FixedSelfResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A denominator-one non-actual recoil encodes fixed self-damage"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(FixedSelfMove, {OpponentTarget}, 4),
				FixedSelfContext,
				FixedSelfRandom,
				FixedSelfResult,
				Error));
		TestTrue(TEXT("Fixed self-damage adds no draw beyond direct damage"), FixedSelfRandom.IsExact());
		TestEqual(TEXT("Fixed self-damage removes seven HP"), FixedSelfContext.GetHp(PlayerLeftBattlerValue), 43);

		FBattleMoveDefinition StruggleShape = MakeDamagingMove();
		StruggleShape.Type = EPokemonType::Invalid;
		StruggleShape.Flags |= EBattleMoveFlags::TypelessDamage;
		FBattleMoveEffectDescriptor StruggleRecoil = MakeEffect(
			1,
			EBattleMoveEffectKind::Recoil,
			EBattleEffectTarget::User);
		StruggleRecoil.MagnitudeNumerator = 1;
		StruggleRecoil.MagnitudeDenominator = 4;
		StruggleRecoil.Flags = EBattleMoveEffectFlags::MinimumOne;
		StruggleShape.Effects.Add(StruggleRecoil);
		FMockExecutionContext StruggleContext;
		StruggleContext.SetHp(PlayerLeftBattlerValue, 100, 101);
		FStrictScriptedRandom StruggleRandom(
			{{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}});
		FBattleEffectExecutionResult StruggleResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("The Struggle-shaped fixed Base-Max-HP recoil executes"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(StruggleShape, {OpponentTarget}, 5),
				StruggleContext,
				StruggleRandom,
				StruggleResult,
				Error));
		TestEqual(
			TEXT("Struggle recoil rounds one quarter of 101 half up to 25"),
			StruggleContext.GetHp(PlayerLeftBattlerValue),
			75);
		return true;
	}

	FBattleMoveDefinition MakeMultiHitMove(
		const int32 MinimumCount,
		const int32 MaximumCount,
		const bool bPerHitAccuracy = false,
		const int32 Accuracy = 0)
	{
		FBattleMoveDefinition Move = MakeDamagingMove();
		Move.Effects[0].Order = 1;
		FBattleMoveEffectDescriptor MultiHit = MakeEffect(
			0,
			EBattleMoveEffectKind::MultiHit,
			EBattleEffectTarget::ResolvedTarget);
		MultiHit.MinimumCount = MinimumCount;
		MultiHit.MaximumCount = MaximumCount;
		Move.Effects.Insert(MultiHit, 0);
		if (Accuracy > 0)
		{
			Move.bAlwaysHits = false;
			Move.Accuracy = Accuracy;
		}
		if (bPerHitAccuracy)
		{
			Move.Flags |= EBattleMoveFlags::UsesPerHitAccuracy;
		}
		return Move;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BMultiHitTest,
		"PokemonSolarus.Battle.C05B.MultiHit.FixedRangedPerHitAndEarlyFaint",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BMultiHitTest::RunTest(const FString& Parameters)
	{
		const FBattleResolvedTarget Target = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);

		FBattleMoveDefinition FixedMove = MakeMultiHitMove(2, 2);
		FBattleMoveEffectDescriptor PerHitSecondary = MakeEffect(
			2,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		PerHitSecondary.ConditionId = MakeDefinitionId<FConditionId>(VolatileConditionName);
		PerHitSecondary.ChanceNumerator = 50;
		PerHitSecondary.ChanceDenominator = 100;
		PerHitSecondary.Flags = EBattleMoveEffectFlags::PerHit;
		FBattleMoveEffectDescriptor OrdinarySecondary = MakeEffect(
			3,
			EBattleMoveEffectKind::ModifyStatStage,
			EBattleEffectTarget::ResolvedTarget);
		OrdinarySecondary.Stat = EBattleStat::Attack;
		OrdinarySecondary.MagnitudeNumerator = 1;
		OrdinarySecondary.ChanceNumerator = 100;
		OrdinarySecondary.ChanceDenominator = 100;
		FixedMove.Effects.Add(PerHitSecondary);
		FixedMove.Effects.Add(OrdinarySecondary);
		FMockExecutionContext FixedContext;
		FStrictScriptedRandom FixedRandom(
			{
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 15, 15, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 99, 0, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()},
				{0, 99, 99, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()},
				{0, 99, 0, FBattleEffectExecutor::GetSecondaryChanceRulePurpose()}
			});
		FBattleEffectExecutionResult FixedResult;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A fixed two-hit move executes"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(FixedMove, {Target}),
				FixedContext,
				FixedRandom,
				FixedResult,
				Error));
		TestTrue(
			TEXT("Fixed-hit damage then repeated/ordinary secondary RNG order is exact"),
			FixedRandom.IsExact());
		TestEqual(TEXT("The fixed move reports two completed hits"), FixedResult.CompletedHitsPerDamageTarget[0], 2);
		TArray<const FBattleEffectExecutionEvent*> FixedDamageEvents;
		for (const FBattleEffectExecutionEvent& Event : FixedResult.Events)
		{
			if (Event.Type == EBattleEventType::Damage)
			{
				FixedDamageEvents.Add(&Event);
			}
		}
		TestEqual(TEXT("The fixed move emits one damage event per hit"), FixedDamageEvents.Num(), 2);
		if (FixedDamageEvents.Num() == 2)
		{
			TestEqual(TEXT("The first hit index is one"), FixedDamageEvents[0]->HitIndex.GetValue(), static_cast<uint16>(1));
			TestEqual(TEXT("The second hit index is two"), FixedDamageEvents[1]->HitIndex.GetValue(), static_cast<uint16>(2));
			TestEqual(TEXT("Both events report the completed count"), FixedDamageEvents[0]->HitCount.GetValue(), static_cast<uint16>(2));
			TestEqual(TEXT("The final event reports the completed count"), FixedDamageEvents[1]->HitCount.GetValue(), static_cast<uint16>(2));
		}
		TestEqual(
			TEXT("The PerHit secondary checks eligibility once per completed hit"),
			static_cast<int32>(Algo::CountIf(
				FixedContext.Calls,
				[](const FString& Call)
				{
					return Call == TEXT("Eligibility:2:21");
				})),
			2);
		TestEqual(
			TEXT("The ordinary secondary checks eligibility once per reached target"),
			static_cast<int32>(Algo::CountIf(
				FixedContext.Calls,
				[](const FString& Call)
				{
					return Call == TEXT("Eligibility:3:21");
				})),
			1);
		TestTrue(
			TEXT("Applied secondaries preserve descriptor order"),
			FixedContext.AppliedEffectOrders == TArray<int32>({2, 3}));
		TestEqual(
			TEXT("One PerHit application and one ordinary application mutate"),
			CountExecutionEvents(FixedResult.Events, EBattleEventType::StatusChanged)
				+ CountExecutionEvents(FixedResult.Events, EBattleEventType::StatStageChanged),
			2);
		TArray<int32> RandomCheckIndices;
		int32 LastDirectDamageIndex = INDEX_NONE;
		int32 StatusIndex = INDEX_NONE;
		int32 StatIndex = INDEX_NONE;
		for (int32 EventIndex = 0; EventIndex < FixedResult.Events.Num(); ++EventIndex)
		{
			switch (FixedResult.Events[EventIndex].Type)
			{
			case EBattleEventType::RandomCheck:
				RandomCheckIndices.Add(EventIndex);
				break;
			case EBattleEventType::Damage:
				LastDirectDamageIndex = EventIndex;
				break;
			case EBattleEventType::StatusChanged:
				StatusIndex = EventIndex;
				break;
			case EBattleEventType::StatStageChanged:
				StatIndex = EventIndex;
				break;
			default:
				break;
			}
		}
		TestEqual(TEXT("Two damage and three secondary draws are public"), RandomCheckIndices.Num(), 5);
		if (RandomCheckIndices.Num() == 5)
		{
			TestTrue(
				TEXT("Every direct damage event precedes the first secondary draw"),
				RandomCheckIndices[2] > LastDirectDamageIndex);
		}
		TestTrue(
			TEXT("PerHit then ordinary mutations follow all direct damage in descriptor order"),
			StatusIndex > LastDirectDamageIndex && StatIndex > StatusIndex);

		const TArray<TPair<uint32, int32>> CountBoundaries =
		{
			{0, 2}, {6, 2}, {7, 3}, {13, 3},
			{14, 4}, {16, 4}, {17, 5}, {19, 5}
		};
		for (int32 CaseIndex = 0; CaseIndex < CountBoundaries.Num(); ++CaseIndex)
		{
			const TPair<uint32, int32>& Boundary = CountBoundaries[CaseIndex];
			TArray<FExpectedDraw> Draws;
			Draws.Add({0, 19, Boundary.Key, FBattleEffectExecutor::GetMultiHitRulePurpose()});
			for (int32 HitIndex = 0; HitIndex < Boundary.Value; ++HitIndex)
			{
				Draws.Add({0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()});
			}
			FStrictScriptedRandom BoundaryRandom(MoveTemp(Draws));
			FMockExecutionContext BoundaryContext;
			BoundaryContext.SetHp(OpponentLeftBattlerValue, 1000, 1000);
			FBattleEffectExecutionResult BoundaryResult;
			Error = EBattleEffectExecutorError::None;
			const FBattleMoveDefinition RangedMove = MakeMultiHitMove(2, 5);
			const bool bExecuted = FBattleEffectExecutor::TryExecute(
				MakeRequest(RangedMove, {Target}, 10 + CaseIndex),
				BoundaryContext,
				BoundaryRandom,
				BoundaryResult,
				Error);
			TestTrue(TEXT("Each 2..5 count boundary executes"), bExecuted);
			TestTrue(TEXT("Each boundary consumes count then reached damage draws"), BoundaryRandom.IsExact());
			if (!BoundaryResult.CompletedHitsPerDamageTarget.IsEmpty())
			{
				TestEqual(
					TEXT("The U[0,19] boundary maps to the frozen hit count"),
					BoundaryResult.CompletedHitsPerDamageTarget[0],
					Boundary.Value);
			}
		}

		FBattleMoveDefinition CriticalMove = MakeMultiHitMove(2, 2);
		CriticalMove.Flags &= ~EBattleMoveFlags::NeverCritical;
		FMockExecutionContext CriticalContext;
		FStrictScriptedRandom CriticalRandom(
			{
				{0, 23, 1, FBattleEffectExecutor::GetCriticalRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 23, 1, FBattleEffectExecutor::GetCriticalRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			});
		FBattleEffectExecutionResult CriticalResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Each reached hit resolves critical then damage independently"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(CriticalMove, {Target}, 30),
				CriticalContext,
				CriticalRandom,
				CriticalResult,
				Error));
		TestTrue(TEXT("Per-hit critical and damage draws are exact"), CriticalRandom.IsExact());

		FBattleMoveDefinition PerHitAccuracyMove = MakeMultiHitMove(3, 3, true, 50);
		FMockExecutionContext PerHitAccuracyContext;
		FStrictScriptedRandom PerHitAccuracyRandom(
			{
				{0, 99, 0, FBattleEffectExecutor::GetAccuracyRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 99, 99, FBattleEffectExecutor::GetAccuracyRulePurpose()}
			});
		FBattleEffectExecutionResult PerHitAccuracyResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A later per-hit accuracy miss cleanly stops the move"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(PerHitAccuracyMove, {Target}, 31),
				PerHitAccuracyContext,
				PerHitAccuracyRandom,
				PerHitAccuracyResult,
				Error));
		TestTrue(TEXT("No draw occurs after the later per-hit miss"), PerHitAccuracyRandom.IsExact());
		TestEqual(
			TEXT("Only the first reached hit is completed"),
			PerHitAccuracyResult.CompletedHitsPerDamageTarget[0],
			1);

		const FBattleMoveDefinition EarlyFaintMove = MakeMultiHitMove(2, 5);
		FMockExecutionContext EarlyFaintContext;
		EarlyFaintContext.SetHp(OpponentLeftBattlerValue, 20, 100);
		FStrictScriptedRandom EarlyFaintRandom(
			{
				{0, 19, 19, FBattleEffectExecutor::GetMultiHitRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()},
				{0, 15, 0, FBattleEffectExecutor::GetDamageRandomRulePurpose()}
			});
		FBattleEffectExecutionResult EarlyFaintResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A requested five-hit move stops at zero HP"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(EarlyFaintMove, {Target}, 32),
				EarlyFaintContext,
				EarlyFaintRandom,
				EarlyFaintResult,
				Error));
		TestTrue(TEXT("Early zero HP omits all later hit draws"), EarlyFaintRandom.IsExact());
		TestEqual(TEXT("The target stopped at zero HP"), EarlyFaintContext.GetHp(OpponentLeftBattlerValue), 0);
		TestEqual(
			TEXT("Actual completed hit count is two, not the requested five"),
			EarlyFaintResult.CompletedHitsPerDamageTarget[0],
			2);
		for (const FBattleEffectExecutionEvent& Event : EarlyFaintResult.Events)
		{
			if (Event.Type == EBattleEventType::Damage)
			{
				TestEqual(
					TEXT("Every early-stop damage event reports the final completed count"),
					Event.HitCount.GetValue(),
					static_cast<uint16>(2));
			}
		}
		TestEqual(
			TEXT("C05B does not emit the C05C faint event"),
			CountExecutionEvents(EarlyFaintResult.Events, EBattleEventType::Fainted),
			0);
		return true;
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BFutureHooksTest,
		"PokemonSolarus.Battle.C05B.Hooks.FutureOperationsAndConditionRemoval",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

	bool FBattleC05BFutureHooksTest::RunTest(const FString& Parameters)
	{
		const FBattleResolvedTarget UserTarget = MakeBattlerTarget(
			EBattleSide::Player,
			EBattlePosition::Left,
			PlayerLeftBattlerValue);
		const FBattleResolvedTarget OpponentTarget = MakeBattlerTarget(
			EBattleSide::Opponent,
			EBattlePosition::Left,
			OpponentLeftBattlerValue);

		TArray<FBattleMoveEffectDescriptor> UserFutureEffects;
		UserFutureEffects.Add(MakeEffect(0, EBattleMoveEffectKind::Protect, EBattleEffectTarget::User));
		UserFutureEffects.Add(MakeEffect(1, EBattleMoveEffectKind::Charge, EBattleEffectTarget::User));
		UserFutureEffects.Add(MakeEffect(2, EBattleMoveEffectKind::Recharge, EBattleEffectTarget::User));
		UserFutureEffects.Add(MakeEffect(3, EBattleMoveEffectKind::SemiInvulnerability, EBattleEffectTarget::User));
		for (FBattleMoveEffectDescriptor& Effect : UserFutureEffects)
		{
			Effect.ConditionId = MakeDefinitionId<FConditionId>(VolatileConditionName);
		}
		const FBattleMoveDefinition UserFutureMove = MakeStatusMove(
			EBattleTargetClass::Self,
			MoveTemp(UserFutureEffects));
		FMockExecutionContext UserFutureContext;
		FStrictScriptedRandom UserFutureRandom({});
		FBattleEffectExecutionResult UserFutureResult;
		EBattleEffectExecutorError Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Protect, charge, recharge, and semi-invulnerability route through hooks"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(UserFutureMove, {UserTarget}),
				UserFutureContext,
				UserFutureRandom,
				UserFutureResult,
				Error));
		TestEqual(
			TEXT("The four not-yet-concrete user operations are typed deferred"),
			CountExecutionEvents(UserFutureResult.Events, EBattleEventType::EffectDeferred),
			4);
		TestEqual(
			TEXT("Deferred user operations produce no generic mutation event"),
			CountExecutionEvents(UserFutureResult.Events, EBattleEventType::StatusChanged)
				+ CountExecutionEvents(UserFutureResult.Events, EBattleEventType::FieldEffectChanged),
			0);

		FBattleMoveEffectDescriptor Switch = MakeEffect(
			0,
			EBattleMoveEffectKind::Switch,
			EBattleEffectTarget::ResolvedTarget);
		FBattleMoveEffectDescriptor ChangeItem = MakeEffect(
			1,
			EBattleMoveEffectKind::ChangeItem,
			EBattleEffectTarget::ResolvedTarget);
		ChangeItem.ItemId = MakeDefinitionId<FItemId>(ItemName);
		const FBattleMoveDefinition TargetFutureMove = MakeStatusMove(
			EBattleTargetClass::SelectedOpponent,
			{Switch, ChangeItem});
		FMockExecutionContext TargetFutureContext;
		FStrictScriptedRandom TargetFutureRandom({});
		FBattleEffectExecutionResult TargetFutureResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Switch and item operations route through typed hooks"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(TargetFutureMove, {OpponentTarget}, 2),
				TargetFutureContext,
				TargetFutureRandom,
				TargetFutureResult,
				Error));
		TestEqual(
			TEXT("Switch and item operations remain deferred without mutation"),
			CountExecutionEvents(TargetFutureResult.Events, EBattleEventType::EffectDeferred),
			2);

		FBattleMoveEffectDescriptor SetField = MakeEffect(
			0,
			EBattleMoveEffectKind::SetFieldCondition,
			EBattleEffectTarget::Field);
		SetField.ConditionId = MakeDefinitionId<FConditionId>(WeatherConditionName);
		SetField.DurationTurns = 5;
		FBattleMoveEffectDescriptor RemoveField = MakeEffect(
			1,
			EBattleMoveEffectKind::RemoveCondition,
			EBattleEffectTarget::Field);
		RemoveField.ConditionId = SetField.ConditionId;
		const FBattleMoveDefinition FieldMove = MakeStatusMove(
			EBattleTargetClass::Field,
			{SetField, RemoveField});
		FMockExecutionContext FieldContext;
		FStrictScriptedRandom FieldRandom({});
		FBattleEffectExecutionResult FieldResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Field add and removal use applied generic hooks"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(FieldMove, {FBattleResolvedTarget::CreateField()}, 3),
				FieldContext,
				FieldRandom,
				FieldResult,
				Error));
		TestEqual(
			TEXT("Field add and removal each emit one field mutation"),
			CountExecutionEvents(FieldResult.Events, EBattleEventType::FieldEffectChanged),
			2);

		FBattleMoveEffectDescriptor SetSide = MakeEffect(
			0,
			EBattleMoveEffectKind::SetSideCondition,
			EBattleEffectTarget::TargetSide);
		SetSide.ConditionId = MakeDefinitionId<FConditionId>(SideConditionName);
		SetSide.DurationTurns = 5;
		FBattleMoveEffectDescriptor RemoveSide = MakeEffect(
			1,
			EBattleMoveEffectKind::RemoveCondition,
			EBattleEffectTarget::TargetSide);
		RemoveSide.ConditionId = SetSide.ConditionId;
		const FBattleMoveDefinition SideMove = MakeStatusMove(
			EBattleTargetClass::OpponentSide,
			{SetSide, RemoveSide});
		FMockExecutionContext SideContext;
		FStrictScriptedRandom SideRandom({});
		FBattleEffectExecutionResult SideResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("Side add and removal use applied generic hooks"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(SideMove, {MakeSideTarget(EBattleSide::Opponent)}, 4),
				SideContext,
				SideRandom,
				SideResult,
				Error));
		TestEqual(
			TEXT("Side add and removal each emit one field mutation"),
			CountExecutionEvents(SideResult.Events, EBattleEventType::FieldEffectChanged),
			2);

		FBattleMoveEffectDescriptor SetBothSides = MakeEffect(
			0,
			EBattleMoveEffectKind::SetSideCondition,
			EBattleEffectTarget::BothSides);
		SetBothSides.ConditionId = MakeDefinitionId<FConditionId>(SideConditionName);
		SetBothSides.DurationTurns = 5;
		const FBattleMoveDefinition BothSidesMove = MakeStatusMove(
			EBattleTargetClass::BothSides,
			{SetBothSides});
		FMockExecutionContext BothSidesContext;
		FStrictScriptedRandom BothSidesRandom({});
		FBattleEffectExecutionResult BothSidesResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A BothSides descriptor executes once for each side"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(
					BothSidesMove,
					{MakeSideTarget(EBattleSide::Player), MakeSideTarget(EBattleSide::Opponent)},
					5),
				BothSidesContext,
				BothSidesRandom,
				BothSidesResult,
				Error));
		TestEqual(
			TEXT("BothSides produces exactly two mutations, not two expansions per reached side"),
			CountExecutionEvents(BothSidesResult.Events, EBattleEventType::FieldEffectChanged),
			2);
		TestTrue(
			TEXT("The one descriptor applies exactly once to each side"),
			BothSidesContext.AppliedEffectOrders == TArray<int32>({0, 0}));
		TArray<EBattleSide> MutatedSides;
		for (const FBattleEffectExecutionEvent& Event : BothSidesResult.Events)
		{
			if (Event.Type == EBattleEventType::FieldEffectChanged
				&& Event.Targets.Num() == 1
				&& Event.Targets[0].bHasSide)
			{
				MutatedSides.Add(Event.Targets[0].Side);
			}
		}
		TestTrue(
			TEXT("BothSides mutation order is Player then Opponent"),
			MutatedSides == TArray<EBattleSide>({EBattleSide::Player, EBattleSide::Opponent}));

		const FBattleMoveDefinition SingleSideReachedMove = MakeStatusMove(
			EBattleTargetClass::OpponentSide,
			{SetBothSides});
		FMockExecutionContext SingleSideReachedContext;
		FStrictScriptedRandom SingleSideReachedRandom({});
		FBattleEffectExecutionResult SingleSideReachedResult;
		Error = EBattleEffectExecutorError::None;
		TestTrue(
			TEXT("A BothSides descriptor expands beyond a single reached side"),
			FBattleEffectExecutor::TryExecute(
				MakeRequest(
					SingleSideReachedMove,
					{MakeSideTarget(EBattleSide::Opponent)},
					6),
				SingleSideReachedContext,
				SingleSideReachedRandom,
				SingleSideReachedResult,
				Error));
		TestEqual(
			TEXT("The single reached side still produces two side mutations"),
			CountExecutionEvents(
				SingleSideReachedResult.Events,
				EBattleEventType::FieldEffectChanged),
			2);
		TArray<EBattleSide> SingleReachMutatedSides;
		for (const FBattleEffectExecutionEvent& Event : SingleSideReachedResult.Events)
		{
			if (Event.Type == EBattleEventType::FieldEffectChanged
				&& Event.Targets.Num() == 1
				&& Event.Targets[0].bHasSide)
			{
				SingleReachMutatedSides.Add(Event.Targets[0].Side);
			}
		}
		TestTrue(
			TEXT("A single-side move expands BothSides in Player then Opponent order"),
			SingleReachMutatedSides
				== TArray<EBattleSide>({EBattleSide::Player, EBattleSide::Opponent}));
		TestTrue(TEXT("All future and generic operations consume no RNG"),
			UserFutureRandom.IsExact()
				&& TargetFutureRandom.IsExact()
				&& FieldRandom.IsExact()
				&& SideRandom.IsExact()
				&& BothSidesRandom.IsExact()
				&& SingleSideReachedRandom.IsExact());
		return true;
	}

	bool TrySerializeEngineReplay(FBattleEngine& Engine, TArray<uint8>& OutBytes)
	{
		FBattleRejection Rejection;
		return FBattleReplaySerializer::TrySerializeCanonical(
			Engine.ExportReplayRecord(),
			OutBytes,
			Rejection);
	}

	IMPLEMENT_SIMPLE_AUTOMATION_TEST(
		FBattleC05BReplayDeterminismTest,
		"PokemonSolarus.Battle.C05B.Replay.DeterminismAndNoDuplication",
		EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

	bool FBattleC05BReplayDeterminismTest::RunTest(const FString& Parameters)
	{
		FBattleMoveDefinition Move = MakeDamagingMove();
		Move.bAlwaysHits = false;
		Move.Accuracy = 100;
		FBattleMoveEffectDescriptor CertainSecondary = MakeEffect(
			1,
			EBattleMoveEffectKind::ApplyCondition,
			EBattleEffectTarget::ResolvedTarget);
		CertainSecondary.ConditionId = MakeDefinitionId<FConditionId>(MajorConditionName);
		CertainSecondary.ChanceNumerator = 100;
		CertainSecondary.ChanceDenominator = 100;
		Move.Effects.Add(CertainSecondary);

		TUniquePtr<IBattleRandom> FirstRandom = MakeUnique<FSeededBattleRandom>(5505);
		TUniquePtr<IBattleRandom> SecondRandom = MakeUnique<FSeededBattleRandom>(5505);
		TUniquePtr<FBattleEngine> First = MakeEngine(Move, MoveTemp(FirstRandom));
		TUniquePtr<FBattleEngine> Second = MakeEngine(Move, MoveTemp(SecondRandom));
		TestTrue(TEXT("The first identical engine reaches C05B"), PrepareFirstMove(*First));
		TestTrue(TEXT("The second identical engine reaches C05B"), PrepareFirstMove(*Second));
		TestTrue(TEXT("The first identical effect execution succeeds"), First->ExecuteCurrentMoveEffects().WasAccepted());
		TestTrue(TEXT("The second identical effect execution succeeds"), Second->ExecuteCurrentMoveEffects().WasAccepted());

		const TArray<FBattleRandomDraw> FirstTrace = First->ExportRandomTrace();
		const TArray<FBattleRandomDraw> SecondTrace = Second->ExportRandomTrace();
		TestTrue(TEXT("Identical seed and input produce an identical RNG trace"), FirstTrace == SecondTrace);
		TestEqual(TEXT("Accuracy, damage, and explicit 100/100 consume three draws"), FirstTrace.Num(), 3);
		if (FirstTrace.Num() == 3)
		{
			TestEqual(TEXT("Accuracy is the first draw"), FirstTrace[0].RulePurpose.GetName(), FBattleEffectExecutor::GetAccuracyRulePurpose().GetName());
			TestEqual(TEXT("Damage random is the second draw"), FirstTrace[1].RulePurpose.GetName(), FBattleEffectExecutor::GetDamageRandomRulePurpose().GetName());
			TestEqual(TEXT("Secondary chance is the third draw"), FirstTrace[2].RulePurpose.GetName(), FBattleEffectExecutor::GetSecondaryChanceRulePurpose().GetName());
		}

		TArray<uint8> FirstBytes;
		TArray<uint8> FirstRepeatBytes;
		TArray<uint8> SecondBytes;
		TestTrue(TEXT("The first replay serializes"), TrySerializeEngineReplay(*First, FirstBytes));
		TestTrue(TEXT("Repeated export of the first replay serializes"), TrySerializeEngineReplay(*First, FirstRepeatBytes));
		TestTrue(TEXT("The second replay serializes"), TrySerializeEngineReplay(*Second, SecondBytes));
		TestTrue(TEXT("Repeated export is byte-identical"), FirstBytes == FirstRepeatBytes);
		TestTrue(TEXT("Identical seed and input are byte-identical"), FirstBytes == SecondBytes);

		const FBattleReplayRecord FirstRecord = First->ExportReplayRecord();
		const FBattleReplayRecord SecondRecord = Second->ExportReplayRecord();
		TestEqual(
			TEXT("C05B preserves the current replay schema"),
			FirstRecord.GetSchemaVersion(),
			static_cast<uint32>(5));
		TestEqual(
			TEXT("The first record has exactly one direct damage mutation"),
			CountResolutionEvents(FirstRecord.GetResolutions(), EBattleEventType::Damage),
			1);
		TestEqual(
			TEXT("The first record has exactly one secondary status mutation"),
			CountResolutionEvents(FirstRecord.GetResolutions(), EBattleEventType::StatusChanged),
			1);
		TestEqual(
			TEXT("The second record has exactly one direct damage mutation"),
			CountResolutionEvents(SecondRecord.GetResolutions(), EBattleEventType::Damage),
			1);
		TestEqual(
			TEXT("The second record has exactly one secondary status mutation"),
			CountResolutionEvents(SecondRecord.GetResolutions(), EBattleEventType::StatusChanged),
			1);

		const int32 FirstHpBeforeDuplicate = First->GetSnapshot().FindBattler(
			MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP;
		const FConditionId FirstStatusBeforeDuplicate = First->GetSnapshot().FindObservedBattler(
			MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->MajorStatusId;
		TestFalse(TEXT("A repeated first-engine execution is rejected"), First->ExecuteCurrentMoveEffects().WasAccepted());
		TestFalse(TEXT("A repeated second-engine execution is rejected"), Second->ExecuteCurrentMoveEffects().WasAccepted());
		TestEqual(
			TEXT("Repeated execution leaves HP unchanged"),
			First->GetSnapshot().FindBattler(MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->CurrentHP,
			FirstHpBeforeDuplicate);
		TestTrue(
			TEXT("Repeated execution leaves status unchanged"),
			First->GetSnapshot().FindObservedBattler(MakeNumericId<FBattlerId>(OpponentLeftBattlerValue))->MajorStatusId
				== FirstStatusBeforeDuplicate);
		TestTrue(
			TEXT("Repeated execution consumes no additional RNG"),
			First->ExportRandomTrace() == FirstTrace);

		const FBattleReplayRecord AfterDuplicate = First->ExportReplayRecord();
		TestEqual(
			TEXT("A rejected duplicate does not duplicate damage"),
			CountResolutionEvents(AfterDuplicate.GetResolutions(), EBattleEventType::Damage),
			1);
		TestEqual(
			TEXT("A rejected duplicate does not duplicate the secondary"),
			CountResolutionEvents(AfterDuplicate.GetResolutions(), EBattleEventType::StatusChanged),
			1);

		TArray<uint8> FirstAfterDuplicateBytes;
		TArray<uint8> FirstAfterDuplicateRepeatBytes;
		TArray<uint8> SecondAfterDuplicateBytes;
		TestTrue(TEXT("The first duplicate-bearing replay serializes"), TrySerializeEngineReplay(*First, FirstAfterDuplicateBytes));
		TestTrue(TEXT("Its repeated export serializes"), TrySerializeEngineReplay(*First, FirstAfterDuplicateRepeatBytes));
		TestTrue(TEXT("The second duplicate-bearing replay serializes"), TrySerializeEngineReplay(*Second, SecondAfterDuplicateBytes));
		TestTrue(
			TEXT("Repeated serialization remains stable after rejection"),
			FirstAfterDuplicateBytes == FirstAfterDuplicateRepeatBytes);
		TestTrue(
			TEXT("Identical duplicate rejection is deterministic"),
			FirstAfterDuplicateBytes == SecondAfterDuplicateBytes);
		return true;
	}
}

#endif
