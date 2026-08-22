#pragma once

#include "CoreMinimal.h"
#include "Battle/BattleIdentifiers.h"
#include "Battle/BattleMoveCategory.h"
#include "Battle/BattleSetupTypes.h"
#include "Battle/BattleStatStages.h"
#include "Battle/BattleStats.h"

struct FNatureIdTag;

/** Stable identity of one authored nature definition. */
using FNatureId = TTypedDefinitionId<FNatureIdTag>;

/** The complete modern 18-type set supported by Solarus. */
enum class EPokemonType : uint8
{
	Normal = 0,
	Fire = 1,
	Water = 2,
	Electric = 3,
	Grass = 4,
	Ice = 5,
	Fighting = 6,
	Poison = 7,
	Ground = 8,
	Flying = 9,
	Psychic = 10,
	Bug = 11,
	Rock = 12,
	Ghost = 13,
	Dragon = 14,
	Dark = 15,
	Steel = 16,
	Fairy = 17,
	Invalid = 255
};

/** Authored item family; later packages add family-specific rule payloads. */
enum class EBattleItemKind : uint8
{
	Held = 0,
	Battle = 1,
	Capture = 2,
	Invalid = 255
};

/** Authored condition family used to validate move-effect references. */
enum class EBattleConditionKind : uint8
{
	MajorStatus = 0,
	Volatile = 1,
	Weather = 2,
	Terrain = 3,
	Hazard = 4,
	Screen = 5,
	Room = 6,
	SideCondition = 7,
	Invalid = 255
};

/** Generic reusable operation represented by one ordered move-effect descriptor. */
enum class EBattleMoveEffectKind : uint8
{
	Damage = 0,
	ApplyCondition = 1,
	ModifyStatStage = 2,
	Heal = 3,
	Drain = 4,
	Recoil = 5,
	MultiHit = 6,
	SetFieldCondition = 7,
	SetSideCondition = 8,
	Switch = 9,
	ChangeItem = 10,
	Charge = 11,
	Recharge = 12,
	Protect = 13,
	SemiInvulnerability = 14,
	RemoveCondition = 15,
	Invalid = 255
};

/** Effect-local selector resolved relative to the move user and resolved targets. */
enum class EBattleEffectTarget : uint8
{
	User = 0,
	ResolvedTarget = 1,
	AllResolvedTargets = 2,
	UserSide = 3,
	TargetSide = 4,
	BothSides = 5,
	Field = 6,
	Invalid = 255
};

/** Move-level traits consumed by later legality, hit, and effect packages. */
enum class EBattleMoveFlags : uint32
{
	None = 0,
	MakesContact = 1U << 0,
	BlockedByProtect = 1U << 1,
	BypassesProtect = 1U << 2,
	BypassesSubstitute = 1U << 3,
	ThawsUser = 1U << 4,
	ThawsTarget = 1U << 5,
	Unencoreable = 1U << 6,
	AlwaysCritical = 1U << 7,
	NeverCritical = 1U << 8,
	UsesPerHitAccuracy = 1U << 9,
	/** Reserved for engine-owned damage definitions such as Struggle. */
	TypelessDamage = 1U << 10,
	/** Reaches a target in a Fly-style airborne semi-invulnerable state. */
	ReachesAirborneSemiInvulnerableTarget = 1U << 11,
	/** Doubles move power when the target is in a Fly-style airborne semi-invulnerable state. */
	DoublesPowerAgainstAirborneSemiInvulnerableTarget = 1U << 12,
	/** Passes Protect, then breaks its current shield at the post-accuracy checkpoint. */
	BreaksProtection = 1U << 13,
	/** Bypasses side-owned protection such as Safeguard and Mist. */
	BypassesSideProtection = 1U << 14,
	/** Receives the Grassy Terrain reduction used by affected Ground-type moves. */
	ReducedByGrassyTerrain = 1U << 15
};
ENUM_CLASS_FLAGS(EBattleMoveFlags);

/** Descriptor-local traits that alter how a reusable effect is applied. */
enum class EBattleMoveEffectFlags : uint32
{
	None = 0,
	BypassesSubstitute = 1U << 0,
	UsesActualDamage = 1U << 1,
	MinimumOne = 1U << 2,
	StopOnFaint = 1U << 3,
	PerHit = 1U << 4
};
ENUM_CLASS_FLAGS(EBattleMoveEffectFlags);

/** One ordered reusable effect copied into a frozen move definition. */
struct POKEMONSOLARUS_API FBattleMoveEffectDescriptor
{
	int32 Order = 0;
	EBattleMoveEffectKind Kind = EBattleMoveEffectKind::Invalid;
	EBattleEffectTarget Target = EBattleEffectTarget::Invalid;
	FConditionId ConditionId;
	FItemId ItemId;
	EBattleStat Stat = static_cast<EBattleStat>(255);
	int32 ChanceNumerator = 1;
	int32 ChanceDenominator = 1;
	int32 MagnitudeNumerator = 0;
	int32 MagnitudeDenominator = 1;
	int32 MinimumCount = 0;
	int32 MaximumCount = 0;
	int32 DurationTurns = 0;
	int32 LayerCount = 0;
	EBattleMoveEffectFlags Flags = EBattleMoveEffectFlags::None;
};

/** Immutable-by-catalog species/form facts copied at content-snapshot construction. */
struct POKEMONSOLARUS_API FBattleSpeciesFormDefinition
{
	FSpeciesFormId Id;
	EPokemonType PrimaryType = EPokemonType::Invalid;
	EPokemonType SecondaryType = EPokemonType::Invalid;
	FPokemonStatValues BaseStats;
	int32 CatchRate = 0;
	TArray<FAbilityId> AbilityChoices;
};

/** Authored nature identity paired with C01's frozen resolved modifier value. */
struct POKEMONSOLARUS_API FBattleNatureDefinition
{
	FNatureId Id;
	FNatureStatModifier Modifier;
};

/** Immutable-by-catalog move facts and their explicit ordered effect list. */
struct POKEMONSOLARUS_API FBattleMoveDefinition
{
	FMoveId Id;
	EPokemonType Type = EPokemonType::Invalid;
	EBattleMoveCategory Category = EBattleMoveCategory::Invalid;
	int32 Power = 0;
	bool bAlwaysHits = false;
	int32 Accuracy = 0;
	bool bUsesPP = true;
	int32 BasePP = 0;
	bool bAllowsPPBoosts = true;
	int32 Priority = 0;
	EBattleTargetClass TargetClass = static_cast<EBattleTargetClass>(255);
	EBattleMoveFlags Flags = EBattleMoveFlags::None;
	TArray<FBattleMoveEffectDescriptor> Effects;
};

/** Ability identity record; later packages add rule payloads without replacing the ID. */
struct POKEMONSOLARUS_API FBattleAbilityDefinition
{
	FAbilityId Id;
};

/** Item identity and family record; later packages add held/battle/capture payloads. */
struct POKEMONSOLARUS_API FBattleItemDefinition
{
	FItemId Id;
	EBattleItemKind Kind = EBattleItemKind::Invalid;
};

/** Condition identity and family record used by generic move effects. */
struct POKEMONSOLARUS_API FBattleConditionDefinition
{
	FConditionId Id;
	EBattleConditionKind Kind = EBattleConditionKind::Invalid;
};
