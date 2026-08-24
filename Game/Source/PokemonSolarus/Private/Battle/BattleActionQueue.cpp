#include "Battle/BattleActionQueue.h"

namespace
{
	bool IsKnownBand(const EBattleActionCommandBand Value)
	{
		return Value == EBattleActionCommandBand::Move
			|| Value == EBattleActionCommandBand::Bag
			|| Value == EBattleActionCommandBand::VoluntarySwitch
			|| Value == EBattleActionCommandBand::Run;
	}

	bool TryGetExpectedBand(
		const EBattleActionKind ActionKind,
		EBattleActionCommandBand& OutBand)
	{
		OutBand = EBattleActionCommandBand::Move;
		switch (ActionKind)
		{
		case EBattleActionKind::Fight:
			OutBand = EBattleActionCommandBand::Move;
			return true;
		case EBattleActionKind::Bag:
			OutBand = EBattleActionCommandBand::Bag;
			return true;
		case EBattleActionKind::Switch:
			OutBand = EBattleActionCommandBand::VoluntarySwitch;
			return true;
		case EBattleActionKind::Run:
		case EBattleActionKind::WildFlee:
			OutBand = EBattleActionCommandBand::Run;
			return true;
		default:
			return false;
		}
	}

	int32 GetResolvedPriorityTenths(const FBattleActionOrderKey& Key)
	{
		return Key.MovePriority * 10 + Key.FractionalPriorityTenths;
	}

	bool IsKnownActionQueueTargetClass(const EBattleTargetClass Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleTargetClass::FixedSpreadSet);
	}

	bool RequiresSelectedActiveTarget(const EBattleTargetClass Value)
	{
		return Value == EBattleTargetClass::SelectedAlly
			|| Value == EBattleTargetClass::SelectedOpponent
			|| Value == EBattleTargetClass::AnySelectedBattler;
	}

	bool IsCandidateValid(const FBattleActionOrderCandidate& Candidate)
	{
		if (!Candidate.ActionId.IsValid()
			|| !Candidate.Decision.IsValid()
			|| !Candidate.OrderKey.ActingSlotId.IsValid()
			|| Candidate.OrderKey.EffectiveSpeed < 0
			|| !IsKnownBand(Candidate.OrderKey.CommandBand))
		{
			return false;
		}

		EBattleActionCommandBand ExpectedBand = EBattleActionCommandBand::Move;
		if (!TryGetExpectedBand(Candidate.Decision.GetActionKind(), ExpectedBand)
			|| ExpectedBand != Candidate.OrderKey.CommandBand)
		{
			return false;
		}

		if (ExpectedBand == EBattleActionCommandBand::Move)
		{
			const bool bRequiresSelectedTarget = RequiresSelectedActiveTarget(Candidate.TargetClass);
			return IsKnownActionQueueTargetClass(Candidate.TargetClass)
				&& Candidate.OrderKey.MovePriority >= -7
				&& Candidate.OrderKey.MovePriority <= 5
				&& Candidate.OrderKey.FractionalPriorityTenths >= 0
				&& Candidate.OrderKey.FractionalPriorityTenths <= 9
				&& Candidate.Decision.GetActiveTargetId().IsValid() == bRequiresSelectedTarget
				&& Candidate.SelectedTargetBattlerId.IsValid() == bRequiresSelectedTarget;
		}

		return Candidate.OrderKey.MovePriority == 0
			&& Candidate.OrderKey.FractionalPriorityTenths == 0;
	}

	bool HasSameExactOrderKeys(
		const FBattleActionOrderCandidate& Left,
		const FBattleActionOrderCandidate& Right)
	{
		return Left.OrderKey.CommandBand == Right.OrderKey.CommandBand
			&& GetResolvedPriorityTenths(Left.OrderKey) == GetResolvedPriorityTenths(Right.OrderKey)
			&& Left.OrderKey.EffectiveSpeed == Right.OrderKey.EffectiveSpeed
			&& Left.OrderKey.ActingSlotId.GetSide() == Right.OrderKey.ActingSlotId.GetSide();
	}

	bool CandidateLess(
		const FBattleActionOrderCandidate& Left,
		const FBattleActionOrderCandidate& Right,
		const bool bReverseSpeed)
	{
		if (Left.OrderKey.CommandBand != Right.OrderKey.CommandBand)
		{
			return static_cast<uint8>(Left.OrderKey.CommandBand)
				> static_cast<uint8>(Right.OrderKey.CommandBand);
		}

		const int32 LeftPriority = GetResolvedPriorityTenths(Left.OrderKey);
		const int32 RightPriority = GetResolvedPriorityTenths(Right.OrderKey);
		if (LeftPriority != RightPriority)
		{
			return LeftPriority > RightPriority;
		}

		if (Left.OrderKey.EffectiveSpeed != Right.OrderKey.EffectiveSpeed)
		{
			return bReverseSpeed
				? Left.OrderKey.EffectiveSpeed < Right.OrderKey.EffectiveSpeed
				: Left.OrderKey.EffectiveSpeed > Right.OrderKey.EffectiveSpeed;
		}

		const EBattleSide LeftSide = Left.OrderKey.ActingSlotId.GetSide();
		const EBattleSide RightSide = Right.OrderKey.ActingSlotId.GetSide();
		if (LeftSide != RightSide)
		{
			return LeftSide == EBattleSide::Player;
		}

		return static_cast<uint8>(Left.OrderKey.ActingSlotId.GetPosition())
			< static_cast<uint8>(Right.OrderKey.ActingSlotId.GetPosition());
	}

	bool IsKnownActionKind(const EBattleActionKind Value)
	{
		return Value == EBattleActionKind::Fight
			|| Value == EBattleActionKind::Bag
			|| Value == EBattleActionKind::Switch
			|| Value == EBattleActionKind::Run
			|| Value == EBattleActionKind::WildFlee
			|| Value == EBattleActionKind::Replacement
			|| Value == EBattleActionKind::ScriptedEnd
			|| Value == EBattleActionKind::Abandon;
	}

	FDefinitionId GetSameSideTieRulePurpose()
	{
		FDefinitionId RulePurpose;
		const bool bCreated = FDefinitionId::TryCreate(
			FName(TEXT("Rule.ActionOrder.SameSideTie")),
			RulePurpose);
		check(bCreated);
		return RulePurpose;
	}
}

bool FBattleActionQueueResolver::TryLock(
	const FBattleActionQueueLockSpec& Spec,
	IBattleRandom& Random,
	TArray<FBattleLockedAction>& OutQueue,
	EBattleActionQueueError& OutError)
{
	OutQueue.Reset();
	OutError = EBattleActionQueueError::None;
	if (!Spec.BattleId.IsValid()
		|| !Spec.TurnId.IsValid()
		|| !Spec.ResolutionId.IsValid()
		|| Spec.Candidates.IsEmpty()
		|| Spec.Candidates.Num() > 4)
	{
		OutError = EBattleActionQueueError::InvalidContext;
		return false;
	}

	for (int32 LeftIndex = 0; LeftIndex < Spec.Candidates.Num(); ++LeftIndex)
	{
		const FBattleActionOrderCandidate& Left = Spec.Candidates[LeftIndex];
		if (!IsCandidateValid(Left))
		{
			OutError = EBattleActionQueueError::InvalidCandidate;
			return false;
		}

		for (int32 RightIndex = LeftIndex + 1; RightIndex < Spec.Candidates.Num(); ++RightIndex)
		{
			const FBattleActionOrderCandidate& Right = Spec.Candidates[RightIndex];
			if (Left.ActionId == Right.ActionId)
			{
				OutError = EBattleActionQueueError::DuplicateAction;
				return false;
			}
			if (Left.Decision.GetActingBattlerId() == Right.Decision.GetActingBattlerId())
			{
				OutError = EBattleActionQueueError::DuplicateActor;
				return false;
			}
			if (Left.OrderKey.ActingSlotId == Right.OrderKey.ActingSlotId)
			{
				OutError = EBattleActionQueueError::DuplicateActiveSlot;
				return false;
			}
		}
	}

	TArray<FBattleActionOrderCandidate> Ordered = Spec.Candidates;
	Ordered.Sort(
		[&Spec](const FBattleActionOrderCandidate& Left, const FBattleActionOrderCandidate& Right)
		{
			return CandidateLess(Left, Right, Spec.bReverseSpeed);
		});

	for (int32 GroupStart = 0; GroupStart < Ordered.Num();)
	{
		int32 GroupEnd = GroupStart + 1;
		while (GroupEnd < Ordered.Num()
			&& HasSameExactOrderKeys(Ordered[GroupStart], Ordered[GroupEnd]))
		{
			++GroupEnd;
		}

		const int32 GroupSize = GroupEnd - GroupStart;
		if (GroupSize == 2)
		{
			FBattleRandomContext Context;
			Context.BattleId = Spec.BattleId;
			Context.TurnId = Spec.TurnId;
			Context.ResolutionId = Spec.ResolutionId;
			Context.RulePurpose = GetSameSideTieRulePurpose();

			FBattleRandomDraw Draw;
			if (!Random.TryDrawUniform(0, 1, Context, Draw))
			{
				OutError = EBattleActionQueueError::RandomFailure;
				return false;
			}
			if (Draw.Result == 1)
			{
				Swap(Ordered[GroupStart], Ordered[GroupStart + 1]);
			}
		}
		else if (GroupSize > 2)
		{
			OutError = EBattleActionQueueError::InvalidCandidate;
			return false;
		}
		GroupStart = GroupEnd;
	}

	OutQueue.Reserve(Ordered.Num());
	for (int32 Index = 0; Index < Ordered.Num(); ++Index)
	{
		FBattleLockedAction Action;
		Action.ActionId = Ordered[Index].ActionId;
		Action.QueueOrdinal = static_cast<uint64>(Index + 1);
		Action.Decision = Ordered[Index].Decision;
		Action.OrderKey = Ordered[Index].OrderKey;
		Action.TargetClass = Ordered[Index].TargetClass;
		Action.SelectedTargetBattlerId = Ordered[Index].SelectedTargetBattlerId;
		OutQueue.Add(MoveTemp(Action));
	}
	return true;
}

bool FBattleActionStartRules::TryEvaluate(
	const FBattleActionStartFacts& Facts,
	FBattleActionStartResult& OutResult)
{
	OutResult = FBattleActionStartResult();
	if (!IsKnownActionKind(Facts.ActionKind)
		|| (Facts.bSelectedTargetCaptured && Facts.ActionKind != EBattleActionKind::Fight)
		|| (Facts.bSubjectToPlayerObedience && Facts.ActionKind != EBattleActionKind::Fight)
		|| (Facts.bSubjectToPlayerObedience
			&& (Facts.ObedienceReferenceLevel < 1
				|| Facts.ObedienceReferenceLevel > 100
				|| Facts.BadgeCount > 8)))
	{
		return false;
	}

	if (!Facts.bActorActive || !Facts.bActorLiving)
	{
		OutResult.Outcome = EBattleActionStartOutcome::ActorUnavailable;
		OutResult.bEndsCommittedAction = true;
		return true;
	}

	if (Facts.bSelectedTargetCaptured)
	{
		OutResult.Outcome = EBattleActionStartOutcome::CapturedTargetCanceled;
		OutResult.bEndsCommittedAction = true;
		return true;
	}

	if (Facts.bSubjectToPlayerObedience)
	{
		const uint8 Cap = Facts.BadgeCount == 8
			? static_cast<uint8>(100)
			: static_cast<uint8>(20 + 5 * Facts.BadgeCount);
		OutResult.ObedienceCap = Cap;
		if (Facts.ObedienceReferenceLevel > Cap)
		{
			OutResult.Outcome = EBattleActionStartOutcome::ObedienceRefused;
			OutResult.bEndsCommittedAction = true;
			return true;
		}
	}

	OutResult.Outcome = EBattleActionStartOutcome::Proceed;
	return true;
}

FMoveId FBattleBuiltInMoveDefinitions::GetStruggleMoveId()
{
	return GetStruggle().Id;
}

const FBattleMoveDefinition& FBattleBuiltInMoveDefinitions::GetStruggle()
{
	static const FBattleMoveDefinition Definition = []
	{
		FBattleMoveDefinition Value;
		const bool bIdCreated = FMoveId::TryCreate(FName(TEXT("Move.BuiltIn.Struggle")), Value.Id);
		check(bIdCreated);
		Value.Type = EPokemonType::Invalid;
		Value.Category = EBattleMoveCategory::Physical;
		Value.Power = 50;
		Value.bAlwaysHits = true;
		Value.Accuracy = 0;
		Value.bUsesPP = false;
		Value.BasePP = 0;
		Value.bAllowsPPBoosts = false;
		Value.Priority = 0;
		Value.TargetClass = EBattleTargetClass::SelectedOpponent;
		Value.Flags = EBattleMoveFlags::TypelessDamage | EBattleMoveFlags::Unencoreable;

		FBattleMoveEffectDescriptor Damage;
		Damage.Order = 0;
		Damage.Kind = EBattleMoveEffectKind::Damage;
		Damage.Target = EBattleEffectTarget::ResolvedTarget;
		Value.Effects.Add(Damage);

		FBattleMoveEffectDescriptor Recoil;
		Recoil.Order = 1;
		Recoil.Kind = EBattleMoveEffectKind::Recoil;
		Recoil.Target = EBattleEffectTarget::User;
		Recoil.MagnitudeNumerator = 1;
		Recoil.MagnitudeDenominator = 4;
		Recoil.Flags = EBattleMoveEffectFlags::MinimumOne;
		Value.Effects.Add(Recoil);
		return Value;
	}();
	return Definition;
}
