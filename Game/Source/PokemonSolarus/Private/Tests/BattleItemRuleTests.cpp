#include "Misc/AutomationTest.h"

#include "Battle/BattleItem.h"
#include "BattleTestFactories.h"

#if WITH_DEV_AUTOMATION_TESTS

namespace BattleItemRuleTests
{
	using BattleTest::MakeDefinitionId;
	using BattleTest::MakeNumericId;

	FBattleTriggerSubject MakeBattlerSubject(const uint64 BattlerValue)
	{
		FBattleTriggerSubject Subject;
		const bool bCreated = FBattleTriggerSubject::TryCreateBattler(
			MakeNumericId<FBattlerId>(BattlerValue),
			Subject);
		check(bCreated);
		return Subject;
	}

	FBattleItemRegistrationFacts MakeRegistrationFacts(
		const FItemId& ItemId,
		const uint64 OwnerValue,
		const bool bSuppressed = false)
	{
		FBattleItemRegistrationFacts Facts;
		Facts.ItemId = ItemId;
		Facts.Owner = MakeBattlerSubject(OwnerValue);
		Facts.Source = Facts.Owner;
		Facts.Targets.Add(Facts.Owner);
		Facts.bSuppressed = bSuppressed;
		return Facts;
	}

	FBattleTriggerEffectRequest MakeTriggerRequest(
		const FItemId& ItemId,
		const FBattleAbilityItemHookDefinition& Definition,
		const uint64 NumericSeed)
	{
		FBattleTriggerEffectRequest Request;
		Request.RequestOrdinal = NumericSeed;
		Request.RegistrationId = MakeNumericId<FBattleTriggerRegistrationId>(NumericSeed);
		Request.Phase = Definition.TriggerRule.Phase;
		Request.EffectId = Definition.TriggerRule.EffectId;
		Request.PayloadId = Definition.TriggerRule.PayloadId;
		const bool bSourceCreated = FBattleTriggerSourceDefinition::TryCreateItem(
			ItemId,
			Request.SourceDefinition);
		check(bSourceCreated);
		Request.Owner = MakeBattlerSubject(1000 + NumericSeed);
		Request.Source = Request.Owner;
		Request.Targets.Add(Request.Owner);
		Request.DurationOwner = Request.Owner;
		Request.Layers = 1;
		Request.Visibility = FBattleTriggerVisibility::CreatePublic();
		Request.ReentrancyToken =
			MakeNumericId<FBattleTriggerReentrancyToken>(NumericSeed);
		return Request;
	}

	FBattleHeldItemInstanceState MakePersistentLedgerItem(
		const uint64 InstanceValue,
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const FItemId& ItemId)
	{
		FBattleHeldItemInstanceState State;
		State.InstanceId = MakeNumericId<FBattleHeldItemInstanceId>(InstanceValue);
		State.Origin = EBattleHeldItemOrigin::Persistent;
		State.DefinitionItemId = ItemId;
		State.OriginalOwnerTrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		State.OriginalOwnerBattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		State.OriginalItemId = ItemId;
		State.CurrentHolderTrainerId = State.OriginalOwnerTrainerId;
		State.CurrentHolderBattlerId = State.OriginalOwnerBattlerId;
		State.CurrentItemId = ItemId;
		return State;
	}

	FBattleHeldItemInstanceState MakeGeneratedLedgerItem(
		const uint64 InstanceValue,
		const uint64 TrainerValue,
		const uint64 BattlerValue,
		const FItemId& ItemId)
	{
		FBattleHeldItemInstanceState State;
		State.InstanceId = MakeNumericId<FBattleHeldItemInstanceId>(InstanceValue);
		State.Origin = EBattleHeldItemOrigin::BattleGenerated;
		State.DefinitionItemId = ItemId;
		State.CurrentHolderTrainerId = MakeNumericId<FTrainerId>(TrainerValue);
		State.CurrentHolderBattlerId = MakeNumericId<FBattlerId>(BattlerValue);
		State.CurrentItemId = ItemId;
		return State;
	}
}

namespace BattleItemRuleTests
{

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CHeldItemRuleHooksTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.CanonicalHooksRegistrationAndTypedRequests",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CHeldItemRuleHooksTest::RunTest(const FString& Parameters)
{
	const TArray<FItemId> ItemIds = FBattleItemRules::GetCanonicalIds();
	const TArray<EBattleHeldItemRuleKind> ExpectedKinds = {
		EBattleHeldItemRuleKind::Leftovers,
		EBattleHeldItemRuleKind::SitrusBerry,
		EBattleHeldItemRuleKind::LumBerry,
		EBattleHeldItemRuleKind::FocusSash,
		EBattleHeldItemRuleKind::LifeOrb,
		EBattleHeldItemRuleKind::ChoiceBand,
		EBattleHeldItemRuleKind::HeavyDutyBoots,
		EBattleHeldItemRuleKind::AirBalloon,
		EBattleHeldItemRuleKind::QuickClaw
	};
	const TArray<int32> ExpectedHookCounts = {1, 1, 1, 1, 2, 4, 1, 3, 1};

	TestEqual(TEXT("C08C owns exactly nine canonical held-item IDs"), ItemIds.Num(), 9);
	TestEqual(TEXT("Every canonical ID has an expected rule kind"),
		ExpectedKinds.Num(),
		ItemIds.Num());
	TestEqual(TEXT("Every canonical ID has an expected hook count"),
		ExpectedHookCounts.Num(),
		ItemIds.Num());
	TestEqual(TEXT("An empty item ID has no rule kind"),
		FBattleItemRules::GetKind(FItemId()),
		EBattleHeldItemRuleKind::None);
	TestEqual(TEXT("An unknown valid item ID has an invalid rule kind"),
		FBattleItemRules::GetKind(
			MakeDefinitionId<FItemId>(TEXT("Item.C08C.Unknown"))),
		EBattleHeldItemRuleKind::Invalid);

	TArray<FDefinitionId> SeenHookIds;
	uint64 RequestSeed = 1;
	for (int32 ItemIndex = 0; ItemIndex < ItemIds.Num(); ++ItemIndex)
	{
		if (!ExpectedKinds.IsValidIndex(ItemIndex)
			|| !ExpectedHookCounts.IsValidIndex(ItemIndex))
		{
			continue;
		}

		const FItemId& ItemId = ItemIds[ItemIndex];
		TestTrue(TEXT("Every listed held-item ID is valid and canonical"),
			ItemId.IsValid() && FBattleItemRules::IsCanonical(ItemId));
		TestEqual(TEXT("Canonical held-item order maps to the exact rule kind"),
			FBattleItemRules::GetKind(ItemId),
			ExpectedKinds[ItemIndex]);
		for (int32 RightIndex = ItemIndex + 1; RightIndex < ItemIds.Num(); ++RightIndex)
		{
			TestTrue(TEXT("Canonical held-item IDs are unique"),
				ItemId != ItemIds[RightIndex]);
		}

		TArray<FBattleAbilityItemHookDefinition> Definitions;
		const bool bBuilt = FBattleItemRules::TryBuildHookDefinitions(
			ItemId,
			Definitions);
		TestTrue(TEXT("Every canonical held item builds valid hook definitions"), bBuilt);
		if (!bBuilt)
		{
			continue;
		}
		TestEqual(TEXT("Each held item exposes only its authored hooks"),
			Definitions.Num(),
			ExpectedHookCounts[ItemIndex]);

		for (const FBattleAbilityItemHookDefinition& Definition : Definitions)
		{
			TestTrue(TEXT("Held-item hook IDs are globally unique"),
				!SeenHookIds.Contains(Definition.HookId));
			SeenHookIds.Add(Definition.HookId);
			TestEqual(TEXT("Hook payload retains the exact item definition"),
				Definition.TriggerRule.PayloadId,
				ItemId.GetDefinitionId());
			TestFalse(TEXT("C08C held-item hooks are not Mold Breaker breakable"),
				Definition.bBreakable);

			FBattleAbilityItemHookDefinition FoundDefinition;
			TestTrue(TEXT("Each hook is addressable by stable identity"),
				FBattleItemRules::TryGetHookDefinition(
					ItemId,
					Definition.HookId,
					FoundDefinition));
			TestEqual(TEXT("Stable hook lookup returns the exact hook point"),
				FoundDefinition.HookPoint,
				Definition.HookPoint);

			TArray<FBattleAbilityItemHookDefinition> PhaseDefinitions;
			TestTrue(TEXT("Each authored hook is discoverable in its trigger phase"),
				FBattleItemRules::TryGetHookDefinitionsForPhase(
					ItemId,
					Definition.TriggerRule.Phase,
					PhaseDefinitions));
			TestTrue(TEXT("Phase lookup retains the authored hook identity"),
				PhaseDefinitions.ContainsByPredicate(
					[&Definition](const FBattleAbilityItemHookDefinition& Candidate)
					{
						return Candidate.HookId == Definition.HookId;
					}));

			const FBattleTriggerEffectRequest TriggerRequest = MakeTriggerRequest(
				ItemId,
				Definition,
				RequestSeed++);
			FBattleAbilityItemEffectRequest TypedRequest;
			EBattleAbilityItemHookError HookError =
				EBattleAbilityItemHookError::InvalidDefinition;
			TestTrue(TEXT("Each authored item hook converts matching C07A work"),
				FBattleItemRules::TryCreateTypedEffectRequest(
					TriggerRequest,
					TypedRequest,
					HookError));
			TestEqual(TEXT("Typed item requests report no hook error"),
				HookError,
				EBattleAbilityItemHookError::None);
			TestEqual(TEXT("Typed item requests retain the semantic hook identity"),
				TypedRequest.HookId,
				Definition.HookId);
			TestEqual(TEXT("Typed item requests retain the semantic effect kind"),
				TypedRequest.EffectKind,
				Definition.EffectKind);
		}

		const bool bSuppressed = ItemId == FBattleItemRules::GetQuickClawId();
		const FBattleItemRegistrationFacts Facts = MakeRegistrationFacts(
			ItemId,
			100 + ItemIndex,
			bSuppressed);
		TArray<FBattleAbilityItemHookRegistrationFacts> HookFacts;
		EBattleAbilityItemHookError HookError =
			EBattleAbilityItemHookError::InvalidDefinition;
		TestTrue(TEXT("Every held item builds validated C08A registration facts"),
			FBattleItemRules::TryBuildHookRegistrationFacts(
				Facts,
				HookFacts,
				HookError));
		TestEqual(TEXT("Registration-fact construction reports no error"),
			HookError,
			EBattleAbilityItemHookError::None);
		TestEqual(TEXT("Every authored hook has one registration fact"),
			HookFacts.Num(),
			ExpectedHookCounts[ItemIndex]);

		FBattleTriggerFramework Framework;
		TestTrue(TEXT("Canonical item hooks register atomically through C08A and C07A"),
			FBattleItemRules::TryRegisterHooks(Framework, Facts, HookError));
		TestEqual(TEXT("Atomic item registration reports no error"),
			HookError,
			EBattleAbilityItemHookError::None);
		const TArray<FBattleTriggerRegistrationState> Registrations =
			Framework.GetActiveRegistrations();
		TestEqual(TEXT("Every authored item hook has one active registration"),
			Registrations.Num(),
			ExpectedHookCounts[ItemIndex]);
		for (int32 RegistrationIndex = 0;
			RegistrationIndex < Registrations.Num();
			++RegistrationIndex)
		{
			const FBattleTriggerRegistrationState& Registration =
				Registrations[RegistrationIndex];
			TestEqual(TEXT("Item registrations retain their source kind"),
				Registration.Spec.SourceDefinition.Kind,
				EBattleTriggerSourceDefinitionKind::Item);
			TestEqual(TEXT("Item registrations retain their exact identity"),
				Registration.Spec.SourceDefinition.ItemId,
				ItemId);
			TestEqual(TEXT("Item hook creation follows authored order"),
				Registration.CreationOrdinal,
				static_cast<uint64>(RegistrationIndex + 1));
			TestEqual(TEXT("Initial item suppression reaches each registration"),
				Registration.bSuppressed,
				bSuppressed);
			TestTrue(TEXT("Item switch cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnSwitch));
			TestTrue(TEXT("Item faint cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnFaint));
			TestTrue(TEXT("Item capture cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnCapture));
			TestTrue(TEXT("Item removal cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnRemoval));
			TestTrue(TEXT("Item battle-end cleanup is explicit"),
				EnumHasAnyFlags(
					Registration.Spec.CleanupPolicy,
					EBattleTriggerCleanupPolicy::OnBattleEnd));
		}
	}

	TestEqual(TEXT("The nine held items expose exactly fifteen semantic hooks"),
		SeenHookIds.Num(),
		15);

	auto CheckHook = [this](
		const FItemId& ItemId,
		const TCHAR* HookName,
		const EBattleAbilityItemHookPoint HookPoint,
		const EBattleAbilityItemEffectKind EffectKind,
		const EBattleTriggerPhase Phase,
		const EBattleAbilityItemRevealPolicy RevealPolicy,
		const int32 Order,
		const int32 Suborder,
		const bool bRepeatable)
	{
		FBattleAbilityItemHookDefinition Definition;
		const FDefinitionId HookId = MakeDefinitionId<FDefinitionId>(HookName);
		const bool bFound = FBattleItemRules::TryGetHookDefinition(
			ItemId,
			HookId,
			Definition);
		TestTrue(TEXT("The exact authored item hook exists"), bFound);
		if (!bFound)
		{
			return;
		}
		TestEqual(TEXT("The item hook point is exact"), Definition.HookPoint, HookPoint);
		TestEqual(TEXT("The item effect kind is exact"), Definition.EffectKind, EffectKind);
		TestEqual(TEXT("The item trigger phase is exact"), Definition.TriggerRule.Phase, Phase);
		TestEqual(TEXT("The item reveal policy is exact"), Definition.RevealPolicy, RevealPolicy);
		TestEqual(TEXT("The item trigger order is exact"), Definition.TriggerRule.Order, Order);
		TestEqual(TEXT("The item trigger suborder is exact"),
			Definition.TriggerRule.Suborder,
			Suborder);
		TestEqual(TEXT("The item trigger repeatability is exact"),
			Definition.TriggerRule.bRepeatable,
			bRepeatable);
	};

	using EHook = EBattleAbilityItemHookPoint;
	using EEffect = EBattleAbilityItemEffectKind;
	using EReveal = EBattleAbilityItemRevealPolicy;
	CheckHook(FBattleItemRules::GetLeftoversId(), TEXT("Hook.Item.Leftovers.EndTurn"),
		EHook::EndTurn, EEffect::Modify, EBattleTriggerPhase::EndTurn,
		EReveal::OnAppliedEffect, 5, 4, false);
	CheckHook(FBattleItemRules::GetSitrusBerryId(), TEXT("Hook.Item.SitrusBerry.ImmediateRecovery"),
		EHook::AfterDamage, EEffect::ConsumeItem, EBattleTriggerPhase::AfterDamage,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetLumBerryId(), TEXT("Hook.Item.LumBerry.ImmediateCure"),
		EHook::EffectApplication, EEffect::ConsumeItem, EBattleTriggerPhase::AfterHit,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetFocusSashId(), TEXT("Hook.Item.FocusSash.FaintPrevention"),
		EHook::FaintPrevention, EEffect::Prevent, EBattleTriggerPhase::BeforeDamage,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetLifeOrbId(), TEXT("Hook.Item.LifeOrb.FinalDamage"),
		EHook::FinalDamage, EEffect::Modify, EBattleTriggerPhase::BeforeDamage,
		EReveal::Never, 0, 0, true);
	CheckHook(FBattleItemRules::GetLifeOrbId(), TEXT("Hook.Item.LifeOrb.PostMoveRecoil"),
		EHook::AfterDamage, EEffect::Modify, EBattleTriggerPhase::AfterAction,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetChoiceBandId(), TEXT("Hook.Item.ChoiceBand.SelectionEligibility"),
		EHook::SelectionEligibility, EEffect::Prevent, EBattleTriggerPhase::SelectionEligibility,
		EReveal::Never, 0, 0, true);
	CheckHook(FBattleItemRules::GetChoiceBandId(), TEXT("Hook.Item.ChoiceBand.EstablishMoveLock"),
		EHook::ActionEligibility, EEffect::Modify, EBattleTriggerPhase::BeforeAction,
		EReveal::Never, 0, 0, true);
	CheckHook(FBattleItemRules::GetChoiceBandId(), TEXT("Hook.Item.ChoiceBand.PhysicalAttack"),
		EHook::OffensiveStat, EEffect::Modify, EBattleTriggerPhase::BeforeDamage,
		EReveal::Never, 0, 0, true);
	CheckHook(FBattleItemRules::GetChoiceBandId(), TEXT("Hook.Item.ChoiceBand.SwitchCleanup"),
		EHook::SwitchOut, EEffect::Modify, EBattleTriggerPhase::SwitchOut,
		EReveal::Never, 0, 0, false);
	CheckHook(FBattleItemRules::GetHeavyDutyBootsId(), TEXT("Hook.Item.HeavyDutyBoots.EntryHazards"),
		EHook::SwitchIn, EEffect::Prevent, EBattleTriggerPhase::SwitchIn,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetAirBalloonId(), TEXT("Hook.Item.AirBalloon.EntryReveal"),
		EHook::SwitchIn, EEffect::Reveal, EBattleTriggerPhase::SwitchIn,
		EReveal::OnAppliedEffect, 0, 0, false);
	CheckHook(FBattleItemRules::GetAirBalloonId(), TEXT("Hook.Item.AirBalloon.TypeImmunity"),
		EHook::TypeImmunity, EEffect::Prevent, EBattleTriggerPhase::BeforeHit,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetAirBalloonId(), TEXT("Hook.Item.AirBalloon.PopOnHit"),
		EHook::AfterDamage, EEffect::RemoveItem, EBattleTriggerPhase::AfterDamage,
		EReveal::OnAppliedEffect, 0, 0, true);
	CheckHook(FBattleItemRules::GetQuickClawId(), TEXT("Hook.Item.QuickClaw.ActionPriority"),
		EHook::ActionPriority, EEffect::Modify, EBattleTriggerPhase::ActionOrderCalculation,
		EReveal::OnAppliedEffect, 0, 0, true);

	TArray<FBattleAbilityItemHookDefinition> InvalidDefinitions;
	TestFalse(TEXT("An unknown item cannot build held-item hooks"),
		FBattleItemRules::TryBuildHookDefinitions(
			MakeDefinitionId<FItemId>(TEXT("Item.C08C.Unknown")),
			InvalidDefinitions));
	TestEqual(TEXT("Failed hook construction leaves no partial definitions"),
		InvalidDefinitions.Num(),
		0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CRecoveryRulesTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.LeftoversAndSitrusRecoveryBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CRecoveryRulesTest::RunTest(const FString& Parameters)
{
	auto Evaluate = [this](
		const FItemId& ItemId,
		const int32 CurrentHP,
		const int32 MaximumHP,
		const bool bHealingPermitted,
		const bool bSuppressed,
		const bool bExpectedApplies,
		const bool bExpectedConsumes,
		const int32 ExpectedHeal,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleItemRecoveryFacts Facts;
		Facts.ItemId = ItemId;
		Facts.CurrentHP = CurrentHP;
		Facts.BaseMaximumHP = MaximumHP;
		Facts.bHealingPermitted = bHealingPermitted;
		Facts.bSuppressed = bSuppressed;
		FBattleItemRecoveryResult Result;
		const bool bEvaluated = FBattleItemRules::TryEvaluateRecovery(Facts, Result);
		TestTrue(TEXT("Valid recovery facts are evaluated"), bEvaluated);
		if (!bEvaluated)
		{
			return;
		}
		TestTrue(TEXT("Recovery result is marked valid"), Result.bValid);
		TestEqual(TEXT("Recovery application is exact"), Result.bApplies, bExpectedApplies);
		TestEqual(TEXT("Recovery consumption is exact"),
			Result.bConsumesItem,
			bExpectedConsumes);
		TestEqual(TEXT("Recovery amount is exact"), Result.HealAmount, ExpectedHeal);
		TestEqual(TEXT("Recovery outcome is explicit"), Result.Outcome, ExpectedOutcome);
	};

	Evaluate(FBattleItemRules::GetLeftoversId(), 80, 160, true, false,
		true, false, 10, EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(FBattleItemRules::GetLeftoversId(), 1, 15, true, false,
		true, false, 1, EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(FBattleItemRules::GetLeftoversId(), 159, 160, true, false,
		true, false, 1, EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(FBattleItemRules::GetLeftoversId(), 160, 160, true, false,
		false, false, 0, EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(FBattleItemRules::GetLeftoversId(), 80, 160, false, false,
		false, false, 0, EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(FBattleItemRules::GetLeftoversId(), 80, 160, true, true,
		false, false, 0, EBattleAbilityItemActivationOutcome::Suppressed);

	Evaluate(FBattleItemRules::GetSitrusBerryId(), 50, 100, true, false,
		true, true, 25, EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(FBattleItemRules::GetSitrusBerryId(), 51, 100, true, false,
		false, false, 0, EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(FBattleItemRules::GetSitrusBerryId(), 1, 3, true, false,
		true, true, 1, EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(FBattleItemRules::GetSitrusBerryId(), 0, 100, true, false,
		false, false, 0, EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(FBattleItemRules::GetSitrusBerryId(), 50, 100, true, true,
		false, false, 0, EBattleAbilityItemActivationOutcome::Suppressed);

	FBattleItemRecoveryFacts InvalidFacts;
	InvalidFacts.ItemId = FBattleItemRules::GetLeftoversId();
	InvalidFacts.CurrentHP = 101;
	InvalidFacts.BaseMaximumHP = 100;
	FBattleItemRecoveryResult InvalidResult;
	TestFalse(TEXT("Impossible recovery HP facts are rejected"),
		FBattleItemRules::TryEvaluateRecovery(InvalidFacts, InvalidResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CLumBerryRuleTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.LumBerryAtomicCureAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CLumBerryRuleTest::RunTest(const FString& Parameters)
{
	auto Evaluate = [this](
		const bool bAbleToBattle,
		const bool bMajorStatus,
		const bool bConfusion,
		const bool bSuppressed,
		const bool bExpectedApplies,
		const bool bExpectedMajorCure,
		const bool bExpectedConfusionCure,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleLumBerryFacts Facts;
		Facts.ItemId = FBattleItemRules::GetLumBerryId();
		Facts.bHolderAbleToBattle = bAbleToBattle;
		Facts.bHasMajorStatus = bMajorStatus;
		Facts.bHasConfusion = bConfusion;
		Facts.bSuppressed = bSuppressed;
		FBattleLumBerryResult Result;
		TestTrue(TEXT("Valid Lum Berry facts are evaluated"),
			FBattleItemRules::TryEvaluateLumBerry(Facts, Result));
		TestTrue(TEXT("Lum Berry result is marked valid"), Result.bValid);
		TestEqual(TEXT("Lum Berry application is exact"),
			Result.bApplies,
			bExpectedApplies);
		TestEqual(TEXT("Lum Berry consumption matches application"),
			Result.bConsumesItem,
			bExpectedApplies);
		TestEqual(TEXT("Lum Berry major-status cure is exact"),
			Result.bCuresMajorStatus,
			bExpectedMajorCure);
		TestEqual(TEXT("Lum Berry Confusion cure is exact"),
			Result.bCuresConfusion,
			bExpectedConfusionCure);
		TestEqual(TEXT("Lum Berry outcome is explicit"), Result.Outcome, ExpectedOutcome);
	};

	Evaluate(true, true, false, false, true, true, false,
		EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(true, false, true, false, true, false, true,
		EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(true, true, true, false, true, true, true,
		EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(true, false, false, false, false, false, false,
		EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(false, true, true, false, false, false, false,
		EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(true, true, true, true, false, false, false,
		EBattleAbilityItemActivationOutcome::Suppressed);

	FBattleLumBerryFacts WrongItemFacts;
	WrongItemFacts.ItemId = FBattleItemRules::GetSitrusBerryId();
	WrongItemFacts.bHolderAbleToBattle = true;
	WrongItemFacts.bHasMajorStatus = true;
	FBattleLumBerryResult WrongItemResult;
	TestFalse(TEXT("A non-Lum item cannot use the Lum Berry rule"),
		FBattleItemRules::TryEvaluateLumBerry(WrongItemFacts, WrongItemResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CFocusSashRuleTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.FocusSashPerHitBoundariesAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CFocusSashRuleTest::RunTest(const FString& Parameters)
{
	auto Evaluate = [this](
		const int32 CurrentHP,
		const int32 MaximumHP,
		const int32 IncomingDamage,
		const bool bDirectMoveDamage,
		const bool bTargetsSubstitute,
		const bool bSuppressed,
		const bool bExpectedApplies,
		const int32 ExpectedDamage,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleFocusSashFacts Facts;
		Facts.ItemId = FBattleItemRules::GetFocusSashId();
		Facts.CurrentHP = CurrentHP;
		Facts.BaseMaximumHP = MaximumHP;
		Facts.IncomingDamage = IncomingDamage;
		Facts.bDirectMoveDamage = bDirectMoveDamage;
		Facts.bDamageTargetsSubstitute = bTargetsSubstitute;
		Facts.bSuppressed = bSuppressed;
		FBattleFocusSashResult Result;
		TestTrue(TEXT("Valid Focus Sash facts are evaluated"),
			FBattleItemRules::TryEvaluateFocusSash(Facts, Result));
		TestTrue(TEXT("Focus Sash result is marked valid"), Result.bValid);
		TestEqual(TEXT("Focus Sash application is exact"),
			Result.bApplies,
			bExpectedApplies);
		TestEqual(TEXT("Focus Sash consumption matches application"),
			Result.bConsumesItem,
			bExpectedApplies);
		TestEqual(TEXT("Focus Sash adjusted damage is exact"),
			Result.AdjustedDamage,
			ExpectedDamage);
		TestEqual(TEXT("Focus Sash outcome is explicit"), Result.Outcome, ExpectedOutcome);
	};

	Evaluate(100, 100, 100, true, false, false, true, 99,
		EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(100, 100, 150, true, false, false, true, 99,
		EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(1, 1, 1, true, false, false, true, 0,
		EBattleAbilityItemActivationOutcome::Applied);
	Evaluate(100, 100, 99, true, false, false, false, 99,
		EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(99, 100, 99, true, false, false, false, 99,
		EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(100, 100, 100, false, false, false, false, 100,
		EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(100, 100, 100, true, true, false, false, 100,
		EBattleAbilityItemActivationOutcome::Ineligible);
	Evaluate(100, 100, 100, true, false, true, false, 100,
		EBattleAbilityItemActivationOutcome::Suppressed);

	FBattleFocusSashFacts InvalidFacts;
	InvalidFacts.ItemId = FBattleItemRules::GetFocusSashId();
	InvalidFacts.CurrentHP = 100;
	InvalidFacts.BaseMaximumHP = 100;
	InvalidFacts.IncomingDamage = -1;
	FBattleFocusSashResult InvalidResult;
	TestFalse(TEXT("Negative incoming damage is rejected"),
		FBattleItemRules::TryEvaluateFocusSash(InvalidFacts, InvalidResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CLifeOrbRuleTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.LifeOrbModifierRecoilAndSuppression",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CLifeOrbRuleTest::RunTest(const FString& Parameters)
{
	auto EvaluateModifier = [this](
		const EBattleMoveCategory Category,
		const bool bDamagingMove,
		const bool bSuppressed,
		const bool bExpectedApplies,
		const int32 ExpectedModifier,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleItemDamageModifierFacts Facts;
		Facts.ItemId = FBattleItemRules::GetLifeOrbId();
		Facts.MoveCategory = Category;
		Facts.bDamagingMove = bDamagingMove;
		Facts.bSuppressed = bSuppressed;
		FBattleItemDamageModifierResult Result;
		TestTrue(TEXT("Valid Life Orb damage facts are evaluated"),
			FBattleItemRules::TryEvaluateDamageModifier(Facts, Result));
		TestEqual(TEXT("Life Orb application is exact"), Result.bApplies, bExpectedApplies);
		TestEqual(TEXT("Life Orb Q12 modifier is exact"),
			Result.ModifierQ12,
			ExpectedModifier);
		TestEqual(TEXT("Life Orb modifier outcome is explicit"),
			Result.Outcome,
			ExpectedOutcome);
	};

	EvaluateModifier(EBattleMoveCategory::Physical, true, false, true, 5324,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateModifier(EBattleMoveCategory::Special, true, false, true, 5324,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateModifier(EBattleMoveCategory::Status, false, false, false, 4096,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateModifier(EBattleMoveCategory::Physical, true, true, false, 4096,
		EBattleAbilityItemActivationOutcome::Suppressed);

	auto EvaluateRecoil = [this](
		const int32 MaximumHP,
		const bool bDamagingMove,
		const bool bAffectedTarget,
		const bool bDifferentTarget,
		const bool bForcedSwitch,
		const bool bSuppressed,
		const bool bExpectedApplies,
		const int32 ExpectedRecoil,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleLifeOrbRecoilFacts Facts;
		Facts.ItemId = FBattleItemRules::GetLifeOrbId();
		Facts.BaseMaximumHP = MaximumHP;
		Facts.bDamagingMove = bDamagingMove;
		Facts.bMoveAffectedTarget = bAffectedTarget;
		Facts.bSourceAndTargetDiffer = bDifferentTarget;
		Facts.bForcedSwitchSuppressesRecoil = bForcedSwitch;
		Facts.bSuppressed = bSuppressed;
		FBattleLifeOrbRecoilResult Result;
		TestTrue(TEXT("Valid Life Orb recoil facts are evaluated"),
			FBattleItemRules::TryEvaluateLifeOrbRecoil(Facts, Result));
		TestEqual(TEXT("Life Orb recoil application is exact"),
			Result.bApplies,
			bExpectedApplies);
		TestEqual(TEXT("Life Orb recoil amount is exact"),
			Result.RecoilDamage,
			ExpectedRecoil);
		TestEqual(TEXT("Life Orb recoil outcome is explicit"),
			Result.Outcome,
			ExpectedOutcome);
	};

	EvaluateRecoil(100, true, true, true, false, false, true, 10,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateRecoil(9, true, true, true, false, false, true, 1,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateRecoil(100, false, true, true, false, false, false, 0,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateRecoil(100, true, false, true, false, false, false, 0,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateRecoil(100, true, true, false, false, false, false, 0,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateRecoil(100, true, true, true, true, false, false, 0,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateRecoil(100, true, true, true, false, true, false, 0,
		EBattleAbilityItemActivationOutcome::Suppressed);

	FBattleItemDamageModifierFacts InvalidDamageFacts;
	InvalidDamageFacts.ItemId = FBattleItemRules::GetLifeOrbId();
	InvalidDamageFacts.MoveCategory = EBattleMoveCategory::Status;
	InvalidDamageFacts.bDamagingMove = true;
	FBattleItemDamageModifierResult InvalidDamageResult;
	TestFalse(TEXT("A damaging Status-category move is rejected"),
		FBattleItemRules::TryEvaluateDamageModifier(
			InvalidDamageFacts,
			InvalidDamageResult));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CChoiceBandRuleTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.ChoiceBandAttackLockStruggleAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CChoiceBandRuleTest::RunTest(const FString& Parameters)
{
	FBattleItemDamageModifierFacts ModifierFacts;
	ModifierFacts.ItemId = FBattleItemRules::GetChoiceBandId();
	ModifierFacts.MoveCategory = EBattleMoveCategory::Physical;
	ModifierFacts.bDamagingMove = true;
	FBattleItemDamageModifierResult ModifierResult;
	TestTrue(TEXT("Choice Band Physical damage facts are evaluated"),
		FBattleItemRules::TryEvaluateDamageModifier(ModifierFacts, ModifierResult));
	TestTrue(TEXT("Choice Band applies to Physical Attack"), ModifierResult.bApplies);
	TestEqual(TEXT("Choice Band uses an exact 1.5 Q12 modifier"),
		ModifierResult.ModifierQ12,
		6144);

	ModifierFacts.MoveCategory = EBattleMoveCategory::Special;
	TestTrue(TEXT("Choice Band Special damage facts are evaluated"),
		FBattleItemRules::TryEvaluateDamageModifier(ModifierFacts, ModifierResult));
	TestFalse(TEXT("Choice Band does not modify Special Attack"), ModifierResult.bApplies);
	TestEqual(TEXT("Choice Band Special damage remains neutral"),
		ModifierResult.ModifierQ12,
		4096);

	ModifierFacts.MoveCategory = EBattleMoveCategory::Physical;
	ModifierFacts.bSuppressed = true;
	TestTrue(TEXT("Suppressed Choice Band damage facts are evaluated"),
		FBattleItemRules::TryEvaluateDamageModifier(ModifierFacts, ModifierResult));
	TestFalse(TEXT("Suppressed Choice Band does not modify Attack"), ModifierResult.bApplies);
	TestEqual(TEXT("Suppressed Choice Band damage remains neutral"),
		ModifierResult.ModifierQ12,
		4096);
	TestEqual(TEXT("Suppressed Choice Band outcome is explicit"),
		ModifierResult.Outcome,
		EBattleAbilityItemActivationOutcome::Suppressed);

	const FMoveId FirstMove = MakeDefinitionId<FMoveId>(TEXT("Move.C08C.First"));
	const FMoveId SecondMove = MakeDefinitionId<FMoveId>(TEXT("Move.C08C.Second"));
	auto EvaluateMove = [this, &FirstMove, &SecondMove](
		const FMoveId& SelectedMove,
		const FMoveId& LockedMove,
		const bool bStruggle,
		const bool bSuppressed,
		const bool bExpectedAllowed,
		const bool bExpectedEstablish,
		const FMoveId& ExpectedLock,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleChoiceBandMoveFacts Facts;
		Facts.ItemId = FBattleItemRules::GetChoiceBandId();
		Facts.SelectedMoveId = SelectedMove;
		Facts.LockedMoveId = LockedMove;
		Facts.bSelectedMoveIsStruggle = bStruggle;
		Facts.bSuppressed = bSuppressed;
		FBattleChoiceBandMoveResult Result;
		TestTrue(TEXT("Valid Choice Band move facts are evaluated"),
			FBattleItemRules::TryEvaluateChoiceBandMove(Facts, Result));
		TestEqual(TEXT("Choice Band move legality is exact"),
			Result.bMoveAllowed,
			bExpectedAllowed);
		TestEqual(TEXT("Choice Band lock creation is exact"),
			Result.bShouldEstablishLock,
			bExpectedEstablish);
		TestEqual(TEXT("Choice Band retained lock is exact"),
			Result.LockMoveId,
			ExpectedLock);
		TestEqual(TEXT("Choice Band move outcome is explicit"),
			Result.Outcome,
			ExpectedOutcome);
	};

	EvaluateMove(FirstMove, FMoveId(), false, false, true, true, FirstMove,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateMove(FirstMove, FirstMove, false, false, true, false, FirstMove,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateMove(SecondMove, FirstMove, false, false, false, false, FirstMove,
		EBattleAbilityItemActivationOutcome::AttemptedButPrevented);
	EvaluateMove(SecondMove, FirstMove, true, false, true, false, FMoveId(),
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateMove(SecondMove, FirstMove, false, true, true, false, FMoveId(),
		EBattleAbilityItemActivationOutcome::Suppressed);

	TestFalse(TEXT("Choice Band lock persists without a cleanup cause"),
		FBattleItemRules::ShouldClearChoiceBandMoveLock(false, false, false));
	TestTrue(TEXT("Choice Band lock clears on switch"),
		FBattleItemRules::ShouldClearChoiceBandMoveLock(true, false, false));
	TestTrue(TEXT("Choice Band lock clears on item loss"),
		FBattleItemRules::ShouldClearChoiceBandMoveLock(false, true, false));
	TestTrue(TEXT("Choice Band lock clears on suppression"),
		FBattleItemRules::ShouldClearChoiceBandMoveLock(false, false, true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CHazardAndBalloonRulesTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.BootsAndAirBalloonBoundaries",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CHazardAndBalloonRulesTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("Active Heavy-Duty Boots bypass entry hazards"),
		FBattleItemRules::ShouldBypassEntryHazards(
			FBattleItemRules::GetHeavyDutyBootsId(),
			false));
	TestFalse(TEXT("Suppressed Heavy-Duty Boots do not bypass entry hazards"),
		FBattleItemRules::ShouldBypassEntryHazards(
			FBattleItemRules::GetHeavyDutyBootsId(),
			true));
	TestFalse(TEXT("A different item does not bypass entry hazards"),
		FBattleItemRules::ShouldBypassEntryHazards(
			FBattleItemRules::GetAirBalloonId(),
			false));

	TestTrue(TEXT("An active Air Balloon makes its holder airborne"),
		FBattleItemRules::IsAirBalloonAirborne(
			FBattleItemRules::GetAirBalloonId(),
			false));
	TestFalse(TEXT("A suppressed Air Balloon does not make its holder airborne"),
		FBattleItemRules::IsAirBalloonAirborne(
			FBattleItemRules::GetAirBalloonId(),
			true));
	TestTrue(TEXT("An active Air Balloon prevents Ground moves"),
		FBattleItemRules::ShouldAirBalloonPreventMove(
			FBattleItemRules::GetAirBalloonId(),
			EPokemonType::Ground,
			false));
	TestFalse(TEXT("An active Air Balloon does not prevent non-Ground moves"),
		FBattleItemRules::ShouldAirBalloonPreventMove(
			FBattleItemRules::GetAirBalloonId(),
			EPokemonType::Electric,
			false));
	TestFalse(TEXT("A suppressed Air Balloon does not prevent Ground moves"),
		FBattleItemRules::ShouldAirBalloonPreventMove(
			FBattleItemRules::GetAirBalloonId(),
			EPokemonType::Ground,
			true));
	TestTrue(TEXT("Any connected damaging hit pops an active Air Balloon"),
		FBattleItemRules::ShouldPopAirBalloon(
			FBattleItemRules::GetAirBalloonId(),
			true,
			false));
	TestFalse(TEXT("A blocked or non-damaging hit does not pop an Air Balloon"),
		FBattleItemRules::ShouldPopAirBalloon(
			FBattleItemRules::GetAirBalloonId(),
			false,
			false));
	TestFalse(TEXT("A connected hit does not pop a suppressed Air Balloon"),
		FBattleItemRules::ShouldPopAirBalloon(
			FBattleItemRules::GetAirBalloonId(),
			true,
			true));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CHeldItemOwnershipLedgerTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Ownership.CanonicalOperationsAndFinalFacts",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CHeldItemOwnershipLedgerTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const FBattleHeldItemInstanceState Restored = MakePersistentLedgerItem(
		1, 1, 101, FBattleItemRules::GetLeftoversId());
	const FBattleHeldItemInstanceState Captured = MakePersistentLedgerItem(
		2, 2, 202, FBattleItemRules::GetAirBalloonId());
	const FBattleHeldItemInstanceState Consumed = MakePersistentLedgerItem(
		3, 3, 303, FBattleItemRules::GetSitrusBerryId());
	const FBattleHeldItemInstanceState Generated = MakeGeneratedLedgerItem(
		4, 4, 404, FBattleItemRules::GetQuickClawId());
	const FBattleHeldItemInstanceState TrickFirst = MakePersistentLedgerItem(
		5, 5, 505, FBattleItemRules::GetChoiceBandId());
	const FBattleHeldItemInstanceState TrickSecond = MakePersistentLedgerItem(
		6, 6, 606, FBattleItemRules::GetHeavyDutyBootsId());
	const FBattleHeldItemInstanceState KnockedOff = MakePersistentLedgerItem(
		7, 7, 707, FBattleItemRules::GetLifeOrbId());
	const TArray<FBattleHeldItemInstanceState> InitialStates{
		Generated,
		TrickSecond,
		Consumed,
		Restored,
		KnockedOff,
		Captured,
		TrickFirst};

	FBattleHeldItemLedger Ledger;
	EBattleHeldItemContractError Error = EBattleHeldItemContractError::InvalidState;
	TestTrue(TEXT("Canonical held items create one stable ownership ledger"),
		FBattleHeldItemLedger::TryCreate(InitialStates, Ledger, Error));
	FBattleHeldItemOperationFact Fact;
	auto Apply = [this, &Ledger, &Fact, &Error](
		const FBattleHeldItemOperationRequest& Request,
		const EBattleHeldItemOperationKind ExpectedKind,
		const uint64 ExpectedOrdinal,
		const TCHAR* Label)
	{
		const bool bApplied = Ledger.TryApplyOperation(Request, Fact, Error);
		TestTrue(Label, bApplied);
		if (bApplied)
		{
			TestEqual(TEXT("Ownership fact preserves its typed operation"),
				Fact.Kind,
				ExpectedKind);
			TestEqual(TEXT("Ownership facts retain stable success order"),
				Fact.FactOrdinal,
				ExpectedOrdinal);
		}
		return bApplied;
	};

	FBattleHeldItemOperationRequest ConsumeForRecycle;
	ConsumeForRecycle.Kind = EBattleHeldItemOperationKind::Consume;
	ConsumeForRecycle.PrimaryInstanceId = Restored.InstanceId;
	Apply(ConsumeForRecycle, EBattleHeldItemOperationKind::Consume, 1,
		TEXT("Consumption records the item as absent"));

	FBattleHeldItemOperationRequest Recycle;
	Recycle.Kind = EBattleHeldItemOperationKind::Restore;
	Recycle.PrimaryInstanceId = Restored.InstanceId;
	Recycle.TargetHolderTrainerId = Restored.OriginalOwnerTrainerId;
	Recycle.TargetHolderBattlerId = Restored.OriginalOwnerBattlerId;
	Apply(Recycle, EBattleHeldItemOperationKind::Restore, 2,
		TEXT("Recycle maps to typed restoration for the original holder"));
	TestTrue(TEXT("Recycle restoration remains explicit in the ledger"),
		Fact.PrimaryAfter.bRestoredAfterConsumption
			&& !Fact.PrimaryAfter.bConsumed);

	FBattleHeldItemOperationRequest KnockOff;
	KnockOff.Kind = EBattleHeldItemOperationKind::Remove;
	KnockOff.PrimaryInstanceId = KnockedOff.InstanceId;
	Apply(KnockOff, EBattleHeldItemOperationKind::Remove, 3,
		TEXT("Knock Off maps to typed temporary removal"));
	TestTrue(TEXT("Knock Off frees the holder without consuming item identity"),
		Fact.PrimaryAfter.bTemporarilyRemoved
			&& Fact.PrimaryAfter.CurrentItemId.IsValid()
			&& !Fact.PrimaryAfter.CurrentHolderBattlerId.IsValid());

	FBattleHeldItemOperationRequest Trick;
	Trick.Kind = EBattleHeldItemOperationKind::Swap;
	Trick.PrimaryInstanceId = TrickFirst.InstanceId;
	Trick.SecondaryInstanceId = TrickSecond.InstanceId;
	Apply(Trick, EBattleHeldItemOperationKind::Swap, 4,
		TEXT("Trick maps to one atomic typed swap"));
	TestTrue(TEXT("Trick emits both item before-and-after records"),
		Fact.SecondaryBefore.IsSet()
			&& Fact.SecondaryAfter.IsSet()
			&& Fact.PrimaryAfter.CurrentHolderBattlerId
				== TrickSecond.OriginalOwnerBattlerId
			&& Fact.SecondaryAfter.GetValue().CurrentHolderBattlerId
				== TrickFirst.OriginalOwnerBattlerId);

	FBattleHeldItemOperationRequest Thief;
	Thief.Kind = EBattleHeldItemOperationKind::TemporarilySteal;
	Thief.PrimaryInstanceId = Captured.InstanceId;
	Thief.TargetHolderTrainerId = KnockedOff.OriginalOwnerTrainerId;
	Thief.TargetHolderBattlerId = KnockedOff.OriginalOwnerBattlerId;
	Apply(Thief, EBattleHeldItemOperationKind::TemporarilySteal, 5,
		TEXT("Thief maps to typed temporary theft after the target holder is free"));
	TestEqual(TEXT("Temporary theft records the transient holder"),
		Fact.PrimaryAfter.CurrentHolderBattlerId,
		KnockedOff.OriginalOwnerBattlerId);

	FBattleHeldItemOperationRequest ConsumeNormally;
	ConsumeNormally.Kind = EBattleHeldItemOperationKind::Consume;
	ConsumeNormally.PrimaryInstanceId = Consumed.InstanceId;
	Apply(ConsumeNormally, EBattleHeldItemOperationKind::Consume, 6,
		TEXT("Normal berry consumption remains a typed consume operation"));

	TArray<FBattleFinalHeldItemFact> FinalFacts;
	const TArray<FBattlerId> CapturedOwners{Captured.OriginalOwnerBattlerId};
	TestTrue(TEXT("Battle end emits ownership facts without persistent writes"),
		Ledger.TryBuildFinalFacts(CapturedOwners, FinalFacts, Error));
	TestEqual(TEXT("Every canonical item instance emits one final fact"),
		FinalFacts.Num(),
		7);
	if (FinalFacts.Num() == 7)
	{
		TestTrue(TEXT("Recycle remains restored for the original owner"),
			FinalFacts[0].Disposition
				== EBattleHeldItemFinalDisposition::OriginalOwner
				&& FinalFacts[0].bRestoredAfterConsumption
				&& FinalFacts[0].FinalItemId == Restored.OriginalItemId);
		TestTrue(TEXT("A captured Pokemon keeps its original held item after Thief"),
			FinalFacts[1].Disposition
				== EBattleHeldItemFinalDisposition::CapturedOriginalOwner
				&& FinalFacts[1].FinalOwnerBattlerId
					== Captured.OriginalOwnerBattlerId
				&& FinalFacts[1].FinalItemId == Captured.OriginalItemId);
		TestEqual(TEXT("Ordinary consumption stays consumed"),
			FinalFacts[2].Disposition,
			EBattleHeldItemFinalDisposition::Consumed);
		TestEqual(TEXT("A battle-generated held item is removed"),
			FinalFacts[3].Disposition,
			EBattleHeldItemFinalDisposition::BattleGeneratedRemoved);
		TestTrue(TEXT("Trick resets both swapped items to their original owners"),
			FinalFacts[4].FinalOwnerBattlerId == TrickFirst.OriginalOwnerBattlerId
				&& FinalFacts[5].FinalOwnerBattlerId
					== TrickSecond.OriginalOwnerBattlerId);
		TestTrue(TEXT("Knock Off resets to original ownership at battle end"),
			FinalFacts[6].Disposition
				== EBattleHeldItemFinalDisposition::OriginalOwner
				&& FinalFacts[6].FinalOwnerBattlerId
					== KnockedOff.OriginalOwnerBattlerId
				&& FinalFacts[6].FinalItemId == KnockedOff.OriginalItemId);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC08CQuickClawRuleTest,
	"PokemonSolarus.Battle.C08C.HeldItem.Rules.QuickClawEligibilityUniformDrawAndPriority",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FBattleC08CQuickClawRuleTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Quick Claw draws uniformly over the inclusive maximum four"),
		FBattleItemRules::GetQuickClawRollMaxInclusive(),
		static_cast<uint32>(4));
	TestEqual(TEXT("Quick Claw succeeds only on raw draw zero"),
		FBattleItemRules::GetQuickClawSuccessRawDraw(),
		static_cast<uint32>(0));
	TestEqual(TEXT("Quick Claw grants exactly positive 0.1 fractional priority"),
		FBattleItemRules::GetQuickClawFractionalPriorityTenths(),
		1);
	TestEqual(TEXT("Quick Claw uses a stable action-order RNG purpose"),
		FBattleItemRules::GetQuickClawActivationPurpose(),
		MakeDefinitionId<FDefinitionId>(TEXT("Rule.Item.QuickClaw.ActionOrder")));

	auto EvaluateEligibility = [this](
		const int32 MovePriority,
		const bool bSelectedMoveEligible,
		const bool bSuppressed,
		const bool bExpectedEligible,
		const bool bExpectedDraw,
		const EBattleAbilityItemActivationOutcome ExpectedOutcome)
	{
		FBattleQuickClawFacts Facts;
		Facts.ItemId = FBattleItemRules::GetQuickClawId();
		Facts.MovePriority = MovePriority;
		Facts.bSelectedMoveEligible = bSelectedMoveEligible;
		Facts.bSuppressed = bSuppressed;
		FBattleQuickClawEligibilityResult Result;
		TestTrue(TEXT("Valid Quick Claw eligibility facts are evaluated"),
			FBattleItemRules::TryEvaluateQuickClawEligibility(Facts, Result));
		TestTrue(TEXT("Quick Claw eligibility result is marked valid"), Result.bValid);
		TestEqual(TEXT("Quick Claw eligibility is exact"),
			Result.bEligible,
			bExpectedEligible);
		TestEqual(TEXT("Quick Claw draw consumption is exact"),
			Result.bConsumesRandomDraw,
			bExpectedDraw);
		TestEqual(TEXT("Quick Claw eligibility outcome is explicit"),
			Result.Outcome,
			ExpectedOutcome);
	};

	EvaluateEligibility(0, true, false, true, true,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateEligibility(-7, true, false, true, true,
		EBattleAbilityItemActivationOutcome::Applied);
	EvaluateEligibility(1, true, false, false, false,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateEligibility(0, false, false, false, false,
		EBattleAbilityItemActivationOutcome::Ineligible);
	EvaluateEligibility(0, true, true, false, false,
		EBattleAbilityItemActivationOutcome::Suppressed);

	FBattleQuickClawFacts EligibleFacts;
	EligibleFacts.ItemId = FBattleItemRules::GetQuickClawId();
	EligibleFacts.MovePriority = -1;
	EligibleFacts.bSelectedMoveEligible = true;
	for (uint32 RawDraw = 0;
		RawDraw <= FBattleItemRules::GetQuickClawRollMaxInclusive();
		++RawDraw)
	{
		FBattleQuickClawDrawResult Result;
		TestTrue(TEXT("Every raw value in Quick Claw U[0,4] resolves"),
			FBattleItemRules::TryResolveQuickClawDraw(
				EligibleFacts,
				RawDraw,
				Result));
		TestTrue(TEXT("Resolved Quick Claw draw is marked valid"), Result.bValid);
		const bool bExpectedSuccess = RawDraw == 0;
		TestEqual(TEXT("Quick Claw succeeds exactly on draw zero"),
			Result.bApplies,
			bExpectedSuccess);
		TestEqual(TEXT("Only a successful Quick Claw draw adds fractional priority"),
			Result.FractionalPriorityTenths,
			bExpectedSuccess ? 1 : 0);
		TestEqual(TEXT("Quick Claw draw outcome is explicit"),
			Result.Outcome,
			bExpectedSuccess
				? EBattleAbilityItemActivationOutcome::Applied
				: EBattleAbilityItemActivationOutcome::Ineligible);
	}

	FBattleQuickClawDrawResult OutOfRangeResult;
	TestFalse(TEXT("Quick Claw rejects a raw draw outside U[0,4]"),
		FBattleItemRules::TryResolveQuickClawDraw(
			EligibleFacts,
			5,
			OutOfRangeResult));

	FBattleQuickClawFacts InvalidPriorityFacts = EligibleFacts;
	InvalidPriorityFacts.MovePriority = -8;
	FBattleQuickClawEligibilityResult InvalidPriorityResult;
	TestFalse(TEXT("Quick Claw rejects priority below the supported move range"),
		FBattleItemRules::TryEvaluateQuickClawEligibility(
			InvalidPriorityFacts,
			InvalidPriorityResult));
	InvalidPriorityFacts.MovePriority = 6;
	TestFalse(TEXT("Quick Claw rejects priority above the supported move range"),
		FBattleItemRules::TryEvaluateQuickClawEligibility(
			InvalidPriorityFacts,
			InvalidPriorityResult));

	return true;
}

}

#endif
