#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleMajorStatus.h"
#include "Battle/BattleStats.h"

/** Poke Ball target family frozen per species/form at battle setup. */
enum class EBattleCaptureSpeciesClassification : uint8
{
	Normal = 0,
	UltraBeast = 1,
	Invalid = 255
};

/** Ordered external destination requested for one successful pending capture. */
enum class EBattlePendingCaptureDestination : uint8
{
	Party = 0,
	Storage = 1,
	Invalid = 255
};

/** Immutable progression inputs that capture must never infer from obedience data. */
struct POKEMONSOLARUS_API FBattleCaptureProgressionSnapshot
{
	bool bHasSnapshot = false;
	uint8 BadgeCount = 0;
	uint32 CaughtSpeciesCount = 0;
	bool bCriticalCaptureEnabled = false;
	bool bCatchingCharm = false;
	bool bUseCaughtCountHPComponentModifier = false;
	int32 CaptureCoefficientQ12 = 4096;
	bool bMustCapture = false;
	/** The unidentified Scarlet special-mode coefficient remains explicitly unsupported. */
	bool bUseUnsupportedLowPlayerLevelCoefficient = false;

	/** Returns whether the optional/required snapshot has one canonical representation. */
	[[nodiscard]] bool IsValid() const;

	friend bool operator==(
		const FBattleCaptureProgressionSnapshot& Left,
		const FBattleCaptureProgressionSnapshot& Right) = default;
};

/** Immutable exact inputs for one Scarlet/Violet capture calculation. */
struct POKEMONSOLARUS_API FBattleCaptureCalculationInput
{
	FItemId BallItemId;
	int32 BallMultiplierQ12 = 4096;
	EBattleCaptureSpeciesClassification SpeciesClassification =
		EBattleCaptureSpeciesClassification::Normal;
	int32 SpeciesCatchRate = 0;
	int32 CurrentHP = 0;
	int32 MaximumHP = 0;
	int32 TargetLevel = 0;
	int32 PlayerLevel = 0;
	EBattleMajorStatusKind MajorStatus = EBattleMajorStatusKind::None;
	FBattleCaptureProgressionSnapshot Progression;
	FBattleRandomContext RandomContext;
};

/** Exact calculation and early-stopping RNG facts for one legal capture attempt. */
struct POKEMONSOLARUS_API FBattleCaptureCalculationResult
{
	bool bValid = false;
	uint32 CaughtCountHPModifierQ12 = 4096;
	uint32 BadgeModifierQ12 = 4096;
	uint32 StatusModifierQ12 = 4096;
	uint32 CaptureCoefficientQ12 = 4096;
	uint64 CaptureIndicatorQ12 = 0;
	uint32 CriticalModifierQ12 = 0;
	uint32 CriticalThreshold = 0;
	uint32 ShakeThreshold = 0;
	bool bCriticalEligible = false;
	bool bCriticalCapture = false;
	bool bGuaranteedCapture = false;
	bool bMustCapture = false;
	bool bSucceeded = false;
	uint8 RequiredShakeChecks = 0;
	uint8 ShakeChecksPerformed = 0;
	uint8 ShakeChecksPassed = 0;
	uint8 VisualShakeCount = 0;
	TOptional<FBattleRandomDraw> CriticalDraw;
	TArray<FBattleRandomDraw> ShakeDraws;
};

/** Deterministic capture facts frozen before any gameplay RNG transaction exists. */
struct POKEMONSOLARUS_API FBattleCapturePreparation
{
	bool bValid = false;
	bool bRequiresRandomResolution = false;
	FBattleRandomContext RandomContext;
	FBattleCaptureCalculationResult PreparedResult;
};

/** Public capture metadata attached to CaptureAttempted and Captured events. */
struct POKEMONSOLARUS_API FBattleCaptureEventMetadata
{
	uint64 CaptureIndicatorQ12 = 0;
	uint32 CaughtCountHPModifierQ12 = 4096;
	uint32 BadgeModifierQ12 = 4096;
	uint32 StatusModifierQ12 = 4096;
	uint32 CaptureCoefficientQ12 = 4096;
	uint32 CriticalModifierQ12 = 0;
	uint32 CriticalThreshold = 0;
	uint32 ShakeThreshold = 0;
	bool bCriticalEligible = false;
	bool bCriticalCapture = false;
	bool bGuaranteedCapture = false;
	bool bMustCapture = false;
	bool bSucceeded = false;
	uint8 RequiredShakeChecks = 0;
	uint8 ShakeChecksPerformed = 0;
	uint8 ShakeChecksPassed = 0;
	uint8 VisualShakeCount = 0;
	bool bHasPendingDestination = false;
	uint64 PendingCaptureOrdinal = 0;
	EBattlePendingCaptureDestination PendingDestination =
		EBattlePendingCaptureDestination::Invalid;

	/** Validates the event-independent ranges and optional destination shape. */
	[[nodiscard]] bool IsValid() const;

	friend bool operator==(
		const FBattleCaptureEventMetadata& Left,
		const FBattleCaptureEventMetadata& Right) = default;
};

/** One captured move slot retained exactly for the external post-battle write. */
struct POKEMONSOLARUS_API FBattleCapturedMoveFact
{
	uint8 SlotIndex = 255;
	FMoveId MoveId;
	int32 CurrentPP = 0;
	int32 MaxPP = 0;

	friend bool operator==(
		const FBattleCapturedMoveFact& Left,
		const FBattleCapturedMoveFact& Right) = default;
};

/** Original and current held-item facts retained at the capture instant. */
struct POKEMONSOLARUS_API FBattleCapturedHeldItemFacts
{
	FItemId OriginalItemId;
	FItemId CurrentItemId;
	bool bConsumed = false;
	bool bSuppressed = false;
	bool bRevealed = false;
	bool bTemporarilyRemoved = false;
	FMoveId ChoiceLockedMoveId;

	friend bool operator==(
		const FBattleCapturedHeldItemFacts& Left,
		const FBattleCapturedHeldItemFacts& Right) = default;
};

/** Ordered pending capture retained until an external party/storage system writes it. */
struct POKEMONSOLARUS_API FBattlePendingCaptureRecord
{
	uint64 CaptureOrdinal = 0;
	EBattlePendingCaptureDestination Destination =
		EBattlePendingCaptureDestination::Invalid;
	FTrainerId OriginalTrainerId;
	FBattlerId BattlerId;
	FSourcePokemonId SourcePokemonId;
	FSpeciesFormId SpeciesFormId;
	EBattleCaptureSpeciesClassification SpeciesClassification =
		EBattleCaptureSpeciesClassification::Invalid;
	int32 Level = 0;
	int32 CurrentHP = 0;
	int32 MaxHP = 0;
	FConditionId MajorStatusId;
	TArray<FBattleCapturedMoveFact> Moves;
	FBattleCapturedHeldItemFacts HeldItem;

	/** Returns whether this record is complete and internally ordered. */
	[[nodiscard]] bool IsValid() const;

	friend bool operator==(
		const FBattlePendingCaptureRecord& Left,
		const FBattlePendingCaptureRecord& Right) = default;
};

/** Pure exact Scarlet/Violet capture calculation with traced early-stopping RNG. */
class POKEMONSOLARUS_API FBattleCaptureCalculator
{
public:
	static constexpr uint32 Q12Neutral = 4096;
	static constexpr uint64 GuaranteedIndicatorQ12 = 255ULL * Q12Neutral;

	/** Validates every immutable input without consuming RNG. */
	[[nodiscard]] static bool IsInputValid(const FBattleCaptureCalculationInput& Input);

	/** Completes every deterministic calculation without acquiring or using RNG. */
	[[nodiscard]] static bool TryPrepare(
		const FBattleCaptureCalculationInput& Input,
		FBattleCapturePreparation& OutPreparation);

	/** Resolves only the critical and early-stopping shake draws from prepared facts. */
	[[nodiscard]] static bool TryResolveRandom(
		const FBattleCapturePreparation& Preparation,
		IBattleRandom& Random,
		FBattleCaptureCalculationResult& OutResult);

	/** Converts a complete calculation result into event-safe public metadata. */
	[[nodiscard]] static FBattleCaptureEventMetadata MakeEventMetadata(
		const FBattleCaptureCalculationResult& Result);

	[[nodiscard]] static FDefinitionId GetCriticalCapturePurpose();
	[[nodiscard]] static FDefinitionId GetShakeCheckPurpose();
};
