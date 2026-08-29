#include "Battle/BattleDataTableAdapter.h"

#include "Battle/BattleDataTableRows.h"
#include "Engine/DataTable.h"

namespace
{
	void AddAdapterDiagnostic(
		TArray<FBattleCatalogDiagnostic>& Diagnostics,
		const EBattleCatalogDiagnosticCode Code,
		const EBattleDefinitionFamily Family,
		const FDefinitionId& DefinitionId,
		const FName Field,
		const int32 EntryIndex = INDEX_NONE)
	{
		Diagnostics.Add({Code, Family, DefinitionId, Field, EntryIndex});
	}

	FDefinitionId DefinitionIdFromRowName(const FName RowName)
	{
		FDefinitionId DefinitionId;
		if (!FDefinitionId::TryCreate(RowName, DefinitionId))
		{
			return FDefinitionId();
		}
		return DefinitionId;
	}

	template <typename RowType>
	bool ValidateTable(
		const UDataTable* Table,
		const EBattleDefinitionFamily Family,
		const FName Field,
		TArray<FBattleCatalogDiagnostic>& Diagnostics)
	{
		if (Table == nullptr)
		{
			AddAdapterDiagnostic(
				Diagnostics,
				EBattleCatalogDiagnosticCode::MissingTable,
				Family,
				FDefinitionId(),
				Field);
			return false;
		}
		if (Table->GetRowStruct() != RowType::StaticStruct())
		{
			AddAdapterDiagnostic(
				Diagnostics,
				EBattleCatalogDiagnosticCode::WrongRowType,
				Family,
				FDefinitionId(),
				Field);
			return false;
		}
		return true;
	}

	template <typename RowType, typename PredicateType>
	void VisitRowsInStableOrder(
		const UDataTable& Table,
		const EBattleDefinitionFamily Family,
		TArray<FBattleCatalogDiagnostic>& Diagnostics,
		PredicateType Predicate)
	{
		TArray<FName> RowNames = Table.GetRowNames();
		RowNames.Sort(
			[](const FName Left, const FName Right)
			{
				return Left.LexicalLess(Right);
			});

		for (const FName RowName : RowNames)
		{
			const RowType* Row = Table.FindRow<RowType>(
				RowName,
				TEXT("FBattleDataTableAdapter::BuildCatalog"),
				false);
			if (Row == nullptr)
			{
				AddAdapterDiagnostic(
					Diagnostics,
					EBattleCatalogDiagnosticCode::MissingRow,
					Family,
					DefinitionIdFromRowName(RowName),
					FName(TEXT("Row")));
				continue;
			}
			Predicate(RowName, *Row);
		}
	}

	bool TryParsePokemonType(const FName Name, EPokemonType& OutType)
	{
		OutType = EPokemonType::Invalid;
		static const struct
		{
			FName Name;
			EPokemonType Type;
		} Mappings[] =
		{
			{FName(TEXT("Normal")), EPokemonType::Normal},
			{FName(TEXT("Fire")), EPokemonType::Fire},
			{FName(TEXT("Water")), EPokemonType::Water},
			{FName(TEXT("Electric")), EPokemonType::Electric},
			{FName(TEXT("Grass")), EPokemonType::Grass},
			{FName(TEXT("Ice")), EPokemonType::Ice},
			{FName(TEXT("Fighting")), EPokemonType::Fighting},
			{FName(TEXT("Poison")), EPokemonType::Poison},
			{FName(TEXT("Ground")), EPokemonType::Ground},
			{FName(TEXT("Flying")), EPokemonType::Flying},
			{FName(TEXT("Psychic")), EPokemonType::Psychic},
			{FName(TEXT("Bug")), EPokemonType::Bug},
			{FName(TEXT("Rock")), EPokemonType::Rock},
			{FName(TEXT("Ghost")), EPokemonType::Ghost},
			{FName(TEXT("Dragon")), EPokemonType::Dragon},
			{FName(TEXT("Dark")), EPokemonType::Dark},
			{FName(TEXT("Steel")), EPokemonType::Steel},
			{FName(TEXT("Fairy")), EPokemonType::Fairy}
		};
		for (const auto& Mapping : Mappings)
		{
			if (Name == Mapping.Name)
			{
				OutType = Mapping.Type;
				return true;
			}
		}
		return false;
	}

	bool TryParseNatureStat(const FName Name, ENatureStat& OutStat)
	{
		OutStat = ENatureStat::None;
		if (Name.IsNone() || Name == FName(TEXT("None")))
		{
			return true;
		}
		if (Name == FName(TEXT("Attack")))
		{
			OutStat = ENatureStat::Attack;
			return true;
		}
		if (Name == FName(TEXT("Defense")))
		{
			OutStat = ENatureStat::Defense;
			return true;
		}
		if (Name == FName(TEXT("SpecialAttack")))
		{
			OutStat = ENatureStat::SpecialAttack;
			return true;
		}
		if (Name == FName(TEXT("SpecialDefense")))
		{
			OutStat = ENatureStat::SpecialDefense;
			return true;
		}
		if (Name == FName(TEXT("Speed")))
		{
			OutStat = ENatureStat::Speed;
			return true;
		}
		return false;
	}

	bool TryParseBattleStat(const FName Name, EBattleStat& OutStat)
	{
		OutStat = static_cast<EBattleStat>(255);
		if (Name == FName(TEXT("Attack")))
		{
			OutStat = EBattleStat::Attack;
			return true;
		}
		if (Name == FName(TEXT("Defense")))
		{
			OutStat = EBattleStat::Defense;
			return true;
		}
		if (Name == FName(TEXT("SpecialAttack")))
		{
			OutStat = EBattleStat::SpecialAttack;
			return true;
		}
		if (Name == FName(TEXT("SpecialDefense")))
		{
			OutStat = EBattleStat::SpecialDefense;
			return true;
		}
		if (Name == FName(TEXT("Speed")))
		{
			OutStat = EBattleStat::Speed;
			return true;
		}
		if (Name == FName(TEXT("Accuracy")))
		{
			OutStat = EBattleStat::Accuracy;
			return true;
		}
		if (Name == FName(TEXT("Evasion")))
		{
			OutStat = EBattleStat::Evasion;
			return true;
		}
		return false;
	}

	bool TryParseMoveCategory(const FName Name, EBattleMoveCategory& OutCategory)
	{
		OutCategory = EBattleMoveCategory::Invalid;
		if (Name == FName(TEXT("Physical")))
		{
			OutCategory = EBattleMoveCategory::Physical;
			return true;
		}
		if (Name == FName(TEXT("Special")))
		{
			OutCategory = EBattleMoveCategory::Special;
			return true;
		}
		if (Name == FName(TEXT("Status")))
		{
			OutCategory = EBattleMoveCategory::Status;
			return true;
		}
		return false;
	}

	bool TryParseItemKind(const FName Name, EBattleItemKind& OutKind)
	{
		OutKind = EBattleItemKind::Invalid;
		if (Name == FName(TEXT("Held")))
		{
			OutKind = EBattleItemKind::Held;
			return true;
		}
		if (Name == FName(TEXT("Battle")))
		{
			OutKind = EBattleItemKind::Battle;
			return true;
		}
		if (Name == FName(TEXT("Capture")))
		{
			OutKind = EBattleItemKind::Capture;
			return true;
		}
		return false;
	}

	bool TryParseConditionKind(const FName Name, EBattleConditionKind& OutKind)
	{
		OutKind = EBattleConditionKind::Invalid;
		static const struct
		{
			FName Name;
			EBattleConditionKind Kind;
		} Mappings[] =
		{
			{FName(TEXT("MajorStatus")), EBattleConditionKind::MajorStatus},
			{FName(TEXT("Volatile")), EBattleConditionKind::Volatile},
			{FName(TEXT("Weather")), EBattleConditionKind::Weather},
			{FName(TEXT("Terrain")), EBattleConditionKind::Terrain},
			{FName(TEXT("Hazard")), EBattleConditionKind::Hazard},
			{FName(TEXT("Screen")), EBattleConditionKind::Screen},
			{FName(TEXT("Room")), EBattleConditionKind::Room},
			{FName(TEXT("SideCondition")), EBattleConditionKind::SideCondition}
		};
		for (const auto& Mapping : Mappings)
		{
			if (Name == Mapping.Name)
			{
				OutKind = Mapping.Kind;
				return true;
			}
		}
		return false;
	}

	bool TryParseEffectKind(const FName Name, EBattleMoveEffectKind& OutKind)
	{
		OutKind = EBattleMoveEffectKind::Invalid;
		static const struct
		{
			FName Name;
			EBattleMoveEffectKind Kind;
		} Mappings[] =
		{
			{FName(TEXT("Damage")), EBattleMoveEffectKind::Damage},
			{FName(TEXT("ApplyCondition")), EBattleMoveEffectKind::ApplyCondition},
			{FName(TEXT("ModifyStatStage")), EBattleMoveEffectKind::ModifyStatStage},
			{FName(TEXT("Heal")), EBattleMoveEffectKind::Heal},
			{FName(TEXT("Drain")), EBattleMoveEffectKind::Drain},
			{FName(TEXT("Recoil")), EBattleMoveEffectKind::Recoil},
			{FName(TEXT("MultiHit")), EBattleMoveEffectKind::MultiHit},
			{FName(TEXT("SetFieldCondition")), EBattleMoveEffectKind::SetFieldCondition},
			{FName(TEXT("SetSideCondition")), EBattleMoveEffectKind::SetSideCondition},
			{FName(TEXT("Switch")), EBattleMoveEffectKind::Switch},
			{FName(TEXT("ChangeItem")), EBattleMoveEffectKind::ChangeItem},
			{FName(TEXT("Charge")), EBattleMoveEffectKind::Charge},
			{FName(TEXT("Recharge")), EBattleMoveEffectKind::Recharge},
			{FName(TEXT("Protect")), EBattleMoveEffectKind::Protect},
			{FName(TEXT("SemiInvulnerability")), EBattleMoveEffectKind::SemiInvulnerability},
			{FName(TEXT("RemoveCondition")), EBattleMoveEffectKind::RemoveCondition},
			{FName(TEXT("RegisterTargetRedirection")),
				EBattleMoveEffectKind::RegisterTargetRedirection},
			{FName(TEXT("RegisterAllyActionPowerModifier")),
				EBattleMoveEffectKind::RegisterAllyActionPowerModifier}
		};
		for (const auto& Mapping : Mappings)
		{
			if (Name == Mapping.Name)
			{
				OutKind = Mapping.Kind;
				return true;
			}
		}
		return false;
	}

	bool TryParseEffectTarget(const FName Name, EBattleEffectTarget& OutTarget)
	{
		OutTarget = EBattleEffectTarget::Invalid;
		static const struct
		{
			FName Name;
			EBattleEffectTarget Target;
		} Mappings[] =
		{
			{FName(TEXT("User")), EBattleEffectTarget::User},
			{FName(TEXT("ResolvedTarget")), EBattleEffectTarget::ResolvedTarget},
			{FName(TEXT("AllResolvedTargets")), EBattleEffectTarget::AllResolvedTargets},
			{FName(TEXT("UserSide")), EBattleEffectTarget::UserSide},
			{FName(TEXT("TargetSide")), EBattleEffectTarget::TargetSide},
			{FName(TEXT("BothSides")), EBattleEffectTarget::BothSides},
			{FName(TEXT("Field")), EBattleEffectTarget::Field}
		};
		for (const auto& Mapping : Mappings)
		{
			if (Name == Mapping.Name)
			{
				OutTarget = Mapping.Target;
				return true;
			}
		}
		return false;
	}

	bool TryParseTargetClass(const FName Name, EBattleTargetClass& OutTargetClass)
	{
		OutTargetClass = static_cast<EBattleTargetClass>(255);
		static const struct
		{
			FName Name;
			EBattleTargetClass TargetClass;
		} Mappings[] =
		{
			{FName(TEXT("Self")), EBattleTargetClass::Self},
			{FName(TEXT("SelectedAlly")), EBattleTargetClass::SelectedAlly},
			{FName(TEXT("SelectedOpponent")), EBattleTargetClass::SelectedOpponent},
			{FName(TEXT("AnySelectedBattler")), EBattleTargetClass::AnySelectedBattler},
			{FName(TEXT("RandomLegalOpponent")), EBattleTargetClass::RandomLegalOpponent},
			{FName(TEXT("UserSide")), EBattleTargetClass::UserSide},
			{FName(TEXT("OpponentSide")), EBattleTargetClass::OpponentSide},
			{FName(TEXT("BothSides")), EBattleTargetClass::BothSides},
			{FName(TEXT("Field")), EBattleTargetClass::Field},
			{FName(TEXT("FixedSpreadSet")), EBattleTargetClass::FixedSpreadSet},
			{FName(TEXT("SelectedOtherBattler")), EBattleTargetClass::SelectedOtherBattler},
			{FName(TEXT("FixedOpponentSpreadSet")), EBattleTargetClass::FixedOpponentSpreadSet}
		};
		for (const auto& Mapping : Mappings)
		{
			if (Name == Mapping.Name)
			{
				OutTargetClass = Mapping.TargetClass;
				return true;
			}
		}
		return false;
	}

	bool TryParseMoveFlags(const TArray<FName>& Names, EBattleMoveFlags& OutFlags)
	{
		OutFlags = EBattleMoveFlags::None;
		for (const FName Name : Names)
		{
			EBattleMoveFlags Flag = EBattleMoveFlags::None;
			if (Name == FName(TEXT("MakesContact"))) Flag = EBattleMoveFlags::MakesContact;
			else if (Name == FName(TEXT("BlockedByProtect"))) Flag = EBattleMoveFlags::BlockedByProtect;
			else if (Name == FName(TEXT("BypassesProtect"))) Flag = EBattleMoveFlags::BypassesProtect;
			else if (Name == FName(TEXT("BypassesSubstitute"))) Flag = EBattleMoveFlags::BypassesSubstitute;
			else if (Name == FName(TEXT("ThawsUser"))) Flag = EBattleMoveFlags::ThawsUser;
			else if (Name == FName(TEXT("ThawsTarget"))) Flag = EBattleMoveFlags::ThawsTarget;
			else if (Name == FName(TEXT("Unencoreable"))) Flag = EBattleMoveFlags::Unencoreable;
			else if (Name == FName(TEXT("AlwaysCritical"))) Flag = EBattleMoveFlags::AlwaysCritical;
			else if (Name == FName(TEXT("NeverCritical"))) Flag = EBattleMoveFlags::NeverCritical;
			else if (Name == FName(TEXT("UsesPerHitAccuracy"))) Flag = EBattleMoveFlags::UsesPerHitAccuracy;
			else if (Name == FName(TEXT("ReachesAirborneSemiInvulnerableTarget"))) Flag = EBattleMoveFlags::ReachesAirborneSemiInvulnerableTarget;
			else if (Name == FName(TEXT("DoublesPowerAgainstAirborneSemiInvulnerableTarget"))) Flag = EBattleMoveFlags::DoublesPowerAgainstAirborneSemiInvulnerableTarget;
			else if (Name == FName(TEXT("BreaksProtection"))) Flag = EBattleMoveFlags::BreaksProtection;
			else if (Name == FName(TEXT("BypassesSideProtection"))) Flag = EBattleMoveFlags::BypassesSideProtection;
			else if (Name == FName(TEXT("ReducedByGrassyTerrain"))) Flag = EBattleMoveFlags::ReducedByGrassyTerrain;
			else if (Name == FName(TEXT("RespectsTypeImmunity"))) Flag = EBattleMoveFlags::RespectsTypeImmunity;
			else if (Name == FName(TEXT("Powder"))) Flag = EBattleMoveFlags::Powder;
			else if (Name == FName(TEXT("PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy")))
			{
				Flag = EBattleMoveFlags::PoisonTypeUserBypassesSemiInvulnerabilityAndAccuracy;
			}
			else return false;

			if (EnumHasAnyFlags(OutFlags, Flag))
			{
				return false;
			}
			OutFlags |= Flag;
		}
		return true;
	}

	bool TryParseEffectFlags(const TArray<FName>& Names, EBattleMoveEffectFlags& OutFlags)
	{
		OutFlags = EBattleMoveEffectFlags::None;
		for (const FName Name : Names)
		{
			EBattleMoveEffectFlags Flag = EBattleMoveEffectFlags::None;
			if (Name == FName(TEXT("BypassesSubstitute"))) Flag = EBattleMoveEffectFlags::BypassesSubstitute;
			else if (Name == FName(TEXT("UsesActualDamage"))) Flag = EBattleMoveEffectFlags::UsesActualDamage;
			else if (Name == FName(TEXT("MinimumOne"))) Flag = EBattleMoveEffectFlags::MinimumOne;
			else if (Name == FName(TEXT("StopOnFaint"))) Flag = EBattleMoveEffectFlags::StopOnFaint;
			else if (Name == FName(TEXT("PerHit"))) Flag = EBattleMoveEffectFlags::PerHit;
			else return false;

			if (EnumHasAnyFlags(OutFlags, Flag))
			{
				return false;
			}
			OutFlags |= Flag;
		}
		return true;
	}

	template <typename IdType>
	bool TryCreateTypedId(
		const FName Name,
		IdType& OutId,
		const EBattleDefinitionFamily Family,
		const FDefinitionId& OwnerId,
		const FName Field,
		TArray<FBattleCatalogDiagnostic>& Diagnostics,
		const int32 EntryIndex = INDEX_NONE)
	{
		if (IdType::TryCreate(Name, OutId))
		{
			return true;
		}
		AddAdapterDiagnostic(
			Diagnostics,
			EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
			Family,
			OwnerId,
			Field,
			EntryIndex);
		return false;
	}
}

bool FBattleDataTableAdapter::BuildCatalog(
	const FBattleDataTableSet& Tables,
	FBattleDefinitionCatalog& OutCatalog,
	TArray<FBattleCatalogDiagnostic>& OutDiagnostics)
{
	OutCatalog = FBattleDefinitionCatalog();
	OutDiagnostics.Reset();

	const bool bSpeciesValid = ValidateTable<FBattleSpeciesFormTableRow>(
		Tables.SpeciesForms,
		EBattleDefinitionFamily::SpeciesForm,
		FName(TEXT("SpeciesForms")),
		OutDiagnostics);
	const bool bNaturesValid = ValidateTable<FBattleNatureTableRow>(
		Tables.Natures,
		EBattleDefinitionFamily::Nature,
		FName(TEXT("Natures")),
		OutDiagnostics);
	const bool bMovesValid = ValidateTable<FBattleMoveTableRow>(
		Tables.Moves,
		EBattleDefinitionFamily::Move,
		FName(TEXT("Moves")),
		OutDiagnostics);
	const bool bAbilitiesValid = ValidateTable<FBattleAbilityTableRow>(
		Tables.Abilities,
		EBattleDefinitionFamily::Ability,
		FName(TEXT("Abilities")),
		OutDiagnostics);
	const bool bItemsValid = ValidateTable<FBattleItemTableRow>(
		Tables.Items,
		EBattleDefinitionFamily::Item,
		FName(TEXT("Items")),
		OutDiagnostics);
	const bool bConditionsValid = ValidateTable<FBattleConditionTableRow>(
		Tables.Conditions,
		EBattleDefinitionFamily::Condition,
		FName(TEXT("Conditions")),
		OutDiagnostics);
	const bool bTypeChartValid = ValidateTable<FBattleTypeChartTableRow>(
		Tables.TypeChart,
		EBattleDefinitionFamily::TypeChart,
		FName(TEXT("TypeChart")),
		OutDiagnostics);

	if (!bSpeciesValid || !bNaturesValid || !bMovesValid || !bAbilitiesValid
		|| !bItemsValid || !bConditionsValid || !bTypeChartValid)
	{
		OutDiagnostics.Sort(FBattleCatalogDiagnostic::Less);
		return false;
	}

	FBattleDefinitionCatalogInput Input;

	VisitRowsInStableOrder<FBattleAbilityTableRow>(
		*Tables.Abilities,
		EBattleDefinitionFamily::Ability,
		OutDiagnostics,
		[&](const FName RowName, const FBattleAbilityTableRow&)
		{
			FBattleAbilityDefinition Definition;
			TryCreateTypedId(
				RowName,
				Definition.Id,
				EBattleDefinitionFamily::Ability,
				DefinitionIdFromRowName(RowName),
				FName(TEXT("Id")),
				OutDiagnostics);
			Input.Abilities.Add(MoveTemp(Definition));
		});

	VisitRowsInStableOrder<FBattleItemTableRow>(
		*Tables.Items,
		EBattleDefinitionFamily::Item,
		OutDiagnostics,
		[&](const FName RowName, const FBattleItemTableRow& Row)
		{
			const FDefinitionId OwnerId = DefinitionIdFromRowName(RowName);
			FBattleItemDefinition Definition;
			TryCreateTypedId(
				RowName,
				Definition.Id,
				EBattleDefinitionFamily::Item,
				OwnerId,
				FName(TEXT("Id")),
				OutDiagnostics);
			if (!TryParseItemKind(Row.Kind, Definition.Kind))
			{
				AddAdapterDiagnostic(
					OutDiagnostics,
					EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Item,
					OwnerId,
					FName(TEXT("Kind")));
			}
			Input.Items.Add(MoveTemp(Definition));
		});

	VisitRowsInStableOrder<FBattleConditionTableRow>(
		*Tables.Conditions,
		EBattleDefinitionFamily::Condition,
		OutDiagnostics,
		[&](const FName RowName, const FBattleConditionTableRow& Row)
		{
			const FDefinitionId OwnerId = DefinitionIdFromRowName(RowName);
			FBattleConditionDefinition Definition;
			TryCreateTypedId(
				RowName,
				Definition.Id,
				EBattleDefinitionFamily::Condition,
				OwnerId,
				FName(TEXT("Id")),
				OutDiagnostics);
			if (!TryParseConditionKind(Row.Kind, Definition.Kind))
			{
				AddAdapterDiagnostic(
					OutDiagnostics,
					EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Condition,
					OwnerId,
					FName(TEXT("Kind")));
			}
			Input.Conditions.Add(MoveTemp(Definition));
		});

	VisitRowsInStableOrder<FBattleSpeciesFormTableRow>(
		*Tables.SpeciesForms,
		EBattleDefinitionFamily::SpeciesForm,
		OutDiagnostics,
		[&](const FName RowName, const FBattleSpeciesFormTableRow& Row)
		{
			const FDefinitionId OwnerId = DefinitionIdFromRowName(RowName);
			FBattleSpeciesFormDefinition Definition;
			TryCreateTypedId(
				RowName,
				Definition.Id,
				EBattleDefinitionFamily::SpeciesForm,
				OwnerId,
				FName(TEXT("Id")),
				OutDiagnostics);
			if (!TryParsePokemonType(Row.PrimaryType, Definition.PrimaryType))
			{
				AddAdapterDiagnostic(
					OutDiagnostics,
					EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::SpeciesForm,
					OwnerId,
					FName(TEXT("PrimaryType")));
			}
			if (!Row.SecondaryType.IsNone()
				&& !TryParsePokemonType(Row.SecondaryType, Definition.SecondaryType))
			{
				AddAdapterDiagnostic(
					OutDiagnostics,
					EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::SpeciesForm,
					OwnerId,
					FName(TEXT("SecondaryType")));
			}
			Definition.BaseStats =
			{
				Row.BaseHP,
				Row.BaseAttack,
				Row.BaseDefense,
				Row.BaseSpecialAttack,
				Row.BaseSpecialDefense,
				Row.BaseSpeed
			};
			Definition.CatchRate = Row.CatchRate;
			for (int32 AbilityIndex = 0; AbilityIndex < Row.AbilityIds.Num(); ++AbilityIndex)
			{
				FAbilityId AbilityId;
				TryCreateTypedId(
					Row.AbilityIds[AbilityIndex],
					AbilityId,
					EBattleDefinitionFamily::SpeciesForm,
					OwnerId,
					FName(TEXT("AbilityIds")),
					OutDiagnostics,
					AbilityIndex);
				Definition.AbilityChoices.Add(AbilityId);
			}
			Input.SpeciesForms.Add(MoveTemp(Definition));
		});

	VisitRowsInStableOrder<FBattleNatureTableRow>(
		*Tables.Natures,
		EBattleDefinitionFamily::Nature,
		OutDiagnostics,
		[&](const FName RowName, const FBattleNatureTableRow& Row)
		{
			const FDefinitionId OwnerId = DefinitionIdFromRowName(RowName);
			FBattleNatureDefinition Definition;
			TryCreateTypedId(
				RowName,
				Definition.Id,
				EBattleDefinitionFamily::Nature,
				OwnerId,
				FName(TEXT("Id")),
				OutDiagnostics);

			ENatureStat BoostedStat = ENatureStat::None;
			ENatureStat ReducedStat = ENatureStat::None;
			const bool bBoostedValid = TryParseNatureStat(Row.BoostedStat, BoostedStat);
			const bool bReducedValid = TryParseNatureStat(Row.ReducedStat, ReducedStat);
			if (!bBoostedValid || !bReducedValid
				|| !FNatureStatModifier::TryCreate(BoostedStat, ReducedStat, Definition.Modifier))
			{
				AddAdapterDiagnostic(
					OutDiagnostics,
					EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Nature,
					OwnerId,
					FName(TEXT("Modifier")));
			}
			Input.Natures.Add(MoveTemp(Definition));
		});

	VisitRowsInStableOrder<FBattleMoveTableRow>(
		*Tables.Moves,
		EBattleDefinitionFamily::Move,
		OutDiagnostics,
		[&](const FName RowName, const FBattleMoveTableRow& Row)
		{
			const FDefinitionId OwnerId = DefinitionIdFromRowName(RowName);
			FBattleMoveDefinition Definition;
			TryCreateTypedId(
				RowName,
				Definition.Id,
				EBattleDefinitionFamily::Move,
				OwnerId,
				FName(TEXT("Id")),
				OutDiagnostics);
			if (!TryParsePokemonType(Row.Type, Definition.Type))
			{
				AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Type")));
			}
			if (!TryParseMoveCategory(Row.Category, Definition.Category))
			{
				AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Category")));
			}
			if (!TryParseTargetClass(Row.TargetClass, Definition.TargetClass))
			{
				AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("TargetClass")));
			}
			if (!TryParseMoveFlags(Row.Flags, Definition.Flags))
			{
				AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Flags")));
			}

			Definition.Power = Row.Power;
			Definition.bAlwaysHits = Row.bAlwaysHits;
			Definition.Accuracy = Row.Accuracy;
			Definition.bUsesPP = Row.bUsesPP;
			Definition.BasePP = Row.BasePP;
			Definition.bAllowsPPBoosts = Row.bAllowsPPBoosts;
			Definition.Priority = Row.Priority;

			for (int32 EffectIndex = 0; EffectIndex < Row.Effects.Num(); ++EffectIndex)
			{
				const FBattleMoveEffectTableRow& SourceEffect = Row.Effects[EffectIndex];
				FBattleMoveEffectDescriptor Effect;
				Effect.Order = SourceEffect.Order;
				Effect.ChanceNumerator = SourceEffect.ChanceNumerator;
				Effect.ChanceDenominator = SourceEffect.ChanceDenominator;
				Effect.MagnitudeNumerator = SourceEffect.MagnitudeNumerator;
				Effect.MagnitudeDenominator = SourceEffect.MagnitudeDenominator;
				Effect.MinimumCount = SourceEffect.MinimumCount;
				Effect.MaximumCount = SourceEffect.MaximumCount;
				Effect.DurationTurns = SourceEffect.DurationTurns;
				Effect.LayerCount = SourceEffect.LayerCount;

				if (!TryParseEffectKind(SourceEffect.Kind, Effect.Kind))
				{
					AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
						EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Effects.Kind")), EffectIndex);
				}
				if (!TryParseEffectTarget(SourceEffect.Target, Effect.Target))
				{
					AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
						EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Effects.Target")), EffectIndex);
				}
				if (!SourceEffect.ConditionId.IsNone())
				{
					TryCreateTypedId(SourceEffect.ConditionId, Effect.ConditionId,
						EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Effects.ConditionId")),
						OutDiagnostics, EffectIndex);
				}
				if (!SourceEffect.ItemId.IsNone())
				{
					TryCreateTypedId(SourceEffect.ItemId, Effect.ItemId,
						EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Effects.ItemId")),
						OutDiagnostics, EffectIndex);
				}
				if (!SourceEffect.Stat.IsNone() && !TryParseBattleStat(SourceEffect.Stat, Effect.Stat))
				{
					AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
						EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Effects.Stat")), EffectIndex);
				}
				if (!TryParseEffectFlags(SourceEffect.Flags, Effect.Flags))
				{
					AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
						EBattleDefinitionFamily::Move, OwnerId, FName(TEXT("Effects.Flags")), EffectIndex);
				}
				Definition.Effects.Add(MoveTemp(Effect));
			}
			Input.Moves.Add(MoveTemp(Definition));
		});

	VisitRowsInStableOrder<FBattleTypeChartTableRow>(
		*Tables.TypeChart,
		EBattleDefinitionFamily::TypeChart,
		OutDiagnostics,
		[&](const FName RowName, const FBattleTypeChartTableRow& Row)
		{
			EPokemonType AttackingType = EPokemonType::Invalid;
			const FDefinitionId OwnerId = DefinitionIdFromRowName(RowName);
			if (!TryParsePokemonType(RowName, AttackingType))
			{
				AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
					EBattleDefinitionFamily::TypeChart, OwnerId, FName(TEXT("AttackingType")));
			}
			for (int32 EntryIndex = 0; EntryIndex < Row.Entries.Num(); ++EntryIndex)
			{
				const FBattleTypeChartCellTableRow& Cell = Row.Entries[EntryIndex];
				FBattleTypeChartEntry Entry;
				Entry.AttackingType = AttackingType;
				Entry.Numerator = Cell.Numerator;
				Entry.Denominator = Cell.Denominator;
				if (!TryParsePokemonType(Cell.DefendingType, Entry.DefendingType))
				{
					AddAdapterDiagnostic(OutDiagnostics, EBattleCatalogDiagnosticCode::InvalidAuthoredValue,
						EBattleDefinitionFamily::TypeChart, OwnerId, FName(TEXT("DefendingType")), EntryIndex);
				}
				Input.TypeChartEntries.Add(Entry);
			}
		});

	OutDiagnostics.Sort(FBattleCatalogDiagnostic::Less);
	if (!OutDiagnostics.IsEmpty())
	{
		return false;
	}

	return FBattleDefinitionCatalog::TryCreate(Input, OutCatalog, OutDiagnostics);
}
