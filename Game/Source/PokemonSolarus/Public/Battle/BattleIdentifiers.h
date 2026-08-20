#pragma once

#include "CoreMinimal.h"

/** Strong, non-zero numeric identity used by runtime battle records. */
template <typename TagType>
class TNumericBattleId
{
public:
	/** Creates an invalid ID. Use TryCreate before exposing the value. */
	constexpr TNumericBattleId() = default;

	/** Creates an ID from a positive stable value and resets OutId on failure. */
	[[nodiscard]] static constexpr bool TryCreate(const uint64 InValue, TNumericBattleId& OutId)
	{
		OutId = TNumericBattleId();
		if (InValue == 0)
		{
			return false;
		}

		OutId.Value = InValue;
		return true;
	}

	/** Returns whether this ID contains a usable non-zero value. */
	[[nodiscard]] constexpr bool IsValid() const
	{
		return Value != 0;
	}

	/** Returns the explicit integer encoding used by canonical serialization. */
	[[nodiscard]] constexpr uint64 GetValue() const
	{
		return Value;
	}

	friend constexpr bool operator==(const TNumericBattleId& Left, const TNumericBattleId& Right)
	{
		return Left.Value == Right.Value;
	}

	friend constexpr bool operator!=(const TNumericBattleId& Left, const TNumericBattleId& Right)
	{
		return !(Left == Right);
	}

	friend constexpr bool operator<(const TNumericBattleId& Left, const TNumericBattleId& Right)
	{
		return Left.Value < Right.Value;
	}

	friend uint32 GetTypeHash(const TNumericBattleId& Id)
	{
		return ::GetTypeHash(Id.Value);
	}

private:
	uint64 Value = 0;
};

struct FBattleIdTag;
struct FTurnIdTag;
struct FActionIdTag;
struct FResolutionIdTag;
struct FTrainerIdTag;
struct FBattlerIdTag;
struct FSourcePokemonIdTag;

/** Stable identity of one battle. */
using FBattleId = TNumericBattleId<FBattleIdTag>;
/** Stable identity of one turn within a battle. */
using FTurnId = TNumericBattleId<FTurnIdTag>;
/** Stable identity of one accepted action within a battle. */
using FActionId = TNumericBattleId<FActionIdTag>;
/** Stable identity of one resolution attempt within a battle. */
using FResolutionId = TNumericBattleId<FResolutionIdTag>;
/** Stable identity of one Trainer participating in a battle. */
using FTrainerId = TNumericBattleId<FTrainerIdTag>;
/** Stable identity of one transient battler record. */
using FBattlerId = TNumericBattleId<FBattlerIdTag>;
/** Opaque identity of the persistent source Pokemon copied into battle. */
using FSourcePokemonId = TNumericBattleId<FSourcePokemonIdTag>;

/** Stable authored definition identity. NAME_None is always invalid. */
class POKEMONSOLARUS_API FDefinitionId
{
public:
	/** Creates an invalid definition ID. */
	FDefinitionId() = default;

	/** Creates a definition ID from an authored name and resets OutId on failure. */
	[[nodiscard]] static bool TryCreate(const FName InName, FDefinitionId& OutId)
	{
		OutId = FDefinitionId();
		if (InName.IsNone())
		{
			return false;
		}

		OutId.Name = InName;
		return true;
	}

	/** Returns whether this ID contains an authored name. */
	[[nodiscard]] bool IsValid() const
	{
		return !Name.IsNone();
	}

	/** Returns the authored name. Canonical serializers must encode the text, not FName internals. */
	[[nodiscard]] FName GetName() const
	{
		return Name;
	}

	/** Provides stable lexical ordering independent of FName allocation order. */
	[[nodiscard]] bool LexicalLess(const FDefinitionId& Other) const
	{
		return Name.LexicalLess(Other.Name);
	}

	friend bool operator==(const FDefinitionId& Left, const FDefinitionId& Right)
	{
		return Left.Name == Right.Name;
	}

	friend bool operator!=(const FDefinitionId& Left, const FDefinitionId& Right)
	{
		return !(Left == Right);
	}

	friend uint32 GetTypeHash(const FDefinitionId& Id)
	{
		return GetTypeHash(Id.Name);
	}

private:
	FName Name = NAME_None;
};

/** Strong definition identity that prevents mixing definition families. */
template <typename TagType>
class TTypedDefinitionId
{
public:
	/** Creates an invalid typed definition ID. */
	TTypedDefinitionId() = default;

	/** Creates a typed ID from a valid generic definition ID. */
	[[nodiscard]] static bool TryCreate(const FDefinitionId& InDefinitionId, TTypedDefinitionId& OutId)
	{
		OutId = TTypedDefinitionId();
		if (!InDefinitionId.IsValid())
		{
			return false;
		}

		OutId.DefinitionId = InDefinitionId;
		return true;
	}

	/** Creates a typed ID directly from an authored name. */
	[[nodiscard]] static bool TryCreate(const FName InName, TTypedDefinitionId& OutId)
	{
		FDefinitionId GenericId;
		if (!FDefinitionId::TryCreate(InName, GenericId))
		{
			OutId = TTypedDefinitionId();
			return false;
		}

		return TryCreate(GenericId, OutId);
	}

	/** Returns whether this typed ID contains a valid generic definition ID. */
	[[nodiscard]] bool IsValid() const
	{
		return DefinitionId.IsValid();
	}

	/** Returns the generic definition identity without losing the family type on this object. */
	[[nodiscard]] const FDefinitionId& GetDefinitionId() const
	{
		return DefinitionId;
	}

	/** Provides stable lexical ordering for deterministic catalog construction. */
	[[nodiscard]] bool LexicalLess(const TTypedDefinitionId& Other) const
	{
		return DefinitionId.LexicalLess(Other.DefinitionId);
	}

	friend bool operator==(const TTypedDefinitionId& Left, const TTypedDefinitionId& Right)
	{
		return Left.DefinitionId == Right.DefinitionId;
	}

	friend bool operator!=(const TTypedDefinitionId& Left, const TTypedDefinitionId& Right)
	{
		return !(Left == Right);
	}

	friend uint32 GetTypeHash(const TTypedDefinitionId& Id)
	{
		return GetTypeHash(Id.DefinitionId);
	}

private:
	FDefinitionId DefinitionId;
};

struct FSpeciesFormIdTag;
struct FMoveIdTag;
struct FConditionIdTag;
struct FAbilityIdTag;
struct FItemIdTag;

/** Stable identity of one battle-facing species/form definition. */
using FSpeciesFormId = TTypedDefinitionId<FSpeciesFormIdTag>;
/** Stable identity of one move definition. */
using FMoveId = TTypedDefinitionId<FMoveIdTag>;
/** Stable identity of one condition definition. */
using FConditionId = TTypedDefinitionId<FConditionIdTag>;
/** Stable identity of one Ability definition. */
using FAbilityId = TTypedDefinitionId<FAbilityIdTag>;
/** Stable identity of one held, battle, or capture item definition. */
using FItemId = TTypedDefinitionId<FItemIdTag>;

/** The two deterministic sides supported by the battle core. */
enum class EBattleSide : uint8
{
	Player = 0,
	Opponent = 1
};

/** The two structurally present active positions on each side. */
enum class EBattlePosition : uint8
{
	Left = 0,
	Right = 1
};

/** Stable zero-based party-slot identity. Only indices 0 through 5 are valid. */
class POKEMONSOLARUS_API FPartySlotId
{
public:
	static constexpr int32 PartySize = 6;

	/** Creates an invalid party slot. */
	constexpr FPartySlotId() = default;

	/** Creates a stable party slot and resets OutId on failure. */
	[[nodiscard]] static constexpr bool TryCreate(const int32 InIndex, FPartySlotId& OutId)
	{
		OutId = FPartySlotId();
		if (InIndex < 0 || InIndex >= PartySize)
		{
			return false;
		}

		OutId.Index = static_cast<uint8>(InIndex);
		return true;
	}

	/** Returns whether this party slot is within 0 through 5. */
	[[nodiscard]] constexpr bool IsValid() const
	{
		return Index < PartySize;
	}

	/** Returns the zero-based slot index, or 255 for an invalid/default slot. */
	[[nodiscard]] constexpr uint8 GetIndex() const
	{
		return Index;
	}

	friend constexpr bool operator==(const FPartySlotId& Left, const FPartySlotId& Right)
	{
		return Left.Index == Right.Index;
	}

	friend constexpr bool operator!=(const FPartySlotId& Left, const FPartySlotId& Right)
	{
		return !(Left == Right);
	}

	friend constexpr bool operator<(const FPartySlotId& Left, const FPartySlotId& Right)
	{
		return Left.Index < Right.Index;
	}

	friend uint32 GetTypeHash(const FPartySlotId& Id)
	{
		return ::GetTypeHash(Id.Index);
	}

private:
	static constexpr uint8 InvalidIndex = 255;
	uint8 Index = InvalidIndex;
};

/** Stable identity of one side/position pair; it never depends on array addresses. */
class POKEMONSOLARUS_API FActiveSlotId
{
public:
	/** Creates an invalid active slot. */
	constexpr FActiveSlotId() = default;

	/** Creates a side/position identity and resets OutId on failure. */
	[[nodiscard]] static constexpr bool TryCreate(
		const EBattleSide InSide,
		const EBattlePosition InPosition,
		FActiveSlotId& OutId)
	{
		OutId = FActiveSlotId();
		if (!IsKnownSide(InSide) || !IsKnownPosition(InPosition))
		{
			return false;
		}

		OutId.Side = InSide;
		OutId.Position = InPosition;
		OutId.bValid = true;
		return true;
	}

	/** Returns whether both the side and position were validated. */
	[[nodiscard]] constexpr bool IsValid() const
	{
		return bValid;
	}

	/** Returns the stored side. Call IsValid first. */
	[[nodiscard]] constexpr EBattleSide GetSide() const
	{
		return Side;
	}

	/** Returns the stored active position. Call IsValid first. */
	[[nodiscard]] constexpr EBattlePosition GetPosition() const
	{
		return Position;
	}

	friend constexpr bool operator==(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		return Left.bValid == Right.bValid
			&& (!Left.bValid || (Left.Side == Right.Side && Left.Position == Right.Position));
	}

	friend constexpr bool operator!=(const FActiveSlotId& Left, const FActiveSlotId& Right)
	{
		return !(Left == Right);
	}

	friend uint32 GetTypeHash(const FActiveSlotId& Id)
	{
		if (!Id.bValid)
		{
			return 0;
		}

		return HashCombineFast(
			::GetTypeHash(static_cast<uint8>(Id.Side)),
			::GetTypeHash(static_cast<uint8>(Id.Position)));
	}

private:
	[[nodiscard]] static constexpr bool IsKnownSide(const EBattleSide InSide)
	{
		return InSide == EBattleSide::Player || InSide == EBattleSide::Opponent;
	}

	[[nodiscard]] static constexpr bool IsKnownPosition(const EBattlePosition InPosition)
	{
		return InPosition == EBattlePosition::Left || InPosition == EBattlePosition::Right;
	}

	EBattleSide Side = EBattleSide::Player;
	EBattlePosition Position = EBattlePosition::Left;
	bool bValid = false;
};
