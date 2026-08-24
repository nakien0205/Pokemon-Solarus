#include "Battle/BattleDataTableRuntimeSource.h"

#include "Battle/BattleDataTableAdapter.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleRuntimeDataTableRows.h"
#include "Battle/BattleStatCalculator.h"
#include "Engine/DataTable.h"

namespace
{
	const FName InitialBattleRowName(TEXT("InitialBattle"));

	class FDataTableBattleDisplayNameResolver final : public IBattleDisplayNameResolver
	{
	public:
		explicit FDataTableBattleDisplayNameResolver(TMap<FSpeciesFormId, FText>&& InSpeciesNames)
			: SpeciesNames(MoveTemp(InSpeciesNames))
		{
		}

		virtual bool TryResolveSpeciesName(
			const FSpeciesFormId SpeciesFormId,
			FText& OutDisplayName) const override
		{
			OutDisplayName = FText::GetEmpty();
			const FText* DisplayName = SpeciesNames.Find(SpeciesFormId);
			if (DisplayName == nullptr || DisplayName->IsEmpty())
			{
				return false;
			}

			OutDisplayName = *DisplayName;
			return true;
		}

	private:
		TMap<FSpeciesFormId, FText> SpeciesNames;
	};

	bool Fail(const FString& Error, FString& OutError)
	{
		OutError = Error;
		return false;
	}

	bool TryParseDecimalUint64(
		const FString& Source,
		const FString& Label,
		uint64& OutValue,
		FString& OutError)
	{
		OutValue = 0;
		if (Source.IsEmpty())
		{
			return Fail(FString::Printf(TEXT("%s is empty."), *Label), OutError);
		}

		uint64 Value = 0;
		for (const TCHAR Character : Source)
		{
			if (Character < TEXT('0') || Character > TEXT('9'))
			{
				return Fail(
					FString::Printf(TEXT("%s must be an unsigned decimal string."), *Label),
					OutError);
			}

			const uint64 Digit = static_cast<uint64>(Character - TEXT('0'));
			if (Value > (MAX_uint64 - Digit) / 10ULL)
			{
				return Fail(FString::Printf(TEXT("%s exceeds uint64."), *Label), OutError);
			}
			Value = (Value * 10ULL) + Digit;
		}

		OutValue = Value;
		return true;
	}

	template <typename IdType>
	bool TryParseNumericId(
		const FString& Source,
		const FString& Label,
		IdType& OutId,
		FString& OutError)
	{
		OutId = IdType();
		uint64 Value = 0;
		if (!TryParseDecimalUint64(Source, Label, Value, OutError))
		{
			return false;
		}
		if (!IdType::TryCreate(Value, OutId))
		{
			return Fail(FString::Printf(TEXT("%s must be greater than zero."), *Label), OutError);
		}
		return true;
	}

	template <typename IdType>
	bool TryParseDefinitionId(
		const FName Source,
		const FString& Label,
		IdType& OutId,
		FString& OutError)
	{
		OutId = IdType();
		if (!IdType::TryCreate(Source, OutId))
		{
			return Fail(FString::Printf(TEXT("%s is empty."), *Label), OutError);
		}
		return true;
	}

	bool TryConvertPositiveSchemaVersion(
		const int32 Source,
		const FString& Label,
		uint32& OutValue,
		FString& OutError)
	{
		OutValue = 0;
		if (Source <= 0)
		{
			return Fail(FString::Printf(TEXT("%s must be greater than zero."), *Label), OutError);
		}
		OutValue = static_cast<uint32>(Source);
		return true;
	}

	bool TryConvertUint8(
		const int32 Source,
		const FString& Label,
		uint8& OutValue,
		FString& OutError)
	{
		OutValue = 0;
		if (Source < 0 || Source > MAX_uint8)
		{
			return Fail(FString::Printf(TEXT("%s must fit uint8."), *Label), OutError);
		}
		OutValue = static_cast<uint8>(Source);
		return true;
	}

	bool TryConvertUint32(
		const int64 Source,
		const FString& Label,
		uint32& OutValue,
		FString& OutError)
	{
		OutValue = 0;
		if (Source < 0 || static_cast<uint64>(Source) > MAX_uint32)
		{
			return Fail(FString::Printf(TEXT("%s must fit uint32."), *Label), OutError);
		}
		OutValue = static_cast<uint32>(Source);
		return true;
	}

	bool TryParseSide(const FName Source, EBattleSide& OutValue)
	{
		if (Source == FName(TEXT("Player")))
		{
			OutValue = EBattleSide::Player;
			return true;
		}
		if (Source == FName(TEXT("Opponent")))
		{
			OutValue = EBattleSide::Opponent;
			return true;
		}
		return false;
	}

	bool TryParsePosition(const FName Source, EBattlePosition& OutValue)
	{
		if (Source == FName(TEXT("Left")))
		{
			OutValue = EBattlePosition::Left;
			return true;
		}
		if (Source == FName(TEXT("Right")))
		{
			OutValue = EBattlePosition::Right;
			return true;
		}
		return false;
	}

	bool TryParseTrainerRole(const FName Source, EBattleTrainerRole& OutValue)
	{
		if (Source == FName(TEXT("Player")))
		{
			OutValue = EBattleTrainerRole::Player;
			return true;
		}
		if (Source == FName(TEXT("Partner")))
		{
			OutValue = EBattleTrainerRole::Partner;
			return true;
		}
		if (Source == FName(TEXT("Opponent")))
		{
			OutValue = EBattleTrainerRole::Opponent;
			return true;
		}
		return false;
	}

	bool TryParseController(const FName Source, EBattleDecisionController& OutValue)
	{
		if (Source == FName(TEXT("Human")))
		{
			OutValue = EBattleDecisionController::Human;
			return true;
		}
		if (Source == FName(TEXT("PartnerAI")))
		{
			OutValue = EBattleDecisionController::PartnerAI;
			return true;
		}
		if (Source == FName(TEXT("EnemyAI")))
		{
			OutValue = EBattleDecisionController::EnemyAI;
			return true;
		}
		if (Source == FName(TEXT("Scripted")))
		{
			OutValue = EBattleDecisionController::Scripted;
			return true;
		}
		return false;
	}

	bool TryParseEncounterKind(const FName Source, EBattleEncounterKind& OutValue)
	{
		if (Source == FName(TEXT("Wild")))
		{
			OutValue = EBattleEncounterKind::Wild;
			return true;
		}
		if (Source == FName(TEXT("Trainer")))
		{
			OutValue = EBattleEncounterKind::Trainer;
			return true;
		}
		if (Source == FName(TEXT("Rival")))
		{
			OutValue = EBattleEncounterKind::Rival;
			return true;
		}
		if (Source == FName(TEXT("BossGym")))
		{
			OutValue = EBattleEncounterKind::BossGym;
			return true;
		}
		if (Source == FName(TEXT("TutorialScripted")))
		{
			OutValue = EBattleEncounterKind::TutorialScripted;
			return true;
		}
		return false;
	}

	bool TryParseFormat(const FName Source, EBattleFormat& OutValue)
	{
		if (Source == FName(TEXT("Single")))
		{
			OutValue = EBattleFormat::Single;
			return true;
		}
		if (Source == FName(TEXT("Double")))
		{
			OutValue = EBattleFormat::Double;
			return true;
		}
		if (Source == FName(TEXT("PartnerDouble")))
		{
			OutValue = EBattleFormat::PartnerDouble;
			return true;
		}
		return false;
	}

	bool TryParseWildFleeMode(const FName Source, EBattleWildFleeMode& OutValue)
	{
		if (Source == FName(TEXT("Disabled")))
		{
			OutValue = EBattleWildFleeMode::Disabled;
			return true;
		}
		if (Source == FName(TEXT("Never")))
		{
			OutValue = EBattleWildFleeMode::Never;
			return true;
		}
		if (Source == FName(TEXT("Always")))
		{
			OutValue = EBattleWildFleeMode::Always;
			return true;
		}
		if (Source == FName(TEXT("Chance")))
		{
			OutValue = EBattleWildFleeMode::Chance;
			return true;
		}
		return false;
	}

	bool TryLoadTable(
		const TSoftObjectPtr<UDataTable>& Reference,
		const FString& Label,
		UDataTable*& OutTable,
		FString& OutError)
	{
		OutTable = nullptr;
		if (Reference.IsNull())
		{
			return Fail(FString::Printf(TEXT("%s reference is empty."), *Label), OutError);
		}

		OutTable = Reference.LoadSynchronous();
		if (OutTable == nullptr)
		{
			return Fail(FString::Printf(TEXT("%s could not be loaded."), *Label), OutError);
		}
		return true;
	}

	template <typename RowType>
	bool TryLoadTypedTable(
		const TSoftObjectPtr<UDataTable>& Reference,
		const FString& Label,
		UDataTable*& OutTable,
		FString& OutError)
	{
		if (!TryLoadTable(Reference, Label, OutTable, OutError))
		{
			return false;
		}
		if (OutTable->GetRowStruct() != RowType::StaticStruct())
		{
			OutTable = nullptr;
			return Fail(FString::Printf(TEXT("%s has the wrong row type."), *Label), OutError);
		}
		return true;
	}

	bool TryLoadScenario(
		const TSoftObjectPtr<UDataTable>& RuntimeTableReference,
		FBattleRuntimeScenarioTableRow& OutScenario,
		FString& OutError)
	{
		OutScenario = FBattleRuntimeScenarioTableRow();
		UDataTable* RuntimeTable = nullptr;
		if (!TryLoadTypedTable<FBattleRuntimeScenarioTableRow>(
			RuntimeTableReference,
			TEXT("Initial Battle runtime table"),
			RuntimeTable,
			OutError))
		{
			return false;
		}

		const FBattleRuntimeScenarioTableRow* Row =
			RuntimeTable->FindRow<FBattleRuntimeScenarioTableRow>(
				InitialBattleRowName,
				TEXT("Initial Battle runtime source"),
				false);
		if (Row == nullptr)
		{
			return Fail(TEXT("Initial Battle runtime row is missing."), OutError);
		}

		OutScenario = *Row;
		return true;
	}

	bool TryLoadCatalogTable(
		const TSoftObjectPtr<UDataTable>& Reference,
		const FString& Label,
		const UDataTable*& OutTable,
		FString& OutError)
	{
		OutTable = nullptr;
		UDataTable* LoadedTable = nullptr;
		if (!TryLoadTable(Reference, Label, LoadedTable, OutError))
		{
			return false;
		}
		OutTable = LoadedTable;
		return true;
	}

	bool TryBuildCatalog(
		const FBattleRuntimeScenarioTableRow& Scenario,
		FBattleDefinitionCatalog& OutCatalog,
		FString& OutError)
	{
		FBattleDataTableSet Tables;
		if (!TryLoadCatalogTable(Scenario.SpeciesForms, TEXT("Species Forms table"), Tables.SpeciesForms, OutError)
			|| !TryLoadCatalogTable(Scenario.Natures, TEXT("Natures table"), Tables.Natures, OutError)
			|| !TryLoadCatalogTable(Scenario.Moves, TEXT("Moves table"), Tables.Moves, OutError)
			|| !TryLoadCatalogTable(Scenario.Abilities, TEXT("Abilities table"), Tables.Abilities, OutError)
			|| !TryLoadCatalogTable(Scenario.Items, TEXT("Items table"), Tables.Items, OutError)
			|| !TryLoadCatalogTable(Scenario.Conditions, TEXT("Conditions table"), Tables.Conditions, OutError)
			|| !TryLoadCatalogTable(Scenario.TypeChart, TEXT("Type Chart table"), Tables.TypeChart, OutError))
		{
			return false;
		}

		TArray<FBattleCatalogDiagnostic> Diagnostics;
		if (!FBattleDataTableAdapter::BuildCatalog(Tables, OutCatalog, Diagnostics))
		{
			return Fail(
				FString::Printf(
					TEXT("Initial Battle catalog validation produced %d diagnostic(s)."),
					Diagnostics.Num()),
				OutError);
		}
		return true;
	}

	bool TryCopyDisplayNameRow(
		const FName RowName,
		const UDataTable& DisplayTable,
		const FBattleDefinitionCatalog& Catalog,
		TMap<FSpeciesFormId, FText>& OutSpeciesNames,
		FString& OutError)
	{
		FSpeciesFormId SpeciesFormId;
		if (!TryParseDefinitionId(
			RowName,
			FString::Printf(TEXT("Display name row '%s' identity"), *RowName.ToString()),
			SpeciesFormId,
			OutError))
		{
			return false;
		}
		if (Catalog.FindSpeciesForm(SpeciesFormId) == nullptr)
		{
			return Fail(
				FString::Printf(TEXT("Display name row '%s' has no catalog species."), *RowName.ToString()),
				OutError);
		}

		const FBattleDisplayNameTableRow* Row = DisplayTable.FindRow<FBattleDisplayNameTableRow>(
			RowName,
			TEXT("Initial Battle display names"),
			false);
		if (Row == nullptr || Row->DisplayName.IsEmpty())
		{
			return Fail(
				FString::Printf(TEXT("Display name row '%s' is empty."), *RowName.ToString()),
				OutError);
		}
		OutSpeciesNames.Add(SpeciesFormId, Row->DisplayName);
		return true;
	}

	bool TryCopyDisplayNames(
		const UDataTable& DisplayTable,
		const FBattleDefinitionCatalog& Catalog,
		TMap<FSpeciesFormId, FText>& OutSpeciesNames,
		FString& OutError)
	{
		OutSpeciesNames.Reset();
		TArray<FName> RowNames = DisplayTable.GetRowNames();
		RowNames.Sort(FNameLexicalLess());
		for (const FName RowName : RowNames)
		{
			if (!TryCopyDisplayNameRow(RowName, DisplayTable, Catalog, OutSpeciesNames, OutError))
			{
				return false;
			}
		}

		for (const FBattleSpeciesFormDefinition& Species : Catalog.GetSpeciesForms())
		{
			if (!OutSpeciesNames.Contains(Species.Id))
			{
				return Fail(
					FString::Printf(
						TEXT("Species '%s' has no display name."),
						*Species.Id.GetDefinitionId().GetName().ToString()),
					OutError);
			}
		}
		return true;
	}

	bool TryBuildDisplayNameResolver(
		const FBattleRuntimeScenarioTableRow& Scenario,
		const FBattleDefinitionCatalog& Catalog,
		TSharedPtr<const IBattleDisplayNameResolver>& OutResolver,
		FString& OutError)
	{
		OutResolver.Reset();
		UDataTable* DisplayTable = nullptr;
		if (!TryLoadTypedTable<FBattleDisplayNameTableRow>(
			Scenario.DisplayNames,
			TEXT("Species display names table"),
			DisplayTable,
			OutError))
		{
			return false;
		}

		TMap<FSpeciesFormId, FText> SpeciesNames;
		if (!TryCopyDisplayNames(*DisplayTable, Catalog, SpeciesNames, OutError))
		{
			return false;
		}
		OutResolver = MakeShared<FDataTableBattleDisplayNameResolver>(MoveTemp(SpeciesNames));
		return true;
	}

	bool TryInitializeSnapshotReferences(
		const FBattleRuntimeScenarioTableRow& Scenario,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		return TryParseDefinitionId(
			Scenario.SettingsSnapshotId,
			TEXT("Settings snapshot identity"),
			OutInput.SettingsReference.SnapshotId,
			OutError)
			&& TryConvertPositiveSchemaVersion(
				Scenario.SettingsSchemaVersion,
				TEXT("Settings schema version"),
				OutInput.SettingsReference.SchemaVersion,
				OutError)
			&& TryParseDefinitionId(
				Scenario.CatalogSnapshotId,
				TEXT("Catalog snapshot identity"),
				OutInput.CatalogReference.SnapshotId,
				OutError)
			&& TryConvertPositiveSchemaVersion(
				Scenario.CatalogSchemaVersion,
				TEXT("Catalog schema version"),
				OutInput.CatalogReference.SchemaVersion,
				OutError);
	}

	bool TryInitializeSetupIdentity(
		const FBattleRuntimeScenarioTableRow& Scenario,
		FBattleSetupInput& OutInput,
		FTrainerId& OutLocalTrainerId,
		uint64& OutSeed,
		FString& OutError)
	{
		if (!TryParseDecimalUint64(Scenario.Seed, TEXT("Battle seed"), OutSeed, OutError)
			|| !TryParseNumericId(Scenario.BattleId, TEXT("Battle identity"), OutInput.BattleId, OutError)
			|| !TryParseNumericId(
				Scenario.LocalTrainerId,
				TEXT("Local Trainer identity"),
				OutLocalTrainerId,
				OutError)
			|| !TryInitializeSnapshotReferences(Scenario, OutInput, OutError))
		{
			return false;
		}
		if (!TryParseEncounterKind(Scenario.EncounterKind, OutInput.EncounterKind))
		{
			return Fail(TEXT("Encounter kind is invalid."), OutError);
		}
		if (!TryParseFormat(Scenario.Format, OutInput.Format))
		{
			return Fail(TEXT("Battle format is invalid."), OutError);
		}
		return true;
	}

	bool TryInitializeEncounterPolicies(
		const FBattleRuntimeScenarioTableRow& Scenario,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		OutInput.CaptureCapacity.PartySlotsRemaining = Scenario.PartySlotsRemaining;
		OutInput.CaptureCapacity.StorageSlotsRemaining = Scenario.StorageSlotsRemaining;
		OutInput.Policies.bRunAllowed = Scenario.Policies.bRunAllowed;
		OutInput.Policies.bCaptureAllowed = Scenario.Policies.bCaptureAllowed;
		OutInput.Policies.bBagAllowed = Scenario.Policies.bBagAllowed;
		OutInput.Policies.bShiftPromptEligible = Scenario.Policies.bShiftPromptEligible;
		if (!TryParseWildFleeMode(Scenario.Policies.WildFleeMode, OutInput.Policies.WildFleeMode)
			|| !TryConvertUint32(
				Scenario.Policies.WildFleeNumerator,
				TEXT("Wild flee numerator"),
				OutInput.Policies.WildFleeNumerator,
				OutError)
			|| !TryConvertUint32(
				Scenario.Policies.WildFleeDenominator,
				TEXT("Wild flee denominator"),
				OutInput.Policies.WildFleeDenominator,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = TEXT("Wild flee mode is invalid.");
			}
			return false;
		}
		return true;
	}

	bool TryInitializeSetupInput(
		const FBattleRuntimeScenarioTableRow& Scenario,
		FBattleSetupInput& OutInput,
		FTrainerId& OutLocalTrainerId,
		uint64& OutSeed,
		FString& OutError)
	{
		OutInput = FBattleSetupInput();
		OutLocalTrainerId = FTrainerId();
		OutSeed = 0;
		return TryInitializeSetupIdentity(
			Scenario,
			OutInput,
			OutLocalTrainerId,
			OutSeed,
			OutError)
			&& TryInitializeEncounterPolicies(Scenario, OutInput, OutError);
	}

	bool TryAppendTrainerBag(
		const TArray<FBattleRuntimeBagItemTableEntry>& Source,
		const FString& Prefix,
		FBattleTrainerSetup& OutTrainer,
		FString& OutError)
	{
		for (int32 BagIndex = 0; BagIndex < Source.Num(); ++BagIndex)
		{
			FBattleBagItemCount Item;
			if (!TryParseDefinitionId(
				Source[BagIndex].ItemId,
				FString::Printf(TEXT("%s Bag[%d] item"), *Prefix, BagIndex),
				Item.ItemId,
				OutError))
			{
				return false;
			}
			Item.Count = Source[BagIndex].Count;
			OutTrainer.Bag.Add(Item);
		}
		return true;
	}

	bool TryAppendTrainer(
		const FBattleRuntimeTrainerTableEntry& Source,
		const int32 TrainerIndex,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		const FString Prefix = FString::Printf(TEXT("Trainer[%d]"), TrainerIndex);
		FBattleTrainerSetup Trainer;
		if (!TryParseNumericId(Source.TrainerId, Prefix + TEXT(" identity"), Trainer.TrainerId, OutError)
			|| !TryParseDefinitionId(
				Source.SelectorProfileId,
				Prefix + TEXT(" selector profile"),
				Trainer.SelectorProfileId,
				OutError)
			|| !TryParseSide(Source.Side, Trainer.Side)
			|| !TryParseTrainerRole(Source.Role, Trainer.Role)
			|| !TryParseController(Source.Controller, Trainer.Controller))
		{
			if (OutError.IsEmpty())
			{
				OutError = Prefix + TEXT(" has an invalid enum value.");
			}
			return false;
		}
		if (!TryAppendTrainerBag(Source.Bag, Prefix, Trainer, OutError))
		{
			return false;
		}
		OutInput.Trainers.Add(MoveTemp(Trainer));
		return true;
	}

	bool TryAppendTrainers(
		const TArray<FBattleRuntimeTrainerTableEntry>& Sources,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			if (!TryAppendTrainer(Sources[Index], Index, OutInput, OutError))
			{
				return false;
			}
		}
		return true;
	}

	FPokemonStatValues ToStatValues(const FBattleRuntimeStatValuesTableEntry& Source)
	{
		return
		{
			Source.HP,
			Source.Attack,
			Source.Defense,
			Source.SpecialAttack,
			Source.SpecialDefense,
			Source.Speed
		};
	}

	bool TryResolveOptionalItem(
		const FName Source,
		const FString& Label,
		const FBattleDefinitionCatalog& Catalog,
		FItemId& OutItemId,
		FString& OutError)
	{
		OutItemId = FItemId();
		if (Source.IsNone())
		{
			return true;
		}
		if (!TryParseDefinitionId(Source, Label, OutItemId, OutError))
		{
			return false;
		}
		if (Catalog.FindItem(OutItemId) == nullptr)
		{
			return Fail(Label + TEXT(" is missing from the catalog."), OutError);
		}
		return true;
	}

	bool TryAppendMove(
		const FBattleRuntimeMoveTableEntry& Source,
		const FString& PartyPrefix,
		const int32 MoveIndex,
		const FBattleDefinitionCatalog& Catalog,
		FBattlePartyEntrySetup& OutPartyEntry,
		FString& OutError)
	{
		const FString Prefix = FString::Printf(TEXT("%s Move[%d]"), *PartyPrefix, MoveIndex);
		if (Source.PPUps != 0)
		{
			return Fail(Prefix + TEXT(" PP Ups must be zero."), OutError);
		}
		if (Source.SlotIndex < 0 || Source.SlotIndex > MAX_uint8)
		{
			return Fail(Prefix + TEXT(" slot must fit uint8."), OutError);
		}

		FBattleMoveSlotSetup Move;
		Move.SlotIndex = static_cast<uint8>(Source.SlotIndex);
		if (!TryParseDefinitionId(Source.MoveId, Prefix + TEXT(" identity"), Move.MoveId, OutError))
		{
			return false;
		}
		const FBattleMoveDefinition* Definition = Catalog.FindMove(Move.MoveId);
		if (Definition == nullptr)
		{
			return Fail(Prefix + TEXT(" is missing from the catalog."), OutError);
		}
		Move.MaxPP = Definition->BasePP;
		Move.CurrentPP = Definition->BasePP;
		OutPartyEntry.Moves.Add(Move);
		return true;
	}

	bool TryInitializePartyIdentity(
		const FBattleRuntimePartyEntryTableEntry& Source,
		const FString& Prefix,
		FBattlePartyEntrySetup& OutEntry,
		FString& OutError)
	{
		if (!TryParseNumericId(Source.TrainerId, Prefix + TEXT(" Trainer identity"), OutEntry.TrainerId, OutError)
			|| !TryParseNumericId(Source.BattlerId, Prefix + TEXT(" battler identity"), OutEntry.BattlerId, OutError)
			|| !TryParseNumericId(
				Source.SourcePokemonId,
				Prefix + TEXT(" source Pokemon identity"),
				OutEntry.SourcePokemonId,
				OutError)
			|| !FPartySlotId::TryCreate(Source.PartySlotIndex, OutEntry.PartySlotId)
			|| !TryParseDefinitionId(
				Source.SpeciesFormId,
				Prefix + TEXT(" species"),
				OutEntry.SpeciesFormId,
				OutError))
		{
			if (OutError.IsEmpty())
			{
				OutError = Prefix + TEXT(" party slot is invalid.");
			}
			return false;
		}
		return true;
	}

	bool TryResolvePartySpeciesAndNature(
		const FBattleRuntimePartyEntryTableEntry& Source,
		const FString& Prefix,
		const FBattleDefinitionCatalog& Catalog,
		const FBattlePartyEntrySetup& OutEntry,
		const FBattleSpeciesFormDefinition*& OutSpecies,
		const FBattleNatureDefinition*& OutNature,
		FString& OutError)
	{
		OutSpecies = Catalog.FindSpeciesForm(OutEntry.SpeciesFormId);
		OutNature = nullptr;
		if (OutSpecies == nullptr)
		{
			return Fail(Prefix + TEXT(" species is missing from the catalog."), OutError);
		}

		FNatureId NatureId;
		if (!TryParseDefinitionId(Source.NatureId, Prefix + TEXT(" nature"), NatureId, OutError))
		{
			return false;
		}
		OutNature = Catalog.FindNature(NatureId);
		if (OutNature == nullptr)
		{
			return Fail(Prefix + TEXT(" nature is missing from the catalog."), OutError);
		}
		return true;
	}

	bool TryResolvePartyAbilityAndItems(
		const FBattleRuntimePartyEntryTableEntry& Source,
		const FString& Prefix,
		const FBattleDefinitionCatalog& Catalog,
		const FBattleSpeciesFormDefinition& Species,
		FBattlePartyEntrySetup& OutEntry,
		FString& OutError)
	{
		if (!TryParseDefinitionId(Source.AbilityId, Prefix + TEXT(" Ability"), OutEntry.AbilityId, OutError)
			|| Catalog.FindAbility(OutEntry.AbilityId) == nullptr
			|| !Species.AbilityChoices.Contains(OutEntry.AbilityId))
		{
			if (OutError.IsEmpty())
			{
				OutError = Prefix + TEXT(" Ability is not allowed by the species catalog.");
			}
			return false;
		}

		if (!TryResolveOptionalItem(
			Source.OriginalHeldItemId,
			Prefix + TEXT(" original held item"),
			Catalog,
			OutEntry.OriginalHeldItemId,
			OutError)
			|| !TryResolveOptionalItem(
				Source.CurrentHeldItemId,
				Prefix + TEXT(" current held item"),
				Catalog,
				OutEntry.CurrentHeldItemId,
				OutError))
		{
			return false;
		}
		return true;
	}

	bool TryCalculatePartyStats(
		const FBattleRuntimePartyEntryTableEntry& Source,
		const FString& Prefix,
		const FBattleSpeciesFormDefinition& Species,
		const FBattleNatureDefinition& Nature,
		FBattlePartyEntrySetup& OutEntry,
		FString& OutError)
	{
		FPokemonStatInputs StatInputs;
		StatInputs.Level = Source.Level;
		StatInputs.BaseStats = Species.BaseStats;
		StatInputs.IndividualValues = ToStatValues(Source.IndividualValues);
		StatInputs.EffortValues = ToStatValues(Source.EffortValues);
		StatInputs.NatureModifier = Nature.Modifier;
		EBattleStatCalculationError StatError = EBattleStatCalculationError::None;
		if (!FBattleStatCalculator::TryCalculatePermanentStats(
			StatInputs,
			OutEntry.Stats,
			StatError))
		{
			return Fail(
				FString::Printf(TEXT("%s stat calculation failed with error %d."), *Prefix, static_cast<int32>(StatError)),
				OutError);
		}

		OutEntry.Level = Source.Level;
		OutEntry.CurrentHP = OutEntry.Stats.MaxHP;
		OutEntry.bEgg = Source.bEgg;
		return true;
	}

	bool TryAppendPartyMoves(
		const FBattleRuntimePartyEntryTableEntry& Source,
		const FString& Prefix,
		const FBattleDefinitionCatalog& Catalog,
		FBattlePartyEntrySetup& OutEntry,
		FString& OutError)
	{
		for (int32 MoveIndex = 0; MoveIndex < Source.Moves.Num(); ++MoveIndex)
		{
			if (!TryAppendMove(Source.Moves[MoveIndex], Prefix, MoveIndex, Catalog, OutEntry, OutError))
			{
				return false;
			}
		}
		return true;
	}

	bool TryBuildPartyEntry(
		const FBattleRuntimePartyEntryTableEntry& Source,
		const int32 PartyIndex,
		const FBattleDefinitionCatalog& Catalog,
		FBattlePartyEntrySetup& OutEntry,
		FString& OutError)
	{
		OutEntry = FBattlePartyEntrySetup();
		const FString Prefix = FString::Printf(TEXT("PartyEntry[%d]"), PartyIndex);
		const FBattleSpeciesFormDefinition* Species = nullptr;
		const FBattleNatureDefinition* Nature = nullptr;
		if (!TryInitializePartyIdentity(Source, Prefix, OutEntry, OutError)
			|| !TryResolvePartySpeciesAndNature(
				Source,
				Prefix,
				Catalog,
				OutEntry,
				Species,
				Nature,
				OutError))
		{
			return false;
		}
		return TryResolvePartyAbilityAndItems(Source, Prefix, Catalog, *Species, OutEntry, OutError)
			&& TryCalculatePartyStats(Source, Prefix, *Species, *Nature, OutEntry, OutError)
			&& TryAppendPartyMoves(Source, Prefix, Catalog, OutEntry, OutError);
	}

	bool TryAppendPartyEntries(
		const TArray<FBattleRuntimePartyEntryTableEntry>& Sources,
		const FBattleDefinitionCatalog& Catalog,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			FBattlePartyEntrySetup Entry;
			if (!TryBuildPartyEntry(Sources[Index], Index, Catalog, Entry, OutError))
			{
				return false;
			}
			OutInput.PartyEntries.Add(MoveTemp(Entry));
		}
		return true;
	}

	bool TryAppendStartingActive(
		const TArray<FBattleRuntimeActiveAssignmentTableEntry>& Sources,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			const FString Prefix = FString::Printf(TEXT("StartingActive[%d]"), Index);
			EBattleSide Side = EBattleSide::Player;
			EBattlePosition Position = EBattlePosition::Left;
			FBattleActiveAssignment Assignment;
			if (!TryParseSide(Sources[Index].Side, Side)
				|| !TryParsePosition(Sources[Index].Position, Position)
				|| !FActiveSlotId::TryCreate(Side, Position, Assignment.ActiveSlotId))
			{
				return Fail(Prefix + TEXT(" slot is invalid."), OutError);
			}
			if (!TryParseNumericId(
				Sources[Index].TrainerId,
				Prefix + TEXT(" Trainer identity"),
				Assignment.TrainerId,
				OutError)
				|| !TryParseNumericId(
					Sources[Index].BattlerId,
					Prefix + TEXT(" battler identity"),
					Assignment.BattlerId,
					OutError))
			{
				return false;
			}
			OutInput.StartingActive.Add(Assignment);
		}
		return true;
	}

	bool TryAppendObedienceInputs(
		const TArray<FBattleRuntimeObedienceTableEntry>& Sources,
		FBattleSetupInput& OutInput,
		FString& OutError)
	{
		for (int32 Index = 0; Index < Sources.Num(); ++Index)
		{
			const FString Prefix = FString::Printf(TEXT("ObedienceInput[%d]"), Index);
			FBattleObedienceInput Input;
			if (!TryParseNumericId(
				Sources[Index].BattlerId,
				Prefix + TEXT(" battler identity"),
				Input.BattlerId,
				OutError)
				|| !TryConvertUint8(
					Sources[Index].ReferenceLevel,
					Prefix + TEXT(" reference level"),
					Input.ReferenceLevel,
					OutError)
				|| !TryConvertUint8(
					Sources[Index].BadgeCount,
					Prefix + TEXT(" badge count"),
					Input.BadgeCount,
					OutError))
			{
				return false;
			}
			Input.bSubjectToPlayerCap = Sources[Index].bSubjectToPlayerCap;
			OutInput.ObedienceInputs.Add(Input);
		}
		return true;
	}

	bool TryBuildSetup(
		const FBattleRuntimeScenarioTableRow& Scenario,
		const FBattleDefinitionCatalog& Catalog,
		FBattleSetup& OutSetup,
		FTrainerId& OutLocalTrainerId,
		uint64& OutSeed,
		FString& OutError)
	{
		OutSetup = FBattleSetup();
		FBattleSetupInput Input;
		if (!TryInitializeSetupInput(Scenario, Input, OutLocalTrainerId, OutSeed, OutError)
			|| !TryAppendTrainers(Scenario.Trainers, Input, OutError)
			|| !TryAppendPartyEntries(Scenario.PartyEntries, Catalog, Input, OutError)
			|| !TryAppendStartingActive(Scenario.StartingActive, Input, OutError)
			|| !TryAppendObedienceInputs(Scenario.ObedienceInputs, Input, OutError))
		{
			return false;
		}

		EBattleSetupValidationError SetupError = EBattleSetupValidationError::None;
		if (!FBattleSetup::TryCreate(Input, OutSetup, SetupError))
		{
			return Fail(
				FString::Printf(TEXT("Initial Battle setup validation failed with error %d."), static_cast<int32>(SetupError)),
				OutError);
		}

		const FBattleTrainerSetup* LocalTrainer = OutSetup.FindTrainer(OutLocalTrainerId);
		if (LocalTrainer == nullptr
			|| LocalTrainer->Side != EBattleSide::Player
			|| LocalTrainer->Role != EBattleTrainerRole::Player
			|| LocalTrainer->Controller != EBattleDecisionController::Human)
		{
			OutSetup = FBattleSetup();
			return Fail(TEXT("Local Trainer must be the human Player-side Trainer."), OutError);
		}
		return true;
	}

	bool TryCreateEngine(
		const FBattleSetup& Setup,
		const FBattleDefinitionCatalog& Catalog,
		const uint64 Seed,
		TUniquePtr<FBattleEngine>& OutEngine,
		FString& OutError)
	{
		OutEngine.Reset();
		FBattleRejection Rejection;
		if (!FBattleEngine::TryCreate(
			Setup,
			Catalog,
			MakeUnique<FSeededBattleRandom>(Seed),
			OutEngine,
			Rejection))
		{
			return Fail(
				FString::Printf(
					TEXT("Initial Battle engine creation failed with rejection %d."),
					static_cast<int32>(Rejection.Reason)),
				OutError);
		}
		return true;
	}
}

FBattleDataTableRuntimeSource::FBattleDataTableRuntimeSource(
	TSoftObjectPtr<UDataTable> InRuntimeTable)
	: RuntimeTable(MoveTemp(InRuntimeTable))
{
}

bool FBattleDataTableRuntimeSource::TryCreateInitialBattle(
	FBattleRuntimeBundle& OutBundle,
	FString& OutError) const
{
	OutBundle.Reset();
	OutError.Reset();

	FBattleRuntimeScenarioTableRow Scenario;
	FBattleDefinitionCatalog Catalog;
	TSharedPtr<const IBattleDisplayNameResolver> DisplayNames;
	FBattleSetup Setup;
	FTrainerId LocalTrainerId;
	uint64 Seed = 0;
	if (!TryLoadScenario(RuntimeTable, Scenario, OutError)
		|| !TryBuildCatalog(Scenario, Catalog, OutError)
		|| !TryBuildDisplayNameResolver(Scenario, Catalog, DisplayNames, OutError)
		|| !TryBuildSetup(Scenario, Catalog, Setup, LocalTrainerId, Seed, OutError))
	{
		return false;
	}

	TUniquePtr<FBattleEngine> Engine;
	if (!TryCreateEngine(Setup, Catalog, Seed, Engine, OutError))
	{
		return false;
	}

	FBattleRuntimeBundle Bundle;
	Bundle.Engine = MoveTemp(Engine);
	Bundle.LocalTrainerId = LocalTrainerId;
	Bundle.DisplayNames = MoveTemp(DisplayNames);
	if (!Bundle.IsValid())
	{
		return Fail(TEXT("Initial Battle runtime bundle is incomplete."), OutError);
	}

	OutBundle = MoveTemp(Bundle);
	return true;
}
