#include "Battle/BattleTriggerFramework.h"

namespace BattleTriggerFrameworkPrivate
{
	constexpr uint8 KnownCleanupPolicyMask =
		static_cast<uint8>(EBattleTriggerCleanupPolicy::OnSwitch)
		| static_cast<uint8>(EBattleTriggerCleanupPolicy::OnFaint)
		| static_cast<uint8>(EBattleTriggerCleanupPolicy::OnCapture)
		| static_cast<uint8>(EBattleTriggerCleanupPolicy::OnBattleEnd)
		| static_cast<uint8>(EBattleTriggerCleanupPolicy::OnRemoval);

	bool IsKnownPhase(const EBattleTriggerPhase Phase)
	{
		return static_cast<uint8>(Phase) <= static_cast<uint8>(EBattleTriggerPhase::Expiry);
	}

	bool IsKnownSide(const EBattleSide Side)
	{
		return Side == EBattleSide::Player || Side == EBattleSide::Opponent;
	}

	bool IsKnownDirection(const EBattleTriggerSortDirection Direction)
	{
		return Direction == EBattleTriggerSortDirection::Ascending
			|| Direction == EBattleTriggerSortDirection::Descending;
	}

	bool IsValidOrderPolicy(const FBattleTriggerOrderPolicy& Policy)
	{
		return IsKnownDirection(Policy.Order)
			&& IsKnownDirection(Policy.Priority)
			&& IsKnownDirection(Policy.Suborder)
			&& IsKnownDirection(Policy.EffectiveSpeed)
			&& IsKnownDirection(Policy.Side)
			&& IsKnownDirection(Policy.Position)
			&& IsKnownDirection(Policy.Creation);
	}

	bool IsKnownCleanupReason(const EBattleTriggerCleanupReason Reason)
	{
		return static_cast<uint8>(Reason)
			<= static_cast<uint8>(EBattleTriggerCleanupReason::Removal);
	}

	bool IsValidCleanupPolicy(const EBattleTriggerCleanupPolicy Policy)
	{
		return (static_cast<uint8>(Policy) & ~KnownCleanupPolicyMask) == 0;
	}

	EBattleTriggerCleanupPolicy PolicyForReason(const EBattleTriggerCleanupReason Reason)
	{
		switch (Reason)
		{
		case EBattleTriggerCleanupReason::Switch:
			return EBattleTriggerCleanupPolicy::OnSwitch;
		case EBattleTriggerCleanupReason::Faint:
			return EBattleTriggerCleanupPolicy::OnFaint;
		case EBattleTriggerCleanupReason::Capture:
			return EBattleTriggerCleanupPolicy::OnCapture;
		case EBattleTriggerCleanupReason::BattleEnd:
			return EBattleTriggerCleanupPolicy::OnBattleEnd;
		case EBattleTriggerCleanupReason::Removal:
			return EBattleTriggerCleanupPolicy::OnRemoval;
		default:
			return EBattleTriggerCleanupPolicy::None;
		}
	}

	EBattleTriggerEndReason EndReasonForCleanup(const EBattleTriggerCleanupReason Reason)
	{
		switch (Reason)
		{
		case EBattleTriggerCleanupReason::Switch:
			return EBattleTriggerEndReason::Switch;
		case EBattleTriggerCleanupReason::Faint:
			return EBattleTriggerEndReason::Faint;
		case EBattleTriggerCleanupReason::Capture:
			return EBattleTriggerEndReason::Capture;
		case EBattleTriggerCleanupReason::BattleEnd:
			return EBattleTriggerEndReason::BattleEnd;
		case EBattleTriggerCleanupReason::Removal:
		default:
			return EBattleTriggerEndReason::Removal;
		}
	}

	bool IsValidOperationContext(const FBattleTriggerOperationContext& Context)
	{
		return Context.ReentrancyToken.IsValid()
			&& (!Context.SimultaneousGroupId.IsSet()
				|| Context.SimultaneousGroupId.GetValue().IsValid());
	}

	bool ContainsSubject(
		const TArray<FBattleTriggerSubject>& Subjects,
		const FBattleTriggerSubject& Subject)
	{
		for (const FBattleTriggerSubject& Candidate : Subjects)
		{
			if (Candidate == Subject)
			{
				return true;
			}
		}
		return false;
	}

	bool ValidateRegistrationSpec(
		const FBattleTriggerRegistrationSpec& Spec,
		EBattleTriggerError& OutError)
	{
		if (!IsKnownPhase(Spec.Rule.Phase))
		{
			OutError = EBattleTriggerError::InvalidPhase;
			return false;
		}
		if (!Spec.Rule.EffectId.IsValid() || !Spec.SourceDefinition.IsValid())
		{
			OutError = EBattleTriggerError::InvalidDefinition;
			return false;
		}
		if (!Spec.Owner.IsValid() || !Spec.Source.IsValid() || !Spec.DurationOwner.IsValid())
		{
			OutError = EBattleTriggerError::InvalidSubject;
			return false;
		}
		for (const FBattleTriggerSubject& Target : Spec.Targets)
		{
			if (!Target.IsValid())
			{
				OutError = EBattleTriggerError::InvalidSubject;
				return false;
			}
		}
		if (Spec.RemainingTurns.IsSet() && Spec.RemainingTurns.GetValue() < 0)
		{
			OutError = EBattleTriggerError::InvalidDuration;
			return false;
		}
		if (Spec.Layers <= 0)
		{
			OutError = EBattleTriggerError::InvalidLayers;
			return false;
		}
		if (!Spec.Visibility.IsValid())
		{
			OutError = EBattleTriggerError::InvalidVisibility;
			return false;
		}
		if (!IsValidCleanupPolicy(Spec.CleanupPolicy))
		{
			OutError = EBattleTriggerError::InvalidCleanupPolicy;
			return false;
		}

		OutError = EBattleTriggerError::None;
		return true;
	}

	int32 CompareInt64(
		const int64 Left,
		const int64 Right,
		const EBattleTriggerSortDirection Direction)
	{
		if (Left == Right)
		{
			return 0;
		}
		const bool bLeftFirst = Direction == EBattleTriggerSortDirection::Ascending
			? Left < Right
			: Left > Right;
		return bLeftFirst ? -1 : 1;
	}

	void ResolveSubjectLocation(
		const FBattleTriggerSubject& Subject,
		uint8& OutSideOrdinal,
		uint8& OutPositionOrdinal)
	{
		OutSideOrdinal = 2;
		OutPositionOrdinal = 2;
		if (Subject.Kind == EBattleTriggerSubjectKind::ActiveSlot && Subject.ActiveSlotId.IsValid())
		{
			OutSideOrdinal = static_cast<uint8>(Subject.ActiveSlotId.GetSide());
			OutPositionOrdinal = static_cast<uint8>(Subject.ActiveSlotId.GetPosition());
		}
		else if (Subject.Kind == EBattleTriggerSubjectKind::Side
			&& Subject.bHasSide
			&& IsKnownSide(Subject.Side))
		{
			OutSideOrdinal = static_cast<uint8>(Subject.Side);
		}
	}
}

bool FBattleTriggerSourceDefinition::TryCreateCondition(
	const FConditionId& InConditionId,
	FBattleTriggerSourceDefinition& OutSource)
{
	OutSource = FBattleTriggerSourceDefinition();
	if (!InConditionId.IsValid())
	{
		return false;
	}
	OutSource.Kind = EBattleTriggerSourceDefinitionKind::Condition;
	OutSource.ConditionId = InConditionId;
	return true;
}

bool FBattleTriggerSourceDefinition::TryCreateAbility(
	const FAbilityId& InAbilityId,
	FBattleTriggerSourceDefinition& OutSource)
{
	OutSource = FBattleTriggerSourceDefinition();
	if (!InAbilityId.IsValid())
	{
		return false;
	}
	OutSource.Kind = EBattleTriggerSourceDefinitionKind::Ability;
	OutSource.AbilityId = InAbilityId;
	return true;
}

bool FBattleTriggerSourceDefinition::TryCreateItem(
	const FItemId& InItemId,
	FBattleTriggerSourceDefinition& OutSource)
{
	OutSource = FBattleTriggerSourceDefinition();
	if (!InItemId.IsValid())
	{
		return false;
	}
	OutSource.Kind = EBattleTriggerSourceDefinitionKind::Item;
	OutSource.ItemId = InItemId;
	return true;
}

bool FBattleTriggerSourceDefinition::IsValid() const
{
	switch (Kind)
	{
	case EBattleTriggerSourceDefinitionKind::Condition:
		return ConditionId.IsValid() && !AbilityId.IsValid() && !ItemId.IsValid();
	case EBattleTriggerSourceDefinitionKind::Ability:
		return !ConditionId.IsValid() && AbilityId.IsValid() && !ItemId.IsValid();
	case EBattleTriggerSourceDefinitionKind::Item:
		return !ConditionId.IsValid() && !AbilityId.IsValid() && ItemId.IsValid();
	default:
		return false;
	}
}

bool FBattleTriggerSubject::TryCreateBattle(
	const FBattleId InBattleId,
	FBattleTriggerSubject& OutSubject)
{
	OutSubject = FBattleTriggerSubject();
	if (!InBattleId.IsValid())
	{
		return false;
	}
	OutSubject.Kind = EBattleTriggerSubjectKind::Battle;
	OutSubject.BattleId = InBattleId;
	return true;
}

FBattleTriggerSubject FBattleTriggerSubject::CreateField()
{
	FBattleTriggerSubject Result;
	Result.Kind = EBattleTriggerSubjectKind::Field;
	return Result;
}

bool FBattleTriggerSubject::TryCreateSide(
	const EBattleSide InSide,
	FBattleTriggerSubject& OutSubject)
{
	OutSubject = FBattleTriggerSubject();
	if (!BattleTriggerFrameworkPrivate::IsKnownSide(InSide))
	{
		return false;
	}
	OutSubject.Kind = EBattleTriggerSubjectKind::Side;
	OutSubject.Side = InSide;
	OutSubject.bHasSide = true;
	return true;
}

bool FBattleTriggerSubject::TryCreateTrainer(
	const FTrainerId InTrainerId,
	FBattleTriggerSubject& OutSubject)
{
	OutSubject = FBattleTriggerSubject();
	if (!InTrainerId.IsValid())
	{
		return false;
	}
	OutSubject.Kind = EBattleTriggerSubjectKind::Trainer;
	OutSubject.TrainerId = InTrainerId;
	return true;
}

bool FBattleTriggerSubject::TryCreateBattler(
	const FBattlerId InBattlerId,
	FBattleTriggerSubject& OutSubject)
{
	OutSubject = FBattleTriggerSubject();
	if (!InBattlerId.IsValid())
	{
		return false;
	}
	OutSubject.Kind = EBattleTriggerSubjectKind::Battler;
	OutSubject.BattlerId = InBattlerId;
	return true;
}

bool FBattleTriggerSubject::TryCreateActiveSlot(
	const FActiveSlotId InActiveSlotId,
	FBattleTriggerSubject& OutSubject)
{
	OutSubject = FBattleTriggerSubject();
	if (!InActiveSlotId.IsValid())
	{
		return false;
	}
	OutSubject.Kind = EBattleTriggerSubjectKind::ActiveSlot;
	OutSubject.ActiveSlotId = InActiveSlotId;
	return true;
}

bool FBattleTriggerSubject::IsValid() const
{
	const bool bNoBattle = !BattleId.IsValid();
	const bool bNoTrainer = !TrainerId.IsValid();
	const bool bNoBattler = !BattlerId.IsValid();
	const bool bNoActiveSlot = !ActiveSlotId.IsValid();
	switch (Kind)
	{
	case EBattleTriggerSubjectKind::Battle:
		return BattleId.IsValid() && !bHasSide && bNoTrainer && bNoBattler && bNoActiveSlot;
	case EBattleTriggerSubjectKind::Field:
		return bNoBattle && !bHasSide && bNoTrainer && bNoBattler && bNoActiveSlot;
	case EBattleTriggerSubjectKind::Side:
		return bNoBattle && bHasSide && BattleTriggerFrameworkPrivate::IsKnownSide(Side)
			&& bNoTrainer && bNoBattler && bNoActiveSlot;
	case EBattleTriggerSubjectKind::Trainer:
		return bNoBattle && !bHasSide && TrainerId.IsValid() && bNoBattler && bNoActiveSlot;
	case EBattleTriggerSubjectKind::Battler:
		return bNoBattle && !bHasSide && bNoTrainer && BattlerId.IsValid() && bNoActiveSlot;
	case EBattleTriggerSubjectKind::ActiveSlot:
		return bNoBattle && !bHasSide && bNoTrainer && bNoBattler && ActiveSlotId.IsValid();
	default:
		return false;
	}
}

FBattleTriggerVisibility FBattleTriggerVisibility::CreateCoreOnly()
{
	return FBattleTriggerVisibility();
}

FBattleTriggerVisibility FBattleTriggerVisibility::CreatePublic()
{
	FBattleTriggerVisibility Result;
	Result.Level = EBattleVisibilityLevel::Public;
	return Result;
}

bool FBattleTriggerVisibility::TryCreateOwningTrainer(
	const FTrainerId InTrainerId,
	FBattleTriggerVisibility& OutVisibility)
{
	OutVisibility = FBattleTriggerVisibility();
	if (!InTrainerId.IsValid())
	{
		return false;
	}
	OutVisibility.Level = EBattleVisibilityLevel::OwningTrainer;
	OutVisibility.OwningTrainerId = InTrainerId;
	return true;
}

bool FBattleTriggerVisibility::TryCreateOwningSide(
	const EBattleSide InSide,
	FBattleTriggerVisibility& OutVisibility)
{
	OutVisibility = FBattleTriggerVisibility();
	if (!BattleTriggerFrameworkPrivate::IsKnownSide(InSide))
	{
		return false;
	}
	OutVisibility.Level = EBattleVisibilityLevel::OwningSide;
	OutVisibility.OwningSide = InSide;
	OutVisibility.bHasOwningSide = true;
	return true;
}

bool FBattleTriggerVisibility::IsValid() const
{
	switch (Level)
	{
	case EBattleVisibilityLevel::CoreOnly:
	case EBattleVisibilityLevel::Public:
		return !OwningTrainerId.IsValid() && !bHasOwningSide;
	case EBattleVisibilityLevel::OwningTrainer:
		return OwningTrainerId.IsValid() && !bHasOwningSide;
	case EBattleVisibilityLevel::OwningSide:
		return !OwningTrainerId.IsValid()
			&& bHasOwningSide
			&& BattleTriggerFrameworkPrivate::IsKnownSide(OwningSide);
	default:
		return false;
	}
}

bool FBattleTriggerFramework::TryRegister(
	const FBattleTriggerRegistrationSpec& Spec,
	FBattleTriggerRegistrationId& OutRegistrationId,
	EBattleTriggerError& OutError)
{
	OutRegistrationId = FBattleTriggerRegistrationId();
	OutError = EBattleTriggerError::None;
	if (!BattleTriggerFrameworkPrivate::ValidateRegistrationSpec(Spec, OutError))
	{
		return false;
	}
	if (NextRegistrationValue == 0 || NextCreationOrdinal == 0 || NextLifecycleFactOrdinal == 0)
	{
		OutError = EBattleTriggerError::InvalidRegistrationId;
		return false;
	}

	FBattleTriggerRegistrationId NewId;
	if (!FBattleTriggerRegistrationId::TryCreate(NextRegistrationValue, NewId))
	{
		OutError = EBattleTriggerError::InvalidRegistrationId;
		return false;
	}

	FRuntimeRegistration Runtime;
	Runtime.RegistrationId = NewId;
	Runtime.CreationOrdinal = NextCreationOrdinal;
	Runtime.Spec = Spec;
	Runtime.RemainingTurns = Spec.RemainingTurns;
	Runtime.Layers = Spec.Layers;
	Runtime.bSuppressed = Spec.bSuppressed;
	Registrations.Add(MoveTemp(Runtime));
	++NextRegistrationValue;
	++NextCreationOrdinal;

	FBattleTriggerLifecycleFact Started;
	Started.Kind = EBattleTriggerLifecycleFactKind::Started;
	Started.RegistrationId = NewId;
	Started.Phase = Spec.Rule.Phase;
	Started.SourceDefinition = Spec.SourceDefinition;
	Started.Owner = Spec.Owner;
	Started.RemainingTurns = Spec.RemainingTurns;
	Started.Layers = Spec.Layers;
	Started.IsSuppressed = Spec.bSuppressed;
	AppendLifecycleFact(MoveTemp(Started));

	OutRegistrationId = NewId;
	return true;
}

int32 FBattleTriggerFramework::FindRegistrationIndex(
	const FBattleTriggerRegistrationId RegistrationId) const
{
	for (int32 Index = 0; Index < Registrations.Num(); ++Index)
	{
		if (Registrations[Index].RegistrationId == RegistrationId)
		{
			return Index;
		}
	}
	return INDEX_NONE;
}

bool FBattleTriggerFramework::ValidateDispatch(
	const FBattleTriggerDispatchSpec& Spec,
	EBattleTriggerError& OutError) const
{
	if (!BattleTriggerFrameworkPrivate::IsKnownPhase(Spec.Phase))
	{
		OutError = EBattleTriggerError::InvalidPhase;
		return false;
	}
	if (!Spec.ReentrancyToken.IsValid())
	{
		OutError = EBattleTriggerError::InvalidReentrancyToken;
		return false;
	}
	if (Spec.SimultaneousGroupId.IsSet()
		&& !Spec.SimultaneousGroupId.GetValue().IsValid())
	{
		OutError = EBattleTriggerError::InvalidSimultaneousGroup;
		return false;
	}
	if (!BattleTriggerFrameworkPrivate::IsValidOrderPolicy(Spec.OrderPolicy))
	{
		OutError = EBattleTriggerError::InvalidOrderPolicy;
		return false;
	}
	for (const FBattleTriggerSubject& DurationOwner : Spec.DurationTickOwners)
	{
		if (!DurationOwner.IsValid())
		{
			OutError = EBattleTriggerError::InvalidSubject;
			return false;
		}
	}
	if (Spec.Participants.IsEmpty() && Spec.OrderPolicy.bUseEffectiveSpeed)
	{
		OutError = EBattleTriggerError::MissingEffectiveSpeed;
		return false;
	}

	TSet<uint64> ParticipantIds;
	for (const FBattleTriggerDispatchParticipant& Participant : Spec.Participants)
	{
		if (!Participant.RegistrationId.IsValid())
		{
			OutError = EBattleTriggerError::InvalidRegistrationId;
			return false;
		}
		if (ParticipantIds.Contains(Participant.RegistrationId.GetValue()))
		{
			OutError = EBattleTriggerError::DuplicateParticipant;
			return false;
		}
		ParticipantIds.Add(Participant.RegistrationId.GetValue());

		const int32 RegistrationIndex = FindRegistrationIndex(Participant.RegistrationId);
		if (RegistrationIndex == INDEX_NONE
			|| Registrations[RegistrationIndex].Spec.Rule.Phase != Spec.Phase)
		{
			OutError = EBattleTriggerError::InvalidParticipant;
			return false;
		}
		if (Participant.EffectiveSpeed.IsSet() && Participant.EffectiveSpeed.GetValue() < 0)
		{
			OutError = EBattleTriggerError::InvalidParticipant;
			return false;
		}
		if (Spec.OrderPolicy.bUseEffectiveSpeed && !Participant.EffectiveSpeed.IsSet())
		{
			OutError = EBattleTriggerError::MissingEffectiveSpeed;
			return false;
		}
		if (Participant.ActiveSlotId.IsSet() && !Participant.ActiveSlotId.GetValue().IsValid())
		{
			OutError = EBattleTriggerError::InvalidParticipant;
			return false;
		}
	}

	OutError = EBattleTriggerError::None;
	return true;
}

bool FBattleTriggerFramework::TryEnqueueDispatch(
	const FBattleTriggerDispatchSpec& Spec,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	if (!ValidateDispatch(Spec, OutError))
	{
		return false;
	}
	PendingDispatches.Add(Spec);
	return true;
}

bool FBattleTriggerFramework::TryResolveNextDispatch(
	FBattleTriggerDispatchResult& OutResult,
	EBattleTriggerError& OutError)
{
	OutResult = FBattleTriggerDispatchResult();
	OutError = EBattleTriggerError::None;
	if (PendingDispatches.IsEmpty())
	{
		OutError = EBattleTriggerError::QueueEmpty;
		return false;
	}

	FBattleTriggerDispatchSpec Dispatch = MoveTemp(PendingDispatches[0]);
	PendingDispatches.RemoveAt(0);
	OutResult.Phase = Dispatch.Phase;

	struct FCandidate
	{
		FBattleTriggerRegistrationId RegistrationId;
		int32 Order = 0;
		int32 Priority = 0;
		int32 Suborder = 0;
		TOptional<int32> EffectiveSpeed;
		uint8 SideOrdinal = 2;
		uint8 PositionOrdinal = 2;
		uint64 CreationOrdinal = 0;
	};

	TArray<FCandidate> Candidates;
	auto AddCandidate = [&Candidates](
		const FRuntimeRegistration& Runtime,
		const FBattleTriggerDispatchParticipant* Participant)
	{
		if (Runtime.bSuppressed)
		{
			return;
		}

		FCandidate Candidate;
		Candidate.RegistrationId = Runtime.RegistrationId;
		Candidate.Order = Runtime.Spec.Rule.Order;
		Candidate.Priority = Runtime.Spec.Rule.Priority;
		Candidate.Suborder = Runtime.Spec.Rule.Suborder;
		Candidate.CreationOrdinal = Runtime.CreationOrdinal;
		BattleTriggerFrameworkPrivate::ResolveSubjectLocation(
			Runtime.Spec.Owner,
			Candidate.SideOrdinal,
			Candidate.PositionOrdinal);
		if (Candidate.SideOrdinal == 2)
		{
			BattleTriggerFrameworkPrivate::ResolveSubjectLocation(
				Runtime.Spec.Source,
				Candidate.SideOrdinal,
				Candidate.PositionOrdinal);
		}
		if (Participant != nullptr)
		{
			Candidate.EffectiveSpeed = Participant->EffectiveSpeed;
			if (Participant->ActiveSlotId.IsSet())
			{
				Candidate.SideOrdinal = static_cast<uint8>(Participant->ActiveSlotId.GetValue().GetSide());
				Candidate.PositionOrdinal = static_cast<uint8>(Participant->ActiveSlotId.GetValue().GetPosition());
			}
		}
		Candidates.Add(MoveTemp(Candidate));
	};

	if (Dispatch.Participants.IsEmpty())
	{
		for (const FRuntimeRegistration& Runtime : Registrations)
		{
			if (Runtime.Spec.Rule.Phase == Dispatch.Phase)
			{
				AddCandidate(Runtime, nullptr);
			}
		}
	}
	else
	{
		for (const FBattleTriggerDispatchParticipant& Participant : Dispatch.Participants)
		{
			const int32 RegistrationIndex = FindRegistrationIndex(Participant.RegistrationId);
			if (RegistrationIndex != INDEX_NONE
				&& Registrations[RegistrationIndex].Spec.Rule.Phase == Dispatch.Phase)
			{
				AddCandidate(Registrations[RegistrationIndex], &Participant);
			}
		}
	}

	const FBattleTriggerOrderPolicy Policy = Dispatch.OrderPolicy;
	Candidates.Sort([Policy](const FCandidate& Left, const FCandidate& Right)
	{
		using BattleTriggerFrameworkPrivate::CompareInt64;
		int32 Comparison = CompareInt64(Left.Order, Right.Order, Policy.Order);
		if (Comparison != 0) return Comparison < 0;
		Comparison = CompareInt64(Left.Priority, Right.Priority, Policy.Priority);
		if (Comparison != 0) return Comparison < 0;
		Comparison = CompareInt64(Left.Suborder, Right.Suborder, Policy.Suborder);
		if (Comparison != 0) return Comparison < 0;
		if (Policy.bUseEffectiveSpeed)
		{
			Comparison = CompareInt64(
				Left.EffectiveSpeed.GetValue(),
				Right.EffectiveSpeed.GetValue(),
				Policy.EffectiveSpeed);
			if (Comparison != 0) return Comparison < 0;
		}
		Comparison = CompareInt64(Left.SideOrdinal, Right.SideOrdinal, Policy.Side);
		if (Comparison != 0) return Comparison < 0;
		Comparison = CompareInt64(Left.PositionOrdinal, Right.PositionOrdinal, Policy.Position);
		if (Comparison != 0) return Comparison < 0;
		Comparison = CompareInt64(
			static_cast<int64>(Left.CreationOrdinal),
			static_cast<int64>(Right.CreationOrdinal),
			Policy.Creation);
		return Comparison < 0;
	});

	OutResult.ConsideredCount = Candidates.Num();
	const uint64 TokenValue = Dispatch.ReentrancyToken.GetValue();
	for (const FCandidate& Candidate : Candidates)
	{
		const int32 RegistrationIndex = FindRegistrationIndex(Candidate.RegistrationId);
		if (RegistrationIndex == INDEX_NONE)
		{
			continue;
		}
		FRuntimeRegistration& Runtime = Registrations[RegistrationIndex];
		if (Runtime.bSuppressed
			|| (!Runtime.Spec.Rule.bRepeatable && Runtime.ExecutedTokens.Contains(TokenValue)))
		{
			continue;
		}

		const bool bDurationOwnerTicks = BattleTriggerFrameworkPrivate::ContainsSubject(
			Dispatch.DurationTickOwners,
			Runtime.Spec.DurationOwner);
		const bool bTickDuration = Runtime.Spec.Rule.bDecrementDurationBeforeEffect
			&& Runtime.RemainingTurns.IsSet()
			&& bDurationOwnerTicks
			&& !Runtime.DurationTickTokens.Contains(TokenValue);
		if (bTickDuration)
		{
			Runtime.DurationTickTokens.Add(TokenValue);
			const int32 PreviousTurns = Runtime.RemainingTurns.GetValue();
			const int32 CurrentTurns = PreviousTurns > 0 ? PreviousTurns - 1 : 0;
			Runtime.RemainingTurns = CurrentTurns;

			FBattleTriggerLifecycleFact DurationChanged;
			DurationChanged.Kind = EBattleTriggerLifecycleFactKind::DurationChanged;
			DurationChanged.RegistrationId = Runtime.RegistrationId;
			DurationChanged.Phase = Runtime.Spec.Rule.Phase;
			DurationChanged.SourceDefinition = Runtime.Spec.SourceDefinition;
			DurationChanged.Owner = Runtime.Spec.Owner;
			DurationChanged.ReentrancyToken = Dispatch.ReentrancyToken;
			DurationChanged.SimultaneousGroupId = Dispatch.SimultaneousGroupId;
			DurationChanged.PreviousRemainingTurns = PreviousTurns;
			DurationChanged.RemainingTurns = CurrentTurns;
			AppendLifecycleFact(MoveTemp(DurationChanged));

			if (CurrentTurns == 0)
			{
				FBattleTriggerLifecycleFact Ended;
				Ended.Kind = EBattleTriggerLifecycleFactKind::Ended;
				Ended.RegistrationId = Runtime.RegistrationId;
				Ended.Phase = Runtime.Spec.Rule.Phase;
				Ended.SourceDefinition = Runtime.Spec.SourceDefinition;
				Ended.Owner = Runtime.Spec.Owner;
				Ended.ReentrancyToken = Dispatch.ReentrancyToken;
				Ended.SimultaneousGroupId = Dispatch.SimultaneousGroupId;
				Ended.RemainingTurns = CurrentTurns;
				Ended.Layers = Runtime.Layers;
				Ended.IsSuppressed = Runtime.bSuppressed;
				Ended.EndReason = EBattleTriggerEndReason::Expired;
				AppendLifecycleFact(MoveTemp(Ended));

				Registrations.RemoveAt(RegistrationIndex);
				++OutResult.ExpiredCount;
				continue;
			}
		}

		if (NextEffectRequestOrdinal == 0)
		{
			continue;
		}
		FBattleTriggerEffectRequest EffectRequest;
		EffectRequest.RequestOrdinal = NextEffectRequestOrdinal++;
		EffectRequest.RegistrationId = Runtime.RegistrationId;
		EffectRequest.Phase = Runtime.Spec.Rule.Phase;
		EffectRequest.EffectId = Runtime.Spec.Rule.EffectId;
		EffectRequest.PayloadId = Runtime.Spec.Rule.PayloadId;
		EffectRequest.SourceDefinition = Runtime.Spec.SourceDefinition;
		EffectRequest.Owner = Runtime.Spec.Owner;
		EffectRequest.Source = Runtime.Spec.Source;
		EffectRequest.Targets = Runtime.Spec.Targets;
		EffectRequest.DurationOwner = Runtime.Spec.DurationOwner;
		EffectRequest.RemainingTurns = Runtime.RemainingTurns;
		EffectRequest.Layers = Runtime.Layers;
		EffectRequest.Visibility = Runtime.Spec.Visibility;
		EffectRequest.ReentrancyToken = Dispatch.ReentrancyToken;
		EffectRequest.SimultaneousGroupId = Dispatch.SimultaneousGroupId;
		EffectRequest.ResolvedOrder.Order = Candidate.Order;
		EffectRequest.ResolvedOrder.Priority = Candidate.Priority;
		EffectRequest.ResolvedOrder.Suborder = Candidate.Suborder;
		EffectRequest.ResolvedOrder.EffectiveSpeed = Candidate.EffectiveSpeed;
		EffectRequest.ResolvedOrder.SideOrdinal = Candidate.SideOrdinal;
		EffectRequest.ResolvedOrder.PositionOrdinal = Candidate.PositionOrdinal;
		EffectRequest.ResolvedOrder.CreationOrdinal = Candidate.CreationOrdinal;
		PendingEffectRequests.Add(MoveTemp(EffectRequest));
		++OutResult.EffectRequestCount;

		if (!Runtime.Spec.Rule.bRepeatable)
		{
			Runtime.ExecutedTokens.Add(TokenValue);
		}
	}

	if (OutResult.ExpiredCount > 0)
	{
		FBattleTriggerDispatchSpec ExpiryDispatch;
		ExpiryDispatch.Phase = EBattleTriggerPhase::Expiry;
		ExpiryDispatch.ReentrancyToken = Dispatch.ReentrancyToken;
		ExpiryDispatch.SimultaneousGroupId = Dispatch.SimultaneousGroupId;
		ExpiryDispatch.OrderPolicy = Dispatch.OrderPolicy;
		ExpiryDispatch.OrderPolicy.bUseEffectiveSpeed = false;
		PendingDispatches.Add(MoveTemp(ExpiryDispatch));
		OutResult.bQueuedExpiryDispatch = true;
	}

	return true;
}

bool FBattleTriggerFramework::TryUpdateLayers(
	const FBattleTriggerRegistrationId RegistrationId,
	const int32 NewLayers,
	const FBattleTriggerOperationContext& Context,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	if (!RegistrationId.IsValid())
	{
		OutError = EBattleTriggerError::InvalidRegistrationId;
		return false;
	}
	if (NewLayers <= 0)
	{
		OutError = EBattleTriggerError::InvalidLayers;
		return false;
	}
	if (!BattleTriggerFrameworkPrivate::IsValidOperationContext(Context))
	{
		OutError = !Context.ReentrancyToken.IsValid()
			? EBattleTriggerError::InvalidReentrancyToken
			: EBattleTriggerError::InvalidSimultaneousGroup;
		return false;
	}
	const int32 RegistrationIndex = FindRegistrationIndex(RegistrationId);
	if (RegistrationIndex == INDEX_NONE)
	{
		OutError = EBattleTriggerError::RegistrationNotFound;
		return false;
	}

	FRuntimeRegistration& Runtime = Registrations[RegistrationIndex];
	if (Runtime.Layers == NewLayers)
	{
		return true;
	}
	const int32 PreviousLayers = Runtime.Layers;
	Runtime.Layers = NewLayers;

	FBattleTriggerLifecycleFact Changed;
	Changed.Kind = EBattleTriggerLifecycleFactKind::LayerChanged;
	Changed.RegistrationId = Runtime.RegistrationId;
	Changed.Phase = Runtime.Spec.Rule.Phase;
	Changed.SourceDefinition = Runtime.Spec.SourceDefinition;
	Changed.Owner = Runtime.Spec.Owner;
	Changed.ReentrancyToken = Context.ReentrancyToken;
	Changed.SimultaneousGroupId = Context.SimultaneousGroupId;
	Changed.PreviousLayers = PreviousLayers;
	Changed.Layers = NewLayers;
	AppendLifecycleFact(MoveTemp(Changed));
	return true;
}

bool FBattleTriggerFramework::TrySetSuppressed(
	const FBattleTriggerRegistrationId RegistrationId,
	const bool bSuppressed,
	const FBattleTriggerOperationContext& Context,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	if (!RegistrationId.IsValid())
	{
		OutError = EBattleTriggerError::InvalidRegistrationId;
		return false;
	}
	if (!BattleTriggerFrameworkPrivate::IsValidOperationContext(Context))
	{
		OutError = !Context.ReentrancyToken.IsValid()
			? EBattleTriggerError::InvalidReentrancyToken
			: EBattleTriggerError::InvalidSimultaneousGroup;
		return false;
	}
	const int32 RegistrationIndex = FindRegistrationIndex(RegistrationId);
	if (RegistrationIndex == INDEX_NONE)
	{
		OutError = EBattleTriggerError::RegistrationNotFound;
		return false;
	}

	FRuntimeRegistration& Runtime = Registrations[RegistrationIndex];
	if (Runtime.bSuppressed == bSuppressed)
	{
		return true;
	}
	const bool bWasSuppressed = Runtime.bSuppressed;
	Runtime.bSuppressed = bSuppressed;

	FBattleTriggerLifecycleFact Changed;
	Changed.Kind = EBattleTriggerLifecycleFactKind::SuppressionChanged;
	Changed.RegistrationId = Runtime.RegistrationId;
	Changed.Phase = Runtime.Spec.Rule.Phase;
	Changed.SourceDefinition = Runtime.Spec.SourceDefinition;
	Changed.Owner = Runtime.Spec.Owner;
	Changed.ReentrancyToken = Context.ReentrancyToken;
	Changed.SimultaneousGroupId = Context.SimultaneousGroupId;
	Changed.WasSuppressed = bWasSuppressed;
	Changed.IsSuppressed = bSuppressed;
	AppendLifecycleFact(MoveTemp(Changed));
	return true;
}

bool FBattleTriggerFramework::TryApplyCleanup(
	const FBattleTriggerCleanupRequest& Request,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	if (!BattleTriggerFrameworkPrivate::IsKnownCleanupReason(Request.Reason))
	{
		OutError = EBattleTriggerError::InvalidCleanupRequest;
		return false;
	}
	if (!BattleTriggerFrameworkPrivate::IsValidOperationContext(Request.Context))
	{
		OutError = !Request.Context.ReentrancyToken.IsValid()
			? EBattleTriggerError::InvalidReentrancyToken
			: EBattleTriggerError::InvalidSimultaneousGroup;
		return false;
	}
	if (Request.Reason != EBattleTriggerCleanupReason::BattleEnd
		&& Request.AffectedOwners.IsEmpty())
	{
		OutError = EBattleTriggerError::InvalidCleanupRequest;
		return false;
	}
	for (const FBattleTriggerSubject& Subject : Request.AffectedOwners)
	{
		if (!Subject.IsValid())
		{
			OutError = EBattleTriggerError::InvalidSubject;
			return false;
		}
	}

	const EBattleTriggerCleanupPolicy RequiredPolicy =
		BattleTriggerFrameworkPrivate::PolicyForReason(Request.Reason);
	TArray<FBattleTriggerRegistrationId> ToRemove;
	for (const FRuntimeRegistration& Runtime : Registrations)
	{
		const bool bPolicyMatches = EnumHasAnyFlags(Runtime.Spec.CleanupPolicy, RequiredPolicy);
		const bool bOwnerMatches = Request.Reason == EBattleTriggerCleanupReason::BattleEnd
			|| BattleTriggerFrameworkPrivate::ContainsSubject(
				Request.AffectedOwners,
				Runtime.Spec.Owner);
		if (bPolicyMatches && bOwnerMatches)
		{
			ToRemove.Add(Runtime.RegistrationId);
		}
	}

	for (const FBattleTriggerRegistrationId RegistrationId : ToRemove)
	{
		const int32 RegistrationIndex = FindRegistrationIndex(RegistrationId);
		if (RegistrationIndex == INDEX_NONE)
		{
			continue;
		}
		const FRuntimeRegistration& Runtime = Registrations[RegistrationIndex];
		FBattleTriggerLifecycleFact Ended;
		Ended.Kind = EBattleTriggerLifecycleFactKind::Ended;
		Ended.RegistrationId = Runtime.RegistrationId;
		Ended.Phase = Runtime.Spec.Rule.Phase;
		Ended.SourceDefinition = Runtime.Spec.SourceDefinition;
		Ended.Owner = Runtime.Spec.Owner;
		Ended.ReentrancyToken = Request.Context.ReentrancyToken;
		Ended.SimultaneousGroupId = Request.Context.SimultaneousGroupId;
		Ended.RemainingTurns = Runtime.RemainingTurns;
		Ended.Layers = Runtime.Layers;
		Ended.IsSuppressed = Runtime.bSuppressed;
		Ended.EndReason = BattleTriggerFrameworkPrivate::EndReasonForCleanup(Request.Reason);
		AppendLifecycleFact(MoveTemp(Ended));
		Registrations.RemoveAt(RegistrationIndex);
	}
	return true;
}

bool FBattleTriggerFramework::TryGetRegistration(
	const FBattleTriggerRegistrationId RegistrationId,
	FBattleTriggerRegistrationState& OutState) const
{
	OutState = FBattleTriggerRegistrationState();
	const int32 RegistrationIndex = FindRegistrationIndex(RegistrationId);
	if (RegistrationIndex == INDEX_NONE)
	{
		return false;
	}
	const FRuntimeRegistration& Runtime = Registrations[RegistrationIndex];
	OutState.RegistrationId = Runtime.RegistrationId;
	OutState.CreationOrdinal = Runtime.CreationOrdinal;
	OutState.Spec = Runtime.Spec;
	OutState.RemainingTurns = Runtime.RemainingTurns;
	OutState.Layers = Runtime.Layers;
	OutState.bSuppressed = Runtime.bSuppressed;
	return true;
}

TArray<FBattleTriggerRegistrationState> FBattleTriggerFramework::GetActiveRegistrations() const
{
	TArray<FBattleTriggerRegistrationState> Result;
	Result.Reserve(Registrations.Num());
	for (const FRuntimeRegistration& Runtime : Registrations)
	{
		FBattleTriggerRegistrationState State;
		State.RegistrationId = Runtime.RegistrationId;
		State.CreationOrdinal = Runtime.CreationOrdinal;
		State.Spec = Runtime.Spec;
		State.RemainingTurns = Runtime.RemainingTurns;
		State.Layers = Runtime.Layers;
		State.bSuppressed = Runtime.bSuppressed;
		Result.Add(MoveTemp(State));
	}
	return Result;
}

void FBattleTriggerFramework::DrainEffectRequests(
	TArray<FBattleTriggerEffectRequest>& OutRequests)
{
	OutRequests = MoveTemp(PendingEffectRequests);
	PendingEffectRequests.Reset();
}

void FBattleTriggerFramework::DrainLifecycleFacts(
	TArray<FBattleTriggerLifecycleFact>& OutFacts)
{
	OutFacts = MoveTemp(PendingLifecycleFacts);
	PendingLifecycleFacts.Reset();
}

void FBattleTriggerFramework::AppendLifecycleFact(FBattleTriggerLifecycleFact&& Fact)
{
	Fact.FactOrdinal = NextLifecycleFactOrdinal++;
	PendingLifecycleFacts.Add(MoveTemp(Fact));
}
