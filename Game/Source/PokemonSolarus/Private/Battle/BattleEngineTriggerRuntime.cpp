#include "BattleEngineTriggerRuntime.h"

namespace BattleEngineTriggerRuntimePrivate
{
	using namespace BattleEngineCommonPrivate;

	bool TryMakeBattlerTriggerSubject(
		const FBattlerId BattlerId,
		FBattleTriggerSubject& OutOwner)
	{
		return FBattleTriggerSubject::TryCreateBattler(BattlerId, OutOwner);
	}

	bool TrySetVolatileLayers(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const int32 Layers)
	{
		FBattleTriggerOperationContext Operation;
		if (Layers <= 0 || !TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			State.TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& !State.TriggerFramework.TryUpdateLayers(
					Registration.RegistrationId,
					Layers,
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TrySetVolatileSuppressed(
		FBattleEngineState& State,
		const FBattlerId BattlerId,
		const FConditionId& VolatileId,
		const bool bSuppressed)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeTriggerOperationContext(State, Operation))
		{
			return false;
		}
		EBattleTriggerError Error = EBattleTriggerError::None;
		const TArray<FBattleTriggerRegistrationState> Registrations =
			State.TriggerFramework.GetActiveRegistrations();
		for (const FBattleTriggerRegistrationState& Registration : Registrations)
		{
			if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
				&& Registration.Spec.Owner.BattlerId == BattlerId
				&& Registration.Spec.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Condition
				&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
				&& !State.TriggerFramework.TrySetSuppressed(
					Registration.RegistrationId,
					bSuppressed,
					Operation,
					Error))
			{
				return false;
			}
		}
		DrainTriggerOutputs(State);
		return true;
	}

	bool TryTakeStagedTriggerOperationContext(
		FWildActionCleanupStage& Stage,
		FBattleTriggerOperationContext& OutContext)
	{
		OutContext = FBattleTriggerOperationContext();
		if (Stage.NextTriggerReentrancyToken == 0
			|| Stage.NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
			|| !FBattleTriggerReentrancyToken::TryCreate(
				Stage.NextTriggerReentrancyToken,
				OutContext.ReentrancyToken))
		{
			return false;
		}
		++Stage.NextTriggerReentrancyToken;
		return true;
	}

	void DrainStagedTriggerOutputs(FWildActionCleanupStage& Stage)
	{
		TArray<FBattleTriggerEffectRequest> IgnoredRequests;
		TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
		Stage.TriggerFramework.DrainEffectRequests(IgnoredRequests);
		Stage.TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
	}

	bool TryStageMajorStatusCleanup(
		FWildActionCleanupStage& Stage,
		const FConditionId& StatusId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleMajorStatusRules::IsCanonical(StatusId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation)
			|| !FBattleMajorStatusRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				StatusId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageAbilityCleanup(
		FWildActionCleanupStage& Stage,
		const FAbilityId& AbilityId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleAbilityRules::IsCanonical(AbilityId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateAbility(AbilityId, SourceDefinition)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageItemCleanup(
		FWildActionCleanupStage& Stage,
		const FItemId& ItemId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleItemRules::IsCanonical(ItemId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerSourceDefinition SourceDefinition;
		FBattleTriggerOperationContext Operation;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation))
		{
			return false;
		}

		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = Reason;
		Cleanup.AffectedOwners.Add(Owner);
		Cleanup.SourceDefinitionFilter = SourceDefinition;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FConditionId& VolatileId,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		if (!FBattleVolatileRules::IsCanonical(VolatileId))
		{
			return true;
		}
		FBattleTriggerSubject Owner;
		FBattleTriggerOperationContext Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!TryMakeBattlerTriggerSubject(BattlerId, Owner)
			|| !TryTakeStagedTriggerOperationContext(Stage, Operation)
			|| !FBattleVolatileRules::TryCleanupTriggers(
				Stage.TriggerFramework,
				VolatileId,
				Owner,
				Reason,
				Operation,
				Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}

	bool TryStageSourceDependentVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FBattlerId SourceBattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		for (FWildActionStagedVolatiles& Candidate : Stage.BattlerVolatiles)
		{
			TArray<FConditionId> ToRemove;
			for (const FBattleConditionState& Condition : Candidate.Volatiles)
			{
				if (Condition.SourceBattlerId == SourceBattlerId
					&& (Condition.ConditionId == FBattleVolatileRules::GetPartialTrapId()
						|| Condition.ConditionId == FBattleVolatileRules::GetTrapId()))
				{
					ToRemove.Add(Condition.ConditionId);
				}
			}
			for (const FConditionId& Id : ToRemove)
			{
				if (!TryStageVolatileCleanup(Stage, Id, Candidate.BattlerId, Reason))
				{
					return false;
				}
				Candidate.Volatiles.RemoveAll(
					[&Id](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Id;
					});
			}
		}
		return true;
	}

	bool TryStageAllOwnedVolatileCleanup(
		FWildActionCleanupStage& Stage,
		const FBattlerId BattlerId,
		const EBattleTriggerCleanupReason Reason)
	{
		FWildActionStagedVolatiles* Battler = Stage.FindMutableVolatiles(BattlerId);
		if (Battler == nullptr)
		{
			return false;
		}
		TArray<FConditionId> Ids;
		for (const FBattleConditionState& Condition : Battler->Volatiles)
		{
			if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
			{
				Ids.Add(Condition.ConditionId);
			}
		}
		for (const FConditionId& Id : Ids)
		{
			if (!TryStageVolatileCleanup(Stage, Id, BattlerId, Reason))
			{
				return false;
			}
		}
		return true;
	}

	bool TryStageBattleEndCleanup(FWildActionCleanupStage& Stage)
	{
		FBattleTriggerOperationContext Operation;
		if (!TryTakeStagedTriggerOperationContext(Stage, Operation))
		{
			return false;
		}
		FBattleTriggerCleanupRequest Cleanup;
		Cleanup.Reason = EBattleTriggerCleanupReason::BattleEnd;
		Cleanup.Context = Operation;
		EBattleTriggerError Error = EBattleTriggerError::None;
		if (!Stage.TriggerFramework.TryApplyCleanup(Cleanup, Error))
		{
			return false;
		}
		DrainStagedTriggerOutputs(Stage);
		return true;
	}
}
