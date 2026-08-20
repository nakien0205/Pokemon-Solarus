#include "Battle/BattleTypeChart.h"

bool FBattleTypeChart::TryCreate(
	const TConstArrayView<FBattleTypeChartEntry> Entries,
	FBattleTypeChart& OutChart,
	EBattleTypeChartValidationError& OutError)
{
	OutChart = FBattleTypeChart();
	OutError = EBattleTypeChartValidationError::None;

	if (Entries.Num() != EntryCount)
	{
		OutError = EBattleTypeChartValidationError::IncompleteChart;
		return false;
	}

	TStaticArray<bool, EntryCount> Seen{};
	for (const FBattleTypeChartEntry& Entry : Entries)
	{
		if (!IsKnownType(Entry.AttackingType) || !IsKnownType(Entry.DefendingType))
		{
			OutError = EBattleTypeChartValidationError::InvalidType;
			return false;
		}

		uint8 Encoded = 0;
		if (!TryEncodeQuarterUnits(Entry.Numerator, Entry.Denominator, Encoded))
		{
			OutError = EBattleTypeChartValidationError::InvalidMultiplier;
			return false;
		}

		const int32 Index = ToIndex(Entry.AttackingType, Entry.DefendingType);
		if (Seen[Index])
		{
			OutError = EBattleTypeChartValidationError::DuplicateEntry;
			return false;
		}

		Seen[Index] = true;
		OutChart.QuarterUnits[Index] = Encoded;
	}

	for (const bool bSeen : Seen)
	{
		if (!bSeen)
		{
			OutChart = FBattleTypeChart();
			OutError = EBattleTypeChartValidationError::IncompleteChart;
			return false;
		}
	}

	OutChart.bValid = true;
	return true;
}

bool FBattleTypeChart::IsKnownType(const EPokemonType Type)
{
	return static_cast<uint8>(Type) < TypeCount;
}

bool FBattleTypeChart::TryGetEffectiveness(
	const EPokemonType AttackingType,
	const EPokemonType DefendingType,
	FBattleTypeEffectiveness& OutEffectiveness) const
{
	OutEffectiveness = FBattleTypeEffectiveness();
	if (!bValid || !IsKnownType(AttackingType) || !IsKnownType(DefendingType))
	{
		return false;
	}

	OutEffectiveness = DecodeQuarterUnits(QuarterUnits[ToIndex(AttackingType, DefendingType)]);
	return true;
}

bool FBattleTypeChart::TryGetDualEffectiveness(
	const EPokemonType AttackingType,
	const EPokemonType FirstDefendingType,
	const EPokemonType SecondDefendingType,
	FBattleTypeEffectiveness& OutEffectiveness) const
{
	OutEffectiveness = FBattleTypeEffectiveness();
	if (!bValid
		|| !IsKnownType(AttackingType)
		|| !IsKnownType(FirstDefendingType)
		|| !IsKnownType(SecondDefendingType)
		|| FirstDefendingType == SecondDefendingType)
	{
		return false;
	}

	const uint8 FirstQuarterUnits = QuarterUnits[ToIndex(AttackingType, FirstDefendingType)];
	const uint8 SecondQuarterUnits = QuarterUnits[ToIndex(AttackingType, SecondDefendingType)];
	const uint8 ProductQuarterUnits = static_cast<uint8>(
		(static_cast<uint16>(FirstQuarterUnits) * static_cast<uint16>(SecondQuarterUnits)) / 4);
	OutEffectiveness = DecodeQuarterUnits(ProductQuarterUnits);
	return true;
}

bool FBattleTypeChart::TryEncodeQuarterUnits(
	const int32 Numerator,
	const int32 Denominator,
	uint8& OutQuarterUnits)
{
	OutQuarterUnits = 0;
	if (Numerator == 0 && Denominator == 1)
	{
		return true;
	}
	if (Numerator == 1 && Denominator == 2)
	{
		OutQuarterUnits = 2;
		return true;
	}
	if (Numerator == 1 && Denominator == 1)
	{
		OutQuarterUnits = 4;
		return true;
	}
	if (Numerator == 2 && Denominator == 1)
	{
		OutQuarterUnits = 8;
		return true;
	}
	return false;
}

FBattleTypeEffectiveness FBattleTypeChart::DecodeQuarterUnits(const uint8 QuarterUnitValue)
{
	switch (QuarterUnitValue)
	{
	case 0:
		return {0, 1};
	case 1:
		return {1, 4};
	case 2:
		return {1, 2};
	case 4:
		return {1, 1};
	case 8:
		return {2, 1};
	case 16:
		return {4, 1};
	default:
		return {};
	}
}

int32 FBattleTypeChart::ToIndex(
	const EPokemonType AttackingType,
	const EPokemonType DefendingType)
{
	return static_cast<int32>(AttackingType) * TypeCount + static_cast<int32>(DefendingType);
}
