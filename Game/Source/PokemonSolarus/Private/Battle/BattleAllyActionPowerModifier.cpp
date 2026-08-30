#include "BattleAllyActionPowerModifier.h"

#include "Battle/BattleState.h"

namespace BattleAllyActionPowerModifierPrivate
{
	constexpr int32 AllyActionPowerModifierPriority = 10;
	constexpr int32 TerrainPowerModifierPriority = 6;
	static_assert(
		AllyActionPowerModifierPriority > TerrainPowerModifierPriority,
		"Ally action power modifiers must be appended before terrain modifiers.");

	bool IsAllyModifierDoubleFormat(const EBattleFormat Format)
	{
		return Format == EBattleFormat::Double
			|| Format == EBattleFormat::PartnerDouble;
	}

	const FBattleBattlerState* FindLivingAllyModifierOccupant(
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
			&& !Battler->bRemoved
				? Battler
				: nullptr;
	}

	bool IsAllyModifierActionOwnedByTarget(
		const FBattleLockedActionState& Action,
		const FBattleBattlerTarget& Target)
	{
		return Action.Decision.GetActingBattlerId() == Target.BattlerId
			&& Action.OrderKey.ActingSlotId == Target.ActiveSlotId;
	}

	int32 CountExactAllyModifierBinding(
		const TConstArrayView<FBattleAllyActionPowerModifierRegistration> Registrations,
		const FActionId TargetActionId,
		const FBattleBattlerTarget& Target)
	{
		int32 Count = 0;
		for (const FBattleAllyActionPowerModifierRegistration& Candidate : Registrations)
		{
			if (Candidate.TargetActionId == TargetActionId
				&& Candidate.Target == Target)
			{
				++Count;
			}
		}
		return Count;
	}
}

bool FBattleAllyActionPowerModifier::ContainsRegistrationEffect(
	const FBattleMoveDefinition& Move)
{
	return Move.Effects.ContainsByPredicate(
		[](const FBattleMoveEffectDescriptor& Effect)
		{
			return Effect.Kind
				== EBattleMoveEffectKind::RegisterAllyActionPowerModifier;
		});
}

bool FBattleAllyActionPowerModifier::IsRegistrationMoveDefinitionValid(
	const FBattleMoveDefinition& Move)
{
	if (Move.Category != EBattleMoveCategory::Status
		|| Move.Power != 0
		|| !Move.bAlwaysHits
		|| Move.Accuracy != 0
		|| Move.TargetClass != EBattleTargetClass::SelectedAlly
		|| Move.Effects.Num() != 1)
	{
		return false;
	}

	const FBattleMoveEffectDescriptor& Effect = Move.Effects[0];
	int32 ModifierQ12 = 0;
	return Effect.Order == 0
		&& Effect.Kind == EBattleMoveEffectKind::RegisterAllyActionPowerModifier
		&& Effect.Target == EBattleEffectTarget::ResolvedTarget
		&& Effect.ChanceNumerator == 1
		&& Effect.ChanceDenominator == 1
		&& !Effect.ConditionId.IsValid()
		&& !Effect.ItemId.IsValid()
		&& Effect.HeldItemOperation == EBattleMoveHeldItemOperation::None
		&& Effect.Stat == static_cast<EBattleStat>(255)
		&& TryConvertMagnitudeToQ12(
			Effect.MagnitudeNumerator,
			Effect.MagnitudeDenominator,
			ModifierQ12)
		&& Effect.MinimumCount == 0
		&& Effect.MaximumCount == 0
		&& Effect.DurationTurns == 0
		&& Effect.LayerCount == 0
		&& Effect.Flags == EBattleMoveEffectFlags::None;
}

bool FBattleAllyActionPowerModifier::AreRegistrationsIdentical(
	const TConstArrayView<FBattleAllyActionPowerModifierRegistration> Left,
	const TConstArrayView<FBattleAllyActionPowerModifierRegistration> Right)
{
	if (Left.Num() != Right.Num())
	{
		return false;
	}
	for (int32 Index = 0; Index < Left.Num(); ++Index)
	{
		const FBattleAllyActionPowerModifierRegistration& L = Left[Index];
		const FBattleAllyActionPowerModifierRegistration& R = Right[Index];
		if (L.TurnId != R.TurnId
			|| L.SourceActionId != R.SourceActionId
			|| L.SourceMoveId != R.SourceMoveId
			|| L.TargetActionId != R.TargetActionId
			|| L.Target != R.Target
			|| L.MagnitudeNumerator != R.MagnitudeNumerator
			|| L.MagnitudeDenominator != R.MagnitudeDenominator)
		{
			return false;
		}
	}
	return true;
}

bool FBattleAllyActionPowerModifier::IsRegistrationCollectionValid(
	const EBattleFormat Format,
	const FTurnId TurnId,
	const TConstArrayView<FBattleAllyActionPowerModifierRegistration> Registrations,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	const TConstArrayView<FBattleLockedActionState> LockedActions)
{
	if (Registrations.IsEmpty())
	{
		return true;
	}
	if (!BattleAllyActionPowerModifierPrivate::IsAllyModifierDoubleFormat(Format)
		|| !TurnId.IsValid())
	{
		return false;
	}
	for (const FBattleAllyActionPowerModifierRegistration& Registration : Registrations)
	{
		int32 ModifierQ12 = 0;
		const FBattleBattlerState* Target =
			BattleAllyActionPowerModifierPrivate::FindLivingAllyModifierOccupant(
			Registration.Target,
			Battlers,
			ActivePositions);
		if (Registration.TurnId != TurnId
			|| !Registration.SourceActionId.IsValid()
			|| !Registration.SourceMoveId.IsValid()
			|| !Registration.Target.IsValid()
			|| Target == nullptr
			|| !TryConvertMagnitudeToQ12(
				Registration.MagnitudeNumerator,
				Registration.MagnitudeDenominator,
				ModifierQ12))
		{
			return false;
		}

		if (Registration.TargetActionId.IsValid())
		{
			const FBattleLockedActionState* Action = LockedActions.FindByPredicate(
				[&Registration](const FBattleLockedActionState& Candidate)
				{
					return Candidate.ActionId == Registration.TargetActionId;
				});
			if (Action == nullptr
				|| !BattleAllyActionPowerModifierPrivate::IsAllyModifierActionOwnedByTarget(
					*Action,
					Registration.Target)
				|| Action->Decision.GetActionKind() != EBattleActionKind::Fight
				|| Action->bFinished)
			{
				return false;
			}
		}
		else
		{
			if (Target->EnteredActiveOnTurnId != TurnId)
			{
				return false;
			}
		}
	}
	return true;
}

EBattleAllyActionPowerModifierRegistrationOutcome
FBattleAllyActionPowerModifier::TryRegister(
	const EBattleFormat Format,
	const FTurnId TurnId,
	const FActionId SourceActionId,
	const FMoveId& SourceMoveId,
	const FBattleBattlerTarget& Source,
	const FBattleBattlerTarget& Target,
	const int32 MagnitudeNumerator,
	const int32 MagnitudeDenominator,
	const TConstArrayView<FBattleBattlerState> Battlers,
	const TConstArrayView<FBattleActivePositionState> ActivePositions,
	const TConstArrayView<FBattleLockedActionState> LockedActions,
	TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations,
	int32& OutBindingCountBefore,
	int32& OutBindingCountAfter)
{
	OutBindingCountBefore = 0;
	OutBindingCountAfter = 0;
	if (!BattleAllyActionPowerModifierPrivate::IsAllyModifierDoubleFormat(Format))
	{
		return Format == EBattleFormat::Single && InOutRegistrations.IsEmpty()
			? EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleFormat
			: EBattleAllyActionPowerModifierRegistrationOutcome::InvalidState;
	}

	int32 ModifierQ12 = 0;
	const FBattleBattlerState* SourceBattler =
		BattleAllyActionPowerModifierPrivate::FindLivingAllyModifierOccupant(
		Source,
		Battlers,
		ActivePositions);
	const FBattleBattlerState* TargetBattler =
		BattleAllyActionPowerModifierPrivate::FindLivingAllyModifierOccupant(
		Target,
		Battlers,
		ActivePositions);
	const FBattleLockedActionState* SourceAction = LockedActions.FindByPredicate(
		[SourceActionId](const FBattleLockedActionState& Candidate)
		{
			return Candidate.ActionId == SourceActionId;
		});
	if (!TurnId.IsValid()
		|| !SourceActionId.IsValid()
		|| !SourceMoveId.IsValid()
		|| !Source.IsValid()
		|| !Target.IsValid()
		|| SourceBattler == nullptr
		|| SourceAction == nullptr
		|| !BattleAllyActionPowerModifierPrivate::IsAllyModifierActionOwnedByTarget(
			*SourceAction,
			Source)
		|| SourceAction->Decision.GetActionKind() != EBattleActionKind::Fight
		|| SourceAction->Decision.GetMoveId() != SourceMoveId
		|| !SourceAction->bStarted
		|| SourceAction->bFinished
		|| !TryConvertMagnitudeToQ12(
			MagnitudeNumerator,
			MagnitudeDenominator,
			ModifierQ12)
		|| !IsRegistrationCollectionValid(
			Format,
			TurnId,
			InOutRegistrations,
			Battlers,
			ActivePositions,
			LockedActions))
	{
		return EBattleAllyActionPowerModifierRegistrationOutcome::InvalidState;
	}
	if (TargetBattler == nullptr
		|| Source == Target
		|| Source.ActiveSlotId.GetSide() != Target.ActiveSlotId.GetSide())
	{
		return EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget;
	}

	FActionId TargetActionId;
	bool bTargetAlreadyActed = false;
	for (const FBattleLockedActionState& Action : LockedActions)
	{
		if (!BattleAllyActionPowerModifierPrivate::IsAllyModifierActionOwnedByTarget(
				Action,
				Target))
		{
			continue;
		}
		if (Action.Decision.GetActionKind() == EBattleActionKind::Fight
			&& !Action.bStarted
			&& !Action.bFinished)
		{
			TargetActionId = Action.ActionId;
			break;
		}
		bTargetAlreadyActed |= Action.bStarted || Action.bFinished;
	}
	if (!TargetActionId.IsValid()
		&& (bTargetAlreadyActed || TargetBattler->EnteredActiveOnTurnId != TurnId))
	{
		return EBattleAllyActionPowerModifierRegistrationOutcome::IneligibleTarget;
	}

	OutBindingCountBefore =
		BattleAllyActionPowerModifierPrivate::CountExactAllyModifierBinding(
		InOutRegistrations,
		TargetActionId,
		Target);
	TArray<FBattleAllyActionPowerModifierRegistration> Candidate = InOutRegistrations;
	FBattleAllyActionPowerModifierRegistration& Registration =
		Candidate.AddDefaulted_GetRef();
	Registration.TurnId = TurnId;
	Registration.SourceActionId = SourceActionId;
	Registration.SourceMoveId = SourceMoveId;
	Registration.TargetActionId = TargetActionId;
	Registration.Target = Target;
	Registration.MagnitudeNumerator = MagnitudeNumerator;
	Registration.MagnitudeDenominator = MagnitudeDenominator;
	if (!IsRegistrationCollectionValid(
			Format,
			TurnId,
			Candidate,
			Battlers,
			ActivePositions,
			LockedActions))
	{
		OutBindingCountBefore = 0;
		return EBattleAllyActionPowerModifierRegistrationOutcome::InvalidState;
	}

	OutBindingCountAfter = OutBindingCountBefore + 1;
	InOutRegistrations = MoveTemp(Candidate);
	return EBattleAllyActionPowerModifierRegistrationOutcome::Registered;
}

bool FBattleAllyActionPowerModifier::TryConvertMagnitudeToQ12(
	const int32 MagnitudeNumerator,
	const int32 MagnitudeDenominator,
	int32& OutModifierQ12)
{
	OutModifierQ12 = 0;
	if (MagnitudeNumerator <= 0 || MagnitudeDenominator <= 0)
	{
		return false;
	}
	const int64 Product = static_cast<int64>(MagnitudeNumerator)
		* FBattleFinalDamageCalculator::Q12Neutral;
	if (Product <= 0 || Product % MagnitudeDenominator != 0)
	{
		return false;
	}
	const int64 Quotient = Product / MagnitudeDenominator;
	if (Quotient <= 0
		|| Quotient > FBattleFinalDamageCalculator::MaximumFinalModifierQ12)
	{
		return false;
	}
	OutModifierQ12 = static_cast<int32>(Quotient);
	return true;
}

bool FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
	const FTurnId TurnId,
	const FActionId ActionId,
	const FBattleBattlerTarget& DamageUser,
	const bool bActualDamageBuild,
	const TConstArrayView<FBattleAllyActionPowerModifierRegistration> Registrations,
	TArray<FBattleDamageModifier>& InOutPowerModifiers)
{
	if (!bActualDamageBuild)
	{
		return true;
	}
	if (!TurnId.IsValid() || !ActionId.IsValid() || !DamageUser.IsValid())
	{
		return false;
	}

	TArray<FBattleDamageModifier> Matching;
	for (const FBattleAllyActionPowerModifierRegistration& Registration : Registrations)
	{
		if (Registration.TurnId != TurnId
			|| Registration.TargetActionId != ActionId
			|| Registration.Target != DamageUser)
		{
			continue;
		}
		int32 ModifierQ12 = 0;
		if (!Registration.SourceMoveId.IsValid()
			|| !TryConvertMagnitudeToQ12(
				Registration.MagnitudeNumerator,
				Registration.MagnitudeDenominator,
				ModifierQ12))
		{
			return false;
		}
		Matching.Add({
			Registration.SourceMoveId.GetDefinitionId(),
			ModifierQ12,
			false});
	}
	InOutPowerModifiers.Append(MoveTemp(Matching));
	return true;
}

void FBattleAllyActionPowerModifier::RemoveForAction(
	TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations,
	const FActionId ActionId)
{
	if (!ActionId.IsValid())
	{
		return;
	}
	InOutRegistrations.RemoveAll(
		[ActionId](const FBattleAllyActionPowerModifierRegistration& Registration)
		{
			return Registration.TargetActionId == ActionId;
		});
}

void FBattleAllyActionPowerModifier::RemoveForOccupant(
	TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations,
	const FBattleBattlerTarget& Occupant)
{
	InOutRegistrations.RemoveAll(
		[&Occupant](const FBattleAllyActionPowerModifierRegistration& Registration)
		{
			return Registration.Target == Occupant;
		});
}

void FBattleAllyActionPowerModifier::Clear(
	TArray<FBattleAllyActionPowerModifierRegistration>& InOutRegistrations)
{
	InOutRegistrations.Reset();
}
