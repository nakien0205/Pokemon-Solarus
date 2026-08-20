#pragma once

#include "CoreMinimal.h"

/** Encounter policy family frozen when a battle begins. */
enum class EBattleEncounterKind : uint8
{
	Wild = 0,
	Trainer = 1,
	Rival = 2,
	BossGym = 3,
	TutorialScripted = 4
};

/** Supported active-slot arrangement. */
enum class EBattleFormat : uint8
{
	Single = 0,
	Double = 1,
	PartnerDouble = 2
};

/** High-level lifecycle phase of a battle. */
enum class EBattlePhase : uint8
{
	Setup = 0,
	Selecting = 1,
	Locked = 2,
	Resolving = 3,
	MandatoryReplacement = 4,
	EndOfTurn = 5,
	Terminal = 6
};

/** Public action family used by later decision and event contracts. */
enum class EBattleActionKind : uint8
{
	Fight = 0,
	Bag = 1,
	Switch = 2,
	Run = 3,
	WildFlee = 4,
	Replacement = 5,
	ScriptedEnd = 6,
	Abandon = 7
};

/** Terminal or non-terminal battle outcome. */
enum class EBattleOutcome : uint8
{
	InProgress = 0,
	Victory = 1,
	Defeat = 2,
	Escape = 3,
	ScriptedEnd = 4,
	Abandoned = 5
};

/** Typed cause that qualifies a battle outcome without display text. */
enum class EBattleOutcomeCause : uint8
{
	None = 0,
	Ordinary = 1,
	Capture = 2,
	PartnerTeamVictory = 3,
	SimultaneousFaint = 4,
	OpponentFled = 5
};

/** Supported move/effect target family. */
enum class EBattleTargetClass : uint8
{
	Self = 0,
	SelectedAlly = 1,
	SelectedOpponent = 2,
	AnySelectedBattler = 3,
	RandomLegalOpponent = 4,
	UserSide = 5,
	OpponentSide = 6,
	BothSides = 7,
	Field = 8,
	FixedSpreadSet = 9
};

/** Observer scope for a fact or event; ownership IDs supply the matching Trainer or side. */
enum class EBattleVisibilityLevel : uint8
{
	CoreOnly = 0,
	OwningTrainer = 1,
	OwningSide = 2,
	Public = 3
};

/** The five non-HP stats a nature may boost or reduce. */
enum class ENatureStat : uint8
{
	None = 0,
	Attack = 1,
	Defense = 2,
	SpecialAttack = 3,
	SpecialDefense = 4,
	Speed = 5
};

/**
 * Resolved nature effect consumed by the pure stat calculator.
 * It owns no nature definition ID or authored row.
 */
class POKEMONSOLARUS_API FNatureStatModifier
{
public:
	/** Creates the valid neutral modifier. */
	constexpr FNatureStatModifier() = default;

	/**
	 * Creates either a neutral modifier or one distinct boost/reduction pair.
	 * Invalid input resets OutModifier to neutral and returns false.
	 */
	[[nodiscard]] static constexpr bool TryCreate(
		const ENatureStat InBoostedStat,
		const ENatureStat InReducedStat,
		FNatureStatModifier& OutModifier)
	{
		OutModifier = FNatureStatModifier();
		const bool bNeutral = InBoostedStat == ENatureStat::None && InReducedStat == ENatureStat::None;
		if (bNeutral)
		{
			return true;
		}

		if (!IsAffectedStat(InBoostedStat)
			|| !IsAffectedStat(InReducedStat)
			|| InBoostedStat == InReducedStat)
		{
			return false;
		}

		OutModifier.BoostedStat = InBoostedStat;
		OutModifier.ReducedStat = InReducedStat;
		return true;
	}

	/** Returns whether no stat is boosted or reduced. */
	[[nodiscard]] constexpr bool IsNeutral() const
	{
		return BoostedStat == ENatureStat::None && ReducedStat == ENatureStat::None;
	}

	/** Returns the one boosted stat, or None for a neutral nature. */
	[[nodiscard]] constexpr ENatureStat GetBoostedStat() const
	{
		return BoostedStat;
	}

	/** Returns the one reduced stat, or None for a neutral nature. */
	[[nodiscard]] constexpr ENatureStat GetReducedStat() const
	{
		return ReducedStat;
	}

	/**
	 * Returns the exact multiplier for a non-HP nature stat.
	 * Boosted is 11/10, reduced is 9/10, and unaffected is 10/10.
	 */
	[[nodiscard]] constexpr bool TryGetMultiplier(
		const ENatureStat InStat,
		int32& OutNumerator,
		int32& OutDenominator) const
	{
		OutNumerator = 0;
		OutDenominator = 0;
		if (!IsAffectedStat(InStat))
		{
			return false;
		}

		OutNumerator = InStat == BoostedStat ? 11 : (InStat == ReducedStat ? 9 : 10);
		OutDenominator = 10;
		return true;
	}

	friend constexpr bool operator==(const FNatureStatModifier& Left, const FNatureStatModifier& Right)
	{
		return Left.BoostedStat == Right.BoostedStat && Left.ReducedStat == Right.ReducedStat;
	}

	friend constexpr bool operator!=(const FNatureStatModifier& Left, const FNatureStatModifier& Right)
	{
		return !(Left == Right);
	}

private:
	[[nodiscard]] static constexpr bool IsAffectedStat(const ENatureStat InStat)
	{
		return InStat == ENatureStat::Attack
			|| InStat == ENatureStat::Defense
			|| InStat == ENatureStat::SpecialAttack
			|| InStat == ENatureStat::SpecialDefense
			|| InStat == ENatureStat::Speed;
	}

	ENatureStat BoostedStat = ENatureStat::None;
	ENatureStat ReducedStat = ENatureStat::None;
};
