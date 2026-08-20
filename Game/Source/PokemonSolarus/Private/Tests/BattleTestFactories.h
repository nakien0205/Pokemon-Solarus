#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"

namespace BattleTest
{
	template <typename IdType>
	IdType MakeNumericId(const uint64 Value)
	{
		IdType Id;
		const bool bCreated = IdType::TryCreate(Value, Id);
		check(bCreated);
		return Id;
	}

	template <typename IdType>
	IdType MakeDefinitionId(const TCHAR* Value)
	{
		IdType Id;
		const bool bCreated = IdType::TryCreate(FName(Value), Id);
		check(bCreated);
		return Id;
	}

	inline FPartySlotId MakePartySlotId(const int32 Index)
	{
		FPartySlotId Id;
		const bool bCreated = FPartySlotId::TryCreate(Index, Id);
		check(bCreated);
		return Id;
	}

	inline FActiveSlotId MakeActiveSlotId(
		const EBattleSide Side,
		const EBattlePosition Position)
	{
		FActiveSlotId Id;
		const bool bCreated = FActiveSlotId::TryCreate(Side, Position, Id);
		check(bCreated);
		return Id;
	}
}
