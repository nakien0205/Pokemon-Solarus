#include "Battle/BattleAbilityItemContracts.h"

namespace BattleAbilityItemContractsPrivate
{
	bool IsKnownHookPoint(const EBattleAbilityItemHookPoint HookPoint)
	{
		return static_cast<uint8>(HookPoint)
			<= static_cast<uint8>(EBattleAbilityItemHookPoint::FieldCreation);
	}

	bool IsKnownEffectKind(const EBattleAbilityItemEffectKind EffectKind)
	{
		return static_cast<uint8>(EffectKind)
			<= static_cast<uint8>(EBattleAbilityItemEffectKind::TemporarilyStealItem);
	}

	bool IsKnownRevealPolicy(const EBattleAbilityItemRevealPolicy RevealPolicy)
	{
		return static_cast<uint8>(RevealPolicy)
			<= static_cast<uint8>(EBattleAbilityItemRevealPolicy::OnPublicAttempt);
	}

	bool IsKnownActivationOutcome(const EBattleAbilityItemActivationOutcome Outcome)
	{
		return static_cast<uint8>(Outcome)
			<= static_cast<uint8>(EBattleAbilityItemActivationOutcome::Ignored);
	}

	bool IsKnownTriggerPhase(const EBattleTriggerPhase Phase)
	{
		return static_cast<uint8>(Phase) <= static_cast<uint8>(EBattleTriggerPhase::Expiry);
	}

	bool IsAbilityOrItemSource(const FBattleTriggerSourceDefinition& SourceDefinition)
	{
		return SourceDefinition.IsValid()
			&& (SourceDefinition.Kind == EBattleTriggerSourceDefinitionKind::Ability
				|| SourceDefinition.Kind == EBattleTriggerSourceDefinitionKind::Item);
	}

	bool IsTriggerRequestShapeValid(const FBattleTriggerEffectRequest& Request)
	{
		if (Request.RequestOrdinal == 0
			|| !Request.RegistrationId.IsValid()
			|| !Request.ReentrancyToken.IsValid()
			|| !IsKnownTriggerPhase(Request.Phase)
			|| !Request.EffectId.IsValid()
			|| !IsAbilityOrItemSource(Request.SourceDefinition)
			|| !Request.Owner.IsValid()
			|| !Request.Source.IsValid()
			|| !Request.DurationOwner.IsValid()
			|| !Request.Visibility.IsValid()
			|| Request.Layers <= 0
			|| (Request.SimultaneousGroupId.IsSet()
				&& !Request.SimultaneousGroupId.GetValue().IsValid()))
		{
			return false;
		}
		for (const FBattleTriggerSubject& Target : Request.Targets)
		{
			if (!Target.IsValid())
			{
				return false;
			}
		}
		return true;
	}

	bool HasValidOwnerPair(const FTrainerId TrainerId, const FBattlerId BattlerId)
	{
		return TrainerId.IsValid() && BattlerId.IsValid();
	}

	bool HasEmptyOwnerPair(const FTrainerId TrainerId, const FBattlerId BattlerId)
	{
		return !TrainerId.IsValid() && !BattlerId.IsValid();
	}

	bool IsKnownItemOrigin(const EBattleHeldItemOrigin Origin)
	{
		return Origin == EBattleHeldItemOrigin::Persistent
			|| Origin == EBattleHeldItemOrigin::BattleGenerated;
	}

	bool IsKnownItemOperation(const EBattleHeldItemOperationKind Kind)
	{
		return static_cast<uint8>(Kind)
			<= static_cast<uint8>(EBattleHeldItemOperationKind::TemporarilySteal);
	}

	bool IsHeld(const FBattleHeldItemInstanceState& State)
	{
		return !State.bConsumed
			&& !State.bTemporarilyRemoved
			&& State.CurrentItemId.IsValid()
			&& HasValidOwnerPair(
				State.CurrentHolderTrainerId,
				State.CurrentHolderBattlerId);
	}

	bool IsItemStateValid(const FBattleHeldItemInstanceState& State)
	{
		if (!State.InstanceId.IsValid()
			|| !IsKnownItemOrigin(State.Origin)
			|| !State.DefinitionItemId.IsValid())
		{
			return false;
		}

		if (State.Origin == EBattleHeldItemOrigin::Persistent)
		{
			if (!HasValidOwnerPair(
					State.OriginalOwnerTrainerId,
					State.OriginalOwnerBattlerId)
				|| !State.OriginalItemId.IsValid()
				|| State.OriginalItemId != State.DefinitionItemId)
			{
				return false;
			}
		}
		else if (!HasEmptyOwnerPair(
				State.OriginalOwnerTrainerId,
				State.OriginalOwnerBattlerId)
			|| State.OriginalItemId.IsValid())
		{
			return false;
		}

		const bool bHasLastConsumer = HasValidOwnerPair(
			State.LastConsumerTrainerId,
			State.LastConsumerBattlerId);
		const bool bHasNoLastConsumer = HasEmptyOwnerPair(
			State.LastConsumerTrainerId,
			State.LastConsumerBattlerId);
		if ((!bHasLastConsumer && !bHasNoLastConsumer)
			|| (bHasLastConsumer && State.LastConsumptionFactOrdinal == 0)
			|| (bHasNoLastConsumer && State.LastConsumptionFactOrdinal != 0)
			|| State.LastConsumptionFactOrdinal == MAX_uint64
			|| (State.bConsumed
				&& !bHasLastConsumer
				&& State.Origin != EBattleHeldItemOrigin::Persistent)
			|| (State.bRestoredAfterConsumption && !bHasLastConsumer))
		{
			return false;
		}

		if (State.bConsumed)
		{
			return !State.CurrentItemId.IsValid()
				&& HasEmptyOwnerPair(
					State.CurrentHolderTrainerId,
					State.CurrentHolderBattlerId)
				&& !State.bSuppressed
				&& !State.bTemporarilyRemoved;
		}

		if (!State.CurrentItemId.IsValid()
			|| State.CurrentItemId != State.DefinitionItemId)
		{
			return false;
		}
		if (State.bTemporarilyRemoved)
		{
			return HasEmptyOwnerPair(
					State.CurrentHolderTrainerId,
					State.CurrentHolderBattlerId)
				&& !State.bSuppressed;
		}
		return HasValidOwnerPair(
			State.CurrentHolderTrainerId,
			State.CurrentHolderBattlerId);
	}

	bool ValidateItemStates(
		const TArray<FBattleHeldItemInstanceState>& States,
		EBattleHeldItemContractError& OutError)
	{
		for (int32 LeftIndex = 0; LeftIndex < States.Num(); ++LeftIndex)
		{
			const FBattleHeldItemInstanceState& Left = States[LeftIndex];
			if (!IsItemStateValid(Left))
			{
				OutError = EBattleHeldItemContractError::InvalidState;
				return false;
			}
			for (int32 RightIndex = LeftIndex + 1; RightIndex < States.Num(); ++RightIndex)
			{
				const FBattleHeldItemInstanceState& Right = States[RightIndex];
				if (Left.InstanceId == Right.InstanceId)
				{
					OutError = EBattleHeldItemContractError::DuplicateInstance;
					return false;
				}
				if (Left.LastConsumptionFactOrdinal != 0
					&& Left.LastConsumptionFactOrdinal
						== Right.LastConsumptionFactOrdinal)
				{
					OutError = EBattleHeldItemContractError::InvalidState;
					return false;
				}
				if (Left.Origin == EBattleHeldItemOrigin::Persistent
					&& Right.Origin == EBattleHeldItemOrigin::Persistent
					&& Left.OriginalOwnerTrainerId == Right.OriginalOwnerTrainerId
					&& Left.OriginalOwnerBattlerId == Right.OriginalOwnerBattlerId)
				{
					OutError = EBattleHeldItemContractError::DuplicateOriginalOwner;
					return false;
				}
				if (IsHeld(Left)
					&& IsHeld(Right)
					&& Left.CurrentHolderTrainerId == Right.CurrentHolderTrainerId
					&& Left.CurrentHolderBattlerId == Right.CurrentHolderBattlerId)
				{
					OutError = EBattleHeldItemContractError::DuplicateCurrentHolder;
					return false;
				}
			}
		}
		OutError = EBattleHeldItemContractError::None;
		return true;
	}

	int32 FindItemStateIndex(
		const TArray<FBattleHeldItemInstanceState>& States,
		const FBattleHeldItemInstanceId InstanceId)
	{
		return States.IndexOfByPredicate(
			[InstanceId](const FBattleHeldItemInstanceState& State)
			{
				return State.InstanceId == InstanceId;
			});
	}

	int32 FindCurrentHolderIndex(
		const TArray<FBattleHeldItemInstanceState>& States,
		const FTrainerId TrainerId,
		const FBattlerId BattlerId,
		const int32 IgnoredIndex)
	{
		for (int32 Index = 0; Index < States.Num(); ++Index)
		{
			if (Index != IgnoredIndex
				&& IsHeld(States[Index])
				&& States[Index].CurrentHolderTrainerId == TrainerId
				&& States[Index].CurrentHolderBattlerId == BattlerId)
			{
				return Index;
			}
		}
		return INDEX_NONE;
	}

	bool IsKnownBagRejection(const EBattleBagUseRejectionReason Reason)
	{
		return static_cast<uint8>(Reason)
			<= static_cast<uint8>(EBattleBagUseRejectionReason::IllegalItemTarget);
	}

	bool ValidateBagState(
		const FBattleTrainerBagState& State,
		EBattleBagContractError& OutError)
	{
		if (!State.TrainerId.IsValid())
		{
			OutError = EBattleBagContractError::InvalidState;
			return false;
		}
		for (int32 LeftIndex = 0; LeftIndex < State.Items.Num(); ++LeftIndex)
		{
			const FBattleBagItemCount& Left = State.Items[LeftIndex];
			if (!Left.ItemId.IsValid() || Left.Count < 0)
			{
				OutError = EBattleBagContractError::InvalidState;
				return false;
			}
			for (int32 RightIndex = LeftIndex + 1; RightIndex < State.Items.Num(); ++RightIndex)
			{
				if (Left.ItemId == State.Items[RightIndex].ItemId)
				{
					OutError = EBattleBagContractError::DuplicateItem;
					return false;
				}
			}
		}
		OutError = EBattleBagContractError::None;
		return true;
	}
}

bool FBattleAbilityItemHookContracts::IsDefinitionValid(
	const FBattleAbilityItemHookDefinition& Definition)
{
	return Definition.HookId.IsValid()
		&& BattleAbilityItemContractsPrivate::IsKnownHookPoint(Definition.HookPoint)
		&& BattleAbilityItemContractsPrivate::IsKnownEffectKind(Definition.EffectKind)
		&& BattleAbilityItemContractsPrivate::IsKnownTriggerPhase(Definition.TriggerRule.Phase)
		&& Definition.TriggerRule.EffectId.IsValid()
		&& BattleAbilityItemContractsPrivate::IsKnownRevealPolicy(Definition.RevealPolicy);
}

bool FBattleAbilityItemHookContracts::TryBuildTriggerRegistration(
	const FBattleAbilityItemHookRegistrationFacts& Facts,
	FBattleTriggerRegistrationSpec& OutRegistration,
	EBattleAbilityItemHookError& OutError)
{
	OutRegistration = FBattleTriggerRegistrationSpec();
	if (!IsDefinitionValid(Facts.Definition))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}
	if (!BattleAbilityItemContractsPrivate::IsAbilityOrItemSource(Facts.SourceDefinition))
	{
		OutError = EBattleAbilityItemHookError::InvalidSourceDefinition;
		return false;
	}

	FBattleTriggerRegistrationSpec Candidate;
	Candidate.Rule = Facts.Definition.TriggerRule;
	Candidate.SourceDefinition = Facts.SourceDefinition;
	Candidate.Owner = Facts.Owner;
	Candidate.Source = Facts.Source;
	Candidate.Targets = Facts.Targets;
	Candidate.DurationOwner = Facts.DurationOwner;
	Candidate.RemainingTurns = Facts.RemainingTurns;
	Candidate.Layers = Facts.Layers;
	Candidate.Visibility = Facts.Visibility;
	Candidate.CleanupPolicy = Facts.CleanupPolicy;
	Candidate.bSuppressed = Facts.bSuppressed;

	FBattleTriggerFramework ValidationFramework;
	FBattleTriggerRegistrationId IgnoredRegistrationId;
	EBattleTriggerError TriggerError = EBattleTriggerError::None;
	if (!ValidationFramework.TryRegister(
			Candidate,
			IgnoredRegistrationId,
			TriggerError))
	{
		OutError = EBattleAbilityItemHookError::InvalidRegistration;
		return false;
	}

	OutRegistration = MoveTemp(Candidate);
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleAbilityItemHookContracts::TryRegisterHook(
	FBattleTriggerFramework& Framework,
	const FBattleAbilityItemHookRegistrationFacts& Facts,
	FBattleTriggerRegistrationId& OutRegistrationId,
	EBattleAbilityItemHookError& OutError)
{
	OutRegistrationId = FBattleTriggerRegistrationId();
	FBattleTriggerRegistrationSpec Registration;
	if (!TryBuildTriggerRegistration(Facts, Registration, OutError))
	{
		return false;
	}

	EBattleTriggerError TriggerError = EBattleTriggerError::None;
	if (!Framework.TryRegister(Registration, OutRegistrationId, TriggerError))
	{
		OutRegistrationId = FBattleTriggerRegistrationId();
		OutError = EBattleAbilityItemHookError::InvalidRegistration;
		return false;
	}

	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleAbilityItemHookContracts::TryCreateTypedEffectRequest(
	const FBattleAbilityItemHookDefinition& Definition,
	const FBattleTriggerEffectRequest& TriggerRequest,
	FBattleAbilityItemEffectRequest& OutRequest,
	EBattleAbilityItemHookError& OutError)
{
	OutRequest = FBattleAbilityItemEffectRequest();
	if (!IsDefinitionValid(Definition))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}
	if (!BattleAbilityItemContractsPrivate::IsTriggerRequestShapeValid(TriggerRequest)
		|| TriggerRequest.Phase != Definition.TriggerRule.Phase
		|| TriggerRequest.EffectId != Definition.TriggerRule.EffectId
		|| TriggerRequest.PayloadId != Definition.TriggerRule.PayloadId)
	{
		OutError = EBattleAbilityItemHookError::MismatchedTriggerRequest;
		return false;
	}

	OutRequest.TriggerRequest = TriggerRequest;
	OutRequest.HookId = Definition.HookId;
	OutRequest.HookPoint = Definition.HookPoint;
	OutRequest.EffectKind = Definition.EffectKind;
	OutRequest.RevealPolicy = Definition.RevealPolicy;
	OutRequest.bBreakable = Definition.bBreakable;
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleAbilityItemRevealTracker::TryRecordActivation(
	const FBattleAbilityItemEffectRequest& Request,
	const EBattleAbilityItemActivationOutcome Outcome,
	TOptional<FBattleAbilityItemActivationFact>& OutFact,
	EBattleAbilityItemHookError& OutError)
{
	OutFact.Reset();
	if (!Request.HookId.IsValid()
		|| !BattleAbilityItemContractsPrivate::IsKnownHookPoint(Request.HookPoint)
		|| !BattleAbilityItemContractsPrivate::IsKnownEffectKind(Request.EffectKind)
		|| !BattleAbilityItemContractsPrivate::IsKnownRevealPolicy(Request.RevealPolicy)
		|| !BattleAbilityItemContractsPrivate::IsTriggerRequestShapeValid(Request.TriggerRequest))
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}
	if (!BattleAbilityItemContractsPrivate::IsKnownActivationOutcome(Outcome))
	{
		OutError = EBattleAbilityItemHookError::InvalidActivationOutcome;
		return false;
	}

	const bool bEmitFact = Outcome == EBattleAbilityItemActivationOutcome::Applied
		|| (Outcome == EBattleAbilityItemActivationOutcome::AttemptedButPrevented
			&& Request.RevealPolicy == EBattleAbilityItemRevealPolicy::OnPublicAttempt);
	if (!bEmitFact)
	{
		OutError = EBattleAbilityItemHookError::None;
		return true;
	}

	FBattleAbilityItemActivationFact Fact;
	Fact.HookPoint = Request.HookPoint;
	Fact.EffectKind = Request.EffectKind;
	Fact.Outcome = Outcome;
	Fact.Owner = Request.TriggerRequest.Owner;
	Fact.Visibility = Request.TriggerRequest.Visibility;
	if (Request.RevealPolicy != EBattleAbilityItemRevealPolicy::Never)
	{
		const FBattleAbilityItemRevealKey Key{
			Request.TriggerRequest.SourceDefinition,
			Request.TriggerRequest.Owner};
		const bool bAlreadyRevealed = RevealedKeys.Contains(Key);
		if (!bAlreadyRevealed)
		{
			RevealedKeys.Add(Key);
		}
		Fact.RevealedSourceDefinition = Request.TriggerRequest.SourceDefinition;
		Fact.bFirstPublicReveal = !bAlreadyRevealed;
	}

	OutFact = MoveTemp(Fact);
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleAbilityItemRevealTracker::HasBeenRevealed(
	const FBattleTriggerSourceDefinition& SourceDefinition,
	const FBattleTriggerSubject& Owner) const
{
	const FBattleAbilityItemRevealKey Key{SourceDefinition, Owner};
	return RevealedKeys.Contains(Key);
}

bool FBattleAbilityItemRevealTracker::TryRecordPublicReveal(
	const FBattleTriggerSourceDefinition& SourceDefinition,
	const FBattleTriggerSubject& Owner,
	bool& bOutFirstPublicReveal,
	EBattleAbilityItemHookError& OutError)
{
	bOutFirstPublicReveal = false;
	if (!BattleAbilityItemContractsPrivate::IsAbilityOrItemSource(SourceDefinition))
	{
		OutError = EBattleAbilityItemHookError::InvalidSourceDefinition;
		return false;
	}
	if (!Owner.IsValid())
	{
		OutError = EBattleAbilityItemHookError::InvalidDefinition;
		return false;
	}

	const FBattleAbilityItemRevealKey Key{SourceDefinition, Owner};
	bOutFirstPublicReveal = !RevealedKeys.Contains(Key);
	if (bOutFirstPublicReveal)
	{
		RevealedKeys.Add(Key);
	}
	OutError = EBattleAbilityItemHookError::None;
	return true;
}

bool FBattleHeldItemLedger::TryCreate(
	const TConstArrayView<FBattleHeldItemInstanceState> InitialStates,
	FBattleHeldItemLedger& OutLedger,
	EBattleHeldItemContractError& OutError)
{
	OutLedger = FBattleHeldItemLedger();
	TArray<FBattleHeldItemInstanceState> CandidateStates;
	for (const FBattleHeldItemInstanceState& State : InitialStates)
	{
		CandidateStates.Add(State);
	}
	if (!BattleAbilityItemContractsPrivate::ValidateItemStates(CandidateStates, OutError))
	{
		return false;
	}
	CandidateStates.Sort(
		[](const FBattleHeldItemInstanceState& Left, const FBattleHeldItemInstanceState& Right)
		{
			return Left.InstanceId < Right.InstanceId;
		});
	OutLedger.States = MoveTemp(CandidateStates);
	for (const FBattleHeldItemInstanceState& State : OutLedger.States)
	{
		OutLedger.NextFactOrdinal = FMath::Max(
			OutLedger.NextFactOrdinal,
			State.LastConsumptionFactOrdinal + 1);
	}
	OutError = EBattleHeldItemContractError::None;
	return true;
}

bool FBattleHeldItemLedger::TryApplyOperation(
	const FBattleHeldItemOperationRequest& Request,
	FBattleHeldItemOperationFact& OutFact,
	EBattleHeldItemContractError& OutError)
{
	OutFact = FBattleHeldItemOperationFact();
	if (!BattleAbilityItemContractsPrivate::IsKnownItemOperation(Request.Kind)
		|| !Request.PrimaryInstanceId.IsValid()
		|| NextFactOrdinal == MAX_uint64)
	{
		OutError = EBattleHeldItemContractError::InvalidOperation;
		return false;
	}

	const bool bSwap = Request.Kind == EBattleHeldItemOperationKind::Swap;
	const bool bNeedsTarget = Request.Kind == EBattleHeldItemOperationKind::Restore
		|| Request.Kind == EBattleHeldItemOperationKind::TemporarilySteal;
	const bool bSupportsSuppressedPayload =
		Request.Kind == EBattleHeldItemOperationKind::Suppress
		|| Request.Kind == EBattleHeldItemOperationKind::Restore;
	if ((bSwap
			&& (!Request.SecondaryInstanceId.IsValid()
				|| Request.SecondaryInstanceId == Request.PrimaryInstanceId
				|| !BattleAbilityItemContractsPrivate::HasEmptyOwnerPair(
					Request.TargetHolderTrainerId,
					Request.TargetHolderBattlerId)))
		|| (!bSwap && Request.SecondaryInstanceId.IsValid())
		|| (bNeedsTarget
			&& !BattleAbilityItemContractsPrivate::HasValidOwnerPair(
				Request.TargetHolderTrainerId,
				Request.TargetHolderBattlerId))
		|| (!bNeedsTarget && !bSwap
			&& !BattleAbilityItemContractsPrivate::HasEmptyOwnerPair(
				Request.TargetHolderTrainerId,
				Request.TargetHolderBattlerId))
		|| (!bSupportsSuppressedPayload && Request.bSuppressed))
	{
		OutError = EBattleHeldItemContractError::InvalidOperation;
		return false;
	}

	TArray<FBattleHeldItemInstanceState> CandidateStates = States;
	const int32 PrimaryIndex = BattleAbilityItemContractsPrivate::FindItemStateIndex(
		CandidateStates,
		Request.PrimaryInstanceId);
	if (PrimaryIndex == INDEX_NONE)
	{
		OutError = EBattleHeldItemContractError::InstanceNotFound;
		return false;
	}
	FBattleHeldItemInstanceState& Primary = CandidateStates[PrimaryIndex];
	const FBattleHeldItemInstanceState PrimaryBefore = Primary;
	int32 SecondaryIndex = INDEX_NONE;
	TOptional<FBattleHeldItemInstanceState> SecondaryBefore;

	switch (Request.Kind)
	{
	case EBattleHeldItemOperationKind::Suppress:
		if (!BattleAbilityItemContractsPrivate::IsHeld(Primary))
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		Primary.bSuppressed = Request.bSuppressed;
		break;
	case EBattleHeldItemOperationKind::Reveal:
		if (!BattleAbilityItemContractsPrivate::IsHeld(Primary))
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		Primary.bRevealed = true;
		break;
	case EBattleHeldItemOperationKind::Consume:
		if (!BattleAbilityItemContractsPrivate::IsHeld(Primary))
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		Primary.LastConsumerTrainerId = Primary.CurrentHolderTrainerId;
		Primary.LastConsumerBattlerId = Primary.CurrentHolderBattlerId;
		Primary.LastConsumptionFactOrdinal = NextFactOrdinal;
		Primary.CurrentItemId = FItemId();
		Primary.CurrentHolderTrainerId = FTrainerId();
		Primary.CurrentHolderBattlerId = FBattlerId();
		Primary.bConsumed = true;
		Primary.bSuppressed = false;
		Primary.bTemporarilyRemoved = false;
		break;
	case EBattleHeldItemOperationKind::Restore:
		if (!Primary.bConsumed)
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		if (BattleAbilityItemContractsPrivate::FindCurrentHolderIndex(
				CandidateStates,
				Request.TargetHolderTrainerId,
				Request.TargetHolderBattlerId,
				PrimaryIndex) != INDEX_NONE)
		{
			OutError = EBattleHeldItemContractError::HolderOccupied;
			return false;
		}
		Primary.CurrentItemId = Primary.DefinitionItemId;
		Primary.CurrentHolderTrainerId = Request.TargetHolderTrainerId;
		Primary.CurrentHolderBattlerId = Request.TargetHolderBattlerId;
		Primary.bConsumed = false;
		Primary.bSuppressed = Request.bSuppressed;
		Primary.bRevealed = true;
		Primary.bTemporarilyRemoved = false;
		Primary.bRestoredAfterConsumption = true;
		break;
	case EBattleHeldItemOperationKind::Remove:
		if (!BattleAbilityItemContractsPrivate::IsHeld(Primary))
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		Primary.CurrentHolderTrainerId = FTrainerId();
		Primary.CurrentHolderBattlerId = FBattlerId();
		Primary.bSuppressed = false;
		Primary.bRevealed = true;
		Primary.bTemporarilyRemoved = true;
		break;
	case EBattleHeldItemOperationKind::Swap:
		SecondaryIndex = BattleAbilityItemContractsPrivate::FindItemStateIndex(
			CandidateStates,
			Request.SecondaryInstanceId);
		if (SecondaryIndex == INDEX_NONE)
		{
			OutError = EBattleHeldItemContractError::InstanceNotFound;
			return false;
		}
		if (!BattleAbilityItemContractsPrivate::IsHeld(Primary)
			|| !BattleAbilityItemContractsPrivate::IsHeld(CandidateStates[SecondaryIndex]))
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		SecondaryBefore = CandidateStates[SecondaryIndex];
		Swap(
			Primary.CurrentHolderTrainerId,
			CandidateStates[SecondaryIndex].CurrentHolderTrainerId);
		Swap(
			Primary.CurrentHolderBattlerId,
			CandidateStates[SecondaryIndex].CurrentHolderBattlerId);
		Primary.bRevealed = true;
		CandidateStates[SecondaryIndex].bRevealed = true;
		break;
	case EBattleHeldItemOperationKind::TemporarilySteal:
		if (!BattleAbilityItemContractsPrivate::IsHeld(Primary)
			|| (Primary.CurrentHolderTrainerId == Request.TargetHolderTrainerId
				&& Primary.CurrentHolderBattlerId == Request.TargetHolderBattlerId))
		{
			OutError = EBattleHeldItemContractError::InvalidOperation;
			return false;
		}
		if (BattleAbilityItemContractsPrivate::FindCurrentHolderIndex(
				CandidateStates,
				Request.TargetHolderTrainerId,
				Request.TargetHolderBattlerId,
				PrimaryIndex) != INDEX_NONE)
		{
			OutError = EBattleHeldItemContractError::HolderOccupied;
			return false;
		}
		Primary.CurrentHolderTrainerId = Request.TargetHolderTrainerId;
		Primary.CurrentHolderBattlerId = Request.TargetHolderBattlerId;
		Primary.bRevealed = true;
		break;
	default:
		OutError = EBattleHeldItemContractError::InvalidOperation;
		return false;
	}

	if (!BattleAbilityItemContractsPrivate::ValidateItemStates(CandidateStates, OutError))
	{
		return false;
	}

	OutFact.FactOrdinal = NextFactOrdinal;
	OutFact.Kind = Request.Kind;
	OutFact.PrimaryBefore = PrimaryBefore;
	OutFact.PrimaryAfter = CandidateStates[PrimaryIndex];
	OutFact.SecondaryBefore = SecondaryBefore;
	if (SecondaryIndex != INDEX_NONE)
	{
		OutFact.SecondaryAfter = CandidateStates[SecondaryIndex];
	}
	States = MoveTemp(CandidateStates);
	++NextFactOrdinal;
	OutError = EBattleHeldItemContractError::None;
	return true;
}

bool FBattleHeldItemLedger::TryBuildFinalFacts(
	const TConstArrayView<FBattlerId> CapturedOriginalOwners,
	TArray<FBattleFinalHeldItemFact>& OutFacts,
	EBattleHeldItemContractError& OutError) const
{
	OutFacts.Reset();
	for (int32 LeftIndex = 0; LeftIndex < CapturedOriginalOwners.Num(); ++LeftIndex)
	{
		if (!CapturedOriginalOwners[LeftIndex].IsValid())
		{
			OutError = EBattleHeldItemContractError::InvalidCapturedOwner;
			return false;
		}
		for (int32 RightIndex = LeftIndex + 1; RightIndex < CapturedOriginalOwners.Num(); ++RightIndex)
		{
			if (CapturedOriginalOwners[LeftIndex] == CapturedOriginalOwners[RightIndex])
			{
				OutError = EBattleHeldItemContractError::InvalidCapturedOwner;
				return false;
			}
		}
	}

	EBattleHeldItemContractError ValidationError = EBattleHeldItemContractError::None;
	if (!BattleAbilityItemContractsPrivate::ValidateItemStates(States, ValidationError))
	{
		OutError = ValidationError;
		return false;
	}

	for (const FBattleHeldItemInstanceState& State : States)
	{
		FBattleFinalHeldItemFact Fact;
		Fact.InstanceId = State.InstanceId;
		Fact.DefinitionItemId = State.DefinitionItemId;
		Fact.OriginalOwnerTrainerId = State.OriginalOwnerTrainerId;
		Fact.OriginalOwnerBattlerId = State.OriginalOwnerBattlerId;
		Fact.OriginalItemId = State.OriginalItemId;
		Fact.bRestoredAfterConsumption = State.bRestoredAfterConsumption;
		if (State.Origin == EBattleHeldItemOrigin::BattleGenerated)
		{
			Fact.Disposition = EBattleHeldItemFinalDisposition::BattleGeneratedRemoved;
		}
		else if (State.bConsumed)
		{
			Fact.Disposition = EBattleHeldItemFinalDisposition::Consumed;
		}
		else
		{
			Fact.Disposition = CapturedOriginalOwners.Contains(State.OriginalOwnerBattlerId)
				? EBattleHeldItemFinalDisposition::CapturedOriginalOwner
				: EBattleHeldItemFinalDisposition::OriginalOwner;
			Fact.FinalOwnerTrainerId = State.OriginalOwnerTrainerId;
			Fact.FinalOwnerBattlerId = State.OriginalOwnerBattlerId;
			Fact.FinalItemId = State.OriginalItemId;
		}
		OutFacts.Add(MoveTemp(Fact));
	}

	OutError = EBattleHeldItemContractError::None;
	return true;
}

const FBattleHeldItemInstanceState* FBattleHeldItemLedger::FindState(
	const FBattleHeldItemInstanceId InstanceId) const
{
	return States.FindByPredicate(
		[InstanceId](const FBattleHeldItemInstanceState& State)
		{
			return State.InstanceId == InstanceId;
		});
}

const FBattleHeldItemInstanceState* FBattleHeldItemLedger::FindMostRecentlyConsumedBy(
	const FTrainerId TrainerId,
	const FBattlerId BattlerId) const
{
	if (!TrainerId.IsValid() || !BattlerId.IsValid())
	{
		return nullptr;
	}

	const FBattleHeldItemInstanceState* Best = nullptr;
	for (const FBattleHeldItemInstanceState& State : States)
	{
		if (State.bConsumed
			&& State.LastConsumerTrainerId == TrainerId
			&& State.LastConsumerBattlerId == BattlerId
			&& (Best == nullptr
				|| State.LastConsumptionFactOrdinal
					> Best->LastConsumptionFactOrdinal))
		{
			Best = &State;
		}
	}
	return Best;
}

bool FBattleBagOwnershipContract::TryCreate(
	const TConstArrayView<FBattleTrainerBagState> InitialStates,
	FBattleBagOwnershipContract& OutContract,
	EBattleBagContractError& OutError)
{
	OutContract = FBattleBagOwnershipContract();
	TArray<FBattleTrainerBagState> CandidateStates;
	for (const FBattleTrainerBagState& State : InitialStates)
	{
		CandidateStates.Add(State);
	}
	for (int32 LeftIndex = 0; LeftIndex < CandidateStates.Num(); ++LeftIndex)
	{
		if (!BattleAbilityItemContractsPrivate::ValidateBagState(
				CandidateStates[LeftIndex],
				OutError))
		{
			return false;
		}
		for (int32 RightIndex = LeftIndex + 1; RightIndex < CandidateStates.Num(); ++RightIndex)
		{
			if (CandidateStates[LeftIndex].TrainerId == CandidateStates[RightIndex].TrainerId)
			{
				OutError = EBattleBagContractError::DuplicateTrainer;
				return false;
			}
		}
		CandidateStates[LeftIndex].Items.Sort(
			[](const FBattleBagItemCount& Left, const FBattleBagItemCount& Right)
			{
				return Left.ItemId.LexicalLess(Right.ItemId);
			});
	}
	CandidateStates.Sort(
		[](const FBattleTrainerBagState& Left, const FBattleTrainerBagState& Right)
		{
			return Left.TrainerId < Right.TrainerId;
		});
	OutContract.TrainerStates = MoveTemp(CandidateStates);
	OutError = EBattleBagContractError::None;
	return true;
}

bool FBattleBagOwnershipContract::TryApplyUse(
	const FBattleBagUseRequest& Request,
	FBattleBagUseResult& OutResult,
	EBattleBagContractError& OutError)
{
	OutResult = FBattleBagUseResult();
	if (!Request.ActingTrainerId.IsValid()
		|| !Request.ItemId.IsValid()
		|| !Request.TargetOwnerTrainerId.IsValid())
	{
		OutError = EBattleBagContractError::InvalidRequest;
		return false;
	}

	FBattleTrainerBagState* TrainerState = TrainerStates.FindByPredicate(
		[&Request](const FBattleTrainerBagState& State)
		{
			return State.TrainerId == Request.ActingTrainerId;
		});
	if (TrainerState == nullptr)
	{
		OutError = EBattleBagContractError::TrainerNotFound;
		return false;
	}
	FBattleBagItemCount* ItemCount = TrainerState->Items.FindByPredicate(
		[&Request](const FBattleBagItemCount& Item)
		{
			return Item.ItemId == Request.ItemId;
		});

	OutResult.bValid = true;
	OutResult.Outcome = EBattleBagUseOutcome::PreUseRejected;
	OutResult.CountBefore = ItemCount != nullptr ? ItemCount->Count : 0;
	OutResult.CountAfter = OutResult.CountBefore;
	auto Reject = [&OutResult, &OutError](const EBattleBagUseRejectionReason Reason)
	{
		check(BattleAbilityItemContractsPrivate::IsKnownBagRejection(Reason));
		OutResult.RejectionReason = Reason;
		OutError = EBattleBagContractError::None;
		return true;
	};

	if (!TrainerState->bBagActionAvailable)
	{
		return Reject(EBattleBagUseRejectionReason::BagQuotaUsed);
	}
	if (ItemCount == nullptr || ItemCount->Count <= 0)
	{
		return Reject(EBattleBagUseRejectionReason::NoItemRemaining);
	}
	if (Request.TargetOwnerTrainerId != Request.ActingTrainerId
		&& !Request.bItemExplicitlyAllowsOtherTrainerTarget)
	{
		return Reject(EBattleBagUseRejectionReason::WrongTargetOwner);
	}
	if (!Request.bItemSpecificTargetLegal)
	{
		return Reject(EBattleBagUseRejectionReason::IllegalItemTarget);
	}

	--ItemCount->Count;
	TrainerState->bBagActionAvailable = false;
	OutResult.Outcome = Request.bEffectPreventedAfterLegalUse
		? EBattleBagUseOutcome::EffectPreventedAfterLegalUse
		: EBattleBagUseOutcome::Applied;
	OutResult.RejectionReason = EBattleBagUseRejectionReason::None;
	OutResult.CountAfter = ItemCount->Count;
	OutResult.bItemConsumed = true;
	OutResult.bActionConsumed = true;
	OutError = EBattleBagContractError::None;
	return true;
}

void FBattleBagOwnershipContract::ResetTurnQuotas()
{
	for (FBattleTrainerBagState& State : TrainerStates)
	{
		State.bBagActionAvailable = true;
	}
}

const FBattleTrainerBagState* FBattleBagOwnershipContract::FindTrainerState(
	const FTrainerId TrainerId) const
{
	return TrainerStates.FindByPredicate(
		[TrainerId](const FBattleTrainerBagState& State)
		{
			return State.TrainerId == TrainerId;
		});
}
