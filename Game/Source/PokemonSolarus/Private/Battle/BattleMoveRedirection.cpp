#include "BattleMoveRedirection.h"

#include "Battle/BattleState.h"

namespace BattleMoveRedirectionPrivate
{
	/** Canonical Follow Me-style redirect-rule priority; separate from Move.Priority. */
	constexpr int32 RegisterTargetRedirectionPriority = 1;

	bool IsDoubleFormat(const EBattleFormat Format)
	{
		return Format == EBattleFormat::Double
			|| Format == EBattleFormat::PartnerDouble;
	}

	bool IsLivingOccupant(
		const FBattleBattlerTarget& Target,
		const TConstArrayView<FBattleBattlerState> Battlers,
		const TConstArrayView<FBattleActivePositionState> ActivePositions)
	{
		const FBattleActivePositionState* Active = ActivePositions.FindByPredicate(
			[&Target](const FBattleActivePositionState& Candidate)
			{
				return Candidate.ActiveSlotId == Target.ActiveSlotId;
			});
		const FBattleBattlerState* Battler = Battlers.FindByPredicate(
			[&Target](const FBattleBattlerState& Candidate)
			{
				return Candidate.BattlerId == Target.BattlerId;
			});
		return Active != nullptr
			&& Battler != nullptr
			&& Active->bAvailable
			&& Active->BattlerId == Target.BattlerId
			&& Battler->CurrentHP > 0
			&& !Battler->bEgg
			&& !Battler->bFainted
			&& !Battler->bCaptured
			&& !Battler->bRemoved;
	}

	bool IsSupportedTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler
			|| TargetClass == EBattleTargetClass::RandomLegalOpponent
			|| TargetClass == EBattleTargetClass::SelectedOtherBattler;
	}

	bool IsLegalRedirectTarget(
		const EBattleTargetClass TargetClass,
		const FBattleBattlerTarget& User,
		const FBattleBattlerTarget& Candidate)
	{
		const bool bSelf = Candidate == User;
		const bool bSameSide = Candidate.ActiveSlotId.GetSide()
			== User.ActiveSlotId.GetSide();
		switch (TargetClass)
		{
		case EBattleTargetClass::SelectedAlly:
			return bSameSide && !bSelf;
		case EBattleTargetClass::SelectedOpponent:
		case EBattleTargetClass::RandomLegalOpponent:
			return !bSameSide;
		case EBattleTargetClass::AnySelectedBattler:
			return true;
		case EBattleTargetClass::SelectedOtherBattler:
			return !bSelf;
		default:
			return false;
		}
	}

	int32 StableActiveSlotOrder(const FActiveSlotId ActiveSlotId)
	{
		return ActiveSlotId.GetPosition() == EBattlePosition::Left ? 0 : 1;
	}

	struct FRedirectCandidate
	{
		FBattleBattlerTarget Target;
		int32 Priority = RegisterTargetRedirectionPriority;
		int32 EffectiveSpeed = 0;
		int32 ActiveSlotOrder = INDEX_NONE;
	};
}

using namespace BattleMoveRedirectionPrivate;

bool FBattleMoveRedirection::ContainsRegistrationEffect(
	const FBattleMoveDefinition& Move)
{
	return Move.Effects.ContainsByPredicate(
		[](const FBattleMoveEffectDescriptor& Effect)
		{
			return Effect.Kind == EBattleMoveEffectKind::RegisterTargetRedirection;
		});
}

bool FBattleMoveRedirection::IsRegistrationMoveDefinitionValid(
	const FBattleMoveDefinition& Move)
{
	if (Move.Category != EBattleMoveCategory::Status
		|| Move.Power != 0
		|| !Move.bAlwaysHits
		|| Move.Accuracy != 0
		|| Move.TargetClass != EBattleTargetClass::Self
		|| Move.Effects.Num() != 1)
	{
		return false;
	}

	const FBattleMoveEffectDescriptor& Effect = Move.Effects[0];
	return Effect.Order == 0
		&& Effect.Kind == EBattleMoveEffectKind::RegisterTargetRedirection
		&& Effect.Target == EBattleEffectTarget::User
		&& Effect.ChanceNumerator == 1
		&& Effect.ChanceDenominator == 1
		&& !Effect.ConditionId.IsValid()
		&& !Effect.ItemId.IsValid()
		&& Effect.HeldItemOperation == EBattleMoveHeldItemOperation::None
		&& Effect.Stat == static_cast<EBattleStat>(255)
		&& Effect.MagnitudeNumerator == 0
		&& Effect.MagnitudeDenominator == 1
		&& Effect.MinimumCount == 0
		&& Effect.MaximumCount == 0
		&& Effect.DurationTurns == 0
		&& Effect.LayerCount == 0
		&& Effect.Flags == EBattleMoveEffectFlags::None;
}

bool FBattleMoveRedirection::AreRegistrationsIdentical(
	const TConstArrayView<FBattleMoveRedirectionRegistration> Left,
	const TConstArrayView<FBattleMoveRedirectionRegistration> Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		if (Left[Index].TurnId != Right[Index].TurnId
			|| Left[Index].SourceActionId != Right[Index].SourceActionId
			|| Left[Index].Redirector != Right[Index].Redirector)
		{
			return false;
		}
	}
	return true;
}

bool FBattleMoveRedirection::IsRegistrationCollectionValid(
	const EBattleFormat Format,
	const FTurnId TurnId,
	const TConstArrayView<FBattleMoveRedirectionRegistration> Registrations,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions)
{
	if (Registrations.IsEmpty())
	{
		return true;
	}
	if (!IsDoubleFormat(Format) || !TurnId.IsValid())
	{
		return false;
	}
	for (int32 Index = 0; Index < Registrations.Num(); ++Index)
	{
		const FBattleMoveRedirectionRegistration& Registration = Registrations[Index];
		if (Registration.TurnId != TurnId
			|| !Registration.SourceActionId.IsValid()
			|| !Registration.Redirector.IsValid()
			|| !IsLivingOccupant(Registration.Redirector, Battlers, ActivePositions))
		{
			return false;
		}
		for (int32 OtherIndex = Index + 1; OtherIndex < Registrations.Num(); ++OtherIndex)
		{
			if (Registration.Redirector == Registrations[OtherIndex].Redirector)
			{
				return false;
			}
		}
	}
	return true;
}

EBattleMoveRedirectionRegistrationOutcome FBattleMoveRedirection::TryRegister(
	const EBattleFormat Format,
	const FTurnId TurnId,
	const FActionId SourceActionId,
	const FBattleBattlerTarget& Redirector,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations)
{
	if (!IsDoubleFormat(Format))
	{
		return Format == EBattleFormat::Single && InOutRegistrations.IsEmpty()
			? EBattleMoveRedirectionRegistrationOutcome::IneligibleFormat
			: EBattleMoveRedirectionRegistrationOutcome::InvalidState;
	}
	if (!TurnId.IsValid()
		|| !SourceActionId.IsValid()
		|| !Redirector.IsValid()
		|| !IsRegistrationCollectionValid(
			Format,
			TurnId,
			InOutRegistrations,
			Battlers,
			ActivePositions)
		|| !IsLivingOccupant(Redirector, Battlers, ActivePositions))
	{
		return EBattleMoveRedirectionRegistrationOutcome::InvalidState;
	}

	FBattleMoveRedirectionRegistration Replacement;
	Replacement.TurnId = TurnId;
	Replacement.SourceActionId = SourceActionId;
	Replacement.Redirector = Redirector;
	FBattleMoveRedirectionRegistration* Existing = InOutRegistrations.FindByPredicate(
		[&Redirector](const FBattleMoveRedirectionRegistration& Candidate)
		{
			return Candidate.Redirector == Redirector;
		});
	if (Existing != nullptr)
	{
		*Existing = Replacement;
	}
	else
	{
		InOutRegistrations.Add(MoveTemp(Replacement));
	}
	return IsRegistrationCollectionValid(
		Format,
		TurnId,
		InOutRegistrations,
		Battlers,
		ActivePositions)
		? EBattleMoveRedirectionRegistrationOutcome::Registered
		: EBattleMoveRedirectionRegistrationOutcome::InvalidState;
}

void FBattleMoveRedirection::RemoveForOccupant(
	TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations,
	const FBattleBattlerTarget& Occupant)
{
	InOutRegistrations.RemoveAll(
		[&Occupant](const FBattleMoveRedirectionRegistration& Registration)
		{
			return Registration.Redirector == Occupant;
		});
}

void FBattleMoveRedirection::Clear(
	TArray<FBattleMoveRedirectionRegistration>& InOutRegistrations)
{
	InOutRegistrations.Reset();
}

bool FBattleMoveRedirection::TrySelectWinningProposal(
	const EBattleFormat Format,
	const FTurnId TurnId,
	const EBattleTargetClass TargetClass,
	const FBattleBattlerTarget& User,
	const TConstArrayView<FBattleMoveRedirectionRegistration> Registrations,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	TFunctionRef<bool(const FBattleBattlerTarget&, int32&)> ResolveEffectiveSpeed,
	TArray<FBattleTargetRedirectionProposal>& OutProposals)
{
	OutProposals.Reset();
	if (!User.IsValid()
		|| !IsRegistrationCollectionValid(
			Format,
			TurnId,
			Registrations,
			Battlers,
			ActivePositions))
	{
		return false;
	}
	if (!IsDoubleFormat(Format)
		|| Registrations.IsEmpty()
		|| !IsSupportedTargetClass(TargetClass))
	{
		return true;
	}

	TArray<FRedirectCandidate> Candidates;
	for (const FBattleMoveRedirectionRegistration& Registration : Registrations)
	{
		const FBattleBattlerTarget& CandidateTarget = Registration.Redirector;
		if (CandidateTarget.ActiveSlotId.GetSide() == User.ActiveSlotId.GetSide()
			|| !IsLegalRedirectTarget(TargetClass, User, CandidateTarget))
		{
			continue;
		}
		FRedirectCandidate& Candidate = Candidates.AddDefaulted_GetRef();
		Candidate.Target = CandidateTarget;
		Candidate.ActiveSlotOrder = StableActiveSlotOrder(CandidateTarget.ActiveSlotId);
		if (!ResolveEffectiveSpeed(CandidateTarget, Candidate.EffectiveSpeed)
			|| Candidate.EffectiveSpeed <= 0)
		{
			return false;
		}
	}

	Candidates.Sort(
		[](const FRedirectCandidate& Left, const FRedirectCandidate& Right)
		{
			if (Left.Priority != Right.Priority)
			{
				return Left.Priority > Right.Priority;
			}
			if (Left.EffectiveSpeed != Right.EffectiveSpeed)
			{
				return Left.EffectiveSpeed > Right.EffectiveSpeed;
			}
			return Left.ActiveSlotOrder < Right.ActiveSlotOrder;
		});
	if (!Candidates.IsEmpty())
	{
		FBattleTargetRedirectionProposal& Proposal = OutProposals.AddDefaulted_GetRef();
		Proposal.ProposedTarget = Candidates[0].Target;
	}
	return true;
}
