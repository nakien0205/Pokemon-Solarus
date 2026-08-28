#if WITH_DEV_AUTOMATION_TESTS

#include "Battle/BattleEncounterPolicy.h"
#include "Battle/BattleSetup.h"
#include "Battle/BattleSetupTypes.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleADR00023B1SetupPolicyOrdinalTest,
	"PokemonSolarus.Battle.ADR0002.3B1.SetupPolicy.EnumAndErrorOrdinals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleADR00023B1SetupPolicyOrdinalTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	TestEqual(TEXT("Player role ordinal remains stable"), static_cast<uint8>(EBattleTrainerRole::Player), static_cast<uint8>(0));
	TestEqual(TEXT("Partner role ordinal remains stable"), static_cast<uint8>(EBattleTrainerRole::Partner), static_cast<uint8>(1));
	TestEqual(TEXT("Opponent role ordinal remains stable"), static_cast<uint8>(EBattleTrainerRole::Opponent), static_cast<uint8>(2));
	TestEqual(TEXT("Human controller ordinal remains stable"), static_cast<uint8>(EBattleDecisionController::Human), static_cast<uint8>(0));
	TestEqual(TEXT("Partner AI controller ordinal remains stable"), static_cast<uint8>(EBattleDecisionController::PartnerAI), static_cast<uint8>(1));
	TestEqual(TEXT("Enemy AI controller ordinal remains stable"), static_cast<uint8>(EBattleDecisionController::EnemyAI), static_cast<uint8>(2));
	TestEqual(TEXT("Scripted controller ordinal remains stable"), static_cast<uint8>(EBattleDecisionController::Scripted), static_cast<uint8>(3));
	TestEqual(TEXT("Disabled WildFlee ordinal remains stable"), static_cast<uint8>(EBattleWildFleeMode::Disabled), static_cast<uint8>(0));
	TestEqual(TEXT("Never WildFlee ordinal remains stable"), static_cast<uint8>(EBattleWildFleeMode::Never), static_cast<uint8>(1));
	TestEqual(TEXT("Always WildFlee ordinal remains stable"), static_cast<uint8>(EBattleWildFleeMode::Always), static_cast<uint8>(2));
	TestEqual(TEXT("Chance WildFlee ordinal remains stable"), static_cast<uint8>(EBattleWildFleeMode::Chance), static_cast<uint8>(3));
	TestEqual(TEXT("Existing setup errors remain stable"), static_cast<uint8>(EBattleSetupValidationError::InvalidReinforcement), static_cast<uint8>(15));
	TestEqual(TEXT("Partner controller setup error is appended"), static_cast<uint8>(EBattleSetupValidationError::InvalidPartnerController), static_cast<uint8>(16));
	TestEqual(TEXT("Wild reserve setup error is appended"), static_cast<uint8>(EBattleSetupValidationError::InvalidWildReserve), static_cast<uint8>(17));
	TestEqual(TEXT("Existing policy errors remain stable"), static_cast<uint8>(EBattleEncounterPolicyError::InvalidWildFleePolicy), static_cast<uint8>(6));
	TestEqual(TEXT("Partner controller policy error is appended"), static_cast<uint8>(EBattleEncounterPolicyError::InvalidPartnerController), static_cast<uint8>(7));
	TestEqual(TEXT("Wild reserve policy error is appended"), static_cast<uint8>(EBattleEncounterPolicyError::InvalidWildReserve), static_cast<uint8>(8));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleSetupTaxonomyTest,
	"PokemonSolarus.Battle.CoreContracts.SetupTypes.ExplicitTaxonomy",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleSetupTaxonomyTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Wild encounter encoding is stable"), static_cast<uint8>(EBattleEncounterKind::Wild), static_cast<uint8>(0));
	TestEqual(TEXT("Trainer encounter encoding is stable"), static_cast<uint8>(EBattleEncounterKind::Trainer), static_cast<uint8>(1));
	TestEqual(TEXT("Rival encounter encoding is stable"), static_cast<uint8>(EBattleEncounterKind::Rival), static_cast<uint8>(2));
	TestEqual(TEXT("Boss/Gym encounter encoding is stable"), static_cast<uint8>(EBattleEncounterKind::BossGym), static_cast<uint8>(3));
	TestEqual(TEXT("Tutorial/Scripted encounter encoding is stable"), static_cast<uint8>(EBattleEncounterKind::TutorialScripted), static_cast<uint8>(4));

	TestEqual(TEXT("Single format encoding is stable"), static_cast<uint8>(EBattleFormat::Single), static_cast<uint8>(0));
	TestEqual(TEXT("Double format encoding is stable"), static_cast<uint8>(EBattleFormat::Double), static_cast<uint8>(1));
	TestEqual(TEXT("Partner Double encoding is stable"), static_cast<uint8>(EBattleFormat::PartnerDouble), static_cast<uint8>(2));

	TestEqual(TEXT("Setup phase encoding is stable"), static_cast<uint8>(EBattlePhase::Setup), static_cast<uint8>(0));
	TestEqual(TEXT("Selecting phase encoding is stable"), static_cast<uint8>(EBattlePhase::Selecting), static_cast<uint8>(1));
	TestEqual(TEXT("Locked phase encoding is stable"), static_cast<uint8>(EBattlePhase::Locked), static_cast<uint8>(2));
	TestEqual(TEXT("Resolving phase encoding is stable"), static_cast<uint8>(EBattlePhase::Resolving), static_cast<uint8>(3));
	TestEqual(TEXT("Mandatory Replacement phase encoding is stable"), static_cast<uint8>(EBattlePhase::MandatoryReplacement), static_cast<uint8>(4));
	TestEqual(TEXT("End Of Turn phase encoding is stable"), static_cast<uint8>(EBattlePhase::EndOfTurn), static_cast<uint8>(5));
	TestEqual(TEXT("Terminal phase encoding is stable"), static_cast<uint8>(EBattlePhase::Terminal), static_cast<uint8>(6));

	TestEqual(TEXT("Fight action encoding is stable"), static_cast<uint8>(EBattleActionKind::Fight), static_cast<uint8>(0));
	TestEqual(TEXT("Bag action encoding is stable"), static_cast<uint8>(EBattleActionKind::Bag), static_cast<uint8>(1));
	TestEqual(TEXT("Switch action encoding is stable"), static_cast<uint8>(EBattleActionKind::Switch), static_cast<uint8>(2));
	TestEqual(TEXT("Run action encoding is stable"), static_cast<uint8>(EBattleActionKind::Run), static_cast<uint8>(3));
	TestEqual(TEXT("Wild Flee action encoding is stable"), static_cast<uint8>(EBattleActionKind::WildFlee), static_cast<uint8>(4));
	TestEqual(TEXT("Replacement action encoding is stable"), static_cast<uint8>(EBattleActionKind::Replacement), static_cast<uint8>(5));
	TestEqual(TEXT("Scripted End action encoding is stable"), static_cast<uint8>(EBattleActionKind::ScriptedEnd), static_cast<uint8>(6));
	TestEqual(TEXT("Abandon action encoding is stable"), static_cast<uint8>(EBattleActionKind::Abandon), static_cast<uint8>(7));

	TestEqual(TEXT("In Progress outcome encoding is stable"), static_cast<uint8>(EBattleOutcome::InProgress), static_cast<uint8>(0));
	TestEqual(TEXT("Victory outcome encoding is stable"), static_cast<uint8>(EBattleOutcome::Victory), static_cast<uint8>(1));
	TestEqual(TEXT("Defeat outcome encoding is stable"), static_cast<uint8>(EBattleOutcome::Defeat), static_cast<uint8>(2));
	TestEqual(TEXT("Escape outcome encoding is stable"), static_cast<uint8>(EBattleOutcome::Escape), static_cast<uint8>(3));
	TestEqual(TEXT("Scripted End outcome encoding is stable"), static_cast<uint8>(EBattleOutcome::ScriptedEnd), static_cast<uint8>(4));
	TestEqual(TEXT("Abandoned outcome encoding is stable"), static_cast<uint8>(EBattleOutcome::Abandoned), static_cast<uint8>(5));

	TestEqual(TEXT("No outcome cause encoding is stable"), static_cast<uint8>(EBattleOutcomeCause::None), static_cast<uint8>(0));
	TestEqual(TEXT("Ordinary outcome cause encoding is stable"), static_cast<uint8>(EBattleOutcomeCause::Ordinary), static_cast<uint8>(1));
	TestEqual(TEXT("Capture outcome cause encoding is stable"), static_cast<uint8>(EBattleOutcomeCause::Capture), static_cast<uint8>(2));
	TestEqual(TEXT("Partner Team Victory cause encoding is stable"), static_cast<uint8>(EBattleOutcomeCause::PartnerTeamVictory), static_cast<uint8>(3));
	TestEqual(TEXT("Simultaneous Faint cause encoding is stable"), static_cast<uint8>(EBattleOutcomeCause::SimultaneousFaint), static_cast<uint8>(4));
	TestEqual(TEXT("Opponent Fled cause encoding is stable"), static_cast<uint8>(EBattleOutcomeCause::OpponentFled), static_cast<uint8>(5));

	TestEqual(TEXT("Self target encoding is stable"), static_cast<uint8>(EBattleTargetClass::Self), static_cast<uint8>(0));
	TestEqual(TEXT("Selected Ally target encoding is stable"), static_cast<uint8>(EBattleTargetClass::SelectedAlly), static_cast<uint8>(1));
	TestEqual(TEXT("Selected Opponent target encoding is stable"), static_cast<uint8>(EBattleTargetClass::SelectedOpponent), static_cast<uint8>(2));
	TestEqual(TEXT("Any Selected Battler target encoding is stable"), static_cast<uint8>(EBattleTargetClass::AnySelectedBattler), static_cast<uint8>(3));
	TestEqual(TEXT("Random Legal Opponent target encoding is stable"), static_cast<uint8>(EBattleTargetClass::RandomLegalOpponent), static_cast<uint8>(4));
	TestEqual(TEXT("User Side target encoding is stable"), static_cast<uint8>(EBattleTargetClass::UserSide), static_cast<uint8>(5));
	TestEqual(TEXT("Opponent Side target encoding is stable"), static_cast<uint8>(EBattleTargetClass::OpponentSide), static_cast<uint8>(6));
	TestEqual(TEXT("Both Sides target encoding is stable"), static_cast<uint8>(EBattleTargetClass::BothSides), static_cast<uint8>(7));
	TestEqual(TEXT("Field target encoding is stable"), static_cast<uint8>(EBattleTargetClass::Field), static_cast<uint8>(8));
	TestEqual(TEXT("Fixed Spread Set target encoding is stable"), static_cast<uint8>(EBattleTargetClass::FixedSpreadSet), static_cast<uint8>(9));

	TestEqual(TEXT("Core Only visibility encoding is stable"), static_cast<uint8>(EBattleVisibilityLevel::CoreOnly), static_cast<uint8>(0));
	TestEqual(TEXT("Owning Trainer visibility encoding is stable"), static_cast<uint8>(EBattleVisibilityLevel::OwningTrainer), static_cast<uint8>(1));
	TestEqual(TEXT("Owning Side visibility encoding is stable"), static_cast<uint8>(EBattleVisibilityLevel::OwningSide), static_cast<uint8>(2));
	TestEqual(TEXT("Public visibility encoding is stable"), static_cast<uint8>(EBattleVisibilityLevel::Public), static_cast<uint8>(3));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FBattleC10TargetOrdinalContractTest,
	"PokemonSolarus.Battle.C04B.C10Targets.Enum.AppendOnlyOrdinals",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FBattleC10TargetOrdinalContractTest::RunTest(const FString& Parameters)
{
	(void)Parameters;
	const TArray<uint8> ExistingOrdinals =
	{
		static_cast<uint8>(EBattleTargetClass::Self),
		static_cast<uint8>(EBattleTargetClass::SelectedAlly),
		static_cast<uint8>(EBattleTargetClass::SelectedOpponent),
		static_cast<uint8>(EBattleTargetClass::AnySelectedBattler),
		static_cast<uint8>(EBattleTargetClass::RandomLegalOpponent),
		static_cast<uint8>(EBattleTargetClass::UserSide),
		static_cast<uint8>(EBattleTargetClass::OpponentSide),
		static_cast<uint8>(EBattleTargetClass::BothSides),
		static_cast<uint8>(EBattleTargetClass::Field),
		static_cast<uint8>(EBattleTargetClass::FixedSpreadSet)
	};
	TestTrue(
		TEXT("The original target vocabulary keeps ordinals zero through nine"),
		ExistingOrdinals == TArray<uint8>({0, 1, 2, 3, 4, 5, 6, 7, 8, 9}));
	TestEqual(
		TEXT("Selected Other Battler is appended after the original vocabulary"),
		static_cast<uint8>(EBattleTargetClass::SelectedOtherBattler),
		static_cast<uint8>(10));
	TestEqual(
		TEXT("Fixed Opponent Spread Set is the appended maximum target class"),
		static_cast<uint8>(EBattleTargetClass::FixedOpponentSpreadSet),
		static_cast<uint8>(11));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FNatureStatModifierContractTest,
	"PokemonSolarus.Battle.CoreContracts.SetupTypes.NatureStatModifier",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FNatureStatModifierContractTest::RunTest(const FString& Parameters)
{
	FNatureStatModifier Modifier;
	TestTrue(TEXT("A default nature modifier is neutral"), Modifier.IsNeutral());

	TestTrue(
		TEXT("An explicit neutral nature is accepted"),
		FNatureStatModifier::TryCreate(ENatureStat::None, ENatureStat::None, Modifier));
	TestTrue(TEXT("The explicit neutral nature remains neutral"), Modifier.IsNeutral());

	TestFalse(
		TEXT("A boost without a reduction is rejected"),
		FNatureStatModifier::TryCreate(ENatureStat::Attack, ENatureStat::None, Modifier));
	TestTrue(TEXT("A rejected nature resets to neutral"), Modifier.IsNeutral());
	TestFalse(
		TEXT("The same boosted and reduced stat is rejected"),
		FNatureStatModifier::TryCreate(ENatureStat::Speed, ENatureStat::Speed, Modifier));
	TestFalse(
		TEXT("An unknown nature stat is rejected"),
		FNatureStatModifier::TryCreate(
			static_cast<ENatureStat>(255),
			ENatureStat::Defense,
			Modifier));

	TestTrue(
		TEXT("One distinct boost/reduction pair is accepted"),
		FNatureStatModifier::TryCreate(ENatureStat::Attack, ENatureStat::Defense, Modifier));
	TestFalse(TEXT("A valid pair is not neutral"), Modifier.IsNeutral());
	TestEqual(TEXT("The boosted stat is retained"), Modifier.GetBoostedStat(), ENatureStat::Attack);
	TestEqual(TEXT("The reduced stat is retained"), Modifier.GetReducedStat(), ENatureStat::Defense);

	int32 Numerator = 0;
	int32 Denominator = 0;
	TestTrue(
		TEXT("The boosted multiplier can be queried"),
		Modifier.TryGetMultiplier(ENatureStat::Attack, Numerator, Denominator));
	TestEqual(TEXT("A boosted nature uses numerator 11"), Numerator, 11);
	TestEqual(TEXT("Nature multipliers use denominator 10"), Denominator, 10);

	TestTrue(
		TEXT("The reduced multiplier can be queried"),
		Modifier.TryGetMultiplier(ENatureStat::Defense, Numerator, Denominator));
	TestEqual(TEXT("A reduced nature uses numerator 9"), Numerator, 9);
	TestEqual(TEXT("Nature multipliers still use denominator 10"), Denominator, 10);

	TestTrue(
		TEXT("An unaffected multiplier can be queried"),
		Modifier.TryGetMultiplier(ENatureStat::Speed, Numerator, Denominator));
	TestEqual(TEXT("An unaffected nature uses numerator 10"), Numerator, 10);
	TestEqual(TEXT("An unaffected nature uses denominator 10"), Denominator, 10);

	TestFalse(
		TEXT("None is not a calculable battle stat"),
		Modifier.TryGetMultiplier(ENatureStat::None, Numerator, Denominator));
	TestEqual(TEXT("A rejected multiplier query resets the numerator"), Numerator, 0);
	TestEqual(TEXT("A rejected multiplier query resets the denominator"), Denominator, 0);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
