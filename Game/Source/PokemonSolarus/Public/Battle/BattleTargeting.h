#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"
#include "Battle/BattleRandom.h"
#include "Battle/BattleSetupTypes.h"

/** One structural active position's current targeting lifecycle. */
enum class EBattleTargetPositionState : uint8
{
	Empty = 0,
	Living = 1,
	Fainted = 2,
	Captured = 3,
	Removed = 4
};

/** Public, immutable-by-copy facts needed to evaluate one structural active position. */
struct POKEMONSOLARUS_API FBattleTargetPositionFacts
{
	FActiveSlotId ActiveSlotId;
	FBattlerId BattlerId;
	EBattleTargetPositionState State = EBattleTargetPositionState::Empty;
	bool bSemiInvulnerable = false;
};

/** Strong pair identifying one battler and the structural position it currently occupies. */
struct POKEMONSOLARUS_API FBattleBattlerTarget
{
	FActiveSlotId ActiveSlotId;
	FBattlerId BattlerId;

	/** Returns whether both identities are present; battlefield legality is validated separately. */
	[[nodiscard]] bool IsValid() const
	{
		return ActiveSlotId.IsValid() && BattlerId.IsValid();
	}

	friend bool operator==(const FBattleBattlerTarget& Left, const FBattleBattlerTarget& Right)
	{
		return Left.ActiveSlotId == Right.ActiveSlotId && Left.BattlerId == Right.BattlerId;
	}

	friend bool operator!=(const FBattleBattlerTarget& Left, const FBattleBattlerTarget& Right)
	{
		return !(Left == Right);
	}
};

/** Discriminator for a frozen battler, side, or field target. */
enum class EBattleResolvedTargetKind : uint8
{
	Invalid = 0,
	Battler = 1,
	Side = 2,
	Field = 3
};

/** Validated typed target consumed by later hit and effect packages. */
class POKEMONSOLARUS_API FBattleResolvedTarget
{
public:
	/** Creates an invalid target. Use a typed factory before exposing it. */
	FBattleResolvedTarget() = default;

	/** Creates one battler target and resets OutTarget on invalid identities. */
	[[nodiscard]] static bool TryCreateBattler(
		const FBattleBattlerTarget& InBattler,
		FBattleResolvedTarget& OutTarget);

	/** Creates one side target and resets OutTarget for an unknown side value. */
	[[nodiscard]] static bool TryCreateSide(
		EBattleSide InSide,
		FBattleResolvedTarget& OutTarget);

	/** Creates the one typed field target. */
	[[nodiscard]] static FBattleResolvedTarget CreateField();

	/** Returns whether a typed factory produced this value. */
	[[nodiscard]] bool IsValid() const;

	/** Returns the stored target discriminator. */
	[[nodiscard]] EBattleResolvedTargetKind GetKind() const
	{
		return Kind;
	}

	/** Returns the battler identity pair. Call only when GetKind is Battler. */
	[[nodiscard]] const FBattleBattlerTarget& GetBattler() const
	{
		return Battler;
	}

	/** Returns the targeted side. Call only when GetKind is Side. */
	[[nodiscard]] EBattleSide GetSide() const
	{
		return Side;
	}

	friend bool operator==(const FBattleResolvedTarget& Left, const FBattleResolvedTarget& Right)
	{
		return Left.Kind == Right.Kind
			&& Left.Battler == Right.Battler
			&& Left.Side == Right.Side;
	}

	friend bool operator!=(const FBattleResolvedTarget& Left, const FBattleResolvedTarget& Right)
	{
		return !(Left == Right);
	}

private:
	EBattleResolvedTargetKind Kind = EBattleResolvedTargetKind::Invalid;
	FBattleBattlerTarget Battler;
	EBattleSide Side = EBattleSide::Player;
};

/** Validated public battlefield facts used to build target-selection options. */
struct POKEMONSOLARUS_API FBattleTargetSelectionSpec
{
	EBattleTargetClass TargetClass = static_cast<EBattleTargetClass>(255);
	FActiveSlotId UserSlotId;
	FBattlerId UserBattlerId;
	TArray<FBattleTargetPositionFacts> Positions;
};

/** Stable selector-facing target information for one move target class. */
struct POKEMONSOLARUS_API FBattleTargetSelectionResult
{
	EBattleTargetClass TargetClass = static_cast<EBattleTargetClass>(255);
	bool bRequiresExplicitChoice = false;
	bool bHasLegalTarget = false;

	/**
	 * Stable battler preview set. For explicit classes these are legal choices;
	 * for random and spread classes these are the candidate or affected battlers.
	 */
	TArray<FBattleBattlerTarget> BattlerCandidates;

	/**
	 * Fully determined automatic targets. RandomLegalOpponent remains empty until
	 * resolution because selecting its one candidate consumes battle RNG.
	 */
	TArray<FBattleResolvedTarget> AutomaticTargets;
};

/** One rule hook's proposed replacement for an already resolved single battler target. */
struct POKEMONSOLARUS_API FBattleTargetRedirectionProposal
{
	FBattleBattlerTarget ProposedTarget;
};

/** Stable valid outcome from resolving one target class. */
enum class EBattleTargetResolutionOutcome : uint8
{
	Invalid = 0,
	Resolved = 1,
	NoLegalTarget = 2,
	CapturedTargetCanceled = 3
};

/** Validation or injected-RNG failure from a target query. */
enum class EBattleTargetingError : uint8
{
	None = 0,
	InvalidContext = 1,
	InvalidPositions = 2,
	InvalidSelection = 3,
	InvalidRedirectionProposal = 4,
	RandomFailure = 5
};

/** Public facts required to freeze one move's final C04B target set. */
struct POKEMONSOLARUS_API FBattleTargetResolutionSpec
{
	EBattleTargetClass TargetClass = static_cast<EBattleTargetClass>(255);
	FActiveSlotId UserSlotId;
	FBattlerId UserBattlerId;
	TArray<FBattleTargetPositionFacts> Positions;

	/** Required only by SelectedAlly, SelectedOpponent, AnySelectedBattler, and SelectedOtherBattler. */
	FBattleBattlerTarget ExplicitTarget;

	/** Applied in caller-supplied rule order after a single battler target exists. */
	TArray<FBattleTargetRedirectionProposal> RedirectionProposals;

	/** Required for RandomLegalOpponent and ignored by classes that consume no target draw. */
	FBattleRandomContext RandomContext;
};

/** Frozen typed target set and redirection facts supplied to C05. */
struct POKEMONSOLARUS_API FBattleTargetResolutionResult
{
	EBattleTargetClass TargetClass = static_cast<EBattleTargetClass>(255);
	EBattleTargetResolutionOutcome Outcome = EBattleTargetResolutionOutcome::Invalid;
	TArray<FBattleResolvedTarget> Targets;
	bool bWasRedirected = false;
	bool bUsedFaintedTargetFallback = false;
};

/** Pure selection and resolution rules for the twelve frozen C04B target classes. */
class POKEMONSOLARUS_API FBattleTargetResolver
{
public:
	/**
	 * Validates four structural position records and returns stable selector/preview targets.
	 * Empty, fainted, captured, and removed records are excluded; semi-invulnerability is not.
	 */
	[[nodiscard]] static bool TryBuildSelection(
		const FBattleTargetSelectionSpec& Spec,
		FBattleTargetSelectionResult& OutResult,
		EBattleTargetingError& OutError);

	/**
	 * Freezes one typed target set, using the injected stream only for RandomLegalOpponent.
	 * Captured explicit targets cancel before RNG; the first effective legal ordered
	 * redirection proposal replaces only an existing single battler target.
	 */
	[[nodiscard]] static bool TryResolve(
		const FBattleTargetResolutionSpec& Spec,
		IBattleRandom& Random,
		FBattleTargetResolutionResult& OutResult,
		EBattleTargetingError& OutError);

	/** Returns the exact typed rule purpose required by a random-opponent context. */
	[[nodiscard]] static FDefinitionId GetRandomLegalOpponentRulePurpose();
};
