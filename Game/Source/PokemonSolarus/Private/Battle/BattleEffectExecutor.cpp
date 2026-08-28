#include "Battle/BattleEffectExecutor.h"

#include "Battle/BattleAbility.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Math/NumericLimits.h"

namespace BattleEffectExecutorPrivate
{
	constexpr uint32 KnownMoveFlags =
		static_cast<uint32>(EBattleMoveFlags::MakesContact)
		| static_cast<uint32>(EBattleMoveFlags::BlockedByProtect)
		| static_cast<uint32>(EBattleMoveFlags::BypassesProtect)
		| static_cast<uint32>(EBattleMoveFlags::BypassesSubstitute)
		| static_cast<uint32>(EBattleMoveFlags::ThawsUser)
		| static_cast<uint32>(EBattleMoveFlags::ThawsTarget)
		| static_cast<uint32>(EBattleMoveFlags::Unencoreable)
		| static_cast<uint32>(EBattleMoveFlags::AlwaysCritical)
		| static_cast<uint32>(EBattleMoveFlags::NeverCritical)
		| static_cast<uint32>(EBattleMoveFlags::UsesPerHitAccuracy)
		| static_cast<uint32>(EBattleMoveFlags::TypelessDamage)
		| static_cast<uint32>(EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget)
		| static_cast<uint32>(EBattleMoveFlags::DoublesPowerAgainstAirborneSemiInvulnerableTarget)
		| static_cast<uint32>(EBattleMoveFlags::BreaksProtection)
		| static_cast<uint32>(EBattleMoveFlags::BypassesSideProtection)
		| static_cast<uint32>(EBattleMoveFlags::ReducedByGrassyTerrain);
	constexpr uint32 KnownEffectFlags =
		static_cast<uint32>(EBattleMoveEffectFlags::BypassesSubstitute)
		| static_cast<uint32>(EBattleMoveEffectFlags::UsesActualDamage)
		| static_cast<uint32>(EBattleMoveEffectFlags::MinimumOne)
		| static_cast<uint32>(EBattleMoveEffectFlags::StopOnFaint)
		| static_cast<uint32>(EBattleMoveEffectFlags::PerHit);

	FDefinitionId MakeRuleId(const TCHAR* Name)
	{
		FDefinitionId Result;
		const bool bCreated = FDefinitionId::TryCreate(FName(Name), Result);
		check(bCreated);
		return Result;
	}

	FBattleRandomContext MakeRandomContext(
		const FBattleEffectExecutionRequest& Request,
		const FDefinitionId& Purpose)
	{
		FBattleRandomContext Context;
		Context.BattleId = Request.BattleId;
		Context.TurnId = Request.TurnId;
		Context.ActionId = Request.ActionId;
		Context.ResolutionId = Request.ResolutionId;
		Context.RulePurpose = Purpose;
		return Context;
	}

	bool IsKnownSide(const EBattleSide Side)
	{
		return Side == EBattleSide::Player || Side == EBattleSide::Opponent;
	}

	bool IsKnownMoveCategory(const EBattleMoveCategory Category)
	{
		return Category == EBattleMoveCategory::Physical
			|| Category == EBattleMoveCategory::Special
			|| Category == EBattleMoveCategory::Status;
	}

	bool AreEffectDescriptorsIdentical(
		const FBattleMoveEffectDescriptor& Left,
		const FBattleMoveEffectDescriptor& Right)
	{
		return Left.Order == Right.Order
			&& Left.Kind == Right.Kind
			&& Left.Target == Right.Target
			&& Left.ConditionId == Right.ConditionId
			&& Left.ItemId == Right.ItemId
			&& Left.Stat == Right.Stat
			&& Left.ChanceNumerator == Right.ChanceNumerator
			&& Left.ChanceDenominator == Right.ChanceDenominator
			&& Left.MagnitudeNumerator == Right.MagnitudeNumerator
			&& Left.MagnitudeDenominator == Right.MagnitudeDenominator
			&& Left.MinimumCount == Right.MinimumCount
			&& Left.MaximumCount == Right.MaximumCount
			&& Left.DurationTurns == Right.DurationTurns
			&& Left.LayerCount == Right.LayerCount
			&& Left.Flags == Right.Flags;
	}

	bool AreMoveDefinitionsIdentical(
		const FBattleMoveDefinition& Left,
		const FBattleMoveDefinition& Right)
	{
		if (Left.Id != Right.Id
			|| Left.Type != Right.Type
			|| Left.Category != Right.Category
			|| Left.Power != Right.Power
			|| Left.bAlwaysHits != Right.bAlwaysHits
			|| Left.Accuracy != Right.Accuracy
			|| Left.bUsesPP != Right.bUsesPP
			|| Left.BasePP != Right.BasePP
			|| Left.bAllowsPPBoosts != Right.bAllowsPPBoosts
			|| Left.Priority != Right.Priority
			|| Left.TargetClass != Right.TargetClass
			|| Left.Flags != Right.Flags
			|| Left.Effects.Num() != Right.Effects.Num())
		{
			return false;
		}
		for (int32 Index = 0; Index < Left.Effects.Num(); ++Index)
		{
			if (!AreEffectDescriptorsIdentical(Left.Effects[Index], Right.Effects[Index]))
			{
				return false;
			}
		}
		return true;
	}

	bool IsKnownEffectKind(const EBattleMoveEffectKind Kind)
	{
		return static_cast<uint8>(Kind)
			<= static_cast<uint8>(EBattleMoveEffectKind::RemoveCondition);
	}

	bool IsKnownEffectTarget(const EBattleEffectTarget Target)
	{
		return static_cast<uint8>(Target)
			<= static_cast<uint8>(EBattleEffectTarget::Field);
	}

	bool IsKnownStat(const EBattleStat Stat)
	{
		return static_cast<uint8>(Stat) <= static_cast<uint8>(EBattleStat::Evasion);
	}

	bool IsBattlerTargetClass(const EBattleTargetClass TargetClass)
	{
		return TargetClass == EBattleTargetClass::Self
			|| TargetClass == EBattleTargetClass::SelectedAlly
			|| TargetClass == EBattleTargetClass::SelectedOpponent
			|| TargetClass == EBattleTargetClass::AnySelectedBattler
			|| TargetClass == EBattleTargetClass::RandomLegalOpponent
			|| TargetClass == EBattleTargetClass::FixedSpreadSet;
	}

	bool IsBattlerEffectTargetCompatible(
		const EBattleTargetClass MoveTargetClass,
		const EBattleEffectTarget EffectTarget)
	{
		return EffectTarget == EBattleEffectTarget::User
			|| ((EffectTarget == EBattleEffectTarget::ResolvedTarget
					|| EffectTarget == EBattleEffectTarget::AllResolvedTargets)
				&& IsBattlerTargetClass(MoveTargetClass));
	}

	bool IsSideEffectTarget(const EBattleEffectTarget Target)
	{
		return Target == EBattleEffectTarget::UserSide
			|| Target == EBattleEffectTarget::TargetSide
			|| Target == EBattleEffectTarget::BothSides;
	}

	bool RequiresConditionReference(const EBattleMoveEffectKind Kind)
	{
		return Kind == EBattleMoveEffectKind::ApplyCondition
			|| Kind == EBattleMoveEffectKind::SetFieldCondition
			|| Kind == EBattleMoveEffectKind::SetSideCondition
			|| Kind == EBattleMoveEffectKind::Charge
			|| Kind == EBattleMoveEffectKind::Recharge
			|| Kind == EBattleMoveEffectKind::Protect
			|| Kind == EBattleMoveEffectKind::SemiInvulnerability
			|| Kind == EBattleMoveEffectKind::RemoveCondition;
	}

	bool IsRemovalTargetCompatible(
		const EBattleEffectTarget Target,
		const EBattleConditionKind ConditionKind)
	{
		switch (ConditionKind)
		{
		case EBattleConditionKind::MajorStatus:
		case EBattleConditionKind::Volatile:
			return Target == EBattleEffectTarget::User
				|| Target == EBattleEffectTarget::ResolvedTarget
				|| Target == EBattleEffectTarget::AllResolvedTargets;
		case EBattleConditionKind::Weather:
		case EBattleConditionKind::Terrain:
		case EBattleConditionKind::Room:
			return Target == EBattleEffectTarget::Field;
		case EBattleConditionKind::Hazard:
		case EBattleConditionKind::Screen:
		case EBattleConditionKind::SideCondition:
			return IsSideEffectTarget(Target);
		default:
			return false;
		}
	}

	bool IsRemovalTargetShapeCompatible(
		const EBattleTargetClass MoveTargetClass,
		const EBattleEffectTarget EffectTarget)
	{
		switch (EffectTarget)
		{
		case EBattleEffectTarget::User:
		case EBattleEffectTarget::UserSide:
		case EBattleEffectTarget::BothSides:
		case EBattleEffectTarget::Field:
			return true;
		case EBattleEffectTarget::ResolvedTarget:
		case EBattleEffectTarget::AllResolvedTargets:
			return IsBattlerTargetClass(MoveTargetClass);
		case EBattleEffectTarget::TargetSide:
			return MoveTargetClass != EBattleTargetClass::Field;
		default:
			return false;
		}
	}

	bool IsEffectTargetShapeCompatible(
		const EBattleTargetClass MoveTargetClass,
		const EBattleMoveEffectKind EffectKind,
		const EBattleEffectTarget EffectTarget)
	{
		switch (EffectKind)
		{
		case EBattleMoveEffectKind::Damage:
			return IsBattlerTargetClass(MoveTargetClass)
				&& (EffectTarget == EBattleEffectTarget::ResolvedTarget
					|| EffectTarget == EBattleEffectTarget::AllResolvedTargets);
		case EBattleMoveEffectKind::ApplyCondition:
		case EBattleMoveEffectKind::ModifyStatStage:
		case EBattleMoveEffectKind::Heal:
		case EBattleMoveEffectKind::Switch:
		case EBattleMoveEffectKind::ChangeItem:
			return IsBattlerEffectTargetCompatible(MoveTargetClass, EffectTarget);
		case EBattleMoveEffectKind::Drain:
		case EBattleMoveEffectKind::Recoil:
		case EBattleMoveEffectKind::Charge:
		case EBattleMoveEffectKind::Recharge:
		case EBattleMoveEffectKind::Protect:
		case EBattleMoveEffectKind::SemiInvulnerability:
			return EffectTarget == EBattleEffectTarget::User;
		case EBattleMoveEffectKind::SetFieldCondition:
			return EffectTarget == EBattleEffectTarget::Field
				&& MoveTargetClass == EBattleTargetClass::Field;
		case EBattleMoveEffectKind::SetSideCondition:
			return IsSideEffectTarget(EffectTarget)
				&& (EffectTarget != EBattleEffectTarget::TargetSide
					|| MoveTargetClass != EBattleTargetClass::Field);
		case EBattleMoveEffectKind::RemoveCondition:
			return IsRemovalTargetShapeCompatible(MoveTargetClass, EffectTarget);
		case EBattleMoveEffectKind::MultiHit:
			// Its selector is checked against the sole Damage descriptor below.
			return true;
		default:
			return false;
		}
	}

	bool IsKnownOutcome(const EBattleEffectExecutionOutcome Outcome)
	{
		return Outcome == EBattleEffectExecutionOutcome::Applied
			|| Outcome == EBattleEffectExecutionOutcome::ChanceFailed
			|| Outcome == EBattleEffectExecutionOutcome::Unreachable
			|| Outcome == EBattleEffectExecutionOutcome::Protected
			|| Outcome == EBattleEffectExecutionOutcome::Immune
			|| Outcome == EBattleEffectExecutionOutcome::Blocked
			|| Outcome == EBattleEffectExecutionOutcome::Failed
			|| Outcome == EBattleEffectExecutionOutcome::Capped
			|| Outcome == EBattleEffectExecutionOutcome::Prevented
			|| Outcome == EBattleEffectExecutionOutcome::Deferred;
	}

	EBattleEventType EventTypeForOutcome(const EBattleEffectExecutionOutcome Outcome)
	{
		switch (Outcome)
		{
		case EBattleEffectExecutionOutcome::Unreachable:
			return EBattleEventType::Unreachable;
		case EBattleEffectExecutionOutcome::Protected:
			return EBattleEventType::Protected;
		case EBattleEffectExecutionOutcome::Immune:
			return EBattleEventType::Immunity;
		case EBattleEffectExecutionOutcome::Blocked:
			return EBattleEventType::EffectBlocked;
		case EBattleEffectExecutionOutcome::Failed:
			return EBattleEventType::EffectFailed;
		case EBattleEffectExecutionOutcome::Capped:
			return EBattleEventType::EffectCapped;
		case EBattleEffectExecutionOutcome::Prevented:
			return EBattleEventType::EffectPrevented;
		case EBattleEffectExecutionOutcome::Deferred:
			return EBattleEventType::EffectDeferred;
		default:
			return EBattleEventType::EffectFailed;
		}
	}

	bool TryCreateUserTarget(
		const FBattleEffectExecutionRequest& Request,
		FBattleResolvedTarget& OutTarget)
	{
		FBattleBattlerTarget User;
		User.ActiveSlotId = Request.UserSlotId;
		User.BattlerId = Request.UserBattlerId;
		return FBattleResolvedTarget::TryCreateBattler(User, OutTarget);
	}

	bool TryCreateSideTarget(const EBattleSide Side, FBattleResolvedTarget& OutTarget)
	{
		return IsKnownSide(Side) && FBattleResolvedTarget::TryCreateSide(Side, OutTarget);
	}

	bool TryGetTargetSide(
		const FBattleResolvedTarget& Target,
		EBattleSide& OutSide)
	{
		OutSide = EBattleSide::Player;
		if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
		{
			OutSide = Target.GetBattler().ActiveSlotId.GetSide();
			return Target.GetBattler().ActiveSlotId.IsValid();
		}
		if (Target.GetKind() == EBattleResolvedTargetKind::Side)
		{
			OutSide = Target.GetSide();
			return IsKnownSide(OutSide);
		}
		return false;
	}

	bool TryExpandEffectTargets(
		const FBattleEffectExecutionRequest& Request,
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& ReachedTarget,
		TArray<FBattleResolvedTarget>& OutTargets)
	{
		OutTargets.Reset();
		switch (Effect.Target)
		{
		case EBattleEffectTarget::User:
		{
			FBattleResolvedTarget User;
			if (!TryCreateUserTarget(Request, User))
			{
				return false;
			}
			OutTargets.Add(MoveTemp(User));
			return true;
		}
		case EBattleEffectTarget::ResolvedTarget:
		case EBattleEffectTarget::AllResolvedTargets:
			OutTargets.Add(ReachedTarget);
			return true;
		case EBattleEffectTarget::UserSide:
		{
			FBattleResolvedTarget Side;
			if (!TryCreateSideTarget(Request.UserSlotId.GetSide(), Side))
			{
				return false;
			}
			OutTargets.Add(MoveTemp(Side));
			return true;
		}
		case EBattleEffectTarget::TargetSide:
		{
			EBattleSide TargetSide = EBattleSide::Player;
			FBattleResolvedTarget Side;
			if (!TryGetTargetSide(ReachedTarget, TargetSide)
				|| !TryCreateSideTarget(TargetSide, Side))
			{
				return false;
			}
			OutTargets.Add(MoveTemp(Side));
			return true;
		}
		case EBattleEffectTarget::BothSides:
		{
			if (Request.Move->TargetClass == EBattleTargetClass::BothSides
				&& ReachedTarget.GetKind() == EBattleResolvedTargetKind::Side)
			{
				OutTargets.Add(ReachedTarget);
				return true;
			}
			FBattleResolvedTarget PlayerSide;
			FBattleResolvedTarget OpponentSide;
			if (!TryCreateSideTarget(EBattleSide::Player, PlayerSide)
				|| !TryCreateSideTarget(EBattleSide::Opponent, OpponentSide))
			{
				return false;
			}
			OutTargets.Add(MoveTemp(PlayerSide));
			OutTargets.Add(MoveTemp(OpponentSide));
			return true;
		}
		case EBattleEffectTarget::Field:
			OutTargets.Add(FBattleResolvedTarget::CreateField());
			return true;
		default:
			return false;
		}
	}

	bool IsActionScopedOrdinaryEffect(const FBattleMoveEffectDescriptor& Effect)
	{
		return !EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::PerHit)
			&& Effect.Target != EBattleEffectTarget::ResolvedTarget
			&& Effect.Target != EBattleEffectTarget::AllResolvedTargets;
	}

	bool AreResolvedTargetsValid(const FBattleEffectExecutionRequest& Request)
	{
		if (Request.Targets.IsEmpty())
		{
			return false;
		}
		for (int32 Index = 0; Index < Request.Targets.Num(); ++Index)
		{
			const FBattleResolvedTarget& Target = Request.Targets[Index];
			if (!Target.IsValid())
			{
				return false;
			}
			for (int32 EarlierIndex = 0; EarlierIndex < Index; ++EarlierIndex)
			{
				if (Target == Request.Targets[EarlierIndex])
				{
					return false;
				}
			}
		}

		auto AllAre = [&Request](const EBattleResolvedTargetKind Kind)
		{
			return Request.Targets.ContainsByPredicate(
				[Kind](const FBattleResolvedTarget& Target)
				{
					return Target.GetKind() != Kind;
				}) == false;
		};

		switch (Request.Move->TargetClass)
		{
		case EBattleTargetClass::Self:
			return Request.Targets.Num() == 1
				&& Request.Targets[0].GetKind() == EBattleResolvedTargetKind::Battler
				&& Request.Targets[0].GetBattler().BattlerId == Request.UserBattlerId
				&& Request.Targets[0].GetBattler().ActiveSlotId == Request.UserSlotId;
		case EBattleTargetClass::SelectedAlly:
		case EBattleTargetClass::SelectedOpponent:
		case EBattleTargetClass::AnySelectedBattler:
		case EBattleTargetClass::RandomLegalOpponent:
			return Request.Targets.Num() == 1 && AllAre(EBattleResolvedTargetKind::Battler);
		case EBattleTargetClass::FixedSpreadSet:
			return Request.Targets.Num() <= 3 && AllAre(EBattleResolvedTargetKind::Battler);
		case EBattleTargetClass::UserSide:
			return Request.Targets.Num() == 1
				&& Request.Targets[0].GetKind() == EBattleResolvedTargetKind::Side
				&& Request.Targets[0].GetSide() == Request.UserSlotId.GetSide();
		case EBattleTargetClass::OpponentSide:
			return Request.Targets.Num() == 1
				&& Request.Targets[0].GetKind() == EBattleResolvedTargetKind::Side
				&& Request.Targets[0].GetSide() != Request.UserSlotId.GetSide();
		case EBattleTargetClass::BothSides:
			return Request.Targets.Num() == 2
				&& Request.Targets[0].GetKind() == EBattleResolvedTargetKind::Side
				&& Request.Targets[1].GetKind() == EBattleResolvedTargetKind::Side
				&& Request.Targets[0].GetSide() == EBattleSide::Player
				&& Request.Targets[1].GetSide() == EBattleSide::Opponent;
		case EBattleTargetClass::Field:
			return Request.Targets.Num() == 1
				&& Request.Targets[0].GetKind() == EBattleResolvedTargetKind::Field;
		default:
			return false;
		}
	}

	bool ValidateMoveDefinition(
		const FBattleEffectExecutionRequest& Request,
		const FBattleMoveEffectDescriptor*& OutDamage,
		const FBattleMoveEffectDescriptor*& OutMultiHit)
	{
		OutDamage = nullptr;
		OutMultiHit = nullptr;
		const FBattleMoveDefinition& Move = *Request.Move;
		const bool bTypeless = EnumHasAllFlags(Move.Flags, EBattleMoveFlags::TypelessDamage);
		const bool bDamaging = Move.Category == EBattleMoveCategory::Physical
			|| Move.Category == EBattleMoveCategory::Special;
		if (!Move.Id.IsValid()
			|| !IsKnownMoveCategory(Move.Category)
			|| static_cast<uint8>(Move.TargetClass)
				> static_cast<uint8>(EBattleTargetClass::FixedSpreadSet)
			|| (static_cast<uint32>(Move.Flags) & ~KnownMoveFlags) != 0
			|| (EnumHasAllFlags(Move.Flags, EBattleMoveFlags::AlwaysCritical)
				&& EnumHasAllFlags(Move.Flags, EBattleMoveFlags::NeverCritical))
			|| (!bTypeless && !FBattleTypeChart::IsKnownType(Move.Type))
			|| (bTypeless && Move.Type != EPokemonType::Invalid)
			|| (bDamaging && (Move.Power < 1 || Move.Power > 1000))
			|| (!bDamaging && Move.Power != 0)
			|| (Move.bAlwaysHits ? Move.Accuracy != 0 : Move.Accuracy < 1 || Move.Accuracy > 100)
			|| (Move.bUsesPP && (Move.BasePP < 1 || Move.BasePP > 64))
			|| (!Move.bUsesPP && (Move.BasePP != 0 || Move.bAllowsPPBoosts))
			|| Move.Priority < -7
			|| Move.Priority > 5
			|| Move.Effects.IsEmpty())
		{
			return false;
		}

		int32 DamageCount = 0;
		int32 MultiHitCount = 0;
		int32 ChargeCount = 0;
		int32 SemiInvulnerabilityCount = 0;
		const FBattleMoveEffectDescriptor* ChargeEffect = nullptr;
		const FBattleMoveEffectDescriptor* SemiInvulnerabilityEffect = nullptr;
		for (int32 Index = 0; Index < Move.Effects.Num(); ++Index)
		{
			const FBattleMoveEffectDescriptor& Effect = Move.Effects[Index];
			const bool bPrimary = Effect.ChanceNumerator == 1 && Effect.ChanceDenominator == 1;
			const bool bSecondary = Effect.ChanceNumerator >= 1
				&& Effect.ChanceNumerator <= 100
				&& Effect.ChanceDenominator == 100;
			if (!IsKnownEffectKind(Effect.Kind)
				|| !IsKnownEffectTarget(Effect.Target)
				|| Effect.Order < 0
				|| (Index > 0 && Effect.Order <= Move.Effects[Index - 1].Order)
				|| (!bPrimary && !bSecondary)
				|| Effect.MagnitudeDenominator <= 0
				|| Effect.MinimumCount < 0 || Effect.MinimumCount > 255
				|| Effect.MaximumCount < 0 || Effect.MaximumCount > 255
				|| Effect.DurationTurns < 0 || Effect.DurationTurns > 255
				|| Effect.LayerCount < 0 || Effect.LayerCount > 3
				|| (static_cast<uint32>(Effect.Flags) & ~KnownEffectFlags) != 0)
			{
				return false;
			}
			const bool bRequiresCondition = RequiresConditionReference(Effect.Kind);
			if (bRequiresCondition != Effect.ConditionId.IsValid()
				|| ((Effect.Kind == EBattleMoveEffectKind::ChangeItem)
					!= Effect.ItemId.IsValid())
				|| (Effect.Kind != EBattleMoveEffectKind::ModifyStatStage
					&& IsKnownStat(Effect.Stat))
				|| !IsEffectTargetShapeCompatible(Move.TargetClass, Effect.Kind, Effect.Target))
			{
				return false;
			}

			if (Effect.Kind == EBattleMoveEffectKind::Damage)
			{
				++DamageCount;
				OutDamage = &Effect;
				if (!bPrimary)
				{
					return false;
				}
			}
			else if (Effect.Kind == EBattleMoveEffectKind::MultiHit)
			{
				++MultiHitCount;
				OutMultiHit = &Effect;
				const bool bFixed = Effect.MinimumCount == Effect.MaximumCount
					&& Effect.MinimumCount >= 2 && Effect.MinimumCount <= 5;
				const bool bRanged = Effect.MinimumCount == 2 && Effect.MaximumCount == 5;
				if (!bPrimary || (!bFixed && !bRanged))
				{
					return false;
				}
			}
			else if (Effect.Kind == EBattleMoveEffectKind::Charge)
			{
				++ChargeCount;
				ChargeEffect = &Effect;
				if (!bPrimary
					|| Effect.ConditionId != FBattleVolatileRules::GetChargingId())
				{
					return false;
				}
			}
			else if (Effect.Kind == EBattleMoveEffectKind::SemiInvulnerability)
			{
				++SemiInvulnerabilityCount;
				SemiInvulnerabilityEffect = &Effect;
				if (!bPrimary
					|| Effect.ConditionId
						!= FBattleVolatileRules::GetFlySemiInvulnerableId())
				{
					return false;
				}
			}
			else if ((Effect.Kind == EBattleMoveEffectKind::Heal
					|| Effect.Kind == EBattleMoveEffectKind::Drain
					|| Effect.Kind == EBattleMoveEffectKind::Recoil)
				&& (Effect.MagnitudeNumerator <= 0
					|| (Effect.MagnitudeDenominator > 1
						&& Effect.MagnitudeNumerator > Effect.MagnitudeDenominator)))
			{
				return false;
			}
			else if (Effect.Kind == EBattleMoveEffectKind::ModifyStatStage
				&& (!IsKnownStat(Effect.Stat)
					|| Effect.MagnitudeNumerator == 0
					|| Effect.MagnitudeDenominator != 1))
			{
				return false;
			}
			const bool bPerHit = EnumHasAllFlags(
				Effect.Flags,
				EBattleMoveEffectFlags::PerHit);
			const bool bCoreOrLinked = Effect.Kind == EBattleMoveEffectKind::Damage
				|| Effect.Kind == EBattleMoveEffectKind::MultiHit
				|| Effect.Kind == EBattleMoveEffectKind::Drain
				|| Effect.Kind == EBattleMoveEffectKind::Recoil;
			if (bPerHit
				&& (Move.Category == EBattleMoveCategory::Status
					|| bCoreOrLinked
					|| bPrimary))
			{
				return false;
			}
		}

		if ((bDamaging && DamageCount != 1)
			|| (!bDamaging && DamageCount != 0)
			|| MultiHitCount > 1
			|| ChargeCount > 1
			|| SemiInvulnerabilityCount > 1)
		{
			return false;
		}
		if (ChargeEffect != nullptr
			&& (OutDamage == nullptr || ChargeEffect->Order >= OutDamage->Order))
		{
			return false;
		}
		if (SemiInvulnerabilityEffect != nullptr
			&& (ChargeEffect == nullptr
				|| OutDamage == nullptr
				|| SemiInvulnerabilityEffect->Order <= ChargeEffect->Order
				|| SemiInvulnerabilityEffect->Order >= OutDamage->Order))
		{
			return false;
		}
		if (OutMultiHit != nullptr
			&& (OutDamage == nullptr
				|| OutMultiHit->Order >= OutDamage->Order
				|| OutMultiHit->Target != OutDamage->Target
				|| Move.TargetClass == EBattleTargetClass::FixedSpreadSet))
		{
			return false;
		}
		return AreResolvedTargetsValid(Request);
	}

	bool TryAddTargetedEvent(
		IBattleEffectExecutionContext& Context,
		FBattleEffectExecutionResult& Result,
		const EBattleEventType Type,
		const EBattleEffectExecutionOutcome Outcome,
		const FBattleResolvedTarget& Target,
		const TOptional<int64>& NumericBefore = TOptional<int64>(),
		const TOptional<int64>& NumericAfter = TOptional<int64>(),
		const TOptional<int64>& NumericDelta = TOptional<int64>(),
		const TOptional<uint16>& HitIndex = TOptional<uint16>())
	{
		FBattleEventTarget EventTarget;
		if (!Context.TryBuildEventTarget(Target, EventTarget))
		{
			return false;
		}
		FBattleEffectExecutionEvent& Event = Result.Events.AddDefaulted_GetRef();
		Event.Type = Type;
		Event.Cause = Type == EBattleEventType::RandomCheck
			? EBattleEventCause::Rule
			: EBattleEventCause::Move;
		Event.Outcome = Outcome;
		Event.Targets.Add(MoveTemp(EventTarget));
		Event.NumericBefore = NumericBefore;
		Event.NumericAfter = NumericAfter;
		Event.NumericDelta = NumericDelta;
		if (HitIndex.IsSet())
		{
			Event.HitIndex = HitIndex;
			Event.HitCount = static_cast<uint16>(1);
		}
		return true;
	}

	bool TryAddRandomEvent(
		IBattleEffectExecutionContext& Context,
		FBattleEffectExecutionResult& Result,
		const FBattleResolvedTarget& Target,
		const FBattleRandomDraw& Draw,
		const EBattleEffectExecutionOutcome Outcome,
		const TOptional<uint16>& HitIndex = TOptional<uint16>())
	{
		return TryAddTargetedEvent(
			Context,
			Result,
			EBattleEventType::RandomCheck,
			Outcome,
			Target,
			static_cast<int64>(Draw.InclusiveMinimum),
			static_cast<int64>(Draw.Result),
			static_cast<int64>(Draw.InclusiveMaximum),
			HitIndex);
	}

	bool TryAddHookOutcomeEvent(
		IBattleEffectExecutionContext& Context,
		FBattleEffectExecutionResult& Result,
		const FBattleResolvedTarget& Target,
		const FBattleEffectHookResult& Hook,
		const TOptional<uint16>& HitIndex = TOptional<uint16>())
	{
		if (!IsKnownOutcome(Hook.Outcome)
			|| Hook.Outcome == EBattleEffectExecutionOutcome::Applied
			|| Hook.Outcome == EBattleEffectExecutionOutcome::ChanceFailed
			|| Hook.bStateMutated)
		{
			return false;
		}
		return TryAddTargetedEvent(
			Context,
			Result,
			EventTypeForOutcome(Hook.Outcome),
			Hook.Outcome,
			Target,
			Hook.NumericBefore,
			Hook.NumericAfter,
			Hook.NumericDelta,
			HitIndex);
	}

	bool TryHandleGate(
		IBattleEffectExecutionContext& Context,
		FBattleEffectExecutionResult& Result,
		const FBattleResolvedTarget& Target,
		const FBattleEffectHookResult& Hook,
		bool& bOutContinue)
	{
		bOutContinue = false;
		if (!IsKnownOutcome(Hook.Outcome)
			|| Hook.Outcome == EBattleEffectExecutionOutcome::ChanceFailed
			|| (Hook.Outcome != EBattleEffectExecutionOutcome::Applied && Hook.bStateMutated))
		{
			return false;
		}
		if (Hook.Outcome == EBattleEffectExecutionOutcome::Applied)
		{
			bOutContinue = true;
			return true;
		}
		return TryAddHookOutcomeEvent(Context, Result, Target, Hook);
	}

	bool TryRoundHalfUp(
		const int64 Basis,
		const int32 Numerator,
		const int32 Denominator,
		const bool bMinimumOne,
		int32& OutValue)
	{
		OutValue = 0;
		if (Basis <= 0 || Numerator <= 0 || Denominator <= 0)
		{
			return Basis == 0 && Numerator > 0 && Denominator > 0;
		}
		if (Basis > TNumericLimits<int64>::Max() / static_cast<int64>(Numerator))
		{
			return false;
		}
		const int64 Product = Basis * static_cast<int64>(Numerator);
		const int64 Offset = static_cast<int64>(Denominator) / 2;
		if (Product > TNumericLimits<int64>::Max() - Offset)
		{
			return false;
		}
		int64 Rounded = (Product + Offset) / static_cast<int64>(Denominator);
		if (bMinimumOne && Rounded == 0)
		{
			Rounded = 1;
		}
		if (Rounded > TNumericLimits<int32>::Max())
		{
			return false;
		}
		OutValue = static_cast<int32>(Rounded);
		return true;
	}

	bool IsValidDamageInput(const FBattleFinalDamageInput& Input)
	{
		if (Input.AttackerLevel < 1 || Input.AttackerLevel > 100
			|| Input.MovePower <= 0
			|| (Input.MoveCategory != EBattleMoveCategory::Physical
				&& Input.MoveCategory != EBattleMoveCategory::Special)
			|| (Input.WeatherModifierQ12 != 2048
				&& Input.WeatherModifierQ12 != 4096
				&& Input.WeatherModifierQ12 != 6144)
			|| (Input.StabModifierQ12 != 4096 && Input.StabModifierQ12 != 6144)
			|| Input.BlockingRuleId.IsValid())
		{
			return false;
		}
		const FBattleTypeEffectiveness& Type = Input.TypeEffectiveness;
		const bool bKnownType = (Type.Numerator == 0 && Type.Denominator == 1)
			|| (Type.Numerator == 1 && Type.Denominator == 4)
			|| (Type.Numerator == 1 && Type.Denominator == 2)
			|| (Type.Numerator == 1 && Type.Denominator == 1)
			|| (Type.Numerator == 2 && Type.Denominator == 1)
			|| (Type.Numerator == 4 && Type.Denominator == 1);
		return bKnownType;
	}


	EBattleEventType MutationEventType(
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target)
	{
		switch (Effect.Kind)
		{
		case EBattleMoveEffectKind::ModifyStatStage:
			return EBattleEventType::StatStageChanged;
		case EBattleMoveEffectKind::SetFieldCondition:
		case EBattleMoveEffectKind::SetSideCondition:
			return EBattleEventType::FieldEffectChanged;
		case EBattleMoveEffectKind::ApplyCondition:
		case EBattleMoveEffectKind::Charge:
		case EBattleMoveEffectKind::Recharge:
		case EBattleMoveEffectKind::Protect:
		case EBattleMoveEffectKind::SemiInvulnerability:
			return EBattleEventType::StatusChanged;
		case EBattleMoveEffectKind::RemoveCondition:
			return Target.GetKind() == EBattleResolvedTargetKind::Battler
				? EBattleEventType::StatusChanged
				: EBattleEventType::FieldEffectChanged;
		default:
			return EBattleEventType::EffectFailed;
		}
	}

	bool TryApplyChance(
		const FBattleEffectExecutionRequest& Request,
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& Target,
		IBattleEffectExecutionContext& Context,
		IBattleRandom& Random,
		FBattleEffectExecutionResult& Result,
		bool& bOutPassed,
		EBattleEffectExecutorError& OutError,
		const TOptional<uint16>& HitIndex = TOptional<uint16>())
	{
		bOutPassed = false;
		if (Effect.ChanceNumerator == 1 && Effect.ChanceDenominator == 1)
		{
			bOutPassed = true;
			return true;
		}
		FBattleRandomDraw Draw;
		const FBattleRandomContext RandomContext = MakeRandomContext(
			Request,
			FBattleEffectExecutor::GetSecondaryChanceRulePurpose());
		if (!Random.TryDrawUniform(0, 99, RandomContext, Draw))
		{
			OutError = EBattleEffectExecutorError::RandomFailure;
			return false;
		}
		bOutPassed = Draw.Result < static_cast<uint32>(Effect.ChanceNumerator);
		if (!TryAddRandomEvent(
			Context,
			Result,
			Target,
			Draw,
			bOutPassed
				? EBattleEffectExecutionOutcome::Applied
				: EBattleEffectExecutionOutcome::ChanceFailed,
			HitIndex))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		return true;
	}

	bool TryApplyOrdinaryDescriptor(
		const FBattleEffectExecutionRequest& Request,
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& ReachedTarget,
		const TOptional<uint16>& HitIndex,
		IBattleEffectExecutionContext& Context,
		IBattleRandom& Random,
		FBattleEffectExecutionResult& Result,
		EBattleEffectExecutorError& OutError,
		TArray<FBattleResolvedTarget>* InOutAppliedTargets)
	{
		if (Context.ShouldSkipEffectDescriptor(Effect))
		{
			return true;
		}
		TArray<FBattleResolvedTarget> EffectTargets;
		if (!TryExpandEffectTargets(Request, Effect, ReachedTarget, EffectTargets))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		for (const FBattleResolvedTarget& Target : EffectTargets)
		{
			if (InOutAppliedTargets != nullptr)
			{
				if (InOutAppliedTargets->Contains(Target))
				{
					continue;
				}
				InOutAppliedTargets->Add(Target);
			}

			const FBattleEffectHookResult Eligibility = Context.CheckEffectEligibility(Effect, Target);
			if (!IsKnownOutcome(Eligibility.Outcome)
				|| Eligibility.Outcome == EBattleEffectExecutionOutcome::ChanceFailed
				|| (Eligibility.Outcome != EBattleEffectExecutionOutcome::Applied
					&& Eligibility.bStateMutated))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (Eligibility.Outcome != EBattleEffectExecutionOutcome::Applied)
			{
				if (!TryAddHookOutcomeEvent(Context, Result, Target, Eligibility, HitIndex))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				continue;
			}

			bool bChancePassed = false;
			if (!TryApplyChance(
				Request,
				Effect,
				Target,
				Context,
				Random,
				Result,
				bChancePassed,
				OutError,
				HitIndex))
			{
				return false;
			}
			if (!bChancePassed)
			{
				continue;
			}

			if (Effect.Kind == EBattleMoveEffectKind::Heal)
			{
				int32 CurrentHP = 0;
				int32 MaximumHP = 0;
				if (!Context.TryGetHp(Target, CurrentHP, MaximumHP))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				(void)CurrentHP;
				int32 RequestedHeal = Effect.MagnitudeNumerator;
				if (Effect.MagnitudeDenominator > 1
					&& !TryRoundHalfUp(
						MaximumHP,
						Effect.MagnitudeNumerator,
						Effect.MagnitudeDenominator,
						EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::MinimumOne),
						RequestedHeal))
				{
					OutError = EBattleEffectExecutorError::ArithmeticOverflow;
					return false;
				}
				const FBattleEffectHookResult Applied = Context.ApplyHpDelta(Target, RequestedHeal);
				if (!IsKnownOutcome(Applied.Outcome)
					|| (Applied.Outcome != EBattleEffectExecutionOutcome::Applied
						&& Applied.bStateMutated))
				{
					OutError = EBattleEffectExecutorError::InvalidHookResult;
					return false;
				}
				if (Applied.Outcome != EBattleEffectExecutionOutcome::Applied)
				{
					if (!TryAddHookOutcomeEvent(Context, Result, Target, Applied, HitIndex))
					{
						OutError = EBattleEffectExecutorError::InvalidTarget;
						return false;
					}
					continue;
				}
				if (Applied.bStateMutated)
				{
					if (!TryAddTargetedEvent(
						Context,
						Result,
						EBattleEventType::Healing,
						Applied.Outcome,
						Target,
						Applied.NumericBefore,
						Applied.NumericAfter,
						Applied.NumericDelta,
						HitIndex)
						|| !TryAddTargetedEvent(
							Context,
							Result,
							EBattleEventType::HPChanged,
							Applied.Outcome,
							Target,
							Applied.NumericBefore,
							Applied.NumericAfter,
							Applied.NumericDelta,
							HitIndex))
					{
						OutError = EBattleEffectExecutorError::InvalidTarget;
						return false;
					}
					Context.RunImmediateUpdate(Target);
					if (!Context.IsRuntimeValid())
					{
						OutError = Context.GetRuntimeError();
						return false;
					}
				}
				if (Applied.bCapped
					&& !TryAddTargetedEvent(
						Context,
						Result,
						EBattleEventType::EffectCapped,
						EBattleEffectExecutionOutcome::Capped,
						Target,
						Applied.NumericBefore,
						Applied.NumericAfter,
						Applied.NumericDelta,
						HitIndex))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				continue;
			}

			const FBattleEffectHookResult Applied = Context.ApplyNonHpEffect(Effect, Target);
			if (!Context.IsRuntimeValid())
			{
				OutError = Context.GetRuntimeError();
				return false;
			}
			if (!IsKnownOutcome(Applied.Outcome)
				|| (Applied.Outcome != EBattleEffectExecutionOutcome::Applied
					&& Applied.bStateMutated))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (Applied.Outcome != EBattleEffectExecutionOutcome::Applied)
			{
				const int32 OutcomeEventIndex = Result.Events.Num();
				if (!TryAddHookOutcomeEvent(Context, Result, Target, Applied, HitIndex))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				if (Effect.Kind == EBattleMoveEffectKind::Switch
					&& Applied.Outcome == EBattleEffectExecutionOutcome::Deferred)
				{
					FBattleSwitchEffectIntent Intent;
					Intent.Kind = Effect.Target == EBattleEffectTarget::User
						? EBattleSwitchKind::Pivot
						: EBattleSwitchKind::Forced;
					Intent.EffectEventIndex = OutcomeEventIndex;
					Intent.Target = Target;
					Result.SwitchIntents.Add(MoveTemp(Intent));
				}
				continue;
			}
			if (!Applied.bStateMutated)
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (Applied.bDefersMove)
			{
				Result.bMoveDeferred = true;
			}
			if (!TryAddTargetedEvent(
				Context,
				Result,
				MutationEventType(Effect, Target),
				Applied.Outcome,
				Target,
				Applied.NumericBefore,
				Applied.NumericAfter,
				Applied.NumericDelta,
				HitIndex))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			if (Applied.bCapped
				&& !TryAddTargetedEvent(
					Context,
					Result,
					EBattleEventType::EffectCapped,
					EBattleEffectExecutionOutcome::Capped,
					Target,
					Applied.NumericBefore,
					Applied.NumericAfter,
					Applied.NumericDelta,
					HitIndex))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			Context.RunImmediateUpdate(Target);
			if (!Context.IsRuntimeValid())
			{
				OutError = Context.GetRuntimeError();
				return false;
			}
		}
		return true;
	}

	bool TryApplyLinkedDescriptor(
		const FBattleEffectExecutionRequest& Request,
		const FBattleMoveEffectDescriptor& Effect,
		const FBattleResolvedTarget& ReachedTarget,
		const int64 TotalActualDamage,
		IBattleEffectExecutionContext& Context,
		IBattleRandom& Random,
		FBattleEffectExecutionResult& Result,
		EBattleEffectExecutorError& OutError)
	{
		TArray<FBattleResolvedTarget> EffectTargets;
		if (!TryExpandEffectTargets(Request, Effect, ReachedTarget, EffectTargets)
			|| EffectTargets.Num() != 1)
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		const FBattleResolvedTarget& Target = EffectTargets[0];
		int32 CurrentHP = 0;
		int32 MaximumHP = 0;
		if (!Context.TryGetHp(Target, CurrentHP, MaximumHP))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		(void)CurrentHP;
		int64 Basis = TotalActualDamage;
		if (Effect.Kind == EBattleMoveEffectKind::Recoil
			&& !EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::UsesActualDamage))
		{
			Basis = Effect.MagnitudeDenominator == 1 ? 1 : MaximumHP;
		}
		if (Basis <= 0)
		{
			return true;
		}
		const FBattleEffectHookResult Eligibility = Context.CheckEffectEligibility(Effect, Target);
		if (!IsKnownOutcome(Eligibility.Outcome)
			|| Eligibility.Outcome == EBattleEffectExecutionOutcome::ChanceFailed
			|| (Eligibility.Outcome != EBattleEffectExecutionOutcome::Applied
				&& Eligibility.bStateMutated))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (Eligibility.Outcome != EBattleEffectExecutionOutcome::Applied)
		{
			if (!TryAddHookOutcomeEvent(Context, Result, Target, Eligibility))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			return true;
		}
		bool bChancePassed = false;
		if (!TryApplyChance(
			Request,
			Effect,
			Target,
			Context,
			Random,
			Result,
			bChancePassed,
			OutError))
		{
			return false;
		}
		if (!bChancePassed)
		{
			return true;
		}

		int32 Magnitude = Effect.MagnitudeNumerator;
		if (Effect.MagnitudeDenominator > 1
			|| Effect.Kind == EBattleMoveEffectKind::Drain
			|| EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::UsesActualDamage))
		{
			if (!TryRoundHalfUp(
				Basis,
				Effect.MagnitudeNumerator,
				Effect.MagnitudeDenominator,
				EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::MinimumOne),
				Magnitude))
			{
				OutError = EBattleEffectExecutorError::ArithmeticOverflow;
				return false;
			}
		}
		if (Magnitude <= 0)
		{
			return true;
		}
		const int32 RequestedDelta = Effect.Kind == EBattleMoveEffectKind::Drain
			? Magnitude
			: -Magnitude;
		const FBattleEffectHookResult Applied = Context.ApplyHpDelta(Target, RequestedDelta);
		if (!IsKnownOutcome(Applied.Outcome)
			|| (Applied.Outcome != EBattleEffectExecutionOutcome::Applied
				&& Applied.bStateMutated))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (Applied.Outcome != EBattleEffectExecutionOutcome::Applied)
		{
			if (!TryAddHookOutcomeEvent(Context, Result, Target, Applied))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			return true;
		}
		if (Applied.bStateMutated)
		{
			const EBattleEventType MutationType = Effect.Kind == EBattleMoveEffectKind::Drain
				? EBattleEventType::Healing
				: EBattleEventType::Damage;
			if (!TryAddTargetedEvent(
				Context,
				Result,
				MutationType,
				Applied.Outcome,
				Target,
				Applied.NumericBefore,
				Applied.NumericAfter,
				Applied.NumericDelta)
				|| !TryAddTargetedEvent(
					Context,
					Result,
					EBattleEventType::HPChanged,
					Applied.Outcome,
					Target,
					Applied.NumericBefore,
					Applied.NumericAfter,
					Applied.NumericDelta))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			Context.RunImmediateUpdate(Target);
			if (!Context.IsRuntimeValid())
			{
				OutError = Context.GetRuntimeError();
				return false;
			}
		}
		if (Applied.bCapped
			&& !TryAddTargetedEvent(
				Context,
				Result,
				EBattleEventType::EffectCapped,
				EBattleEffectExecutionOutcome::Capped,
				Target,
				Applied.NumericBefore,
				Applied.NumericAfter,
				Applied.NumericDelta))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		return true;
	}
}

FDefinitionId FBattleEffectExecutor::GetAccuracyRulePurpose()
{
	static const FDefinitionId Id = BattleEffectExecutorPrivate::MakeRuleId(TEXT("Rule.C05B.Accuracy"));
	return Id;
}

FDefinitionId FBattleEffectExecutor::GetCriticalRulePurpose()
{
	static const FDefinitionId Id = BattleEffectExecutorPrivate::MakeRuleId(TEXT("Rule.C05B.Critical"));
	return Id;
}

FDefinitionId FBattleEffectExecutor::GetDamageRandomRulePurpose()
{
	static const FDefinitionId Id = BattleEffectExecutorPrivate::MakeRuleId(TEXT("Rule.C05B.DamageRandom"));
	return Id;
}

FDefinitionId FBattleEffectExecutor::GetMultiHitRulePurpose()
{
	static const FDefinitionId Id = BattleEffectExecutorPrivate::MakeRuleId(TEXT("Rule.C05B.MultiHitCount"));
	return Id;
}

FDefinitionId FBattleEffectExecutor::GetSecondaryChanceRulePurpose()
{
	static const FDefinitionId Id = BattleEffectExecutorPrivate::MakeRuleId(TEXT("Rule.C05B.SecondaryChance"));
	return Id;
}

bool FBattleEffectExecutor::TryExecute(
	const FBattleEffectExecutionRequest& Request,
	IBattleEffectExecutionContext& Context,
	IBattleRandom& Random,
	FBattleEffectExecutionResult& OutResult,
	EBattleEffectExecutorError& OutError)
{
	using namespace BattleEffectExecutorPrivate;

	OutResult = FBattleEffectExecutionResult();
	OutError = EBattleEffectExecutorError::None;
	struct FFailureResultGuard
	{
		FBattleEffectExecutionResult& Result;
		bool bKeepResult = false;

		~FFailureResultGuard()
		{
			if (!bKeepResult)
			{
				Result = FBattleEffectExecutionResult();
			}
		}
	} FailureResultGuard{OutResult};
	if (!Request.BattleId.IsValid()
		|| !Request.TurnId.IsValid()
		|| !Request.ActionId.IsValid()
		|| !Request.ResolutionId.IsValid()
		|| !Request.UserBattlerId.IsValid()
		|| !Request.UserSlotId.IsValid()
		|| Request.Move == nullptr)
	{
		OutError = EBattleEffectExecutorError::InvalidRequest;
		return false;
	}

	const FBattleMoveEffectDescriptor* DamageEffect = nullptr;
	const FBattleMoveEffectDescriptor* MultiHitEffect = nullptr;
	if (!ValidateMoveDefinition(Request, DamageEffect, MultiHitEffect))
	{
		OutError = EBattleEffectExecutorError::InvalidMoveDefinition;
		return false;
	}
	if (!Context.PrevalidateRequest(Request))
	{
		OutError = EBattleEffectExecutorError::InvalidRequest;
		return false;
	}

	FBattleResolvedTarget UserTarget;
	FBattleEventTarget UserEventTarget;
	if (!TryCreateUserTarget(Request, UserTarget)
		|| !Context.TryBuildEventTarget(UserTarget, UserEventTarget))
	{
		OutError = EBattleEffectExecutorError::InvalidTarget;
		return false;
	}
	for (const FBattleResolvedTarget& Target : Request.Targets)
	{
		FBattleEventTarget EventTarget;
		if (!Context.TryBuildEventTarget(Target, EventTarget))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
	}

	const FBattleMoveEffectDescriptor* ChargeEffect = Request.Move->Effects.FindByPredicate(
		[](const FBattleMoveEffectDescriptor& Effect)
		{
			return Effect.Kind == EBattleMoveEffectKind::Charge;
		});
	if (ChargeEffect != nullptr && !Context.ShouldSkipEffectDescriptor(*ChargeEffect))
	{
		if (!TryApplyOrdinaryDescriptor(
				Request,
				*ChargeEffect,
				UserTarget,
				TOptional<uint16>(),
				Context,
				Random,
				OutResult,
				OutError,
				nullptr))
		{
			return false;
		}
		OutResult.TotalActualDamage = 0;
		OutResult.bValid = true;
		FailureResultGuard.bKeepResult = true;
		return true;
	}

	const bool bDamagingMove = DamageEffect != nullptr;
	const bool bSpread = Request.Move->TargetClass == EBattleTargetClass::FixedSpreadSet
		&& Request.Targets.Num() > 1;
	int64 TotalActualDamage = 0;
	int32 TotalCompletedHits = 0;
	TOptional<FBattleResolvedTarget> FirstReachedTarget;
	TArray<FBattleResolvedTarget> CompletedDamageTargets;
	TMap<int32, TArray<FBattleResolvedTarget>> AppliedActionScopedTargetsByEffectOrder;
	auto GetAppliedActionScopedTargets =
		[&AppliedActionScopedTargetsByEffectOrder](
			const FBattleMoveEffectDescriptor& Effect) -> TArray<FBattleResolvedTarget>*
		{
			return IsActionScopedOrdinaryEffect(Effect)
				? &AppliedActionScopedTargetsByEffectOrder.FindOrAdd(Effect.Order)
				: nullptr;
		};

	for (const FBattleResolvedTarget& Target : Request.Targets)
	{
		bool bContinue = false;
		if (!TryHandleGate(Context, OutResult, Target, Context.CheckReachability(*Request.Move, Target), bContinue))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (!bContinue)
		{
			if (bDamagingMove)
			{
				OutResult.CompletedHitsPerDamageTarget.Add(0);
			}
			continue;
		}
		if (!TryHandleGate(
			Context,
			OutResult,
			Target,
			Context.CheckProtection(*Request.Move, Target),
			bContinue))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (!bContinue)
		{
			if (bDamagingMove)
			{
				OutResult.CompletedHitsPerDamageTarget.Add(0);
			}
			continue;
		}
		if (!TryHandleGate(
			Context,
			OutResult,
			Target,
			Context.CheckTryHit(*Request.Move, Target),
			bContinue))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (!bContinue)
		{
			if (bDamagingMove)
			{
				OutResult.CompletedHitsPerDamageTarget.Add(0);
			}
			continue;
		}

		if (bDamagingMove)
		{
			FBattleFinalDamageInput TypeInput;
			if (!Context.TryBuildDamageInput(*Request.Move, Target, bSpread, TypeInput)
				|| !IsValidDamageInput(TypeInput))
			{
				OutError = EBattleEffectExecutorError::DamageResolutionFailure;
				return false;
			}
			bool bNoEffect = false;
			FBattleFinalDamageResult NoEffectResult;
			EBattleDamageCalculationError DamageError = EBattleDamageCalculationError::None;
			if (!FBattleFinalDamageCalculator::TryResolvePreAccuracyNoEffect(
				TypeInput,
				bNoEffect,
				NoEffectResult,
				DamageError))
			{
				OutError = EBattleEffectExecutorError::DamageResolutionFailure;
				return false;
			}
			if (bNoEffect)
			{
				if (!TryAddTargetedEvent(
					Context,
					OutResult,
					EBattleEventType::Immunity,
					EBattleEffectExecutionOutcome::Immune,
					Target))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				OutResult.CompletedHitsPerDamageTarget.Add(0);
				continue;
			}
		}

		bool bStoppedByRuleGate = false;
		auto RunRuleGate = [&](const FBattleEffectHookResult& Gate)
		{
			if (!TryHandleGate(Context, OutResult, Target, Gate, bContinue))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (!bContinue)
			{
				bStoppedByRuleGate = true;
			}
			return true;
		};
		if (!RunRuleGate(Context.CheckMoveImmunity(*Request.Move, Target)))
		{
			return false;
		}
		if (!bStoppedByRuleGate
			&& !RunRuleGate(Context.CheckAbilityImmunity(*Request.Move, Target)))
		{
			return false;
		}
		if (!bStoppedByRuleGate
			&& !RunRuleGate(Context.CheckItemImmunity(*Request.Move, Target)))
		{
			return false;
		}
		if (bStoppedByRuleGate)
		{
			if (bDamagingMove)
			{
				OutResult.CompletedHitsPerDamageTarget.Add(0);
			}
			continue;
		}

		FBattleAccuracyCheckInput AccuracyInput;
		if (!Context.TryBuildAccuracyInput(*Request.Move, Target, AccuracyInput))
		{
			OutError = EBattleEffectExecutorError::HitResolutionFailure;
			return false;
		}
		AccuracyInput.RandomContext = MakeRandomContext(Request, GetAccuracyRulePurpose());
		FBattleAccuracyCheckResult AccuracyResult;
		EBattleHitResolverError HitError = EBattleHitResolverError::None;
		if (!FBattleHitResolver::TryResolveAccuracy(AccuracyInput, Random, AccuracyResult, HitError))
		{
			OutError = HitError == EBattleHitResolverError::RandomFailure
				? EBattleEffectExecutorError::RandomFailure
				: EBattleEffectExecutorError::HitResolutionFailure;
			return false;
		}
		if (AccuracyResult.bDrawConsumed
			&& !TryAddRandomEvent(Context, OutResult, Target, AccuracyResult.Draw, EBattleEffectExecutionOutcome::Applied))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		const bool bAccuracyHit = AccuracyResult.Outcome == EBattleAccuracyCheckOutcome::Hit;
		if (!TryAddTargetedEvent(
			Context,
			OutResult,
			EBattleEventType::AccuracyChecked,
			bAccuracyHit ? EBattleEffectExecutionOutcome::Applied : EBattleEffectExecutionOutcome::Failed,
			Target,
			static_cast<int64>(AccuracyResult.EffectiveAccuracy),
			static_cast<int64>(bAccuracyHit ? 1 : 0)))
		{
			OutError = EBattleEffectExecutorError::InvalidTarget;
			return false;
		}
		if (!bAccuracyHit)
		{
			if (!TryAddTargetedEvent(
				Context,
				OutResult,
				EBattleEventType::Missed,
				EBattleEffectExecutionOutcome::Failed,
				Target))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			if (bDamagingMove)
			{
				OutResult.CompletedHitsPerDamageTarget.Add(0);
			}
			continue;
		}

		if (!TryHandleGate(
			Context,
			OutResult,
			Target,
			Context.ApplyProtectionBreaking(*Request.Move, Target),
			bContinue))
		{
			OutError = EBattleEffectExecutorError::InvalidHookResult;
			return false;
		}
		if (!bContinue)
		{
			if (bDamagingMove)
			{
				OutResult.CompletedHitsPerDamageTarget.Add(0);
			}
			continue;
		}

		if (!FirstReachedTarget.IsSet())
		{
			FirstReachedTarget = Target;
		}

		if (!bDamagingMove)
		{
			for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
			{
				if (!TryApplyOrdinaryDescriptor(
					Request,
					Effect,
					Target,
					TOptional<uint16>(),
					Context,
					Random,
					OutResult,
					OutError,
					GetAppliedActionScopedTargets(Effect)))
				{
					return false;
				}
				if (OutResult.bMoveDeferred)
				{
					OutResult.bValid = true;
					FailureResultGuard.bKeepResult = true;
					return true;
				}
			}
			if (EnumHasAllFlags(Request.Move->Flags, EBattleMoveFlags::ThawsTarget))
			{
				Context.RunImmediateUpdate(Target);
				if (!Context.IsRuntimeValid())
				{
					OutError = Context.GetRuntimeError();
					return false;
				}
			}
			continue;
		}

		for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
		{
			const bool bPrimary = Effect.ChanceNumerator == 1
				&& Effect.ChanceDenominator == 1;
			const bool bCoreOrLinked = Effect.Kind == EBattleMoveEffectKind::Damage
				|| Effect.Kind == EBattleMoveEffectKind::MultiHit
				|| Effect.Kind == EBattleMoveEffectKind::Drain
				|| Effect.Kind == EBattleMoveEffectKind::Recoil;
			if (!bPrimary || bCoreOrLinked || Effect.Order >= DamageEffect->Order)
			{
				continue;
			}
			if (!TryApplyOrdinaryDescriptor(
				Request,
				Effect,
				Target,
				TOptional<uint16>(),
				Context,
				Random,
				OutResult,
				OutError,
				GetAppliedActionScopedTargets(Effect)))
			{
				return false;
			}
			if (OutResult.bMoveDeferred)
			{
				OutResult.bValid = true;
				FailureResultGuard.bKeepResult = true;
				return true;
			}
		}

		int32 RequestedHitCount = 1;
		if (MultiHitEffect != nullptr)
		{
			if (MultiHitEffect->MinimumCount == MultiHitEffect->MaximumCount)
			{
				RequestedHitCount = MultiHitEffect->MinimumCount;
			}
			else
			{
				FBattleRandomDraw Draw;
				const FBattleRandomContext MultiHitContext = MakeRandomContext(
					Request,
					GetMultiHitRulePurpose());
				if (!Random.TryDrawUniform(0, 19, MultiHitContext, Draw))
				{
					OutError = EBattleEffectExecutorError::RandomFailure;
					return false;
				}
				RequestedHitCount = Draw.Result <= 6 ? 2
					: Draw.Result <= 13 ? 3
					: Draw.Result <= 16 ? 4
					: 5;
				if (!TryAddRandomEvent(
					Context,
					OutResult,
					Target,
					Draw,
					EBattleEffectExecutionOutcome::Applied))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
			}
		}

		const int32 TargetEventStart = OutResult.Events.Num();
		int32 CompletedHits = 0;
		for (int32 HitIndex = 1; HitIndex <= RequestedHitCount; ++HitIndex)
		{
			if (!Context.IsSourceAbleToContinue() || !Context.IsTargetAbleToContinue(Target))
			{
				break;
			}
			if (HitIndex > 1
				&& EnumHasAllFlags(Request.Move->Flags, EBattleMoveFlags::UsesPerHitAccuracy))
			{
				FBattleAccuracyCheckInput PerHitAccuracyInput;
				if (!Context.TryBuildAccuracyInput(*Request.Move, Target, PerHitAccuracyInput))
				{
					OutError = EBattleEffectExecutorError::HitResolutionFailure;
					return false;
				}
				PerHitAccuracyInput.RandomContext = MakeRandomContext(Request, GetAccuracyRulePurpose());
				FBattleAccuracyCheckResult PerHitAccuracyResult;
				if (!FBattleHitResolver::TryResolveAccuracy(
					PerHitAccuracyInput,
					Random,
					PerHitAccuracyResult,
					HitError))
				{
					OutError = HitError == EBattleHitResolverError::RandomFailure
						? EBattleEffectExecutorError::RandomFailure
						: EBattleEffectExecutorError::HitResolutionFailure;
					return false;
				}
				if (PerHitAccuracyResult.bDrawConsumed
					&& !TryAddRandomEvent(
						Context,
						OutResult,
						Target,
						PerHitAccuracyResult.Draw,
						EBattleEffectExecutionOutcome::Applied))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				const bool bPerHitAccuracyHit = PerHitAccuracyResult.Outcome
					== EBattleAccuracyCheckOutcome::Hit;
				if (!TryAddTargetedEvent(
					Context,
					OutResult,
					EBattleEventType::AccuracyChecked,
					bPerHitAccuracyHit
						? EBattleEffectExecutionOutcome::Applied
						: EBattleEffectExecutionOutcome::Failed,
					Target,
					static_cast<int64>(PerHitAccuracyResult.EffectiveAccuracy),
					static_cast<int64>(bPerHitAccuracyHit ? 1 : 0)))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				if (!bPerHitAccuracyHit)
				{
					if (!TryAddTargetedEvent(
						Context,
						OutResult,
						EBattleEventType::Missed,
						EBattleEffectExecutionOutcome::Failed,
						Target))
					{
						OutError = EBattleEffectExecutorError::InvalidTarget;
						return false;
					}
					break;
				}
			}

			FBattleCriticalCheckInput CriticalInput;
			if (!Context.TryBuildCriticalInput(*Request.Move, Target, CriticalInput))
			{
				OutError = EBattleEffectExecutorError::HitResolutionFailure;
				return false;
			}
			CriticalInput.RandomContext = MakeRandomContext(Request, GetCriticalRulePurpose());
			FBattleCriticalCheckResult CriticalResult;
			if (!FBattleHitResolver::TryResolveCritical(CriticalInput, Random, CriticalResult, HitError))
			{
				OutError = HitError == EBattleHitResolverError::RandomFailure
					? EBattleEffectExecutorError::RandomFailure
					: EBattleEffectExecutorError::HitResolutionFailure;
				return false;
			}
			if (CriticalResult.bDrawConsumed
				&& !TryAddRandomEvent(Context, OutResult, Target, CriticalResult.Draw, EBattleEffectExecutionOutcome::Applied))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			const bool bCritical = CriticalResult.Outcome == EBattleCriticalCheckOutcome::Critical;
			if (!TryAddTargetedEvent(
				Context,
				OutResult,
				EBattleEventType::CriticalChecked,
				bCritical ? EBattleEffectExecutionOutcome::Applied : EBattleEffectExecutionOutcome::Failed,
				Target,
				static_cast<int64>(CriticalResult.ResolvedStage),
				static_cast<int64>(bCritical ? 1 : 0),
				TOptional<int64>(),
				static_cast<uint16>(HitIndex)))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}

			FBattleFinalDamageInput DamageInput;
			if (!Context.TryBuildDamageInput(*Request.Move, Target, bSpread, DamageInput)
				|| !IsValidDamageInput(DamageInput))
			{
				OutError = EBattleEffectExecutorError::DamageResolutionFailure;
				return false;
			}
			DamageInput.bCritical = bCritical;
			DamageInput.RandomContext = MakeRandomContext(Request, GetDamageRandomRulePurpose());
			FBattleFinalDamageResult DamageResult;
			EBattleDamageCalculationError DamageError = EBattleDamageCalculationError::None;
			if (!FBattleFinalDamageCalculator::TryCalculateFinalDamage(
				DamageInput,
				Random,
				DamageResult,
				DamageError))
			{
				OutError = DamageError == EBattleDamageCalculationError::RandomFailure
					? EBattleEffectExecutorError::RandomFailure
					: DamageError == EBattleDamageCalculationError::ArithmeticOverflow
						? EBattleEffectExecutorError::ArithmeticOverflow
						: EBattleEffectExecutorError::DamageResolutionFailure;
				return false;
			}
			if (DamageResult.Outcome != EBattleDamageOutcome::Damage)
			{
				OutError = EBattleEffectExecutorError::DamageResolutionFailure;
				return false;
			}
			if (DamageResult.bRandomDrawConsumed
				&& !TryAddRandomEvent(Context, OutResult, Target, DamageResult.RandomDraw, EBattleEffectExecutionOutcome::Applied))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}

			if (!TryAddTargetedEvent(
				Context,
				OutResult,
				EBattleEventType::Effectiveness,
				EBattleEffectExecutionOutcome::Applied,
				Target,
				static_cast<int64>(DamageInput.TypeEffectiveness.Numerator),
				static_cast<int64>(DamageInput.TypeEffectiveness.Denominator),
				TOptional<int64>(),
				static_cast<uint16>(HitIndex)))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			Context.SetDirectMoveDamageHit(true);
			const FBattleEffectHookResult AppliedDamage = Context.ApplyHpDelta(
				Target,
				-DamageResult.Damage);
			Context.SetDirectMoveDamageHit(false);
			if (!IsKnownOutcome(AppliedDamage.Outcome)
				|| (AppliedDamage.Outcome != EBattleEffectExecutionOutcome::Applied
					&& AppliedDamage.bStateMutated))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (AppliedDamage.Outcome != EBattleEffectExecutionOutcome::Applied)
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}

			int64 ActualRemoved = 0;
			if (AppliedDamage.NumericDelta.IsSet())
			{
				ActualRemoved = FMath::Max<int64>(0, -AppliedDamage.NumericDelta.GetValue());
			}
			if (TotalActualDamage > TNumericLimits<int32>::Max() - ActualRemoved)
			{
				OutError = EBattleEffectExecutorError::ArithmeticOverflow;
				return false;
			}
			TotalActualDamage += ActualRemoved;
			++CompletedHits;
			++TotalCompletedHits;
			if (!TryAddTargetedEvent(
				Context,
				OutResult,
				EBattleEventType::Damage,
				AppliedDamage.Outcome,
				Target,
				AppliedDamage.NumericBefore,
				AppliedDamage.NumericAfter,
				AppliedDamage.NumericDelta,
				static_cast<uint16>(HitIndex)))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			if (AppliedDamage.bStateMutated
				&& !AppliedDamage.bAffectsSubstitute
				&& !TryAddTargetedEvent(
					Context,
					OutResult,
					EBattleEventType::HPChanged,
					AppliedDamage.Outcome,
					Target,
					AppliedDamage.NumericBefore,
					AppliedDamage.NumericAfter,
					AppliedDamage.NumericDelta,
					static_cast<uint16>(HitIndex)))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			if (AppliedDamage.bSubstituteBroken
				&& !TryAddTargetedEvent(
					Context,
					OutResult,
					EBattleEventType::StatusChanged,
					AppliedDamage.Outcome,
					Target,
					static_cast<int64>(1),
					static_cast<int64>(0),
					static_cast<int64>(-1),
					static_cast<uint16>(HitIndex)))
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			Context.RunImmediateUpdate(Target);
			if (!Context.IsRuntimeValid())
			{
				OutError = Context.GetRuntimeError();
				return false;
			}

		}
		OutResult.CompletedHitsPerDamageTarget.Add(CompletedHits);

		if (CompletedHits > 0)
		{
			CompletedDamageTargets.Add(Target);
			for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
			{
				if (Effect.Kind == EBattleMoveEffectKind::Damage
					|| Effect.Kind == EBattleMoveEffectKind::MultiHit
					|| Effect.Kind == EBattleMoveEffectKind::Drain
					|| Effect.Kind == EBattleMoveEffectKind::Recoil)
				{
					continue;
				}
				const bool bPrimaryBeforeDamage = Effect.ChanceNumerator == 1
					&& Effect.ChanceDenominator == 1
					&& Effect.Order < DamageEffect->Order;
				if (bPrimaryBeforeDamage)
				{
					continue;
				}
				const bool bPerHit = EnumHasAllFlags(
					Effect.Flags,
					EBattleMoveEffectFlags::PerHit);
				if (bSpread && !bPerHit)
				{
					continue;
				}
				const int32 ApplicationCount = bPerHit ? CompletedHits : 1;
				for (int32 ApplicationIndex = 1;
					ApplicationIndex <= ApplicationCount;
					++ApplicationIndex)
				{
					const TOptional<uint16> EffectHitIndex = bPerHit
						? TOptional<uint16>(static_cast<uint16>(ApplicationIndex))
						: TOptional<uint16>();
					if (!TryApplyOrdinaryDescriptor(
						Request,
						Effect,
						Target,
						EffectHitIndex,
						Context,
						Random,
						OutResult,
						OutError,
						nullptr))
					{
						return false;
					}
				}
			}
		}

		for (int32 EventIndex = TargetEventStart; EventIndex < OutResult.Events.Num(); ++EventIndex)
		{
			FBattleEffectExecutionEvent& Event = OutResult.Events[EventIndex];
			if (Event.HitIndex.IsSet())
			{
				Event.HitCount = static_cast<uint16>(CompletedHits);
			}
		}
	}

	if (bDamagingMove && bSpread && !CompletedDamageTargets.IsEmpty())
	{
		for (const FBattleResolvedTarget& Target : CompletedDamageTargets)
		{
			for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
			{
				if (Effect.Kind == EBattleMoveEffectKind::Damage
					|| Effect.Kind == EBattleMoveEffectKind::MultiHit
					|| Effect.Kind == EBattleMoveEffectKind::Drain
					|| Effect.Kind == EBattleMoveEffectKind::Recoil)
				{
					continue;
				}
				const bool bPrimaryBeforeDamage = Effect.ChanceNumerator == 1
					&& Effect.ChanceDenominator == 1
					&& Effect.Order < DamageEffect->Order;
				if (bPrimaryBeforeDamage
					|| EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::PerHit))
				{
					continue;
				}
				if (!TryApplyOrdinaryDescriptor(
					Request,
					Effect,
					Target,
					TOptional<uint16>(),
					Context,
					Random,
					OutResult,
					OutError,
					GetAppliedActionScopedTargets(Effect)))
				{
					return false;
				}
			}
		}
	}

	if (bDamagingMove && TotalCompletedHits > 0 && FirstReachedTarget.IsSet())
	{
		for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
		{
			if (Effect.Kind != EBattleMoveEffectKind::Drain
				&& Effect.Kind != EBattleMoveEffectKind::Recoil)
			{
				continue;
			}
			if (!TryApplyLinkedDescriptor(
				Request,
				Effect,
				FirstReachedTarget.GetValue(),
				TotalActualDamage,
				Context,
				Random,
				OutResult,
				OutError))
			{
				return false;
			}
		}
	}

	OutResult.TotalActualDamage = static_cast<int32>(TotalActualDamage);
	OutResult.bValid = true;
	FailureResultGuard.bKeepResult = true;
	return true;
}
