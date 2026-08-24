#include "Battle/BattleCapture.h"

#include "Math/NumericLimits.h"

#include <cmath>

namespace
{
	template <typename IdType>
	IdType MakeCaptureDefinitionId(const TCHAR* Value)
	{
		IdType Id;
		const bool bCreated = IdType::TryCreate(FName(Value), Id);
		check(bCreated);
		return Id;
	}

	bool IsKnownCaptureSpeciesClassification(
		const EBattleCaptureSpeciesClassification Classification)
	{
		return Classification == EBattleCaptureSpeciesClassification::Normal
			|| Classification == EBattleCaptureSpeciesClassification::UltraBeast;
	}

	bool IsKnownPendingDestination(const EBattlePendingCaptureDestination Destination)
	{
		return Destination == EBattlePendingCaptureDestination::Party
			|| Destination == EBattlePendingCaptureDestination::Storage;
	}

	bool IsKnownMajorStatus(const EBattleMajorStatusKind Status)
	{
		return Status >= EBattleMajorStatusKind::None
			&& Status <= EBattleMajorStatusKind::Toxic;
	}

	bool TryMultiply(const uint64 Left, const uint64 Right, uint64& OutProduct)
	{
		OutProduct = 0;
		if (Right != 0 && Left > TNumericLimits<uint64>::Max() / Right)
		{
			return false;
		}
		OutProduct = Left * Right;
		return true;
	}

	bool TryRoundQ12(const uint64 Value, const uint64 ModifierQ12, uint64& OutValue)
	{
		uint64 Product = 0;
		if (!TryMultiply(Value, ModifierQ12, Product)
			|| Product > TNumericLimits<uint64>::Max() - 2048ULL)
		{
			return false;
		}
		OutValue = (Product + 2048ULL) / FBattleCaptureCalculator::Q12Neutral;
		return true;
	}

	uint32 GetCaughtCountHPModifierQ12(const uint32 CaughtSpeciesCount)
	{
		if (CaughtSpeciesCount <= 30) return 1229;
		if (CaughtSpeciesCount <= 150) return 2048;
		if (CaughtSpeciesCount <= 300) return 2867;
		if (CaughtSpeciesCount <= 450) return 3277;
		if (CaughtSpeciesCount <= 600) return 3686;
		return 4096;
	}

	uint32 GetCriticalModifierQ12(const uint32 CaughtSpeciesCount)
	{
		if (CaughtSpeciesCount <= 150) return 2048;
		if (CaughtSpeciesCount <= 300) return 4096;
		if (CaughtSpeciesCount <= 450) return 6144;
		if (CaughtSpeciesCount <= 600) return 8192;
		return 10240;
	}

	uint32 GetStatusModifierQ12(const EBattleMajorStatusKind Status)
	{
		if (Status == EBattleMajorStatusKind::Sleep
			|| Status == EBattleMajorStatusKind::Freeze)
		{
			return 10240;
		}
		if (Status == EBattleMajorStatusKind::Poison
			|| Status == EBattleMajorStatusKind::Toxic
			|| Status == EBattleMajorStatusKind::Burn
			|| Status == EBattleMajorStatusKind::Paralysis)
		{
			return 6144;
		}
		return 4096;
	}

	uint32 GetBadgeModifierQ12(const uint8 BadgeCount, const int32 TargetLevel)
	{
		static constexpr int32 Thresholds[] = {25, 30, 35, 40, 45, 50, 55, 60, 100};
		float Modifier = 1.0F;
		for (int32 Index = BadgeCount; Index < UE_ARRAY_COUNT(Thresholds); ++Index)
		{
			if (Thresholds[Index] < TargetLevel)
			{
				Modifier *= 0.8F;
			}
		}
		return static_cast<uint32>(std::floor(Modifier * 4096.0F + 0.5F));
	}

	bool TryCalculateShakeThreshold(const uint64 IndicatorQ12, uint32& OutThreshold)
	{
		OutThreshold = 0;
		if (IndicatorQ12 == 0
			|| IndicatorQ12 >= FBattleCaptureCalculator::GuaranteedIndicatorQ12)
		{
			return false;
		}

		const double Indicator = static_cast<double>(IndicatorQ12)
			/ static_cast<double>(FBattleCaptureCalculator::Q12Neutral);
		const uint64 RatioQ12 = static_cast<uint64>(
			std::floor((255.0 / Indicator) * 4096.0 + 0.5));
		const double RootInput = static_cast<double>(RatioQ12) / 4096.0;
		const float RootInputSingle = static_cast<float>(RootInput);
		const float RootQ12Single =
			::powf(RootInputSingle, 3.0F / 16.0F) * 4096.0F + 0.5F;
		const uint64 RootQ12 = static_cast<uint64>(std::floor(RootQ12Single));
		if (RootQ12 == 0)
		{
			return false;
		}
		const double Root = static_cast<double>(RootQ12) / 4096.0;
		const uint64 Scaled = static_cast<uint64>(
			std::floor((65536.0 / Root) * 4096.0 + 0.5));
		const uint64 Threshold = Scaled / 4096ULL;
		if (Threshold > 65536ULL)
		{
			return false;
		}
		OutThreshold = static_cast<uint32>(Threshold);
		return true;
	}
}

bool FBattleCaptureProgressionSnapshot::IsValid() const
{
	if (!bHasSnapshot)
	{
		return BadgeCount == 0
			&& CaughtSpeciesCount == 0
			&& !bCriticalCaptureEnabled
			&& !bCatchingCharm
			&& !bUseCaughtCountHPComponentModifier
			&& CaptureCoefficientQ12 == 4096
			&& !bMustCapture
			&& !bUseUnsupportedLowPlayerLevelCoefficient;
	}
	return BadgeCount <= 8
		&& CaptureCoefficientQ12 > 0
		&& !bUseUnsupportedLowPlayerLevelCoefficient;
}

bool FBattleCaptureEventMetadata::IsValid() const
{
	if (CaughtCountHPModifierQ12 == 0
		|| BadgeModifierQ12 == 0
		|| StatusModifierQ12 == 0
		|| CaptureCoefficientQ12 == 0
		|| CriticalThreshold > 255
		|| ShakeThreshold > 65536
		|| RequiredShakeChecks > 4
		|| ShakeChecksPerformed > RequiredShakeChecks
		|| ShakeChecksPassed > ShakeChecksPerformed
		|| VisualShakeCount > 3
		|| (!bCriticalEligible
			&& (CriticalModifierQ12 != 0
				|| CriticalThreshold != 0
				|| bCriticalCapture))
		|| (bCriticalEligible && CriticalModifierQ12 == 0)
		|| (bMustCapture && (!bSucceeded
			|| CaptureIndicatorQ12 != 0
			|| bCriticalEligible
			|| bCriticalCapture
			|| bGuaranteedCapture
			|| CriticalModifierQ12 != 0
			|| CriticalThreshold != 0
			|| ShakeThreshold != 0
			|| RequiredShakeChecks != 0
			|| ShakeChecksPerformed != 0
			|| ShakeChecksPassed != 0
			|| VisualShakeCount != 3)))
	{
		return false;
	}
	if (!bMustCapture)
	{
		if (CaptureIndicatorQ12 == 0)
		{
			return false;
		}
		if (bGuaranteedCapture)
		{
			if (!bSucceeded
				|| ShakeThreshold != 0
				|| RequiredShakeChecks != 0
				|| ShakeChecksPerformed != 0
				|| ShakeChecksPassed != 0
				|| VisualShakeCount != (bCriticalCapture ? 1 : 3))
			{
				return false;
			}
		}
		else
		{
			const uint8 ExpectedChecks = bCriticalCapture ? 1 : 4;
			if (ShakeThreshold == 0
				|| RequiredShakeChecks != ExpectedChecks
				|| ShakeChecksPerformed == 0
				|| bSucceeded != (ShakeChecksPerformed == ExpectedChecks
					&& ShakeChecksPassed == ExpectedChecks)
				|| (!bSucceeded
					&& ShakeChecksPassed + 1 != ShakeChecksPerformed)
				|| VisualShakeCount != (bSucceeded
					? (bCriticalCapture ? 1 : 3)
					: FMath::Min<uint8>(ShakeChecksPassed, 3)))
			{
				return false;
			}
		}
	}
	if (bHasPendingDestination)
	{
		return bSucceeded
			&& PendingCaptureOrdinal > 0
			&& IsKnownPendingDestination(PendingDestination);
	}
	return PendingCaptureOrdinal == 0
		&& PendingDestination == EBattlePendingCaptureDestination::Invalid;
}

bool FBattlePendingCaptureRecord::IsValid() const
{
	if (CaptureOrdinal == 0
		|| !IsKnownPendingDestination(Destination)
		|| !OriginalTrainerId.IsValid()
		|| !BattlerId.IsValid()
		|| !SourcePokemonId.IsValid()
		|| !SpeciesFormId.IsValid()
		|| !IsKnownCaptureSpeciesClassification(SpeciesClassification)
		|| Level < 1 || Level > 100
		|| MaxHP <= 0
		|| CurrentHP <= 0 || CurrentHP > MaxHP
		|| Moves.Num() > 4
		|| (HeldItem.CurrentItemId.IsValid() && !HeldItem.OriginalItemId.IsValid())
		|| (!HeldItem.OriginalItemId.IsValid()
			&& (HeldItem.bConsumed
				|| HeldItem.bSuppressed
				|| HeldItem.bRevealed
				|| HeldItem.bTemporarilyRemoved
				|| HeldItem.ChoiceLockedMoveId.IsValid())))
	{
		return false;
	}

	for (int32 Index = 0; Index < Moves.Num(); ++Index)
	{
		const FBattleCapturedMoveFact& Move = Moves[Index];
		if (Move.SlotIndex >= 4
			|| !Move.MoveId.IsValid()
			|| Move.MaxPP <= 0
			|| Move.CurrentPP < 0
			|| Move.CurrentPP > Move.MaxPP
			|| (Index > 0 && Moves[Index - 1].SlotIndex >= Move.SlotIndex))
		{
			return false;
		}
		for (int32 PriorIndex = 0; PriorIndex < Index; ++PriorIndex)
		{
			if (Moves[PriorIndex].MoveId == Move.MoveId)
			{
				return false;
			}
		}
	}
	return true;
}

bool FBattleCaptureCalculator::IsInputValid(const FBattleCaptureCalculationInput& Input)
{
	return Input.BallItemId.IsValid()
		&& Input.BallMultiplierQ12 > 0
		&& Input.SpeciesClassification == EBattleCaptureSpeciesClassification::Normal
		&& Input.SpeciesCatchRate >= 1 && Input.SpeciesCatchRate <= 255
		&& Input.MaximumHP > 0
		&& Input.CurrentHP > 0 && Input.CurrentHP <= Input.MaximumHP
		&& Input.TargetLevel >= 1 && Input.TargetLevel <= 100
		&& Input.PlayerLevel >= 1 && Input.PlayerLevel <= 100
		&& IsKnownMajorStatus(Input.MajorStatus)
		&& Input.Progression.bHasSnapshot
		&& Input.Progression.IsValid()
		&& Input.RandomContext.IsValid()
		&& Input.RandomContext.ActionId.IsValid();
}

bool FBattleCaptureCalculator::TryResolve(
	const FBattleCaptureCalculationInput& Input,
	IBattleRandom& Random,
	FBattleCaptureCalculationResult& OutResult)
{
	OutResult = FBattleCaptureCalculationResult();
	if (!IsInputValid(Input))
	{
		return false;
	}

	OutResult.BadgeModifierQ12 = GetBadgeModifierQ12(
		Input.Progression.BadgeCount,
		Input.TargetLevel);
	OutResult.StatusModifierQ12 = GetStatusModifierQ12(Input.MajorStatus);
	OutResult.CaptureCoefficientQ12 = static_cast<uint32>(
		Input.Progression.CaptureCoefficientQ12);
	OutResult.bMustCapture = Input.Progression.bMustCapture;
	if (OutResult.bMustCapture)
	{
		OutResult.bValid = true;
		OutResult.bSucceeded = true;
		OutResult.VisualShakeCount = 3;
		return true;
	}

	const uint64 MaximumHP = static_cast<uint64>(Input.MaximumHP);
	const uint64 CurrentHP = static_cast<uint64>(Input.CurrentHP);
	const uint64 HPComponent = 3ULL * MaximumHP - 2ULL * CurrentHP;
	if (HPComponent > (TNumericLimits<uint64>::Max() >> 12U))
	{
		return false;
	}
	uint64 HPQ12 = HPComponent << 12U;
	if (Input.Progression.bUseCaughtCountHPComponentModifier)
	{
		OutResult.CaughtCountHPModifierQ12 = GetCaughtCountHPModifierQ12(
			Input.Progression.CaughtSpeciesCount);
		if (!TryRoundQ12(
				HPQ12,
				OutResult.CaughtCountHPModifierQ12,
				HPQ12))
		{
			return false;
		}
	}

	uint64 CatchRateHPQ12 = 0;
	uint64 AfterBall = 0;
	uint64 AfterBadge = 0;
	if (!TryMultiply(static_cast<uint64>(Input.SpeciesCatchRate), HPQ12, CatchRateHPQ12)
		|| !TryRoundQ12(
			CatchRateHPQ12,
			static_cast<uint64>(Input.BallMultiplierQ12),
			AfterBall)
		|| !TryRoundQ12(AfterBall, OutResult.BadgeModifierQ12, AfterBadge))
	{
		return false;
	}
	uint64 IndicatorQ12 = AfterBadge / (3ULL * MaximumHP);
	if (Input.TargetLevel <= 13)
	{
		uint64 LowLevelProduct = 0;
		if (!TryMultiply(
				static_cast<uint64>(36 - 2 * Input.TargetLevel),
				IndicatorQ12,
				LowLevelProduct))
		{
			return false;
		}
		IndicatorQ12 = LowLevelProduct / 10ULL;
	}
	if (!TryRoundQ12(IndicatorQ12, OutResult.StatusModifierQ12, IndicatorQ12)
		|| !TryRoundQ12(
			IndicatorQ12,
			static_cast<uint64>(Input.Progression.CaptureCoefficientQ12),
			IndicatorQ12)
		|| IndicatorQ12 == 0)
	{
		return false;
	}
	OutResult.CaptureIndicatorQ12 = IndicatorQ12;

	OutResult.bCriticalEligible = Input.Progression.bCriticalCaptureEnabled
		&& Input.Progression.CaughtSpeciesCount >= 31;
	if (OutResult.bCriticalEligible)
	{
		uint64 CriticalModifierQ12 = GetCriticalModifierQ12(
			Input.Progression.CaughtSpeciesCount);
		if (Input.Progression.bCatchingCharm)
		{
			CriticalModifierQ12 *= 2ULL;
		}
		OutResult.CriticalModifierQ12 = static_cast<uint32>(CriticalModifierQ12);
		uint64 CriticalQ12 = 0;
		if (!TryRoundQ12(
				FMath::Min(IndicatorQ12, GuaranteedIndicatorQ12),
				CriticalModifierQ12,
				CriticalQ12))
		{
			return false;
		}
		OutResult.CriticalThreshold = static_cast<uint32>(
			(CriticalQ12 / 6ULL) / Q12Neutral);
		FBattleRandomContext CriticalContext = Input.RandomContext;
		CriticalContext.RulePurpose = GetCriticalCapturePurpose();
		FBattleRandomDraw Draw;
		if (!Random.TryDrawUniform(0, 255, CriticalContext, Draw))
		{
			return false;
		}
		OutResult.CriticalDraw = Draw;
		OutResult.bCriticalCapture = Draw.Result < OutResult.CriticalThreshold;
	}

	OutResult.bGuaranteedCapture = IndicatorQ12 >= GuaranteedIndicatorQ12;
	if (OutResult.bGuaranteedCapture)
	{
		OutResult.bValid = true;
		OutResult.bSucceeded = true;
		OutResult.VisualShakeCount = OutResult.bCriticalCapture ? 1 : 3;
		return true;
	}

	if (!TryCalculateShakeThreshold(IndicatorQ12, OutResult.ShakeThreshold))
	{
		return false;
	}
	OutResult.RequiredShakeChecks = OutResult.bCriticalCapture ? 1 : 4;
	FBattleRandomContext ShakeContext = Input.RandomContext;
	ShakeContext.RulePurpose = GetShakeCheckPurpose();
	for (uint8 CheckIndex = 0; CheckIndex < OutResult.RequiredShakeChecks; ++CheckIndex)
	{
		FBattleRandomDraw Draw;
		if (!Random.TryDrawUniform(0, 65535, ShakeContext, Draw))
		{
			return false;
		}
		OutResult.ShakeDraws.Add(Draw);
		++OutResult.ShakeChecksPerformed;
		if (Draw.Result >= OutResult.ShakeThreshold)
		{
			OutResult.bValid = true;
			OutResult.VisualShakeCount = FMath::Min<uint8>(
				OutResult.ShakeChecksPassed,
				3);
			return true;
		}
		++OutResult.ShakeChecksPassed;
	}

	OutResult.bValid = true;
	OutResult.bSucceeded = true;
	OutResult.VisualShakeCount = OutResult.bCriticalCapture ? 1 : 3;
	return true;
}

FBattleCaptureEventMetadata FBattleCaptureCalculator::MakeEventMetadata(
	const FBattleCaptureCalculationResult& Result)
{
	FBattleCaptureEventMetadata Metadata;
	if (!Result.bValid)
	{
		return Metadata;
	}
	Metadata.CaptureIndicatorQ12 = Result.CaptureIndicatorQ12;
	Metadata.CaughtCountHPModifierQ12 = Result.CaughtCountHPModifierQ12;
	Metadata.BadgeModifierQ12 = Result.BadgeModifierQ12;
	Metadata.StatusModifierQ12 = Result.StatusModifierQ12;
	Metadata.CaptureCoefficientQ12 = Result.CaptureCoefficientQ12;
	Metadata.CriticalModifierQ12 = Result.CriticalModifierQ12;
	Metadata.CriticalThreshold = Result.CriticalThreshold;
	Metadata.ShakeThreshold = Result.ShakeThreshold;
	Metadata.bCriticalEligible = Result.bCriticalEligible;
	Metadata.bCriticalCapture = Result.bCriticalCapture;
	Metadata.bGuaranteedCapture = Result.bGuaranteedCapture;
	Metadata.bMustCapture = Result.bMustCapture;
	Metadata.bSucceeded = Result.bSucceeded;
	Metadata.RequiredShakeChecks = Result.RequiredShakeChecks;
	Metadata.ShakeChecksPerformed = Result.ShakeChecksPerformed;
	Metadata.ShakeChecksPassed = Result.ShakeChecksPassed;
	Metadata.VisualShakeCount = Result.VisualShakeCount;
	return Metadata;
}

FDefinitionId FBattleCaptureCalculator::GetCriticalCapturePurpose()
{
	return MakeCaptureDefinitionId<FDefinitionId>(TEXT("Rule.C09B.Capture.Critical"));
}

FDefinitionId FBattleCaptureCalculator::GetShakeCheckPurpose()
{
	return MakeCaptureDefinitionId<FDefinitionId>(TEXT("Rule.C09B.Capture.Shake"));
}
