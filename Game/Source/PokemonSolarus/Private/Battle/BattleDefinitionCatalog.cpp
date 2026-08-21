#include "Battle/BattleDefinitionCatalog.h"

namespace
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
		| static_cast<uint32>(EBattleMoveFlags::TypelessDamage);

	constexpr uint32 KnownEffectFlags =
		static_cast<uint32>(EBattleMoveEffectFlags::BypassesSubstitute)
		| static_cast<uint32>(EBattleMoveEffectFlags::UsesActualDamage)
		| static_cast<uint32>(EBattleMoveEffectFlags::MinimumOne)
		| static_cast<uint32>(EBattleMoveEffectFlags::StopOnFaint)
		| static_cast<uint32>(EBattleMoveEffectFlags::PerHit);

	template <typename IdType>
	FDefinitionId GenericId(const IdType& Id)
	{
		return Id.IsValid() ? Id.GetDefinitionId() : FDefinitionId();
	}

	void AddDiagnostic(
		TArray<FBattleCatalogDiagnostic>& Diagnostics,
		const EBattleCatalogDiagnosticCode Code,
		const EBattleDefinitionFamily Family,
		const FDefinitionId& DefinitionId = FDefinitionId(),
		const FName Field = NAME_None,
		const int32 EntryIndex = INDEX_NONE)
	{
		Diagnostics.Add({Code, Family, DefinitionId, Field, EntryIndex});
	}

	bool IsKnownMoveCategory(const EBattleMoveCategory Value)
	{
		return Value == EBattleMoveCategory::Physical
			|| Value == EBattleMoveCategory::Special
			|| Value == EBattleMoveCategory::Status;
	}

	bool IsKnownItemKind(const EBattleItemKind Value)
	{
		return Value == EBattleItemKind::Held
			|| Value == EBattleItemKind::Battle
			|| Value == EBattleItemKind::Capture;
	}

	bool IsKnownConditionKind(const EBattleConditionKind Value)
	{
		return Value == EBattleConditionKind::MajorStatus
			|| Value == EBattleConditionKind::Volatile
			|| Value == EBattleConditionKind::Weather
			|| Value == EBattleConditionKind::Terrain
			|| Value == EBattleConditionKind::Hazard
			|| Value == EBattleConditionKind::Screen
			|| Value == EBattleConditionKind::Room
			|| Value == EBattleConditionKind::SideCondition;
	}

	bool IsKnownEffectKind(const EBattleMoveEffectKind Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleMoveEffectKind::RemoveCondition);
	}

	bool IsKnownEffectTarget(const EBattleEffectTarget Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleEffectTarget::Field);
	}

	bool IsKnownTargetClass(const EBattleTargetClass Value)
	{
		return Value == EBattleTargetClass::Self
			|| Value == EBattleTargetClass::SelectedAlly
			|| Value == EBattleTargetClass::SelectedOpponent
			|| Value == EBattleTargetClass::AnySelectedBattler
			|| Value == EBattleTargetClass::RandomLegalOpponent
			|| Value == EBattleTargetClass::UserSide
			|| Value == EBattleTargetClass::OpponentSide
			|| Value == EBattleTargetClass::BothSides
			|| Value == EBattleTargetClass::Field
			|| Value == EBattleTargetClass::FixedSpreadSet;
	}

	bool IsKnownBattleStat(const EBattleStat Value)
	{
		return static_cast<uint8>(Value) <= static_cast<uint8>(EBattleStat::Evasion);
	}

	bool IsKnownNatureStat(const ENatureStat Value)
	{
		return Value == ENatureStat::Attack
			|| Value == ENatureStat::Defense
			|| Value == ENatureStat::SpecialAttack
			|| Value == ENatureStat::SpecialDefense
			|| Value == ENatureStat::Speed;
	}

	bool IsValidNatureModifier(const FNatureStatModifier& Modifier)
	{
		if (Modifier.IsNeutral())
		{
			return true;
		}
		return IsKnownNatureStat(Modifier.GetBoostedStat())
			&& IsKnownNatureStat(Modifier.GetReducedStat())
			&& Modifier.GetBoostedStat() != Modifier.GetReducedStat();
	}

	bool HasPositiveBaseStats(const FPokemonStatValues& Stats)
	{
		return Stats.HP > 0
			&& Stats.Attack > 0
			&& Stats.Defense > 0
			&& Stats.SpecialAttack > 0
			&& Stats.SpecialDefense > 0
			&& Stats.Speed > 0;
	}

	template <typename DefinitionType>
	void SortDefinitions(TArray<DefinitionType>& Definitions)
	{
		Definitions.Sort(
			[](const DefinitionType& Left, const DefinitionType& Right)
			{
				return Left.Id.LexicalLess(Right.Id);
			});
	}

	template <typename DefinitionType>
	void ValidateDefinitionIdentities(
		const TArray<DefinitionType>& Definitions,
		const EBattleDefinitionFamily Family,
		TArray<FBattleCatalogDiagnostic>& Diagnostics)
	{
		for (int32 Index = 0; Index < Definitions.Num(); ++Index)
		{
			if (!Definitions[Index].Id.IsValid())
			{
				AddDiagnostic(
					Diagnostics,
					EBattleCatalogDiagnosticCode::InvalidIdentity,
					Family,
					FDefinitionId(),
					FName(TEXT("Id")),
					Index);
			}
			if (Index > 0 && Definitions[Index].Id == Definitions[Index - 1].Id)
			{
				AddDiagnostic(
					Diagnostics,
					EBattleCatalogDiagnosticCode::DuplicateIdentity,
					Family,
					GenericId(Definitions[Index].Id),
					FName(TEXT("Id")));
			}
		}
	}

	template <typename DefinitionType, typename IdType>
	const DefinitionType* FindDefinition(const TArray<DefinitionType>& Definitions, const IdType Id)
	{
		return Definitions.FindByPredicate(
			[Id](const DefinitionType& Definition)
			{
				return Definition.Id == Id;
			});
	}

	const FBattleConditionDefinition* FindConditionDefinition(
		const TArray<FBattleConditionDefinition>& Conditions,
		const FConditionId Id)
	{
		return FindDefinition(Conditions, Id);
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

	bool ConditionKindMatchesEffect(
		const EBattleMoveEffectKind EffectKind,
		const EBattleConditionKind ConditionKind)
	{
		switch (EffectKind)
		{
		case EBattleMoveEffectKind::ApplyCondition:
			return ConditionKind == EBattleConditionKind::MajorStatus
				|| ConditionKind == EBattleConditionKind::Volatile;
		case EBattleMoveEffectKind::SetFieldCondition:
			return ConditionKind == EBattleConditionKind::Weather
				|| ConditionKind == EBattleConditionKind::Terrain
				|| ConditionKind == EBattleConditionKind::Room;
		case EBattleMoveEffectKind::SetSideCondition:
			return ConditionKind == EBattleConditionKind::Hazard
				|| ConditionKind == EBattleConditionKind::Screen
				|| ConditionKind == EBattleConditionKind::SideCondition;
		case EBattleMoveEffectKind::Charge:
		case EBattleMoveEffectKind::Recharge:
		case EBattleMoveEffectKind::Protect:
		case EBattleMoveEffectKind::SemiInvulnerability:
			return ConditionKind == EBattleConditionKind::Volatile;
		case EBattleMoveEffectKind::RemoveCondition:
			return IsKnownConditionKind(ConditionKind);
		default:
			return false;
		}
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
			return Target == EBattleEffectTarget::UserSide
				|| Target == EBattleEffectTarget::TargetSide
				|| Target == EBattleEffectTarget::BothSides;
		default:
			return false;
		}
	}

	bool IsDamageTarget(const EBattleEffectTarget Target)
	{
		return Target == EBattleEffectTarget::ResolvedTarget
			|| Target == EBattleEffectTarget::AllResolvedTargets;
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

	void ValidateEffect(
		const FBattleMoveDefinition& Move,
		const FBattleMoveEffectDescriptor& Effect,
		const int32 EffectIndex,
		const TArray<FBattleConditionDefinition>& Conditions,
		const TArray<FBattleItemDefinition>& Items,
		TArray<FBattleCatalogDiagnostic>& Diagnostics)
	{
		const FDefinitionId MoveId = GenericId(Move.Id);
		auto AddEffectDiagnostic = [&](const EBattleCatalogDiagnosticCode Code, const TCHAR* Field)
		{
			AddDiagnostic(
				Diagnostics,
				Code,
				EBattleDefinitionFamily::Move,
				MoveId,
				FName(Field),
				EffectIndex);
		};

		if (Effect.Order < 0)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidEffectOrder, TEXT("Effects.Order"));
		}
		if (!IsKnownEffectKind(Effect.Kind))
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("Effects.Kind"));
			return;
		}
		if (!IsKnownEffectTarget(Effect.Target))
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("Effects.Target"));
		}
		const bool bPrimaryChance = Effect.ChanceNumerator == 1
			&& Effect.ChanceDenominator == 1;
		const bool bIndependentPercentageChance = Effect.ChanceNumerator >= 1
			&& Effect.ChanceNumerator <= 100
			&& Effect.ChanceDenominator == 100;
		if (!bPrimaryChance && !bIndependentPercentageChance)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Effects.Chance"));
		}
		if (Effect.MagnitudeDenominator <= 0
			|| Effect.DurationTurns < 0 || Effect.DurationTurns > 255
			|| Effect.MinimumCount < 0 || Effect.MinimumCount > 255
			|| Effect.MaximumCount < 0 || Effect.MaximumCount > 255
			|| Effect.LayerCount < 0 || Effect.LayerCount > 3)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Effects.Payload"));
		}
		if ((static_cast<uint32>(Effect.Flags) & ~KnownEffectFlags) != 0)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("Effects.Flags"));
		}

		if (Effect.Kind == EBattleMoveEffectKind::Damage
			&& (!IsDamageTarget(Effect.Target)
				|| !IsBattlerTargetClass(Move.TargetClass)
				|| !bPrimaryChance))
		{
			AddEffectDiagnostic(
				EBattleCatalogDiagnosticCode::IncompatibleEffect,
				bPrimaryChance ? TEXT("Effects.Target") : TEXT("Effects.Chance"));
		}
		if ((Effect.Kind == EBattleMoveEffectKind::ApplyCondition
				|| Effect.Kind == EBattleMoveEffectKind::ModifyStatStage
				|| Effect.Kind == EBattleMoveEffectKind::Heal
				|| Effect.Kind == EBattleMoveEffectKind::Switch
				|| Effect.Kind == EBattleMoveEffectKind::ChangeItem)
			&& !IsBattlerEffectTargetCompatible(Move.TargetClass, Effect.Target))
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
		}
		if ((Effect.Kind == EBattleMoveEffectKind::Drain
				|| Effect.Kind == EBattleMoveEffectKind::Recoil
				|| Effect.Kind == EBattleMoveEffectKind::Charge
				|| Effect.Kind == EBattleMoveEffectKind::Recharge
				|| Effect.Kind == EBattleMoveEffectKind::Protect
				|| Effect.Kind == EBattleMoveEffectKind::SemiInvulnerability)
			&& Effect.Target != EBattleEffectTarget::User)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
		}
		if (Effect.Kind == EBattleMoveEffectKind::SetFieldCondition
			&& (Effect.Target != EBattleEffectTarget::Field || Move.TargetClass != EBattleTargetClass::Field))
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
		}
		if (Effect.Kind == EBattleMoveEffectKind::SetSideCondition
			&& Effect.Target != EBattleEffectTarget::UserSide
			&& Effect.Target != EBattleEffectTarget::TargetSide
			&& Effect.Target != EBattleEffectTarget::BothSides)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
		}
		if ((Effect.Kind == EBattleMoveEffectKind::SetSideCondition
				|| Effect.Kind == EBattleMoveEffectKind::RemoveCondition)
			&& Effect.Target == EBattleEffectTarget::TargetSide
			&& Move.TargetClass == EBattleTargetClass::Field)
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
		}

		if (RequiresConditionReference(Effect.Kind))
		{
			const FBattleConditionDefinition* Condition = Effect.ConditionId.IsValid()
				? FindConditionDefinition(Conditions, Effect.ConditionId)
				: nullptr;
			if (Condition == nullptr)
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::MissingReference, TEXT("Effects.ConditionId"));
			}
			else if (!ConditionKindMatchesEffect(Effect.Kind, Condition->Kind))
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.ConditionId"));
			}
			else if (Effect.Kind == EBattleMoveEffectKind::RemoveCondition
				&& !IsRemovalTargetCompatible(Effect.Target, Condition->Kind))
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
			}
			else if (Effect.Kind == EBattleMoveEffectKind::RemoveCondition
				&& (Condition->Kind == EBattleConditionKind::MajorStatus
					|| Condition->Kind == EBattleConditionKind::Volatile)
				&& !IsBattlerEffectTargetCompatible(Move.TargetClass, Effect.Target))
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Target"));
			}
		}
		else if (Effect.ConditionId.IsValid())
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.ConditionId"));
		}

		if (Effect.Kind == EBattleMoveEffectKind::ChangeItem)
		{
			if (!Effect.ItemId.IsValid() || FindDefinition(Items, Effect.ItemId) == nullptr)
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::MissingReference, TEXT("Effects.ItemId"));
			}
		}
		else if (Effect.ItemId.IsValid())
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.ItemId"));
		}

		if (Effect.Kind == EBattleMoveEffectKind::ModifyStatStage)
		{
			if (!IsKnownBattleStat(Effect.Stat)
				|| Effect.MagnitudeNumerator == 0
				|| Effect.MagnitudeDenominator != 1)
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Effects.StatMagnitude"));
			}
		}
		else if (IsKnownBattleStat(Effect.Stat))
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Stat"));
		}

		if (Effect.Kind == EBattleMoveEffectKind::Heal
			|| Effect.Kind == EBattleMoveEffectKind::Drain
			|| Effect.Kind == EBattleMoveEffectKind::Recoil)
		{
			if (Effect.MagnitudeNumerator <= 0)
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Effects.Magnitude"));
			}
		}
		if (Effect.Kind == EBattleMoveEffectKind::MultiHit)
		{
			const bool bFixedCount = Effect.MinimumCount == Effect.MaximumCount
				&& Effect.MinimumCount >= 2
				&& Effect.MinimumCount <= 5;
			const bool bApprovedRange = Effect.MinimumCount == 2
				&& Effect.MaximumCount == 5;
			if (!bFixedCount && !bApprovedRange)
			{
				AddEffectDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Effects.Count"));
			}
		}
		if (EnumHasAllFlags(Effect.Flags, EBattleMoveEffectFlags::PerHit)
			&& (Move.Category == EBattleMoveCategory::Status
				|| bPrimaryChance
				|| Effect.Kind == EBattleMoveEffectKind::Damage
				|| Effect.Kind == EBattleMoveEffectKind::MultiHit
				|| Effect.Kind == EBattleMoveEffectKind::Drain
				|| Effect.Kind == EBattleMoveEffectKind::Recoil))
		{
			AddEffectDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.Flags"));
		}
	}

	void ValidateMove(
		const FBattleMoveDefinition& Move,
		const TArray<FBattleConditionDefinition>& Conditions,
		const TArray<FBattleItemDefinition>& Items,
		TArray<FBattleCatalogDiagnostic>& Diagnostics)
	{
		const FDefinitionId MoveId = GenericId(Move.Id);
		auto AddMoveDiagnostic = [&](const EBattleCatalogDiagnosticCode Code, const TCHAR* Field)
		{
			AddDiagnostic(
				Diagnostics,
				Code,
				EBattleDefinitionFamily::Move,
				MoveId,
				FName(Field));
		};

		if (!FBattleTypeChart::IsKnownType(Move.Type))
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("Type"));
		}
		if (!IsKnownMoveCategory(Move.Category))
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("Category"));
		}
		if (!IsKnownTargetClass(Move.TargetClass))
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("TargetClass"));
		}
		if ((static_cast<uint32>(Move.Flags) & ~KnownMoveFlags) != 0)
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidEnum, TEXT("Flags"));
		}
		if (EnumHasAllFlags(Move.Flags, EBattleMoveFlags::AlwaysCritical)
			&& EnumHasAllFlags(Move.Flags, EBattleMoveFlags::NeverCritical))
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Flags"));
		}

		if (Move.Category == EBattleMoveCategory::Status)
		{
			if (Move.Power != 0)
			{
				AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Power"));
			}
		}
		else if (Move.Category == EBattleMoveCategory::Physical
			|| Move.Category == EBattleMoveCategory::Special)
		{
			if (Move.Power < 1 || Move.Power > 1000)
			{
				AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Power"));
			}
		}

		if ((Move.bAlwaysHits && Move.Accuracy != 0)
			|| (!Move.bAlwaysHits && (Move.Accuracy < 1 || Move.Accuracy > 100)))
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Accuracy"));
		}
		if ((Move.bUsesPP && (Move.BasePP < 1 || Move.BasePP > 64))
			|| (!Move.bUsesPP && (Move.BasePP != 0 || Move.bAllowsPPBoosts)))
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("PP"));
		}
		if (Move.Priority < -7 || Move.Priority > 5)
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Priority"));
		}
		if (Move.Effects.IsEmpty())
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::InvalidRange, TEXT("Effects"));
		}

		int32 DamageEffectCount = 0;
		int32 MultiHitEffectCount = 0;
		const FBattleMoveEffectDescriptor* DamageEffect = nullptr;
		const FBattleMoveEffectDescriptor* MultiHitEffect = nullptr;
		int32 MultiHitEffectIndex = INDEX_NONE;
		for (int32 EffectIndex = 0; EffectIndex < Move.Effects.Num(); ++EffectIndex)
		{
			const FBattleMoveEffectDescriptor& Effect = Move.Effects[EffectIndex];
			if (EffectIndex > 0 && Effect.Order <= Move.Effects[EffectIndex - 1].Order)
			{
				AddDiagnostic(
					Diagnostics,
					EBattleCatalogDiagnosticCode::InvalidEffectOrder,
					EBattleDefinitionFamily::Move,
					MoveId,
					FName(TEXT("Effects.Order")),
					EffectIndex);
			}
			if (Effect.Kind == EBattleMoveEffectKind::Damage)
			{
				++DamageEffectCount;
				DamageEffect = &Effect;
			}
			else if (Effect.Kind == EBattleMoveEffectKind::MultiHit)
			{
				++MultiHitEffectCount;
				MultiHitEffect = &Effect;
				MultiHitEffectIndex = EffectIndex;
			}
			ValidateEffect(Move, Effect, EffectIndex, Conditions, Items, Diagnostics);
		}

		if (Move.Category == EBattleMoveCategory::Status && DamageEffectCount != 0)
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Category"));
		}
		if ((Move.Category == EBattleMoveCategory::Physical
			|| Move.Category == EBattleMoveCategory::Special)
			&& DamageEffectCount != 1)
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Category"));
		}
		if (MultiHitEffectCount > 1)
		{
			AddMoveDiagnostic(EBattleCatalogDiagnosticCode::IncompatibleEffect, TEXT("Effects.MultiHit"));
		}
		if (MultiHitEffect != nullptr
			&& (DamageEffect == nullptr
				|| MultiHitEffect->ChanceNumerator != 1
				|| MultiHitEffect->ChanceDenominator != 1
				|| MultiHitEffect->Order >= DamageEffect->Order
				|| MultiHitEffect->Target != DamageEffect->Target
				|| Move.TargetClass == EBattleTargetClass::FixedSpreadSet))
		{
			AddDiagnostic(
				Diagnostics,
				EBattleCatalogDiagnosticCode::IncompatibleEffect,
				EBattleDefinitionFamily::Move,
				MoveId,
				FName(TEXT("Effects.MultiHit")),
				MultiHitEffectIndex);
		}
	}
}

bool FBattleCatalogDiagnostic::Less(
	const FBattleCatalogDiagnostic& Left,
	const FBattleCatalogDiagnostic& Right)
{
	if (Left.Family != Right.Family)
	{
		return static_cast<uint8>(Left.Family) < static_cast<uint8>(Right.Family);
	}
	if (Left.DefinitionId != Right.DefinitionId)
	{
		if (!Left.DefinitionId.IsValid())
		{
			return true;
		}
		if (!Right.DefinitionId.IsValid())
		{
			return false;
		}
		return Left.DefinitionId.LexicalLess(Right.DefinitionId);
	}
	if (Left.Code != Right.Code)
	{
		return static_cast<uint8>(Left.Code) < static_cast<uint8>(Right.Code);
	}
	if (Left.Field != Right.Field)
	{
		return Left.Field.LexicalLess(Right.Field);
	}
	return Left.EntryIndex < Right.EntryIndex;
}

bool FBattleDefinitionCatalog::TryCreate(
	const FBattleDefinitionCatalogInput& Input,
	FBattleDefinitionCatalog& OutCatalog,
	TArray<FBattleCatalogDiagnostic>& OutDiagnostics)
{
	OutCatalog = FBattleDefinitionCatalog();
	OutDiagnostics.Reset();

	FBattleDefinitionCatalogInput Canonical = Input;
	SortDefinitions(Canonical.SpeciesForms);
	SortDefinitions(Canonical.Natures);
	SortDefinitions(Canonical.Moves);
	SortDefinitions(Canonical.Abilities);
	SortDefinitions(Canonical.Items);
	SortDefinitions(Canonical.Conditions);

	ValidateDefinitionIdentities(
		Canonical.SpeciesForms,
		EBattleDefinitionFamily::SpeciesForm,
		OutDiagnostics);
	ValidateDefinitionIdentities(Canonical.Natures, EBattleDefinitionFamily::Nature, OutDiagnostics);
	ValidateDefinitionIdentities(Canonical.Moves, EBattleDefinitionFamily::Move, OutDiagnostics);
	ValidateDefinitionIdentities(Canonical.Abilities, EBattleDefinitionFamily::Ability, OutDiagnostics);
	ValidateDefinitionIdentities(Canonical.Items, EBattleDefinitionFamily::Item, OutDiagnostics);
	ValidateDefinitionIdentities(Canonical.Conditions, EBattleDefinitionFamily::Condition, OutDiagnostics);

	FBattleTypeChart ValidatedTypeChart;
	EBattleTypeChartValidationError TypeChartError = EBattleTypeChartValidationError::None;
	if (!FBattleTypeChart::TryCreate(Canonical.TypeChartEntries, ValidatedTypeChart, TypeChartError))
	{
		EBattleCatalogDiagnosticCode Code = EBattleCatalogDiagnosticCode::InvalidTypeChartEntry;
		if (TypeChartError == EBattleTypeChartValidationError::DuplicateEntry)
		{
			Code = EBattleCatalogDiagnosticCode::DuplicateTypeChartEntry;
		}
		else if (TypeChartError == EBattleTypeChartValidationError::IncompleteChart)
		{
			Code = EBattleCatalogDiagnosticCode::IncompleteTypeChart;
		}
		AddDiagnostic(
			OutDiagnostics,
			Code,
			EBattleDefinitionFamily::TypeChart,
			FDefinitionId(),
			FName(TEXT("Entries")));
	}

	for (const FBattleItemDefinition& Item : Canonical.Items)
	{
		if (!IsKnownItemKind(Item.Kind))
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::InvalidEnum,
				EBattleDefinitionFamily::Item,
				GenericId(Item.Id),
				FName(TEXT("Kind")));
		}
	}

	for (const FBattleConditionDefinition& Condition : Canonical.Conditions)
	{
		if (!IsKnownConditionKind(Condition.Kind))
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::InvalidEnum,
				EBattleDefinitionFamily::Condition,
				GenericId(Condition.Id),
				FName(TEXT("Kind")));
		}
	}

	for (const FBattleNatureDefinition& Nature : Canonical.Natures)
	{
		if (!IsValidNatureModifier(Nature.Modifier))
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
				EBattleDefinitionFamily::Nature,
				GenericId(Nature.Id),
				FName(TEXT("Modifier")));
		}
	}

	for (const FBattleSpeciesFormDefinition& Species : Canonical.SpeciesForms)
	{
		const FDefinitionId SpeciesId = GenericId(Species.Id);
		if (!FBattleTypeChart::IsKnownType(Species.PrimaryType)
			|| (Species.SecondaryType != EPokemonType::Invalid
				&& !FBattleTypeChart::IsKnownType(Species.SecondaryType))
			|| Species.PrimaryType == Species.SecondaryType)
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::InvalidEnum,
				EBattleDefinitionFamily::SpeciesForm,
				SpeciesId,
				FName(TEXT("Types")));
		}
		if (!HasPositiveBaseStats(Species.BaseStats))
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::InvalidRange,
				EBattleDefinitionFamily::SpeciesForm,
				SpeciesId,
				FName(TEXT("BaseStats")));
		}
		if (Species.CatchRate < 1 || Species.CatchRate > 255)
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::InvalidRange,
				EBattleDefinitionFamily::SpeciesForm,
				SpeciesId,
				FName(TEXT("CatchRate")));
		}
		if (Species.AbilityChoices.IsEmpty())
		{
			AddDiagnostic(
				OutDiagnostics,
				EBattleCatalogDiagnosticCode::MissingReference,
				EBattleDefinitionFamily::SpeciesForm,
				SpeciesId,
				FName(TEXT("AbilityChoices")));
		}
		for (int32 AbilityIndex = 0; AbilityIndex < Species.AbilityChoices.Num(); ++AbilityIndex)
		{
			const FAbilityId AbilityId = Species.AbilityChoices[AbilityIndex];
			if (!AbilityId.IsValid() || FindDefinition(Canonical.Abilities, AbilityId) == nullptr)
			{
				AddDiagnostic(
					OutDiagnostics,
					EBattleCatalogDiagnosticCode::MissingReference,
					EBattleDefinitionFamily::SpeciesForm,
					SpeciesId,
					FName(TEXT("AbilityChoices")),
					AbilityIndex);
			}
			for (int32 PreviousIndex = 0; PreviousIndex < AbilityIndex; ++PreviousIndex)
			{
				if (Species.AbilityChoices[PreviousIndex] == AbilityId)
				{
					AddDiagnostic(
						OutDiagnostics,
						EBattleCatalogDiagnosticCode::DuplicateIdentity,
						EBattleDefinitionFamily::SpeciesForm,
						SpeciesId,
						FName(TEXT("AbilityChoices")),
						AbilityIndex);
					break;
				}
			}
		}
	}

	for (const FBattleMoveDefinition& Move : Canonical.Moves)
	{
		ValidateMove(Move, Canonical.Conditions, Canonical.Items, OutDiagnostics);
	}

	OutDiagnostics.Sort(FBattleCatalogDiagnostic::Less);
	if (!OutDiagnostics.IsEmpty())
	{
		return false;
	}

	OutCatalog.bValid = true;
	OutCatalog.TypeChart = MoveTemp(ValidatedTypeChart);
	OutCatalog.SpeciesForms = MoveTemp(Canonical.SpeciesForms);
	OutCatalog.Natures = MoveTemp(Canonical.Natures);
	OutCatalog.Moves = MoveTemp(Canonical.Moves);
	OutCatalog.Abilities = MoveTemp(Canonical.Abilities);
	OutCatalog.Items = MoveTemp(Canonical.Items);
	OutCatalog.Conditions = MoveTemp(Canonical.Conditions);
	return true;
}

const FBattleSpeciesFormDefinition* FBattleDefinitionCatalog::FindSpeciesForm(
	const FSpeciesFormId Id) const
{
	return FindDefinition(SpeciesForms, Id);
}

const FBattleNatureDefinition* FBattleDefinitionCatalog::FindNature(const FNatureId Id) const
{
	return FindDefinition(Natures, Id);
}

const FBattleMoveDefinition* FBattleDefinitionCatalog::FindMove(const FMoveId Id) const
{
	return FindDefinition(Moves, Id);
}

const FBattleAbilityDefinition* FBattleDefinitionCatalog::FindAbility(const FAbilityId Id) const
{
	return FindDefinition(Abilities, Id);
}

const FBattleItemDefinition* FBattleDefinitionCatalog::FindItem(const FItemId Id) const
{
	return FindDefinition(Items, Id);
}

const FBattleConditionDefinition* FBattleDefinitionCatalog::FindCondition(const FConditionId Id) const
{
	return FindDefinition(Conditions, Id);
}
