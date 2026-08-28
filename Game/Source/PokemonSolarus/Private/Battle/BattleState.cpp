#include "Battle/BattleState.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleWildFlow.h"

#include "Battle/BattleFieldSideConditions.h"
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

	bool IsKnownCaptureClassification(
		const EBattleCaptureSpeciesClassification Value)
	{
		return Value == EBattleCaptureSpeciesClassification::Normal
			|| Value == EBattleCaptureSpeciesClassification::UltraBeast;
	}

	bool StateActiveSlotLess(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		if (Left.GetSide() != Right.GetSide())
		{
			return static_cast<uint8>(Left.GetSide())
				< static_cast<uint8>(Right.GetSide());
		}
		return static_cast<uint8>(Left.GetPosition())
			< static_cast<uint8>(Right.GetPosition());
	}

	bool IsKnownCommandBand(const EBattleActionCommandBand Value)
	{
		return Value == EBattleActionCommandBand::Move
			|| Value == EBattleActionCommandBand::Bag
			|| Value == EBattleActionCommandBand::VoluntarySwitch
			|| Value == EBattleActionCommandBand::Run;
	}

	bool IsKnownEffectExecutionState(const EBattleLockedEffectExecutionState Value)
	{
		return Value == EBattleLockedEffectExecutionState::Pending
			|| Value == EBattleLockedEffectExecutionState::Executing
			|| Value == EBattleLockedEffectExecutionState::AwaitingPivot
			|| Value == EBattleLockedEffectExecutionState::Completed;
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

	bool IsConditionValidForFamily(
		const FBattleConditionState& Condition,
		const FBattleDefinitionCatalog* Catalog,
		const TArray<EBattleConditionKind>& AllowedFamilies)
	{
		if (!IsConditionValid(Condition, Catalog))
		{
			return false;
		}
		const FBattleConditionDefinition* Definition = Catalog != nullptr
			? Catalog->FindCondition(Condition.ConditionId)
			: nullptr;
		if (Definition != nullptr && !AllowedFamilies.Contains(Definition->Kind))
		{
			return false;
		}
		if (!FBattleFieldSideConditionRules::IsCanonical(Condition.ConditionId))
		{
			return true;
		}
		if (Definition != nullptr
			&& Definition->Kind
				!= FBattleFieldSideConditionRules::GetConditionFamily(Condition.ConditionId))
		{
			return false;
		}
		int32 MaximumLayers = 0;
		TOptional<int32> OrdinaryDuration;
		if (!FBattleFieldSideConditionRules::TryGetMaximumLayers(
				Condition.ConditionId,
				MaximumLayers)
			|| Condition.LayerCount <= 0
			|| Condition.LayerCount > MaximumLayers
			|| !FBattleFieldSideConditionRules::TryGetDuration(
				Condition.ConditionId,
				false,
				OrdinaryDuration))
		{
			return false;
		}
		if (!OrdinaryDuration.IsSet())
		{
			return !Condition.RemainingTurns.IsSet();
		}
		if (!Condition.RemainingTurns.IsSet())
		{
			return false;
		}
		int32 MaximumDuration = OrdinaryDuration.GetValue();
		TOptional<int32> ExtendedDuration;
		if (FBattleFieldSideConditionRules::SupportsDurationExtension(Condition.ConditionId)
			&& FBattleFieldSideConditionRules::TryGetDuration(
				Condition.ConditionId,
				true,
				ExtendedDuration)
			&& ExtendedDuration.IsSet())
		{
			MaximumDuration = ExtendedDuration.GetValue();
		}
		return Condition.RemainingTurns.GetValue() > 0
			&& Condition.RemainingTurns.GetValue() <= MaximumDuration;
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

	bool IsBattleStateKnownTargetClass(const EBattleTargetClass TargetClass)
	{
		return static_cast<uint8>(TargetClass)
			<= static_cast<uint8>(EBattleTargetClass::FixedOpponentSpreadSet);
	}

	bool DoesBattleStateTargetClassRequireSelection(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler
			|| TargetClass == EBattleTargetClass::SelectedOtherBattler;
	}

	int32 GetBattleStateActiveSlotOrder(const FActiveSlotId ActiveSlotId)
	{
		const int32 SideOffset = ActiveSlotId.GetSide() == EBattleSide::Player ? 0 : 2;
		const int32 PositionOffset = ActiveSlotId.GetPosition() == EBattlePosition::Left ? 0 : 1;
		return SideOffset + PositionOffset;
	}

	bool IsBattleStateTargetResolutionValid(const FBattleLockedActionState& Action)
	{
		if (!Action.TargetResolution.IsSet())
		{
			return true;
		}

		const FBattleTargetResolutionResult& Resolution = Action.TargetResolution.GetValue();
		if (!Action.bStarted
			|| !Action.bMoveCommitted
			|| Action.Decision.GetActionKind() != EBattleActionKind::Fight
			|| Resolution.TargetClass != Action.TargetClass
			|| (Resolution.Outcome != EBattleTargetResolutionOutcome::Resolved
				&& Resolution.Outcome != EBattleTargetResolutionOutcome::NoLegalTarget)
			|| (Resolution.bUsedFaintedTargetFallback && !Resolution.bWasRedirected)
			|| (Resolution.bUsedFaintedTargetFallback
				&& Action.TargetClass != EBattleTargetClass::SelectedOpponent
				&& Action.TargetClass != EBattleTargetClass::AnySelectedBattler
				&& Action.TargetClass != EBattleTargetClass::SelectedOtherBattler)
			|| (Resolution.Outcome == EBattleTargetResolutionOutcome::Resolved) == Resolution.Targets.IsEmpty()
			|| (Resolution.Targets.IsEmpty()
				&& (Resolution.bWasRedirected || Resolution.bUsedFaintedTargetFallback)))
		{
			return false;
		}

		for (int32 Index = 0; Index < Resolution.Targets.Num(); ++Index)
		{
			const FBattleResolvedTarget& Target = Resolution.Targets[Index];
			if (!Target.IsValid())
			{
				return false;
			}
			for (int32 PriorIndex = 0; PriorIndex < Index; ++PriorIndex)
			{
				const FBattleResolvedTarget& PriorTarget = Resolution.Targets[PriorIndex];
				if (PriorTarget == Target
					|| (PriorTarget.GetKind() == EBattleResolvedTargetKind::Battler
						&& Target.GetKind() == EBattleResolvedTargetKind::Battler
						&& (PriorTarget.GetBattler().BattlerId == Target.GetBattler().BattlerId
							|| PriorTarget.GetBattler().ActiveSlotId
								== Target.GetBattler().ActiveSlotId)))
				{
					return false;
				}
			}
			if (Index > 0 && Resolution.Targets[Index - 1].GetKind() != Target.GetKind())
			{
				return false;
			}
			if (Index > 0 && Target.GetKind() == EBattleResolvedTargetKind::Battler
				&& GetBattleStateActiveSlotOrder(Resolution.Targets[Index - 1].GetBattler().ActiveSlotId)
					>= GetBattleStateActiveSlotOrder(Target.GetBattler().ActiveSlotId))
			{
				return false;
			}
			if (Index > 0 && Target.GetKind() == EBattleResolvedTargetKind::Side
				&& static_cast<uint8>(Resolution.Targets[Index - 1].GetSide())
					>= static_cast<uint8>(Target.GetSide()))
			{
				return false;
			}
		}

		const EBattleSide UserSide = Action.OrderKey.ActingSlotId.GetSide();
		const EBattleSide OtherSide = UserSide == EBattleSide::Player
			? EBattleSide::Opponent
			: EBattleSide::Player;
		if (Resolution.Outcome == EBattleTargetResolutionOutcome::NoLegalTarget)
		{
			return Action.TargetClass == EBattleTargetClass::SelectedAlly
				|| Action.TargetClass == EBattleTargetClass::SelectedOpponent
				|| Action.TargetClass == EBattleTargetClass::AnySelectedBattler
				|| Action.TargetClass == EBattleTargetClass::SelectedOtherBattler
				|| Action.TargetClass == EBattleTargetClass::RandomLegalOpponent
				|| Action.TargetClass == EBattleTargetClass::FixedSpreadSet
				|| Action.TargetClass == EBattleTargetClass::FixedOpponentSpreadSet;
		}

		switch (Action.TargetClass)
		{
		case EBattleTargetClass::Self:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler
				&& Resolution.Targets[0].GetBattler().ActiveSlotId == Action.OrderKey.ActingSlotId
				&& Resolution.Targets[0].GetBattler().BattlerId == Action.Decision.GetActingBattlerId()
				&& !Resolution.bWasRedirected;
		case EBattleTargetClass::SelectedAlly:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler
				&& Resolution.Targets[0].GetBattler().ActiveSlotId.GetSide() == UserSide
				&& (Resolution.Targets[0].GetBattler().ActiveSlotId != Action.OrderKey.ActingSlotId
					|| Resolution.Targets[0].GetBattler().BattlerId != Action.Decision.GetActingBattlerId());
		case EBattleTargetClass::SelectedOpponent:
		case EBattleTargetClass::RandomLegalOpponent:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler
				&& Resolution.Targets[0].GetBattler().ActiveSlotId.GetSide() == OtherSide;
		case EBattleTargetClass::AnySelectedBattler:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler;
		case EBattleTargetClass::SelectedOtherBattler:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler
				&& Resolution.Targets[0].GetBattler().ActiveSlotId
					!= Action.OrderKey.ActingSlotId
				&& Resolution.Targets[0].GetBattler().BattlerId
					!= Action.Decision.GetActingBattlerId();
		case EBattleTargetClass::UserSide:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Side
				&& Resolution.Targets[0].GetSide() == UserSide
				&& !Resolution.bWasRedirected;
		case EBattleTargetClass::OpponentSide:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Side
				&& Resolution.Targets[0].GetSide() == OtherSide
				&& !Resolution.bWasRedirected;
		case EBattleTargetClass::BothSides:
			return Resolution.Targets.Num() == 2
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Side
				&& Resolution.Targets[0].GetSide() == EBattleSide::Player
				&& Resolution.Targets[1].GetSide() == EBattleSide::Opponent
				&& !Resolution.bWasRedirected;
		case EBattleTargetClass::Field:
			return Resolution.Targets.Num() == 1
				&& Resolution.Targets[0].GetKind() == EBattleResolvedTargetKind::Field
				&& !Resolution.bWasRedirected;
		case EBattleTargetClass::FixedSpreadSet:
			if (Resolution.bWasRedirected || Resolution.Targets.Num() > 3)
			{
				return false;
			}
			for (const FBattleResolvedTarget& Target : Resolution.Targets)
			{
				if (Target.GetKind() != EBattleResolvedTargetKind::Battler
					|| (Target.GetBattler().ActiveSlotId == Action.OrderKey.ActingSlotId
						&& Target.GetBattler().BattlerId == Action.Decision.GetActingBattlerId()))
				{
					return false;
				}
			}
			return true;
		case EBattleTargetClass::FixedOpponentSpreadSet:
			if (Resolution.bWasRedirected || Resolution.Targets.Num() > 2)
			{
				return false;
			}
			for (const FBattleResolvedTarget& Target : Resolution.Targets)
			{
				if (Target.GetKind() != EBattleResolvedTargetKind::Battler
					|| Target.GetBattler().ActiveSlotId.GetSide() != OtherSide)
				{
					return false;
				}
			}
			return true;
		default:
			return false;
		}
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
	NewState->CompiledEncounterPolicies = Setup.GetCompiledEncounterPolicies();
	if (!NewState->CompiledEncounterPolicies.IsValid())
	{
		OutError = EBattleStateValidationError::InvalidSetup;
		return false;
	}
	NewState->EncounterKind = NewState->CompiledEncounterPolicies.GetEncounterKind();
	NewState->Format = NewState->CompiledEncounterPolicies.GetFormat();
	NewState->CaptureCapacity = Setup.GetCaptureCapacity();
	if (NewState->CompiledEncounterPolicies.IsWildFleeConfigured())
	{
		FBattleWildFleePolicyState Policy;
		Policy.TriggerId = FBattleWildFleeRules::GetActionSelectionTriggerId();
		Policy.EligibilityId = FBattleWildFleeRules::GetActiveLivingWildEligibilityId();
		Policy.ProbabilityMode = NewState->CompiledEncounterPolicies.GetWildFleeMode();
		Policy.Numerator = NewState->CompiledEncounterPolicies.GetWildFleeNumerator();
		Policy.Denominator = NewState->CompiledEncounterPolicies.GetWildFleeDenominator();
		NewState->WildFleePolicies.Add(MoveTemp(Policy));
	}
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

	for (const FBattleTrainerEncounterPolicy& TrainerPolicy :
		NewState->CompiledEncounterPolicies.GetTrainerPolicies())
	{
		const FBattleTrainerSetup* SetupTrainer = Setup.FindTrainer(TrainerPolicy.TrainerId);
		if (SetupTrainer == nullptr)
		{
			OutError = EBattleStateValidationError::InvalidTrainer;
			return false;
		}
		FBattleTrainerState Trainer;
		Trainer.TrainerId = TrainerPolicy.TrainerId;
		Trainer.Side = TrainerPolicy.Side;
		Trainer.Role = TrainerPolicy.Role;
		Trainer.Controller = TrainerPolicy.Controller;
		Trainer.SelectorProfileId = TrainerPolicy.SelectorProfileId;
		Trainer.Bag = SetupTrainer->Bag;
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

	TArray<FBattleHeldItemInstanceState> InitialHeldItems;
	uint64 NextHeldItemInstanceValue = 1;
	for (const FBattlePartyEntrySetup& SetupEntry : Setup.GetPartyEntries())
	{
		FBattleBattlerState Battler;
		Battler.TrainerId = SetupEntry.TrainerId;
		Battler.BattlerId = SetupEntry.BattlerId;
		Battler.SourcePokemonId = SetupEntry.SourcePokemonId;
		Battler.PartySlotId = SetupEntry.PartySlotId;
		Battler.SpeciesFormId = SetupEntry.SpeciesFormId;
		Battler.CaptureClassification = SetupEntry.CaptureClassification;
		Battler.Level = SetupEntry.Level;
		Battler.PermanentStats = SetupEntry.Stats;
		Battler.CurrentHP = SetupEntry.CurrentHP;
		Battler.bFainted = SetupEntry.CurrentHP == 0;
		Battler.bEgg = SetupEntry.bEgg;
		Battler.bRemoved = SetupEntry.BattlerId
			== Setup.GetConfiguredReinforcementBattlerId();
		Battler.AbilityId = SetupEntry.AbilityId;
		Battler.HeldItem.OriginalItemId = SetupEntry.OriginalHeldItemId;
		Battler.HeldItem.CurrentItemId = SetupEntry.CurrentHeldItemId;
		if (SetupEntry.CurrentHeldItemId.IsValid()
			&& SetupEntry.CurrentHeldItemId != SetupEntry.OriginalHeldItemId)
		{
			OutError = EBattleStateValidationError::InvalidResource;
			return false;
		}
		if (SetupEntry.OriginalHeldItemId.IsValid())
		{
			if (!FBattleHeldItemInstanceId::TryCreate(
					NextHeldItemInstanceValue,
					Battler.HeldItem.InstanceId))
			{
				OutError = EBattleStateValidationError::InvalidCounter;
				return false;
			}
			++NextHeldItemInstanceValue;
			Battler.HeldItem.bConsumed = !SetupEntry.CurrentHeldItemId.IsValid();

			FBattleHeldItemInstanceState ItemState;
			ItemState.InstanceId = Battler.HeldItem.InstanceId;
			ItemState.Origin = EBattleHeldItemOrigin::Persistent;
			ItemState.DefinitionItemId = SetupEntry.OriginalHeldItemId;
			ItemState.OriginalOwnerTrainerId = SetupEntry.TrainerId;
			ItemState.OriginalOwnerBattlerId = SetupEntry.BattlerId;
			ItemState.OriginalItemId = SetupEntry.OriginalHeldItemId;
			ItemState.CurrentItemId = SetupEntry.CurrentHeldItemId;
			ItemState.bConsumed = Battler.HeldItem.bConsumed;
			if (SetupEntry.CurrentHeldItemId.IsValid())
			{
				ItemState.CurrentHolderTrainerId = SetupEntry.TrainerId;
				ItemState.CurrentHolderBattlerId = SetupEntry.BattlerId;
			}
			InitialHeldItems.Add(MoveTemp(ItemState));
		}
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
	EBattleHeldItemContractError HeldItemError = EBattleHeldItemContractError::None;
	if (!FBattleHeldItemLedger::TryCreate(
			InitialHeldItems,
			NewState->HeldItemLedger,
			HeldItemError))
	{
		OutError = EBattleStateValidationError::InvalidResource;
		return false;
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
		|| !CompiledEncounterPolicies.IsValid()
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
		|| EncounterKind != CompiledEncounterPolicies.GetEncounterKind()
		|| Format != CompiledEncounterPolicies.GetFormat()
		|| !IsKnownPhase(Phase)
		|| !IsKnownOutcome(Outcome)
		|| (Phase == EBattlePhase::Terminal) != (Outcome != EBattleOutcome::InProgress)
		|| (Outcome == EBattleOutcome::InProgress && OutcomeCause != EBattleOutcomeCause::None)
		|| (Phase == EBattlePhase::Terminal
			&& (PendingDecision.IsSet()
				|| !PendingDecisionRequests.IsEmpty()
				|| !PendingReplacements.IsEmpty())))
	{
		return Fail(EBattleStateValidationError::InvalidLifecycle);
	}
	if (StateVersion == 0
		|| NextResolutionId == 0
		|| NextActionId == 0
		|| NextEventOrdinal == 0
		|| NextConditionCreationOrdinal == 0
		|| NextTriggerReentrancyToken == 0
		|| EscapeAttemptCount == 0)
	{
		return Fail(EBattleStateValidationError::InvalidCounter);
	}

	const int32 ExpectedTrainerCount = Format == EBattleFormat::PartnerDouble ? 3 : 2;
	if (Trainers.Num() != ExpectedTrainerCount
		|| Trainers.Num() != CompiledEncounterPolicies.GetTrainerPolicies().Num())
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
		const FBattleTrainerEncounterPolicy* TrainerPolicy =
			CompiledEncounterPolicies.FindTrainerPolicy(Trainer.TrainerId);
		if (!Trainer.TrainerId.IsValid()
			|| TrainerPolicy == nullptr
			|| !IsKnownSide(Trainer.Side)
			|| !IsKnownRole(Trainer.Role)
			|| !IsKnownController(Trainer.Controller)
			|| !Trainer.SelectorProfileId.IsValid()
			|| Trainer.Side != TrainerPolicy->Side
			|| Trainer.Role != TrainerPolicy->Role
			|| Trainer.Controller != TrainerPolicy->Controller
			|| Trainer.SelectorProfileId != TrainerPolicy->SelectorProfileId
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

	int32 BattlersWithOriginalHeldItems = 0;
	TArray<FBattleHeldItemInstanceId> ReferencedHeldItemInstances;
	for (const FBattleBattlerState& Battler : Battlers)
	{
		const FBattleTrainerState* Trainer = FindTrainer(Battler.TrainerId);
		if (Trainer == nullptr
			|| !Battler.BattlerId.IsValid()
			|| !Battler.SourcePokemonId.IsValid()
			|| !Battler.PartySlotId.IsValid()
			|| !Battler.SpeciesFormId.IsValid()
			|| !IsKnownCaptureClassification(Battler.CaptureClassification)
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
		if (Battler.HeldItem.OriginalItemId.IsValid())
		{
			++BattlersWithOriginalHeldItems;
			bool bOriginalLedgerItemFound = false;
			for (const FBattleHeldItemInstanceState& Item : HeldItemLedger.GetStates())
			{
				if (Item.Origin == EBattleHeldItemOrigin::Persistent
					&& Item.OriginalOwnerTrainerId == Battler.TrainerId
					&& Item.OriginalOwnerBattlerId == Battler.BattlerId
					&& Item.OriginalItemId == Battler.HeldItem.OriginalItemId)
				{
					bOriginalLedgerItemFound = true;
					break;
				}
			}
			if (!bOriginalLedgerItemFound)
			{
				return Fail(EBattleStateValidationError::InvalidResource);
			}
		}

		if (!Battler.HeldItem.InstanceId.IsValid())
		{
			if (Battler.HeldItem.CurrentItemId.IsValid()
				|| Battler.HeldItem.bConsumed
				|| Battler.HeldItem.bSuppressed
				|| Battler.HeldItem.bRevealed
				|| Battler.HeldItem.bTemporarilyRemoved
				|| Battler.HeldItem.ChoiceLockedMoveId.IsValid())
			{
				return Fail(EBattleStateValidationError::InvalidResource);
			}
		}
		else
		{
			if (ReferencedHeldItemInstances.Contains(Battler.HeldItem.InstanceId))
			{
				return Fail(EBattleStateValidationError::InvalidResource);
			}
			ReferencedHeldItemInstances.Add(Battler.HeldItem.InstanceId);
			const FBattleHeldItemInstanceState* LedgerItem = HeldItemLedger.FindState(
				Battler.HeldItem.InstanceId);
			if (LedgerItem == nullptr
				|| LedgerItem->CurrentItemId != Battler.HeldItem.CurrentItemId
				|| LedgerItem->bConsumed != Battler.HeldItem.bConsumed
				|| LedgerItem->bSuppressed != Battler.HeldItem.bSuppressed
				|| LedgerItem->bRevealed != Battler.HeldItem.bRevealed
				|| LedgerItem->bTemporarilyRemoved
					!= Battler.HeldItem.bTemporarilyRemoved
				|| (Battler.HeldItem.CurrentItemId.IsValid()
					&& !Battler.HeldItem.bTemporarilyRemoved
					&& (LedgerItem->CurrentHolderTrainerId != Battler.TrainerId
						|| LedgerItem->CurrentHolderBattlerId != Battler.BattlerId))
				|| ((!Battler.HeldItem.CurrentItemId.IsValid()
						|| Battler.HeldItem.bTemporarilyRemoved)
					&& (LedgerItem->CurrentHolderTrainerId.IsValid()
						|| LedgerItem->CurrentHolderBattlerId.IsValid()))
				|| (Battler.HeldItem.ChoiceLockedMoveId.IsValid()
					&& (Battler.HeldItem.bConsumed
						|| Battler.HeldItem.bSuppressed
						|| Battler.HeldItem.bTemporarilyRemoved
						|| Battler.HeldItem.CurrentItemId
							!= FBattleItemRules::GetChoiceBandId()
						|| !Battler.Moves.ContainsByPredicate(
							[&Battler](const FBattleMoveSlotState& Move)
							{
								return Move.MoveId
									== Battler.HeldItem.ChoiceLockedMoveId;
							}))))
			{
				return Fail(EBattleStateValidationError::InvalidResource);
			}
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
			if (Battler.LastMoveId.IsValid()
				&& Catalog.FindMove(Battler.LastMoveId) == nullptr
				&& Battler.LastMoveId != FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
			{
				return Fail(EBattleStateValidationError::MissingCatalogReference);
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
	if (HeldItemLedger.GetStates().Num() < BattlersWithOriginalHeldItems)
	{
		return Fail(EBattleStateValidationError::InvalidResource);
	}
	for (const FBattleHeldItemInstanceState& Item : HeldItemLedger.GetStates())
	{
		if (Item.Origin == EBattleHeldItemOrigin::Persistent)
		{
			const FBattleBattlerState* OriginalOwner = FindBattler(
				Item.OriginalOwnerBattlerId);
			if (OriginalOwner == nullptr
				|| OriginalOwner->TrainerId != Item.OriginalOwnerTrainerId
				|| OriginalOwner->HeldItem.OriginalItemId != Item.OriginalItemId)
			{
				return Fail(EBattleStateValidationError::InvalidResource);
			}
		}
		if (!Item.CurrentHolderBattlerId.IsValid())
		{
			continue;
		}
		const FBattleBattlerState* Holder = FindBattler(Item.CurrentHolderBattlerId);
		if (Holder == nullptr
			|| Holder->TrainerId != Item.CurrentHolderTrainerId
			|| Holder->HeldItem.InstanceId != Item.InstanceId
			|| Holder->HeldItem.CurrentItemId != Item.CurrentItemId
			|| Holder->HeldItem.bConsumed
			|| Holder->HeldItem.bTemporarilyRemoved)
		{
			return Fail(EBattleStateValidationError::InvalidResource);
		}
	}

	if ((Phase == EBattlePhase::MandatoryReplacement) != !PendingReplacements.IsEmpty()
		|| HasDuplicatePair(
			PendingReplacements,
			[](const FBattlePendingReplacementState& Left,
				const FBattlePendingReplacementState& Right)
			{
				return Left.ActiveSlotId == Right.ActiveSlotId;
			}))
	{
		return Fail(EBattleStateValidationError::InvalidLifecycle);
	}
	for (int32 PendingIndex = 0;
		PendingIndex < PendingReplacements.Num();
		++PendingIndex)
	{
		const FBattlePendingReplacementState& Pending = PendingReplacements[PendingIndex];
		const FBattleTrainerState* Trainer = FindTrainer(Pending.TrainerId);
		const FBattleActivePositionState* Position = FindActivePosition(Pending.ActiveSlotId);
		FTrainerId InitialOwner;
		for (const FBattleActiveAssignment& Assignment : Setup.GetStartingActive())
		{
			if (Assignment.ActiveSlotId == Pending.ActiveSlotId)
			{
				InitialOwner = Assignment.TrainerId;
				break;
			}
		}
		if (!Pending.TrainerId.IsValid()
			|| !Pending.ActiveSlotId.IsValid()
			|| Trainer == nullptr
			|| Position == nullptr
			|| InitialOwner != Pending.TrainerId
			|| Trainer->Side != Pending.ActiveSlotId.GetSide()
			|| !Position->bAvailable
			|| Position->TrainerId.IsValid()
			|| Position->BattlerId.IsValid()
			|| (PendingIndex > 0
				&& !StateActiveSlotLess(
					PendingReplacements[PendingIndex - 1].ActiveSlotId,
					Pending.ActiveSlotId)))
		{
			return Fail(EBattleStateValidationError::InvalidLifecycle);
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
	if (!FBattleMoveRedirection::IsRegistrationCollectionValid(
			Format,
			TurnId,
			MoveRedirectionRegistrations,
			Battlers,
			ActivePositions))
	{
		return Fail(EBattleStateValidationError::InvalidLifecycle);
	}

	if (Sides.Num() != 2
		|| Sides[0].Side != EBattleSide::Player
		|| Sides[1].Side != EBattleSide::Opponent)
	{
		return Fail(EBattleStateValidationError::InvalidCondition);
	}
	const FBattleDefinitionCatalog* ConditionCatalog = bHasCatalog ? &Catalog : nullptr;
	auto ValidateConditionArray = [ConditionCatalog](
		const TArray<FBattleConditionState>& Conditions,
		const TArray<EBattleConditionKind>& AllowedFamilies)
	{
		return !HasDuplicatePair(
				Conditions,
				[](const FBattleConditionState& Left, const FBattleConditionState& Right)
				{
					return Left.ConditionId == Right.ConditionId;
				})
			&& !Conditions.ContainsByPredicate(
			[ConditionCatalog, &AllowedFamilies](const FBattleConditionState& Condition)
			{
				return !IsConditionValidForFamily(
					Condition,
					ConditionCatalog,
					AllowedFamilies);
			});
	};
	if ((Field.Weather.IsSet()
			&& !IsConditionValidForFamily(
				Field.Weather.GetValue(),
				ConditionCatalog,
				{EBattleConditionKind::Weather}))
		|| (Field.Terrain.IsSet()
			&& !IsConditionValidForFamily(
				Field.Terrain.GetValue(),
				ConditionCatalog,
				{EBattleConditionKind::Terrain}))
		|| !ValidateConditionArray(Field.Rooms, {EBattleConditionKind::Room})
		|| Field.Effects.ContainsByPredicate(
			[ConditionCatalog](const FBattleConditionState& Condition)
			{
				return !IsConditionValid(Condition, ConditionCatalog);
			}))
	{
		return Fail(EBattleStateValidationError::InvalidCondition);
	}
	for (const FBattleSideState& Side : Sides)
	{
		if (!ValidateConditionArray(
				Side.Conditions,
				{EBattleConditionKind::Screen, EBattleConditionKind::SideCondition})
			|| !ValidateConditionArray(Side.Hazards, {EBattleConditionKind::Hazard}))
		{
			return Fail(EBattleStateValidationError::InvalidCondition);
		}
	}

	const int64 TotalCaptureCapacity = static_cast<int64>(CaptureCapacity.PartySlotsRemaining)
		+ static_cast<int64>(CaptureCapacity.StorageSlotsRemaining);
	if (CaptureCapacity.PartySlotsRemaining < 0
		|| CaptureCapacity.StorageSlotsRemaining < 0
		|| PendingCaptures.Num() > TotalCaptureCapacity)
	{
		return Fail(EBattleStateValidationError::InvalidResource);
	}
	for (int32 CaptureIndex = 0; CaptureIndex < PendingCaptures.Num(); ++CaptureIndex)
	{
		const FBattlePendingCaptureState& Capture = PendingCaptures[CaptureIndex];
		const EBattlePendingCaptureDestination ExpectedDestination =
			CaptureIndex < CaptureCapacity.PartySlotsRemaining
				? EBattlePendingCaptureDestination::Party
				: EBattlePendingCaptureDestination::Storage;
		const FBattleBattlerState* CapturedBattler = FindBattler(Capture.BattlerId);
		if (!Capture.IsValid()
			|| Capture.CaptureOrdinal != static_cast<uint64>(CaptureIndex + 1)
			|| Capture.Destination != ExpectedDestination
			|| CapturedBattler == nullptr
			|| !CapturedBattler->bCaptured
			|| !CapturedBattler->bRemoved
			|| CapturedBattler->TrainerId != Capture.OriginalTrainerId
			|| CapturedBattler->SourcePokemonId != Capture.SourcePokemonId
			|| CapturedBattler->SpeciesFormId != Capture.SpeciesFormId
			|| CapturedBattler->CaptureClassification != Capture.SpeciesClassification)
		{
			return Fail(EBattleStateValidationError::InvalidPendingCapture);
		}
	}
	for (const FBattleBattlerState& Battler : Battlers)
	{
		const bool bHasPendingCapture = PendingCaptures.ContainsByPredicate(
			[&Battler](const FBattlePendingCaptureState& Capture)
			{
				return Capture.BattlerId == Battler.BattlerId;
			});
		if (Battler.bCaptured != bHasPendingCapture)
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
	if (CurrentLockedActionIndex < 0 || CurrentLockedActionIndex > LockedActions.Num())
	{
		return Fail(EBattleStateValidationError::InvalidCounter);
	}
	int32 AwaitingPivotActionIndex = INDEX_NONE;
	for (int32 ActionIndex = 0; ActionIndex < LockedActions.Num(); ++ActionIndex)
	{
		const FBattleLockedActionState& Action = LockedActions[ActionIndex];
		const bool bMoveBand = Action.OrderKey.CommandBand == EBattleActionCommandBand::Move;
		const bool bFight = Action.Decision.GetActionKind() == EBattleActionKind::Fight;
		const bool bRequiresSelectedTarget = bFight
			&& DoesBattleStateTargetClassRequireSelection(Action.TargetClass);
		const bool bEffectsExecuting = Action.EffectExecutionState
			== EBattleLockedEffectExecutionState::Executing;
		const bool bEffectsAwaitingPivot = Action.EffectExecutionState
			== EBattleLockedEffectExecutionState::AwaitingPivot;
		const bool bEffectsCompleted = Action.EffectExecutionState
			== EBattleLockedEffectExecutionState::Completed;
		const bool bHasResolvedEffectTargets = Action.TargetResolution.IsSet()
			&& Action.TargetResolution.GetValue().Outcome
				== EBattleTargetResolutionOutcome::Resolved;
		if (!Action.ActionId.IsValid()
			|| Action.QueueOrdinal != static_cast<uint64>(ActionIndex + 1)
			|| !Action.Decision.IsValid()
			|| !IsKnownCommandBand(Action.OrderKey.CommandBand)
			|| !Action.OrderKey.ActingSlotId.IsValid()
			|| Action.OrderKey.EffectiveSpeed < 0
			|| bMoveBand != bFight
			|| (bMoveBand
				&& (!IsBattleStateKnownTargetClass(Action.TargetClass)
					|| Action.OrderKey.MovePriority < -7
					|| Action.OrderKey.MovePriority > 5
					|| Action.OrderKey.FractionalPriorityTenths < 0
					|| Action.OrderKey.FractionalPriorityTenths > 9
					|| Action.Decision.GetActiveTargetId().IsValid() != bRequiresSelectedTarget
					|| Action.SelectedTargetBattlerId.IsValid() != bRequiresSelectedTarget))
			|| (!bMoveBand
				&& (Action.OrderKey.MovePriority != 0
					|| Action.OrderKey.FractionalPriorityTenths != 0
					|| Action.TargetResolution.IsSet()))
			|| (Action.bMoveCommitted
				&& (!Action.bStarted || Action.Decision.GetActionKind() != EBattleActionKind::Fight))
			|| !IsKnownEffectExecutionState(Action.EffectExecutionState)
			|| ((bEffectsExecuting || bEffectsAwaitingPivot || bEffectsCompleted)
				&& (!bFight
					|| !Action.bStarted
					|| !Action.bMoveCommitted
					|| !bHasResolvedEffectTargets))
			|| (bEffectsExecuting && Action.bFinished)
			|| (bEffectsAwaitingPivot && Action.bFinished)
			|| (bEffectsCompleted && !Action.bFinished)
			|| (bFight
				&& Action.bFinished
				&& bHasResolvedEffectTargets
				&& !bEffectsCompleted)
			|| !IsBattleStateTargetResolutionValid(Action))
		{
			return Fail(EBattleStateValidationError::InvalidCounter);
		}
		if (bEffectsAwaitingPivot)
		{
			if (AwaitingPivotActionIndex != INDEX_NONE)
			{
				return Fail(EBattleStateValidationError::InvalidLifecycle);
			}
			AwaitingPivotActionIndex = ActionIndex;
		}
	}

	const bool bHasPivotRequest = PendingDecision.IsSet()
		&& PendingDecision.GetValue().GetRequestKind()
			== EBattleDecisionRequestKind::PivotSwitch;
	if ((AwaitingPivotActionIndex != INDEX_NONE) != bHasPivotRequest)
	{
		return Fail(EBattleStateValidationError::InvalidLifecycle);
	}
	if (bHasPivotRequest)
	{
		const FBattleDecisionRequest& Request = PendingDecision.GetValue();
		if (Phase != EBattlePhase::Resolving
			|| AwaitingPivotActionIndex != CurrentLockedActionIndex
			|| !LockedActions.IsValidIndex(AwaitingPivotActionIndex)
			|| PendingDecisionRequests.Num() != 1
			|| PendingDecisionRequests[0].GetRequestKind()
				!= EBattleDecisionRequestKind::PivotSwitch
			|| PendingDecisionRequests[0].GetStateVersion() != Request.GetStateVersion()
			|| PendingDecisionRequests[0].GetActingBattlerId() != Request.GetActingBattlerId()
			|| Request.GetStateVersion() != StateVersion
			|| Request.GetActingBattlerId()
				!= LockedActions[AwaitingPivotActionIndex].Decision.GetActingBattlerId()
			|| Request.GetActingSlotId()
				!= LockedActions[AwaitingPivotActionIndex].OrderKey.ActingSlotId
			|| Request.GetLegalActionKinds().Num() != 1
			|| Request.GetLegalActionKinds()[0] != EBattleActionKind::Switch
			|| Request.GetLegalSwitchPartySlots().IsEmpty())
		{
			return Fail(EBattleStateValidationError::InvalidLifecycle);
		}
	}
	const bool bHasReplacementPhaseRequest = PendingDecision.IsSet()
		&& (PendingDecision.GetValue().GetRequestKind()
				== EBattleDecisionRequestKind::MandatoryReplacement
			|| PendingDecision.GetValue().GetRequestKind()
				== EBattleDecisionRequestKind::ShiftResponse);
	if (Phase == EBattlePhase::MandatoryReplacement)
	{
		if (!bHasReplacementPhaseRequest
			|| PendingDecisionRequests.IsEmpty()
			|| PendingDecisionRequests[0].GetRequestKind()
				!= PendingDecision.GetValue().GetRequestKind()
			|| PendingDecisionRequests[0].GetStateVersion() != StateVersion
			|| PendingDecision.GetValue().GetStateVersion() != StateVersion)
		{
			return Fail(EBattleStateValidationError::InvalidLifecycle);
		}

		const EBattleDecisionRequestKind RequestKind =
			PendingDecisionRequests[0].GetRequestKind();
		if (RequestKind == EBattleDecisionRequestKind::ShiftResponse)
		{
			const FBattleDecisionRequest& Request = PendingDecisionRequests[0];
			const FBattleActivePositionState* ActingPosition = FindActivePosition(
				Request.GetActingSlotId());
			const FBattleBattlerState* ActingBattler = FindBattler(
				Request.GetActingBattlerId());
			const FBattleTrainerState* Owner = FindTrainer(
				Request.GetDecisionOwnerTrainerId());
			if (PendingDecisionRequests.Num() != 1
				|| CompiledEncounterPolicies.GetBattleStyle() != EBattleStylePolicy::Shift
				|| Format != EBattleFormat::Single
				|| EncounterKind == EBattleEncounterKind::Wild
				|| Owner == nullptr
				|| Owner->Side != EBattleSide::Player
				|| Owner->Role != EBattleTrainerRole::Player
				|| ActingPosition == nullptr
				|| ActingBattler == nullptr
				|| ActingPosition->TrainerId != Owner->TrainerId
				|| ActingPosition->BattlerId != ActingBattler->BattlerId
				|| Request.GetLegalActionKinds().Num() != 1
				|| Request.GetLegalActionKinds()[0] != EBattleActionKind::Switch
				|| Request.GetLegalSwitchPartySlots().IsEmpty()
				|| PendingReplacements.ContainsByPredicate(
					[](const FBattlePendingReplacementState& Pending)
					{
						return Pending.ActiveSlotId.GetSide() == EBattleSide::Player;
					}))
			{
				return Fail(EBattleStateValidationError::InvalidLifecycle);
			}
		}
		else
		{
			const FTrainerId OwnerTrainerId =
				PendingDecisionRequests[0].GetDecisionOwnerTrainerId();
			int32 ExpectedRequestCount = 0;
			for (const FBattlePendingReplacementState& Pending : PendingReplacements)
			{
				if (Pending.TrainerId == OwnerTrainerId)
				{
					if (!PendingDecisionRequests.IsValidIndex(ExpectedRequestCount))
					{
						return Fail(EBattleStateValidationError::InvalidLifecycle);
					}
					const FBattleDecisionRequest& Request =
						PendingDecisionRequests[ExpectedRequestCount];
					if (Request.GetRequestKind()
							!= EBattleDecisionRequestKind::MandatoryReplacement
						|| Request.GetDecisionOwnerTrainerId() != OwnerTrainerId
						|| Request.GetActingBattlerId().IsValid()
						|| Request.GetActingSlotId() != Pending.ActiveSlotId
						|| Request.GetLegalActionKinds().Num() != 1
						|| Request.GetLegalActionKinds()[0]
							!= EBattleActionKind::Replacement
						|| Request.GetLegalSwitchPartySlots().IsEmpty())
					{
						return Fail(EBattleStateValidationError::InvalidLifecycle);
					}
					++ExpectedRequestCount;
				}
			}
			if (OwnerTrainerId != PendingReplacements[0].TrainerId
				|| ExpectedRequestCount != PendingDecisionRequests.Num())
			{
				return Fail(EBattleStateValidationError::InvalidLifecycle);
			}
		}
	}
	else if (bHasReplacementPhaseRequest)
	{
		return Fail(EBattleStateValidationError::InvalidLifecycle);
	}

	if (!DecisionOwnerSequence.IsEmpty())
	{
		if (CurrentDecisionOwnerIndex < 0
			|| CurrentDecisionOwnerIndex > DecisionOwnerSequence.Num()
			|| CurrentDecisionActorOffset < 0
			|| (Phase == EBattlePhase::Selecting && PendingDecisionRequests.IsEmpty())
			|| (Phase != EBattlePhase::Selecting
				&& !PendingDecisionRequests.IsEmpty()
				&& !bHasPivotRequest
				&& !bHasReplacementPhaseRequest))
		{
			return Fail(EBattleStateValidationError::InvalidLifecycle);
		}

		for (const FBattleDecisionOwnerState& Owner : DecisionOwnerSequence)
		{
			if (!Owner.TrainerId.IsValid()
				|| FindTrainer(Owner.TrainerId) == nullptr
				|| Owner.Actors.IsEmpty()
				|| Owner.Actors.Num() > 2
				|| HasDuplicatePair(
					Owner.Actors,
					[](const FBattleDecisionActorState& Left, const FBattleDecisionActorState& Right)
					{
						return Left.BattlerId == Right.BattlerId
							|| Left.ActiveSlotId == Right.ActiveSlotId;
					}))
			{
				return Fail(EBattleStateValidationError::InvalidLifecycle);
			}
		}

		if (!PendingDecisionRequests.IsEmpty()
			&& Phase == EBattlePhase::Selecting
			&& !bHasPivotRequest)
		{
			if (!DecisionOwnerSequence.IsValidIndex(CurrentDecisionOwnerIndex)
				|| CurrentDecisionActorOffset >= DecisionOwnerSequence[CurrentDecisionOwnerIndex].Actors.Num()
				|| PendingDecisionRequests.Num()
					!= DecisionOwnerSequence[CurrentDecisionOwnerIndex].Actors.Num() - CurrentDecisionActorOffset
				|| !PendingDecision.IsSet())
			{
				return Fail(EBattleStateValidationError::InvalidLifecycle);
			}
			for (int32 RequestIndex = 0; RequestIndex < PendingDecisionRequests.Num(); ++RequestIndex)
			{
				const FBattleDecisionRequest& Request = PendingDecisionRequests[RequestIndex];
				const FBattleDecisionActorState& Actor =
					DecisionOwnerSequence[CurrentDecisionOwnerIndex].Actors[CurrentDecisionActorOffset + RequestIndex];
				if (!Request.IsValid()
					|| Request.GetStateVersion() != StateVersion
					|| Request.GetDecisionOwnerTrainerId()
						!= DecisionOwnerSequence[CurrentDecisionOwnerIndex].TrainerId
					|| Request.GetActingBattlerId() != Actor.BattlerId
					|| Request.GetActingSlotId() != Actor.ActiveSlotId)
				{
					return Fail(EBattleStateValidationError::InvalidLifecycle);
				}
			}
		}
	}
	for (const FBattleDecision& Decision : AcceptedSelections)
	{
		if (!Decision.IsValid())
		{
			return Fail(EBattleStateValidationError::InvalidLifecycle);
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
		Entry.CaptureClassification = Battler.CaptureClassification;
		Entry.Level = Battler.Level;
		Entry.Stats = Battler.PermanentStats;
		Entry.CurrentHP = Battler.CurrentHP;
		Entry.bEgg = Battler.bEgg;
		Entry.AbilityId = Battler.AbilityId;
		Entry.OriginalHeldItemId = Battler.HeldItem.OriginalItemId;
		Entry.CurrentHeldItemId = Battler.HeldItem.bTemporarilyRemoved
			? FItemId()
			: Battler.HeldItem.CurrentItemId;
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
