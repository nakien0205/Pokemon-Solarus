#include "Battle/BattleSetup.h"

#include "Battle/BattleEncounterPolicy.h"

namespace
{
	bool IsKnownBattleSetupEncounterKind(const EBattleEncounterKind Value)
	{
		return Value == EBattleEncounterKind::Wild
			|| Value == EBattleEncounterKind::Trainer
			|| Value == EBattleEncounterKind::Rival
			|| Value == EBattleEncounterKind::BossGym
			|| Value == EBattleEncounterKind::TutorialScripted;
	}

	bool IsKnownBattleSetupFormat(const EBattleFormat Value)
	{
		return Value == EBattleFormat::Single
			|| Value == EBattleFormat::Double
			|| Value == EBattleFormat::PartnerDouble;
	}

	bool IsKnownBattleSetupSide(const EBattleSide Value)
	{
		return Value == EBattleSide::Player || Value == EBattleSide::Opponent;
	}

	bool IsKnownBattleSetupRole(const EBattleTrainerRole Value)
	{
		return Value == EBattleTrainerRole::Player
			|| Value == EBattleTrainerRole::Partner
			|| Value == EBattleTrainerRole::Opponent;
	}

	bool IsKnownBattleSetupController(const EBattleDecisionController Value)
	{
		return Value == EBattleDecisionController::Human
			|| Value == EBattleDecisionController::PartnerAI
			|| Value == EBattleDecisionController::EnemyAI
			|| Value == EBattleDecisionController::Scripted;
	}

	bool IsKnownKnowledgeKind(const EBattleKnowledgeKind Value)
	{
		return Value == EBattleKnowledgeKind::SpeciesFormKnown
			|| Value == EBattleKnowledgeKind::MoveRevealed
			|| Value == EBattleKnowledgeKind::AbilityRevealed
			|| Value == EBattleKnowledgeKind::ItemRevealed;
	}

	bool IsKnownBattleSetupVisibility(const EBattleVisibilityLevel Value)
	{
		return Value == EBattleVisibilityLevel::CoreOnly
			|| Value == EBattleVisibilityLevel::OwningTrainer
			|| Value == EBattleVisibilityLevel::OwningSide
			|| Value == EBattleVisibilityLevel::Public;
	}

	bool IsKnownWildFleeMode(const EBattleWildFleeMode Value)
	{
		return Value == EBattleWildFleeMode::Disabled
			|| Value == EBattleWildFleeMode::Never
			|| Value == EBattleWildFleeMode::Always
			|| Value == EBattleWildFleeMode::Chance;
	}

	bool IsKnownCaptureClassification(
		const EBattleCaptureSpeciesClassification Value)
	{
		return Value == EBattleCaptureSpeciesClassification::Normal
			|| Value == EBattleCaptureSpeciesClassification::UltraBeast;
	}

	const FBattleTrainerSetup* FindTrainerIn(
		const TArray<FBattleTrainerSetup>& Trainers,
		const FTrainerId TrainerId)
	{
		return Trainers.FindByPredicate(
			[TrainerId](const FBattleTrainerSetup& Trainer)
			{
				return Trainer.TrainerId == TrainerId;
			});
	}

	const FBattlePartyEntrySetup* FindBattlerIn(
		const TArray<FBattlePartyEntrySetup>& Entries,
		const FBattlerId BattlerId)
	{
		return Entries.FindByPredicate(
			[BattlerId](const FBattlePartyEntrySetup& Entry)
			{
				return Entry.BattlerId == BattlerId;
			});
	}

	bool HasExpectedSlot(
		const TArray<FBattleActiveAssignment>& Assignments,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		return Assignments.ContainsByPredicate(
			[Side, Position](const FBattleActiveAssignment& Assignment)
			{
				return Assignment.ActiveSlotId.IsValid()
					&& Assignment.ActiveSlotId.GetSide() == Side
					&& Assignment.ActiveSlotId.GetPosition() == Position;
			});
	}

	const FBattleActiveAssignment* FindExpectedSlot(
		const TArray<FBattleActiveAssignment>& Assignments,
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		return Assignments.FindByPredicate(
			[Side, Position](const FBattleActiveAssignment& Assignment)
			{
				return Assignment.ActiveSlotId.IsValid()
					&& Assignment.ActiveSlotId.GetSide() == Side
					&& Assignment.ActiveSlotId.GetPosition() == Position;
			});
	}

	bool HasPositiveBattleSetupStats(const FPokemonBattleStats& Stats)
	{
		return Stats.MaxHP > 0
			&& Stats.Attack > 0
			&& Stats.Defense > 0
			&& Stats.SpecialAttack > 0
			&& Stats.SpecialDefense > 0
			&& Stats.Speed > 0;
	}

	template <typename ElementType, typename PredicateType>
	bool HasDuplicateBattleSetupPair(const TArray<ElementType>& Values, PredicateType Predicate)
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

	void Canonicalize(FBattleSetupInput& Input)
	{
		if (Input.EncounterKind == EBattleEncounterKind::Wild
			|| Input.Format != EBattleFormat::Single)
		{
			Input.Policies.bShiftPromptEligible = false;
		}

		for (FBattleTrainerSetup& Trainer : Input.Trainers)
		{
			Trainer.Bag.Sort(
				[](const FBattleBagItemCount& Left, const FBattleBagItemCount& Right)
				{
					return Left.ItemId.LexicalLess(Right.ItemId);
				});
		}

		Input.Trainers.Sort(
			[](const FBattleTrainerSetup& Left, const FBattleTrainerSetup& Right)
			{
				return Left.TrainerId < Right.TrainerId;
			});

		for (FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			Entry.Moves.Sort(
				[](const FBattleMoveSlotSetup& Left, const FBattleMoveSlotSetup& Right)
				{
					return Left.SlotIndex < Right.SlotIndex;
				});
		}

		Input.PartyEntries.Sort(
			[](const FBattlePartyEntrySetup& Left, const FBattlePartyEntrySetup& Right)
			{
				if (Left.TrainerId != Right.TrainerId)
				{
					return Left.TrainerId < Right.TrainerId;
				}
				return Left.PartySlotId < Right.PartySlotId;
			});

		Input.StartingActive.Sort(
			[](const FBattleActiveAssignment& Left, const FBattleActiveAssignment& Right)
			{
				if (Left.ActiveSlotId.GetSide() != Right.ActiveSlotId.GetSide())
				{
					return static_cast<uint8>(Left.ActiveSlotId.GetSide())
						< static_cast<uint8>(Right.ActiveSlotId.GetSide());
				}
				return static_cast<uint8>(Left.ActiveSlotId.GetPosition())
					< static_cast<uint8>(Right.ActiveSlotId.GetPosition());
			});

		Input.KnowledgeFacts.Sort(
			[](const FBattleKnowledgeFact& Left, const FBattleKnowledgeFact& Right)
			{
				if (Left.ObserverTrainerId != Right.ObserverTrainerId)
				{
					return Left.ObserverTrainerId < Right.ObserverTrainerId;
				}
				if (Left.Kind != Right.Kind)
				{
					return static_cast<uint8>(Left.Kind) < static_cast<uint8>(Right.Kind);
				}
				if (Left.SubjectBattlerId != Right.SubjectBattlerId)
				{
					return Left.SubjectBattlerId < Right.SubjectBattlerId;
				}
				return Left.DefinitionId.LexicalLess(Right.DefinitionId);
			});

		Input.ObedienceInputs.Sort(
			[](const FBattleObedienceInput& Left, const FBattleObedienceInput& Right)
			{
				return Left.BattlerId < Right.BattlerId;
			});
	}
}

bool FBattleSetup::TryCreate(
	const FBattleSetupInput& Input,
	FBattleSetup& OutSetup,
	EBattleSetupValidationError& OutError)
{
	OutSetup = FBattleSetup();
	OutError = EBattleSetupValidationError::None;

	auto Fail = [&OutError](const EBattleSetupValidationError Error)
	{
		OutError = Error;
		return false;
	};

	if (!Input.BattleId.IsValid())
	{
		return Fail(EBattleSetupValidationError::InvalidIdentity);
	}
	if (!Input.SettingsReference.IsValid() || !Input.CatalogReference.IsValid())
	{
		return Fail(EBattleSetupValidationError::InvalidReference);
	}
	if (!IsKnownBattleSetupEncounterKind(Input.EncounterKind) || !IsKnownBattleSetupFormat(Input.Format))
	{
		return Fail(EBattleSetupValidationError::InvalidEnum);
	}
	if (Input.CaptureCapacity.PartySlotsRemaining < 0
		|| Input.CaptureCapacity.StorageSlotsRemaining < 0)
	{
		return Fail(EBattleSetupValidationError::InvalidResource);
	}
	if (!Input.CaptureProgression.IsValid()
		|| (Input.Policies.bCaptureAllowed
			&& !Input.CaptureProgression.bHasSnapshot))
	{
		return Fail(EBattleSetupValidationError::InvalidCaptureProgression);
	}
	if (!IsKnownWildFleeMode(Input.Policies.WildFleeMode))
	{
		return Fail(EBattleSetupValidationError::InvalidEncounterPolicy);
	}
	if (Input.Policies.WildFleeMode == EBattleWildFleeMode::Chance)
	{
		if (Input.Policies.WildFleeNumerator == 0
			|| Input.Policies.WildFleeNumerator >= Input.Policies.WildFleeDenominator)
		{
			return Fail(EBattleSetupValidationError::InvalidEncounterPolicy);
		}
	}
	else if (Input.Policies.WildFleeNumerator != 0 || Input.Policies.WildFleeDenominator != 0)
	{
		return Fail(EBattleSetupValidationError::InvalidEncounterPolicy);
	}

	const int32 ExpectedTrainerCount = Input.Format == EBattleFormat::PartnerDouble ? 3 : 2;
	if (Input.Trainers.Num() != ExpectedTrainerCount)
	{
		return Fail(EBattleSetupValidationError::TrainerShape);
	}
	if (HasDuplicateBattleSetupPair(
		Input.Trainers,
		[](const FBattleTrainerSetup& Left, const FBattleTrainerSetup& Right)
		{
			return Left.TrainerId == Right.TrainerId;
		}))
	{
		return Fail(EBattleSetupValidationError::DuplicateIdentity);
	}

	int32 PlayerRoleCount = 0;
	int32 PartnerRoleCount = 0;
	int32 OpponentRoleCount = 0;
	for (const FBattleTrainerSetup& Trainer : Input.Trainers)
	{
		if (!Trainer.TrainerId.IsValid()
			|| !IsKnownBattleSetupSide(Trainer.Side)
			|| !IsKnownBattleSetupRole(Trainer.Role)
			|| !IsKnownBattleSetupController(Trainer.Controller)
			|| !Trainer.SelectorProfileId.IsValid())
		{
			return Fail(EBattleSetupValidationError::InvalidIdentity);
		}
		if ((Trainer.Role == EBattleTrainerRole::Player || Trainer.Role == EBattleTrainerRole::Partner)
			&& Trainer.Side != EBattleSide::Player)
		{
			return Fail(EBattleSetupValidationError::TrainerOwnership);
		}
		if (Trainer.Role == EBattleTrainerRole::Opponent && Trainer.Side != EBattleSide::Opponent)
		{
			return Fail(EBattleSetupValidationError::TrainerOwnership);
		}

		PlayerRoleCount += Trainer.Role == EBattleTrainerRole::Player ? 1 : 0;
		PartnerRoleCount += Trainer.Role == EBattleTrainerRole::Partner ? 1 : 0;
		OpponentRoleCount += Trainer.Role == EBattleTrainerRole::Opponent ? 1 : 0;

		if (HasDuplicateBattleSetupPair(
			Trainer.Bag,
			[](const FBattleBagItemCount& Left, const FBattleBagItemCount& Right)
			{
				return Left.ItemId == Right.ItemId;
			}))
		{
			return Fail(EBattleSetupValidationError::DuplicateIdentity);
		}
		for (const FBattleBagItemCount& Item : Trainer.Bag)
		{
			if (!Item.ItemId.IsValid() || Item.Count <= 0)
			{
				return Fail(EBattleSetupValidationError::InvalidResource);
			}
		}
	}
	if (PlayerRoleCount != 1 || OpponentRoleCount != 1
		|| PartnerRoleCount != (Input.Format == EBattleFormat::PartnerDouble ? 1 : 0))
	{
		return Fail(EBattleSetupValidationError::TrainerShape);
	}

	if (Input.PartyEntries.IsEmpty())
	{
		return Fail(EBattleSetupValidationError::PartyShape);
	}
	if (HasDuplicateBattleSetupPair(
		Input.PartyEntries,
		[](const FBattlePartyEntrySetup& Left, const FBattlePartyEntrySetup& Right)
		{
			return Left.BattlerId == Right.BattlerId
				|| Left.SourcePokemonId == Right.SourcePokemonId
				|| (Left.TrainerId == Right.TrainerId && Left.PartySlotId == Right.PartySlotId);
		}))
	{
		return Fail(EBattleSetupValidationError::DuplicateIdentity);
	}

	for (const FBattleTrainerSetup& Trainer : Input.Trainers)
	{
		int32 PartyCount = 0;
		for (const FBattlePartyEntrySetup& Entry : Input.PartyEntries)
		{
			PartyCount += Entry.TrainerId == Trainer.TrainerId ? 1 : 0;
		}
		if (PartyCount < 1 || PartyCount > FPartySlotId::PartySize)
		{
			return Fail(EBattleSetupValidationError::PartyShape);
		}
	}

	for (const FBattlePartyEntrySetup& Entry : Input.PartyEntries)
	{
		if (!Entry.TrainerId.IsValid()
			|| FindTrainerIn(Input.Trainers, Entry.TrainerId) == nullptr
			|| !Entry.BattlerId.IsValid()
			|| !Entry.SourcePokemonId.IsValid()
			|| !Entry.PartySlotId.IsValid()
			|| !Entry.SpeciesFormId.IsValid()
			|| !Entry.AbilityId.IsValid()
			|| Entry.Level < 1 || Entry.Level > 100
			|| !HasPositiveBattleSetupStats(Entry.Stats)
			|| Entry.CurrentHP < 0 || Entry.CurrentHP > Entry.Stats.MaxHP
			|| Entry.Moves.Num() > 4
			|| !IsKnownCaptureClassification(Entry.CaptureClassification))
		{
			return Fail(EBattleSetupValidationError::InvalidPartyEntry);
		}
		if (Entry.CurrentHeldItemId.IsValid() && !Entry.OriginalHeldItemId.IsValid())
		{
			return Fail(EBattleSetupValidationError::InvalidPartyEntry);
		}
		if (HasDuplicateBattleSetupPair(
			Entry.Moves,
			[](const FBattleMoveSlotSetup& Left, const FBattleMoveSlotSetup& Right)
			{
				return Left.SlotIndex == Right.SlotIndex || Left.MoveId == Right.MoveId;
			}))
		{
			return Fail(EBattleSetupValidationError::DuplicateIdentity);
		}
		for (const FBattleMoveSlotSetup& Move : Entry.Moves)
		{
			if (Move.SlotIndex >= 4
				|| !Move.MoveId.IsValid()
				|| Move.MaxPP <= 0
				|| Move.CurrentPP < 0
				|| Move.CurrentPP > Move.MaxPP)
			{
				return Fail(EBattleSetupValidationError::InvalidPartyEntry);
			}
		}
	}

	const int32 ExpectedActiveCount = Input.Format == EBattleFormat::Single ? 2 : 4;
	if (Input.StartingActive.Num() != ExpectedActiveCount
		|| !HasExpectedSlot(Input.StartingActive, EBattleSide::Player, EBattlePosition::Left)
		|| !HasExpectedSlot(Input.StartingActive, EBattleSide::Opponent, EBattlePosition::Left)
		|| (Input.Format != EBattleFormat::Single
			&& (!HasExpectedSlot(Input.StartingActive, EBattleSide::Player, EBattlePosition::Right)
				|| !HasExpectedSlot(Input.StartingActive, EBattleSide::Opponent, EBattlePosition::Right)))
		|| (Input.Format == EBattleFormat::Single
			&& (HasExpectedSlot(Input.StartingActive, EBattleSide::Player, EBattlePosition::Right)
				|| HasExpectedSlot(Input.StartingActive, EBattleSide::Opponent, EBattlePosition::Right))))
	{
		return Fail(EBattleSetupValidationError::ActiveSlotShape);
	}
	if (HasDuplicateBattleSetupPair(
		Input.StartingActive,
		[](const FBattleActiveAssignment& Left, const FBattleActiveAssignment& Right)
		{
			return Left.ActiveSlotId == Right.ActiveSlotId || Left.BattlerId == Right.BattlerId;
		}))
	{
		return Fail(EBattleSetupValidationError::DuplicateIdentity);
	}

	for (const FBattleActiveAssignment& Assignment : Input.StartingActive)
	{
		const FBattleTrainerSetup* Trainer = FindTrainerIn(Input.Trainers, Assignment.TrainerId);
		const FBattlePartyEntrySetup* Battler = FindBattlerIn(Input.PartyEntries, Assignment.BattlerId);
		if (!Assignment.ActiveSlotId.IsValid()
			|| Trainer == nullptr
			|| Battler == nullptr
			|| Battler->TrainerId != Assignment.TrainerId
			|| Trainer->Side != Assignment.ActiveSlotId.GetSide()
			|| Battler->bEgg
			|| Battler->CurrentHP <= 0)
		{
			return Fail(EBattleSetupValidationError::TrainerOwnership);
		}
	}

	if (Input.ConfiguredReinforcementBattlerId.IsValid())
	{
		const FBattlePartyEntrySetup* Reinforcement = FindBattlerIn(
			Input.PartyEntries,
			Input.ConfiguredReinforcementBattlerId);
		const FBattleTrainerSetup* ReinforcementTrainer = Reinforcement != nullptr
			? FindTrainerIn(Input.Trainers, Reinforcement->TrainerId)
			: nullptr;
		const bool bAlreadyActive = Input.StartingActive.ContainsByPredicate(
			[&Input](const FBattleActiveAssignment& Assignment)
			{
				return Assignment.BattlerId == Input.ConfiguredReinforcementBattlerId;
			});
		if (Input.EncounterKind != EBattleEncounterKind::Wild
			|| Input.Format == EBattleFormat::Single
			|| Reinforcement == nullptr
			|| ReinforcementTrainer == nullptr
			|| ReinforcementTrainer->Role != EBattleTrainerRole::Opponent
			|| Reinforcement->bEgg
			|| Reinforcement->CurrentHP <= 0
			|| bAlreadyActive)
		{
			return Fail(EBattleSetupValidationError::InvalidReinforcement);
		}
	}

	const FBattleActiveAssignment* PlayerLeft = FindExpectedSlot(
		Input.StartingActive,
		EBattleSide::Player,
		EBattlePosition::Left);
	const FBattleActiveAssignment* PlayerRight = FindExpectedSlot(
		Input.StartingActive,
		EBattleSide::Player,
		EBattlePosition::Right);
	if (PlayerLeft == nullptr
		|| FindTrainerIn(Input.Trainers, PlayerLeft->TrainerId)->Role != EBattleTrainerRole::Player)
	{
		return Fail(EBattleSetupValidationError::TrainerOwnership);
	}
	if (Input.Format == EBattleFormat::PartnerDouble)
	{
		if (PlayerRight == nullptr
			|| FindTrainerIn(Input.Trainers, PlayerRight->TrainerId)->Role != EBattleTrainerRole::Partner)
		{
			return Fail(EBattleSetupValidationError::TrainerOwnership);
		}
	}
	else if (PlayerRight != nullptr
		&& FindTrainerIn(Input.Trainers, PlayerRight->TrainerId)->Role != EBattleTrainerRole::Player)
	{
		return Fail(EBattleSetupValidationError::TrainerOwnership);
	}
	for (const FBattleActiveAssignment& Assignment : Input.StartingActive)
	{
		if (Assignment.ActiveSlotId.GetSide() == EBattleSide::Opponent
			&& FindTrainerIn(Input.Trainers, Assignment.TrainerId)->Role != EBattleTrainerRole::Opponent)
		{
			return Fail(EBattleSetupValidationError::TrainerOwnership);
		}
	}

	if (HasDuplicateBattleSetupPair(
		Input.KnowledgeFacts,
		[](const FBattleKnowledgeFact& Left, const FBattleKnowledgeFact& Right)
		{
			return Left.ObserverTrainerId == Right.ObserverTrainerId
				&& Left.SubjectBattlerId == Right.SubjectBattlerId
				&& Left.Kind == Right.Kind
				&& Left.DefinitionId == Right.DefinitionId;
		}))
	{
		return Fail(EBattleSetupValidationError::DuplicateIdentity);
	}
	for (const FBattleKnowledgeFact& Fact : Input.KnowledgeFacts)
	{
		if (FindTrainerIn(Input.Trainers, Fact.ObserverTrainerId) == nullptr
			|| !IsKnownKnowledgeKind(Fact.Kind)
			|| !Fact.DefinitionId.IsValid()
			|| !IsKnownBattleSetupVisibility(Fact.Visibility)
			|| (Fact.Kind != EBattleKnowledgeKind::SpeciesFormKnown
				&& FindBattlerIn(Input.PartyEntries, Fact.SubjectBattlerId) == nullptr))
		{
			return Fail(EBattleSetupValidationError::InvalidKnowledge);
		}
	}

	if (HasDuplicateBattleSetupPair(
		Input.ObedienceInputs,
		[](const FBattleObedienceInput& Left, const FBattleObedienceInput& Right)
		{
			return Left.BattlerId == Right.BattlerId;
		}))
	{
		return Fail(EBattleSetupValidationError::DuplicateIdentity);
	}
	for (const FBattleObedienceInput& Obedience : Input.ObedienceInputs)
	{
		if (FindBattlerIn(Input.PartyEntries, Obedience.BattlerId) == nullptr
			|| Obedience.ReferenceLevel < 1
			|| Obedience.ReferenceLevel > 100
			|| Obedience.BadgeCount > 8)
		{
			return Fail(EBattleSetupValidationError::InvalidObedience);
		}
	}

	FBattleSetupInput Canonical = Input;
	Canonicalize(Canonical);
	OutSetup.bValid = true;
	OutSetup.BattleId = Canonical.BattleId;
	OutSetup.SettingsReference = Canonical.SettingsReference;
	OutSetup.CatalogReference = Canonical.CatalogReference;
	OutSetup.EncounterKind = Canonical.EncounterKind;
	OutSetup.Format = Canonical.Format;
	OutSetup.Trainers = MoveTemp(Canonical.Trainers);
	OutSetup.PartyEntries = MoveTemp(Canonical.PartyEntries);
	OutSetup.StartingActive = MoveTemp(Canonical.StartingActive);
	OutSetup.CaptureCapacity = Canonical.CaptureCapacity;
	OutSetup.CaptureProgression = Canonical.CaptureProgression;
	OutSetup.ConfiguredReinforcementBattlerId = Canonical.ConfiguredReinforcementBattlerId;
	OutSetup.KnowledgeFacts = MoveTemp(Canonical.KnowledgeFacts);
	OutSetup.ObedienceInputs = MoveTemp(Canonical.ObedienceInputs);
	OutSetup.Policies = Canonical.Policies;

	FBattleCompiledEncounterPolicies CompiledPolicies;
	EBattleEncounterPolicyError PolicyError = EBattleEncounterPolicyError::None;
	if (!FBattleEncounterPolicyCompiler::TryCompile(OutSetup, CompiledPolicies, PolicyError))
	{
		OutSetup = FBattleSetup();
		return Fail(
			PolicyError == EBattleEncounterPolicyError::InvalidTrainerShape
				? EBattleSetupValidationError::TrainerShape
				: EBattleSetupValidationError::InvalidEncounterPolicy);
	}
	return true;
}

const FBattleTrainerSetup* FBattleSetup::FindTrainer(const FTrainerId TrainerId) const
{
	return Trainers.FindByPredicate(
		[TrainerId](const FBattleTrainerSetup& Trainer)
		{
			return Trainer.TrainerId == TrainerId;
		});
}

const FBattlePartyEntrySetup* FBattleSetup::FindBattler(const FBattlerId BattlerId) const
{
	return PartyEntries.FindByPredicate(
		[BattlerId](const FBattlePartyEntrySetup& Entry)
		{
			return Entry.BattlerId == BattlerId;
		});
}

const FBattleActiveAssignment* FBattleSetup::FindActive(const FActiveSlotId ActiveSlotId) const
{
	return StartingActive.FindByPredicate(
		[ActiveSlotId](const FBattleActiveAssignment& Assignment)
		{
			return Assignment.ActiveSlotId == ActiveSlotId;
		});
}
