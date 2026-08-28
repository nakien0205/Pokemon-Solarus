#include "Battle/BattleTargeting.h"

namespace BattleTargetingPrivate
{
	constexpr int32 StructuralPositionCount = 4;

	bool IsKnownSide(const EBattleSide Side)
	{
		return Side == EBattleSide::Player || Side == EBattleSide::Opponent;
	}

	bool IsKnownTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::Self
			|| TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler
			|| TargetClass == EBattleTargetClass::RandomLegalOpponent
			|| TargetClass == EBattleTargetClass::UserSide
			|| TargetClass == EBattleTargetClass::OpponentSide
			|| TargetClass == EBattleTargetClass::BothSides
			|| TargetClass == EBattleTargetClass::Field
			|| TargetClass == EBattleTargetClass::FixedSpreadSet
			|| TargetClass == EBattleTargetClass::SelectedOtherBattler
			|| TargetClass == EBattleTargetClass::FixedOpponentSpreadSet;
	}

	bool IsKnownPositionState(const EBattleTargetPositionState State)
	{
		return State == EBattleTargetPositionState::Empty
			|| State == EBattleTargetPositionState::Living
			|| State == EBattleTargetPositionState::Fainted
			|| State == EBattleTargetPositionState::Captured
			|| State == EBattleTargetPositionState::Removed;
	}

	int32 GetStablePositionIndex(const FActiveSlotId ActiveSlotId)
	{
		if (!ActiveSlotId.IsValid())
		{
			return INDEX_NONE;
		}

		const int32 SideOffset = ActiveSlotId.GetSide() == EBattleSide::Player ? 0 : 2;
		const int32 PositionOffset = ActiveSlotId.GetPosition() == EBattlePosition::Left ? 0 : 1;
		return SideOffset + PositionOffset;
	}

	bool IsExplicitTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler
			|| TargetClass == EBattleTargetClass::SelectedOtherBattler;
	}

	bool SupportsRuleRedirection(const EBattleTargetClass TargetClass)
	{
		return IsExplicitTargetClass(TargetClass)
			|| TargetClass == EBattleTargetClass::RandomLegalOpponent;
	}

	EBattleSide GetOtherSide(const EBattleSide Side)
	{
		return Side == EBattleSide::Player ? EBattleSide::Opponent : EBattleSide::Player;
	}

	bool IsLiving(const FBattleTargetPositionFacts& Position)
	{
		return Position.State == EBattleTargetPositionState::Living;
	}

	bool IsPermittedBattler(
		const EBattleTargetClass TargetClass,
		const FBattleTargetPositionFacts& User,
		const FBattleTargetPositionFacts& Candidate)
	{
		const bool bSelf = Candidate.ActiveSlotId == User.ActiveSlotId
			&& Candidate.BattlerId == User.BattlerId;
		const bool bSameSide = Candidate.ActiveSlotId.GetSide() == User.ActiveSlotId.GetSide();

		switch (TargetClass)
		{
		case EBattleTargetClass::Self:
			return bSelf;
		case EBattleTargetClass::SelectedAlly:
			return bSameSide && !bSelf;
		case EBattleTargetClass::SelectedOpponent:
		case EBattleTargetClass::RandomLegalOpponent:
			return !bSameSide;
		case EBattleTargetClass::AnySelectedBattler:
			return true;
		case EBattleTargetClass::SelectedOtherBattler:
			return !bSelf;
		case EBattleTargetClass::FixedSpreadSet:
			return !bSelf;
		case EBattleTargetClass::FixedOpponentSpreadSet:
			return !bSameSide;
		default:
			return false;
		}
	}

	const FBattleTargetPositionFacts* FindPosition(
		const TArray<FBattleTargetPositionFacts>& Positions,
		const FActiveSlotId ActiveSlotId)
	{
		for (const FBattleTargetPositionFacts& Position : Positions)
		{
			if (Position.ActiveSlotId == ActiveSlotId)
			{
				return &Position;
			}
		}
		return nullptr;
	}

	bool TryValidateAndCanonicalizePositions(
		const EBattleTargetClass TargetClass,
		const FActiveSlotId UserSlotId,
		const FBattlerId UserBattlerId,
		const TArray<FBattleTargetPositionFacts>& InPositions,
		TArray<FBattleTargetPositionFacts>& OutPositions,
		const FBattleTargetPositionFacts*& OutUser,
		EBattleTargetingError& OutError)
	{
		OutPositions.Reset();
		OutUser = nullptr;
		if (!IsKnownTargetClass(TargetClass)
			|| !UserSlotId.IsValid()
			|| !UserBattlerId.IsValid())
		{
			OutError = EBattleTargetingError::InvalidContext;
			return false;
		}

		if (InPositions.Num() != StructuralPositionCount)
		{
			OutError = EBattleTargetingError::InvalidPositions;
			return false;
		}

		bool bSeenPositions[StructuralPositionCount] = {false, false, false, false};
		for (int32 LeftIndex = 0; LeftIndex < InPositions.Num(); ++LeftIndex)
		{
			const FBattleTargetPositionFacts& Position = InPositions[LeftIndex];
			const int32 StableIndex = GetStablePositionIndex(Position.ActiveSlotId);
			if (StableIndex == INDEX_NONE
				|| StableIndex >= StructuralPositionCount
				|| bSeenPositions[StableIndex]
				|| !IsKnownPositionState(Position.State))
			{
				OutError = EBattleTargetingError::InvalidPositions;
				return false;
			}
			bSeenPositions[StableIndex] = true;

			const bool bEmpty = Position.State == EBattleTargetPositionState::Empty;
			if ((bEmpty && (Position.BattlerId.IsValid() || Position.bSemiInvulnerable))
				|| (!bEmpty && !Position.BattlerId.IsValid())
				|| (Position.State != EBattleTargetPositionState::Living
					&& Position.bSemiInvulnerable))
			{
				OutError = EBattleTargetingError::InvalidPositions;
				return false;
			}

			if (Position.BattlerId.IsValid())
			{
				for (int32 RightIndex = LeftIndex + 1; RightIndex < InPositions.Num(); ++RightIndex)
				{
					if (Position.BattlerId == InPositions[RightIndex].BattlerId)
					{
						OutError = EBattleTargetingError::InvalidPositions;
						return false;
					}
				}
			}
		}

		for (const bool bSeen : bSeenPositions)
		{
			if (!bSeen)
			{
				OutError = EBattleTargetingError::InvalidPositions;
				return false;
			}
		}

		OutPositions = InPositions;
		OutPositions.Sort(
			[](const FBattleTargetPositionFacts& Left, const FBattleTargetPositionFacts& Right)
			{
				return GetStablePositionIndex(Left.ActiveSlotId)
					< GetStablePositionIndex(Right.ActiveSlotId);
			});

		OutUser = FindPosition(OutPositions, UserSlotId);
		if (OutUser == nullptr
			|| OutUser->BattlerId != UserBattlerId
			|| OutUser->State != EBattleTargetPositionState::Living)
		{
			OutPositions.Reset();
			OutUser = nullptr;
			OutError = EBattleTargetingError::InvalidContext;
			return false;
		}

		return true;
	}

	void AddBattlerCandidate(
		const FBattleTargetPositionFacts& Position,
		TArray<FBattleBattlerTarget>& OutCandidates)
	{
		FBattleBattlerTarget Target;
		Target.ActiveSlotId = Position.ActiveSlotId;
		Target.BattlerId = Position.BattlerId;
		OutCandidates.Add(Target);
	}

	bool AddResolvedBattler(
		const FBattleBattlerTarget& Battler,
		TArray<FBattleResolvedTarget>& OutTargets)
	{
		FBattleResolvedTarget Target;
		if (!FBattleResolvedTarget::TryCreateBattler(Battler, Target))
		{
			return false;
		}
		OutTargets.Add(MoveTemp(Target));
		return true;
	}

	bool AddResolvedSide(
		const EBattleSide Side,
		TArray<FBattleResolvedTarget>& OutTargets)
	{
		FBattleResolvedTarget Target;
		if (!FBattleResolvedTarget::TryCreateSide(Side, Target))
		{
			return false;
		}
		OutTargets.Add(MoveTemp(Target));
		return true;
	}

	bool HasCompletelyEmptyTarget(const FBattleBattlerTarget& Target)
	{
		return !Target.ActiveSlotId.IsValid() && !Target.BattlerId.IsValid();
	}
}

bool FBattleResolvedTarget::TryCreateBattler(
	const FBattleBattlerTarget& InBattler,
	FBattleResolvedTarget& OutTarget)
{
	OutTarget = FBattleResolvedTarget();
	if (!InBattler.IsValid())
	{
		return false;
	}

	OutTarget.Kind = EBattleResolvedTargetKind::Battler;
	OutTarget.Battler = InBattler;
	return true;
}

bool FBattleResolvedTarget::TryCreateSide(
	const EBattleSide InSide,
	FBattleResolvedTarget& OutTarget)
{
	OutTarget = FBattleResolvedTarget();
	if (!BattleTargetingPrivate::IsKnownSide(InSide))
	{
		return false;
	}

	OutTarget.Kind = EBattleResolvedTargetKind::Side;
	OutTarget.Side = InSide;
	return true;
}

FBattleResolvedTarget FBattleResolvedTarget::CreateField()
{
	FBattleResolvedTarget Target;
	Target.Kind = EBattleResolvedTargetKind::Field;
	return Target;
}

bool FBattleResolvedTarget::IsValid() const
{
	switch (Kind)
	{
	case EBattleResolvedTargetKind::Battler:
		return Battler.IsValid();
	case EBattleResolvedTargetKind::Side:
		return BattleTargetingPrivate::IsKnownSide(Side);
	case EBattleResolvedTargetKind::Field:
		return true;
	default:
		return false;
	}
}

bool FBattleTargetResolver::TryBuildSelection(
	const FBattleTargetSelectionSpec& Spec,
	FBattleTargetSelectionResult& OutResult,
	EBattleTargetingError& OutError)
{
	using namespace BattleTargetingPrivate;

	OutResult = FBattleTargetSelectionResult();
	OutError = EBattleTargetingError::None;
	TArray<FBattleTargetPositionFacts> Positions;
	const FBattleTargetPositionFacts* User = nullptr;
	if (!TryValidateAndCanonicalizePositions(
		Spec.TargetClass,
		Spec.UserSlotId,
		Spec.UserBattlerId,
		Spec.Positions,
		Positions,
		User,
		OutError))
	{
		return false;
	}

	OutResult.TargetClass = Spec.TargetClass;
	OutResult.bRequiresExplicitChoice = IsExplicitTargetClass(Spec.TargetClass);

	if (Spec.TargetClass == EBattleTargetClass::UserSide
		|| Spec.TargetClass == EBattleTargetClass::OpponentSide
		|| Spec.TargetClass == EBattleTargetClass::BothSides)
	{
		if (Spec.TargetClass == EBattleTargetClass::UserSide)
		{
			AddResolvedSide(User->ActiveSlotId.GetSide(), OutResult.AutomaticTargets);
		}
		else if (Spec.TargetClass == EBattleTargetClass::OpponentSide)
		{
			AddResolvedSide(GetOtherSide(User->ActiveSlotId.GetSide()), OutResult.AutomaticTargets);
		}
		else
		{
			AddResolvedSide(EBattleSide::Player, OutResult.AutomaticTargets);
			AddResolvedSide(EBattleSide::Opponent, OutResult.AutomaticTargets);
		}
		OutResult.bHasLegalTarget = true;
		return true;
	}

	if (Spec.TargetClass == EBattleTargetClass::Field)
	{
		OutResult.AutomaticTargets.Add(FBattleResolvedTarget::CreateField());
		OutResult.bHasLegalTarget = true;
		return true;
	}

	for (const FBattleTargetPositionFacts& Position : Positions)
	{
		if (!IsLiving(Position) || !IsPermittedBattler(Spec.TargetClass, *User, Position))
		{
			continue;
		}

		AddBattlerCandidate(Position, OutResult.BattlerCandidates);
		if (Spec.TargetClass == EBattleTargetClass::Self
			|| Spec.TargetClass == EBattleTargetClass::FixedSpreadSet
			|| Spec.TargetClass == EBattleTargetClass::FixedOpponentSpreadSet)
		{
			const bool bAdded = AddResolvedBattler(
				OutResult.BattlerCandidates.Last(),
				OutResult.AutomaticTargets);
			check(bAdded);
		}
	}

	OutResult.bHasLegalTarget = !OutResult.BattlerCandidates.IsEmpty();
	return true;
}

bool FBattleTargetResolver::TryResolve(
	const FBattleTargetResolutionSpec& Spec,
	IBattleRandom& Random,
	FBattleTargetResolutionResult& OutResult,
	EBattleTargetingError& OutError)
{
	using namespace BattleTargetingPrivate;

	OutResult = FBattleTargetResolutionResult();
	OutError = EBattleTargetingError::None;
	TArray<FBattleTargetPositionFacts> Positions;
	const FBattleTargetPositionFacts* User = nullptr;
	if (!TryValidateAndCanonicalizePositions(
		Spec.TargetClass,
		Spec.UserSlotId,
		Spec.UserBattlerId,
		Spec.Positions,
		Positions,
		User,
		OutError))
	{
		return false;
	}

	const bool bExplicit = IsExplicitTargetClass(Spec.TargetClass);
	const FBattleTargetPositionFacts* ExplicitPosition = nullptr;
	if (bExplicit)
	{
		if (!Spec.ExplicitTarget.IsValid())
		{
			OutError = EBattleTargetingError::InvalidSelection;
			return false;
		}

		ExplicitPosition = FindPosition(Positions, Spec.ExplicitTarget.ActiveSlotId);
		if (ExplicitPosition == nullptr
			|| ExplicitPosition->BattlerId != Spec.ExplicitTarget.BattlerId
			|| !IsPermittedBattler(Spec.TargetClass, *User, *ExplicitPosition))
		{
			OutError = EBattleTargetingError::InvalidSelection;
			return false;
		}
	}
	else if (!HasCompletelyEmptyTarget(Spec.ExplicitTarget))
	{
		OutError = EBattleTargetingError::InvalidSelection;
		return false;
	}

	OutResult.TargetClass = Spec.TargetClass;
	if (bExplicit && ExplicitPosition->State == EBattleTargetPositionState::Captured)
	{
		OutResult.Outcome = EBattleTargetResolutionOutcome::CapturedTargetCanceled;
		return true;
	}

	for (const FBattleTargetRedirectionProposal& Proposal : Spec.RedirectionProposals)
	{
		if (!Proposal.ProposedTarget.IsValid())
		{
			OutError = EBattleTargetingError::InvalidRedirectionProposal;
			return false;
		}
	}

	if (Spec.TargetClass == EBattleTargetClass::RandomLegalOpponent
		&& (!Spec.RandomContext.IsValid()
			|| !Spec.RandomContext.ActionId.IsValid()
			|| Spec.RandomContext.RulePurpose != GetRandomLegalOpponentRulePurpose()))
	{
		OutError = EBattleTargetingError::InvalidContext;
		return false;
	}

	if (bExplicit)
	{
		if (ExplicitPosition->State == EBattleTargetPositionState::Living)
		{
			const bool bAdded = AddResolvedBattler(Spec.ExplicitTarget, OutResult.Targets);
			check(bAdded);
		}
		else if (ExplicitPosition->State == EBattleTargetPositionState::Fainted
			&& ExplicitPosition->ActiveSlotId.GetSide() != User->ActiveSlotId.GetSide()
			&& (Spec.TargetClass == EBattleTargetClass::SelectedOpponent
				|| Spec.TargetClass == EBattleTargetClass::AnySelectedBattler
				|| Spec.TargetClass == EBattleTargetClass::SelectedOtherBattler))
		{
			for (const FBattleTargetPositionFacts& Candidate : Positions)
			{
				if (Candidate.ActiveSlotId == ExplicitPosition->ActiveSlotId
					|| !IsLiving(Candidate)
					|| Candidate.ActiveSlotId.GetSide() == User->ActiveSlotId.GetSide())
				{
					continue;
				}

				FBattleBattlerTarget Fallback;
				Fallback.ActiveSlotId = Candidate.ActiveSlotId;
				Fallback.BattlerId = Candidate.BattlerId;
				const bool bAdded = AddResolvedBattler(Fallback, OutResult.Targets);
				check(bAdded);
				OutResult.bWasRedirected = true;
				OutResult.bUsedFaintedTargetFallback = true;
				break;
			}
		}
	}
	else
	{
		switch (Spec.TargetClass)
		{
		case EBattleTargetClass::Self:
		{
			FBattleBattlerTarget Self;
			Self.ActiveSlotId = User->ActiveSlotId;
			Self.BattlerId = User->BattlerId;
			const bool bAdded = AddResolvedBattler(Self, OutResult.Targets);
			check(bAdded);
			break;
		}
		case EBattleTargetClass::RandomLegalOpponent:
		{
			TArray<FBattleBattlerTarget> Candidates;
			for (const FBattleTargetPositionFacts& Position : Positions)
			{
				if (IsLiving(Position) && IsPermittedBattler(Spec.TargetClass, *User, Position))
				{
					AddBattlerCandidate(Position, Candidates);
				}
			}

			if (!Candidates.IsEmpty())
			{
				FBattleRandomDraw Draw;
				if (!Random.TryDrawUniform(
					0,
					static_cast<uint32>(Candidates.Num() - 1),
					Spec.RandomContext,
					Draw))
				{
					OutResult = FBattleTargetResolutionResult();
					OutError = EBattleTargetingError::RandomFailure;
					return false;
				}

				const bool bAdded = AddResolvedBattler(
					Candidates[static_cast<int32>(Draw.Result)],
					OutResult.Targets);
				check(bAdded);
			}
			break;
		}
		case EBattleTargetClass::UserSide:
			AddResolvedSide(User->ActiveSlotId.GetSide(), OutResult.Targets);
			break;
		case EBattleTargetClass::OpponentSide:
			AddResolvedSide(GetOtherSide(User->ActiveSlotId.GetSide()), OutResult.Targets);
			break;
		case EBattleTargetClass::BothSides:
			AddResolvedSide(EBattleSide::Player, OutResult.Targets);
			AddResolvedSide(EBattleSide::Opponent, OutResult.Targets);
			break;
		case EBattleTargetClass::Field:
			OutResult.Targets.Add(FBattleResolvedTarget::CreateField());
			break;
		case EBattleTargetClass::FixedSpreadSet:
		case EBattleTargetClass::FixedOpponentSpreadSet:
			for (const FBattleTargetPositionFacts& Position : Positions)
			{
				if (!IsLiving(Position) || !IsPermittedBattler(Spec.TargetClass, *User, Position))
				{
					continue;
				}

				FBattleBattlerTarget Target;
				Target.ActiveSlotId = Position.ActiveSlotId;
				Target.BattlerId = Position.BattlerId;
				const bool bAdded = AddResolvedBattler(Target, OutResult.Targets);
				check(bAdded);
			}
			break;
		default:
			OutError = EBattleTargetingError::InvalidContext;
			return false;
		}
	}

	if (SupportsRuleRedirection(Spec.TargetClass)
		&& OutResult.Targets.Num() == 1
		&& OutResult.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler)
	{
		for (const FBattleTargetRedirectionProposal& Proposal : Spec.RedirectionProposals)
		{
			const FBattleTargetPositionFacts* ProposedPosition = FindPosition(
				Positions,
				Proposal.ProposedTarget.ActiveSlotId);
			if (ProposedPosition == nullptr
				|| ProposedPosition->BattlerId != Proposal.ProposedTarget.BattlerId
				|| !IsLiving(*ProposedPosition)
				|| !IsPermittedBattler(Spec.TargetClass, *User, *ProposedPosition)
				|| Proposal.ProposedTarget == OutResult.Targets[0].GetBattler())
			{
				continue;
			}

			FBattleResolvedTarget Redirected;
			const bool bCreated = FBattleResolvedTarget::TryCreateBattler(
				Proposal.ProposedTarget,
				Redirected);
			check(bCreated);
			OutResult.Targets[0] = MoveTemp(Redirected);
			OutResult.bWasRedirected = true;
			break;
		}
	}

	OutResult.Outcome = OutResult.Targets.IsEmpty()
		? EBattleTargetResolutionOutcome::NoLegalTarget
		: EBattleTargetResolutionOutcome::Resolved;
	return true;
}

FDefinitionId FBattleTargetResolver::GetRandomLegalOpponentRulePurpose()
{
	static const FDefinitionId RulePurpose = []
	{
		FDefinitionId Value;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Rule.Targeting.RandomLegalOpponent")),
			Value);
		check(bCreated);
		return Value;
	}();
	return RulePurpose;
}
