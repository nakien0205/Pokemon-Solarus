#pragma once

#include "Battle/BattleEffectExecutor.h"

struct FBattleMoveUserHitQualifiers;

namespace BattleEffectExecutorPrivate
{
	class FStateExecutionContext final : public IBattleEffectExecutionContext
	{
	public:
		FStateExecutionContext(
			const FBattleEffectExecutionRequest& InRequest,
			const FBattleEngineState& InState,
			IBattleRandom& InRandom);

		void MovePreparedState(FBattleEffectExecutionPlan& OutPlan);

		void BindExecutionResult(FBattleEffectExecutionResult& InResult);

		bool TryResolveForcedSwitches(
			FBattleEffectExecutionResult& Result,
			EBattleEffectExecutorError& OutError);

		bool TryResolveHeldItemMoveIntents(
			FBattleEffectExecutionResult& Result,
			EBattleEffectExecutorError& OutError);

		bool TryApplyPostMoveLifeOrbRecoil(
			FBattleEffectExecutionResult& Result,
			EBattleEffectExecutorError& OutError);

		virtual bool PrevalidateRequest(
			const FBattleEffectExecutionRequest& CandidateRequest) const override;

		virtual FBattleEffectHookResult CheckReachability(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual FBattleEffectHookResult CheckProtection(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual FBattleEffectHookResult CheckTryHit(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual FBattleEffectHookResult CheckMoveImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual FBattleEffectHookResult CheckAbilityImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual FBattleEffectHookResult CheckItemImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual FBattleEffectHookResult ApplyProtectionBreaking(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override;

		virtual bool TryBuildAccuracyInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleAccuracyCheckInput& OutInput) override;

		virtual bool TryBuildCriticalInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleCriticalCheckInput& OutInput) override;

		virtual bool TryBuildDamageInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			const bool bSpreadAcrossMultipleTargets,
			FBattleFinalDamageInput& OutInput) override;

		virtual void SetDirectMoveDamageHit(const bool bActive) override;

		virtual bool IsRuntimeValid() const override;

		virtual EBattleEffectExecutorError GetRuntimeError() const override;

		virtual bool IsSourceAbleToContinue() const override;

		virtual bool IsTargetAbleToContinue(const FBattleResolvedTarget& Target) const override;

		virtual bool TryShouldSkipEffectDescriptor(
			const FBattleMoveEffectDescriptor& Effect,
			bool& OutShouldSkip) override;

		virtual FBattleEffectHookResult CheckEffectEligibility(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override;

		virtual bool TryGetHp(
			const FBattleResolvedTarget& Target,
			int32& OutCurrentHP,
			int32& OutMaximumHP) const override;

		virtual FBattleEffectHookResult ApplyHpDelta(
			const FBattleResolvedTarget& Target,
			const int32 RequestedDelta) override;

		virtual FBattleEffectHookResult ApplyNonHpEffect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override;

		virtual void RunImmediateUpdate(const FBattleResolvedTarget& Target) override;

		virtual bool TryBuildEventTarget(
			const FBattleResolvedTarget& Target,
			FBattleEventTarget& OutTarget) const override;

	private:
		static FBattleEffectHookResult Applied();

		static FBattleEffectHookResult Outcome(const EBattleEffectExecutionOutcome Value);

		const FBattleBattlerState* FindBattler(const FBattlerId Id) const;

		bool TryResolveMoveUserHitQualifiers(
			const FBattleMoveDefinition& Move,
			FBattleMoveUserHitQualifiers& OutQualifiers) const;

		FBattleBattlerState* FindMutableBattler(const FBattlerId Id);

		const FBattleActivePositionState* FindActiveForBattler(const FBattlerId Id) const;

		FBattleActivePositionState* FindMutableActivePosition(const FActiveSlotId Id);

		FBattleSideState* FindMutableSide(const EBattleSide Side);

		const FBattleSideState* FindSide(const EBattleSide Side) const;

		FConditionId GetWeatherId() const;

		FConditionId GetTerrainId() const;

		bool HasRoom(const FConditionId& RoomId) const;

		bool HasSideCondition(const EBattleSide Side, const FConditionId& ConditionId) const;

		TArray<FConditionId> GetSideConditionIds(const EBattleSide Side) const;

		bool TryIsGrounded(
			const FBattleBattlerState& Battler,
			bool& bOutGrounded,
			const bool bAbilityIgnoredForMove = false,
			bool* bOutLevitateMadeAirborne = nullptr) const;

		bool ShouldIgnoreLevitateForCurrentMove(
			const FBattleBattlerState& Defender) const;

		bool TryApplyHeldItemOperation(
			FBattleBattlerState& Battler,
			const EBattleHeldItemOperationKind Kind,
			const bool bSuppressed,
			FBattleHeldItemOperationFact& OutFact);

		bool TryCleanupItemHooks(
			const FBattleBattlerState& Battler,
			const FItemId& ItemId,
			const EBattleTriggerCleanupReason Reason);

		bool TryRegisterItemHooks(
			const FBattleBattlerState& Battler,
			const FBattleActivePositionState& Active);

		bool TryDispatchItemPhase(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			TArray<FBattleTriggerEffectRequest>& OutRequests);

		bool TryGetItemEffectRequest(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			const EBattleAbilityItemHookPoint HookPoint,
			FBattleAbilityItemEffectRequest& OutRequest);

		bool TryBuildItemEventIdentity(
			const FBattleBattlerState& Battler,
			const FItemId& ItemId,
			FBattleEventSource& OutSource,
			FBattleEventTarget& OutTarget) const;

		bool TryRecordItemActivation(
			const FBattleAbilityItemEffectRequest& RequestToRecord,
			const EBattleAbilityItemActivationOutcome Outcome,
			FBattleBattlerState& SourceBattler,
			const FItemId& ItemId);

		bool TryAppendItemMutationEvent(
			const EBattleEventType Type,
			const FItemId& ItemId,
			const FBattleBattlerState& Battler,
			const int64 Before,
			const int64 After,
			const int64 Delta);

		bool TryConsumeHeldItem(FBattleBattlerState& Battler, const FItemId& ItemId);

		bool TryResolveHeldItemSwitchIn(
			FBattleBattlerState& Battler,
			const FBattleActivePositionState& Active);

		bool TryRunImmediateHeldItemUpdate(FBattleBattlerState& Battler);

		bool TryCleanupAbilityHooks(
			const FBattleBattlerState& Battler,
			const EBattleTriggerCleanupReason Reason);

		bool TryRegisterAbilityHooks(
			const FBattleBattlerState& Battler,
			const FBattleActivePositionState& Active);

		bool TryDispatchAbilityPhase(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			TArray<FBattleTriggerEffectRequest>& OutRequests);

		bool TryGetAbilityEffectRequest(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			const EBattleAbilityItemHookPoint HookPoint,
			FBattleAbilityItemEffectRequest& OutRequest);

		bool TryRecordAbilityActivation(
			const FBattleAbilityItemEffectRequest& RequestToRecord,
			const EBattleAbilityItemActivationOutcome Outcome,
			const FBattleBattlerState& SourceBattler);

		bool TryRecordLevitateGroundedActivation(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			const EBattleAbilityItemHookPoint HookPoint);

		FBattleConditionState MakeCanonicalConditionState(
			const FConditionId& ConditionId,
			const TOptional<int32>& RemainingTurns,
			const int32 Layers);

		bool TryBuildFieldSideOwner(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			FBattleTriggerSubject& OutOwner) const;

		const FBattleConditionState* FindFieldSideConditionState(
			const FBattleTriggerSubject& Owner,
			const FConditionId& ConditionId) const;

		bool TryDispatchFieldSidePhase(
			const FBattleTriggerSubject& Owner,
			const EBattleTriggerPhase Phase,
			const FConditionId& FilterConditionId,
			const TOptional<FActiveSlotId>& ActiveSlotId,
			TArray<FBattleTriggerEffectRequest>& OutRequests);

		bool TryIsFieldSideConditionActiveForPhase(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			const EBattleTriggerPhase Phase,
			const TOptional<FActiveSlotId>& ActiveSlotId,
			bool& bOutActive);

		bool TryRegisterFieldSideCondition(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			const TOptional<int32>& RemainingTurns,
			const int32 Layers);

		bool TryCleanupFieldSideCondition(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			const EBattleTriggerCleanupReason Reason);

		bool TryUpdateFieldSideLayers(
			const FConditionId& ConditionId,
			const EBattleSide Side,
			const int32 Layers);

		bool TrySetMagicRoomSuppression(const bool bSuppressed);

		bool TryApplyEntryHazards(
			FBattleBattlerState& Incoming,
			const FBattleActivePositionState& Active);

		FBattleConditionState MakeConditionState(const FBattleMoveEffectDescriptor& Effect);

		static const FBattleConditionState* FindVolatile(
			const FBattleBattlerState& Battler,
			const FConditionId& VolatileId);

		static FBattleConditionState* FindMutableVolatile(
			FBattleBattlerState& Battler,
			const FConditionId& VolatileId);

		static bool HasVolatile(
			const FBattleBattlerState& Battler,
			const FConditionId& VolatileId);

		bool IsVolatileActiveForPhase(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const EBattleTriggerPhase Phase) const;

		bool TryGetVolatilePayloadMoveId(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			FMoveId& OutMoveId) const;

		bool TryAppendRandomDraw(
			const FBattleResolvedTarget& Target,
			const FBattleRandomDraw& Draw);

		bool TryRegisterVolatile(
			FBattleBattlerState& Battler,
			const FConditionId& VolatileId,
			const FDefinitionId& PayloadId,
			const FBattleTriggerSubject& Source,
			const TArray<FBattleTriggerSubject>& Targets,
			const TOptional<int32>& RemainingTurns,
			const int32 Layers,
			const bool bSuppressed = false);

		bool TryCleanupVolatile(
			const FBattleBattlerState& Battler,
			const FConditionId& VolatileId,
			const EBattleTriggerCleanupReason Reason);

		bool TryCleanupAllOwnedVolatiles(
			const FBattleBattlerState& Battler,
			const EBattleTriggerCleanupReason Reason);

		bool TryCleanupSourceDependentVolatiles(
			const FBattlerId SourceBattlerId,
			const EBattleTriggerCleanupReason Reason);

		bool TrySetVolatileLayers(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const int32 Layers);

		bool TrySetVolatileSuppressed(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const bool bSuppressed);

		bool TrySetVolatilePhaseSuppressed(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const EBattleTriggerPhase Phase,
			const bool bSuppressed);

		bool TryBuildVolatileSource(
			const FConditionId& VolatileId,
			FBattleTriggerSubject& OutSource) const;

		bool HasActedThisTurn(const FBattlerId BattlerId) const;

		int32 GetCurrentPP(const FBattleBattlerState& Battler, const FMoveId MoveId) const;

		FBattleEffectHookResult ApplyCanonicalVolatile(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target,
			FBattleBattlerState& Battler);

		FBattleEffectHookResult ApplySimpleSpecialVolatile(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target,
			const FConditionId& ExpectedId);

		FBattleEffectHookResult ApplyCharge(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult ApplyProtect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult ApplyCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult ApplyStatStage(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult SetFieldCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult SetSideCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult RemoveCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult ApplyTargetRedirectionRegistration(
			const FBattleResolvedTarget& Target);

		FBattleEffectHookResult ApplyAllyActionPowerModifierRegistration(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target);

		bool TryTakeTriggerContext(FBattleTriggerOperationContext& OutContext);

		bool TryDispatchStatusPhase(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			bool& bOutEmitted);

		void DrainTriggerOutputs();

		bool TryCleanupCanonicalStatus(const FBattleBattlerState& Battler);

		bool TryRunSwitchOutStatus(const FBattleBattlerState& Battler);

		void SetRuntimeFailure(const EBattleEffectExecutorError Error);

		const FBattleEffectExecutionRequest& Request;
		const FBattleEngineState& State;
		IBattleRandom& Random;
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
		TArray<FBattleMoveRedirectionRegistration> MoveRedirectionRegistrations;
		TArray<FBattleAllyActionPowerModifierRegistration>
			AllyActionPowerModifierRegistrations;
		FBattleFieldState Field;
		TArray<FBattleSideState> Sides;
		FBattleTriggerFramework TriggerFramework;
		FBattleAbilityItemRevealTracker AbilityItemRevealTracker;
		FBattleHeldItemLedger HeldItemLedger;
		TMap<FBattlerId, int32> DamageInputBuildCounts;
		TSet<FBattlerId> SubstituteProtectedTargets;
		TSet<FBattlerId> PendingDamagingHitConnections;
		TSet<FBattlerId> PendingImmediateItemUpdates;
		FBattleEffectExecutionResult* ExecutionResult = nullptr;
		TOptional<bool> CachedFirstTurnChargeSkip;
		bool bApplyingDirectMoveDamageHit = false;
		bool bMoveAffectedDifferentBattler = false;
		bool bLifeOrbBoostAppliedThisMove = false;
		bool bRuntimeValid = true;
		EBattleEffectExecutorError RuntimeError = EBattleEffectExecutorError::InvalidHookResult;
		uint64 NextConditionCreationOrdinal = 1;
		uint64 NextTriggerReentrancyToken = 1;
	};
}
