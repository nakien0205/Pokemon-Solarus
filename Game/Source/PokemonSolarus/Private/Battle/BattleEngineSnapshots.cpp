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

namespace BattleEngineSnapshotsPrivate
{
	using namespace BattleEngineCheckpointStatePrivate;
	using namespace BattleEngineCommonPrivate;
	using namespace BattleEngineEventsPrivate;
	using namespace BattleEngineQueueBoundaryPrivate;
	using namespace BattleEngineSwitchPipelinePrivate;
	using namespace BattleEngineTriggerRuntimePrivate;

	bool IsEventVisibleToTrainer(
		const FBattleEngineState& State,
		const FBattleEventVisibility& Visibility,
		const FTrainerId ObserverTrainerId)
	{
		if (Visibility.Level == EBattleVisibilityLevel::Public)
		{
			return true;
		}
		if (Visibility.Level == EBattleVisibilityLevel::OwningTrainer)
		{
			return Visibility.OwningTrainerId == ObserverTrainerId;
		}
		if (Visibility.Level == EBattleVisibilityLevel::OwningSide && Visibility.bHasOwningSide)
		{
			const FBattleTrainerState* Observer = State.FindTrainer(ObserverTrainerId);
			return Observer != nullptr && Observer->Side == Visibility.OwningSide;
		}
		return false;
	}

	bool IsDefinitionKnown(
		const FBattleEngineState& State,
		const FTrainerId ObserverTrainerId,
		const FBattlerId SubjectBattlerId,
		const EBattleKnowledgeKind Kind,
		const FDefinitionId& DefinitionId)
	{
		const FBattleBattlerState* Subject = State.FindBattler(SubjectBattlerId);
		if (Subject != nullptr && Subject->TrainerId == ObserverTrainerId)
		{
			return true;
		}
		if (State.Setup.GetKnowledgeFacts().ContainsByPredicate(
			[ObserverTrainerId, SubjectBattlerId, Kind, &DefinitionId](const FBattleKnowledgeFact& Fact)
			{
				return Fact.ObserverTrainerId == ObserverTrainerId
					&& Fact.Visibility != EBattleVisibilityLevel::CoreOnly
					&& Fact.SubjectBattlerId == SubjectBattlerId
					&& Fact.Kind == Kind
					&& Fact.DefinitionId == DefinitionId;
			}))
		{
			return true;
		}
		if (Kind == EBattleKnowledgeKind::SpeciesFormKnown
			&& State.Setup.GetKnowledgeFacts().ContainsByPredicate(
				[ObserverTrainerId, Kind, &DefinitionId](const FBattleKnowledgeFact& Fact)
				{
					return Fact.ObserverTrainerId == ObserverTrainerId
						&& Fact.Visibility != EBattleVisibilityLevel::CoreOnly
						&& Fact.Kind == Kind
						&& Fact.DefinitionId == DefinitionId;
				}))
		{
			return true;
		}

		if (Kind == EBattleKnowledgeKind::SpeciesFormKnown)
		{
			return false;
		}

		return State.OrderedEvents.ContainsByPredicate(
			[&State, ObserverTrainerId, SubjectBattlerId, Kind, &DefinitionId](const FBattleEvent& Event)
			{
				const bool bMatchingDefinitionFamily =
					(Kind == EBattleKnowledgeKind::MoveRevealed
						&& Event.GetCause() == EBattleEventCause::Move)
					|| (Kind == EBattleKnowledgeKind::ItemRevealed
						&& Event.GetCause() == EBattleEventCause::Item)
					|| (Kind == EBattleKnowledgeKind::AbilityRevealed
						&& Event.GetCause() == EBattleEventCause::Rule);
				return Event.GetVisibility().bRevealSourceDefinition
					&& bMatchingDefinitionFamily
					&& Event.GetSource().BattlerId == SubjectBattlerId
					&& Event.GetSource().DefinitionId == DefinitionId
					&& IsEventVisibleToTrainer(State, Event.GetVisibility(), ObserverTrainerId);
			});
	}

	FBattleObservedCondition ProjectCondition(const FBattleConditionState& Condition)
	{
		FBattleObservedCondition Projection;
		Projection.ConditionId = Condition.ConditionId;
		Projection.RemainingTurns = Condition.RemainingTurns;
		Projection.LayerCount = Condition.LayerCount;
		Projection.CreationOrdinal = Condition.CreationOrdinal;
		Projection.SourceBattlerId = Condition.SourceBattlerId;
		return Projection;
	}

	EBattleEffectivenessKnowledge ToKnowledge(const FBattleTypeEffectiveness& Effectiveness)
	{
		if (Effectiveness.IsImmune())
		{
			return EBattleEffectivenessKnowledge::Immune;
		}
		if (Effectiveness.Numerator == Effectiveness.Denominator)
		{
			return EBattleEffectivenessKnowledge::Neutral;
		}
		return Effectiveness.Numerator < Effectiveness.Denominator
			? EBattleEffectivenessKnowledge::NotVeryEffective
			: EBattleEffectivenessKnowledge::SuperEffective;
	}

	EBattleEffectivenessKnowledge CalculateEffectivenessKnowledge(
		const FBattleEngineState& State,
		const FTrainerId ObserverTrainerId,
		const FMoveId MoveId,
		const FActiveSlotId TargetSlotId)
	{
		if (MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
		{
			const FBattleActivePositionState* Position = State.FindActivePosition(TargetSlotId);
			const FBattleBattlerState* Target = Position != nullptr
				? State.FindBattler(Position->BattlerId)
				: nullptr;
			return Target != nullptr
				&& IsDefinitionKnown(
					State,
					ObserverTrainerId,
					Target->BattlerId,
					EBattleKnowledgeKind::SpeciesFormKnown,
					Target->SpeciesFormId.GetDefinitionId())
				? EBattleEffectivenessKnowledge::Neutral
				: EBattleEffectivenessKnowledge::Unknown;
		}

		const FBattleMoveDefinition* Move = State.Catalog.FindMove(MoveId);
		if (Move == nullptr)
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}
		if (Move->Category == EBattleMoveCategory::Status)
		{
			return EBattleEffectivenessKnowledge::NotApplicable;
		}

		const FBattleActivePositionState* Position = State.FindActivePosition(TargetSlotId);
		const FBattleBattlerState* Target = Position != nullptr ? State.FindBattler(Position->BattlerId) : nullptr;
		if (Target == nullptr
			|| !IsDefinitionKnown(
				State,
				ObserverTrainerId,
				Target->BattlerId,
				EBattleKnowledgeKind::SpeciesFormKnown,
				Target->SpeciesFormId.GetDefinitionId()))
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}

		const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(Target->SpeciesFormId);
		if (Species == nullptr)
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}

		FBattleTypeEffectiveness Effectiveness;
		const bool bFound = Species->SecondaryType == EPokemonType::Invalid
			? State.Catalog.GetTypeChart().TryGetEffectiveness(
				Move->Type,
				Species->PrimaryType,
				Effectiveness)
			: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
				Move->Type,
				Species->PrimaryType,
				Species->SecondaryType,
				Effectiveness);
		return bFound ? ToKnowledge(Effectiveness) : EBattleEffectivenessKnowledge::Unknown;
	}

	EBattleEffectivenessKnowledge SummarizeEffectiveness(
		const TArray<EBattleEffectivenessKnowledge>& Values)
	{
		if (Values.IsEmpty())
		{
			return EBattleEffectivenessKnowledge::Unknown;
		}
		for (int32 Index = 1; Index < Values.Num(); ++Index)
		{
			if (Values[Index] != Values[0])
			{
				return EBattleEffectivenessKnowledge::Varies;
			}
		}
		return Values[0];
	}
}

using namespace BattleEngineSnapshotsPrivate;

FBattleSnapshot FBattleEngine::BuildSnapshot(const FTrainerId* ObserverTrainerId) const
{
	FBattleSnapshot Snapshot;
	if (!State.IsValid())
	{
		return Snapshot;
	}

	const bool bFiltered = ObserverTrainerId != nullptr;
	const FBattleTrainerState* Observer = bFiltered ? State->FindTrainer(*ObserverTrainerId) : nullptr;
	if (bFiltered && Observer == nullptr)
	{
		return Snapshot;
	}

	Snapshot.bValid = true;
	Snapshot.StateVersion = State->StateVersion;
	Snapshot.BattleId = State->Setup.GetBattleId();
	Snapshot.TurnId = State->TurnId;
	Snapshot.EncounterKind = State->CompiledEncounterPolicies.GetEncounterKind();
	Snapshot.Format = State->CompiledEncounterPolicies.GetFormat();
	Snapshot.Phase = State->Phase;
	Snapshot.Outcome = State->Outcome;
	Snapshot.OutcomeCause = State->OutcomeCause;
	Snapshot.SettingsReference = State->Setup.GetSettingsReference();
	Snapshot.CatalogReference = State->Setup.GetCatalogReference();
	Snapshot.EscapeAttemptCount = State->EscapeAttemptCount;
	Snapshot.bReinforcementSucceeded = State->bReinforcementSucceeded;
	const FBattleTrainerEncounterPolicy* ObserverPolicy = bFiltered
		? FindTrainerEncounterPolicy(*State, Observer->TrainerId)
		: nullptr;
	Snapshot.bCaptureStateVisible = !bFiltered
		|| (ObserverPolicy != nullptr
			&& ObserverPolicy->Role == EBattleTrainerRole::Player);
	if (Snapshot.bCaptureStateVisible)
	{
		Snapshot.CaptureCapacity = State->CaptureCapacity;
		Snapshot.CaptureProgression = State->Setup.GetCaptureProgression();
		Snapshot.PendingCaptures = State->PendingCaptures;
	}
	if (!bFiltered)
	{
		Snapshot.ConfiguredReinforcementBattlerId =
			State->Setup.GetConfiguredReinforcementBattlerId();
	}
	Snapshot.bObserverFiltered = bFiltered;
	if (bFiltered)
	{
		Snapshot.ObserverTrainerId = *ObserverTrainerId;
	}
	else
	{
		Snapshot.Trainers = State->BuildTrainerProjection();
		Snapshot.PartyEntries = State->BuildPartyProjection();
		Snapshot.ActiveAssignments = State->BuildActiveProjection();
		for (const FBattleBattlerState& Battler : State->Battlers)
		{
			const FBattleTrainerEncounterPolicy* TrainerPolicy =
				FindTrainerEncounterPolicy(*State, Battler.TrainerId);
			if (TrainerPolicy == nullptr
				|| !TrainerPolicy->bPartnerOwnsSeparatePartyAndBag)
			{
				continue;
			}
			FBattlePersistentProgressionEligibilityFact& Fact =
				Snapshot.PersistentProgressionEligibilityFacts.AddDefaulted_GetRef();
			Fact.TrainerId = Battler.TrainerId;
			Fact.BattlerId = Battler.BattlerId;
			Fact.SourcePokemonId = Battler.SourcePokemonId;
			Fact.bExperienceEligible = false;
			Fact.bEffortValueEligible = false;
			Fact.Restriction = EBattlePersistentProgressionRestriction::NpcPartner;
			check(Fact.IsValid());
		}
	}

	for (const FBattleTrainerState& Trainer : State->Trainers)
	{
		FBattleObservedTrainer Projection;
		Projection.TrainerId = Trainer.TrainerId;
		Projection.Side = Trainer.Side;
		Projection.Role = Trainer.Role;
		Projection.Controller = Trainer.Controller;
		Projection.bBagVisible = !bFiltered || Trainer.TrainerId == *ObserverTrainerId;
		if (Projection.bBagVisible)
		{
			Projection.Bag = Trainer.Bag;
		}
		Snapshot.ObservedTrainers.Add(MoveTemp(Projection));
	}

	for (const FBattleActivePositionState& Position : State->ActivePositions)
	{
		Snapshot.ObservedActiveSlots.Add(
			{Position.ActiveSlotId, Position.bAvailable, Position.TrainerId, Position.BattlerId});
	}

	for (const FBattleBattlerState& Battler : State->Battlers)
	{
		const bool bOwned = bFiltered && Battler.TrainerId == *ObserverTrainerId;
		const bool bActive = FindActiveForBattler(*State, Battler.BattlerId) != nullptr;
		if (bFiltered && !bOwned && !bActive)
		{
			continue;
		}

		FBattleObservedBattler Projection;
		Projection.TrainerId = Battler.TrainerId;
		Projection.BattlerId = Battler.BattlerId;
		Projection.bPartySlotVisible = !bFiltered || bOwned;
		if (Projection.bPartySlotVisible)
		{
			Projection.PartySlotId = Battler.PartySlotId;
		}
		Projection.SpeciesFormId = Battler.SpeciesFormId;
		Projection.Level = Battler.Level;
		Projection.CurrentHP = Battler.CurrentHP;
		Projection.MaxHP = Battler.PermanentStats.MaxHP;
		Projection.bFainted = Battler.bFainted;
		Projection.MajorStatusId = Battler.MajorStatusId;
		Projection.StatStages = Battler.Stages;

		Projection.bAbilityKnown = !bFiltered
			|| bOwned
			|| IsDefinitionKnown(
				*State,
				*ObserverTrainerId,
				Battler.BattlerId,
				EBattleKnowledgeKind::AbilityRevealed,
				Battler.AbilityId.GetDefinitionId());
		if (Projection.bAbilityKnown)
		{
			Projection.AbilityId = Battler.AbilityId;
		}

		const bool bHeldItemPresent = IsHeldItemActive(Battler);
		const bool bHeldItemDefinitionKnown = bFiltered
			&& (Battler.HeldItem.bRevealed
				|| (Battler.HeldItem.CurrentItemId.IsValid()
					&& IsDefinitionKnown(
						*State,
						*ObserverTrainerId,
						Battler.BattlerId,
						EBattleKnowledgeKind::ItemRevealed,
						Battler.HeldItem.CurrentItemId.GetDefinitionId())));
		Projection.bHeldItemKnown = !bFiltered
			|| bOwned
			|| bHeldItemDefinitionKnown;
		if (Projection.bHeldItemKnown)
		{
			Projection.HeldItemId = bHeldItemPresent
				? Battler.HeldItem.CurrentItemId
				: FItemId();
		}

		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			const bool bMoveKnown = !bFiltered
				|| bOwned
				|| IsDefinitionKnown(
					*State,
					*ObserverTrainerId,
					Battler.BattlerId,
					EBattleKnowledgeKind::MoveRevealed,
					Move.MoveId.GetDefinitionId());
			if (!bMoveKnown)
			{
				continue;
			}
			FBattleObservedMove MoveProjection;
			MoveProjection.SlotIndex = Move.SlotIndex;
			MoveProjection.MoveId = Move.MoveId;
			MoveProjection.bPPVisible = !bFiltered || bOwned;
			if (MoveProjection.bPPVisible)
			{
				MoveProjection.CurrentPP = Move.CurrentPP;
				MoveProjection.MaxPP = Move.MaxPP;
			}
			Projection.Moves.Add(MoveProjection);
		}
		Snapshot.ObservedBattlers.Add(MoveTemp(Projection));
	}

	if (State->Field.Weather.IsSet())
	{
		Snapshot.Weather = ProjectCondition(State->Field.Weather.GetValue());
	}
	if (State->Field.Terrain.IsSet())
	{
		Snapshot.Terrain = ProjectCondition(State->Field.Terrain.GetValue());
	}
	for (const FBattleConditionState& Room : State->Field.Rooms)
	{
		Snapshot.Rooms.Add(ProjectCondition(Room));
	}
	for (const FBattleConditionState& Effect : State->Field.Effects)
	{
		Snapshot.FieldEffects.Add(ProjectCondition(Effect));
	}
	for (const FBattleSideState& Side : State->Sides)
	{
		FBattleObservedSide SideProjection;
		SideProjection.Side = Side.Side;
		for (const FBattleConditionState& Condition : Side.Conditions)
		{
			SideProjection.Conditions.Add(ProjectCondition(Condition));
		}
		for (const FBattleConditionState& Hazard : Side.Hazards)
		{
			SideProjection.Hazards.Add(ProjectCondition(Hazard));
		}
		Snapshot.ObservedSides.Add(MoveTemp(SideProjection));
	}

	if (!bFiltered
		|| (!State->PendingDecisionRequests.IsEmpty()
			&& State->PendingDecisionRequests[0].GetDecisionOwnerTrainerId() == *ObserverTrainerId))
	{
		Snapshot.PendingDecisionRequests = State->PendingDecisionRequests;
		if (!Snapshot.PendingDecisionRequests.IsEmpty())
		{
			Snapshot.PendingDecision = Snapshot.PendingDecisionRequests[0];
		}
	}
	if (Snapshot.PendingDecisionRequests.IsEmpty()
		&& State->PendingDecision.IsSet()
		&& (!bFiltered
			|| State->PendingDecision.GetValue().GetDecisionOwnerTrainerId() == *ObserverTrainerId))
	{
		Snapshot.PendingDecision = State->PendingDecision;
		Snapshot.PendingDecisionRequests.Add(State->PendingDecision.GetValue());
	}

	for (const FBattleDecision& Decision : State->AcceptedSelections)
	{
		bool bVisible = !bFiltered || Decision.GetDecisionOwnerTrainerId() == *ObserverTrainerId;
		if (bFiltered && !bVisible && Observer->Role == EBattleTrainerRole::Partner)
		{
			const FBattleTrainerState* DecisionTrainer = State->FindTrainer(Decision.GetDecisionOwnerTrainerId());
			bVisible = DecisionTrainer != nullptr
				&& DecisionTrainer->Role == EBattleTrainerRole::Player
				&& DecisionTrainer->Side == Observer->Side;
		}
		if (bVisible)
		{
			Snapshot.VisibleSelections.Add(Decision);
		}
	}

	if (bFiltered)
	{
		for (const FBattleDecisionRequest& Request : Snapshot.PendingDecisionRequests)
		{
			for (const FMoveId& MoveId : Request.GetLegalMoveIds())
			{
				TArray<EBattleEffectivenessKnowledge> TargetValues;
				for (const FBattleMoveTargetOption& Pair : Request.GetLegalMoveTargets())
				{
					if (Pair.MoveId != MoveId)
					{
						continue;
					}
					const EBattleEffectivenessKnowledge Value = CalculateEffectivenessKnowledge(
						*State,
						*ObserverTrainerId,
						MoveId,
						Pair.ActiveSlotId);
					TargetValues.Add(Value);
					Snapshot.TargetEffectivenessKnowledge.Add({MoveId, Pair.ActiveSlotId, Value});
				}
				const FBattleMoveDefinition* Move = State->Catalog.FindMove(MoveId);
				const EBattleEffectivenessKnowledge Summary = Move != nullptr
					&& Move->Category == EBattleMoveCategory::Status
					? EBattleEffectivenessKnowledge::NotApplicable
					: SummarizeEffectiveness(TargetValues);
				Snapshot.MoveEffectivenessKnowledge.Add({MoveId, Summary});
			}
		}
	}
	return Snapshot;
}

FBattleSnapshot FBattleEngine::GetSnapshot() const
{
	return BuildSnapshot(nullptr);
}

FBattleSnapshot FBattleEngine::GetSnapshotForObserver(const FTrainerId ObserverTrainerId) const
{
	return BuildSnapshot(&ObserverTrainerId);
}

const FBattleCompiledEncounterPolicies& FBattleEngine::GetCompiledEncounterPolicies() const
{
	check(State.IsValid());
	return State->CompiledEncounterPolicies;
}

TOptional<FBattleDecisionRequest> FBattleEngine::GetPendingDecision() const
{
	return State.IsValid() ? State->PendingDecision : TOptional<FBattleDecisionRequest>();
}

TArray<FBattleDecisionRequest> FBattleEngine::GetPendingDecisionRequests() const
{
	return State.IsValid() ? State->PendingDecisionRequests : TArray<FBattleDecisionRequest>();
}

TArray<FBattleLockedAction> FBattleEngine::GetLockedActions() const
{
	TArray<FBattleLockedAction> Actions;
	if (!State.IsValid())
	{
		return Actions;
	}

	Actions.Reserve(State->LockedActions.Num());
	for (const FBattleLockedActionState& StateAction : State->LockedActions)
	{
		FBattleLockedAction Action;
		Action.ActionId = StateAction.ActionId;
		Action.QueueOrdinal = StateAction.QueueOrdinal;
		Action.Decision = StateAction.Decision;
		Action.OrderKey = StateAction.OrderKey;
		Action.TargetClass = StateAction.TargetClass;
		Action.SelectedTargetBattlerId = StateAction.SelectedTargetBattlerId;
		Action.TargetResolution = StateAction.TargetResolution;
		Actions.Add(MoveTemp(Action));
	}
	return Actions;
}

TOptional<FBattleLockedAction> FBattleEngine::GetCurrentLockedAction() const
{
	if (!State.IsValid()
		|| !State->LockedActions.IsValidIndex(State->CurrentLockedActionIndex))
	{
		return TOptional<FBattleLockedAction>();
	}

	const FBattleLockedActionState& StateAction = State->LockedActions[State->CurrentLockedActionIndex];
	if (!StateAction.bStarted || StateAction.bFinished)
	{
		return TOptional<FBattleLockedAction>();
	}

	FBattleLockedAction Action;
	Action.ActionId = StateAction.ActionId;
	Action.QueueOrdinal = StateAction.QueueOrdinal;
	Action.Decision = StateAction.Decision;
	Action.OrderKey = StateAction.OrderKey;
	Action.TargetClass = StateAction.TargetClass;
	Action.SelectedTargetBattlerId = StateAction.SelectedTargetBattlerId;
	Action.TargetResolution = StateAction.TargetResolution;
	return Action;
}

const FBattlePartyEntrySetup* FBattleSnapshot::FindBattler(const FBattlerId BattlerId) const
{
	return PartyEntries.FindByPredicate(
		[BattlerId](const FBattlePartyEntrySetup& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}

const FBattleObservedBattler* FBattleSnapshot::FindObservedBattler(const FBattlerId BattlerId) const
{
	return ObservedBattlers.FindByPredicate(
		[BattlerId](const FBattleObservedBattler& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}
