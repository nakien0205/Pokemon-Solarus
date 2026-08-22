#include "Battle/BattleFieldSideConditions.h"

#include "Battle/BattleMajorStatus.h"
#include "Math/NumericLimits.h"

namespace BattleFieldSideConditionsPrivate
{
	template <typename IdType>
	IdType MakeDefinitionId(const TCHAR* Name)
	{
		IdType Result;
		const bool bCreated = IdType::TryCreate(FName(Name), Result);
		check(bCreated);
		return Result;
	}

	bool HasType(
		const EPokemonType Primary,
		const EPokemonType Secondary,
		const EPokemonType Type)
	{
		return Primary == Type || Secondary == Type;
	}

	bool IsValidTypePair(const EPokemonType Primary, const EPokemonType Secondary)
	{
		return FBattleTypeChart::IsKnownType(Primary)
			&& (Secondary == EPokemonType::Invalid
				|| (FBattleTypeChart::IsKnownType(Secondary) && Secondary != Primary));
	}

	bool IsKnownMoveCategory(const EBattleMoveCategory Category)
	{
		return Category == EBattleMoveCategory::Physical
			|| Category == EBattleMoveCategory::Special
			|| Category == EBattleMoveCategory::Status;
	}

	bool IsWeatherKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::Sun
			&& Kind <= EBattleFieldSideConditionKind::Snow;
	}

	bool IsTerrainKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::ElectricTerrain
			&& Kind <= EBattleFieldSideConditionKind::PsychicTerrain;
	}

	bool IsHazardKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::Spikes
			&& Kind <= EBattleFieldSideConditionKind::StickyWeb;
	}

	bool IsScreenKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::Reflect
			&& Kind <= EBattleFieldSideConditionKind::AuroraVeil;
	}

	bool IsRoomKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::TrickRoom
			&& Kind <= EBattleFieldSideConditionKind::WonderRoom;
	}

	bool IsOrdinarySideKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::Tailwind
			&& Kind <= EBattleFieldSideConditionKind::Mist;
	}

	bool IsCanonicalKind(const EBattleFieldSideConditionKind Kind)
	{
		return Kind >= EBattleFieldSideConditionKind::Sun
			&& Kind <= EBattleFieldSideConditionKind::Mist;
	}

	const TCHAR* KindName(const EBattleFieldSideConditionKind Kind)
	{
		switch (Kind)
		{
		case EBattleFieldSideConditionKind::Sun: return TEXT("Sun");
		case EBattleFieldSideConditionKind::Rain: return TEXT("Rain");
		case EBattleFieldSideConditionKind::Sandstorm: return TEXT("Sandstorm");
		case EBattleFieldSideConditionKind::Snow: return TEXT("Snow");
		case EBattleFieldSideConditionKind::ElectricTerrain: return TEXT("ElectricTerrain");
		case EBattleFieldSideConditionKind::GrassyTerrain: return TEXT("GrassyTerrain");
		case EBattleFieldSideConditionKind::MistyTerrain: return TEXT("MistyTerrain");
		case EBattleFieldSideConditionKind::PsychicTerrain: return TEXT("PsychicTerrain");
		case EBattleFieldSideConditionKind::Spikes: return TEXT("Spikes");
		case EBattleFieldSideConditionKind::ToxicSpikes: return TEXT("ToxicSpikes");
		case EBattleFieldSideConditionKind::StealthRock: return TEXT("StealthRock");
		case EBattleFieldSideConditionKind::StickyWeb: return TEXT("StickyWeb");
		case EBattleFieldSideConditionKind::Reflect: return TEXT("Reflect");
		case EBattleFieldSideConditionKind::LightScreen: return TEXT("LightScreen");
		case EBattleFieldSideConditionKind::AuroraVeil: return TEXT("AuroraVeil");
		case EBattleFieldSideConditionKind::TrickRoom: return TEXT("TrickRoom");
		case EBattleFieldSideConditionKind::MagicRoom: return TEXT("MagicRoom");
		case EBattleFieldSideConditionKind::WonderRoom: return TEXT("WonderRoom");
		case EBattleFieldSideConditionKind::Tailwind: return TEXT("Tailwind");
		case EBattleFieldSideConditionKind::Safeguard: return TEXT("Safeguard");
		case EBattleFieldSideConditionKind::Mist: return TEXT("Mist");
		default: return TEXT("Invalid");
		}
	}

	const TCHAR* PhaseName(const EBattleTriggerPhase Phase)
	{
		switch (Phase)
		{
		case EBattleTriggerPhase::ActionOrderCalculation: return TEXT("ActionOrderCalculation");
		case EBattleTriggerPhase::BeforeAction: return TEXT("BeforeAction");
		case EBattleTriggerPhase::BeforeHit: return TEXT("BeforeHit");
		case EBattleTriggerPhase::BeforeDamage: return TEXT("BeforeDamage");
		case EBattleTriggerPhase::SwitchIn: return TEXT("SwitchIn");
		case EBattleTriggerPhase::EndTurn: return TEXT("EndTurn");
		default: return TEXT("Invalid");
		}
	}

	bool HasActiveEffectAtPhase(
		const EBattleFieldSideConditionKind Kind,
		const EBattleTriggerPhase Phase)
	{
		switch (Kind)
		{
		case EBattleFieldSideConditionKind::Sun:
			return Phase == EBattleTriggerPhase::BeforeHit
				|| Phase == EBattleTriggerPhase::BeforeDamage;
		case EBattleFieldSideConditionKind::Rain:
			return Phase == EBattleTriggerPhase::BeforeDamage;
		case EBattleFieldSideConditionKind::Sandstorm:
			return Phase == EBattleTriggerPhase::BeforeDamage
				|| Phase == EBattleTriggerPhase::EndTurn;
		case EBattleFieldSideConditionKind::Snow:
			return Phase == EBattleTriggerPhase::BeforeDamage;
		case EBattleFieldSideConditionKind::ElectricTerrain:
		case EBattleFieldSideConditionKind::MistyTerrain:
		case EBattleFieldSideConditionKind::PsychicTerrain:
			return Phase == EBattleTriggerPhase::BeforeHit
				|| Phase == EBattleTriggerPhase::BeforeDamage;
		case EBattleFieldSideConditionKind::GrassyTerrain:
			return Phase == EBattleTriggerPhase::BeforeDamage
				|| Phase == EBattleTriggerPhase::EndTurn;
		case EBattleFieldSideConditionKind::Spikes:
		case EBattleFieldSideConditionKind::ToxicSpikes:
		case EBattleFieldSideConditionKind::StealthRock:
		case EBattleFieldSideConditionKind::StickyWeb:
			return Phase == EBattleTriggerPhase::SwitchIn;
		case EBattleFieldSideConditionKind::Reflect:
		case EBattleFieldSideConditionKind::LightScreen:
		case EBattleFieldSideConditionKind::AuroraVeil:
		case EBattleFieldSideConditionKind::WonderRoom:
			return Phase == EBattleTriggerPhase::BeforeDamage;
		case EBattleFieldSideConditionKind::TrickRoom:
		case EBattleFieldSideConditionKind::Tailwind:
			return Phase == EBattleTriggerPhase::ActionOrderCalculation;
		case EBattleFieldSideConditionKind::MagicRoom:
			return Phase == EBattleTriggerPhase::BeforeAction;
		case EBattleFieldSideConditionKind::Safeguard:
		case EBattleFieldSideConditionKind::Mist:
			return Phase == EBattleTriggerPhase::BeforeHit;
		default:
			return false;
		}
	}

	FBattleTriggerEffectId MakeEffectId(
		const EBattleFieldSideConditionKind Kind,
		const EBattleTriggerPhase Phase)
	{
		return MakeDefinitionId<FBattleTriggerEffectId>(
			*FString::Printf(
				TEXT("Trigger.FieldSide.%s.%s"),
				KindName(Kind),
				PhaseName(Phase)));
	}

	FBattleTriggerEffectId MakeDurationEffectId(
		const EBattleFieldSideConditionKind Kind)
	{
		return MakeDefinitionId<FBattleTriggerEffectId>(
			*FString::Printf(
				TEXT("Trigger.FieldSide.%s.EndTurnDuration"),
				KindName(Kind)));
	}

	FBattleTriggerRegistrationSpec MakeTriggerSpec(
		const FBattleFieldSideTriggerRegistrationFacts& Facts,
		const EBattleFieldSideConditionKind Kind,
		const EBattleTriggerPhase Phase,
		const int32 Order,
		const int32 Suborder = 0)
	{
		FBattleTriggerRegistrationSpec Spec;
		Spec.Rule.Phase = Phase;
		Spec.Rule.EffectId = MakeEffectId(Kind, Phase);
		Spec.Rule.PayloadId = Facts.PayloadId;
		Spec.Rule.Order = Order;
		Spec.Rule.Suborder = Suborder;
		const bool bSourceCreated = FBattleTriggerSourceDefinition::TryCreateCondition(
			Facts.ConditionId,
			Spec.SourceDefinition);
		check(bSourceCreated);
		Spec.Owner = Facts.Owner;
		Spec.Source = Facts.Source;
		Spec.Targets = Facts.Targets;
		Spec.DurationOwner = Facts.Owner;
		Spec.Layers = Facts.Layers;
		Spec.Visibility = FBattleTriggerVisibility::CreateCoreOnly();
		Spec.CleanupPolicy = EBattleTriggerCleanupPolicy::OnBattleEnd
			| EBattleTriggerCleanupPolicy::OnRemoval;
		Spec.bSuppressed = Facts.bSuppressed;
		return Spec;
	}

	FBattleTriggerRegistrationSpec MakeDurationSpec(
		const FBattleFieldSideTriggerRegistrationFacts& Facts,
		const EBattleFieldSideConditionKind Kind)
	{
		FBattleTriggerRegistrationSpec Spec = MakeTriggerSpec(
			Facts,
			Kind,
			EBattleTriggerPhase::EndTurn,
			TNumericLimits<int32>::Max());
		Spec.Rule.EffectId = MakeDurationEffectId(Kind);
		Spec.Rule.bDecrementDurationBeforeEffect = true;
		Spec.RemainingTurns = Facts.RemainingTurns;
		return Spec;
	}

	bool IsValidOwner(
		const EBattleFieldSideConditionKind Kind,
		const FBattleTriggerSubject& Owner)
	{
		const bool bFieldOwned = IsWeatherKind(Kind)
			|| IsTerrainKind(Kind)
			|| IsRoomKind(Kind);
		return Owner.IsValid()
			&& (bFieldOwned
				? Owner.Kind == EBattleTriggerSubjectKind::Field
				: Owner.Kind == EBattleTriggerSubjectKind::Side);
	}

	bool IsValidRegistrationDuration(
		const FConditionId& ConditionId,
		const TOptional<int32>& RemainingTurns)
	{
		TOptional<int32> OrdinaryDuration;
		if (!FBattleFieldSideConditionRules::TryGetDuration(
			ConditionId,
			false,
			OrdinaryDuration))
		{
			return false;
		}
		if (!OrdinaryDuration.IsSet())
		{
			return !RemainingTurns.IsSet();
		}
		if (!RemainingTurns.IsSet())
		{
			return false;
		}
		if (RemainingTurns.GetValue() == OrdinaryDuration.GetValue())
		{
			return true;
		}
		TOptional<int32> ExtendedDuration;
		return FBattleFieldSideConditionRules::SupportsDurationExtension(ConditionId)
			&& FBattleFieldSideConditionRules::TryGetDuration(
				ConditionId,
				true,
				ExtendedDuration)
			&& ExtendedDuration.IsSet()
			&& RemainingTurns.GetValue() == ExtendedDuration.GetValue();
	}

	bool IsSameSubject(
		const FBattleTriggerSubject& Left,
		const FBattleTriggerSubject& Right)
	{
		return Left == Right;
	}
}

FConditionId FBattleFieldSideConditionRules::GetSunId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Sun"));
}

FConditionId FBattleFieldSideConditionRules::GetRainId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Rain"));
}

FConditionId FBattleFieldSideConditionRules::GetSandstormId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Sandstorm"));
}

FConditionId FBattleFieldSideConditionRules::GetSnowId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Snow"));
}

FConditionId FBattleFieldSideConditionRules::GetElectricTerrainId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.ElectricTerrain"));
}

FConditionId FBattleFieldSideConditionRules::GetGrassyTerrainId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.GrassyTerrain"));
}

FConditionId FBattleFieldSideConditionRules::GetMistyTerrainId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.MistyTerrain"));
}

FConditionId FBattleFieldSideConditionRules::GetPsychicTerrainId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.PsychicTerrain"));
}

FConditionId FBattleFieldSideConditionRules::GetSpikesId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Spikes"));
}

FConditionId FBattleFieldSideConditionRules::GetToxicSpikesId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.ToxicSpikes"));
}

FConditionId FBattleFieldSideConditionRules::GetStealthRockId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.StealthRock"));
}

FConditionId FBattleFieldSideConditionRules::GetStickyWebId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.StickyWeb"));
}

FConditionId FBattleFieldSideConditionRules::GetReflectId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Reflect"));
}

FConditionId FBattleFieldSideConditionRules::GetLightScreenId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.LightScreen"));
}

FConditionId FBattleFieldSideConditionRules::GetAuroraVeilId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.AuroraVeil"));
}

FConditionId FBattleFieldSideConditionRules::GetTrickRoomId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.TrickRoom"));
}

FConditionId FBattleFieldSideConditionRules::GetMagicRoomId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.MagicRoom"));
}

FConditionId FBattleFieldSideConditionRules::GetWonderRoomId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.WonderRoom"));
}

FConditionId FBattleFieldSideConditionRules::GetTailwindId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Tailwind"));
}

FConditionId FBattleFieldSideConditionRules::GetSafeguardId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Safeguard"));
}

FConditionId FBattleFieldSideConditionRules::GetMistId()
{
	return BattleFieldSideConditionsPrivate::MakeDefinitionId<FConditionId>(TEXT("Condition.Mist"));
}

TArray<FConditionId> FBattleFieldSideConditionRules::GetCanonicalIds()
{
	return {
		GetSunId(),
		GetRainId(),
		GetSandstormId(),
		GetSnowId(),
		GetElectricTerrainId(),
		GetGrassyTerrainId(),
		GetMistyTerrainId(),
		GetPsychicTerrainId(),
		GetSpikesId(),
		GetToxicSpikesId(),
		GetStealthRockId(),
		GetStickyWebId(),
		GetReflectId(),
		GetLightScreenId(),
		GetAuroraVeilId(),
		GetTrickRoomId(),
		GetMagicRoomId(),
		GetWonderRoomId(),
		GetTailwindId(),
		GetSafeguardId(),
		GetMistId()
	};
}

EBattleFieldSideConditionKind FBattleFieldSideConditionRules::GetKind(
	const FConditionId& ConditionId)
{
	if (!ConditionId.IsValid()) return EBattleFieldSideConditionKind::None;
	if (ConditionId == GetSunId()) return EBattleFieldSideConditionKind::Sun;
	if (ConditionId == GetRainId()) return EBattleFieldSideConditionKind::Rain;
	if (ConditionId == GetSandstormId()) return EBattleFieldSideConditionKind::Sandstorm;
	if (ConditionId == GetSnowId()) return EBattleFieldSideConditionKind::Snow;
	if (ConditionId == GetElectricTerrainId()) return EBattleFieldSideConditionKind::ElectricTerrain;
	if (ConditionId == GetGrassyTerrainId()) return EBattleFieldSideConditionKind::GrassyTerrain;
	if (ConditionId == GetMistyTerrainId()) return EBattleFieldSideConditionKind::MistyTerrain;
	if (ConditionId == GetPsychicTerrainId()) return EBattleFieldSideConditionKind::PsychicTerrain;
	if (ConditionId == GetSpikesId()) return EBattleFieldSideConditionKind::Spikes;
	if (ConditionId == GetToxicSpikesId()) return EBattleFieldSideConditionKind::ToxicSpikes;
	if (ConditionId == GetStealthRockId()) return EBattleFieldSideConditionKind::StealthRock;
	if (ConditionId == GetStickyWebId()) return EBattleFieldSideConditionKind::StickyWeb;
	if (ConditionId == GetReflectId()) return EBattleFieldSideConditionKind::Reflect;
	if (ConditionId == GetLightScreenId()) return EBattleFieldSideConditionKind::LightScreen;
	if (ConditionId == GetAuroraVeilId()) return EBattleFieldSideConditionKind::AuroraVeil;
	if (ConditionId == GetTrickRoomId()) return EBattleFieldSideConditionKind::TrickRoom;
	if (ConditionId == GetMagicRoomId()) return EBattleFieldSideConditionKind::MagicRoom;
	if (ConditionId == GetWonderRoomId()) return EBattleFieldSideConditionKind::WonderRoom;
	if (ConditionId == GetTailwindId()) return EBattleFieldSideConditionKind::Tailwind;
	if (ConditionId == GetSafeguardId()) return EBattleFieldSideConditionKind::Safeguard;
	if (ConditionId == GetMistId()) return EBattleFieldSideConditionKind::Mist;
	return EBattleFieldSideConditionKind::Invalid;
}

EBattleConditionKind FBattleFieldSideConditionRules::GetConditionFamily(
	const FConditionId& ConditionId)
{
	const EBattleFieldSideConditionKind Kind = GetKind(ConditionId);
	if (BattleFieldSideConditionsPrivate::IsWeatherKind(Kind)) return EBattleConditionKind::Weather;
	if (BattleFieldSideConditionsPrivate::IsTerrainKind(Kind)) return EBattleConditionKind::Terrain;
	if (BattleFieldSideConditionsPrivate::IsHazardKind(Kind)) return EBattleConditionKind::Hazard;
	if (BattleFieldSideConditionsPrivate::IsScreenKind(Kind)) return EBattleConditionKind::Screen;
	if (BattleFieldSideConditionsPrivate::IsRoomKind(Kind)) return EBattleConditionKind::Room;
	if (BattleFieldSideConditionsPrivate::IsOrdinarySideKind(Kind))
	{
		return EBattleConditionKind::SideCondition;
	}
	return EBattleConditionKind::Invalid;
}

bool FBattleFieldSideConditionRules::IsCanonical(const FConditionId& ConditionId)
{
	return BattleFieldSideConditionsPrivate::IsCanonicalKind(GetKind(ConditionId));
}

bool FBattleFieldSideConditionRules::IsFieldOwned(const FConditionId& ConditionId)
{
	const EBattleConditionKind Family = GetConditionFamily(ConditionId);
	return Family == EBattleConditionKind::Weather
		|| Family == EBattleConditionKind::Terrain
		|| Family == EBattleConditionKind::Room;
}

bool FBattleFieldSideConditionRules::IsSideOwned(const FConditionId& ConditionId)
{
	const EBattleConditionKind Family = GetConditionFamily(ConditionId);
	return Family == EBattleConditionKind::Hazard
		|| Family == EBattleConditionKind::Screen
		|| Family == EBattleConditionKind::SideCondition;
}

bool FBattleFieldSideConditionRules::SupportsDurationExtension(
	const FConditionId& ConditionId)
{
	const EBattleFieldSideConditionKind Kind = GetKind(ConditionId);
	return BattleFieldSideConditionsPrivate::IsWeatherKind(Kind)
		|| BattleFieldSideConditionsPrivate::IsTerrainKind(Kind);
}

bool FBattleFieldSideConditionRules::TryGetDuration(
	const FConditionId& ConditionId,
	const bool bDurationExtensionActive,
	TOptional<int32>& OutDurationTurns)
{
	OutDurationTurns.Reset();
	const EBattleFieldSideConditionKind Kind = GetKind(ConditionId);
	if (!BattleFieldSideConditionsPrivate::IsCanonicalKind(Kind)
		|| (bDurationExtensionActive && !SupportsDurationExtension(ConditionId)))
	{
		return false;
	}
	if (BattleFieldSideConditionsPrivate::IsHazardKind(Kind))
	{
		return true;
	}
	if (Kind == EBattleFieldSideConditionKind::Tailwind)
	{
		OutDurationTurns = 4;
		return true;
	}
	OutDurationTurns = bDurationExtensionActive ? 8 : 5;
	return true;
}

bool FBattleFieldSideConditionRules::TryGetMaximumLayers(
	const FConditionId& ConditionId,
	int32& OutMaximumLayers)
{
	OutMaximumLayers = 0;
	switch (GetKind(ConditionId))
	{
	case EBattleFieldSideConditionKind::Spikes:
		OutMaximumLayers = 3;
		return true;
	case EBattleFieldSideConditionKind::ToxicSpikes:
		OutMaximumLayers = 2;
		return true;
	case EBattleFieldSideConditionKind::StealthRock:
	case EBattleFieldSideConditionKind::StickyWeb:
		OutMaximumLayers = 1;
		return true;
	default:
		if (IsCanonical(ConditionId))
		{
			OutMaximumLayers = 1;
			return true;
		}
		return false;
	}
}

bool FBattleFieldSideConditionRules::TryEvaluateApplication(
	const FBattleFieldSideApplicationFacts& Facts,
	FBattleFieldSideApplicationResult& OutResult)
{
	OutResult = FBattleFieldSideApplicationResult();
	const EBattleFieldSideConditionKind Kind = GetKind(Facts.RequestedConditionId);
	if (!BattleFieldSideConditionsPrivate::IsCanonicalKind(Kind)
		|| Facts.ExistingLayers < 0
		|| (Facts.bDurationExtensionActive
			&& !SupportsDurationExtension(Facts.RequestedConditionId)))
	{
		return false;
	}

	int32 MaximumLayers = 0;
	TOptional<int32> DurationTurns;
	if (!TryGetMaximumLayers(Facts.RequestedConditionId, MaximumLayers)
		|| !TryGetDuration(
			Facts.RequestedConditionId,
			Facts.bDurationExtensionActive,
			DurationTurns))
	{
		return false;
	}

	const bool bExclusive = BattleFieldSideConditionsPrivate::IsWeatherKind(Kind)
		|| BattleFieldSideConditionsPrivate::IsTerrainKind(Kind);
	const bool bHazard = BattleFieldSideConditionsPrivate::IsHazardKind(Kind);
	if (bExclusive)
	{
		if (Facts.ExistingLayers != 0)
		{
			return false;
		}
		if (Facts.ExistingExclusiveConditionId.IsSet())
		{
			const FConditionId& ExistingId = Facts.ExistingExclusiveConditionId.GetValue();
			if (!ExistingId.IsValid()
				|| (IsCanonical(ExistingId)
					&& GetConditionFamily(ExistingId)
						!= GetConditionFamily(Facts.RequestedConditionId)))
			{
				return false;
			}
		}
	}
	else if (Facts.ExistingExclusiveConditionId.IsSet())
	{
		return false;
	}
	if (!bHazard && Facts.ExistingLayers != 0)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.Kind = Kind;
	OutResult.MaximumLayers = MaximumLayers;
	if (Kind == EBattleFieldSideConditionKind::AuroraVeil && !Facts.bSnowActive)
	{
		OutResult.Outcome = EBattleFieldSideApplicationOutcome::ActivationRequirementFailed;
		return true;
	}

	if (bExclusive)
	{
		if (!Facts.ExistingExclusiveConditionId.IsSet())
		{
			OutResult.Outcome = EBattleFieldSideApplicationOutcome::Create;
			OutResult.DurationTurns = DurationTurns;
			OutResult.Layers = 1;
			return true;
		}
		if (Facts.ExistingExclusiveConditionId.GetValue() == Facts.RequestedConditionId)
		{
			OutResult.Outcome = EBattleFieldSideApplicationOutcome::AlreadyActive;
			return true;
		}
		OutResult.Outcome = EBattleFieldSideApplicationOutcome::ReplaceExclusive;
		OutResult.DurationTurns = DurationTurns;
		OutResult.Layers = 1;
		OutResult.bRemoveExistingExclusive = true;
		return true;
	}

	if (bHazard)
	{
		if (Facts.ExistingLayers > MaximumLayers)
		{
			return false;
		}
		if (Facts.ExistingLayers == 0)
		{
			OutResult.Outcome = EBattleFieldSideApplicationOutcome::Create;
			OutResult.Layers = 1;
		}
		else if (Facts.ExistingLayers < MaximumLayers)
		{
			OutResult.Outcome = EBattleFieldSideApplicationOutcome::AddLayer;
			OutResult.Layers = Facts.ExistingLayers + 1;
		}
		else
		{
			OutResult.Outcome = EBattleFieldSideApplicationOutcome::LayerCapReached;
			OutResult.Layers = Facts.ExistingLayers;
		}
		return true;
	}

	if (BattleFieldSideConditionsPrivate::IsRoomKind(Kind)
		&& Facts.bRequestedAlreadyActive)
	{
		OutResult.Outcome = EBattleFieldSideApplicationOutcome::ToggleOff;
		return true;
	}
	if (Facts.bRequestedAlreadyActive)
	{
		OutResult.Outcome = EBattleFieldSideApplicationOutcome::AlreadyActive;
		return true;
	}
	OutResult.Outcome = EBattleFieldSideApplicationOutcome::Create;
	OutResult.DurationTurns = DurationTurns;
	OutResult.Layers = 1;
	return true;
}

bool FBattleFieldSideConditionRules::TryResolveGrounded(
	const FBattleGroundedFacts& Facts,
	bool& bOutGrounded)
{
	bOutGrounded = false;
	if (!BattleFieldSideConditionsPrivate::IsValidTypePair(
		Facts.PrimaryType,
		Facts.SecondaryType))
	{
		return false;
	}
	bOutGrounded = !BattleFieldSideConditionsPrivate::HasType(
			Facts.PrimaryType,
			Facts.SecondaryType,
			EPokemonType::Flying)
		&& !(Facts.bAbilityMakesAirborne && !Facts.bAbilitySuppressed)
		&& !(Facts.bItemMakesAirborne && !Facts.bItemSuppressed)
		&& !Facts.bAirborneSemiInvulnerable;
	return true;
}

bool FBattleFieldSideConditionRules::TryGetWeatherDamageModifierQ12(
	const FConditionId& WeatherId,
	const EPokemonType MoveType,
	int32& OutModifierQ12)
{
	OutModifierQ12 = GetNeutralModifierQ12();
	const EBattleFieldSideConditionKind Kind = GetKind(WeatherId);
	if (!BattleFieldSideConditionsPrivate::IsWeatherKind(Kind)
		|| !FBattleTypeChart::IsKnownType(MoveType))
	{
		return false;
	}
	if (Kind == EBattleFieldSideConditionKind::Sun)
	{
		if (MoveType == EPokemonType::Fire) OutModifierQ12 = GetBoostedModifierQ12();
		else if (MoveType == EPokemonType::Water) OutModifierQ12 = GetWeakenedModifierQ12();
	}
	else if (Kind == EBattleFieldSideConditionKind::Rain)
	{
		if (MoveType == EPokemonType::Water) OutModifierQ12 = GetBoostedModifierQ12();
		else if (MoveType == EPokemonType::Fire) OutModifierQ12 = GetWeakenedModifierQ12();
	}
	return true;
}

bool FBattleFieldSideConditionRules::TryGetWeatherDirectDefensiveModifierQ12(
	const FConditionId& WeatherId,
	const EPokemonType DefenderPrimaryType,
	const EPokemonType DefenderSecondaryType,
	const EBattleMoveCategory MoveCategory,
	int32& OutModifierQ12)
{
	OutModifierQ12 = GetNeutralModifierQ12();
	const EBattleFieldSideConditionKind Kind = GetKind(WeatherId);
	if (!BattleFieldSideConditionsPrivate::IsWeatherKind(Kind)
		|| !BattleFieldSideConditionsPrivate::IsValidTypePair(
			DefenderPrimaryType,
			DefenderSecondaryType)
		|| !BattleFieldSideConditionsPrivate::IsKnownMoveCategory(MoveCategory))
	{
		return false;
	}
	if (Kind == EBattleFieldSideConditionKind::Sandstorm
		&& MoveCategory == EBattleMoveCategory::Special
		&& BattleFieldSideConditionsPrivate::HasType(
			DefenderPrimaryType,
			DefenderSecondaryType,
			EPokemonType::Rock))
	{
		OutModifierQ12 = GetBoostedModifierQ12();
	}
	else if (Kind == EBattleFieldSideConditionKind::Snow
		&& MoveCategory == EBattleMoveCategory::Physical
		&& BattleFieldSideConditionsPrivate::HasType(
			DefenderPrimaryType,
			DefenderSecondaryType,
			EPokemonType::Ice))
	{
		OutModifierQ12 = GetBoostedModifierQ12();
	}
	return true;
}

bool FBattleFieldSideConditionRules::ShouldSunPreventFreeze(
	const FConditionId& WeatherId)
{
	return WeatherId == GetSunId();
}

bool FBattleFieldSideConditionRules::TryGetTerrainPowerModifierQ12(
	const FConditionId& TerrainId,
	const EPokemonType MoveType,
	const bool bAttackerGrounded,
	int32& OutModifierQ12)
{
	OutModifierQ12 = GetNeutralModifierQ12();
	const EBattleFieldSideConditionKind Kind = GetKind(TerrainId);
	if (!BattleFieldSideConditionsPrivate::IsTerrainKind(Kind)
		|| !FBattleTypeChart::IsKnownType(MoveType))
	{
		return false;
	}
	if (!bAttackerGrounded)
	{
		return true;
	}
	const bool bBoosted = (Kind == EBattleFieldSideConditionKind::ElectricTerrain
			&& MoveType == EPokemonType::Electric)
		|| (Kind == EBattleFieldSideConditionKind::GrassyTerrain
			&& MoveType == EPokemonType::Grass)
		|| (Kind == EBattleFieldSideConditionKind::PsychicTerrain
			&& MoveType == EPokemonType::Psychic);
	if (bBoosted)
	{
		OutModifierQ12 = GetTerrainBoostModifierQ12();
	}
	return true;
}

bool FBattleFieldSideConditionRules::TryGetTerrainFinalDamageModifierQ12(
	const FConditionId& TerrainId,
	const EPokemonType MoveType,
	const bool bDefenderGrounded,
	const bool bMoveAffectedByGrassyTerrain,
	int32& OutModifierQ12)
{
	OutModifierQ12 = GetNeutralModifierQ12();
	const EBattleFieldSideConditionKind Kind = GetKind(TerrainId);
	if (!BattleFieldSideConditionsPrivate::IsTerrainKind(Kind)
		|| !FBattleTypeChart::IsKnownType(MoveType))
	{
		return false;
	}
	if (!bDefenderGrounded)
	{
		return true;
	}
	if ((Kind == EBattleFieldSideConditionKind::GrassyTerrain
			&& bMoveAffectedByGrassyTerrain)
		|| (Kind == EBattleFieldSideConditionKind::MistyTerrain
			&& MoveType == EPokemonType::Dragon))
	{
		OutModifierQ12 = GetWeakenedModifierQ12();
	}
	return true;
}

bool FBattleFieldSideConditionRules::ShouldTerrainPreventMajorStatus(
	const FConditionId& TerrainId,
	const FConditionId& RequestedStatusId,
	const bool bTargetGrounded)
{
	if (!bTargetGrounded || !RequestedStatusId.IsValid())
	{
		return false;
	}
	return TerrainId == GetMistyTerrainId()
		|| (TerrainId == GetElectricTerrainId()
			&& RequestedStatusId == FBattleMajorStatusRules::GetSleepId());
}

bool FBattleFieldSideConditionRules::ShouldTerrainPreventConfusion(
	const FConditionId& TerrainId,
	const bool bTargetGrounded)
{
	return bTargetGrounded && TerrainId == GetMistyTerrainId();
}

bool FBattleFieldSideConditionRules::ShouldPsychicTerrainBlockMove(
	const FConditionId& TerrainId,
	const bool bAppliedByOpponent,
	const bool bTargetGrounded,
	const int32 ResolvedIntegerPriority,
	const int32 ResolvedFractionalPriorityTenths)
{
	if (TerrainId != GetPsychicTerrainId()
		|| !bAppliedByOpponent
		|| !bTargetGrounded
		|| ResolvedFractionalPriorityTenths < 0
		|| ResolvedFractionalPriorityTenths > 9)
	{
		return false;
	}
	const int64 ResolvedPriorityTenths = static_cast<int64>(ResolvedIntegerPriority) * 10
		+ ResolvedFractionalPriorityTenths;
	return ResolvedPriorityTenths > 1;
}

bool FBattleFieldSideConditionRules::TryResolveFieldResidual(
	const FBattleFieldResidualFacts& Facts,
	FBattleFieldResidualResult& OutResult)
{
	OutResult = FBattleFieldResidualResult();
	const EBattleFieldSideConditionKind Kind = GetKind(Facts.ConditionId);
	if ((!BattleFieldSideConditionsPrivate::IsWeatherKind(Kind)
			&& !BattleFieldSideConditionsPrivate::IsTerrainKind(Kind))
		|| Facts.BaseMaximumHP <= 0
		|| Facts.CurrentHP < 0
		|| Facts.CurrentHP > Facts.BaseMaximumHP
		|| !BattleFieldSideConditionsPrivate::IsValidTypePair(
			Facts.PrimaryType,
			Facts.SecondaryType))
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.EffectKind = EBattleFieldResidualEffectKind::None;
	if (Facts.CurrentHP == 0)
	{
		return true;
	}
	if (Kind == EBattleFieldSideConditionKind::Sandstorm)
	{
		const bool bTypeImmune = BattleFieldSideConditionsPrivate::HasType(
				Facts.PrimaryType,
				Facts.SecondaryType,
				EPokemonType::Rock)
			|| BattleFieldSideConditionsPrivate::HasType(
				Facts.PrimaryType,
				Facts.SecondaryType,
				EPokemonType::Ground)
			|| BattleFieldSideConditionsPrivate::HasType(
				Facts.PrimaryType,
				Facts.SecondaryType,
				EPokemonType::Steel);
		if (!bTypeImmune && !Facts.bIndirectDamagePrevented)
		{
			OutResult.EffectKind = EBattleFieldResidualEffectKind::Damage;
			OutResult.Amount = FMath::Min(
				Facts.CurrentHP,
				FMath::Max(1, Facts.BaseMaximumHP / 16));
		}
	}
	else if (Kind == EBattleFieldSideConditionKind::GrassyTerrain
		&& Facts.bGrounded
		&& Facts.CurrentHP < Facts.BaseMaximumHP)
	{
		OutResult.EffectKind = EBattleFieldResidualEffectKind::Heal;
		OutResult.Amount = FMath::Min(
			Facts.BaseMaximumHP - Facts.CurrentHP,
			FMath::Max(1, Facts.BaseMaximumHP / 16));
	}
	return true;
}

bool FBattleFieldSideConditionRules::TryResolveHazardSwitchIn(
	const FBattleHazardSwitchInFacts& Facts,
	FBattleHazardSwitchInResult& OutResult)
{
	OutResult = FBattleHazardSwitchInResult();
	const EBattleFieldSideConditionKind Kind = GetKind(Facts.HazardId);
	int32 MaximumLayers = 0;
	if (!BattleFieldSideConditionsPrivate::IsHazardKind(Kind)
		|| !TryGetMaximumLayers(Facts.HazardId, MaximumLayers)
		|| Facts.Layers <= 0
		|| Facts.Layers > MaximumLayers
		|| Facts.BaseMaximumHP <= 0
		|| Facts.CurrentHP < 0
		|| Facts.CurrentHP > Facts.BaseMaximumHP
		|| !BattleFieldSideConditionsPrivate::IsValidTypePair(
			Facts.PrimaryType,
			Facts.SecondaryType)
		|| Facts.RockEffectiveness.Numerator < 0
		|| Facts.RockEffectiveness.Denominator <= 0)
	{
		return false;
	}

	OutResult.bValid = true;
	OutResult.EffectKind = EBattleHazardSwitchInEffectKind::None;
	if (Facts.bBypassesEntryHazards || Facts.CurrentHP == 0)
	{
		return true;
	}

	switch (Kind)
	{
	case EBattleFieldSideConditionKind::Spikes:
	{
		if (!Facts.bGrounded || Facts.bIndirectDamagePrevented)
		{
			return true;
		}
		const int32 Denominator = Facts.Layers == 1 ? 8 : Facts.Layers == 2 ? 6 : 4;
		OutResult.EffectKind = EBattleHazardSwitchInEffectKind::Damage;
		OutResult.Damage = FMath::Min(
			Facts.CurrentHP,
			FMath::Max(1, Facts.BaseMaximumHP / Denominator));
		return true;
	}
	case EBattleFieldSideConditionKind::ToxicSpikes:
	{
		if (!Facts.bGrounded)
		{
			return true;
		}
		if (BattleFieldSideConditionsPrivate::HasType(
			Facts.PrimaryType,
			Facts.SecondaryType,
			EPokemonType::Poison))
		{
			OutResult.EffectKind = EBattleHazardSwitchInEffectKind::RemoveHazard;
			OutResult.bRemoveHazard = true;
			return true;
		}
		if (BattleFieldSideConditionsPrivate::HasType(
				Facts.PrimaryType,
				Facts.SecondaryType,
				EPokemonType::Steel)
			|| Facts.bMajorStatusPrevented)
		{
			return true;
		}
		OutResult.EffectKind = EBattleHazardSwitchInEffectKind::ApplyMajorStatus;
		OutResult.MajorStatusId = Facts.Layers == 1
			? FBattleMajorStatusRules::GetPoisonId()
			: FBattleMajorStatusRules::GetToxicId();
		return true;
	}
	case EBattleFieldSideConditionKind::StealthRock:
	{
		if (Facts.bIndirectDamagePrevented || Facts.RockEffectiveness.IsImmune())
		{
			return true;
		}
		const int64 Denominator = static_cast<int64>(Facts.RockEffectiveness.Denominator) * 8;
		if (Denominator <= 0)
		{
			return false;
		}
		const int64 Product = static_cast<int64>(Facts.BaseMaximumHP)
			* Facts.RockEffectiveness.Numerator;
		const int64 RequestedDamage = FMath::Max<int64>(1, Product / Denominator);
		OutResult.EffectKind = EBattleHazardSwitchInEffectKind::Damage;
		OutResult.Damage = static_cast<int32>(FMath::Min<int64>(
			Facts.CurrentHP,
			RequestedDamage));
		return true;
	}
	case EBattleFieldSideConditionKind::StickyWeb:
		if (Facts.bGrounded && !Facts.bStatStageDropPrevented)
		{
			OutResult.EffectKind = EBattleHazardSwitchInEffectKind::ModifyStatStage;
			OutResult.Stat = EBattleStat::Speed;
			OutResult.StatStageDelta = -1;
		}
		return true;
	default:
		return false;
	}
}

bool FBattleFieldSideConditionRules::TryGetScreenDamageModifierQ12(
	const TConstArrayView<FConditionId> ActiveSideConditions,
	const EBattleMoveCategory MoveCategory,
	const bool bDoubles,
	const bool bCritical,
	const bool bBypassesScreens,
	int32& OutModifierQ12)
{
	OutModifierQ12 = GetNeutralModifierQ12();
	if (!BattleFieldSideConditionsPrivate::IsKnownMoveCategory(MoveCategory))
	{
		return false;
	}
	bool bReflect = false;
	bool bLightScreen = false;
	bool bAuroraVeil = false;
	for (const FConditionId& ConditionId : ActiveSideConditions)
	{
		if (!ConditionId.IsValid()
			|| (IsCanonical(ConditionId) && !IsSideOwned(ConditionId)))
		{
			return false;
		}
		bReflect |= ConditionId == GetReflectId();
		bLightScreen |= ConditionId == GetLightScreenId();
		bAuroraVeil |= ConditionId == GetAuroraVeilId();
	}
	if (MoveCategory == EBattleMoveCategory::Status || bCritical || bBypassesScreens)
	{
		return true;
	}
	const bool bCovered = bAuroraVeil
		|| (MoveCategory == EBattleMoveCategory::Physical && bReflect)
		|| (MoveCategory == EBattleMoveCategory::Special && bLightScreen);
	if (bCovered)
	{
		OutModifierQ12 = bDoubles
			? GetDoublesScreenModifierQ12()
			: GetWeakenedModifierQ12();
	}
	return true;
}

bool FBattleFieldSideConditionRules::TryApplyTailwindSpeed(
	const bool bTailwindActive,
	const int32 EffectiveSpeed,
	int32& OutEffectiveSpeed)
{
	OutEffectiveSpeed = 0;
	if (EffectiveSpeed < 0
		|| (bTailwindActive && EffectiveSpeed > TNumericLimits<int32>::Max() / 2))
	{
		return false;
	}
	OutEffectiveSpeed = bTailwindActive ? EffectiveSpeed * 2 : EffectiveSpeed;
	return true;
}

bool FBattleFieldSideConditionRules::ShouldSafeguardPrevent(
	const bool bSafeguardActive,
	const bool bAppliedByOpponent,
	const bool bBypassesSideProtection)
{
	return bSafeguardActive && bAppliedByOpponent && !bBypassesSideProtection;
}

bool FBattleFieldSideConditionRules::ShouldMistPreventStatDrop(
	const bool bMistActive,
	const bool bAppliedByOpponent,
	const bool bBypassesSideProtection,
	const int32 RequestedStageDelta)
{
	return RequestedStageDelta < 0
		&& bMistActive
		&& bAppliedByOpponent
		&& !bBypassesSideProtection;
}

bool FBattleFieldSideConditionRules::ShouldReverseSpeedOrder(
	const bool bTrickRoomActive)
{
	return bTrickRoomActive;
}

bool FBattleFieldSideConditionRules::ShouldSuppressHeldItemEffects(
	const bool bMagicRoomActive)
{
	return bMagicRoomActive;
}

EBattleStat FBattleFieldSideConditionRules::ResolveWonderRoomDefensiveStat(
	const bool bWonderRoomActive,
	const EBattleStat RequestedStat)
{
	if (!bWonderRoomActive)
	{
		return RequestedStat;
	}
	if (RequestedStat == EBattleStat::Defense)
	{
		return EBattleStat::SpecialDefense;
	}
	if (RequestedStat == EBattleStat::SpecialDefense)
	{
		return EBattleStat::Defense;
	}
	return RequestedStat;
}

bool FBattleFieldSideConditionRules::TryGetTriggerEffectId(
	const FConditionId& ConditionId,
	const EBattleTriggerPhase Phase,
	FBattleTriggerEffectId& OutEffectId)
{
	OutEffectId = FBattleTriggerEffectId();
	const EBattleFieldSideConditionKind Kind = GetKind(ConditionId);
	if (!BattleFieldSideConditionsPrivate::HasActiveEffectAtPhase(Kind, Phase))
	{
		return false;
	}
	OutEffectId = BattleFieldSideConditionsPrivate::MakeEffectId(Kind, Phase);
	return true;
}

bool FBattleFieldSideConditionRules::TryBuildTriggerRegistrationSpecs(
	const FBattleFieldSideTriggerRegistrationFacts& Facts,
	TArray<FBattleTriggerRegistrationSpec>& OutSpecs)
{
	OutSpecs.Reset();
	const EBattleFieldSideConditionKind Kind = GetKind(Facts.ConditionId);
	int32 MaximumLayers = 0;
	if (!BattleFieldSideConditionsPrivate::IsCanonicalKind(Kind)
		|| !Facts.PayloadId.IsValid()
		|| !BattleFieldSideConditionsPrivate::IsValidOwner(Kind, Facts.Owner)
		|| !Facts.Source.IsValid()
		|| !BattleFieldSideConditionsPrivate::IsValidRegistrationDuration(
			Facts.ConditionId,
			Facts.RemainingTurns)
		|| !TryGetMaximumLayers(Facts.ConditionId, MaximumLayers)
		|| Facts.Layers <= 0
		|| Facts.Layers > MaximumLayers)
	{
		return false;
	}
	for (const FBattleTriggerSubject& Target : Facts.Targets)
	{
		if (!Target.IsValid())
		{
			return false;
		}
	}

	auto Add = [&OutSpecs, &Facts, Kind](
		const EBattleTriggerPhase Phase,
		const int32 Order = 0,
		const int32 Suborder = 0) -> FBattleTriggerRegistrationSpec&
	{
		return OutSpecs.Add_GetRef(
			BattleFieldSideConditionsPrivate::MakeTriggerSpec(
				Facts,
				Kind,
				Phase,
				Order,
				Suborder));
	};

	switch (Kind)
	{
	case EBattleFieldSideConditionKind::Sun:
		Add(EBattleTriggerPhase::BeforeHit);
		Add(EBattleTriggerPhase::BeforeDamage);
		break;
	case EBattleFieldSideConditionKind::Rain:
		Add(EBattleTriggerPhase::BeforeDamage);
		break;
	case EBattleFieldSideConditionKind::Sandstorm:
		Add(EBattleTriggerPhase::BeforeDamage);
		Add(EBattleTriggerPhase::EndTurn, 1);
		break;
	case EBattleFieldSideConditionKind::Snow:
		Add(EBattleTriggerPhase::BeforeDamage);
		break;
	case EBattleFieldSideConditionKind::ElectricTerrain:
	case EBattleFieldSideConditionKind::MistyTerrain:
	case EBattleFieldSideConditionKind::PsychicTerrain:
		Add(EBattleTriggerPhase::BeforeHit);
		Add(EBattleTriggerPhase::BeforeDamage);
		break;
	case EBattleFieldSideConditionKind::GrassyTerrain:
		Add(EBattleTriggerPhase::BeforeDamage);
		Add(EBattleTriggerPhase::EndTurn, 5, 2);
		break;
	case EBattleFieldSideConditionKind::Spikes:
	case EBattleFieldSideConditionKind::ToxicSpikes:
	case EBattleFieldSideConditionKind::StealthRock:
	case EBattleFieldSideConditionKind::StickyWeb:
		Add(EBattleTriggerPhase::SwitchIn);
		break;
	case EBattleFieldSideConditionKind::Reflect:
	case EBattleFieldSideConditionKind::LightScreen:
	case EBattleFieldSideConditionKind::AuroraVeil:
	case EBattleFieldSideConditionKind::WonderRoom:
		Add(EBattleTriggerPhase::BeforeDamage);
		break;
	case EBattleFieldSideConditionKind::TrickRoom:
	case EBattleFieldSideConditionKind::Tailwind:
		Add(EBattleTriggerPhase::ActionOrderCalculation);
		break;
	case EBattleFieldSideConditionKind::MagicRoom:
		Add(EBattleTriggerPhase::BeforeAction);
		break;
	case EBattleFieldSideConditionKind::Safeguard:
	case EBattleFieldSideConditionKind::Mist:
		Add(EBattleTriggerPhase::BeforeHit);
		break;
	default:
		return false;
	}

	if (Facts.RemainingTurns.IsSet())
	{
		OutSpecs.Add(BattleFieldSideConditionsPrivate::MakeDurationSpec(Facts, Kind));
	}
	return true;
}

bool FBattleFieldSideConditionRules::TryRegisterTriggers(
	FBattleTriggerFramework& Framework,
	const FBattleFieldSideTriggerRegistrationFacts& Facts,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	TArray<FBattleTriggerRegistrationSpec> Specs;
	if (!TryBuildTriggerRegistrationSpecs(Facts, Specs))
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerFramework Staged = Framework;
	for (const FBattleTriggerRegistrationSpec& Spec : Specs)
	{
		FBattleTriggerRegistrationId RegistrationId;
		if (!Staged.TryRegister(Spec, RegistrationId, OutError))
		{
			return false;
		}
	}
	Framework = MoveTemp(Staged);
	return true;
}

bool FBattleFieldSideConditionRules::TryUpdateTriggerLayers(
	FBattleTriggerFramework& Framework,
	const FConditionId& ConditionId,
	const FBattleTriggerSubject& Owner,
	const int32 NewLayers,
	const FBattleTriggerOperationContext& Context,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	const EBattleFieldSideConditionKind Kind = GetKind(ConditionId);
	int32 MaximumLayers = 0;
	if (!BattleFieldSideConditionsPrivate::IsHazardKind(Kind)
		|| !BattleFieldSideConditionsPrivate::IsValidOwner(Kind, Owner)
		|| !TryGetMaximumLayers(ConditionId, MaximumLayers)
		|| NewLayers <= 0
		|| NewLayers > MaximumLayers)
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerSourceDefinition SourceDefinition;
	if (!FBattleTriggerSourceDefinition::TryCreateCondition(ConditionId, SourceDefinition))
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}

	FBattleTriggerFramework Staged = Framework;
	bool bFound = false;
	for (const FBattleTriggerRegistrationState& Registration :
		Staged.GetActiveRegistrations())
	{
		if (Registration.Spec.SourceDefinition == SourceDefinition
			&& BattleFieldSideConditionsPrivate::IsSameSubject(
				Registration.Spec.Owner,
				Owner))
		{
			bFound = true;
			if (!Staged.TryUpdateLayers(
				Registration.RegistrationId,
				NewLayers,
				Context,
				OutError))
			{
				return false;
			}
		}
	}
	if (!bFound)
	{
		OutError = EBattleTriggerError::RegistrationNotFound;
		return false;
	}
	Framework = MoveTemp(Staged);
	return true;
}

bool FBattleFieldSideConditionRules::TryCleanupTriggers(
	FBattleTriggerFramework& Framework,
	const FConditionId& ConditionId,
	const FBattleTriggerSubject& Owner,
	const EBattleTriggerCleanupReason Reason,
	const FBattleTriggerOperationContext& Context,
	EBattleTriggerError& OutError)
{
	OutError = EBattleTriggerError::None;
	const EBattleFieldSideConditionKind Kind = GetKind(ConditionId);
	if (!BattleFieldSideConditionsPrivate::IsCanonicalKind(Kind)
		|| !BattleFieldSideConditionsPrivate::IsValidOwner(Kind, Owner))
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerSourceDefinition SourceDefinition;
	if (!FBattleTriggerSourceDefinition::TryCreateCondition(ConditionId, SourceDefinition))
	{
		OutError = EBattleTriggerError::InvalidDefinition;
		return false;
	}
	FBattleTriggerCleanupRequest Request;
	Request.Reason = Reason;
	Request.AffectedOwners.Add(Owner);
	Request.SourceDefinitionFilter = SourceDefinition;
	Request.Context = Context;
	return Framework.TryApplyCleanup(Request, OutError);
}
