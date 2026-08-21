#include "Battle/BattleState.h"

namespace
{
	bool IsKnownSide(const EBattleSide Value)
	{
		return Value == EBattleSide::Player || Value == EBattleSide::Opponent;
	}

	bool IsKnownRole(const EBattleTrainerRole Value)
	{
		return Value == EBattleTrainerRole::Player
			|| Value == EBattleTrainerRole::Partner
			|| Value == EBattleTrainerRole::Opponent;
	}

	bool IsKnownController(const EBattleDecisionController Value)
	{
		return Value == EBattleDecisionController::Human
			|| Value == EBattleDecisionController::PartnerAI
			|| Value == EBattleDecisionController::EnemyAI
			|| Value == EBattleDecisionController::Scripted;
	}

	bool IsKnownEncounterKind(const EBattleEncounterKind Value)
	{
		return Value == EBattleEncounterKind::Wild
			|| Value == EBattleEncounterKind::Trainer
			|| Value == EBattleEncounterKind::Rival
			|| Value == EBattleEncounterKind::BossGym
			|| Value == EBattleEncounterKind::TutorialScripted;
	}

	bool IsKnownFormat(const EBattleFormat Value)
	{
		return Value == EBattleFormat::Single
			|| Value == EBattleFormat::Double
			|| Value == EBattleFormat::PartnerDouble;
	}

	bool IsKnownPhase(const EBattlePhase Value)
	{
		return Value == EBattlePhase::Setup
			|| Value == EBattlePhase::Selecting
			|| Value == EBattlePhase::Locked
			|| Value == EBattlePhase::Resolving
			|| Value == EBattlePhase::MandatoryReplacement
			|| Value == EBattlePhase::EndOfTurn
			|| Value == EBattlePhase::Terminal;
	}

	bool IsKnownOutcome(const EBattleOutcome Value)
	{
		return Value == EBattleOutcome::InProgress
			|| Value == EBattleOutcome::Victory
			|| Value == EBattleOutcome::Defeat
			|| Value == EBattleOutcome::Escape
			|| Value == EBattleOutcome::ScriptedEnd
			|| Value == EBattleOutcome::Abandoned;
	}

	bool IsPositiveStats(const FPokemonBattleStats& Stats)
	{
		return Stats.MaxHP > 0
			&& Stats.Attack > 0
			&& Stats.Defense > 0
			&& Stats.SpecialAttack > 0
			&& Stats.SpecialDefense > 0
			&& Stats.Speed > 0;
	}

	template <typename ElementType, typename PredicateType>
	bool HasDuplicatePair(const TArray<ElementType>& Values, PredicateType Predicate)
	{
		for (int32 LeftIndex = 0; LeftIndex < Values.Num(); ++LeftIndex)
		{
			for (int32 RightIndex = LeftIndex + 1; RightIndex < Values.Num(); ++RightIndex)
			{
				if (Predicate(Values[LeftIndex], Values[RightIndex]))
				{
					return true;
				}
			}
		}
		return false;
	}

	bool IsConditionValid(
		const FBattleConditionState& Condition,
		const FBattleDefinitionCatalog* Catalog)
	{
		if (!Condition.ConditionId.IsValid()
			|| Condition.CreationOrdinal == 0
			|| Condition.LayerCount < 0
			|| (Condition.RemainingTurns.IsSet() && Condition.RemainingTurns.GetValue() <= 0))
		{
			return false;
		}

		return Catalog == nullptr || Catalog->FindCondition(Condition.ConditionId) != nullptr;
	}

	bool IsWildFleeModeKnown(const EBattleWildFleeMode Value)
	{
		return Value == EBattleWildFleeMode::Disabled
			|| Value == EBattleWildFleeMode::Never
			|| Value == EBattleWildFleeMode::Always
			|| Value == EBattleWildFleeMode::Chance;
	}

	bool IsWildFleePolicyValid(
		const FBattleWildFleePolicyState& Policy,
		const FBattleDefinitionCatalog* Catalog)
	{
		if (!Policy.TriggerId.IsValid()
			|| !Policy.EligibilityId.IsValid()
			|| !IsWildFleeModeKnown(Policy.ProbabilityMode)
			|| Policy.ProbabilityMode == EBattleWildFleeMode::Disabled)
		{
			return false;
		}

		if (Policy.ProbabilityMode == EBattleWildFleeMode::Chance)
		{
			if (Policy.Numerator == 0 || Policy.Numerator >= Policy.Denominator)
			{
				return false;
			}
		}
		else if (Policy.Numerator != 0 || Policy.Denominator != 0)
		{
			return false;
		}

		return Catalog == nullptr
			|| !Policy.SpeciesFormId.IsValid()
			|| Catalog->FindSpeciesForm(Policy.SpeciesFormId) != nullptr;
	}

	bool ContainsAbility(
		const FBattleSpeciesFormDefinition& Species,
		const FAbilityId AbilityId)
	{
		return Species.AbilityChoices.Contains(AbilityId);
	}
}

bool FBattleEngineState::TryCreate(
	const FBattleSetup& Setup,
	const FBattleDefinitionCatalog* Catalog,
	TUniquePtr<IBattleRandom>&& Random,
	TUniquePtr<FBattleEngineState>& OutState,
	EBattleStateValidationError& OutError)
{
	OutState.Reset();
	OutError = EBattleStateValidationError::None;
	if (!Setup.IsValid() || !Random.IsValid())
	{
		OutError = EBattleStateValidationError::InvalidSetup;
		return false;
	}
	if (Catalog != nullptr && !Catalog->IsValid())
	{
		OutError = EBattleStateValidationError::InvalidCatalog;
		return false;
	}

	TUniquePtr<FBattleEngineState> NewState = MakeUnique<FBattleEngineState>();
	NewState->Setup = Setup;
	NewState->EncounterKind = Setup.GetEncounterKind();
	NewState->Format = Setup.GetFormat();
	NewState->CaptureCapacity = Setup.GetCaptureCapacity();
	NewState->EncounterPolicies = Setup.GetPolicies();
	NewState->Random = MoveTemp(Random);
	if (!FTurnId::TryCreate(1, NewState->TurnId))
	{
		OutError = EBattleStateValidationError::InvalidCounter;
		return false;
	}
	if (Catalog != nullptr)
	{
		NewState->Catalog = *Catalog;
		NewState->bHasCatalog = true;
	}

	for (const FBattleTrainerSetup& SetupTrainer : Setup.GetTrainers())
	{
		FBattleTrainerState Trainer;
		Trainer.TrainerId = SetupTrainer.TrainerId;
		Trainer.Side = SetupTrainer.Side;
		Trainer.Role = SetupTrainer.Role;
		Trainer.Controller = SetupTrainer.Controller;
		Trainer.SelectorProfileId = SetupTrainer.SelectorProfileId;
		Trainer.Bag = SetupTrainer.Bag;
		Trainer.PartySlots.Reserve(FPartySlotId::PartySize);
		for (int32 PartyIndex = 0; PartyIndex < FPartySlotId::PartySize; ++PartyIndex)
		{
			FBattlePartySlotState PartySlot;
			const bool bCreated = FPartySlotId::TryCreate(PartyIndex, PartySlot.PartySlotId);
			check(bCreated);
			Trainer.PartySlots.Add(PartySlot);
		}
		NewState->Trainers.Add(MoveTemp(Trainer));
	}

	for (const FBattlePartyEntrySetup& SetupEntry : Setup.GetPartyEntries())
	{
		FBattleBattlerState Battler;
		Battler.TrainerId = SetupEntry.TrainerId;
		Battler.BattlerId = SetupEntry.BattlerId;
		Battler.SourcePokemonId = SetupEntry.SourcePokemonId;
		Battler.PartySlotId = SetupEntry.PartySlotId;
		Battler.SpeciesFormId = SetupEntry.SpeciesFormId;
		Battler.Level = SetupEntry.Level;
		Battler.PermanentStats = SetupEntry.Stats;
		Battler.CurrentHP = SetupEntry.CurrentHP;
		Battler.bFainted = SetupEntry.CurrentHP == 0;
		Battler.bEgg = SetupEntry.bEgg;
		Battler.AbilityId = SetupEntry.AbilityId;
		Battler.HeldItem.OriginalItemId = SetupEntry.OriginalHeldItemId;
		Battler.HeldItem.CurrentItemId = SetupEntry.CurrentHeldItemId;
		for (const FBattleMoveSlotSetup& SetupMove : SetupEntry.Moves)
		{
			Battler.Moves.Add(
				{
					SetupMove.SlotIndex,
					SetupMove.MoveId,
					SetupMove.CurrentPP,
					SetupMove.MaxPP
				});
		}

		const FBattleObedienceInput* Obedience = Setup.GetObedienceInputs().FindByPredicate(
			[&SetupEntry](const FBattleObedienceInput& Candidate)
			{
				return Candidate.BattlerId == SetupEntry.BattlerId;
			});
		if (Obedience != nullptr)
		{
			Battler.Obedience.bHasSnapshot = true;
			Battler.Obedience.bSubjectToPlayerCap = Obedience->bSubjectToPlayerCap;
			Battler.Obedience.ReferenceLevel = Obedience->ReferenceLevel;
			Battler.Obedience.BadgeCount = Obedience->BadgeCount;
		}

		FBattleTrainerState* Trainer = NewState->FindMutableTrainer(SetupEntry.TrainerId);
		check(Trainer != nullptr);
		Trainer->PartySlots[SetupEntry.PartySlotId.GetIndex()].BattlerId = SetupEntry.BattlerId;
		NewState->Battlers.Add(MoveTemp(Battler));
	}

	for (int32 SideIndex = 0; SideIndex < 2; ++SideIndex)
	{
		const EBattleSide Side = static_cast<EBattleSide>(SideIndex);
		NewState->Sides.Add({Side});
		for (int32 PositionIndex = 0; PositionIndex < 2; ++PositionIndex)
		{
			const EBattlePosition Position = static_cast<EBattlePosition>(PositionIndex);
			FBattleActivePositionState Active;
			const bool bCreated = FActiveSlotId::TryCreate(Side, Position, Active.ActiveSlotId);
			check(bCreated);
			Active.bAvailable = Setup.GetFormat() != EBattleFormat::Single
				|| Position == EBattlePosition::Left;
			NewState->ActivePositions.Add(Active);
		}
	}

	for (const FBattleActiveAssignment& Assignment : Setup.GetStartingActive())
	{
		FBattleActivePositionState* Position = NewState->FindMutableActivePosition(Assignment.ActiveSlotId);
		FBattleTrainerState* Trainer = NewState->FindMutableTrainer(Assignment.TrainerId);
		check(Position != nullptr && Trainer != nullptr);
		Position->TrainerId = Assignment.TrainerId;
		Position->BattlerId = Assignment.BattlerId;
		++Trainer->ActionAllowance.MaximumActions;
		++Trainer->ActionAllowance.RemainingActions;
	}

	if (!NewState->ValidateInvariants(OutError))
	{
		return false;
	}

	const int32 ExpectedOccupiedPositions = Setup.GetFormat() == EBattleFormat::Single ? 2 : 4;
	int32 OccupiedPositions = 0;
	for (const FBattleActivePositionState& Position : NewState->ActivePositions)
	{
		if (Position.BattlerId.IsValid())
		{
			++OccupiedPositions;
		}
	}
	if (OccupiedPositions != ExpectedOccupiedPositions)
	{
		OutError = EBattleStateValidationError::InvalidActivePosition;
		return false;
	}

	OutState = MoveTemp(NewState);
	return true;
}

bool FBattleEngineState::ValidateInvariants(EBattleStateValidationError& OutError) const
{
	OutError = EBattleStateValidationError::None;
	auto Fail = [&OutError](const EBattleStateValidationError Error)
	{
		OutError = Error;
		return false;
	};

	if (!Setup.IsValid()
		|| !Setup.GetBattleId().IsValid()
		|| !Setup.GetSettingsReference().IsValid()
		|| !Setup.GetCatalogReference().IsValid()
		|| !TurnId.IsValid()
		|| !Random.IsValid())
	{
		return Fail(EBattleStateValidationError::InvalidSetup);
	}
	if (bHasCatalog && !Catalog.IsValid())
	{
		return Fail(EBattleStateValidationError::InvalidCatalog);
	}
	if (!IsKnownEncounterKind(EncounterKind)
		|| !IsKnownFormat(Format)
		|| !IsKnownPhase(Phase)
		|| !IsKnownOutcome(Outcome)
		|| (Phase == EBattlePhase::Terminal) != (Outcome != EBattleOutcome::InProgress)
		|| (Outcome == EBattleOutcome::InProgress && OutcomeCause != EBattleOutcomeCause::None)
		|| (Phase == EBattlePhase::Terminal && PendingDecision.IsSet()))
	{
		return Fail(EBattleStateValidationError::InvalidLifecycle);
	}
	if (StateVersion == 0
		|| NextResolutionId == 0
		|| NextActionId == 0
		|| NextEventOrdinal == 0
		|| NextConditionCreationOrdinal == 0
		|| EscapeAttemptCount == 0)
	{
		return Fail(EBattleStateValidationError::InvalidCounter);
	}

	const int32 ExpectedTrainerCount = Format == EBattleFormat::PartnerDouble ? 3 : 2;
	if (Trainers.Num() != ExpectedTrainerCount)
	{
		return Fail(EBattleStateValidationError::InvalidTrainer);
	}
	if (HasDuplicatePair(
		Trainers,
		[](const FBattleTrainerState& Left, const FBattleTrainerState& Right)
		{
			return Left.TrainerId == Right.TrainerId;
		}))
	{
		return Fail(EBattleStateValidationError::DuplicateTrainer);
	}

	for (const FBattleTrainerState& Trainer : Trainers)
	{
		if (!Trainer.TrainerId.IsValid()
			|| !IsKnownSide(Trainer.Side)
			|| !IsKnownRole(Trainer.Role)
			|| !IsKnownController(Trainer.Controller)
			|| !Trainer.SelectorProfileId.IsValid()
			|| Trainer.PartySlots.Num() != FPartySlotId::PartySize
			|| Trainer.ActionAllowance.MaximumActions < 0
			|| Trainer.ActionAllowance.MaximumActions > 2
			|| Trainer.ActionAllowance.RemainingActions < 0
			|| Trainer.ActionAllowance.RemainingActions > Trainer.ActionAllowance.MaximumActions)
		{
			return Fail(EBattleStateValidationError::InvalidTrainer);
		}

		for (int32 PartyIndex = 0; PartyIndex < FPartySlotId::PartySize; ++PartyIndex)
		{
			FPartySlotId ExpectedPartySlot;
			const bool bCreated = FPartySlotId::TryCreate(PartyIndex, ExpectedPartySlot);
			check(bCreated);
			if (Trainer.PartySlots[PartyIndex].PartySlotId != ExpectedPartySlot)
			{
				return Fail(EBattleStateValidationError::InvalidParty);
			}
		}

		if (HasDuplicatePair(
			Trainer.Bag,
			[](const FBattleBagItemCount& Left, const FBattleBagItemCount& Right)
			{
				return Left.ItemId == Right.ItemId;
			}))
		{
			return Fail(EBattleStateValidationError::InvalidResource);
		}
		for (const FBattleBagItemCount& Item : Trainer.Bag)
		{
			if (!Item.ItemId.IsValid() || Item.Count < 0)
			{
				return Fail(EBattleStateValidationError::InvalidResource);
			}
			if (bHasCatalog && Catalog.FindItem(Item.ItemId) == nullptr)
			{
				return Fail(EBattleStateValidationError::MissingCatalogReference);
			}
		}
	}

	if (HasDuplicatePair(
		Battlers,
		[](const FBattleBattlerState& Left, const FBattleBattlerState& Right)
		{
			return Left.BattlerId == Right.BattlerId
				|| Left.SourcePokemonId == Right.SourcePokemonId
				|| (Left.TrainerId == Right.TrainerId && Left.PartySlotId == Right.PartySlotId);
		}))
	{
		return Fail(EBattleStateValidationError::DuplicateBattler);
	}

	for (const FBattleBattlerState& Battler : Battlers)
	{
		const FBattleTrainerState* Trainer = FindTrainer(Battler.TrainerId);
		if (Trainer == nullptr
			|| !Battler.BattlerId.IsValid()
			|| !Battler.SourcePokemonId.IsValid()
			|| !Battler.PartySlotId.IsValid()
			|| !Battler.SpeciesFormId.IsValid()
			|| !Battler.AbilityId.IsValid()
			|| Battler.Level < 1
			|| Battler.Level > 100
			|| !IsPositiveStats(Battler.PermanentStats))
		{
			return Fail(EBattleStateValidationError::InvalidBattler);
		}
		if (Battler.CurrentHP < 0
			|| Battler.CurrentHP > Battler.PermanentStats.MaxHP
			|| Battler.bFainted != (Battler.CurrentHP == 0))
		{
			return Fail(EBattleStateValidationError::InvalidHP);
		}
		if (Battler.bFaintTransitionPending && !Battler.bFainted)
		{
			return Fail(EBattleStateValidationError::InvalidHP);
		}
		if (Trainer->PartySlots[Battler.PartySlotId.GetIndex()].BattlerId != Battler.BattlerId)
		{
			return Fail(EBattleStateValidationError::InvalidParty);
		}
		if (Battler.HeldItem.CurrentItemId.IsValid() && !Battler.HeldItem.OriginalItemId.IsValid())
		{
			return Fail(EBattleStateValidationError::InvalidBattler);
		}
		if (Battler.Obedience.bHasSnapshot
			&& (Battler.Obedience.ReferenceLevel < 1
				|| Battler.Obedience.ReferenceLevel > 100
				|| Battler.Obedience.BadgeCount > 8))
		{
			return Fail(EBattleStateValidationError::InvalidBattler);
		}

		for (int32 StatIndex = 0; StatIndex < 7; ++StatIndex)
		{
			int32 Stage = 0;
			if (!Battler.Stages.TryGetStage(static_cast<EBattleStat>(StatIndex), Stage)
				|| Stage < FBattleStatStages::MinimumStage
				|| Stage > FBattleStatStages::MaximumStage)
			{
				return Fail(EBattleStateValidationError::InvalidStage);
			}
		}

		if (Battler.Moves.Num() > 4
			|| HasDuplicatePair(
				Battler.Moves,
				[](const FBattleMoveSlotState& Left, const FBattleMoveSlotState& Right)
				{
					return Left.SlotIndex == Right.SlotIndex || Left.MoveId == Right.MoveId;
				}))
		{
			return Fail(EBattleStateValidationError::InvalidPP);
		}
		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			if (Move.SlotIndex >= 4
				|| !Move.MoveId.IsValid()
				|| Move.MaxPP <= 0
				|| Move.CurrentPP < 0
				|| Move.CurrentPP > Move.MaxPP)
			{
				return Fail(EBattleStateValidationError::InvalidPP);
			}
		}

		if (bHasCatalog)
		{
			const FBattleSpeciesFormDefinition* Species = Catalog.FindSpeciesForm(Battler.SpeciesFormId);
			if (Species == nullptr
				|| Catalog.FindAbility(Battler.AbilityId) == nullptr
				|| !ContainsAbility(*Species, Battler.AbilityId))
			{
				return Fail(EBattleStateValidationError::MissingCatalogReference);
			}
			for (const FBattleMoveSlotState& Move : Battler.Moves)
			{
				if (Catalog.FindMove(Move.MoveId) == nullptr)
				{
					return Fail(EBattleStateValidationError::MissingCatalogReference);
				}
			}
			if ((Battler.HeldItem.OriginalItemId.IsValid()
					&& Catalog.FindItem(Battler.HeldItem.OriginalItemId) == nullptr)
				|| (Battler.HeldItem.CurrentItemId.IsValid()
					&& Catalog.FindItem(Battler.HeldItem.CurrentItemId) == nullptr))
			{
				return Fail(EBattleStateValidationError::MissingCatalogReference);
			}
			if (Battler.MajorStatusId.IsValid())
			{
				const FBattleConditionDefinition* MajorStatus = Catalog.FindCondition(Battler.MajorStatusId);
				if (MajorStatus == nullptr || MajorStatus->Kind != EBattleConditionKind::MajorStatus)
				{
					return Fail(EBattleStateValidationError::InvalidCondition);
				}
			}
		}
		for (const FBattleConditionState& Volatile : Battler.Volatiles)
		{
			if (!IsConditionValid(Volatile, bHasCatalog ? &Catalog : nullptr))
			{
				return Fail(EBattleStateValidationError::InvalidCondition);
			}
		}
	}

	for (const FBattleTrainerState& Trainer : Trainers)
	{
		for (const FBattlePartySlotState& PartySlot : Trainer.PartySlots)
		{
			if (!PartySlot.BattlerId.IsValid())
			{
				continue;
			}
			const FBattleBattlerState* Battler = FindBattler(PartySlot.BattlerId);
			if (Battler == nullptr
				|| Battler->TrainerId != Trainer.TrainerId
				|| Battler->PartySlotId != PartySlot.PartySlotId)
			{
				return Fail(EBattleStateValidationError::InvalidParty);
			}
		}
	}

	if (ActivePositions.Num() != 4
		|| HasDuplicatePair(
			ActivePositions,
			[](const FBattleActivePositionState& Left, const FBattleActivePositionState& Right)
			{
				return Left.ActiveSlotId == Right.ActiveSlotId;
			}))
	{
		return Fail(EBattleStateValidationError::InvalidActivePosition);
	}
	if (HasDuplicatePair(
		ActivePositions,
		[](const FBattleActivePositionState& Left, const FBattleActivePositionState& Right)
		{
			return Left.BattlerId.IsValid()
				&& Right.BattlerId.IsValid()
				&& Left.BattlerId == Right.BattlerId;
		}))
	{
		return Fail(EBattleStateValidationError::DuplicateActiveBattler);
	}

	for (const FBattleActivePositionState& Position : ActivePositions)
	{
		if (!Position.ActiveSlotId.IsValid())
		{
			return Fail(EBattleStateValidationError::InvalidActivePosition);
		}
		const bool bExpectedAvailable = Format != EBattleFormat::Single
			|| Position.ActiveSlotId.GetPosition() == EBattlePosition::Left;
		if (Position.bAvailable != bExpectedAvailable)
		{
			return Fail(EBattleStateValidationError::InvalidActivePosition);
		}
		const bool bHasTrainer = Position.TrainerId.IsValid();
		const bool bHasBattler = Position.BattlerId.IsValid();
		if (bHasTrainer != bHasBattler || (!Position.bAvailable && bHasBattler))
		{
			return Fail(EBattleStateValidationError::InvalidActivePosition);
		}
		if (!bHasBattler)
		{
			continue;
		}

		const FBattleTrainerState* Trainer = FindTrainer(Position.TrainerId);
		const FBattleBattlerState* Battler = FindBattler(Position.BattlerId);
		if (Trainer == nullptr
			|| Battler == nullptr
			|| Trainer->Side != Position.ActiveSlotId.GetSide()
			|| Battler->TrainerId != Trainer->TrainerId
			|| Battler->bEgg
			|| Battler->bCaptured
			|| Battler->bRemoved
			|| (Battler->bFainted && !Battler->bFaintTransitionPending))
		{
			return Fail(EBattleStateValidationError::InvalidActivePosition);
		}
	}

	for (const FBattleBattlerState& Battler : Battlers)
	{
		if (Battler.bFaintTransitionPending)
		{
			const bool bStillActive = ActivePositions.ContainsByPredicate(
				[&Battler](const FBattleActivePositionState& Position)
				{
					return Position.BattlerId == Battler.BattlerId;
				});
			if (!bStillActive)
			{
				return Fail(EBattleStateValidationError::InvalidActivePosition);
			}
		}
	}

	if (Sides.Num() != 2
		|| Sides[0].Side != EBattleSide::Player
		|| Sides[1].Side != EBattleSide::Opponent)
	{
		return Fail(EBattleStateValidationError::InvalidCondition);
	}
	const FBattleDefinitionCatalog* ConditionCatalog = bHasCatalog ? &Catalog : nullptr;
	auto ValidateConditionArray = [ConditionCatalog](const TArray<FBattleConditionState>& Conditions)
	{
		return !Conditions.ContainsByPredicate(
			[ConditionCatalog](const FBattleConditionState& Condition)
			{
				return !IsConditionValid(Condition, ConditionCatalog);
			});
	};
	if ((Field.Weather.IsSet() && !IsConditionValid(Field.Weather.GetValue(), ConditionCatalog))
		|| (Field.Terrain.IsSet() && !IsConditionValid(Field.Terrain.GetValue(), ConditionCatalog))
		|| !ValidateConditionArray(Field.Rooms)
		|| !ValidateConditionArray(Field.Effects))
	{
		return Fail(EBattleStateValidationError::InvalidCondition);
	}
	for (const FBattleSideState& Side : Sides)
	{
		if (!ValidateConditionArray(Side.Conditions) || !ValidateConditionArray(Side.Hazards))
		{
			return Fail(EBattleStateValidationError::InvalidCondition);
		}
	}

	if (CaptureCapacity.PartySlotsRemaining < 0
		|| CaptureCapacity.StorageSlotsRemaining < 0
		|| PendingCaptures.Num() > CaptureCapacity.PartySlotsRemaining + CaptureCapacity.StorageSlotsRemaining)
	{
		return Fail(EBattleStateValidationError::InvalidResource);
	}
	for (const FBattlePendingCaptureState& Capture : PendingCaptures)
	{
		if (!Capture.BattlerId.IsValid()
			|| !Capture.SourcePokemonId.IsValid()
			|| !Capture.SpeciesFormId.IsValid()
			|| Capture.MaxHP <= 0
			|| Capture.CurrentHP < 0
			|| Capture.CurrentHP > Capture.MaxHP)
		{
			return Fail(EBattleStateValidationError::InvalidPendingCapture);
		}
	}
	for (const FBattleWildFleePolicyState& Policy : WildFleePolicies)
	{
		if (!IsWildFleePolicyValid(Policy, bHasCatalog ? &Catalog : nullptr))
		{
			return Fail(EBattleStateValidationError::InvalidWildFleePolicy);
		}
	}
	if (HasDuplicatePair(
		LockedActions,
		[](const FBattleLockedActionState& Left, const FBattleLockedActionState& Right)
		{
			return Left.ActionId == Right.ActionId || Left.QueueOrdinal == Right.QueueOrdinal;
		}))
	{
		return Fail(EBattleStateValidationError::InvalidCounter);
	}
	for (const FBattleLockedActionState& Action : LockedActions)
	{
		if (!Action.ActionId.IsValid() || Action.QueueOrdinal == 0 || !Action.Decision.IsValid())
		{
			return Fail(EBattleStateValidationError::InvalidCounter);
		}
	}

	uint64 PreviousEventOrdinal = 0;
	for (const FBattleEvent& Event : OrderedEvents)
	{
		if (!Event.IsValid()
			|| Event.GetBattleId() != Setup.GetBattleId()
			|| Event.GetEventOrdinal() <= PreviousEventOrdinal)
		{
			return Fail(EBattleStateValidationError::InvalidEventOrder);
		}
		PreviousEventOrdinal = Event.GetEventOrdinal();
	}
	if (NextEventOrdinal <= PreviousEventOrdinal)
	{
		return Fail(EBattleStateValidationError::InvalidEventOrder);
	}
	return true;
}

const FBattleTrainerState* FBattleEngineState::FindTrainer(const FTrainerId TrainerId) const
{
	return Trainers.FindByPredicate(
		[TrainerId](const FBattleTrainerState& Trainer)
		{
			return Trainer.TrainerId == TrainerId;
		});
}

FBattleTrainerState* FBattleEngineState::FindMutableTrainer(const FTrainerId TrainerId)
{
	return Trainers.FindByPredicate(
		[TrainerId](const FBattleTrainerState& Trainer)
		{
			return Trainer.TrainerId == TrainerId;
		});
}

const FBattleBattlerState* FBattleEngineState::FindBattler(const FBattlerId BattlerId) const
{
	return Battlers.FindByPredicate(
		[BattlerId](const FBattleBattlerState& Battler)
		{
			return Battler.BattlerId == BattlerId;
		});
}

FBattleBattlerState* FBattleEngineState::FindMutableBattler(const FBattlerId BattlerId)
{
	return Battlers.FindByPredicate(
		[BattlerId](const FBattleBattlerState& Battler)
		{
			return Battler.BattlerId == BattlerId;
		});
}

const FBattleActivePositionState* FBattleEngineState::FindActivePosition(
	const FActiveSlotId ActiveSlotId) const
{
	return ActivePositions.FindByPredicate(
		[ActiveSlotId](const FBattleActivePositionState& Position)
		{
			return Position.ActiveSlotId == ActiveSlotId;
		});
}

FBattleActivePositionState* FBattleEngineState::FindMutableActivePosition(
	const FActiveSlotId ActiveSlotId)
{
	return ActivePositions.FindByPredicate(
		[ActiveSlotId](const FBattleActivePositionState& Position)
		{
			return Position.ActiveSlotId == ActiveSlotId;
		});
}

TArray<FBattleTrainerSetup> FBattleEngineState::BuildTrainerProjection() const
{
	TArray<FBattleTrainerSetup> Projection;
	Projection.Reserve(Trainers.Num());
	for (const FBattleTrainerState& Trainer : Trainers)
	{
		FBattleTrainerSetup Entry;
		Entry.TrainerId = Trainer.TrainerId;
		Entry.Side = Trainer.Side;
		Entry.Role = Trainer.Role;
		Entry.Controller = Trainer.Controller;
		Entry.SelectorProfileId = Trainer.SelectorProfileId;
		Entry.Bag = Trainer.Bag;
		Projection.Add(MoveTemp(Entry));
	}
	return Projection;
}

TArray<FBattlePartyEntrySetup> FBattleEngineState::BuildPartyProjection() const
{
	TArray<FBattlePartyEntrySetup> Projection;
	Projection.Reserve(Battlers.Num());
	for (const FBattleBattlerState& Battler : Battlers)
	{
		FBattlePartyEntrySetup Entry;
		Entry.TrainerId = Battler.TrainerId;
		Entry.BattlerId = Battler.BattlerId;
		Entry.SourcePokemonId = Battler.SourcePokemonId;
		Entry.PartySlotId = Battler.PartySlotId;
		Entry.SpeciesFormId = Battler.SpeciesFormId;
		Entry.Level = Battler.Level;
		Entry.Stats = Battler.PermanentStats;
		Entry.CurrentHP = Battler.CurrentHP;
		Entry.bEgg = Battler.bEgg;
		Entry.AbilityId = Battler.AbilityId;
		Entry.OriginalHeldItemId = Battler.HeldItem.OriginalItemId;
		Entry.CurrentHeldItemId = Battler.HeldItem.CurrentItemId;
		for (const FBattleMoveSlotState& Move : Battler.Moves)
		{
			Entry.Moves.Add({Move.SlotIndex, Move.MoveId, Move.CurrentPP, Move.MaxPP});
		}
		Projection.Add(MoveTemp(Entry));
	}
	return Projection;
}

TArray<FBattleActiveAssignment> FBattleEngineState::BuildActiveProjection() const
{
	TArray<FBattleActiveAssignment> Projection;
	for (const FBattleActivePositionState& Position : ActivePositions)
	{
		if (Position.BattlerId.IsValid())
		{
			Projection.Add({Position.ActiveSlotId, Position.TrainerId, Position.BattlerId});
		}
	}
	return Projection;
}

void FBattleEngineState::AppendResolution(const FBattleResolution& Resolution)
{
	check(Resolution.IsValid());
	Resolutions.Add(Resolution);
	for (const FBattleEvent& Event : Resolution.GetEvents())
	{
		OrderedEvents.Add(Event);
	}
}
