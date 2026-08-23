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

	class FStateExecutionContext final : public IBattleEffectExecutionContext
	{
	public:
		FStateExecutionContext(
			const FBattleEffectExecutionRequest& InRequest,
			FBattleEngineState& InState)
			: Request(InRequest)
			, State(InState)
			, Battlers(InState.Battlers)
			, ActivePositions(InState.ActivePositions)
			, Field(InState.Field)
			, Sides(InState.Sides)
			, TriggerFramework(InState.TriggerFramework)
			, AbilityItemRevealTracker(InState.AbilityItemRevealTracker)
			, HeldItemLedger(InState.HeldItemLedger)
			, NextConditionCreationOrdinal(InState.NextConditionCreationOrdinal)
			, NextTriggerReentrancyToken(InState.NextTriggerReentrancyToken)
		{
		}

		void Commit()
		{
			State.Battlers = MoveTemp(Battlers);
			State.ActivePositions = MoveTemp(ActivePositions);
			State.Field = MoveTemp(Field);
			State.Sides = MoveTemp(Sides);
			State.TriggerFramework = MoveTemp(TriggerFramework);
			State.AbilityItemRevealTracker = MoveTemp(AbilityItemRevealTracker);
			State.HeldItemLedger = MoveTemp(HeldItemLedger);
			State.NextConditionCreationOrdinal = NextConditionCreationOrdinal;
			State.NextTriggerReentrancyToken = NextTriggerReentrancyToken;
		}

		void BindExecutionResult(FBattleEffectExecutionResult& InResult)
		{
			ExecutionResult = &InResult;
		}

		bool TryResolveForcedSwitches(
			FBattleEffectExecutionResult& Result,
			EBattleEffectExecutorError& OutError)
		{
			for (FBattleSwitchEffectIntent& Intent : Result.SwitchIntents)
			{
				if (Intent.Kind != EBattleSwitchKind::Forced)
				{
					continue;
				}
				if (Intent.Target.GetKind() != EBattleResolvedTargetKind::Battler)
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}

				const FBattleBattlerTarget& ForcedTarget = Intent.Target.GetBattler();
				FBattleActivePositionState* Active = FindMutableActivePosition(
					ForcedTarget.ActiveSlotId);
				FBattleBattlerState* Outgoing = FindMutableBattler(ForcedTarget.BattlerId);
				if (Active == nullptr
					|| Outgoing == nullptr
					|| !Active->bAvailable
					|| Active->BattlerId != Outgoing->BattlerId
					|| Outgoing->CurrentHP <= 0
					|| Outgoing->bFainted
					|| Outgoing->bCaptured
					|| Outgoing->bRemoved)
				{
					Intent.BlockReason = EBattleSwitchBlockReason::ActingBattlerUnavailable;
					continue;
				}

				const FBattleTrainerState* Trainer = State.FindTrainer(Outgoing->TrainerId);
				if (Trainer == nullptr || Active->TrainerId != Trainer->TrainerId)
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}

				FBattleSwitchLegalitySpec LegalitySpec;
				LegalitySpec.Kind = EBattleSwitchKind::Forced;
				LegalitySpec.ActingTrainerId = Trainer->TrainerId;
				LegalitySpec.ActingBattlerId = Outgoing->BattlerId;
				LegalitySpec.ActiveSlotId = Active->ActiveSlotId;
				LegalitySpec.TransferPolicy = EBattleSwitchStateTransferPolicy::ClearTransient;
				for (const FBattlePartySlotState& PartySlot : Trainer->PartySlots)
				{
					FBattleSwitchCandidateFacts Candidate;
					Candidate.PartySlotId = PartySlot.PartySlotId;
					Candidate.bOccupied = PartySlot.BattlerId.IsValid();
					if (Candidate.bOccupied)
					{
						const FBattleBattlerState* Battler = FindBattler(PartySlot.BattlerId);
						if (Battler == nullptr)
						{
							OutError = EBattleEffectExecutorError::InvalidTarget;
							return false;
						}
						Candidate.TrainerId = Battler->TrainerId;
						Candidate.BattlerId = Battler->BattlerId;
						Candidate.bAlreadyActive = FindActiveForBattler(Battler->BattlerId) != nullptr;
						Candidate.bFainted = Battler->CurrentHP <= 0 || Battler->bFainted;
						Candidate.bEgg = Battler->bEgg;
						Candidate.bCaptured = Battler->bCaptured;
						Candidate.bRemoved = Battler->bRemoved;
					}
					LegalitySpec.Candidates.Add(MoveTemp(Candidate));
				}

				FBattleSwitchLegalityResult Legality;
				if (!FBattleSwitchResolver::TryBuildLegality(LegalitySpec, Legality))
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				FBattleSwitchSelectionSpec SelectionSpec;
				SelectionSpec.RandomContext.BattleId = Request.BattleId;
				SelectionSpec.RandomContext.TurnId = Request.TurnId;
				SelectionSpec.RandomContext.ActionId = Request.ActionId;
				SelectionSpec.RandomContext.ResolutionId = Request.ResolutionId;
				SelectionSpec.RandomContext.RulePurpose =
					FBattleSwitchResolver::GetForcedSelectionRulePurpose();
				FBattleSwitchResolution Resolution;
				if (!FBattleSwitchResolver::TryResolve(
					Legality,
					SelectionSpec,
					*State.Random,
					Resolution))
				{
					OutError = EBattleEffectExecutorError::RandomFailure;
					return false;
				}
				Intent.BlockReason = Resolution.GetReason();
				if (!Resolution.HasSelection())
				{
					continue;
				}

				FBattleBattlerState* Incoming = FindMutableBattler(
					Resolution.GetSelectedBattlerId());
				if (Incoming == nullptr || Incoming->TrainerId != Trainer->TrainerId)
				{
					OutError = EBattleEffectExecutorError::InvalidTarget;
					return false;
				}
				Intent.OutgoingTarget.TrainerId = Outgoing->TrainerId;
				Intent.OutgoingTarget.BattlerId = Outgoing->BattlerId;
				Intent.OutgoingTarget.ActiveSlotId = Active->ActiveSlotId;
				Intent.IncomingTarget.TrainerId = Incoming->TrainerId;
				Intent.IncomingTarget.BattlerId = Incoming->BattlerId;
				Intent.IncomingTarget.ActiveSlotId = Active->ActiveSlotId;
				Intent.SelectedPartySlotId = Resolution.GetSelectedPartySlotId();
				Intent.IncomingBattlerId = Incoming->BattlerId;

				if (!TryRunSwitchOutStatus(*Outgoing))
				{
					OutError = EBattleEffectExecutorError::InvalidHookResult;
					return false;
				}
				if (!TryCleanupAllOwnedVolatiles(
						*Outgoing,
						EBattleTriggerCleanupReason::Switch)
					|| !TryCleanupSourceDependentVolatiles(
						Outgoing->BattlerId,
						EBattleTriggerCleanupReason::Removal)
					|| !TryCleanupAbilityHooks(
						*Outgoing,
						EBattleTriggerCleanupReason::Switch)
					|| !TryCleanupItemHooks(
						*Outgoing,
						Outgoing->HeldItem.CurrentItemId,
						EBattleTriggerCleanupReason::Switch))
				{
					OutError = EBattleEffectExecutorError::InvalidHookResult;
					return false;
				}
				Outgoing->Stages = FBattleStatStages();
				Outgoing->Volatiles.Reset();
				Outgoing->LastMoveId = FMoveId();
				Outgoing->HeldItem.ChoiceLockedMoveId = FMoveId();
				Outgoing->bAbilitySuppressed = false;
				Outgoing->EnteredActiveOnTurnId = FTurnId();
				Active->BattlerId = Incoming->BattlerId;
				Incoming->bAbilitySuppressed = false;
				Incoming->EnteredActiveOnTurnId = Request.TurnId;
				if (!TryRegisterAbilityHooks(*Incoming, *Active)
					|| !TryResolveHeldItemSwitchIn(*Incoming, *Active)
					|| !TryApplyEntryHazards(*Incoming, *Active))
				{
					OutError = EBattleEffectExecutorError::InvalidHookResult;
					return false;
				}
				Intent.bApplied = true;
			}
			return true;
		}

		bool TryApplyPostMoveLifeOrbRecoil(
			FBattleEffectExecutionResult& Result,
			EBattleEffectExecutorError& OutError)
		{
			FBattleBattlerState* User = FindMutableBattler(Request.UserBattlerId);
			if (User == nullptr)
			{
				OutError = EBattleEffectExecutorError::InvalidTarget;
				return false;
			}
			const FItemId ItemId = User->HeldItem.CurrentItemId;
			if (User->CurrentHP <= 0
				|| User->bFainted
				|| User->bCaptured
				|| User->bRemoved
				|| User->HeldItem.bConsumed
				|| User->HeldItem.bTemporarilyRemoved
				|| ItemId != FBattleItemRules::GetLifeOrbId())
			{
				return true;
			}

			FBattleLifeOrbRecoilFacts Facts;
			Facts.ItemId = ItemId;
			Facts.BaseMaximumHP = User->PermanentStats.MaxHP;
			Facts.bDamagingMove = Request.Move->Category == EBattleMoveCategory::Physical
				|| Request.Move->Category == EBattleMoveCategory::Special;
			Facts.bMoveAffectedTarget = bMoveAffectedDifferentBattler;
			Facts.bSourceAndTargetDiffer = bMoveAffectedDifferentBattler;
			Facts.bForcedSwitchSuppressesRecoil = Result.SwitchIntents.ContainsByPredicate(
				[](const FBattleSwitchEffectIntent& Intent)
				{
					return Intent.Kind == EBattleSwitchKind::Forced && Intent.bApplied;
				});
			Facts.bSuppressed = User->HeldItem.bSuppressed;
			FBattleLifeOrbRecoilResult Recoil;
			if (!FBattleItemRules::TryEvaluateLifeOrbRecoil(Facts, Recoil)
				|| !Recoil.bValid)
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			if (!Recoil.bApplies)
			{
				return true;
			}

			if (FBattleAbilityRules::ShouldMagicGuardPreventDamage(
					User->AbilityId,
					EBattleHPChangeSourceKind::Item,
					User->bAbilitySuppressed))
			{
				FBattleAbilityItemEffectRequest AbilityRequest;
				if (!TryGetAbilityEffectRequest(
						*User,
						EBattleTriggerPhase::AfterDamage,
						EBattleAbilityItemHookPoint::AfterDamage,
						AbilityRequest)
					|| !TryRecordAbilityActivation(
						AbilityRequest,
						EBattleAbilityItemActivationOutcome::Applied,
						*User))
				{
					OutError = EBattleEffectExecutorError::InvalidHookResult;
					return false;
				}
				return true;
			}

			FBattleAbilityItemEffectRequest ItemRequest;
			if (!TryGetItemEffectRequest(
					*User,
					EBattleTriggerPhase::AfterAction,
					EBattleAbilityItemHookPoint::AfterDamage,
					ItemRequest)
				|| !TryRecordItemActivation(
					ItemRequest,
					Recoil.Outcome,
					*User,
					ItemId))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}

			const int32 PreviousHP = User->CurrentHP;
			const int32 AppliedDamage = FMath::Min(PreviousHP, Recoil.RecoilDamage);
			User->CurrentHP -= AppliedDamage;
			if (User->CurrentHP == 0)
			{
				User->bFainted = true;
				User->bFaintTransitionPending = true;
			}
			if (!TryAppendItemMutationEvent(
					EBattleEventType::Damage,
					ItemId,
					*User,
					PreviousHP,
					User->CurrentHP,
					-AppliedDamage)
				|| !TryAppendItemMutationEvent(
					EBattleEventType::HPChanged,
					ItemId,
					*User,
					PreviousHP,
					User->CurrentHP,
					-AppliedDamage))
			{
				OutError = EBattleEffectExecutorError::InvalidHookResult;
				return false;
			}
			return true;
		}

		virtual bool PrevalidateRequest(
			const FBattleEffectExecutionRequest& CandidateRequest) const override
		{
			if (&CandidateRequest != &Request)
			{
				return false;
			}
			const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
			const FBattleActivePositionState* UserPosition = State.FindActivePosition(
				Request.UserSlotId);
			if (User == nullptr
				|| UserPosition == nullptr
				|| !UserPosition->bAvailable
				|| UserPosition->BattlerId != Request.UserBattlerId
				|| User->CurrentHP <= 0
				|| User->bFainted
				|| User->bCaptured
				|| User->bRemoved)
			{
				return false;
			}

			const bool bStruggle = Request.Move->Id
				== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
			const FBattleMoveDefinition* CatalogMove = State.Catalog.FindMove(Request.Move->Id);
			if ((bStruggle && Request.Move != &FBattleBuiltInMoveDefinitions::GetStruggle())
				|| (!bStruggle && CatalogMove != Request.Move))
			{
				return false;
			}

			for (const FBattleResolvedTarget& Target : Request.Targets)
			{
				if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
				{
					const FBattleActivePositionState* Position = State.FindActivePosition(
						Target.GetBattler().ActiveSlotId);
					if (Position == nullptr
						|| !Position->bAvailable
						|| Position->BattlerId != Target.GetBattler().BattlerId
						|| FindBattler(Target.GetBattler().BattlerId) == nullptr)
					{
						return false;
					}
				}
				else if (Target.GetKind() == EBattleResolvedTargetKind::Side)
				{
					if (Sides.FindByPredicate(
						[&Target](const FBattleSideState& Side)
						{
							return Side.Side == Target.GetSide();
						}) == nullptr)
					{
						return false;
					}
				}
				else if (Target.GetKind() != EBattleResolvedTargetKind::Field)
				{
					return false;
				}
			}

			for (const FBattleMoveEffectDescriptor& Effect : Request.Move->Effects)
			{
				const bool bNeedsCondition = RequiresConditionReference(Effect.Kind);
				const FBattleConditionDefinition* Condition = bNeedsCondition
					? State.Catalog.FindCondition(Effect.ConditionId)
					: nullptr;
				if (bNeedsCondition && Condition == nullptr)
				{
					return false;
				}
				if (Effect.Kind == EBattleMoveEffectKind::ApplyCondition
					&& Condition->Kind != EBattleConditionKind::MajorStatus
					&& Condition->Kind != EBattleConditionKind::Volatile)
				{
					return false;
				}
				if (Effect.Kind == EBattleMoveEffectKind::SetFieldCondition
					&& Condition->Kind != EBattleConditionKind::Weather
					&& Condition->Kind != EBattleConditionKind::Terrain
					&& Condition->Kind != EBattleConditionKind::Room)
				{
					return false;
				}
				if (Effect.Kind == EBattleMoveEffectKind::SetSideCondition
					&& Condition->Kind != EBattleConditionKind::Hazard
					&& Condition->Kind != EBattleConditionKind::Screen
					&& Condition->Kind != EBattleConditionKind::SideCondition)
				{
					return false;
				}
				if ((Effect.Kind == EBattleMoveEffectKind::Charge
						|| Effect.Kind == EBattleMoveEffectKind::Recharge
						|| Effect.Kind == EBattleMoveEffectKind::Protect
						|| Effect.Kind == EBattleMoveEffectKind::SemiInvulnerability)
					&& Condition->Kind != EBattleConditionKind::Volatile)
				{
					return false;
				}
				if (Effect.Kind == EBattleMoveEffectKind::RemoveCondition
					&& !IsRemovalTargetCompatible(Effect.Target, Condition->Kind))
				{
					return false;
				}
				if (Effect.Kind == EBattleMoveEffectKind::ChangeItem
					&& State.Catalog.FindItem(Effect.ItemId) == nullptr)
				{
					return false;
				}
			}
			return true;
		}

		virtual FBattleEffectHookResult CheckReachability(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return Applied();
			}
			const FBattleBattlerTarget& BattlerTarget = Target.GetBattler();
			const FBattleActivePositionState* Position = State.FindActivePosition(
				BattlerTarget.ActiveSlotId);
			const FBattleBattlerState* Battler = FindBattler(BattlerTarget.BattlerId);
			if (Position == nullptr
				|| !Position->bAvailable
				|| Position->BattlerId != BattlerTarget.BattlerId
				|| Battler == nullptr
				|| Battler->CurrentHP <= 0
				|| Battler->bFainted
				|| Battler->bCaptured
				|| Battler->bRemoved)
			{
				return Outcome(EBattleEffectExecutionOutcome::Unreachable);
			}
			FBattleFlyReachabilityFacts Facts;
			Facts.bTargetFlySemiInvulnerable = IsVolatileActiveForPhase(
				Battler->BattlerId,
				FBattleVolatileRules::GetFlySemiInvulnerableId(),
				EBattleTriggerPhase::BeforeHit);
			Facts.bMoveReachesFlyTarget = EnumHasAllFlags(
				Move.Flags,
				EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget);
			Facts.bMoveDoublesPowerAgainstFlyTarget = EnumHasAllFlags(
				Move.Flags,
				EBattleMoveFlags::DoublesPowerAgainstAirborneSemiInvulnerableTarget);
			FBattleFlyReachabilityResult Reachability;
			if (!FBattleVolatileRules::TryResolveFlyReachability(Facts, Reachability))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (!Reachability.bReachable)
			{
				return Outcome(EBattleEffectExecutionOutcome::Unreachable);
			}
			return Applied();
		}

		virtual FBattleEffectHookResult CheckProtection(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler
				&& FBattleVolatileRules::ShouldProtectBlockEffect(
					IsVolatileActiveForPhase(
						Target.GetBattler().BattlerId,
						FBattleVolatileRules::GetProtectId(),
						EBattleTriggerPhase::BeforeHit),
					EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BlockedByProtect),
					EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BypassesProtect)
						|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BreaksProtection)))
			{
				return Outcome(EBattleEffectExecutionOutcome::Protected);
			}
			return Applied();
		}

		virtual FBattleEffectHookResult CheckTryHit(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				const FBattleBattlerState* TargetBattler = FindBattler(
					Target.GetBattler().BattlerId);
				bool bTargetGrounded = false;
				bool bLevitateMadeAirborne = false;
				if (TargetBattler == nullptr
					|| !TryIsGrounded(
						*TargetBattler,
						bTargetGrounded,
						ShouldIgnoreLevitateForCurrentMove(*TargetBattler),
						&bLevitateMadeAirborne))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				int32 IntegerPriority = Move.Priority;
				int32 FractionalPriorityTenths = 0;
				const FBattleLockedActionState* LockedAction = State.LockedActions.FindByPredicate(
					[this](const FBattleLockedActionState& Candidate)
					{
						return Candidate.ActionId == Request.ActionId;
					});
				if (LockedAction != nullptr)
				{
					IntegerPriority = LockedAction->OrderKey.MovePriority;
					FractionalPriorityTenths = LockedAction->OrderKey.FractionalPriorityTenths;
				}
				const FConditionId TerrainId = GetTerrainId();
				bool bTerrainTriggerActive = false;
				if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
					&& !TryIsFieldSideConditionActiveForPhase(
						TerrainId,
						TOptional<EBattleSide>(),
						EBattleTriggerPhase::BeforeHit,
						Target.GetBattler().ActiveSlotId,
						bTerrainTriggerActive))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const bool bBlockedByTerrain = bTerrainTriggerActive
					&& FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
						TerrainId,
						Target.GetBattler().ActiveSlotId.GetSide() != Request.UserSlotId.GetSide(),
						bTargetGrounded,
						IntegerPriority,
						FractionalPriorityTenths);
				if (bBlockedByTerrain)
				{
					return Outcome(EBattleEffectExecutionOutcome::Blocked);
				}
				if (bLevitateMadeAirborne
					&& bTerrainTriggerActive
					&& FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
						TerrainId,
						Target.GetBattler().ActiveSlotId.GetSide() != Request.UserSlotId.GetSide(),
						true,
						IntegerPriority,
						FractionalPriorityTenths)
					&& !TryRecordLevitateGroundedActivation(
						*TargetBattler,
						EBattleTriggerPhase::BeforeHit,
						EBattleAbilityItemHookPoint::TypeImmunity))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			return Applied();
		}

		virtual FBattleEffectHookResult CheckMoveImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			(void)Move;
			(void)Target;
			return Applied();
		}

		virtual FBattleEffectHookResult CheckAbilityImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler
				|| Move.Type != EPokemonType::Ground
				|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::TypelessDamage))
			{
				return Applied();
			}

			const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
			const FBattleBattlerState* Defender = FindBattler(
				Target.GetBattler().BattlerId);
			if (User == nullptr || Defender == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Defender->AbilityId != FBattleAbilityRules::GetLevitateId())
			{
				return Applied();
			}

			TArray<FBattleAbilityItemHookDefinition> DefenderHooks;
			if (!FBattleAbilityRules::TryGetHookDefinitionsForPhase(
					Defender->AbilityId,
					EBattleTriggerPhase::BeforeHit,
					DefenderHooks)
				|| DefenderHooks.Num() != 1
				|| DefenderHooks[0].HookPoint
					!= EBattleAbilityItemHookPoint::TypeImmunity)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			const bool bIgnoredForMove =
				FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
					User->AbilityId,
					User->bAbilitySuppressed,
					Defender->AbilityId,
					DefenderHooks[0]);
			if (!FBattleAbilityRules::ShouldLevitatePreventMove(
					Defender->AbilityId,
					Move.Type,
					Defender->bAbilitySuppressed,
					bIgnoredForMove))
			{
				if (bIgnoredForMove)
				{
					FBattleAbilityItemEffectRequest IgnoredRequest;
					if (!TryGetAbilityEffectRequest(
							*Defender,
							EBattleTriggerPhase::BeforeHit,
							EBattleAbilityItemHookPoint::TypeImmunity,
							IgnoredRequest)
						|| !TryRecordAbilityActivation(
							IgnoredRequest,
							EBattleAbilityItemActivationOutcome::Ignored,
							*Defender))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				return Applied();
			}

			FBattleAbilityItemEffectRequest AbilityRequest;
			if (!TryGetAbilityEffectRequest(
					*Defender,
					EBattleTriggerPhase::BeforeHit,
					EBattleAbilityItemHookPoint::TypeImmunity,
					AbilityRequest)
				|| !TryRecordAbilityActivation(
					AbilityRequest,
					EBattleAbilityItemActivationOutcome::Applied,
					*Defender))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			FBattleEffectHookResult Result = Outcome(
				EBattleEffectExecutionOutcome::Immune);
			Result.RuleId = Defender->AbilityId.GetDefinitionId();
			return Result;
		}

		virtual FBattleEffectHookResult CheckItemImmunity(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler
				|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::TypelessDamage))
			{
				return Applied();
			}
			FBattleBattlerState* Defender = FindMutableBattler(
				Target.GetBattler().BattlerId);
			if (Defender == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const FItemId ItemId = Defender->HeldItem.CurrentItemId;
			if (Defender->HeldItem.bConsumed
				|| Defender->HeldItem.bTemporarilyRemoved
				|| !FBattleItemRules::ShouldAirBalloonPreventMove(
					ItemId,
					Move.Type,
					Defender->HeldItem.bSuppressed))
			{
				return Applied();
			}
			FBattleAbilityItemEffectRequest ItemRequest;
			if (!TryGetItemEffectRequest(
					*Defender,
					EBattleTriggerPhase::BeforeHit,
					EBattleAbilityItemHookPoint::TypeImmunity,
					ItemRequest)
				|| !TryRecordItemActivation(
					ItemRequest,
					EBattleAbilityItemActivationOutcome::Applied,
					*Defender,
					ItemId))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectHookResult Result = Outcome(
				EBattleEffectExecutionOutcome::Immune);
			Result.RuleId = ItemId.GetDefinitionId();
			return Result;
		}

		virtual FBattleEffectHookResult ApplyProtectionBreaking(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target) override
		{
			if (!EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BreaksProtection)
				|| Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return Applied();
			}
			FBattleBattlerState* Battler = FindMutableBattler(
				Target.GetBattler().BattlerId);
			if (Battler == nullptr
				|| !HasVolatile(*Battler, FBattleVolatileRules::GetProtectId()))
			{
				return Applied();
			}
			if (!TrySetVolatilePhaseSuppressed(
					Battler->BattlerId,
					FBattleVolatileRules::GetProtectId(),
					EBattleTriggerPhase::BeforeHit,
					true))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (ExecutionResult == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEventTarget EventTarget;
			if (!TryBuildEventTarget(Target, EventTarget))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectExecutionEvent& Event =
				ExecutionResult->Events.AddDefaulted_GetRef();
			Event.Type = EBattleEventType::StatusChanged;
			Event.Cause = EBattleEventCause::Move;
			Event.Outcome = EBattleEffectExecutionOutcome::Applied;
			Event.Targets.Add(MoveTemp(EventTarget));
			Event.NumericBefore = 1;
			Event.NumericAfter = 0;
			Event.NumericDelta = -1;
			FBattleEffectHookResult Result = Applied();
			Result.bStateMutated = true;
			Result.NumericBefore = 1;
			Result.NumericAfter = 0;
			Result.NumericDelta = -1;
			return Result;
		}

		virtual bool TryBuildAccuracyInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleAccuracyCheckInput& OutInput) override
		{
			OutInput = FBattleAccuracyCheckInput();
			const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
			if (User == nullptr)
			{
				return false;
			}
			OutInput.bAlwaysHits = Move.bAlwaysHits;
			OutInput.BaseAccuracy = Move.Accuracy;
			OutInput.AttackerStages = User->Stages;
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				const FBattleBattlerState* TargetBattler = FindBattler(Target.GetBattler().BattlerId);
				if (TargetBattler == nullptr)
				{
					return false;
				}
				OutInput.DefenderStages = TargetBattler->Stages;
			}
			return true;
		}

		virtual bool TryBuildCriticalInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			FBattleCriticalCheckInput& OutInput) override
		{
			(void)Target;
			OutInput = FBattleCriticalCheckInput();
			if (EnumHasAllFlags(Move.Flags, EBattleMoveFlags::AlwaysCritical))
			{
				OutInput.Mode = EBattleCriticalCheckMode::Always;
			}
			else if (EnumHasAllFlags(Move.Flags, EBattleMoveFlags::NeverCritical))
			{
				OutInput.Mode = EBattleCriticalCheckMode::Never;
			}
			else
			{
				OutInput.Mode = EBattleCriticalCheckMode::Standard;
				OutInput.BaseStage = 1;
			}
			return true;
		}

		virtual bool TryBuildDamageInput(
			const FBattleMoveDefinition& Move,
			const FBattleResolvedTarget& Target,
			const bool bSpreadAcrossMultipleTargets,
			FBattleFinalDamageInput& OutInput) override
		{
			OutInput = FBattleFinalDamageInput();
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return false;
			}
			const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
			const FBattleBattlerState* TargetBattler = FindBattler(Target.GetBattler().BattlerId);
			const FBattleSpeciesFormDefinition* UserSpecies = User != nullptr
				? State.Catalog.FindSpeciesForm(User->SpeciesFormId)
				: nullptr;
			const FBattleSpeciesFormDefinition* TargetSpecies = TargetBattler != nullptr
				? State.Catalog.FindSpeciesForm(TargetBattler->SpeciesFormId)
				: nullptr;
			if (User == nullptr || TargetBattler == nullptr
				|| UserSpecies == nullptr || TargetSpecies == nullptr)
			{
				return false;
			}
			int32& DamageInputBuildCount = DamageInputBuildCounts.FindOrAdd(
				Target.GetBattler().BattlerId);
			++DamageInputBuildCount;
			// The first build is the pre-accuracy type-immunity probe. Every later
			// build is an actual per-hit BeforeDamage checkpoint.
			const bool bActualDamageBuild = DamageInputBuildCount > 1;

			OutInput.AttackerLevel = User->Level;
			OutInput.AttackerStats = User->PermanentStats;
			OutInput.DefenderStats = TargetBattler->PermanentStats;
			OutInput.AttackerStages = User->Stages;
			OutInput.DefenderStages = TargetBattler->Stages;
			bool bWonderRoomTriggerActive = false;
			if (!TryIsFieldSideConditionActiveForPhase(
					FBattleFieldSideConditionRules::GetWonderRoomId(),
					TOptional<EBattleSide>(),
					EBattleTriggerPhase::BeforeDamage,
					Target.GetBattler().ActiveSlotId,
					bWonderRoomTriggerActive))
			{
				return false;
			}
			if (bWonderRoomTriggerActive)
			{
				Swap(OutInput.DefenderStats.Defense, OutInput.DefenderStats.SpecialDefense);
				int32 DefenseStage = 0;
				int32 SpecialDefenseStage = 0;
				if (!OutInput.DefenderStages.TryGetStage(EBattleStat::Defense, DefenseStage)
					|| !OutInput.DefenderStages.TryGetStage(
						EBattleStat::SpecialDefense,
						SpecialDefenseStage))
				{
					return false;
				}
				if (DefenseStage != SpecialDefenseStage)
				{
					const FBattleStatStageChangeResult DefenseSwap =
						OutInput.DefenderStages.ApplyChange(
							EBattleStat::Defense,
							SpecialDefenseStage - DefenseStage);
					const FBattleStatStageChangeResult SpecialDefenseSwap =
						OutInput.DefenderStages.ApplyChange(
							EBattleStat::SpecialDefense,
							DefenseStage - SpecialDefenseStage);
					if (DefenseSwap.Outcome != EBattleStatStageChangeOutcome::Applied
						|| SpecialDefenseSwap.Outcome != EBattleStatStageChangeOutcome::Applied)
					{
						return false;
					}
				}
			}
			OutInput.MoveCategory = Move.Category;
			OutInput.MovePower = Move.Power;
			if (IsVolatileActiveForPhase(
					TargetBattler->BattlerId,
					FBattleVolatileRules::GetFlySemiInvulnerableId(),
					EBattleTriggerPhase::BeforeHit)
				&& EnumHasAllFlags(
					Move.Flags,
					EBattleMoveFlags::DoublesPowerAgainstAirborneSemiInvulnerableTarget))
			{
				if (OutInput.MovePower > TNumericLimits<int32>::Max() / 2)
				{
					return false;
				}
				OutInput.MovePower *= 2;
			}
			OutInput.bSpreadAcrossMultipleTargets = bSpreadAcrossMultipleTargets;
			OutInput.bBypassTypeImmunity = EnumHasAllFlags(
				Move.Flags,
				EBattleMoveFlags::TypelessDamage);
			OutInput.WeatherModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
			OutInput.StabModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
			OutInput.TypeEffectiveness = {1, 1};

			FBattleAbilityOffensiveStatFacts AbilityFacts;
			AbilityFacts.AbilityId = User->AbilityId;
			AbilityFacts.MoveType = Move.Type;
			AbilityFacts.CurrentHP = User->CurrentHP;
			AbilityFacts.BaseMaximumHP = User->PermanentStats.MaxHP;
			AbilityFacts.bSuppressed = User->bAbilitySuppressed;
			FBattleAbilityOffensiveStatResult AbilityResult;
			const EBattleAbilityKind UserAbilityKind = FBattleAbilityRules::GetKind(
				User->AbilityId);
			if ((UserAbilityKind == EBattleAbilityKind::Blaze
					|| UserAbilityKind == EBattleAbilityKind::Overgrow)
				&& (!FBattleAbilityRules::TryEvaluateOffensiveStatModifier(
						AbilityFacts,
						AbilityResult)
					|| !AbilityResult.bValid))
			{
				return false;
			}
			if (AbilityResult.bApplies)
			{
				OutInput.OffensiveStatModifiers.Add({
					User->AbilityId.GetDefinitionId(),
					AbilityResult.ModifierQ12,
					false});
				if (bActualDamageBuild)
				{
					FBattleAbilityItemEffectRequest AbilityRequest;
					if (!TryGetAbilityEffectRequest(
							*User,
							EBattleTriggerPhase::BeforeDamage,
							EBattleAbilityItemHookPoint::OffensiveStat,
							AbilityRequest)
						|| !TryRecordAbilityActivation(
							AbilityRequest,
							AbilityResult.Outcome,
							*User))
					{
						return false;
					}
				}
			}

			const EBattleHeldItemRuleKind ItemKind = User->HeldItem.bConsumed
					|| User->HeldItem.bTemporarilyRemoved
				? EBattleHeldItemRuleKind::None
				: FBattleItemRules::GetKind(User->HeldItem.CurrentItemId);
			if (ItemKind == EBattleHeldItemRuleKind::LifeOrb
				|| ItemKind == EBattleHeldItemRuleKind::ChoiceBand)
			{
				FBattleItemDamageModifierFacts ItemFacts;
				ItemFacts.ItemId = User->HeldItem.CurrentItemId;
				ItemFacts.MoveCategory = Move.Category;
				ItemFacts.bDamagingMove = Move.Category == EBattleMoveCategory::Physical
					|| Move.Category == EBattleMoveCategory::Special;
				ItemFacts.bSuppressed = User->HeldItem.bSuppressed;
				FBattleItemDamageModifierResult ItemResult;
				if (!FBattleItemRules::TryEvaluateDamageModifier(ItemFacts, ItemResult)
					|| !ItemResult.bValid)
				{
					return false;
				}
				if (ItemResult.bApplies)
				{
					FBattleDamageModifier Modifier{
						User->HeldItem.CurrentItemId.GetDefinitionId(),
						ItemResult.ModifierQ12,
						false};
					if (ItemKind == EBattleHeldItemRuleKind::ChoiceBand)
					{
						OutInput.OffensiveStatModifiers.Add(Modifier);
					}
					else
					{
						OutInput.FinalModifiers.Add(Modifier);
					}
					if (bActualDamageBuild)
					{
						FBattleBattlerState* MutableUser = FindMutableBattler(
							User->BattlerId);
						FBattleAbilityItemEffectRequest ItemRequest;
						const EBattleAbilityItemHookPoint HookPoint =
							ItemKind == EBattleHeldItemRuleKind::ChoiceBand
								? EBattleAbilityItemHookPoint::OffensiveStat
								: EBattleAbilityItemHookPoint::FinalDamage;
						if (MutableUser == nullptr
							|| !TryGetItemEffectRequest(
								*MutableUser,
								EBattleTriggerPhase::BeforeDamage,
								HookPoint,
								ItemRequest)
							|| !TryRecordItemActivation(
								ItemRequest,
								ItemResult.Outcome,
								*MutableUser,
								ItemFacts.ItemId))
						{
							return false;
						}
					}
				}
			}

			bool bAttackerGrounded = false;
			bool bDefenderGrounded = false;
			bool bAttackerLevitateMadeAirborne = false;
			bool bDefenderLevitateMadeAirborne = false;
			if (!TryIsGrounded(
					*User,
					bAttackerGrounded,
					false,
					&bAttackerLevitateMadeAirborne)
				|| !TryIsGrounded(
					*TargetBattler,
					bDefenderGrounded,
					ShouldIgnoreLevitateForCurrentMove(*TargetBattler),
					&bDefenderLevitateMadeAirborne))
			{
				return false;
			}
			const FConditionId WeatherId = GetWeatherId();
			bool bWeatherTriggerActive = false;
			if (WeatherId.IsValid()
				&& FBattleFieldSideConditionRules::IsCanonical(WeatherId))
			{
				if (!TryIsFieldSideConditionActiveForPhase(
						WeatherId,
						TOptional<EBattleSide>(),
						EBattleTriggerPhase::BeforeDamage,
						Target.GetBattler().ActiveSlotId,
						bWeatherTriggerActive))
				{
					return false;
				}
			}
			if (bWeatherTriggerActive)
			{
				if (!FBattleFieldSideConditionRules::TryGetWeatherDamageModifierQ12(
					WeatherId,
					Move.Type,
					OutInput.WeatherModifierQ12))
				{
					return false;
				}
				int32 DefensiveModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
				if (!FBattleFieldSideConditionRules::TryGetWeatherDirectDefensiveModifierQ12(
						WeatherId,
						TargetSpecies->PrimaryType,
						TargetSpecies->SecondaryType,
						Move.Category,
						DefensiveModifierQ12))
				{
					return false;
				}
				if (DefensiveModifierQ12 != FBattleFinalDamageCalculator::Q12Neutral)
				{
					OutInput.DirectDefensiveStatModifiers.Add({
						WeatherId.GetDefinitionId(),
						DefensiveModifierQ12,
						false});
				}
			}

			const FConditionId TerrainId = GetTerrainId();
			bool bTerrainTriggerActive = false;
			if (TerrainId.IsValid()
				&& FBattleFieldSideConditionRules::IsCanonical(TerrainId))
			{
				if (!TryIsFieldSideConditionActiveForPhase(
						TerrainId,
						TOptional<EBattleSide>(),
						EBattleTriggerPhase::BeforeDamage,
						Target.GetBattler().ActiveSlotId,
						bTerrainTriggerActive))
				{
					return false;
				}
			}
			if (bTerrainTriggerActive)
			{
				bool bAttackerTerrainEffectSkippedByLevitate = false;
				bool bDefenderTerrainEffectSkippedByLevitate = false;
				int32 PowerModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
				if (!FBattleFieldSideConditionRules::TryGetTerrainPowerModifierQ12(
						TerrainId,
						Move.Type,
						bAttackerGrounded,
						PowerModifierQ12))
				{
					return false;
				}
				if (PowerModifierQ12 != FBattleFinalDamageCalculator::Q12Neutral)
				{
					OutInput.PowerModifiers.Add({
						TerrainId.GetDefinitionId(),
						PowerModifierQ12,
						false});
				}
				if (bAttackerLevitateMadeAirborne)
				{
					int32 GroundedPowerModifierQ12 =
						FBattleFinalDamageCalculator::Q12Neutral;
					if (!FBattleFieldSideConditionRules::TryGetTerrainPowerModifierQ12(
							TerrainId,
							Move.Type,
							true,
							GroundedPowerModifierQ12))
					{
						return false;
					}
					bAttackerTerrainEffectSkippedByLevitate =
						GroundedPowerModifierQ12 != PowerModifierQ12;
				}
				int32 TerrainDamageModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
				if (!FBattleFieldSideConditionRules::TryGetTerrainFinalDamageModifierQ12(
						TerrainId,
						Move.Type,
						bDefenderGrounded,
						EnumHasAllFlags(Move.Flags, EBattleMoveFlags::ReducedByGrassyTerrain),
						TerrainDamageModifierQ12))
				{
					return false;
				}
				if (TerrainDamageModifierQ12 != FBattleFinalDamageCalculator::Q12Neutral)
				{
					OutInput.FinalModifiers.Add({
						TerrainId.GetDefinitionId(),
						TerrainDamageModifierQ12,
						false});
				}
				if (bDefenderLevitateMadeAirborne)
				{
					int32 GroundedDamageModifierQ12 =
						FBattleFinalDamageCalculator::Q12Neutral;
					if (!FBattleFieldSideConditionRules::TryGetTerrainFinalDamageModifierQ12(
							TerrainId,
							Move.Type,
							true,
							EnumHasAllFlags(
								Move.Flags,
								EBattleMoveFlags::ReducedByGrassyTerrain),
							GroundedDamageModifierQ12))
					{
						return false;
					}
					bDefenderTerrainEffectSkippedByLevitate =
						GroundedDamageModifierQ12 != TerrainDamageModifierQ12;
				}
				if (bActualDamageBuild
					&& bAttackerTerrainEffectSkippedByLevitate
					&& !TryRecordLevitateGroundedActivation(
						*User,
						EBattleTriggerPhase::BeforeHit,
						EBattleAbilityItemHookPoint::TypeImmunity))
				{
					return false;
				}
				if (bActualDamageBuild
					&& bDefenderTerrainEffectSkippedByLevitate
					&& !TryRecordLevitateGroundedActivation(
						*TargetBattler,
						EBattleTriggerPhase::BeforeHit,
						EBattleAbilityItemHookPoint::TypeImmunity))
				{
					return false;
				}
			}

			int32 ScreenModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
			TArray<FConditionId> DefenderSideConditions;
			for (const FConditionId& ScreenId : {
				FBattleFieldSideConditionRules::GetReflectId(),
				FBattleFieldSideConditionRules::GetLightScreenId(),
				FBattleFieldSideConditionRules::GetAuroraVeilId()})
			{
				bool bScreenTriggerActive = false;
				if (!TryIsFieldSideConditionActiveForPhase(
						ScreenId,
						Target.GetBattler().ActiveSlotId.GetSide(),
						EBattleTriggerPhase::BeforeDamage,
						Target.GetBattler().ActiveSlotId,
						bScreenTriggerActive))
				{
					return false;
				}
				if (bScreenTriggerActive)
				{
					DefenderSideConditions.Add(ScreenId);
				}
			}
			if (!FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
					DefenderSideConditions,
					Move.Category,
					State.Format != EBattleFormat::Single,
					false,
					EnumHasAllFlags(Move.Flags, EBattleMoveFlags::BypassesSideProtection),
					ScreenModifierQ12))
			{
				return false;
			}
			if (ScreenModifierQ12 != FBattleFinalDamageCalculator::Q12Neutral)
			{
				OutInput.FinalModifiers.Add({
					MakeRuleId(TEXT("Rule.C07D.Screen")),
					ScreenModifierQ12,
					true});
			}
			bool bBurnPenalty = FBattleMajorStatusRules::ShouldApplyBurnPhysicalPenalty(
				User->MajorStatusId,
				Move.Category,
				false);
			if (bBurnPenalty)
			{
				if (bActualDamageBuild)
				{
					bool bEmitted = false;
					if (!TryDispatchStatusPhase(
						*User,
						EBattleTriggerPhase::BeforeDamage,
						bEmitted))
					{
						return false;
					}
					bBurnPenalty = bEmitted;
				}
			}
			OutInput.bAttackerBurned = bBurnPenalty;
			OutInput.bBypassBurnPenalty = false;

			if (!OutInput.bBypassTypeImmunity)
			{
				if (UserSpecies->PrimaryType == Move.Type || UserSpecies->SecondaryType == Move.Type)
				{
					OutInput.StabModifierQ12 = 6144;
				}
				const bool bTypeFound = TargetSpecies->SecondaryType == EPokemonType::Invalid
					? State.Catalog.GetTypeChart().TryGetEffectiveness(
						Move.Type,
						TargetSpecies->PrimaryType,
						OutInput.TypeEffectiveness)
					: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
						Move.Type,
						TargetSpecies->PrimaryType,
						TargetSpecies->SecondaryType,
						OutInput.TypeEffectiveness);
				if (!bTypeFound)
				{
					return false;
				}
			}
			return true;
		}

		virtual void SetDirectMoveDamageHit(const bool bActive) override
		{
			bApplyingDirectMoveDamageHit = bActive;
		}

		virtual bool IsRuntimeValid() const override
		{
			return bRuntimeValid;
		}

		virtual bool IsSourceAbleToContinue() const override
		{
			const FBattleBattlerState* Source = FindBattler(Request.UserBattlerId);
			return Source != nullptr
				&& Source->CurrentHP > 0
				&& !Source->bFainted
				&& !Source->bCaptured
				&& !Source->bRemoved;
		}

		virtual bool IsTargetAbleToContinue(const FBattleResolvedTarget& Target) const override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return Target.IsValid();
			}
			const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
			return Battler != nullptr
				&& Battler->CurrentHP > 0
				&& !Battler->bFainted
				&& !Battler->bCaptured
				&& !Battler->bRemoved;
		}

		virtual bool ShouldSkipEffectDescriptor(
			const FBattleMoveEffectDescriptor& Effect) const override
		{
			if (Effect.Kind != EBattleMoveEffectKind::Charge
				&& Effect.Kind != EBattleMoveEffectKind::SemiInvulnerability)
			{
				return false;
			}
			FMoveId LockedMoveId;
			return TryGetVolatilePayloadMoveId(
				Request.UserBattlerId,
				FBattleVolatileRules::GetChargingId(),
				LockedMoveId)
				&& LockedMoveId == Request.Move->Id;
		}

		virtual FBattleEffectHookResult CheckEffectEligibility(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			if (!Target.IsValid())
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
				if (Battler == nullptr
					|| Battler->CurrentHP <= 0
					|| Battler->bFainted
					|| Battler->bCaptured
					|| Battler->bRemoved)
				{
					return Outcome(EBattleEffectExecutionOutcome::Prevented);
				}
				const bool bEffectFromOpponent = Target.GetBattler().ActiveSlotId.GetSide()
					!= Request.UserSlotId.GetSide();
				const bool bBypassesSubstitute = EnumHasAllFlags(
					Request.Move->Flags,
					EBattleMoveFlags::BypassesSubstitute)
					|| EnumHasAllFlags(
						Effect.Flags,
						EBattleMoveEffectFlags::BypassesSubstitute);
				if (FBattleVolatileRules::ShouldSubstituteBlockEffect(
						HasVolatile(*Battler, FBattleVolatileRules::GetSubstituteId())
							|| SubstituteProtectedTargets.Contains(Battler->BattlerId),
						bEffectFromOpponent,
						bBypassesSubstitute))
				{
					return Outcome(EBattleEffectExecutionOutcome::Blocked);
				}
				if ((Effect.Kind == EBattleMoveEffectKind::Heal
						|| Effect.Kind == EBattleMoveEffectKind::Drain)
					&& Battler->CurrentHP >= Battler->PermanentStats.MaxHP)
				{
					FBattleEffectHookResult Result = Outcome(EBattleEffectExecutionOutcome::Capped);
					Result.NumericBefore = Battler->CurrentHP;
					Result.NumericAfter = Battler->CurrentHP;
					Result.NumericDelta = 0;
					Result.bCapped = true;
					return Result;
				}
				if (Effect.Kind == EBattleMoveEffectKind::ApplyCondition)
				{
					const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
						Effect.ConditionId);
					if (Definition == nullptr)
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					if (Definition->Kind == EBattleConditionKind::MajorStatus
						&& FBattleMajorStatusRules::IsCanonical(Effect.ConditionId))
					{
						const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
							Battler->SpeciesFormId);
						if (Species == nullptr)
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						FBattleMajorStatusApplicationFacts Facts;
						Facts.RequestedStatusId = Effect.ConditionId;
						Facts.ExistingMajorStatusId = Battler->MajorStatusId;
						Facts.PrimaryType = Species->PrimaryType;
						Facts.SecondaryType = Species->SecondaryType;
						bool bTargetGrounded = false;
						bool bLevitateMadeAirborne = false;
						if (!TryIsGrounded(
							*Battler,
							bTargetGrounded,
							ShouldIgnoreLevitateForCurrentMove(*Battler),
							&bLevitateMadeAirborne))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						const bool bAppliedByOpponent =
							Target.GetBattler().ActiveSlotId.GetSide()
							!= Request.UserSlotId.GetSide();
						const FConditionId WeatherId = GetWeatherId();
						bool bWeatherTriggerActive = false;
						if (FBattleFieldSideConditionRules::IsCanonical(WeatherId)
							&& !TryIsFieldSideConditionActiveForPhase(
								WeatherId,
								TOptional<EBattleSide>(),
								EBattleTriggerPhase::BeforeHit,
								Target.GetBattler().ActiveSlotId,
								bWeatherTriggerActive))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						Facts.Prevention.bSunActive =
							bWeatherTriggerActive
							&& FBattleFieldSideConditionRules::ShouldSunPreventFreeze(WeatherId);
						const FConditionId TerrainId = GetTerrainId();
						bool bTerrainTriggerActive = false;
						if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
							&& !TryIsFieldSideConditionActiveForPhase(
								TerrainId,
								TOptional<EBattleSide>(),
								EBattleTriggerPhase::BeforeHit,
								Target.GetBattler().ActiveSlotId,
								bTerrainTriggerActive))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						Facts.Prevention.bTerrainPrevents =
							bTerrainTriggerActive
							&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
								TerrainId,
								Effect.ConditionId,
								bTargetGrounded);
						bool bSafeguardTriggerActive = false;
						if (!TryIsFieldSideConditionActiveForPhase(
								FBattleFieldSideConditionRules::GetSafeguardId(),
								Target.GetBattler().ActiveSlotId.GetSide(),
								EBattleTriggerPhase::BeforeHit,
								Target.GetBattler().ActiveSlotId,
								bSafeguardTriggerActive))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						Facts.Prevention.bSafeguardPrevents =
							FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
								bSafeguardTriggerActive,
								bAppliedByOpponent,
								EnumHasAllFlags(
									Request.Move->Flags,
									EBattleMoveFlags::BypassesSideProtection));
						FBattleMajorStatusApplicationResult Application;
						if (!FBattleMajorStatusRules::TryEvaluateApplication(Facts, Application))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						if (Application.Outcome == EBattleMajorStatusApplicationOutcome::TypeImmune)
						{
							return Outcome(EBattleEffectExecutionOutcome::Immune);
						}
						if (Application.Outcome == EBattleMajorStatusApplicationOutcome::Prevented)
						{
							return Outcome(EBattleEffectExecutionOutcome::Prevented);
						}
						if (Application.Outcome
							!= EBattleMajorStatusApplicationOutcome::CanApply)
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						if (bLevitateMadeAirborne
							&& bTerrainTriggerActive
							&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
								TerrainId,
								Effect.ConditionId,
								true)
							&& !TryRecordLevitateGroundedActivation(
								*Battler,
								EBattleTriggerPhase::BeforeHit,
								EBattleAbilityItemHookPoint::TypeImmunity))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
					}
					if ((Definition->Kind == EBattleConditionKind::MajorStatus
							&& Battler->MajorStatusId.IsValid())
						|| (Definition->Kind == EBattleConditionKind::Volatile
							&& Battler->Volatiles.ContainsByPredicate(
								[&Effect](const FBattleConditionState& Existing)
								{
									return Existing.ConditionId == Effect.ConditionId;
								})))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				if (Effect.Kind == EBattleMoveEffectKind::ModifyStatStage)
				{
					const bool bAppliedByOpponent =
						Target.GetBattler().ActiveSlotId.GetSide()
						!= Request.UserSlotId.GetSide();
					bool bMistTriggerActive = false;
					if (!TryIsFieldSideConditionActiveForPhase(
							FBattleFieldSideConditionRules::GetMistId(),
							Target.GetBattler().ActiveSlotId.GetSide(),
							EBattleTriggerPhase::BeforeHit,
							Target.GetBattler().ActiveSlotId,
							bMistTriggerActive))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					if (FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
							bMistTriggerActive,
							bAppliedByOpponent,
							EnumHasAllFlags(
								Request.Move->Flags,
								EBattleMoveFlags::BypassesSideProtection),
							Effect.MagnitudeNumerator))
					{
						return Outcome(EBattleEffectExecutionOutcome::Prevented);
					}
					int32 Stage = 0;
					if (!Battler->Stages.TryGetStage(Effect.Stat, Stage))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					if ((Effect.MagnitudeNumerator > 0 && Stage >= FBattleStatStages::MaximumStage)
						|| (Effect.MagnitudeNumerator < 0 && Stage <= FBattleStatStages::MinimumStage))
					{
						FBattleEffectHookResult Result = Outcome(EBattleEffectExecutionOutcome::Capped);
						Result.NumericBefore = Stage;
						Result.NumericAfter = Stage;
						Result.NumericDelta = 0;
						Result.bCapped = true;
						return Result;
					}
				}
				if (Effect.Kind == EBattleMoveEffectKind::RemoveCondition)
				{
					const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
						Effect.ConditionId);
					const bool bPresent = Definition != nullptr
						&& ((Definition->Kind == EBattleConditionKind::MajorStatus
								&& Battler->MajorStatusId == Effect.ConditionId)
							|| (Definition->Kind == EBattleConditionKind::Volatile
								&& Battler->Volatiles.ContainsByPredicate(
									[&Effect](const FBattleConditionState& Existing)
									{
										return Existing.ConditionId == Effect.ConditionId;
									})));
					if (!bPresent)
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				return Applied();
			}

			if (Target.GetKind() == EBattleResolvedTargetKind::Side)
			{
				const FBattleSideState* Side = Sides.FindByPredicate(
					[&Target](const FBattleSideState& Entry)
					{
						return Entry.Side == Target.GetSide();
					});
				if (Side == nullptr)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Effect.Kind == EBattleMoveEffectKind::SetSideCondition
					&& FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
				{
					const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
						Effect.ConditionId);
					if (Definition == nullptr
						|| Definition->Kind
							!= FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					const TArray<FBattleConditionState>& Collection =
						Definition->Kind == EBattleConditionKind::Hazard
						? Side->Hazards
						: Side->Conditions;
					const FBattleConditionState* Existing = Collection.FindByPredicate(
						[&Effect](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId == Effect.ConditionId;
						});
					FBattleFieldSideApplicationFacts Facts;
					Facts.RequestedConditionId = Effect.ConditionId;
					Facts.bRequestedAlreadyActive = Existing != nullptr;
					Facts.ExistingLayers = Existing != nullptr ? Existing->LayerCount : 0;
					Facts.bSnowActive = GetWeatherId()
						== FBattleFieldSideConditionRules::GetSnowId();
					Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
					FBattleFieldSideApplicationResult Application;
					if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					return Application.Outcome == EBattleFieldSideApplicationOutcome::Create
						|| Application.Outcome == EBattleFieldSideApplicationOutcome::AddLayer
						? Applied()
						: Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const bool bPresent = Side->Conditions.ContainsByPredicate(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					}) || Side->Hazards.ContainsByPredicate(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					});
				if ((Effect.Kind == EBattleMoveEffectKind::SetSideCondition && bPresent)
					|| (Effect.Kind == EBattleMoveEffectKind::RemoveCondition && !bPresent))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				return Applied();
			}

			if (Target.GetKind() == EBattleResolvedTargetKind::Field)
			{
				if (Effect.Kind == EBattleMoveEffectKind::SetFieldCondition
					&& FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
				{
					const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
						Effect.ConditionId);
					const EBattleConditionKind Family =
						FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId);
					if (Definition == nullptr || Definition->Kind != Family)
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					FBattleFieldSideApplicationFacts Facts;
					Facts.RequestedConditionId = Effect.ConditionId;
					if (Family == EBattleConditionKind::Weather && Field.Weather.IsSet())
					{
						Facts.ExistingExclusiveConditionId = Field.Weather.GetValue().ConditionId;
					}
					else if (Family == EBattleConditionKind::Terrain && Field.Terrain.IsSet())
					{
						Facts.ExistingExclusiveConditionId = Field.Terrain.GetValue().ConditionId;
					}
					Facts.bRequestedAlreadyActive =
						(Family == EBattleConditionKind::Weather
							&& Field.Weather.IsSet()
							&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
						|| (Family == EBattleConditionKind::Terrain
							&& Field.Terrain.IsSet()
							&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
						|| (Family == EBattleConditionKind::Room && HasRoom(Effect.ConditionId));
					Facts.bSnowActive = GetWeatherId()
						== FBattleFieldSideConditionRules::GetSnowId();
					Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
					FBattleFieldSideApplicationResult Application;
					if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					return Application.Outcome == EBattleFieldSideApplicationOutcome::Create
						|| Application.Outcome == EBattleFieldSideApplicationOutcome::ReplaceExclusive
						|| Application.Outcome == EBattleFieldSideApplicationOutcome::ToggleOff
						? Applied()
						: Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const bool bPresent = (Field.Weather.IsSet()
						&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
					|| (Field.Terrain.IsSet()
						&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
					|| Field.Rooms.ContainsByPredicate(
						[&Effect](const FBattleConditionState& Existing)
						{
							return Existing.ConditionId == Effect.ConditionId;
						})
					|| Field.Effects.ContainsByPredicate(
						[&Effect](const FBattleConditionState& Existing)
						{
							return Existing.ConditionId == Effect.ConditionId;
						});
				if ((Effect.Kind == EBattleMoveEffectKind::SetFieldCondition && bPresent)
					|| (Effect.Kind == EBattleMoveEffectKind::RemoveCondition && !bPresent))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				return Applied();
			}
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		virtual bool TryGetHp(
			const FBattleResolvedTarget& Target,
			int32& OutCurrentHP,
			int32& OutMaximumHP) const override
		{
			OutCurrentHP = 0;
			OutMaximumHP = 0;
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return false;
			}
			const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
			if (Battler == nullptr)
			{
				return false;
			}
			OutCurrentHP = Battler->CurrentHP;
			OutMaximumHP = Battler->PermanentStats.MaxHP;
			return OutMaximumHP > 0 && OutCurrentHP >= 0 && OutCurrentHP <= OutMaximumHP;
		}

		virtual FBattleEffectHookResult ApplyHpDelta(
			const FBattleResolvedTarget& Target,
			const int32 RequestedDelta) override
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
			if (Battler == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectHookResult Result;
			Result.NumericBefore = Battler->CurrentHP;
			if (RequestedDelta > 0)
			{
				if (Battler->CurrentHP <= 0 || Battler->bFainted)
				{
					Result.Outcome = EBattleEffectExecutionOutcome::Prevented;
					Result.NumericAfter = Battler->CurrentHP;
					Result.NumericDelta = 0;
					return Result;
				}
				const int32 Missing = Battler->PermanentStats.MaxHP - Battler->CurrentHP;
				if (Missing <= 0)
				{
					Result.Outcome = EBattleEffectExecutionOutcome::Capped;
					Result.bCapped = true;
					Result.NumericAfter = Battler->CurrentHP;
					Result.NumericDelta = 0;
					return Result;
				}
				const int32 AppliedDelta = FMath::Min(RequestedDelta, Missing);
				Battler->CurrentHP += AppliedDelta;
				Result.Outcome = EBattleEffectExecutionOutcome::Applied;
				Result.bStateMutated = AppliedDelta != 0;
				Result.bCapped = AppliedDelta < RequestedDelta;
				Result.NumericAfter = Battler->CurrentHP;
				Result.NumericDelta = AppliedDelta;
				return Result;
			}

			const int64 RequestedDamage = -static_cast<int64>(RequestedDelta);
			if (Battler->CurrentHP <= 0 || Battler->bFainted)
			{
				Result.Outcome = EBattleEffectExecutionOutcome::Prevented;
				Result.NumericAfter = Battler->CurrentHP;
				Result.NumericDelta = 0;
				return Result;
			}
			FBattleConditionState* Substitute = FindMutableVolatile(
				*Battler,
				FBattleVolatileRules::GetSubstituteId());
			const bool bOpposingMoveDamage = Target.GetBattler().ActiveSlotId.GetSide()
				!= Request.UserSlotId.GetSide();
			const bool bBypassesSubstitute = EnumHasAllFlags(
				Request.Move->Flags,
				EBattleMoveFlags::BypassesSubstitute);
			if (Substitute != nullptr && bOpposingMoveDamage && !bBypassesSubstitute)
			{
				FBattleSubstituteDamageFacts Facts;
				Facts.SubstituteHP = Substitute->LayerCount;
				Facts.OwnerCurrentHP = Battler->CurrentHP;
				Facts.IncomingDamage = RequestedDamage > TNumericLimits<int32>::Max()
					? TNumericLimits<int32>::Max()
					: static_cast<int32>(RequestedDamage);
				FBattleSubstituteDamageResult Routed;
				if (!FBattleVolatileRules::TryResolveSubstituteDamage(Facts, Routed))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				SubstituteProtectedTargets.Add(Battler->BattlerId);
				Result.Outcome = EBattleEffectExecutionOutcome::Applied;
				Result.NumericBefore = Facts.SubstituteHP;
				Result.NumericAfter = Routed.RemainingSubstituteHP;
				Result.NumericDelta = -static_cast<int64>(Routed.DamageToSubstitute);
				Result.bStateMutated = Routed.DamageToSubstitute > 0;
				Result.bAffectsSubstitute = true;
				Result.bSubstituteBroken = Routed.bBrokeSubstitute;
				if (Routed.bBrokeSubstitute)
				{
					if (!TryCleanupVolatile(
							*Battler,
							FBattleVolatileRules::GetSubstituteId(),
							EBattleTriggerCleanupReason::Removal))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Battler->Volatiles.RemoveAll(
						[](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId
								== FBattleVolatileRules::GetSubstituteId();
						});
				}
				else if (!TrySetVolatileLayers(
					Battler->BattlerId,
					FBattleVolatileRules::GetSubstituteId(),
					Routed.RemainingSubstituteHP))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				else
				{
					Substitute->LayerCount = Routed.RemainingSubstituteHP;
				}
				if (bApplyingDirectMoveDamageHit && Result.bStateMutated)
				{
					PendingDamagingHitConnections.Add(Battler->BattlerId);
					bMoveAffectedDifferentBattler = bMoveAffectedDifferentBattler
						|| Battler->BattlerId != Request.UserBattlerId;
				}
				return Result;
			}
			int32 AdjustedDamage = static_cast<int32>(FMath::Min<int64>(
				RequestedDamage,
				TNumericLimits<int32>::Max()));
			const FItemId ItemId = Battler->HeldItem.CurrentItemId;
			if (!Battler->HeldItem.bConsumed
				&& !Battler->HeldItem.bTemporarilyRemoved
				&& ItemId == FBattleItemRules::GetFocusSashId())
			{
				FBattleFocusSashFacts Facts;
				Facts.ItemId = ItemId;
				Facts.CurrentHP = Battler->CurrentHP;
				Facts.BaseMaximumHP = Battler->PermanentStats.MaxHP;
				Facts.IncomingDamage = AdjustedDamage;
				Facts.bDirectMoveDamage = bApplyingDirectMoveDamageHit;
				Facts.bDamageTargetsSubstitute = false;
				Facts.bSuppressed = Battler->HeldItem.bSuppressed;
				FBattleFocusSashResult Sash;
				if (!FBattleItemRules::TryEvaluateFocusSash(Facts, Sash) || !Sash.bValid)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Sash.bApplies)
				{
					FBattleAbilityItemEffectRequest ItemRequest;
					if (!Sash.bConsumesItem
						|| !TryGetItemEffectRequest(
							*Battler,
							EBattleTriggerPhase::BeforeDamage,
							EBattleAbilityItemHookPoint::FaintPrevention,
							ItemRequest)
						|| !TryRecordItemActivation(
							ItemRequest,
							Sash.Outcome,
							*Battler,
							ItemId)
						|| !TryConsumeHeldItem(*Battler, ItemId)
						|| !TryAppendItemMutationEvent(
							EBattleEventType::ItemConsumed,
							ItemId,
							*Battler,
							1,
							0,
							-1))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					AdjustedDamage = Sash.AdjustedDamage;
				}
			}
			const int32 AppliedDamage = FMath::Min(
				AdjustedDamage,
				Battler->CurrentHP);
			Battler->CurrentHP -= AppliedDamage;
			if (Battler->CurrentHP == 0)
			{
				Battler->bFainted = true;
				Battler->bFaintTransitionPending = true;
			}
			Result.Outcome = EBattleEffectExecutionOutcome::Applied;
			Result.bStateMutated = AppliedDamage != 0;
			Result.NumericAfter = Battler->CurrentHP;
			Result.NumericDelta = -static_cast<int64>(AppliedDamage);
			if (bApplyingDirectMoveDamageHit && Result.bStateMutated)
			{
				PendingDamagingHitConnections.Add(Battler->BattlerId);
				bMoveAffectedDifferentBattler = bMoveAffectedDifferentBattler
					|| Battler->BattlerId != Request.UserBattlerId;
			}
			return Result;
		}

		virtual FBattleEffectHookResult ApplyNonHpEffect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target) override
		{
			switch (Effect.Kind)
			{
			case EBattleMoveEffectKind::ApplyCondition:
				return ApplyCondition(Effect, Target);
			case EBattleMoveEffectKind::ModifyStatStage:
				return ApplyStatStage(Effect, Target);
			case EBattleMoveEffectKind::SetFieldCondition:
				return SetFieldCondition(Effect, Target);
			case EBattleMoveEffectKind::SetSideCondition:
				return SetSideCondition(Effect, Target);
			case EBattleMoveEffectKind::RemoveCondition:
				return RemoveCondition(Effect, Target);
			case EBattleMoveEffectKind::Charge:
				return ApplyCharge(Effect, Target);
			case EBattleMoveEffectKind::Recharge:
				return ApplySimpleSpecialVolatile(
					Effect,
					Target,
					FBattleVolatileRules::GetRechargeId());
			case EBattleMoveEffectKind::Protect:
				return ApplyProtect(Effect, Target);
			case EBattleMoveEffectKind::SemiInvulnerability:
				return ApplySimpleSpecialVolatile(
					Effect,
					Target,
					FBattleVolatileRules::GetFlySemiInvulnerableId());
			case EBattleMoveEffectKind::Switch:
			case EBattleMoveEffectKind::ChangeItem:
				return Outcome(EBattleEffectExecutionOutcome::Deferred);
			default:
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
		}

		virtual void RunImmediateUpdate(const FBattleResolvedTarget& Target) override
		{
			if (!bRuntimeValid)
			{
				return;
			}
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				const FBattlerId TargetId = Target.GetBattler().BattlerId;
				const bool bOriginalReachedTarget = Request.Targets.ContainsByPredicate(
					[TargetId](const FBattleResolvedTarget& Candidate)
					{
						return Candidate.GetKind() == EBattleResolvedTargetKind::Battler
							&& Candidate.GetBattler().BattlerId == TargetId;
					});
				FBattleBattlerState* Battler = FindMutableBattler(TargetId);
				const bool bDamagingMove = Request.Move->Category
						== EBattleMoveCategory::Physical
					|| Request.Move->Category == EBattleMoveCategory::Special;
				if (ExecutionResult != nullptr
					&& Battler != nullptr
					&& Battler->CurrentHP > 0
					&& !Battler->bFainted
					&& !Battler->bCaptured
					&& !Battler->bRemoved
					&& FBattleMajorStatusRules::ShouldThawReachedTarget(
						Battler->MajorStatusId,
						Request.Move->Type,
						bDamagingMove,
						EnumHasAllFlags(
							Request.Move->Flags,
							EBattleMoveFlags::ThawsTarget),
						bOriginalReachedTarget)
					&& TryCleanupCanonicalStatus(*Battler))
				{
					FBattleEventTarget EventTarget;
					EventTarget.TrainerId = Battler->TrainerId;
					EventTarget.BattlerId = Battler->BattlerId;
					EventTarget.ActiveSlotId = Target.GetBattler().ActiveSlotId;
					Battler->MajorStatusId = FConditionId();
					FBattleEffectExecutionEvent& Event =
						ExecutionResult->Events.AddDefaulted_GetRef();
					Event.Type = EBattleEventType::StatusChanged;
					Event.Cause = EBattleEventCause::Rule;
					Event.Outcome = EBattleEffectExecutionOutcome::Applied;
					Event.Targets.Add(MoveTemp(EventTarget));
					Event.NumericBefore = 1;
					Event.NumericAfter = 0;
					Event.NumericDelta = -1;
				}
				PendingImmediateItemUpdates.Remove(TargetId);
				if (Battler != nullptr && !TryRunImmediateHeldItemUpdate(*Battler))
				{
					bRuntimeValid = false;
					return;
				}
			}

			TArray<FBattlerId> PendingUpdates = PendingImmediateItemUpdates.Array();
			PendingUpdates.Sort(
				[this](const FBattlerId LeftId, const FBattlerId RightId)
				{
					const FBattleActivePositionState* Left = FindActiveForBattler(LeftId);
					const FBattleActivePositionState* Right = FindActiveForBattler(RightId);
					if (Left == nullptr || Right == nullptr)
					{
						return Left != nullptr || (Right == nullptr && LeftId < RightId);
					}
					if (Left->ActiveSlotId.GetSide() != Right->ActiveSlotId.GetSide())
					{
						return static_cast<uint8>(Left->ActiveSlotId.GetSide())
							< static_cast<uint8>(Right->ActiveSlotId.GetSide());
					}
					return static_cast<uint8>(Left->ActiveSlotId.GetPosition())
						< static_cast<uint8>(Right->ActiveSlotId.GetPosition());
				});
			PendingImmediateItemUpdates.Reset();
			for (const FBattlerId BattlerId : PendingUpdates)
			{
				FBattleBattlerState* Battler = FindMutableBattler(BattlerId);
				if (Battler != nullptr && !TryRunImmediateHeldItemUpdate(*Battler))
				{
					bRuntimeValid = false;
					return;
				}
			}
		}

		virtual bool TryBuildEventTarget(
			const FBattleResolvedTarget& Target,
			FBattleEventTarget& OutTarget) const override
		{
			OutTarget = FBattleEventTarget();
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				const FBattleBattlerState* Battler = FindBattler(Target.GetBattler().BattlerId);
				if (Battler == nullptr)
				{
					return false;
				}
				OutTarget.TrainerId = Battler->TrainerId;
				OutTarget.BattlerId = Battler->BattlerId;
				OutTarget.ActiveSlotId = Target.GetBattler().ActiveSlotId;
				return true;
			}
			if (Target.GetKind() == EBattleResolvedTargetKind::Side)
			{
				OutTarget.Side = Target.GetSide();
				OutTarget.bHasSide = true;
				return IsKnownSide(Target.GetSide());
			}
			if (Target.GetKind() == EBattleResolvedTargetKind::Field)
			{
				OutTarget.bField = true;
				return true;
			}
			return false;
		}

	private:
		static FBattleEffectHookResult Applied()
		{
			return Outcome(EBattleEffectExecutionOutcome::Applied);
		}

		static FBattleEffectHookResult Outcome(const EBattleEffectExecutionOutcome Value)
		{
			FBattleEffectHookResult Result;
			Result.Outcome = Value;
			return Result;
		}

		const FBattleBattlerState* FindBattler(const FBattlerId Id) const
		{
			return Battlers.FindByPredicate(
				[Id](const FBattleBattlerState& Battler)
				{
					return Battler.BattlerId == Id;
				});
		}

		FBattleBattlerState* FindMutableBattler(const FBattlerId Id)
		{
			return Battlers.FindByPredicate(
				[Id](const FBattleBattlerState& Battler)
				{
					return Battler.BattlerId == Id;
				});
		}

		const FBattleActivePositionState* FindActiveForBattler(const FBattlerId Id) const
		{
			return ActivePositions.FindByPredicate(
				[Id](const FBattleActivePositionState& Position)
				{
					return Position.BattlerId == Id;
				});
		}

		FBattleActivePositionState* FindMutableActivePosition(const FActiveSlotId Id)
		{
			return ActivePositions.FindByPredicate(
				[Id](const FBattleActivePositionState& Position)
				{
					return Position.ActiveSlotId == Id;
				});
		}

		FBattleSideState* FindMutableSide(const EBattleSide Side)
		{
			return Sides.FindByPredicate(
				[Side](const FBattleSideState& Entry)
				{
					return Entry.Side == Side;
				});
		}

		const FBattleSideState* FindSide(const EBattleSide Side) const
		{
			return Sides.FindByPredicate(
				[Side](const FBattleSideState& Entry)
				{
					return Entry.Side == Side;
				});
		}

		FConditionId GetWeatherId() const
		{
			return Field.Weather.IsSet() ? Field.Weather.GetValue().ConditionId : FConditionId();
		}

		FConditionId GetTerrainId() const
		{
			return Field.Terrain.IsSet() ? Field.Terrain.GetValue().ConditionId : FConditionId();
		}

		bool HasRoom(const FConditionId& RoomId) const
		{
			return Field.Rooms.ContainsByPredicate(
				[&RoomId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == RoomId;
				});
		}

		bool HasSideCondition(const EBattleSide Side, const FConditionId& ConditionId) const
		{
			const FBattleSideState* SideState = FindSide(Side);
			return SideState != nullptr && SideState->Conditions.ContainsByPredicate(
				[&ConditionId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == ConditionId;
				});
		}

		TArray<FConditionId> GetSideConditionIds(const EBattleSide Side) const
		{
			TArray<FConditionId> Result;
			const FBattleSideState* SideState = FindSide(Side);
			if (SideState != nullptr)
			{
				for (const FBattleConditionState& Condition : SideState->Conditions)
				{
					Result.Add(Condition.ConditionId);
				}
			}
			return Result;
		}

		bool TryIsGrounded(
			const FBattleBattlerState& Battler,
			bool& bOutGrounded,
			const bool bAbilityIgnoredForMove = false,
			bool* bOutLevitateMadeAirborne = nullptr) const
		{
			if (bOutLevitateMadeAirborne != nullptr)
			{
				*bOutLevitateMadeAirborne = false;
			}
			const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
				Battler.SpeciesFormId);
			if (Species == nullptr)
			{
				bOutGrounded = false;
				return false;
			}
			FBattleGroundedFacts Facts;
			Facts.PrimaryType = Species->PrimaryType;
			Facts.SecondaryType = Species->SecondaryType;
			Facts.bAbilityMakesAirborne = FBattleAbilityRules::IsLevitateAirborne(
				Battler.AbilityId,
				false,
				bAbilityIgnoredForMove);
			Facts.bAbilitySuppressed = Battler.bAbilitySuppressed;
			Facts.bItemMakesAirborne = !Battler.HeldItem.bConsumed
				&& !Battler.HeldItem.bTemporarilyRemoved
				&& FBattleItemRules::IsAirBalloonAirborne(
					Battler.HeldItem.CurrentItemId,
					Battler.HeldItem.bSuppressed);
			Facts.bItemSuppressed = Battler.HeldItem.bSuppressed;
			Facts.bAirborneSemiInvulnerable = HasVolatile(
				Battler,
				FBattleVolatileRules::GetFlySemiInvulnerableId());
			if (!FBattleFieldSideConditionRules::TryResolveGrounded(Facts, bOutGrounded))
			{
				return false;
			}
			if (bOutLevitateMadeAirborne != nullptr && Facts.bAbilityMakesAirborne)
			{
				FBattleGroundedFacts WithoutLevitate = Facts;
				WithoutLevitate.bAbilityMakesAirborne = false;
				bool bGroundedWithoutLevitate = false;
				if (!FBattleFieldSideConditionRules::TryResolveGrounded(
						WithoutLevitate,
						bGroundedWithoutLevitate))
				{
					return false;
				}
				*bOutLevitateMadeAirborne = bGroundedWithoutLevitate && !bOutGrounded;
			}
			return true;
		}

		bool ShouldIgnoreLevitateForCurrentMove(
			const FBattleBattlerState& Defender) const
		{
			if (Defender.AbilityId != FBattleAbilityRules::GetLevitateId())
			{
				return false;
			}
			const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
			TArray<FBattleAbilityItemHookDefinition> Hooks;
			if (User == nullptr
				|| !FBattleAbilityRules::TryGetHookDefinitionsForPhase(
					Defender.AbilityId,
					EBattleTriggerPhase::BeforeHit,
					Hooks))
			{
				return false;
			}
			const FBattleAbilityItemHookDefinition* TypeImmunityHook =
				Hooks.FindByPredicate(
					[](const FBattleAbilityItemHookDefinition& Hook)
					{
						return Hook.HookPoint
							== EBattleAbilityItemHookPoint::TypeImmunity;
					});
			return TypeImmunityHook != nullptr
				&& FBattleAbilityRules::ShouldMoldBreakerIgnoreDefenderHookForMove(
					User->AbilityId,
					User->bAbilitySuppressed,
					Defender.AbilityId,
					*TypeImmunityHook);
		}

		bool TryApplyHeldItemOperation(
			FBattleBattlerState& Battler,
			const EBattleHeldItemOperationKind Kind,
			const bool bSuppressed,
			FBattleHeldItemOperationFact& OutFact)
		{
			if (!Battler.HeldItem.InstanceId.IsValid())
			{
				return false;
			}
			FBattleHeldItemOperationRequest Operation;
			Operation.Kind = Kind;
			Operation.PrimaryInstanceId = Battler.HeldItem.InstanceId;
			Operation.bSuppressed = bSuppressed;
			EBattleHeldItemContractError Error = EBattleHeldItemContractError::None;
			if (!HeldItemLedger.TryApplyOperation(Operation, OutFact, Error))
			{
				return false;
			}
			Battler.HeldItem.CurrentItemId = OutFact.PrimaryAfter.CurrentItemId;
			Battler.HeldItem.bConsumed = OutFact.PrimaryAfter.bConsumed;
			Battler.HeldItem.bSuppressed = OutFact.PrimaryAfter.bSuppressed;
			Battler.HeldItem.bRevealed = OutFact.PrimaryAfter.bRevealed;
			Battler.HeldItem.bTemporarilyRemoved =
				OutFact.PrimaryAfter.bTemporarilyRemoved;
			const bool bItemLost = !Battler.HeldItem.CurrentItemId.IsValid()
				|| Battler.HeldItem.bConsumed
				|| Battler.HeldItem.bTemporarilyRemoved;
			if (FBattleItemRules::ShouldClearChoiceBandMoveLock(
				false,
				bItemLost,
				Battler.HeldItem.bSuppressed))
			{
				Battler.HeldItem.ChoiceLockedMoveId = FMoveId();
			}
			return true;
		}

		bool TryCleanupItemHooks(
			const FBattleBattlerState& Battler,
			const FItemId& ItemId,
			const EBattleTriggerCleanupReason Reason)
		{
			if (!FBattleItemRules::IsCanonical(ItemId))
			{
				return true;
			}
			FBattleTriggerSubject Owner;
			FBattleTriggerSourceDefinition SourceDefinition;
			FBattleTriggerOperationContext Operation;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition)
				|| !TryTakeTriggerContext(Operation))
			{
				return false;
			}
			FBattleTriggerCleanupRequest Cleanup;
			Cleanup.Reason = Reason;
			Cleanup.AffectedOwners.Add(Owner);
			Cleanup.SourceDefinitionFilter = SourceDefinition;
			Cleanup.Context = Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!TriggerFramework.TryApplyCleanup(Cleanup, Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryRegisterItemHooks(
			const FBattleBattlerState& Battler,
			const FBattleActivePositionState& Active)
		{
			const FItemId ItemId = Battler.HeldItem.CurrentItemId;
			if (!FBattleItemRules::IsCanonical(ItemId))
			{
				return true;
			}
			if (Battler.HeldItem.bConsumed || Battler.HeldItem.bTemporarilyRemoved)
			{
				return true;
			}
			if (!Active.bAvailable
				|| Active.BattlerId != Battler.BattlerId
				|| Active.TrainerId != Battler.TrainerId
				|| !TryCleanupItemHooks(
					Battler,
					ItemId,
					EBattleTriggerCleanupReason::Removal))
			{
				return false;
			}
			FBattleTriggerSubject Owner;
			FBattleTriggerSubject Source;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Source))
			{
				return false;
			}
			FBattleItemRegistrationFacts Facts;
			Facts.ItemId = ItemId;
			Facts.Owner = Owner;
			Facts.Source = Source;
			Facts.Targets.Add(Owner);
			Facts.bSuppressed = Battler.HeldItem.bSuppressed;
			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			if (!FBattleItemRules::TryRegisterHooks(TriggerFramework, Facts, Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryDispatchItemPhase(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			TArray<FBattleTriggerEffectRequest>& OutRequests)
		{
			OutRequests.Reset();
			const FItemId ItemId = Battler.HeldItem.CurrentItemId;
			if (!FBattleItemRules::IsCanonical(ItemId)
				|| Battler.HeldItem.bConsumed
				|| Battler.HeldItem.bTemporarilyRemoved)
			{
				return false;
			}
			FBattleTriggerSubject Owner;
			FBattleTriggerSourceDefinition SourceDefinition;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSourceDefinition::TryCreateItem(ItemId, SourceDefinition))
			{
				return false;
			}
			FBattleTriggerDispatchSpec Dispatch;
			Dispatch.Phase = Phase;
			const FBattleActivePositionState* Active = FindActiveForBattler(
				Battler.BattlerId);
			for (const FBattleTriggerRegistrationState& Registration :
				TriggerFramework.GetActiveRegistrations())
			{
				if (Registration.Spec.Owner == Owner
					&& Registration.Spec.SourceDefinition == SourceDefinition
					&& Registration.Spec.Rule.Phase == Phase)
				{
					FBattleTriggerDispatchParticipant& Participant =
						Dispatch.Participants.AddDefaulted_GetRef();
					Participant.RegistrationId = Registration.RegistrationId;
					if (Active != nullptr)
					{
						Participant.ActiveSlotId = Active->ActiveSlotId;
					}
				}
			}
			if (Dispatch.Participants.IsEmpty())
			{
				return true;
			}
			FBattleTriggerOperationContext Operation;
			FBattleTriggerDispatchResult Result;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!TryTakeTriggerContext(Operation))
			{
				return false;
			}
			Dispatch.ReentrancyToken = Operation.ReentrancyToken;
			if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
				|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
			{
				return false;
			}
			TArray<FBattleTriggerLifecycleFact> Facts;
			TriggerFramework.DrainEffectRequests(OutRequests);
			TriggerFramework.DrainLifecycleFacts(Facts);
			if (Result.bQueuedExpiryDispatch)
			{
				FBattleTriggerDispatchResult ExpiryResult;
				if (!TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
				{
					return false;
				}
				TArray<FBattleTriggerEffectRequest> ExpiryRequests;
				TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
				TriggerFramework.DrainEffectRequests(ExpiryRequests);
				TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
				OutRequests.Append(MoveTemp(ExpiryRequests));
			}
			return true;
		}

		bool TryGetItemEffectRequest(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			const EBattleAbilityItemHookPoint HookPoint,
			FBattleAbilityItemEffectRequest& OutRequest)
		{
			OutRequest = FBattleAbilityItemEffectRequest();
			TArray<FBattleTriggerEffectRequest> Requests;
			if (!TryDispatchItemPhase(Battler, Phase, Requests))
			{
				return false;
			}
			bool bFound = false;
			for (const FBattleTriggerEffectRequest& TriggerRequest : Requests)
			{
				FBattleAbilityItemEffectRequest TypedRequest;
				EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
				if (!FBattleItemRules::TryCreateTypedEffectRequest(
						TriggerRequest,
						TypedRequest,
						Error))
				{
					return false;
				}
				if (TypedRequest.HookPoint == HookPoint)
				{
					if (bFound)
					{
						return false;
					}
					OutRequest = MoveTemp(TypedRequest);
					bFound = true;
				}
			}
			return bFound;
		}

		bool TryBuildItemEventIdentity(
			const FBattleBattlerState& Battler,
			const FItemId& ItemId,
			FBattleEventSource& OutSource,
			FBattleEventTarget& OutTarget) const
		{
			OutSource = FBattleEventSource();
			OutTarget = FBattleEventTarget();
			const FBattleActivePositionState* Active = FindActiveForBattler(
				Battler.BattlerId);
			if (!ItemId.IsValid() || Active == nullptr || !Active->bAvailable)
			{
				return false;
			}
			OutSource.TrainerId = Battler.TrainerId;
			OutSource.BattlerId = Battler.BattlerId;
			OutSource.ActiveSlotId = Active->ActiveSlotId;
			OutSource.DefinitionId = ItemId.GetDefinitionId();
			OutTarget.TrainerId = Battler.TrainerId;
			OutTarget.BattlerId = Battler.BattlerId;
			OutTarget.ActiveSlotId = Active->ActiveSlotId;
			return true;
		}

		bool TryRecordItemActivation(
			const FBattleAbilityItemEffectRequest& RequestToRecord,
			const EBattleAbilityItemActivationOutcome Outcome,
			FBattleBattlerState& SourceBattler,
			const FItemId& ItemId)
		{
			TOptional<FBattleAbilityItemActivationFact> Fact;
			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			if (!AbilityItemRevealTracker.TryRecordActivation(
					RequestToRecord,
					Outcome,
					Fact,
					Error))
			{
				return false;
			}
			if (!Fact.IsSet() || !Fact.GetValue().RevealedSourceDefinition.IsSet())
			{
				return true;
			}
			const FBattleTriggerSourceDefinition& Revealed =
				Fact.GetValue().RevealedSourceDefinition.GetValue();
			if (Revealed.Kind != EBattleTriggerSourceDefinitionKind::Item
				|| Revealed.ItemId != ItemId
				|| SourceBattler.HeldItem.CurrentItemId != ItemId
				|| SourceBattler.HeldItem.bConsumed
				|| SourceBattler.HeldItem.bTemporarilyRemoved)
			{
				return false;
			}
			if (!SourceBattler.HeldItem.bRevealed)
			{
				FBattleHeldItemOperationFact RevealFact;
				if (!TryApplyHeldItemOperation(
						SourceBattler,
						EBattleHeldItemOperationKind::Reveal,
						false,
						RevealFact))
				{
					return false;
				}
			}
			if (ExecutionResult == nullptr)
			{
				return false;
			}
			FBattleEventSource Source;
			FBattleEventTarget Target;
			if (!TryBuildItemEventIdentity(SourceBattler, ItemId, Source, Target))
			{
				return false;
			}
			FBattleEffectExecutionEvent& Event =
				ExecutionResult->Events.AddDefaulted_GetRef();
			Event.Type = EBattleEventType::ItemActivated;
			Event.Cause = EBattleEventCause::Item;
			Event.Outcome = Outcome == EBattleAbilityItemActivationOutcome::Applied
				? EBattleEffectExecutionOutcome::Applied
				: EBattleEffectExecutionOutcome::Prevented;
			Event.SourceOverride = MoveTemp(Source);
			Event.Targets.Add(MoveTemp(Target));
			Event.NumericBefore = Fact.GetValue().bFirstPublicReveal ? 0 : 1;
			Event.NumericAfter = 1;
			Event.NumericDelta = Fact.GetValue().bFirstPublicReveal ? 1 : 0;
			return true;
		}

		bool TryAppendItemMutationEvent(
			const EBattleEventType Type,
			const FItemId& ItemId,
			const FBattleBattlerState& Battler,
			const int64 Before,
			const int64 After,
			const int64 Delta)
		{
			if (ExecutionResult == nullptr)
			{
				return false;
			}
			FBattleEventSource Source;
			FBattleEventTarget Target;
			if (!TryBuildItemEventIdentity(Battler, ItemId, Source, Target))
			{
				return false;
			}
			FBattleEffectExecutionEvent& Event =
				ExecutionResult->Events.AddDefaulted_GetRef();
			Event.Type = Type;
			Event.Cause = EBattleEventCause::Item;
			Event.Outcome = EBattleEffectExecutionOutcome::Applied;
			Event.SourceOverride = MoveTemp(Source);
			Event.Targets.Add(MoveTemp(Target));
			Event.NumericBefore = Before;
			Event.NumericAfter = After;
			Event.NumericDelta = Delta;
			return true;
		}

		bool TryConsumeHeldItem(FBattleBattlerState& Battler, const FItemId& ItemId)
		{
			if (Battler.HeldItem.CurrentItemId != ItemId
				|| Battler.HeldItem.bConsumed
				|| Battler.HeldItem.bTemporarilyRemoved
				|| !TryCleanupItemHooks(
					Battler,
					ItemId,
					EBattleTriggerCleanupReason::Removal))
			{
				return false;
			}
			FBattleHeldItemOperationFact ConsumeFact;
			return TryApplyHeldItemOperation(
				Battler,
				EBattleHeldItemOperationKind::Consume,
				false,
				ConsumeFact);
		}

		bool TryResolveHeldItemSwitchIn(
			FBattleBattlerState& Battler,
			const FBattleActivePositionState& Active)
		{
			if (!TryRegisterItemHooks(Battler, Active))
			{
				return false;
			}
			const bool bAirBalloonActive = !Battler.HeldItem.bConsumed
				&& !Battler.HeldItem.bTemporarilyRemoved
				&& !Battler.HeldItem.bSuppressed
				&& Battler.HeldItem.CurrentItemId == FBattleItemRules::GetAirBalloonId();
			if (bAirBalloonActive)
			{
				const FItemId ItemId = Battler.HeldItem.CurrentItemId;
				FBattleAbilityItemEffectRequest ItemRequest;
				if (!TryGetItemEffectRequest(
						Battler,
						EBattleTriggerPhase::SwitchIn,
						EBattleAbilityItemHookPoint::SwitchIn,
						ItemRequest)
					|| !TryRecordItemActivation(
						ItemRequest,
						EBattleAbilityItemActivationOutcome::Applied,
						Battler,
						ItemId))
				{
					return false;
				}
			}
			return TryRunImmediateHeldItemUpdate(Battler);
		}

		bool TryRunImmediateHeldItemUpdate(FBattleBattlerState& Battler)
		{
			const bool bDamagingHitConnected =
				PendingDamagingHitConnections.Remove(Battler.BattlerId) > 0;
			const FItemId ItemId = Battler.HeldItem.CurrentItemId;
			if (!FBattleItemRules::IsCanonical(ItemId)
				|| Battler.HeldItem.bConsumed
				|| Battler.HeldItem.bTemporarilyRemoved)
			{
				return true;
			}

			if (ItemId == FBattleItemRules::GetAirBalloonId())
			{
				if (!FBattleItemRules::ShouldPopAirBalloon(
						ItemId,
						bDamagingHitConnected,
						Battler.HeldItem.bSuppressed))
				{
					return true;
				}
				FBattleAbilityItemEffectRequest ItemRequest;
				if (!TryGetItemEffectRequest(
						Battler,
						EBattleTriggerPhase::AfterDamage,
						EBattleAbilityItemHookPoint::AfterDamage,
						ItemRequest)
					|| !TryRecordItemActivation(
						ItemRequest,
						EBattleAbilityItemActivationOutcome::Applied,
						Battler,
						ItemId)
					|| !TryCleanupItemHooks(
						Battler,
						ItemId,
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				FBattleHeldItemOperationFact RemoveFact;
				if (!TryApplyHeldItemOperation(
						Battler,
						EBattleHeldItemOperationKind::Remove,
						false,
						RemoveFact)
					|| !TryAppendItemMutationEvent(
						EBattleEventType::ItemRemoved,
						ItemId,
						Battler,
						1,
						0,
						-1))
				{
					return false;
				}
				return true;
			}

			if (ItemId == FBattleItemRules::GetSitrusBerryId())
			{
				FBattleItemRecoveryFacts Facts;
				Facts.ItemId = ItemId;
				Facts.CurrentHP = Battler.CurrentHP;
				Facts.BaseMaximumHP = Battler.PermanentStats.MaxHP;
				Facts.bHealingPermitted = Battler.CurrentHP > 0 && !Battler.bFainted;
				Facts.bSuppressed = Battler.HeldItem.bSuppressed;
				FBattleItemRecoveryResult Recovery;
				if (!FBattleItemRules::TryEvaluateRecovery(Facts, Recovery)
					|| !Recovery.bValid)
				{
					return false;
				}
				if (!Recovery.bApplies)
				{
					return true;
				}
				FBattleAbilityItemEffectRequest ItemRequest;
				if (!Recovery.bConsumesItem
					|| !TryGetItemEffectRequest(
						Battler,
						EBattleTriggerPhase::AfterDamage,
						EBattleAbilityItemHookPoint::AfterDamage,
						ItemRequest)
					|| !TryRecordItemActivation(
						ItemRequest,
						Recovery.Outcome,
						Battler,
						ItemId)
					|| !TryConsumeHeldItem(Battler, ItemId)
					|| !TryAppendItemMutationEvent(
						EBattleEventType::ItemConsumed,
						ItemId,
						Battler,
						1,
						0,
						-1))
				{
					return false;
				}
				const int32 PreviousHP = Battler.CurrentHP;
				Battler.CurrentHP = FMath::Min(
					Battler.PermanentStats.MaxHP,
					Battler.CurrentHP + Recovery.HealAmount);
				const int32 AppliedHeal = Battler.CurrentHP - PreviousHP;
				return AppliedHeal > 0
					&& TryAppendItemMutationEvent(
						EBattleEventType::Healing,
						ItemId,
						Battler,
						PreviousHP,
						Battler.CurrentHP,
						AppliedHeal)
					&& TryAppendItemMutationEvent(
						EBattleEventType::HPChanged,
						ItemId,
						Battler,
						PreviousHP,
						Battler.CurrentHP,
						AppliedHeal);
			}

			if (ItemId == FBattleItemRules::GetLumBerryId())
			{
				const bool bHasMajorStatus = Battler.MajorStatusId.IsValid();
				const bool bHasConfusion = HasVolatile(
					Battler,
					FBattleVolatileRules::GetConfusionId());
				FBattleLumBerryFacts Facts;
				Facts.ItemId = ItemId;
				Facts.bHolderAbleToBattle = Battler.CurrentHP > 0
					&& !Battler.bFainted
					&& !Battler.bCaptured
					&& !Battler.bRemoved;
				Facts.bHasMajorStatus = bHasMajorStatus;
				Facts.bHasConfusion = bHasConfusion;
				Facts.bSuppressed = Battler.HeldItem.bSuppressed;
				FBattleLumBerryResult Cure;
				if (!FBattleItemRules::TryEvaluateLumBerry(Facts, Cure) || !Cure.bValid)
				{
					return false;
				}
				if (!Cure.bApplies)
				{
					return true;
				}
				FBattleAbilityItemEffectRequest ItemRequest;
				if (!Cure.bConsumesItem
					|| !TryGetItemEffectRequest(
						Battler,
						EBattleTriggerPhase::AfterHit,
						EBattleAbilityItemHookPoint::EffectApplication,
						ItemRequest)
					|| !TryRecordItemActivation(
						ItemRequest,
						Cure.Outcome,
						Battler,
						ItemId)
					|| !TryConsumeHeldItem(Battler, ItemId)
					|| !TryAppendItemMutationEvent(
						EBattleEventType::ItemConsumed,
						ItemId,
						Battler,
						1,
						0,
						-1))
				{
					return false;
				}
				if (Cure.bCuresMajorStatus)
				{
					if (!TryCleanupCanonicalStatus(Battler))
					{
						return false;
					}
					Battler.MajorStatusId = FConditionId();
				}
				if (Cure.bCuresConfusion)
				{
					if (!TryCleanupVolatile(
							Battler,
							FBattleVolatileRules::GetConfusionId(),
							EBattleTriggerCleanupReason::Removal))
					{
						return false;
					}
					Battler.Volatiles.RemoveAll(
						[](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId
								== FBattleVolatileRules::GetConfusionId();
						});
				}
				const int32 CuredCount = (Cure.bCuresMajorStatus ? 1 : 0)
					+ (Cure.bCuresConfusion ? 1 : 0);
				return CuredCount > 0
					&& TryAppendItemMutationEvent(
						EBattleEventType::StatusChanged,
						ItemId,
						Battler,
						CuredCount,
						0,
						-CuredCount);
			}

			return true;
		}

		bool TryCleanupAbilityHooks(
			const FBattleBattlerState& Battler,
			const EBattleTriggerCleanupReason Reason)
		{
			if (!FBattleAbilityRules::IsCanonical(Battler.AbilityId))
			{
				return true;
			}

			FBattleTriggerSubject Owner;
			FBattleTriggerSourceDefinition SourceDefinition;
			FBattleTriggerOperationContext Operation;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSourceDefinition::TryCreateAbility(
					Battler.AbilityId,
					SourceDefinition)
				|| !TryTakeTriggerContext(Operation))
			{
				return false;
			}

			FBattleTriggerCleanupRequest Cleanup;
			Cleanup.Reason = Reason;
			Cleanup.AffectedOwners.Add(Owner);
			Cleanup.SourceDefinitionFilter = SourceDefinition;
			Cleanup.Context = Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!TriggerFramework.TryApplyCleanup(Cleanup, Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryRegisterAbilityHooks(
			const FBattleBattlerState& Battler,
			const FBattleActivePositionState& Active)
		{
			if (!FBattleAbilityRules::IsCanonical(Battler.AbilityId))
			{
				return true;
			}
			if (!Active.bAvailable
				|| Active.BattlerId != Battler.BattlerId
				|| Active.TrainerId != Battler.TrainerId
				|| !TryCleanupAbilityHooks(
					Battler,
					EBattleTriggerCleanupReason::Removal))
			{
				return false;
			}

			FBattleTriggerSubject Owner;
			FBattleTriggerSubject Source;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Source))
			{
				return false;
			}

			FBattleAbilityRegistrationFacts Facts;
			Facts.AbilityId = Battler.AbilityId;
			Facts.Owner = Owner;
			Facts.Source = Source;
			Facts.bSuppressed = Battler.bAbilitySuppressed;
			if (Battler.AbilityId == FBattleAbilityRules::GetIntimidateId())
			{
				TArray<const FBattleActivePositionState*> OpposingPositions;
				for (const FBattleActivePositionState& Candidate : ActivePositions)
				{
					if (Candidate.bAvailable
						&& Candidate.ActiveSlotId.GetSide()
							!= Active.ActiveSlotId.GetSide())
					{
						OpposingPositions.Add(&Candidate);
					}
				}
				OpposingPositions.Sort(
					[](const FBattleActivePositionState& Left,
						const FBattleActivePositionState& Right)
					{
						if (Left.ActiveSlotId.GetSide() != Right.ActiveSlotId.GetSide())
						{
							return static_cast<uint8>(Left.ActiveSlotId.GetSide())
								< static_cast<uint8>(Right.ActiveSlotId.GetSide());
						}
						return static_cast<uint8>(Left.ActiveSlotId.GetPosition())
							< static_cast<uint8>(Right.ActiveSlotId.GetPosition());
					});
				for (const FBattleActivePositionState* Position : OpposingPositions)
				{
					const FBattleBattlerState* TargetBattler = Position != nullptr
						? FindBattler(Position->BattlerId)
						: nullptr;
					FBattleTriggerSubject Target;
					if (TargetBattler == nullptr
						|| TargetBattler->CurrentHP <= 0
						|| TargetBattler->bFainted
						|| TargetBattler->bCaptured
						|| TargetBattler->bRemoved)
					{
						continue;
					}
					if (!FBattleTriggerSubject::TryCreateBattler(
							TargetBattler->BattlerId,
							Target))
					{
						return false;
					}
					Facts.Targets.Add(MoveTemp(Target));
				}
			}
			else
			{
				Facts.Targets.Add(Owner);
			}

			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			if (!FBattleAbilityRules::TryRegisterHooks(
					TriggerFramework,
					Facts,
					Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryDispatchAbilityPhase(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			TArray<FBattleTriggerEffectRequest>& OutRequests)
		{
			OutRequests.Reset();
			if (!FBattleAbilityRules::IsCanonical(Battler.AbilityId))
			{
				return false;
			}

			FBattleTriggerSubject Owner;
			FBattleTriggerSourceDefinition SourceDefinition;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSourceDefinition::TryCreateAbility(
					Battler.AbilityId,
					SourceDefinition))
			{
				return false;
			}

			FBattleTriggerDispatchSpec Dispatch;
			Dispatch.Phase = Phase;
			const FBattleActivePositionState* Active = FindActiveForBattler(
				Battler.BattlerId);
			for (const FBattleTriggerRegistrationState& Registration :
				TriggerFramework.GetActiveRegistrations())
			{
				if (Registration.Spec.Owner == Owner
					&& Registration.Spec.SourceDefinition == SourceDefinition
					&& Registration.Spec.Rule.Phase == Phase)
				{
					FBattleTriggerDispatchParticipant& Participant =
						Dispatch.Participants.AddDefaulted_GetRef();
					Participant.RegistrationId = Registration.RegistrationId;
					if (Active != nullptr)
					{
						Participant.ActiveSlotId = Active->ActiveSlotId;
					}
				}
			}
			if (Dispatch.Participants.IsEmpty())
			{
				return true;
			}

			FBattleTriggerOperationContext Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			FBattleTriggerDispatchResult Result;
			if (!TryTakeTriggerContext(Operation))
			{
				return false;
			}
			Dispatch.ReentrancyToken = Operation.ReentrancyToken;
			if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
				|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
			{
				return false;
			}
			TArray<FBattleTriggerLifecycleFact> Facts;
			TriggerFramework.DrainEffectRequests(OutRequests);
			TriggerFramework.DrainLifecycleFacts(Facts);
			if (Result.bQueuedExpiryDispatch)
			{
				FBattleTriggerDispatchResult ExpiryResult;
				if (!TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
				{
					return false;
				}
				TArray<FBattleTriggerEffectRequest> ExpiryRequests;
				TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
				TriggerFramework.DrainEffectRequests(ExpiryRequests);
				TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
				OutRequests.Append(MoveTemp(ExpiryRequests));
			}
			return true;
		}

		bool TryGetAbilityEffectRequest(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			const EBattleAbilityItemHookPoint HookPoint,
			FBattleAbilityItemEffectRequest& OutRequest)
		{
			OutRequest = FBattleAbilityItemEffectRequest();
			TArray<FBattleTriggerEffectRequest> Requests;
			if (!TryDispatchAbilityPhase(Battler, Phase, Requests))
			{
				return false;
			}

			bool bFound = false;
			for (const FBattleTriggerEffectRequest& TriggerRequest : Requests)
			{
				FBattleAbilityItemEffectRequest TypedRequest;
				EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
				if (!FBattleAbilityRules::TryCreateTypedEffectRequest(
						TriggerRequest,
						TypedRequest,
						Error))
				{
					return false;
				}
				if (TypedRequest.HookPoint == HookPoint)
				{
					if (bFound)
					{
						return false;
					}
					OutRequest = MoveTemp(TypedRequest);
					bFound = true;
				}
			}
			return bFound;
		}

		bool TryRecordAbilityActivation(
			const FBattleAbilityItemEffectRequest& RequestToRecord,
			const EBattleAbilityItemActivationOutcome Outcome,
			const FBattleBattlerState& SourceBattler)
		{
			TOptional<FBattleAbilityItemActivationFact> Fact;
			EBattleAbilityItemHookError Error = EBattleAbilityItemHookError::None;
			if (!AbilityItemRevealTracker.TryRecordActivation(
					RequestToRecord,
					Outcome,
					Fact,
					Error))
			{
				return false;
			}
			if (!Fact.IsSet())
			{
				return true;
			}
			if (ExecutionResult == nullptr
				|| !Fact.GetValue().RevealedSourceDefinition.IsSet()
				|| Fact.GetValue().RevealedSourceDefinition.GetValue().Kind
					!= EBattleTriggerSourceDefinitionKind::Ability
				|| Fact.GetValue().RevealedSourceDefinition.GetValue().AbilityId
					!= SourceBattler.AbilityId)
			{
				return false;
			}
			const FBattleActivePositionState* Active = FindActiveForBattler(
				SourceBattler.BattlerId);
			if (Active == nullptr || !Active->bAvailable)
			{
				return false;
			}

			FBattleEventSource Source;
			Source.TrainerId = SourceBattler.TrainerId;
			Source.BattlerId = SourceBattler.BattlerId;
			Source.ActiveSlotId = Active->ActiveSlotId;
			Source.DefinitionId = SourceBattler.AbilityId.GetDefinitionId();
			FBattleEventTarget Target;
			Target.TrainerId = SourceBattler.TrainerId;
			Target.BattlerId = SourceBattler.BattlerId;
			Target.ActiveSlotId = Active->ActiveSlotId;

			FBattleEffectExecutionEvent& Event =
				ExecutionResult->Events.AddDefaulted_GetRef();
			Event.Type = EBattleEventType::AbilityActivated;
			Event.Cause = EBattleEventCause::Rule;
			Event.Outcome = Outcome == EBattleAbilityItemActivationOutcome::Applied
				? EBattleEffectExecutionOutcome::Applied
				: EBattleEffectExecutionOutcome::Prevented;
			Event.SourceOverride = MoveTemp(Source);
			Event.Targets.Add(MoveTemp(Target));
			Event.NumericBefore = Fact.GetValue().bFirstPublicReveal ? 0 : 1;
			Event.NumericAfter = 1;
			Event.NumericDelta = Fact.GetValue().bFirstPublicReveal ? 1 : 0;
			return true;
		}

		bool TryRecordLevitateGroundedActivation(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			const EBattleAbilityItemHookPoint HookPoint)
		{
			FBattleAbilityItemEffectRequest AbilityRequest;
			return Battler.AbilityId == FBattleAbilityRules::GetLevitateId()
				&& TryGetAbilityEffectRequest(
					Battler,
					Phase,
					HookPoint,
					AbilityRequest)
				&& TryRecordAbilityActivation(
					AbilityRequest,
					EBattleAbilityItemActivationOutcome::Applied,
					Battler);
		}

		FBattleConditionState MakeCanonicalConditionState(
			const FConditionId& ConditionId,
			const TOptional<int32>& RemainingTurns,
			const int32 Layers)
		{
			FBattleConditionState Condition;
			Condition.ConditionId = ConditionId;
			Condition.RemainingTurns = RemainingTurns;
			Condition.LayerCount = Layers;
			Condition.CreationOrdinal = NextConditionCreationOrdinal++;
			Condition.SourceBattlerId = Request.UserBattlerId;
			return Condition;
		}

		bool TryBuildFieldSideOwner(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			FBattleTriggerSubject& OutOwner) const
		{
			if (FBattleFieldSideConditionRules::IsFieldOwned(ConditionId))
			{
				OutOwner = FBattleTriggerSubject::CreateField();
				return OutOwner.IsValid() && !Side.IsSet();
			}
			return FBattleFieldSideConditionRules::IsSideOwned(ConditionId)
				&& Side.IsSet()
				&& FBattleTriggerSubject::TryCreateSide(Side.GetValue(), OutOwner);
		}

		const FBattleConditionState* FindFieldSideConditionState(
			const FBattleTriggerSubject& Owner,
			const FConditionId& ConditionId) const
		{
			if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId))
			{
				return nullptr;
			}
			if (Owner.Kind == EBattleTriggerSubjectKind::Field)
			{
				if (Field.Weather.IsSet()
					&& Field.Weather.GetValue().ConditionId == ConditionId)
				{
					return &Field.Weather.GetValue();
				}
				if (Field.Terrain.IsSet()
					&& Field.Terrain.GetValue().ConditionId == ConditionId)
				{
					return &Field.Terrain.GetValue();
				}
				return Field.Rooms.FindByPredicate(
					[&ConditionId](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == ConditionId;
					});
			}
			if (Owner.Kind != EBattleTriggerSubjectKind::Side || !Owner.bHasSide)
			{
				return nullptr;
			}
			const FBattleSideState* Side = FindSide(Owner.Side);
			if (Side == nullptr)
			{
				return nullptr;
			}
			const FBattleConditionState* Condition = Side->Conditions.FindByPredicate(
				[&ConditionId](const FBattleConditionState& Candidate)
				{
					return Candidate.ConditionId == ConditionId;
				});
			return Condition != nullptr
				? Condition
				: Side->Hazards.FindByPredicate(
					[&ConditionId](const FBattleConditionState& Candidate)
					{
						return Candidate.ConditionId == ConditionId;
					});
		}

		bool TryDispatchFieldSidePhase(
			const FBattleTriggerSubject& Owner,
			const EBattleTriggerPhase Phase,
			const FConditionId& FilterConditionId,
			const TOptional<FActiveSlotId>& ActiveSlotId,
			TArray<FBattleTriggerEffectRequest>& OutRequests)
		{
			OutRequests.Reset();
			if (!Owner.IsValid()
				|| (Owner.Kind != EBattleTriggerSubjectKind::Field
					&& Owner.Kind != EBattleTriggerSubjectKind::Side))
			{
				return false;
			}
			FBattleTriggerDispatchSpec Dispatch;
			Dispatch.Phase = Phase;
			for (const FBattleTriggerRegistrationState& Registration :
				TriggerFramework.GetActiveRegistrations())
			{
				if (Registration.Spec.Owner != Owner
					|| Registration.Spec.Rule.Phase != Phase
					|| Registration.Spec.SourceDefinition.Kind
						!= EBattleTriggerSourceDefinitionKind::Condition)
				{
					continue;
				}
				const FConditionId& ConditionId =
					Registration.Spec.SourceDefinition.ConditionId;
				if (!FBattleFieldSideConditionRules::IsCanonical(ConditionId)
					|| (FilterConditionId.IsValid() && ConditionId != FilterConditionId)
					|| FindFieldSideConditionState(Owner, ConditionId) == nullptr)
				{
					continue;
				}
				FBattleTriggerDispatchParticipant& Participant =
					Dispatch.Participants.AddDefaulted_GetRef();
				Participant.RegistrationId = Registration.RegistrationId;
				Participant.ActiveSlotId = ActiveSlotId;
			}
			if (Dispatch.Participants.IsEmpty())
			{
				return true;
			}

			FBattleTriggerOperationContext Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			FBattleTriggerDispatchResult Result;
			if (!TryTakeTriggerContext(Operation))
			{
				return false;
			}
			Dispatch.ReentrancyToken = Operation.ReentrancyToken;
			if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
				|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
			{
				return false;
			}
			TArray<FBattleTriggerLifecycleFact> Facts;
			TriggerFramework.DrainEffectRequests(OutRequests);
			TriggerFramework.DrainLifecycleFacts(Facts);
			if (Result.bQueuedExpiryDispatch)
			{
				FBattleTriggerDispatchResult ExpiryResult;
				if (!TriggerFramework.TryResolveNextDispatch(ExpiryResult, Error))
				{
					return false;
				}
				TArray<FBattleTriggerEffectRequest> ExpiryRequests;
				TArray<FBattleTriggerLifecycleFact> ExpiryFacts;
				TriggerFramework.DrainEffectRequests(ExpiryRequests);
				TriggerFramework.DrainLifecycleFacts(ExpiryFacts);
				OutRequests.Append(MoveTemp(ExpiryRequests));
			}
			return true;
		}

		bool TryIsFieldSideConditionActiveForPhase(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			const EBattleTriggerPhase Phase,
			const TOptional<FActiveSlotId>& ActiveSlotId,
			bool& bOutActive)
		{
			bOutActive = false;
			FBattleTriggerSubject Owner;
			if (!TryBuildFieldSideOwner(ConditionId, Side, Owner))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> Requests;
			if (!TryDispatchFieldSidePhase(
					Owner,
					Phase,
					ConditionId,
					ActiveSlotId,
					Requests))
			{
				return false;
			}
			bOutActive = Requests.ContainsByPredicate(
				[&ConditionId, Phase](const FBattleTriggerEffectRequest& Request)
				{
					return Request.Phase == Phase
						&& Request.SourceDefinition.Kind
							== EBattleTriggerSourceDefinitionKind::Condition
						&& Request.SourceDefinition.ConditionId == ConditionId;
				});
			return true;
		}

		bool TryRegisterFieldSideCondition(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			const TOptional<int32>& RemainingTurns,
			const int32 Layers)
		{
			FBattleTriggerSubject Owner;
			FBattleTriggerSubject Source;
			if (!TryBuildFieldSideOwner(ConditionId, Side, Owner)
				|| !FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source))
			{
				return false;
			}
			FBattleFieldSideTriggerRegistrationFacts Facts;
			Facts.ConditionId = ConditionId;
			Facts.PayloadId = ConditionId.GetDefinitionId();
			Facts.Owner = Owner;
			Facts.Source = Source;
			Facts.RemainingTurns = RemainingTurns;
			Facts.Layers = Layers;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!FBattleFieldSideConditionRules::TryRegisterTriggers(
				TriggerFramework,
				Facts,
				Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryCleanupFieldSideCondition(
			const FConditionId& ConditionId,
			const TOptional<EBattleSide>& Side,
			const EBattleTriggerCleanupReason Reason)
		{
			FBattleTriggerSubject Owner;
			FBattleTriggerOperationContext Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!TryBuildFieldSideOwner(ConditionId, Side, Owner)
				|| !TryTakeTriggerContext(Operation)
				|| !FBattleFieldSideConditionRules::TryCleanupTriggers(
					TriggerFramework,
					ConditionId,
					Owner,
					Reason,
					Operation,
					Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryUpdateFieldSideLayers(
			const FConditionId& ConditionId,
			const EBattleSide Side,
			const int32 Layers)
		{
			FBattleTriggerSubject Owner;
			FBattleTriggerOperationContext Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!FBattleTriggerSubject::TryCreateSide(Side, Owner)
				|| !TryTakeTriggerContext(Operation)
				|| !FBattleFieldSideConditionRules::TryUpdateTriggerLayers(
					TriggerFramework,
					ConditionId,
					Owner,
					Layers,
					Operation,
					Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TrySetMagicRoomSuppression(const bool bSuppressed)
		{
			for (FBattleBattlerState& Battler : Battlers)
			{
				const bool bPresent = Battler.HeldItem.CurrentItemId.IsValid()
					&& !Battler.HeldItem.bConsumed
					&& !Battler.HeldItem.bTemporarilyRemoved;
				const bool bDesiredSuppression = bSuppressed && bPresent;
				if (!bPresent || Battler.HeldItem.bSuppressed == bDesiredSuppression)
				{
					continue;
				}
				const FItemId ItemId = Battler.HeldItem.CurrentItemId;
				if (!TryCleanupItemHooks(
						Battler,
						ItemId,
						EBattleTriggerCleanupReason::Removal))
				{
					return false;
				}
				FBattleHeldItemOperationFact SuppressFact;
				if (!TryApplyHeldItemOperation(
						Battler,
						EBattleHeldItemOperationKind::Suppress,
						bDesiredSuppression,
						SuppressFact))
				{
					return false;
				}
				const FBattleActivePositionState* Active = FindActiveForBattler(
					Battler.BattlerId);
				if (Active != nullptr && !TryRegisterItemHooks(Battler, *Active))
				{
					return false;
				}
				if (!bDesiredSuppression && Active != nullptr)
				{
					PendingImmediateItemUpdates.Add(Battler.BattlerId);
				}
			}
			return true;
		}

		bool TryApplyEntryHazards(
			FBattleBattlerState& Incoming,
			const FBattleActivePositionState& Active)
		{
			if (ExecutionResult == nullptr)
			{
				return false;
			}
			FBattleSideState* Side = FindMutableSide(Active.ActiveSlotId.GetSide());
			const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
				Incoming.SpeciesFormId);
			if (Side == nullptr || Species == nullptr)
			{
				return false;
			}
			FBattleTriggerSubject SideOwner;
			TArray<FBattleTriggerEffectRequest> HazardRequests;
			if (!FBattleTriggerSubject::TryCreateSide(Active.ActiveSlotId.GetSide(), SideOwner)
				|| !TryDispatchFieldSidePhase(
					SideOwner,
					EBattleTriggerPhase::SwitchIn,
					FConditionId(),
					Active.ActiveSlotId,
					HazardRequests))
			{
				return false;
			}
			for (const FBattleTriggerEffectRequest& HazardRequest : HazardRequests)
			{
				if (Incoming.CurrentHP <= 0 || Incoming.bFainted)
				{
					break;
				}
				if (HazardRequest.Phase != EBattleTriggerPhase::SwitchIn
					|| HazardRequest.SourceDefinition.Kind
						!= EBattleTriggerSourceDefinitionKind::Condition)
				{
					return false;
				}
				const FConditionId HazardId = HazardRequest.SourceDefinition.ConditionId;
				const FBattleConditionState* CurrentHazard = Side->Hazards.FindByPredicate(
					[&HazardId](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == HazardId;
					});
				if (CurrentHazard == nullptr
					|| !FBattleFieldSideConditionRules::IsCanonical(HazardId))
				{
					continue;
				}

				bool bGrounded = false;
				bool bLevitateMadeAirborne = false;
				if (!TryIsGrounded(
						Incoming,
						bGrounded,
						false,
						&bLevitateMadeAirborne))
				{
					return false;
				}
				FBattleTypeEffectiveness RockEffectiveness{1, 1};
				if (HazardId == FBattleFieldSideConditionRules::GetStealthRockId())
				{
					const bool bTypeFound = Species->SecondaryType == EPokemonType::Invalid
						? State.Catalog.GetTypeChart().TryGetEffectiveness(
							EPokemonType::Rock,
							Species->PrimaryType,
							RockEffectiveness)
						: State.Catalog.GetTypeChart().TryGetDualEffectiveness(
							EPokemonType::Rock,
							Species->PrimaryType,
							Species->SecondaryType,
							RockEffectiveness);
					if (!bTypeFound)
					{
						return false;
					}
				}

				const FConditionId HazardStatusId = HazardRequest.Layers >= 2
					? FBattleMajorStatusRules::GetToxicId()
					: FBattleMajorStatusRules::GetPoisonId();
				const FBattleBattlerState* SourceBattler = FindBattler(
					HazardRequest.Source.BattlerId);
				const FBattleTrainerState* SourceTrainer = SourceBattler != nullptr
					? State.FindTrainer(SourceBattler->TrainerId)
					: nullptr;
				const bool bHazardAppliedByOpponent = SourceTrainer == nullptr
					|| SourceTrainer->Side != Active.ActiveSlotId.GetSide();
				FBattleMajorStatusApplicationFacts StatusFacts;
				StatusFacts.RequestedStatusId = HazardStatusId;
				StatusFacts.ExistingMajorStatusId = Incoming.MajorStatusId;
				StatusFacts.PrimaryType = Species->PrimaryType;
				StatusFacts.SecondaryType = Species->SecondaryType;
				const FConditionId TerrainId = GetTerrainId();
				bool bTerrainTriggerActive = false;
				if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
					&& !TryIsFieldSideConditionActiveForPhase(
						TerrainId,
						TOptional<EBattleSide>(),
						EBattleTriggerPhase::BeforeHit,
						Active.ActiveSlotId,
						bTerrainTriggerActive))
				{
					return false;
				}
				StatusFacts.Prevention.bTerrainPrevents =
					bTerrainTriggerActive
					&& FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
						TerrainId,
						HazardStatusId,
						bGrounded);
				bool bSafeguardTriggerActive = false;
				if (!TryIsFieldSideConditionActiveForPhase(
						FBattleFieldSideConditionRules::GetSafeguardId(),
						Active.ActiveSlotId.GetSide(),
						EBattleTriggerPhase::BeforeHit,
						Active.ActiveSlotId,
						bSafeguardTriggerActive))
				{
					return false;
				}
				StatusFacts.Prevention.bSafeguardPrevents =
					FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
						bSafeguardTriggerActive,
						bHazardAppliedByOpponent,
						false);
				FBattleMajorStatusApplicationResult StatusApplication;
				if (!FBattleMajorStatusRules::TryEvaluateApplication(
					StatusFacts,
					StatusApplication))
				{
					return false;
				}

				FBattleHazardSwitchInFacts Facts;
				Facts.HazardId = HazardId;
				Facts.Layers = HazardRequest.Layers;
				Facts.BaseMaximumHP = Incoming.PermanentStats.MaxHP;
				Facts.CurrentHP = Incoming.CurrentHP;
				Facts.PrimaryType = Species->PrimaryType;
				Facts.SecondaryType = Species->SecondaryType;
				Facts.bGrounded = bGrounded;
				const bool bBootsBypassActive = !Incoming.HeldItem.bConsumed
					&& !Incoming.HeldItem.bTemporarilyRemoved
					&& FBattleItemRules::ShouldBypassEntryHazards(
						Incoming.HeldItem.CurrentItemId,
						Incoming.HeldItem.bSuppressed);
				Facts.bMajorStatusPrevented = StatusApplication.Outcome
					!= EBattleMajorStatusApplicationOutcome::CanApply;
				bool bMistTriggerActive = false;
				if (!TryIsFieldSideConditionActiveForPhase(
						FBattleFieldSideConditionRules::GetMistId(),
						Active.ActiveSlotId.GetSide(),
						EBattleTriggerPhase::BeforeHit,
						Active.ActiveSlotId,
						bMistTriggerActive))
				{
					return false;
				}
				Facts.bStatStageDropPrevented =
					FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
						bMistTriggerActive,
						bHazardAppliedByOpponent,
						false,
						-1);
				Facts.RockEffectiveness = RockEffectiveness;
				const bool bDamagingHazardWouldApply =
					(HazardId == FBattleFieldSideConditionRules::GetSpikesId()
						&& bGrounded)
					|| (HazardId
							== FBattleFieldSideConditionRules::GetStealthRockId()
						&& !RockEffectiveness.IsImmune());
				const bool bMagicGuardWouldPrevent = bDamagingHazardWouldApply
					&& FBattleAbilityRules::ShouldMagicGuardPreventDamage(
						Incoming.AbilityId,
						EBattleHPChangeSourceKind::Condition,
						Incoming.bAbilitySuppressed);
				const BattleEntryHazardPrevention::FResult Prevention =
					BattleEntryHazardPrevention::Resolve(
						bBootsBypassActive,
						bMagicGuardWouldPrevent);
				Facts.bBypassesEntryHazards = Prevention.bBypassesEntryHazards;
				Facts.bIndirectDamagePrevented = Prevention.bIndirectDamagePrevented;
				if (Facts.bIndirectDamagePrevented)
				{
					FBattleAbilityItemEffectRequest AbilityRequest;
					if (!TryGetAbilityEffectRequest(
							Incoming,
							EBattleTriggerPhase::SwitchIn,
							EBattleAbilityItemHookPoint::FinalDamage,
							AbilityRequest)
						|| !TryRecordAbilityActivation(
							AbilityRequest,
							EBattleAbilityItemActivationOutcome::Applied,
							Incoming))
					{
						return false;
					}
				}
				FBattleHazardSwitchInResult HazardResult;
				if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
					Facts,
					HazardResult))
				{
					return false;
				}
				if (bLevitateMadeAirborne)
				{
					FBattleHazardSwitchInFacts GroundedFacts = Facts;
					GroundedFacts.bGrounded = true;
					FBattleHazardSwitchInResult GroundedResult;
					if (!FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
							GroundedFacts,
							GroundedResult))
					{
						return false;
					}
					if (HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::None
						&& GroundedResult.EffectKind
							!= EBattleHazardSwitchInEffectKind::None
						&& !TryRecordLevitateGroundedActivation(
							Incoming,
							EBattleTriggerPhase::SwitchIn,
							EBattleAbilityItemHookPoint::SwitchIn))
					{
						return false;
					}
				}

				FBattleResolvedTarget ResolvedIncoming;
				FBattleBattlerTarget IncomingTarget;
				IncomingTarget.ActiveSlotId = Active.ActiveSlotId;
				IncomingTarget.BattlerId = Incoming.BattlerId;
				if (!FBattleResolvedTarget::TryCreateBattler(
						IncomingTarget,
						ResolvedIncoming))
				{
					return false;
				}
				FBattleEventTarget EventTarget;
				if (!TryBuildEventTarget(ResolvedIncoming, EventTarget))
				{
					return false;
				}
				FBattleEventSource HazardSource;
				HazardSource.DefinitionId = HazardId.GetDefinitionId();
				if (SourceBattler != nullptr)
				{
					HazardSource.TrainerId = SourceBattler->TrainerId;
					HazardSource.BattlerId = SourceBattler->BattlerId;
					const FBattleActivePositionState* SourceActive = FindActiveForBattler(
						SourceBattler->BattlerId);
					if (SourceActive != nullptr)
					{
						HazardSource.ActiveSlotId = SourceActive->ActiveSlotId;
					}
				}

				if (HazardResult.EffectKind == EBattleHazardSwitchInEffectKind::Damage)
				{
					const int32 PreviousHP = Incoming.CurrentHP;
					const int32 AppliedDamage = FMath::Min(PreviousHP, HazardResult.Damage);
					Incoming.CurrentHP -= AppliedDamage;
					if (Incoming.CurrentHP == 0)
					{
						Incoming.bFainted = true;
						Incoming.bFaintTransitionPending = true;
					}
					for (const EBattleEventType EventType : {
						EBattleEventType::Damage,
						EBattleEventType::HPChanged})
					{
						FBattleEffectExecutionEvent& Event =
							ExecutionResult->Events.AddDefaulted_GetRef();
						Event.Type = EventType;
						Event.Cause = EBattleEventCause::Rule;
						Event.Outcome = EBattleEffectExecutionOutcome::Applied;
						Event.SourceOverride = HazardSource;
						Event.Targets.Add(EventTarget);
						Event.NumericBefore = PreviousHP;
						Event.NumericAfter = Incoming.CurrentHP;
						Event.NumericDelta = -AppliedDamage;
					}
				}
				else if (HazardResult.EffectKind
					== EBattleHazardSwitchInEffectKind::ApplyMajorStatus)
				{
					FBattleTriggerSubject Owner;
					EBattleTriggerError Error = EBattleTriggerError::None;
					if (!FBattleTriggerSubject::TryCreateBattler(Incoming.BattlerId, Owner)
						|| !FBattleMajorStatusRules::TryRegisterTriggers(
							TriggerFramework,
							HazardResult.MajorStatusId,
							Owner,
							TOptional<int32>(),
							Error))
					{
						return false;
					}
					DrainTriggerOutputs();
					Incoming.MajorStatusId = HazardResult.MajorStatusId;
					FBattleEffectExecutionEvent& Event =
						ExecutionResult->Events.AddDefaulted_GetRef();
					Event.Type = EBattleEventType::StatusChanged;
					Event.Cause = EBattleEventCause::Rule;
					Event.Outcome = EBattleEffectExecutionOutcome::Applied;
					Event.SourceOverride = HazardSource;
					Event.Targets.Add(EventTarget);
					Event.NumericBefore = 0;
					Event.NumericAfter = 1;
					Event.NumericDelta = 1;
				}
				else if (HazardResult.EffectKind
					== EBattleHazardSwitchInEffectKind::ModifyStatStage)
				{
					const FBattleStatStageChangeResult Change = Incoming.Stages.ApplyChange(
						HazardResult.Stat,
						HazardResult.StatStageDelta);
					if (Change.Outcome == EBattleStatStageChangeOutcome::Applied)
					{
						FBattleEffectExecutionEvent& Event =
							ExecutionResult->Events.AddDefaulted_GetRef();
						Event.Type = EBattleEventType::StatStageChanged;
						Event.Cause = EBattleEventCause::Rule;
						Event.Outcome = EBattleEffectExecutionOutcome::Applied;
						Event.SourceOverride = HazardSource;
						Event.Targets.Add(EventTarget);
						Event.NumericBefore = Change.PreviousStage;
						Event.NumericAfter = Change.NewStage;
						Event.NumericDelta = Change.AppliedDelta;
					}
				}
				else if (HazardResult.EffectKind
					== EBattleHazardSwitchInEffectKind::RemoveHazard)
				{
					if (!TryCleanupFieldSideCondition(
							HazardId,
							Active.ActiveSlotId.GetSide(),
							EBattleTriggerCleanupReason::Removal))
					{
						return false;
					}
					const int32 PreviousLayers = HazardRequest.Layers;
					const FConditionId RemovedId = HazardId;
					Side->Hazards.RemoveAll(
						[&RemovedId](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId == RemovedId;
						});
					FBattleResolvedTarget ResolvedSide;
					if (!FBattleResolvedTarget::TryCreateSide(
							Active.ActiveSlotId.GetSide(),
							ResolvedSide))
					{
						return false;
					}
					FBattleEventTarget SideTarget;
					if (!TryBuildEventTarget(ResolvedSide, SideTarget))
					{
						return false;
					}
					FBattleEffectExecutionEvent& Event =
						ExecutionResult->Events.AddDefaulted_GetRef();
					Event.Type = EBattleEventType::FieldEffectChanged;
					Event.Cause = EBattleEventCause::Rule;
					Event.Outcome = EBattleEffectExecutionOutcome::Applied;
					Event.SourceOverride = HazardSource;
					Event.Targets.Add(SideTarget);
					Event.NumericBefore = PreviousLayers;
					Event.NumericAfter = 0;
					Event.NumericDelta = -PreviousLayers;
				}
				if (!TryRunImmediateHeldItemUpdate(Incoming))
				{
					return false;
				}
				if ((Incoming.CurrentHP <= 0 || Incoming.bFainted)
					&& !TryCleanupItemHooks(
						Incoming,
						Incoming.HeldItem.CurrentItemId,
						EBattleTriggerCleanupReason::Faint))
				{
					return false;
				}
			}
			return true;
		}

		FBattleConditionState MakeConditionState(const FBattleMoveEffectDescriptor& Effect)
		{
			FBattleConditionState Condition;
			Condition.ConditionId = Effect.ConditionId;
			if (Effect.DurationTurns > 0)
			{
				Condition.RemainingTurns = Effect.DurationTurns;
			}
			Condition.LayerCount = Effect.LayerCount;
			Condition.CreationOrdinal = NextConditionCreationOrdinal++;
			Condition.SourceBattlerId = Request.UserBattlerId;
			return Condition;
		}

		static const FBattleConditionState* FindVolatile(
			const FBattleBattlerState& Battler,
			const FConditionId& VolatileId)
		{
			return Battler.Volatiles.FindByPredicate(
				[&VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
		}

		static FBattleConditionState* FindMutableVolatile(
			FBattleBattlerState& Battler,
			const FConditionId& VolatileId)
		{
			return Battler.Volatiles.FindByPredicate(
				[&VolatileId](const FBattleConditionState& Condition)
				{
					return Condition.ConditionId == VolatileId;
				});
		}

		static bool HasVolatile(
			const FBattleBattlerState& Battler,
			const FConditionId& VolatileId)
		{
			return FindVolatile(Battler, VolatileId) != nullptr;
		}

		bool IsVolatileActiveForPhase(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const EBattleTriggerPhase Phase) const
		{
			const FBattleBattlerState* Battler = FindBattler(BattlerId);
			if (Battler == nullptr || !HasVolatile(*Battler, VolatileId))
			{
				return false;
			}
			for (const FBattleTriggerRegistrationState& Registration :
				TriggerFramework.GetActiveRegistrations())
			{
				if (!Registration.bSuppressed
					&& Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
					&& Registration.Spec.Rule.Phase == Phase)
				{
					return true;
				}
			}
			return false;
		}

		bool TryGetVolatilePayloadMoveId(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			FMoveId& OutMoveId) const
		{
			OutMoveId = FMoveId();
			for (const FBattleTriggerRegistrationState& Registration :
				TriggerFramework.GetActiveRegistrations())
			{
				if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == VolatileId)
				{
					return FMoveId::TryCreate(
						Registration.Spec.Rule.PayloadId,
						OutMoveId);
				}
			}
			return false;
		}

		bool TryAppendRandomDraw(
			const FBattleResolvedTarget& Target,
			const FBattleRandomDraw& Draw)
		{
			return ExecutionResult != nullptr
				&& TryAddRandomEvent(
					*this,
					*ExecutionResult,
					Target,
					Draw,
					EBattleEffectExecutionOutcome::Applied);
		}

		bool TryRegisterVolatile(
			FBattleBattlerState& Battler,
			const FConditionId& VolatileId,
			const FDefinitionId& PayloadId,
			const FBattleTriggerSubject& Source,
			const TArray<FBattleTriggerSubject>& Targets,
			const TOptional<int32>& RemainingTurns,
			const int32 Layers,
			const bool bSuppressed = false)
		{
			FBattleTriggerSubject Owner;
			FBattleVolatileTriggerRegistrationFacts Facts;
			Facts.VolatileId = VolatileId;
			Facts.PayloadId = PayloadId;
			Facts.Source = Source;
			Facts.Targets = Targets;
			Facts.RemainingTurns = RemainingTurns;
			Facts.Layers = Layers;
			Facts.bSuppressed = bSuppressed;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner))
			{
				return false;
			}
			Facts.Owner = Owner;
			if (!FBattleVolatileRules::TryRegisterTriggers(
				TriggerFramework,
				Facts,
				Error))
			{
				return false;
			}
			DrainTriggerOutputs();

			FBattleConditionState Condition;
			Condition.ConditionId = VolatileId;
			Condition.RemainingTurns = RemainingTurns;
			Condition.LayerCount = Layers;
			Condition.CreationOrdinal = NextConditionCreationOrdinal++;
			Condition.SourceBattlerId = Request.UserBattlerId;
			Battler.Volatiles.Add(MoveTemp(Condition));
			return true;
		}

		bool TryCleanupVolatile(
			const FBattleBattlerState& Battler,
			const FConditionId& VolatileId,
			const EBattleTriggerCleanupReason Reason)
		{
			FBattleTriggerSubject Owner;
			FBattleTriggerOperationContext Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !TryTakeTriggerContext(Operation)
				|| !FBattleVolatileRules::TryCleanupTriggers(
					TriggerFramework,
					VolatileId,
					Owner,
					Reason,
					Operation,
					Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryCleanupAllOwnedVolatiles(
			const FBattleBattlerState& Battler,
			const EBattleTriggerCleanupReason Reason)
		{
			TArray<FConditionId> Ids;
			for (const FBattleConditionState& Condition : Battler.Volatiles)
			{
				if (FBattleVolatileRules::IsCanonical(Condition.ConditionId))
				{
					Ids.Add(Condition.ConditionId);
				}
			}
			for (const FConditionId& Id : Ids)
			{
				if (!TryCleanupVolatile(Battler, Id, Reason))
				{
					return false;
				}
			}
			return true;
		}

		bool TryCleanupSourceDependentVolatiles(
			const FBattlerId SourceBattlerId,
			const EBattleTriggerCleanupReason Reason)
		{
			for (FBattleBattlerState& Candidate : Battlers)
			{
				TArray<FConditionId> ToRemove;
				for (const FBattleConditionState& Condition : Candidate.Volatiles)
				{
					if (Condition.SourceBattlerId == SourceBattlerId
						&& (Condition.ConditionId == FBattleVolatileRules::GetPartialTrapId()
							|| Condition.ConditionId == FBattleVolatileRules::GetTrapId()))
					{
						ToRemove.Add(Condition.ConditionId);
					}
				}
				for (const FConditionId& Id : ToRemove)
				{
					if (!TryCleanupVolatile(Candidate, Id, Reason))
					{
						return false;
					}
					Candidate.Volatiles.RemoveAll(
						[&Id](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId == Id;
						});
				}
			}
			return true;
		}

		bool TrySetVolatileLayers(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const int32 Layers)
		{
			FBattleTriggerOperationContext Operation;
			if (Layers <= 0 || !TryTakeTriggerContext(Operation))
			{
				return false;
			}
			EBattleTriggerError Error = EBattleTriggerError::None;
			const TArray<FBattleTriggerRegistrationState> Registrations =
				TriggerFramework.GetActiveRegistrations();
			for (const FBattleTriggerRegistrationState& Registration : Registrations)
			{
				if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
					&& !TriggerFramework.TryUpdateLayers(
						Registration.RegistrationId,
						Layers,
						Operation,
						Error))
				{
					return false;
				}
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TrySetVolatileSuppressed(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const bool bSuppressed)
		{
			FBattleTriggerOperationContext Operation;
			if (!TryTakeTriggerContext(Operation))
			{
				return false;
			}
			EBattleTriggerError Error = EBattleTriggerError::None;
			const TArray<FBattleTriggerRegistrationState> Registrations =
				TriggerFramework.GetActiveRegistrations();
			for (const FBattleTriggerRegistrationState& Registration : Registrations)
			{
				if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
					&& !TriggerFramework.TrySetSuppressed(
						Registration.RegistrationId,
						bSuppressed,
						Operation,
						Error))
				{
					return false;
				}
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TrySetVolatilePhaseSuppressed(
			const FBattlerId BattlerId,
			const FConditionId& VolatileId,
			const EBattleTriggerPhase Phase,
			const bool bSuppressed)
		{
			FBattleTriggerOperationContext Operation;
			if (!TryTakeTriggerContext(Operation))
			{
				return false;
			}
			bool bUpdated = false;
			EBattleTriggerError Error = EBattleTriggerError::None;
			const TArray<FBattleTriggerRegistrationState> Registrations =
				TriggerFramework.GetActiveRegistrations();
			for (const FBattleTriggerRegistrationState& Registration : Registrations)
			{
				if (Registration.Spec.Owner.Kind == EBattleTriggerSubjectKind::Battler
					&& Registration.Spec.Owner.BattlerId == BattlerId
					&& Registration.Spec.SourceDefinition.Kind
						== EBattleTriggerSourceDefinitionKind::Condition
					&& Registration.Spec.SourceDefinition.ConditionId == VolatileId
					&& Registration.Spec.Rule.Phase == Phase)
				{
					if (!TriggerFramework.TrySetSuppressed(
							Registration.RegistrationId,
							bSuppressed,
							Operation,
							Error))
					{
						return false;
					}
					bUpdated = true;
				}
			}
			DrainTriggerOutputs();
			return bUpdated;
		}

		bool TryBuildVolatileSource(
			const FConditionId& VolatileId,
			FBattleTriggerSubject& OutSource) const
		{
			if (VolatileId == FBattleVolatileRules::GetLeechSeedId())
			{
				return FBattleTriggerSubject::TryCreateActiveSlot(
					Request.UserSlotId,
					OutSource);
			}
			return FBattleTriggerSubject::TryCreateBattler(
				Request.UserBattlerId,
				OutSource);
		}

		bool HasActedThisTurn(const FBattlerId BattlerId) const
		{
			return State.LockedActions.ContainsByPredicate(
				[BattlerId](const FBattleLockedActionState& Action)
				{
					return Action.Decision.GetActingBattlerId() == BattlerId
						&& (Action.bStarted || Action.bFinished);
				});
		}

		int32 GetCurrentPP(const FBattleBattlerState& Battler, const FMoveId MoveId) const
		{
			if (!MoveId.IsValid())
			{
				return 0;
			}
			if (MoveId == FBattleBuiltInMoveDefinitions::GetStruggleMoveId())
			{
				return 1;
			}
			const FBattleMoveSlotState* Slot = Battler.Moves.FindByPredicate(
				[MoveId](const FBattleMoveSlotState& Candidate)
				{
					return Candidate.MoveId == MoveId;
				});
			return Slot != nullptr ? Slot->CurrentPP : 0;
		}

		FBattleEffectHookResult ApplyCanonicalVolatile(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target,
			FBattleBattlerState& Battler)
		{
			const FBattleSpeciesFormDefinition* Species = State.Catalog.FindSpeciesForm(
				Battler.SpeciesFormId);
			if (Species == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			const EBattleVolatileKind Kind = FBattleVolatileRules::GetKind(Effect.ConditionId);
			FBattleVolatileApplicationFacts Facts;
			Facts.RequestedVolatileId = Effect.ConditionId;
			Facts.bAlreadyPresent = HasVolatile(Battler, Effect.ConditionId);
			Facts.PrimaryType = Species->PrimaryType;
			Facts.SecondaryType = Species->SecondaryType;
			bool bLevitateMadeAirborne = false;
			if (!TryIsGrounded(
					Battler,
					Facts.bTargetGrounded,
					ShouldIgnoreLevitateForCurrentMove(Battler),
					&bLevitateMadeAirborne))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const FConditionId TerrainId = GetTerrainId();
			bool bTerrainTriggerActive = false;
			if (FBattleFieldSideConditionRules::IsCanonical(TerrainId)
				&& !TryIsFieldSideConditionActiveForPhase(
					TerrainId,
					TOptional<EBattleSide>(),
					EBattleTriggerPhase::BeforeHit,
					Target.GetBattler().ActiveSlotId,
					bTerrainTriggerActive))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Facts.bMistyTerrainActive = bTerrainTriggerActive
				&& FBattleFieldSideConditionRules::ShouldTerrainPreventConfusion(
					TerrainId,
					Facts.bTargetGrounded);
			Facts.bAppliedByOpponent = Target.GetBattler().ActiveSlotId.GetSide()
				!= Request.UserSlotId.GetSide();
			if (!TryIsFieldSideConditionActiveForPhase(
					FBattleFieldSideConditionRules::GetSafeguardId(),
					Target.GetBattler().ActiveSlotId.GetSide(),
					EBattleTriggerPhase::BeforeHit,
					Target.GetBattler().ActiveSlotId,
					Facts.bSafeguardActive))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Facts.bBypassesSafeguard = EnumHasAllFlags(
				Request.Move->Flags,
				EBattleMoveFlags::BypassesSideProtection);
			Facts.bTargetAlreadyActed = HasActedThisTurn(Battler.BattlerId);
			Facts.LastMoveId = Battler.LastMoveId;
			Facts.LastMoveCurrentPP = GetCurrentPP(Battler, Battler.LastMoveId);
			Facts.bLastMoveIsStruggle = Battler.LastMoveId
				== FBattleBuiltInMoveDefinitions::GetStruggleMoveId();
			const FBattleMoveDefinition* LastMove = Facts.bLastMoveIsStruggle
				? &FBattleBuiltInMoveDefinitions::GetStruggle()
				: State.Catalog.FindMove(Battler.LastMoveId);
			Facts.bLastMoveUnencoreable = LastMove != nullptr
				&& EnumHasAllFlags(LastMove->Flags, EBattleMoveFlags::Unencoreable);
			Facts.BaseMaximumHP = Battler.PermanentStats.MaxHP;
			Facts.CurrentHP = Battler.CurrentHP;

			FBattleVolatileApplicationResult Application;
			if (!FBattleVolatileRules::TryEvaluateApplication(Facts, Application))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Application.Outcome == EBattleVolatileApplicationOutcome::TypeImmune)
			{
				return Outcome(EBattleEffectExecutionOutcome::Immune);
			}
			if (Application.Outcome == EBattleVolatileApplicationOutcome::PreventedByTerrain
				|| Application.Outcome == EBattleVolatileApplicationOutcome::PreventedBySafeguard)
			{
				return Outcome(EBattleEffectExecutionOutcome::Prevented);
			}
			if (Application.Outcome != EBattleVolatileApplicationOutcome::CanApply)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Kind == EBattleVolatileKind::Confusion
				&& bLevitateMadeAirborne
				&& bTerrainTriggerActive
				&& FBattleFieldSideConditionRules::ShouldTerrainPreventConfusion(
					TerrainId,
					true)
				&& !TryRecordLevitateGroundedActivation(
					Battler,
					EBattleTriggerPhase::BeforeHit,
					EBattleAbilityItemHookPoint::TypeImmunity))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			TOptional<int32> RemainingTurns;
			int32 Layers = 1;
			TOptional<FBattleRandomDraw> DurationDraw;
			FBattleRandomContext RandomContext;
			RandomContext.BattleId = Request.BattleId;
			RandomContext.TurnId = Request.TurnId;
			RandomContext.ActionId = Request.ActionId;
			RandomContext.ResolutionId = Request.ResolutionId;
			RandomContext.RulePurpose = Effect.ConditionId.GetDefinitionId();
			if (Kind == EBattleVolatileKind::Confusion)
			{
				FBattleVolatileDurationResult Duration;
				if (!State.Random.IsValid()
					|| !FBattleVolatileRules::TryRollConfusionDuration(
						RandomContext,
						*State.Random,
						Duration))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				RemainingTurns = Duration.Turns;
				DurationDraw = Duration.Draw;
			}
			else if (Kind == EBattleVolatileKind::PartialTrap)
			{
				FBattleVolatileDurationResult Duration;
				if (!State.Random.IsValid()
					|| !FBattleVolatileRules::TryRollPartialTrapDuration(
						RandomContext,
						*State.Random,
						Duration))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				RemainingTurns = Duration.Turns;
				DurationDraw = Duration.Draw;
			}
			else if (Kind == EBattleVolatileKind::Taunt)
			{
				RemainingTurns = FBattleVolatileRules::GetTauntDuration(
					Facts.bTargetAlreadyActed);
			}
			else if (Kind == EBattleVolatileKind::Encore)
			{
				RemainingTurns = FBattleVolatileRules::GetEncoreDuration();
			}
			else if (Kind == EBattleVolatileKind::Disable)
			{
				RemainingTurns = FBattleVolatileRules::GetDisableDuration();
			}
			else if (Kind == EBattleVolatileKind::Substitute)
			{
				FBattleSubstituteCreationResult Creation;
				if (!FBattleVolatileRules::TryResolveSubstituteCreation(
						Battler.PermanentStats.MaxHP,
						Battler.CurrentHP,
						Creation)
					|| !Creation.bCanCreate)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Layers = Creation.SubstituteHP;
			}

			FDefinitionId PayloadId = Effect.ConditionId.GetDefinitionId();
			if (Kind == EBattleVolatileKind::Encore || Kind == EBattleVolatileKind::Disable)
			{
				PayloadId = Battler.LastMoveId.GetDefinitionId();
			}
			FBattleTriggerSubject Source;
			TArray<FBattleTriggerSubject> Targets;
			if (!TryBuildVolatileSource(Effect.ConditionId, Source)
				|| !TryRegisterVolatile(
					Battler,
					Effect.ConditionId,
					PayloadId,
					Source,
					Targets,
					RemainingTurns,
					Layers))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			if (DurationDraw.IsSet() && !TryAppendRandomDraw(Target, DurationDraw.GetValue()))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Kind == EBattleVolatileKind::Substitute)
			{
				const int32 PreviousHP = Battler.CurrentHP;
				Battler.CurrentHP -= Layers;
				if (ExecutionResult == nullptr)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				FBattleEffectExecutionEvent& CostEvent =
					ExecutionResult->Events.AddDefaulted_GetRef();
				CostEvent.Type = EBattleEventType::HPChanged;
				CostEvent.Cause = EBattleEventCause::Move;
				CostEvent.Outcome = EBattleEffectExecutionOutcome::Applied;
				FBattleEventTarget EventTarget;
				if (!TryBuildEventTarget(Target, EventTarget))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				CostEvent.Targets.Add(MoveTemp(EventTarget));
				CostEvent.NumericBefore = PreviousHP;
				CostEvent.NumericAfter = Battler.CurrentHP;
				CostEvent.NumericDelta = -Layers;
			}

			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = 1;
			Result.bStateMutated = true;
			return Result;
		}

		FBattleEffectHookResult ApplySimpleSpecialVolatile(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target,
			const FConditionId& ExpectedId)
		{
			if (Effect.ConditionId != ExpectedId
				|| Target.GetKind() != EBattleResolvedTargetKind::Battler
				|| Target.GetBattler().BattlerId != Request.UserBattlerId)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleBattlerState* Battler = FindMutableBattler(Request.UserBattlerId);
			const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(ExpectedId);
			if (Battler == nullptr
				|| Definition == nullptr
				|| Definition->Kind != EBattleConditionKind::Volatile
				|| HasVolatile(*Battler, ExpectedId))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleTriggerSubject Source;
			TArray<FBattleTriggerSubject> Targets;
			if (!FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source)
				|| !TryRegisterVolatile(
					*Battler,
					ExpectedId,
					ExpectedId.GetDefinitionId(),
					Source,
					Targets,
					TOptional<int32>(),
					1))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = 1;
			Result.bStateMutated = true;
			return Result;
		}

		FBattleEffectHookResult ApplyCharge(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			if (Effect.ConditionId != FBattleVolatileRules::GetChargingId()
				|| Target.GetKind() != EBattleResolvedTargetKind::Battler
				|| Target.GetBattler().BattlerId != Request.UserBattlerId)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleBattlerState* Battler = FindMutableBattler(Request.UserBattlerId);
			const FBattleConditionDefinition* ChargeDefinition = State.Catalog.FindCondition(
				FBattleVolatileRules::GetChargingId());
			if (Battler == nullptr
				|| ChargeDefinition == nullptr
				|| ChargeDefinition->Kind != EBattleConditionKind::Volatile
				|| HasVolatile(*Battler, FBattleVolatileRules::GetChargingId()))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			const FBattleMoveEffectDescriptor* FlyDescriptor = Request.Move->Effects.FindByPredicate(
				[](const FBattleMoveEffectDescriptor& Candidate)
				{
					return Candidate.Kind == EBattleMoveEffectKind::SemiInvulnerability;
				});
			if (FlyDescriptor != nullptr)
			{
				const FBattleConditionDefinition* FlyDefinition = State.Catalog.FindCondition(
					FlyDescriptor->ConditionId);
				if (FlyDescriptor->ConditionId != FBattleVolatileRules::GetFlySemiInvulnerableId()
					|| FlyDefinition == nullptr
					|| FlyDefinition->Kind != EBattleConditionKind::Volatile
					|| HasVolatile(*Battler, FlyDescriptor->ConditionId))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}

			FBattleTriggerSubject Source;
			TArray<FBattleTriggerSubject> LockedTargets;
			if (!FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			for (const FBattleResolvedTarget& Resolved : Request.Targets)
			{
				if (Resolved.GetKind() == EBattleResolvedTargetKind::Battler)
				{
					FBattleTriggerSubject TargetSlotSubject;
					FBattleTriggerSubject TargetBattlerSubject;
					if (!FBattleTriggerSubject::TryCreateActiveSlot(
							Resolved.GetBattler().ActiveSlotId,
							TargetSlotSubject)
						|| !FBattleTriggerSubject::TryCreateBattler(
							Resolved.GetBattler().BattlerId,
							TargetBattlerSubject))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					LockedTargets.Add(MoveTemp(TargetSlotSubject));
					LockedTargets.Add(MoveTemp(TargetBattlerSubject));
					break;
				}
			}

			const FBattleTriggerFramework FrameworkBefore = TriggerFramework;
			const TArray<FBattleConditionState> VolatilesBefore = Battler->Volatiles;
			const uint64 OrdinalBefore = NextConditionCreationOrdinal;
			if (!TryRegisterVolatile(
					*Battler,
					FBattleVolatileRules::GetChargingId(),
					Request.Move->Id.GetDefinitionId(),
					Source,
					LockedTargets,
					TOptional<int32>(),
					1)
				|| (FlyDescriptor != nullptr
					&& !TryRegisterVolatile(
						*Battler,
						FlyDescriptor->ConditionId,
						FlyDescriptor->ConditionId.GetDefinitionId(),
						Source,
						TArray<FBattleTriggerSubject>(),
						TOptional<int32>(),
						1)))
			{
				TriggerFramework = FrameworkBefore;
				Battler->Volatiles = VolatilesBefore;
				NextConditionCreationOrdinal = OrdinalBefore;
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = 1;
			Result.bStateMutated = true;
			Result.bDefersMove = true;
			return Result;
		}

		FBattleEffectHookResult ApplyProtect(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			if (Effect.ConditionId != FBattleVolatileRules::GetProtectId()
				|| Target.GetKind() != EBattleResolvedTargetKind::Battler
				|| Target.GetBattler().BattlerId != Request.UserBattlerId)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleBattlerState* Battler = FindMutableBattler(Request.UserBattlerId);
			const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(
				Effect.ConditionId);
			if (Battler == nullptr
				|| Definition == nullptr
				|| Definition->Kind != EBattleConditionKind::Volatile)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleConditionState* Existing = FindMutableVolatile(*Battler, Effect.ConditionId);
			FBattleProtectAttemptFacts Facts;
			Facts.bHasQueuedAction = true;
			Facts.bConsecutiveEligibleUse = Existing != nullptr;
			Facts.ChainCounter = Existing != nullptr ? Existing->LayerCount : 0;
			FBattleRandomContext RandomContext;
			RandomContext.BattleId = Request.BattleId;
			RandomContext.TurnId = Request.TurnId;
			RandomContext.ActionId = Request.ActionId;
			RandomContext.ResolutionId = Request.ResolutionId;
			RandomContext.RulePurpose = FBattleVolatileRules::GetProtectConsecutiveUsePurpose();
			FBattleProtectAttemptResult Attempt;
			if (!State.Random.IsValid()
				|| !FBattleVolatileRules::TryResolveProtectAttempt(
					Facts,
					RandomContext,
					*State.Random,
					Attempt)
				|| (Attempt.bDrawConsumed && !TryAppendRandomDraw(Target, Attempt.Draw)))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			if (!Attempt.bSucceeded)
			{
				if (Existing != nullptr)
				{
					if (!TryCleanupVolatile(
							*Battler,
							Effect.ConditionId,
							EBattleTriggerCleanupReason::Removal))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Battler->Volatiles.RemoveAll(
						[&Effect](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId == Effect.ConditionId;
						});
				}
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}

			if (Existing == nullptr)
			{
				FBattleTriggerSubject Source;
				if (!FBattleTriggerSubject::TryCreateBattler(Request.UserBattlerId, Source)
					|| !TryRegisterVolatile(
						*Battler,
						Effect.ConditionId,
						Effect.ConditionId.GetDefinitionId(),
						Source,
						TArray<FBattleTriggerSubject>(),
						TOptional<int32>(),
						Attempt.NextChainCounter))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			else
			{
				if (!TrySetVolatileLayers(
						Battler->BattlerId,
						Effect.ConditionId,
						Attempt.NextChainCounter)
					|| !TrySetVolatileSuppressed(
						Battler->BattlerId,
						Effect.ConditionId,
						false))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Existing->LayerCount = Attempt.NextChainCounter;
			}

			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = Facts.bConsecutiveEligibleUse ? Facts.ChainCounter : 0;
			Result.NumericAfter = Attempt.NextChainCounter;
			Result.NumericDelta = Attempt.NextChainCounter - Result.NumericBefore.GetValue();
			Result.bStateMutated = true;
			return Result;
		}

		FBattleEffectHookResult ApplyCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
			const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
			if (Battler == nullptr || Definition == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = 1;
			Result.bStateMutated = true;
			if (Definition->Kind == EBattleConditionKind::MajorStatus)
			{
				if (Battler->MajorStatusId.IsValid())
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (FBattleMajorStatusRules::IsCanonical(Effect.ConditionId))
				{
					TOptional<int32> SleepTurns;
					if (Effect.ConditionId == FBattleMajorStatusRules::GetSleepId())
					{
						FBattleRandomContext BaseContext;
						BaseContext.BattleId = Request.BattleId;
						BaseContext.TurnId = Request.TurnId;
						BaseContext.ActionId = Request.ActionId;
						BaseContext.ResolutionId = Request.ResolutionId;
						BaseContext.RulePurpose = FBattleMajorStatusRules::GetSleepDurationPurpose();
						FBattleSleepDurationResult Duration;
						if (!State.Random.IsValid()
							|| !FBattleMajorStatusRules::TryRollSleepDuration(
								BaseContext,
								*State.Random,
								Duration))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
						SleepTurns = Duration.Turns;
					}
					FBattleTriggerSubject Owner;
					EBattleTriggerError TriggerError = EBattleTriggerError::None;
					if (!FBattleTriggerSubject::TryCreateBattler(Battler->BattlerId, Owner)
						|| !FBattleMajorStatusRules::TryRegisterTriggers(
							TriggerFramework,
							Effect.ConditionId,
							Owner,
							SleepTurns,
							TriggerError))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					DrainTriggerOutputs();
				}
				Battler->MajorStatusId = Effect.ConditionId;
				return Result;
			}
			if (Definition->Kind == EBattleConditionKind::Volatile)
			{
				if (FBattleVolatileRules::IsCanonical(Effect.ConditionId))
				{
					return ApplyCanonicalVolatile(Effect, Target, *Battler);
				}
				if (Battler->Volatiles.ContainsByPredicate(
					[&Effect](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Effect.ConditionId;
					}))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Battler->Volatiles.Add(MakeConditionState(Effect));
				return Result;
			}
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}

		FBattleEffectHookResult ApplyStatStage(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
			if (Battler == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const FBattleStatStageChangeResult Change = Battler->Stages.ApplyChange(
				Effect.Stat,
				Effect.MagnitudeNumerator);
			FBattleEffectHookResult Result;
			Result.NumericBefore = Change.PreviousStage;
			Result.NumericAfter = Change.NewStage;
			Result.NumericDelta = Change.AppliedDelta;
			if (Change.Outcome == EBattleStatStageChangeOutcome::Blocked)
			{
				Result.Outcome = EBattleEffectExecutionOutcome::Capped;
				Result.bCapped = true;
				return Result;
			}
			if (Change.Outcome != EBattleStatStageChangeOutcome::Applied)
			{
				Result.Outcome = EBattleEffectExecutionOutcome::Failed;
				return Result;
			}
			Result.Outcome = EBattleEffectExecutionOutcome::Applied;
			Result.bStateMutated = true;
			Result.bCapped = Change.bClamped;
			return Result;
		}

		FBattleEffectHookResult SetFieldCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Field)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
			if (Definition == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
			{
				const EBattleConditionKind Family =
					FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId);
				if (Definition->Kind != Family
					|| (Family != EBattleConditionKind::Weather
						&& Family != EBattleConditionKind::Terrain
						&& Family != EBattleConditionKind::Room))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				FBattleFieldSideApplicationFacts Facts;
				Facts.RequestedConditionId = Effect.ConditionId;
				if (Family == EBattleConditionKind::Weather && Field.Weather.IsSet())
				{
					Facts.ExistingExclusiveConditionId = Field.Weather.GetValue().ConditionId;
				}
				else if (Family == EBattleConditionKind::Terrain && Field.Terrain.IsSet())
				{
					Facts.ExistingExclusiveConditionId = Field.Terrain.GetValue().ConditionId;
				}
				Facts.bRequestedAlreadyActive =
					(Family == EBattleConditionKind::Weather
						&& Field.Weather.IsSet()
						&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
					|| (Family == EBattleConditionKind::Terrain
						&& Field.Terrain.IsSet()
						&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
					|| (Family == EBattleConditionKind::Room && HasRoom(Effect.ConditionId));
				Facts.bSnowActive = GetWeatherId()
					== FBattleFieldSideConditionRules::GetSnowId();
				Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
				FBattleFieldSideApplicationResult Application;
				if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}

				if (Application.Outcome == EBattleFieldSideApplicationOutcome::ToggleOff)
				{
					if (!TryCleanupFieldSideCondition(
							Effect.ConditionId,
							TOptional<EBattleSide>(),
							EBattleTriggerCleanupReason::Removal))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Field.Rooms.RemoveAll(
						[&Effect](const FBattleConditionState& Condition)
						{
							return Condition.ConditionId == Effect.ConditionId;
						});
					if (Effect.ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
					{
						if (!TrySetMagicRoomSuppression(false))
						{
							return Outcome(EBattleEffectExecutionOutcome::Failed);
						}
					}
					FBattleEffectHookResult Result = Applied();
					Result.NumericBefore = 1;
					Result.NumericAfter = 0;
					Result.NumericDelta = -1;
					Result.bStateMutated = true;
					return Result;
				}
				if (Application.Outcome != EBattleFieldSideApplicationOutcome::Create
					&& Application.Outcome
						!= EBattleFieldSideApplicationOutcome::ReplaceExclusive)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}

				if (Application.Outcome == EBattleFieldSideApplicationOutcome::ReplaceExclusive)
				{
					const FConditionId ExistingId = Family == EBattleConditionKind::Weather
						? Field.Weather.GetValue().ConditionId
						: Field.Terrain.GetValue().ConditionId;
					if (FBattleFieldSideConditionRules::IsCanonical(ExistingId)
						&& !TryCleanupFieldSideCondition(
							ExistingId,
							TOptional<EBattleSide>(),
							EBattleTriggerCleanupReason::Removal))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				if (!TryRegisterFieldSideCondition(
						Effect.ConditionId,
						TOptional<EBattleSide>(),
						Application.DurationTurns,
						Application.Layers))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				FBattleConditionState NewCondition = MakeCanonicalConditionState(
					Effect.ConditionId,
					Application.DurationTurns,
					Application.Layers);
				if (Family == EBattleConditionKind::Weather)
				{
					Field.Weather = MoveTemp(NewCondition);
				}
				else if (Family == EBattleConditionKind::Terrain)
				{
					Field.Terrain = MoveTemp(NewCondition);
				}
				else
				{
					Field.Rooms.Add(MoveTemp(NewCondition));
				}
				if (Effect.ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
				{
					if (!TrySetMagicRoomSuppression(true))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
				}
				FBattleEffectHookResult Result = Applied();
				Result.NumericBefore = Application.Outcome
					== EBattleFieldSideApplicationOutcome::ReplaceExclusive ? 1 : 0;
				Result.NumericAfter = 1;
				Result.NumericDelta = Application.Outcome
					== EBattleFieldSideApplicationOutcome::ReplaceExclusive ? 0 : 1;
				Result.bStateMutated = true;
				return Result;
			}
			if (Definition->Kind == EBattleConditionKind::Weather)
			{
				if (Field.Weather.IsSet()
					&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Field.Weather.IsSet()
					&& FBattleFieldSideConditionRules::IsCanonical(
						Field.Weather.GetValue().ConditionId)
					&& !TryCleanupFieldSideCondition(
						Field.Weather.GetValue().ConditionId,
						TOptional<EBattleSide>(),
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Field.Weather = MakeConditionState(Effect);
			}
			else if (Definition->Kind == EBattleConditionKind::Terrain)
			{
				if (Field.Terrain.IsSet()
					&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Field.Terrain.IsSet()
					&& FBattleFieldSideConditionRules::IsCanonical(
						Field.Terrain.GetValue().ConditionId)
					&& !TryCleanupFieldSideCondition(
						Field.Terrain.GetValue().ConditionId,
						TOptional<EBattleSide>(),
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Field.Terrain = MakeConditionState(Effect);
			}
			else if (Definition->Kind == EBattleConditionKind::Room)
			{
				if (Field.Rooms.ContainsByPredicate(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					}))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Field.Rooms.Add(MakeConditionState(Effect));
			}
			else
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = 1;
			Result.bStateMutated = true;
			return Result;
		}

		FBattleEffectHookResult SetSideCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			if (Target.GetKind() != EBattleResolvedTargetKind::Side)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			FBattleSideState* Side = FindMutableSide(Target.GetSide());
			const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
			if (Side == nullptr || Definition == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			TArray<FBattleConditionState>* Collection = Definition->Kind == EBattleConditionKind::Hazard
				? &Side->Hazards
				: &Side->Conditions;
			if (FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
			{
				if (Definition->Kind
					!= FBattleFieldSideConditionRules::GetConditionFamily(Effect.ConditionId))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				FBattleConditionState* Existing = Collection->FindByPredicate(
					[&Effect](const FBattleConditionState& Condition)
					{
						return Condition.ConditionId == Effect.ConditionId;
					});
				FBattleFieldSideApplicationFacts Facts;
				Facts.RequestedConditionId = Effect.ConditionId;
				Facts.bRequestedAlreadyActive = Existing != nullptr;
				Facts.ExistingLayers = Existing != nullptr ? Existing->LayerCount : 0;
				Facts.bSnowActive = GetWeatherId()
					== FBattleFieldSideConditionRules::GetSnowId();
				Facts.bDurationExtensionActive = Effect.DurationTurns == 8;
				FBattleFieldSideApplicationResult Application;
				if (!FBattleFieldSideConditionRules::TryEvaluateApplication(Facts, Application))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Application.Outcome == EBattleFieldSideApplicationOutcome::AddLayer)
				{
					if (Existing == nullptr
						|| !TryUpdateFieldSideLayers(
							Effect.ConditionId,
							Target.GetSide(),
							Application.Layers))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					const int32 PreviousLayers = Existing->LayerCount;
					Existing->LayerCount = Application.Layers;
					FBattleEffectHookResult Result = Applied();
					Result.NumericBefore = PreviousLayers;
					Result.NumericAfter = Application.Layers;
					Result.NumericDelta = Application.Layers - PreviousLayers;
					Result.bStateMutated = true;
					return Result;
				}
				if (Application.Outcome != EBattleFieldSideApplicationOutcome::Create
					|| !TryRegisterFieldSideCondition(
						Effect.ConditionId,
						Target.GetSide(),
						Application.DurationTurns,
						Application.Layers))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				Collection->Add(MakeCanonicalConditionState(
					Effect.ConditionId,
					Application.DurationTurns,
					Application.Layers));
				FBattleEffectHookResult Result = Applied();
				Result.NumericBefore = 0;
				Result.NumericAfter = Application.Layers;
				Result.NumericDelta = Application.Layers;
				Result.bStateMutated = true;
				return Result;
			}
			if (Collection->ContainsByPredicate(
				[&Effect](const FBattleConditionState& Existing)
				{
					return Existing.ConditionId == Effect.ConditionId;
				}))
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			Collection->Add(MakeConditionState(Effect));
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 0;
			Result.NumericAfter = 1;
			Result.NumericDelta = 1;
			Result.bStateMutated = true;
			return Result;
		}

		FBattleEffectHookResult RemoveCondition(
			const FBattleMoveEffectDescriptor& Effect,
			const FBattleResolvedTarget& Target)
		{
			const FBattleConditionDefinition* Definition = State.Catalog.FindCondition(Effect.ConditionId);
			if (Definition == nullptr)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (FBattleFieldSideConditionRules::IsCanonical(Effect.ConditionId))
			{
				TOptional<EBattleSide> SideOwner;
				if (Target.GetKind() == EBattleResolvedTargetKind::Side)
				{
					SideOwner = Target.GetSide();
				}
				if (!TryCleanupFieldSideCondition(
						Effect.ConditionId,
						SideOwner,
						EBattleTriggerCleanupReason::Removal))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			bool bRemoved = false;
			if (Target.GetKind() == EBattleResolvedTargetKind::Battler)
			{
				FBattleBattlerState* Battler = FindMutableBattler(Target.GetBattler().BattlerId);
				if (Battler == nullptr)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				if (Definition->Kind == EBattleConditionKind::MajorStatus
					&& Battler->MajorStatusId == Effect.ConditionId)
				{
					if (FBattleMajorStatusRules::IsCanonical(Effect.ConditionId)
						&& !TryCleanupCanonicalStatus(*Battler))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					Battler->MajorStatusId = FConditionId();
					bRemoved = true;
				}
				else if (Definition->Kind == EBattleConditionKind::Volatile)
				{
					if (FBattleVolatileRules::IsCanonical(Effect.ConditionId)
						&& HasVolatile(*Battler, Effect.ConditionId)
						&& !TryCleanupVolatile(
							*Battler,
							Effect.ConditionId,
							EBattleTriggerCleanupReason::Removal))
					{
						return Outcome(EBattleEffectExecutionOutcome::Failed);
					}
					const int32 Removed = Battler->Volatiles.RemoveAll(
						[&Effect](const FBattleConditionState& Existing)
						{
							return Existing.ConditionId == Effect.ConditionId;
						});
					bRemoved = Removed > 0;
				}
			}
			else if (Target.GetKind() == EBattleResolvedTargetKind::Side)
			{
				FBattleSideState* Side = FindMutableSide(Target.GetSide());
				if (Side == nullptr)
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
				const int32 Removed = Side->Conditions.RemoveAll(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					}) + Side->Hazards.RemoveAll(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					});
				bRemoved = Removed > 0;
			}
			else if (Target.GetKind() == EBattleResolvedTargetKind::Field)
			{
				if (Field.Weather.IsSet()
					&& Field.Weather.GetValue().ConditionId == Effect.ConditionId)
				{
					Field.Weather.Reset();
					bRemoved = true;
				}
				if (Field.Terrain.IsSet()
					&& Field.Terrain.GetValue().ConditionId == Effect.ConditionId)
				{
					Field.Terrain.Reset();
					bRemoved = true;
				}
				bRemoved = Field.Rooms.RemoveAll(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					}) > 0 || bRemoved;
				bRemoved = Field.Effects.RemoveAll(
					[&Effect](const FBattleConditionState& Existing)
					{
						return Existing.ConditionId == Effect.ConditionId;
					}) > 0 || bRemoved;
			}

			if (!bRemoved)
			{
				return Outcome(EBattleEffectExecutionOutcome::Failed);
			}
			if (Effect.ConditionId == FBattleFieldSideConditionRules::GetMagicRoomId())
			{
				if (!TrySetMagicRoomSuppression(false))
				{
					return Outcome(EBattleEffectExecutionOutcome::Failed);
				}
			}
			FBattleEffectHookResult Result = Applied();
			Result.NumericBefore = 1;
			Result.NumericAfter = 0;
			Result.NumericDelta = -1;
			Result.bStateMutated = true;
			return Result;
		}

		bool TryTakeTriggerContext(FBattleTriggerOperationContext& OutContext)
		{
			OutContext = FBattleTriggerOperationContext();
			if (NextTriggerReentrancyToken == 0
				|| NextTriggerReentrancyToken == TNumericLimits<uint64>::Max()
				|| !FBattleTriggerReentrancyToken::TryCreate(
					NextTriggerReentrancyToken,
					OutContext.ReentrancyToken))
			{
				return false;
			}
			++NextTriggerReentrancyToken;
			return true;
		}

		bool TryDispatchStatusPhase(
			const FBattleBattlerState& Battler,
			const EBattleTriggerPhase Phase,
			bool& bOutEmitted)
		{
			bOutEmitted = false;
			FBattleTriggerSubject Owner;
			FBattleTriggerSourceDefinition SourceDefinition;
			FBattleTriggerOperationContext Operation;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSourceDefinition::TryCreateCondition(
					Battler.MajorStatusId,
					SourceDefinition)
				|| !TryTakeTriggerContext(Operation))
			{
				return false;
			}
			FBattleTriggerDispatchSpec Dispatch;
			Dispatch.Phase = Phase;
			Dispatch.ReentrancyToken = Operation.ReentrancyToken;
			for (const FBattleTriggerRegistrationState& Registration :
				TriggerFramework.GetActiveRegistrations())
			{
				if (Registration.Spec.SourceDefinition == SourceDefinition
					&& Registration.Spec.Owner == Owner
					&& Registration.Spec.Rule.Phase == Phase)
				{
					FBattleTriggerDispatchParticipant& Participant =
						Dispatch.Participants.AddDefaulted_GetRef();
					Participant.RegistrationId = Registration.RegistrationId;
				}
			}
			if (Dispatch.Participants.IsEmpty())
			{
				return true;
			}

			EBattleTriggerError Error = EBattleTriggerError::None;
			FBattleTriggerDispatchResult Result;
			if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
				|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
			{
				return false;
			}
			TArray<FBattleTriggerEffectRequest> Requests;
			TArray<FBattleTriggerLifecycleFact> Facts;
			TriggerFramework.DrainEffectRequests(Requests);
			TriggerFramework.DrainLifecycleFacts(Facts);
			bOutEmitted = !Requests.IsEmpty();
			return true;
		}

		void DrainTriggerOutputs()
		{
			TArray<FBattleTriggerEffectRequest> IgnoredRequests;
			TArray<FBattleTriggerLifecycleFact> IgnoredFacts;
			TriggerFramework.DrainEffectRequests(IgnoredRequests);
			TriggerFramework.DrainLifecycleFacts(IgnoredFacts);
		}

		bool TryCleanupCanonicalStatus(const FBattleBattlerState& Battler)
		{
			if (!FBattleMajorStatusRules::IsCanonical(Battler.MajorStatusId))
			{
				return true;
			}
			FBattleTriggerSubject Owner;
			FBattleTriggerOperationContext Operation;
			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !TryTakeTriggerContext(Operation)
				|| !FBattleMajorStatusRules::TryCleanupTriggers(
					TriggerFramework,
					Battler.MajorStatusId,
					Owner,
					EBattleTriggerCleanupReason::Removal,
					Operation,
					Error))
			{
				return false;
			}
			DrainTriggerOutputs();
			return true;
		}

		bool TryRunSwitchOutStatus(const FBattleBattlerState& Battler)
		{
			if (Battler.MajorStatusId != FBattleMajorStatusRules::GetToxicId())
			{
				return true;
			}
			FBattleTriggerSubject Owner;
			FBattleTriggerSourceDefinition SourceDefinition;
			FBattleTriggerOperationContext Operation;
			if (!FBattleTriggerSubject::TryCreateBattler(Battler.BattlerId, Owner)
				|| !FBattleTriggerSourceDefinition::TryCreateCondition(
					Battler.MajorStatusId,
					SourceDefinition)
				|| !TryTakeTriggerContext(Operation))
			{
				return false;
			}

			const TArray<FBattleTriggerRegistrationState> Registrations =
				TriggerFramework.GetActiveRegistrations();
			FBattleTriggerDispatchSpec Dispatch;
			Dispatch.Phase = EBattleTriggerPhase::SwitchOut;
			Dispatch.ReentrancyToken = Operation.ReentrancyToken;
			for (const FBattleTriggerRegistrationState& Registration : Registrations)
			{
				if (Registration.Spec.SourceDefinition == SourceDefinition
					&& Registration.Spec.Owner == Owner
					&& Registration.Spec.Rule.Phase == EBattleTriggerPhase::SwitchOut)
				{
					FBattleTriggerDispatchParticipant& Participant =
						Dispatch.Participants.AddDefaulted_GetRef();
					Participant.RegistrationId = Registration.RegistrationId;
				}
			}

			EBattleTriggerError Error = EBattleTriggerError::None;
			if (!Dispatch.Participants.IsEmpty())
			{
				FBattleTriggerDispatchResult Result;
				if (!TriggerFramework.TryEnqueueDispatch(Dispatch, Error)
					|| !TriggerFramework.TryResolveNextDispatch(Result, Error))
				{
					return false;
				}
			}
			for (const FBattleTriggerRegistrationState& Registration : Registrations)
			{
				if (Registration.Spec.SourceDefinition == SourceDefinition
					&& Registration.Spec.Owner == Owner
					&& !TriggerFramework.TryUpdateLayers(
						Registration.RegistrationId,
						FBattleMajorStatusRules::GetResetToxicLayerEncoding(),
						Operation,
						Error))
				{
					return false;
				}
			}
			DrainTriggerOutputs();
			return true;
		}

		const FBattleEffectExecutionRequest& Request;
		FBattleEngineState& State;
		TArray<FBattleBattlerState> Battlers;
		TArray<FBattleActivePositionState> ActivePositions;
		FBattleFieldState Field;
		TArray<FBattleSideState> Sides;
		FBattleTriggerFramework TriggerFramework;
		FBattleAbilityItemRevealTracker AbilityItemRevealTracker;
		FBattleHeldItemLedger HeldItemLedger;
		TMap<FBattlerId, int32> DamageInputBuildCounts;
		TSet<FBattlerId> SubstituteProtectedTargets;
		TSet<FBattlerId> PendingDamagingHitConnections;
		TSet<FBattlerId> PendingImmediateItemUpdates;
		FBattleEffectExecutionResult* ExecutionResult = nullptr;
		bool bApplyingDirectMoveDamageHit = false;
		bool bMoveAffectedDifferentBattler = false;
		bool bRuntimeValid = true;
		uint64 NextConditionCreationOrdinal = 1;
		uint64 NextTriggerReentrancyToken = 1;
	};

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
						OutError = EBattleEffectExecutorError::InvalidHookResult;
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
				OutError = EBattleEffectExecutorError::InvalidHookResult;
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
				OutError = EBattleEffectExecutorError::InvalidHookResult;
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
					OutError = EBattleEffectExecutorError::InvalidHookResult;
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
				OutError = EBattleEffectExecutorError::InvalidHookResult;
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

bool FBattleEffectExecutor::TryExecuteAgainstState(
	const FBattleEffectExecutionRequest& Request,
	FBattleEngineState& State,
	FBattleEffectExecutionResult& OutResult,
	EBattleEffectExecutorError& OutError)
{
	OutResult = FBattleEffectExecutionResult();
	OutError = EBattleEffectExecutorError::None;
	if (!State.Random.IsValid())
	{
		OutError = EBattleEffectExecutorError::InvalidRequest;
		return false;
	}
	BattleEffectExecutorPrivate::FStateExecutionContext Context(Request, State);
	Context.BindExecutionResult(OutResult);
	if (!TryExecute(Request, Context, *State.Random, OutResult, OutError))
	{
		return false;
	}
	if (!Context.TryResolveForcedSwitches(OutResult, OutError))
	{
		OutResult = FBattleEffectExecutionResult();
		return false;
	}
	if (!Context.TryApplyPostMoveLifeOrbRecoil(OutResult, OutError))
	{
		OutResult = FBattleEffectExecutionResult();
		return false;
	}
	Context.Commit();
	return true;
}
