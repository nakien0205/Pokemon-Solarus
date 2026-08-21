#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleRandom.h"

/** Semantic switch path. Blockers can apply differently without hardcoded condition IDs. */
enum class EBattleSwitchKind : uint8
{
	Voluntary = 0,
	Forced = 1,
	Pivot = 2
};

/** State carried from the outgoing battler to the incoming battler. */
enum class EBattleSwitchStateTransferPolicy : uint8
{
	ClearTransient = 0,
	BatonPassLike = 1
};

/** Stable reason a switch or one candidate cannot proceed. */
enum class EBattleSwitchBlockReason : uint8
{
	None = 0,
	InvalidRequest = 1,
	PartyTooLarge = 2,
	EncounterPolicy = 3,
	Trapped = 4,
	EmptyPartySlot = 5,
	WrongOwner = 6,
	AlreadyActive = 7,
	Fainted = 8,
	Egg = 9,
	Captured = 10,
	Removed = 11,
	AlreadyReserved = 12,
	NoLegalReserve = 13,
	UnsupportedTransferPolicy = 14,
	IllegalRequestedSlot = 15,
	ActingBattlerUnavailable = 16,
	RandomFailure = 17
};

/** Current facts for one structural party position. */
struct POKEMONSOLARUS_API FBattleSwitchCandidateFacts
{
	FPartySlotId PartySlotId;
	FTrainerId TrainerId;
	FBattlerId BattlerId;
	bool bOccupied = false;
	bool bAlreadyActive = false;
	bool bFainted = false;
	bool bEgg = false;
	bool bCaptured = false;
	bool bRemoved = false;
	bool bAlreadyReserved = false;
};

/** Future-rule inputs. C07/C09 may populate these without changing switching logic. */
struct POKEMONSOLARUS_API FBattleSwitchBlockers
{
	bool bEncounterPolicyAllows = true;
	FDefinitionId EncounterPolicyRuleId;
	bool bTrapped = false;
	FDefinitionId TrappingRuleId;
};

/** Mutable input for deterministic party legality evaluation. */
struct POKEMONSOLARUS_API FBattleSwitchLegalitySpec
{
	EBattleSwitchKind Kind = EBattleSwitchKind::Voluntary;
	FTrainerId ActingTrainerId;
	FBattlerId ActingBattlerId;
	FActiveSlotId ActiveSlotId;
	EBattleSwitchStateTransferPolicy TransferPolicy =
		EBattleSwitchStateTransferPolicy::ClearTransient;
	FBattleSwitchBlockers Blockers;
	TArray<FBattleSwitchCandidateFacts> Candidates;
};

/** One canonical party-slot result. */
struct POKEMONSOLARUS_API FBattleSwitchCandidateResult
{
	FPartySlotId PartySlotId;
	FBattlerId BattlerId;
	bool bLegal = false;
	EBattleSwitchBlockReason Reason = EBattleSwitchBlockReason::None;
};

/** Immutable-by-interface canonical legality result in party-slot order. */
class POKEMONSOLARUS_API FBattleSwitchLegalityResult
{
public:
	[[nodiscard]] bool IsValid() const { return bValid; }
	[[nodiscard]] EBattleSwitchKind GetKind() const { return Kind; }
	[[nodiscard]] bool IsBlocked() const { return bBlocked; }
	[[nodiscard]] EBattleSwitchBlockReason GetBlockReason() const { return BlockReason; }
	[[nodiscard]] FDefinitionId GetBlockingRuleId() const { return BlockingRuleId; }
	[[nodiscard]] TConstArrayView<FBattleSwitchCandidateResult> GetCandidates() const
	{
		return Candidates;
	}
	[[nodiscard]] TConstArrayView<FPartySlotId> GetLegalPartySlots() const
	{
		return LegalPartySlots;
	}

private:
	friend class FBattleSwitchResolver;

	bool bValid = false;
	EBattleSwitchKind Kind = EBattleSwitchKind::Voluntary;
	bool bBlocked = false;
	EBattleSwitchBlockReason BlockReason = EBattleSwitchBlockReason::None;
	FDefinitionId BlockingRuleId;
	TArray<FBattleSwitchCandidateResult> Candidates;
	TArray<FPartySlotId> LegalPartySlots;
};

/** Selection input after legality is known. RandomContext is required only for forced switching. */
struct POKEMONSOLARUS_API FBattleSwitchSelectionSpec
{
	FPartySlotId RequestedPartySlotId;
	FBattleRandomContext RandomContext;
};

/** Immutable-by-interface selected switch destination or typed no-selection outcome. */
class POKEMONSOLARUS_API FBattleSwitchResolution
{
public:
	[[nodiscard]] bool IsValid() const { return bValid; }
	[[nodiscard]] bool HasSelection() const { return bHasSelection; }
	[[nodiscard]] EBattleSwitchBlockReason GetReason() const { return Reason; }
	[[nodiscard]] FPartySlotId GetSelectedPartySlotId() const { return SelectedPartySlotId; }
	[[nodiscard]] FBattlerId GetSelectedBattlerId() const { return SelectedBattlerId; }
	[[nodiscard]] const TOptional<FBattleRandomDraw>& GetRandomDraw() const { return RandomDraw; }

private:
	friend class FBattleSwitchResolver;

	bool bValid = false;
	bool bHasSelection = false;
	EBattleSwitchBlockReason Reason = EBattleSwitchBlockReason::None;
	FPartySlotId SelectedPartySlotId;
	FBattlerId SelectedBattlerId;
	TOptional<FBattleRandomDraw> RandomDraw;
};

/** Pure reusable C06A legality and reserve-selection rules. */
class POKEMONSOLARUS_API FBattleSwitchResolver
{
public:
	/** Validates and canonicalizes candidate facts without consuming RNG. */
	[[nodiscard]] static bool TryBuildLegality(
		const FBattleSwitchLegalitySpec& Spec,
		FBattleSwitchLegalityResult& OutResult);

	/** Selects an explicit reserve or performs the one required forced-switch draw. */
	[[nodiscard]] static bool TryResolve(
		const FBattleSwitchLegalityResult& Legality,
		const FBattleSwitchSelectionSpec& Spec,
		IBattleRandom& Random,
		FBattleSwitchResolution& OutResolution);

	/** Stable semantic purpose attached to every non-empty forced-switch draw. */
	[[nodiscard]] static FDefinitionId GetForcedSelectionRulePurpose();
};
