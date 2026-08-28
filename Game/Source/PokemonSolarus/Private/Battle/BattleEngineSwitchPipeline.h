#pragma once

#include "Battle/BattleSwitching.h"
#include "BattleEngineCommon.h"
#include "BattleEngineEvents.h"
#include "BattleEngineTriggerRuntime.h"
#include "BattleEntryHazardPrevention.h"
#include "BattleFaintOutcomeResolver.h"
#include "BattleMoveRedirection.h"

namespace BattleEngineSwitchPipelinePrivate
{
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	template <typename TState>
	bool TryFindItemRequestForPhase(
		TState& State,
		const FBattlerId BattlerId,
		const FItemId& ItemId,
		const EBattleTriggerPhase Phase,
		FBattleTriggerEffectRequest& OutRequest)
	{
		OutRequest = FBattleTriggerEffectRequest();
		TArray<FBattleTriggerEffectRequest> Requests;
		const TArray<FBattlerId> Owners{BattlerId};
		if (!TryDispatchItemPhase(State, Owners, Phase, Requests))
		{
			return false;
		}
		const FBattleTriggerEffectRequest* Request = Requests.FindByPredicate(
			[&ItemId](const FBattleTriggerEffectRequest& Candidate)
			{
				return Candidate.SourceDefinition.Kind
					== EBattleTriggerSourceDefinitionKind::Item
					&& Candidate.SourceDefinition.ItemId == ItemId;
			});
		if (Request == nullptr)
		{
			return false;
		}
		OutRequest = *Request;
		return true;
	}

	template <typename TState>
	bool TryResolveImmediateHeldItem(
		TState& State,
		const FBattlerId BattlerId,
		const FResolutionId ResolutionId,
		const FActionId ActionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events)
	{
		FBattleBattlerState* Battler = State.FindMutableBattler(BattlerId);
		const FBattleActivePositionState* Active = Battler != nullptr
			? FindActiveForBattler(State, BattlerId)
			: nullptr;
		if (Battler == nullptr)
		{
			return false;
		}
		if (Active == nullptr)
		{
			return true;
		}
		if (!IsHeldItemActive(*Battler))
		{
			return true;
		}
		const FItemId ItemId = Battler->HeldItem.CurrentItemId;
		if (ItemId == FBattleItemRules::GetSitrusBerryId())
		{
			FBattleItemRecoveryFacts Facts;
			Facts.ItemId = ItemId;
			Facts.CurrentHP = Battler->CurrentHP;
			Facts.BaseMaximumHP = Battler->PermanentStats.MaxHP;
			Facts.bHealingPermitted = Battler->CurrentHP > 0
				&& !Battler->bFainted
				&& !Battler->bCaptured
				&& !Battler->bRemoved;
			Facts.bSuppressed = Battler->HeldItem.bSuppressed;
			FBattleItemRecoveryResult Result;
			if (!FBattleItemRules::TryEvaluateRecovery(Facts, Result))
			{
				return false;
			}
			if (!Result.bApplies)
			{
				return true;
			}
			FBattleTriggerEffectRequest Request;
			TOptional<FBattleAbilityItemActivationFact> Activation;
			if (!TryFindItemRequestForPhase(
					State,
					BattlerId,
					ItemId,
					EBattleTriggerPhase::AfterDamage,
					Request)
				|| !TryRecordItemActivation(
					State,
					Request,
					EBattleAbilityItemActivationOutcome::Applied,
					Activation)
				|| !Activation.IsSet())
			{
				return false;
			}
			const int32 PreviousHP = Battler->CurrentHP;
			const FActiveSlotId ActiveSlotId = Active->ActiveSlotId;
			if (!TryConsumeHeldItem(State, BattlerId))
			{
				return false;
			}
			Battler->CurrentHP += Result.HealAmount;
			Events.Add(MakeItemActivationEvent(
				State,
				ResolutionId,
				ActionId,
				ActionKind,
				Request,
				Activation.GetValue()));
			Events.Add(MakeHeldItemMutationEvent(
				State, ResolutionId, ActionId, ActionKind,
				EBattleEventType::ItemConsumed, BattlerId, ActiveSlotId, ItemId,
				1, 0, -1));
			for (const EBattleEventType Type : {
				EBattleEventType::Healing,
				EBattleEventType::HPChanged})
			{
				Events.Add(MakeHeldItemMutationEvent(
					State, ResolutionId, ActionId, ActionKind,
					Type, BattlerId, ActiveSlotId, ItemId,
					PreviousHP, Battler->CurrentHP, Result.HealAmount));
			}
			return true;
		}

		if (ItemId != FBattleItemRules::GetLumBerryId())
		{
			return true;
		}
		const bool bHasMajorStatus = FBattleMajorStatusRules::IsCanonical(
			Battler->MajorStatusId);
		const bool bHasConfusion = HasVolatile(
			*Battler,
			FBattleVolatileRules::GetConfusionId());
		FBattleLumBerryFacts Facts;
		Facts.ItemId = ItemId;
		Facts.bHolderAbleToBattle = Battler->CurrentHP > 0
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
		Facts.bHasMajorStatus = bHasMajorStatus;
		Facts.bHasConfusion = bHasConfusion;
		Facts.bSuppressed = Battler->HeldItem.bSuppressed;
		FBattleLumBerryResult Result;
		if (!FBattleItemRules::TryEvaluateLumBerry(Facts, Result))
		{
			return false;
		}
		if (!Result.bApplies)
		{
			return true;
		}
		FBattleTriggerEffectRequest Request;
		TOptional<FBattleAbilityItemActivationFact> Activation;
		if (!TryFindItemRequestForPhase(
				State,
				BattlerId,
				ItemId,
				EBattleTriggerPhase::AfterHit,
				Request)
			|| !TryRecordItemActivation(
				State,
				Request,
				EBattleAbilityItemActivationOutcome::Applied,
				Activation)
			|| !Activation.IsSet())
		{
			return false;
		}
		const FActiveSlotId ActiveSlotId = Active->ActiveSlotId;
		const int32 CuredCount = (bHasMajorStatus ? 1 : 0) + (bHasConfusion ? 1 : 0);
		const FConditionId MajorStatusId = Battler->MajorStatusId;
		if (!TryConsumeHeldItem(State, BattlerId)
			|| (bHasMajorStatus
				&& !TryCleanupMajorStatusTriggers(
					State,
					MajorStatusId,
					BattlerId,
					EBattleTriggerCleanupReason::Removal))
			|| (bHasConfusion
				&& !TryCleanupVolatileTriggers(
					State,
					FBattleVolatileRules::GetConfusionId(),
					BattlerId,
					EBattleTriggerCleanupReason::Removal)))
		{
			return false;
		}
		if (bHasMajorStatus)
		{
			Battler->MajorStatusId = FConditionId();
		}
		if (bHasConfusion)
		{
			Battler->Volatiles.RemoveAll(
				[](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == FBattleVolatileRules::GetConfusionId();
				});
		}
		Events.Add(MakeItemActivationEvent(
			State,
			ResolutionId,
			ActionId,
			ActionKind,
			Request,
			Activation.GetValue()));
		Events.Add(MakeHeldItemMutationEvent(
			State, ResolutionId, ActionId, ActionKind,
			EBattleEventType::ItemConsumed, BattlerId, ActiveSlotId, ItemId,
			1, 0, -1));
		Events.Add(MakeHeldItemMutationEvent(
			State, ResolutionId, ActionId, ActionKind,
			EBattleEventType::StatusChanged, BattlerId, ActiveSlotId, ItemId,
			CuredCount, 0, -CuredCount));
		return true;
	}

	template <typename TState>
	bool TryRevealAirBalloonOnEntry(
		TState& State,
		const FBattlerId BattlerId,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events)
	{
		const FBattleBattlerState* Battler = State.FindBattler(BattlerId);
		const FBattleActivePositionState* Active = Battler != nullptr
			? FindActiveForBattler(State, BattlerId)
			: nullptr;
		if (Battler == nullptr || Active == nullptr)
		{
			return false;
		}
		if (!IsHeldItemActive(*Battler)
			|| Battler->HeldItem.CurrentItemId != FBattleItemRules::GetAirBalloonId()
			|| Battler->HeldItem.bSuppressed)
		{
			return true;
		}
		FBattleTriggerEffectRequest Request;
		TOptional<FBattleAbilityItemActivationFact> Activation;
		if (!TryFindItemRequestForPhase(
				State,
				BattlerId,
				Battler->HeldItem.CurrentItemId,
				EBattleTriggerPhase::SwitchIn,
				Request)
			|| !TryRecordItemActivation(
				State,
				Request,
				EBattleAbilityItemActivationOutcome::Applied,
				Activation)
			|| !Activation.IsSet())
		{
			return false;
		}
		FBattleEventTarget Target;
		Target.TrainerId = Battler->TrainerId;
		Target.BattlerId = BattlerId;
		Target.ActiveSlotId = Active->ActiveSlotId;
		Events.Add(MakeItemActivationEvent(
			State,
			ResolutionId,
			FActionId(),
			ActionKind,
			Request,
			Activation.GetValue(),
			&Target));
		return true;
	}

	template <typename TState>
	bool TryResolveAbilityEntries(
		TState& State,
		const TConstArrayView<FBattlerId> Entrants,
		const FResolutionId ResolutionId,
		const EBattleActionKind ActionKind,
		TArray<FBattleEvent>& Events)
	{
		TArray<FBattleTriggerEffectRequest> Requests;
		if (!TryDispatchAbilityPhase(
				State,
				Entrants,
				EBattleTriggerPhase::SwitchIn,
				Requests))
		{
			return false;
		}
		for (const FBattleTriggerEffectRequest& Request : Requests)
		{
			if (Request.SourceDefinition.Kind
				!= EBattleTriggerSourceDefinitionKind::Ability
				|| Request.Owner.Kind != EBattleTriggerSubjectKind::Battler)
			{
				continue;
			}
			FBattleBattlerState* Owner = State.FindMutableBattler(
				Request.Owner.BattlerId);
			const FBattleActivePositionState* OwnerActive = Owner != nullptr
				? FindActiveForBattler(State, Owner->BattlerId)
				: nullptr;
			if (Owner == nullptr
				|| OwnerActive == nullptr
				|| Owner->CurrentHP <= 0
				|| Owner->bFainted
				|| Owner->bCaptured
				|| Owner->bRemoved
				|| Owner->AbilityId != Request.SourceDefinition.AbilityId)
			{
				continue;
			}

			const EBattleAbilityKind Kind = FBattleAbilityRules::GetKind(Owner->AbilityId);
			if (Kind == EBattleAbilityKind::Intimidate)
			{
				struct FPendingIntimidateMutation
				{
					FBattleEventTarget Target;
					FBattleStatStageChangeResult Change;
				};
				EBattleAbilityItemActivationOutcome OverallOutcome =
					EBattleAbilityItemActivationOutcome::Ineligible;
				TArray<FPendingIntimidateMutation> Mutations;
				for (const FBattleTriggerSubject& TargetSubject : Request.Targets)
				{
					FBattleBattlerState* Target =
						TargetSubject.Kind == EBattleTriggerSubjectKind::Battler
							? State.FindMutableBattler(TargetSubject.BattlerId)
							: nullptr;
					const FBattleActivePositionState* TargetActive = Target != nullptr
						? FindActiveForBattler(State, Target->BattlerId)
						: nullptr;
					if (Target == nullptr || TargetActive == nullptr)
					{
						continue;
					}
					bool bMistActive = false;
					if (!TryIsFieldSideConditionActiveForPhase(
							State,
							FBattleFieldSideConditionRules::GetMistId(),
							TargetActive->ActiveSlotId.GetSide(),
							EBattleTriggerPhase::BeforeHit,
							TargetActive->ActiveSlotId,
							bMistActive))
					{
						return false;
					}
					FBattleIntimidateTargetFacts Facts;
					Facts.bAdjacentOpponent = TargetActive->ActiveSlotId.GetSide()
						!= OwnerActive->ActiveSlotId.GetSide();
					Facts.bTargetAbleToBattle = Target->CurrentHP > 0
						&& !Target->bFainted
						&& !Target->bCaptured
						&& !Target->bRemoved;
					Facts.bSubstituteActive = HasVolatile(
						*Target,
						FBattleVolatileRules::GetSubstituteId());
					Facts.bStatStageDropPrevented =
						FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
							bMistActive,
							true,
							false,
							-1);
					if (!Target->Stages.TryGetStage(
							EBattleStat::Attack,
							Facts.CurrentAttackStage))
					{
						return false;
					}
					Facts.bSuppressed = Owner->bAbilitySuppressed;
					FBattleIntimidateTargetResult Result;
					if (!FBattleAbilityRules::TryEvaluateIntimidateTarget(Facts, Result))
					{
						return false;
					}
					if (Result.Outcome == EBattleAbilityItemActivationOutcome::Applied)
					{
						OverallOutcome = EBattleAbilityItemActivationOutcome::Applied;
						const FBattleStatStageChangeResult Change = Target->Stages.ApplyChange(
							EBattleStat::Attack,
							Result.AttackStageDelta);
						if (Change.Outcome != EBattleStatStageChangeOutcome::Applied)
						{
							return false;
						}
						FPendingIntimidateMutation& Mutation =
							Mutations.AddDefaulted_GetRef();
						Mutation.Target.TrainerId = Target->TrainerId;
						Mutation.Target.BattlerId = Target->BattlerId;
						Mutation.Target.ActiveSlotId = TargetActive->ActiveSlotId;
						Mutation.Change = Change;
					}
					else if (Result.Outcome
						== EBattleAbilityItemActivationOutcome::AttemptedButPrevented
						&& OverallOutcome != EBattleAbilityItemActivationOutcome::Applied)
					{
						OverallOutcome = Result.Outcome;
					}
				}
				TOptional<FBattleAbilityItemActivationFact> Fact;
				if (!TryRecordAbilityActivation(State, Request, OverallOutcome, Fact))
				{
					return false;
				}
				if (Fact.IsSet())
				{
					Events.Add(MakeAbilityActivationEvent(
						State,
						ResolutionId,
						FActionId(),
						ActionKind,
						Request,
						Fact.GetValue()));
				}
				FBattleEventSource Source;
				Source.TrainerId = Owner->TrainerId;
				Source.BattlerId = Owner->BattlerId;
				Source.ActiveSlotId = OwnerActive->ActiveSlotId;
				Source.DefinitionId = Owner->AbilityId.GetDefinitionId();
				for (const FPendingIntimidateMutation& Mutation : Mutations)
				{
					Events.Add(MakeRuleMutationEvent(
						State,
						ResolutionId,
						EBattleEventType::StatStageChanged,
						ActionKind,
						Source,
						Mutation.Target,
						Mutation.Change.PreviousStage,
						Mutation.Change.NewStage,
						Mutation.Change.AppliedDelta));
				}
				continue;
			}

			if (Kind == EBattleAbilityKind::Drizzle)
			{
				const FConditionId ExistingWeather = State.Field.Weather.IsSet()
					? State.Field.Weather.GetValue().ConditionId
					: FConditionId();
				FBattleDrizzleEntryResult Drizzle;
				if (!FBattleAbilityRules::TryEvaluateDrizzleEntry(
						Owner->AbilityId,
						ExistingWeather,
						Owner->bAbilitySuppressed,
						Drizzle))
				{
					return false;
				}
				if (Drizzle.Outcome != EBattleAbilityItemActivationOutcome::Applied)
				{
					TOptional<FBattleAbilityItemActivationFact> IgnoredFact;
					if (!TryRecordAbilityActivation(
							State,
							Request,
							Drizzle.Outcome,
							IgnoredFact))
					{
						return false;
					}
					continue;
				}

				FBattleFieldSideApplicationFacts ApplicationFacts;
				ApplicationFacts.RequestedConditionId = Drizzle.RainId;
				if (ExistingWeather.IsValid())
				{
					ApplicationFacts.ExistingExclusiveConditionId = ExistingWeather;
				}
				ApplicationFacts.bRequestedAlreadyActive = ExistingWeather == Drizzle.RainId;
				FBattleFieldSideApplicationResult Application;
				if (!FBattleFieldSideConditionRules::TryEvaluateApplication(
						ApplicationFacts,
						Application)
					|| (Application.Outcome != EBattleFieldSideApplicationOutcome::Create
						&& Application.Outcome
							!= EBattleFieldSideApplicationOutcome::ReplaceExclusive))
				{
					return false;
				}
				if (ExistingWeather.IsValid()
					&& FBattleFieldSideConditionRules::IsCanonical(ExistingWeather)
					&& !TryCleanupFieldSideTriggers(
						State,
						ExistingWeather,
						TOptional<EBattleSide>(),
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				FBattleTriggerSubject SourceSubject;
				if (!TryMakeBattlerTriggerSubject(Owner->BattlerId, SourceSubject))
				{
					return false;
				}
				FBattleFieldSideTriggerRegistrationFacts TriggerFacts;
				TriggerFacts.ConditionId = Drizzle.RainId;
				TriggerFacts.PayloadId = Drizzle.RainId.GetDefinitionId();
				TriggerFacts.Owner = FBattleTriggerSubject::CreateField();
				TriggerFacts.Source = SourceSubject;
				TriggerFacts.RemainingTurns = Drizzle.DurationTurns;
				TriggerFacts.Layers = 1;
				EBattleTriggerError TriggerError = EBattleTriggerError::None;
				if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
						State.TriggerFramework,
						TriggerFacts,
						TriggerError))
				{
					return false;
				}
				DrainTriggerOutputs(State);
				FBattleConditionState Rain;
				Rain.ConditionId = Drizzle.RainId;
				Rain.RemainingTurns = Drizzle.DurationTurns;
				Rain.LayerCount = 1;
				Rain.CreationOrdinal = State.NextConditionCreationOrdinal++;
				Rain.SourceBattlerId = Owner->BattlerId;
				State.Field.Weather = Rain;

				TOptional<FBattleAbilityItemActivationFact> Fact;
				if (!TryRecordAbilityActivation(
						State,
						Request,
						EBattleAbilityItemActivationOutcome::Applied,
						Fact)
					|| !Fact.IsSet())
				{
					return false;
				}
				Events.Add(MakeAbilityActivationEvent(
					State,
					ResolutionId,
					FActionId(),
					ActionKind,
					Request,
					Fact.GetValue()));
				FBattleEventSource Source;
				Source.TrainerId = Owner->TrainerId;
				Source.BattlerId = Owner->BattlerId;
				Source.ActiveSlotId = OwnerActive->ActiveSlotId;
				Source.DefinitionId = Owner->AbilityId.GetDefinitionId();
				FBattleEventTarget FieldTarget;
				FieldTarget.bField = true;
				Events.Add(MakeRuleMutationEvent(
					State,
					ResolutionId,
					EBattleEventType::FieldEffectChanged,
					ActionKind,
					Source,
					FieldTarget,
					ExistingWeather.IsValid() ? 1 : 0,
					1,
					ExistingWeather.IsValid() ? 0 : 1));
				continue;
			}

			if (Kind == EBattleAbilityKind::MoldBreaker)
			{
				TOptional<FBattleAbilityItemActivationFact> Fact;
				const EBattleAbilityItemActivationOutcome Outcome = Owner->bAbilitySuppressed
					? EBattleAbilityItemActivationOutcome::Suppressed
					: EBattleAbilityItemActivationOutcome::Applied;
				if (!TryRecordAbilityActivation(State, Request, Outcome, Fact))
				{
					return false;
				}
				if (Fact.IsSet())
				{
					Events.Add(MakeAbilityActivationEvent(
						State,
						ResolutionId,
						FActionId(),
						ActionKind,
						Request,
						Fact.GetValue()));
				}
			}
		}
		return true;
	}

	template <typename TState>
	bool TryResolveEntryHazards(
		TState& State,
		const FBattlerId IncomingBattlerId,
		const FActiveSlotId ActiveSlotId,
		const FResolutionId ResolutionId,
		TArray<FBattleEvent>& Events)
	{
		FBattleBattlerState* Incoming = State.FindMutableBattler(IncomingBattlerId);
		const FBattleActivePositionState* Active = State.FindActivePosition(ActiveSlotId);
		FBattleSideState* Side = FindMutableSide(State, ActiveSlotId.GetSide());
		const FBattleSpeciesFormDefinition* Species = Incoming != nullptr
			? State.Catalog.FindSpeciesForm(Incoming->SpeciesFormId)
			: nullptr;
		if (Incoming == nullptr
			|| Active == nullptr
			|| Active->BattlerId != IncomingBattlerId
			|| Side == nullptr
			|| Species == nullptr)
		{
			return false;
		}

		FBattleTriggerSubject SideOwner;
		TArray<FBattleTriggerEffectRequest> HazardRequests;
		TArray<FBattleTriggerLifecycleFact> HazardFacts;
		if (!FBattleTriggerSubject::TryCreateSide(ActiveSlotId.GetSide(), SideOwner)
			|| !TryDispatchFieldSidePhase(
				State,
				SideOwner,
				EBattleTriggerPhase::SwitchIn,
				FConditionId(),
				ActiveSlotId,
				HazardRequests,
				HazardFacts))
		{
			return false;
		}
		for (const FBattleTriggerEffectRequest& HazardRequest : HazardRequests)
		{
			if (Incoming->CurrentHP <= 0 || Incoming->bFainted || State.Phase == EBattlePhase::Terminal)
			{
				break;
			}
			if (HazardRequest.Phase != EBattleTriggerPhase::SwitchIn
				|| HazardRequest.SourceDefinition.Kind
					!= EBattleTriggerSourceDefinitionKind::Condition)
			{
				return false;
			}
			const FConditionId HazardId = HazardRequest.SourceDefinition.ConditionId;
			const FBattleConditionState* Hazard = Side->Hazards.FindByPredicate(
				[&HazardId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == HazardId;
				});
			if (Hazard == nullptr
				|| !FBattleFieldSideConditionRules::IsCanonical(HazardId))
			{
				continue;
			}

			bool bGrounded = false;
			bool bLevitateMadeAirborne = false;
			if (!TryResolveGrounded(
					State,
					*Incoming,
					bGrounded,
					&bLevitateMadeAirborne))
			{
				return false;
			}
			FBattleTypeEffectiveness RockEffectiveness{1, 1};
			if (HazardId == FBattleFieldSideConditionRules::GetStealthRockId())
			{
				const bool bTypeFound = Species->SecondaryType == EPokemonType::Invalid
					? State.Catalog.GetTypeChart().TryGetEffectiveness(
						EPokemonType::Rock,
						Species->PrimaryType,
						RockEffectiveness)
					: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
						EPokemonType::Rock,
						Species->PrimaryType,
						Species->SecondaryType,
						RockEffectiveness);
				if (!bTypeFound)
				{
					return false;
				}
			}

			const FConditionId HazardStatusId = HazardRequest.Layers >= 2
				? FBattleMajorStatusRules::GetToxicId()
				: FBattleMajorStatusRules::GetPoisonId();
			const FBattleBattlerState* HazardSourceBattler =
				State.FindBattler(HazardRequest.Source.BattlerId);
			const FBattleTrainerState* HazardSourceTrainer = HazardSourceBattler != nullptr
				? State.FindTrainer(HazardSourceBattler->TrainerId)
				: nullptr;
			const bool bHazardAppliedByOpponent = HazardSourceTrainer == nullptr
				|| HazardSourceTrainer->Side != ActiveSlotId.GetSide();
			FBattleMajorStatusApplicationFacts StatusFacts;
			StatusFacts.RequestedStatusId = HazardStatusId;
			StatusFacts.ExistingMajorStatusId = Incoming->MajorStatusId;
			StatusFacts.PrimaryType = Species->PrimaryType;
			StatusFacts.SecondaryType = Species->SecondaryType;
			const FConditionId TerrainId = State.Field.Terrain.IsSet()
				? State.Field.Terrain.GetValue().ConditionId
				: FConditionId();
			bool bTerrainTriggerActive = false;
			if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
				&& !TryIsFieldSideConditionActiveForPhase(
						State,
						TerrainId,
						TOptional<EBattleSide>(),
						EBattleTriggerPhase::BeforeHit,
						ActiveSlotId,
						bTerrainTriggerActive))
			{
				return false;
			}
			StatusFacts.Prevention.bTerrainPrevents =
				bTerrainTriggerActive
				&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
					TerrainId,
					HazardStatusId,
					bGrounded);
			bool bSafeguardTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					State,
					FBattleFieldSideConditionRules::GetSafeguardId(),
					ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					ActiveSlotId,
					bSafeguardTriggerActive))
			{
				return false;
			}
			StatusFacts.Prevention.bSafeguardPrevents =
				FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
					bSafeguardTriggerActive,
					bHazardAppliedByOpponent,
					false);
			FBattleMajorStatusApplicationResult StatusApplication;
			if (!FBattleMajorStatusRules::TryEvaluateApplication(StatusFacts, StatusApplication))
			{
				return false;
			}

			FBattleHazardSwitchInFacts Facts;
			Facts.HazardId = HazardId;
			Facts.Layers = HazardRequest.Layers;
			Facts.BaseMaximumHP = Incoming->PermanentStats.MaxHP;
			Facts.CurrentHP = Incoming->CurrentHP;
			Facts.PrimaryType = Species->PrimaryType;
			Facts.SecondaryType = Species->SecondaryType;
			Facts.bGrounded = bGrounded;
			Facts.bMajorStatusPrevented = StatusApplication.Outcome
				!= EBattleMajorStatusApplicationOutcome::CanApply;
			bool bMistTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					State,
					FBattleFieldSideConditionRules::GetMistId(),
					ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					ActiveSlotId,
					bMistTriggerActive))
			{
				return false;
			}
			Facts.bStatStageDropPrevented =
				FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
					bMistTriggerActive,
					bHazardAppliedByOpponent,
					false,
					-1);
			Facts.RockEffectiveness = RockEffectiveness;
			const bool bBootsBypassActive = IsHeldItemActive(*Incoming)
				&& FBattleItemRules::ShouldBypassEntryHazards(
					Incoming->HeldItem.CurrentItemId,
					Incoming->HeldItem.bSuppressed);
			const bool bDamagingHazardWouldApply =
				(HazardId == FBattleFieldSideConditionRules::GetSpikesId()
					&& bGrounded)
				|| (HazardId == FBattleFieldSideConditionRules::GetStealthRockId()
					&& !RockEffectiveness.IsImmune());
			const bool bMagicGuardWouldPreventDamage = bDamagingHazardWouldApply
				&& FBattleAbilityRules::ShouldMagicGuardPreventDamage(
					Incoming->AbilityId,
					EBattleHPChangeSourceKind::Condition,
					Incoming->bAbilitySuppressed);
			const BattleEntryHazardPrevention::FResult Prevention =
				BattleEntryHazardPrevention::Resolve(
					bBootsBypassActive,
					bMagicGuardWouldPreventDamage);
			Facts.bBypassesEntryHazards = Prevention.bBypassesEntryHazards;
			Facts.bIndirectDamagePrevented = Prevention.bIndirectDamagePrevented;
			FBattleHazardSwitchInResult HazardResult;
			if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(Facts, HazardResult))
			{
				return false;
			}
			const bool bMagicGuardPreventedDamage = Facts.bIndirectDamagePrevented;
			bool bLevitatePreventedHazard = false;
			if (bLevitateMadeAirborne)
			{
				FBattleHazardSwitchInFacts GroundedFacts = Facts;
				GroundedFacts.bGrounded = true;
				FBattleHazardSwitchInResult GroundedResult;
				if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
						GroundedFacts,
						GroundedResult))
				{
					return false;
				}
				bLevitatePreventedHazard =
					HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::None
					&& GroundedResult.EffectKind != EBattleHazardSwitchInEffectKind::None;
			}

			FBattleEventSource Source;
			Source.DefinitionId = HazardId.GetDefinitionId();
			if (HazardSourceBattler != nullptr)
			{
				Source.TrainerId = HazardSourceBattler->TrainerId;
				Source.BattlerId = HazardSourceBattler->BattlerId;
				const FBattleActivePositionState* SourceActive = FindActiveForBattler(
					State,
					HazardSourceBattler->BattlerId);
				if (SourceActive != nullptr)
				{
					Source.ActiveSlotId = SourceActive->ActiveSlotId;
				}
			}
			FBattleEventTarget Target;
			Target.TrainerId = Incoming->TrainerId;
			Target.BattlerId = Incoming->BattlerId;
			Target.ActiveSlotId = ActiveSlotId;
			if (bLevitatePreventedHazard
				&& !TryAppendAbilityActivationForPhase(
					State,
					Incoming->BattlerId,
					EBattleTriggerPhase::SwitchIn,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					Events,
					&Target))
			{
				return false;
			}
			if (bMagicGuardPreventedDamage
				&& !TryAppendAbilityActivationForPhase(
					State,
					Incoming->BattlerId,
					EBattleTriggerPhase::SwitchIn,
					EBattleAbilityItemActivationOutcome::Applied,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					Events,
					&Target))
			{
				return false;
			}

			FBattleEffectExecutionResult FaintInput;
			FaintInput.bValid = true;
			if (HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::Damage)
			{
				const int32 PreviousHP = Incoming->CurrentHP;
				const int32 AppliedDamage = FMath::Min(PreviousHP, HazardResult.Damage);
				Incoming->CurrentHP -= AppliedDamage;
				if (Incoming->CurrentHP == 0)
				{
					Incoming->bFainted = true;
					Incoming->bFaintTransitionPending = true;
				}
				for (const EBattleEventType Type : {EBattleEventType::Damage, EBattleEventType::HPChanged})
				{
					Events.Add(MakeRuleMutationEvent(
						State,
						ResolutionId,
						Type,
						EBattleActionKind::Switch,
						Source,
						Target,
						PreviousHP,
						Incoming->CurrentHP,
						-AppliedDamage));
					FBattleEffectExecutionEvent& Record = FaintInput.Events.AddDefaulted_GetRef();
					Record.Type = Type;
					Record.Cause = EBattleEventCause::Rule;
					Record.Outcome = EBattleEffectExecutionOutcome::Applied;
					Record.Targets.Add(Target);
					Record.NumericBefore = PreviousHP;
					Record.NumericAfter = Incoming->CurrentHP;
					Record.NumericDelta = -AppliedDamage;
				}
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::ApplyMajorStatus)
			{
				FBattleTriggerSubject Owner;
				EBattleTriggerError Error = EBattleTriggerError::None;
				if (!FBattleTriggerSubject::TryCreateBattler(Incoming->BattlerId, Owner)
					|| !FBattleMajorStatusRules::TryRegisterTriggers(
						State.TriggerFramework,
						HazardResult.MajorStatusId,
						Owner,
						TOptional<int32>(),
						Error))
				{
					return false;
				}
				DrainTriggerOutputs(State);
				Incoming->MajorStatusId = HazardResult.MajorStatusId;
				Events.Add(MakeRuleMutationEvent(
					State,
					ResolutionId,
					EBattleEventType::StatusChanged,
					EBattleActionKind::Switch,
					Source,
					Target,
					0,
					1,
					1));
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::ModifyStatStage)
			{
				const FBattleStatStageChangeResult Change = Incoming->Stages.ApplyChange(
					HazardResult.Stat,
					HazardResult.StatStageDelta);
				if (Change.Outcome == EBattleStatStageChangeOutcome::Applied)
				{
					Events.Add(MakeRuleMutationEvent(
						State,
						ResolutionId,
						EBattleEventType::StatStageChanged,
						EBattleActionKind::Switch,
						Source,
						Target,
						Change.PreviousStage,
						Change.NewStage,
						Change.AppliedDelta));
				}
			}
			else if (HazardResult.EffectKind
				== EBattleHazardSwitchInEffectKind::RemoveHazard)
			{
				const int32 PreviousLayers = HazardRequest.Layers;
				if (!TryCleanupFieldSideTriggers(
						State,
						HazardId,
						ActiveSlotId.GetSide(),
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				Side->Hazards.RemoveAll(
					[&HazardId](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == HazardId;
					});
				FBattleEventTarget SideTarget;
				SideTarget.Side = ActiveSlotId.GetSide();
				SideTarget.bHasSide = true;
				Events.Add(MakeRuleMutationEvent(
					State,
					ResolutionId,
					EBattleEventType::FieldEffectChanged,
					EBattleActionKind::Switch,
					Source,
					SideTarget,
					PreviousLayers,
					0,
					-PreviousLayers));
			}

			if (!TryResolveImmediateHeldItem(
					State,
					Incoming->BattlerId,
					ResolutionId,
					FActionId(),
					EBattleActionKind::Switch,
					Events))
			{
				return false;
			}

			if (Incoming->bFaintTransitionPending)
			{
				const FConditionId PendingStatus = Incoming->MajorStatusId;
				TArray<FConditionId> PendingVolatiles;
				for (const FBattleConditionState& Condition : Incoming->Volatiles)
				{
					if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
					{
						PendingVolatiles.Add(Condition.ConditionId);
					}
				}
				Incoming->LastMoveId = FMoveId();
				if (!TryCleanupSourceDependentVolatiles(
						State,
						Incoming->BattlerId,
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				FBattleFaintOutcomePlan FaintPlan;
				if (!FBattleFaintOutcomeResolver::TryResolveAction(
						FaintInput,
						EBattleTargetClass::SelectedOpponent,
						ResolutionId,
						State.Battlers,
						State.ActivePositions,
						State.MoveRedirectionRegistrations,
						State.CompiledEncounterPolicies,
						FaintPlan)
					|| !FBattleFaintOutcomeResolver::TryApplyActionPlan(
						State.Battlers,
						State.ActivePositions,
						State.MoveRedirectionRegistrations,
						State.Phase,
						State.Outcome,
						State.OutcomeCause,
						State.PendingDecision,
						State.PendingDecisionRequests,
						FaintPlan))
				{
					return false;
				}
				const FBattleFaintOutcomeResolution& FaintResolution =
					FaintPlan.Resolution;
				if (!FaintResolution.Removals.IsEmpty())
				{
					if (!TryCleanupAbilityTriggers(
							State,
							Incoming->AbilityId,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint)
						|| !TryCleanupItemTriggers(
							State,
							Incoming->HeldItem.CurrentItemId,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint)
						|| (FBattleMajorStatusRules::IsCanonical(PendingStatus)
						&& !TryCleanupMajorStatusTriggers(
							State,
							PendingStatus,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint)))
					{
						return false;
					}
					Incoming->bAbilitySuppressed = false;
					Incoming->EnteredActiveOnTurnId = FTurnId();
					for (const FConditionId& VolatileId : PendingVolatiles)
					{
						if (!TryCleanupVolatileTriggers(
							State,
							VolatileId,
							IncomingBattlerId,
							EBattleTriggerCleanupReason::Faint))
						{
							return false;
						}
					}
				}
				for (const FBattleFaintTransitionRecord& Faint : FaintResolution.Faints)
				{
					Events.Add(MakeTargetedActionlessEvent(
						State,
						ResolutionId,
						EBattleEventType::Fainted,
						EBattleEventCause::Rule,
						EBattleActionKind::Switch,
						Source,
						Faint.Target));
				}
				for (const FBattleFaintTransitionRecord& Removal : FaintResolution.Removals)
				{
					Events.Add(MakeTargetedActionlessEvent(
						State,
						ResolutionId,
						EBattleEventType::LeftActiveSlot,
						EBattleEventCause::Rule,
						EBattleActionKind::Switch,
						Source,
						Removal.Target));
					Events.Add(MakeTargetedActionlessEvent(
						State,
						ResolutionId,
						EBattleEventType::Removed,
						EBattleEventCause::Rule,
						EBattleActionKind::Switch,
						Source,
						Removal.Target));
					if (Removal.Target.ActiveSlotId.GetSide() == EBattleSide::Opponent)
					{
						FBattleEvent Checkpoint = MakeTargetedActionlessEvent(
							State,
							ResolutionId,
							EBattleEventType::OpponentRemovalCheckpoint,
							EBattleEventCause::Rule,
							EBattleActionKind::Switch,
							Source,
							Removal.Target);
						State.AvailableOpponentRemovalCheckpoints.Add(
							Checkpoint.GetEventOrdinal());
						Events.Add(MoveTemp(Checkpoint));
					}
				}
				if (FaintResolution.bBattleEnded)
				{
					AppendPartnerTeamVictoryRecoveryEvent(
						State,
						ResolutionId,
						FActionId(),
						EBattleActionKind::Switch,
						Source,
						FaintResolution,
						Events);
					Events.Add(MakeEvent(
						State,
						ResolutionId,
						FActionId(),
						EBattleEventType::BattleEnded,
						EBattleEventCause::Outcome,
						EBattleActionKind::Switch,
						FaintResolution.OutcomeCause,
						Source));
				}
			}
		}
		return true;
	}

	FDefinitionId GetWildOpponentSwitchRestrictionRuleId();

	template <typename TState>
	bool TryBuildSwitchLegality(
		const TState& State,
		const EBattleSwitchKind Kind,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const FActiveSlotId ActiveSlotId,
		const TConstArrayView<FPartySlotId> ReservedPartySlots,
		FBattleSwitchLegalityResult& OutLegality)
	{
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		const FBattleActivePositionState* Active = State.FindActivePosition(ActiveSlotId);
		const bool bReplacement = Kind == EBattleSwitchKind::Replacement;
		const FBattleBattlerState* Battler = bReplacement
			? nullptr
			: State.FindBattler(BattlerId);
		if (Trainer == nullptr || Active == nullptr)
		{
			return false;
		}
		if (bReplacement)
		{
			if (BattlerId.IsValid()
				|| !Active->bAvailable
				|| Active->ActiveSlotId.GetSide() != Trainer->Side
				|| Active->TrainerId.IsValid()
				|| Active->BattlerId.IsValid())
			{
				return false;
			}
		}
		else if (Battler == nullptr
			|| Battler->TrainerId != Trainer->TrainerId
			|| Active->TrainerId != Trainer->TrainerId
			|| Active->BattlerId != Battler->BattlerId)
		{
			return false;
		}

		FBattleSwitchLegalitySpec Spec;
		Spec.Kind = Kind;
		Spec.ActingTrainerId = Trainer->TrainerId;
		Spec.ActingBattlerId = bReplacement ? FBattlerId() : Battler->BattlerId;
		Spec.ActiveSlotId = Active->ActiveSlotId;
		Spec.TransferPolicy = EBattleSwitchStateTransferPolicy::ClearTransient;
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			FindTrainerEncounterPolicy(State, Trainer->TrainerId);
		if (TrainerPolicy == nullptr)
		{
			return false;
		}
		Spec.Blockers.bEncounterPolicyAllows = Kind != EBattleSwitchKind::Voluntary
			|| TrainerPolicy->bMayVoluntarilySwitch;
		if (!Spec.Blockers.bEncounterPolicyAllows)
		{
			Spec.Blockers.EncounterPolicyRuleId = GetWildOpponentSwitchRestrictionRuleId();
		}
		if (Kind == EBattleSwitchKind::Voluntary && Battler != nullptr)
		{
			const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
				Battler->SpeciesFormId);
			if (Species == nullptr)
			{
				return false;
			}
			for (const FBattleConditionState& Condition : Battler->Volatiles)
			{
				if (Condition.ConditionId != FBattleVolatileRules::GetPartialTrapId()
					&& Condition.ConditionId != FBattleVolatileRules::GetTrapId())
				{
					continue;
				}
				const FBattleBattlerState* Source = State.FindBattler(
					Condition.SourceBattlerId);
				const FBattleActivePositionState* SourceActive = Source != nullptr
					? State.ActivePositions.FindByPredicate(
						[Source](const FBattleActivePositionState& Position)
						{
							return Position.BattlerId == Source->BattlerId;
						})
					: nullptr;
				const bool bSourceActiveAndLiving = Source != nullptr
					&& SourceActive != nullptr
					&& Source->CurrentHP > 0
					&& !Source->bFainted
					&& !Source->bCaptured
					&& !Source->bRemoved;
				if (FBattleVolatileRules::ShouldBlockVoluntarySwitch(
						Condition.ConditionId,
						Species->PrimaryType,
						Species->SecondaryType,
						bSourceActiveAndLiving,
						true))
				{
					Spec.Blockers.bTrapped = true;
					Spec.Blockers.TrappingRuleId = Condition.ConditionId.GetDefinitionId();
					break;
				}
			}
		}

		Spec.Candidates.Reserve(Trainer->PartySlots.Num());
		for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
		{
			FBattleSwitchCandidateFacts Candidate;
			Candidate.PartySlotId = PartySlot.PartySlotId;
			Candidate.bOccupied = PartySlot.BattlerId.IsValid();
			if (Candidate.bOccupied)
			{
				const FBattleBattlerState* CandidateBattler = State.FindBattler(PartySlot.BattlerId);
				if (CandidateBattler == nullptr)
				{
					return false;
				}
				Candidate.TrainerId = CandidateBattler->TrainerId;
				Candidate.BattlerId = CandidateBattler->BattlerId;
				Candidate.bAlreadyActive = FindActiveForBattler(State, CandidateBattler->BattlerId) != nullptr;
				Candidate.bFainted = CandidateBattler->CurrentHP <= 0 || CandidateBattler->bFainted;
				Candidate.bEgg = CandidateBattler->bEgg;
				Candidate.bCaptured = CandidateBattler->bCaptured;
				Candidate.bRemoved = CandidateBattler->bRemoved;
				Candidate.bAlreadyReserved = ReservedPartySlots.Contains(PartySlot.PartySlotId);
			}
			Spec.Candidates.Add(MoveTemp(Candidate));
		}
		return FBattleSwitchResolver::TryBuildLegality(Spec, OutLegality);
	}

	template <typename TState>
	bool TryApplySwitchSelection(
		TState& State,
		const FTrainerId TrainerId,
		const FBattlerId OutgoingBattlerId,
		const FActiveSlotId ActiveSlotId,
		const FBattleSwitchResolution& Resolution,
		FBattleEventTarget& OutOutgoingTarget,
		FBattleEventTarget& OutIncomingTarget)
	{
		OutOutgoingTarget = FBattleEventTarget();
		OutIncomingTarget = FBattleEventTarget();
		if (!Resolution.IsValid() || !Resolution.HasSelection())
		{
			return false;
		}
		FBattleActivePositionState* Active = State.FindMutableActivePosition(ActiveSlotId);
		FBattleBattlerState* Outgoing = State.FindMutableBattler(OutgoingBattlerId);
		FBattleBattlerState* Incoming = State.FindMutableBattler(Resolution.GetSelectedBattlerId());
		const FBattleTrainerState* Trainer = State.FindTrainer(TrainerId);
		if (Active == nullptr
			|| Outgoing == nullptr
			|| Incoming == nullptr
			|| Trainer == nullptr
			|| !Active->bAvailable
			|| Active->TrainerId != Trainer->TrainerId
			|| Active->BattlerId != Outgoing->BattlerId
			|| Outgoing->TrainerId != Trainer->TrainerId
			|| Incoming->TrainerId != Trainer->TrainerId
			|| Incoming->PartySlotId != Resolution.GetSelectedPartySlotId())
		{
			return false;
		}
		if (!TryRunToxicSwitchOut(State, *Outgoing))
		{
			return false;
		}
		if (!TryCleanupAbilityTriggers(
				State,
				Outgoing->AbilityId,
				Outgoing->BattlerId,
				EBattleTriggerCleanupReason::Switch)
			|| !TryCleanupItemTriggers(
				State,
				Outgoing->HeldItem.CurrentItemId,
				Outgoing->BattlerId,
				EBattleTriggerCleanupReason::Switch)
			|| !TryCleanupAllOwnedVolatileTriggers(
				State,
				*Outgoing,
				EBattleTriggerCleanupReason::Switch)
			|| !TryCleanupSourceDependentVolatiles(
				State,
				Outgoing->BattlerId,
				EBattleTriggerCleanupReason::Removal))
		{
			return false;
		}

		OutOutgoingTarget.TrainerId = Outgoing->TrainerId;
		OutOutgoingTarget.BattlerId = Outgoing->BattlerId;
		OutOutgoingTarget.ActiveSlotId = Active->ActiveSlotId;
		OutIncomingTarget.TrainerId = Incoming->TrainerId;
		OutIncomingTarget.BattlerId = Incoming->BattlerId;
		OutIncomingTarget.ActiveSlotId = Active->ActiveSlotId;
		Outgoing->Stages = FBattleStatStages();
		Outgoing->Volatiles.Reset();
		Outgoing->LastMoveId = FMoveId();
		Outgoing->bAbilitySuppressed = false;
		Outgoing->HeldItem.ChoiceLockedMoveId = FMoveId();
		Outgoing->EnteredActiveOnTurnId = FTurnId();
		FBattleMoveRedirection::RemoveForOccupant(
			State.MoveRedirectionRegistrations,
			{Active->ActiveSlotId, Outgoing->BattlerId});
		Active->BattlerId = Incoming->BattlerId;
		Incoming->bAbilitySuppressed = false;
		Incoming->EnteredActiveOnTurnId = State.TurnId;
		return TryRegisterAbilityTriggers(State, Incoming->BattlerId)
			&& TryRegisterItemTriggers(State, Incoming->BattlerId);
	}

	bool TryApplyReplacementSelection(
		FBattleEngineState& State,
		const FTrainerId TrainerId,
		const FActiveSlotId ActiveSlotId,
		const FBattleSwitchResolution& Resolution,
		FBattleEventTarget& OutIncomingTarget);
}
