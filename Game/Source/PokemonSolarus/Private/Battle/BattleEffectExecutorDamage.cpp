#include "BattleEffectExecutorContext.h"

#include "BattleAllyActionPowerModifier.h"
#include "Battle/BattleAbility.h"
#include "BattleEntryHazardPrevention.h"
#include "Battle/BattleFieldSideConditions.h"
#include "Battle/BattleItem.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleMoveHitRules.h"
#include "Battle/BattleState.h"
#include "Battle/BattleVolatile.h"
#include "Math/NumericLimits.h"

namespace BattleEffectExecutorPrivate
{
	FDefinitionId MakeRuleId(const TCHAR* Name);

	bool FStateExecutionContext::TryResolveMoveUserHitQualifiers(
		const FBattleMoveDefinition& Move,
		FBattleMoveUserHitQualifiers& OutQualifiers) const
	{
		const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
		const FBattleSpeciesFormDefinition* UserSpecies = User != nullptr
			? State.Catalog.FindSpeciesForm(User->SpeciesFormId)
			: nullptr;
		return UserSpecies != nullptr
			&& FBattleMoveHitRules::TryResolveUserHitQualifiers(
				Move,
				UserSpecies->PrimaryType,
				UserSpecies->SecondaryType,
				OutQualifiers);
	}

	FBattleEffectHookResult FStateExecutionContext::CheckTryHit(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
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

	FBattleEffectHookResult FStateExecutionContext::CheckMoveImmunity(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target)
	{
		const bool bHasMoveImmunityRule = EnumHasAllFlags(
			Move.Flags,
			EBattleMoveFlags::RespectsTypeImmunity)
			|| EnumHasAllFlags(Move.Flags, EBattleMoveFlags::Powder);
		if (!bHasMoveImmunityRule)
		{
			return Applied();
		}
		if (Target.GetKind() != EBattleResolvedTargetKind::Battler)
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		const FBattleBattlerState* TargetBattler = FindBattler(
			Target.GetBattler().BattlerId);
		const FBattleSpeciesFormDefinition* TargetSpecies = TargetBattler != nullptr
			? State.Catalog.FindSpeciesForm(TargetBattler->SpeciesFormId)
			: nullptr;
		FBattleMoveHitImmunityResult Immunity;
		if (TargetSpecies == nullptr
			|| !FBattleMoveHitRules::TryResolveMoveImmunity(
				Move,
				TargetSpecies->PrimaryType,
				TargetSpecies->SecondaryType,
				State.Catalog.GetTypeChart(),
				Immunity))
		{
			return Outcome(EBattleEffectExecutionOutcome::Failed);
		}
		return Immunity.IsImmune()
			? Outcome(EBattleEffectExecutionOutcome::Immune)
			: Applied();
	}

	bool FStateExecutionContext::TryBuildAccuracyInput(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target,
		FBattleAccuracyCheckInput& OutInput)
	{
		OutInput = FBattleAccuracyCheckInput();
		const FBattleBattlerState* User = FindBattler(Request.UserBattlerId);
		if (User == nullptr)
		{
			return false;
		}
		OutInput.bAlwaysHits = Move.bAlwaysHits;
		OutInput.BaseAccuracy = Move.Accuracy;
		if (EnumHasAllFlags(
				Move.Flags,
				EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy))
		{
			FBattleMoveUserHitQualifiers Qualifiers;
			if (!TryResolveMoveUserHitQualifiers(Move, Qualifiers))
			{
				return false;
			}
			if (Qualifiers.bBypassAccuracy)
			{
				OutInput.bAlwaysHits = true;
				OutInput.BaseAccuracy = 0;
			}
		}
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

	bool FStateExecutionContext::TryBuildCriticalInput(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target,
		FBattleCriticalCheckInput& OutInput)
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

	bool FStateExecutionContext::TryBuildDamageInput(
		const FBattleMoveDefinition& Move,
		const FBattleResolvedTarget& Target,
		const bool bSpreadAcrossMultipleTargets,
		FBattleFinalDamageInput& OutInput)
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

		// Priority 10 ally-action modifiers precede priority 6 terrain modifiers.
		if (!FBattleAllyActionPowerModifier::AppendMatchingPowerModifiers(
				Request.TurnId,
				Request.ActionId,
				{Request.UserSlotId, Request.UserBattlerId},
				bActualDamageBuild,
				AllyActionPowerModifierRegistrations,
				OutInput.PowerModifiers))
		{
			return false;
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

	void FStateExecutionContext::SetDirectMoveDamageHit(const bool bActive)
	{
		bApplyingDirectMoveDamageHit = bActive;
	}

	bool FStateExecutionContext::TryGetHp(
		const FBattleResolvedTarget& Target,
		int32& OutCurrentHP,
		int32& OutMaximumHP) const
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

	FBattleEffectHookResult FStateExecutionContext::ApplyHpDelta(
		const FBattleResolvedTarget& Target,
		const int32 RequestedDelta)
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
}
