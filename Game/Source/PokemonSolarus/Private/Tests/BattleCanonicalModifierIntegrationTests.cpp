#if WITH_DEV_AUTOMATION_TESTS

#include "BattleCanonicalIntegrationTestSupport.h"

#include "Misc/AutomationTest.h"

namespace BattleCanonicalModifierPrivate
{
	using namespace BattleCanonicalIntegrationTestSupport;

	FSetupSpec MakeModifierSpec()
	{
		FSetupSpec Spec;
		Spec.BattleValue = 11301;
		Spec.EncounterKind = EBattleEncounterKind::Trainer;
		Spec.Format = EBattleFormat::Single;
		Spec.Policies.bBagAllowed = false;
		Spec.Policies.bShiftPromptEligible = false;
		Spec.Trainers = {
			{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		FBattlerSpec Charizard;
		Charizard.TrainerValue = 1;
		Charizard.BattlerValue = 11;
		Charizard.SpeciesId = FName(TEXT("Species.Charizard"));
		Charizard.NatureId = FName(TEXT("Nature.Hardy"));
		Charizard.AbilityId = FName(TEXT("Ability.Blaze"));
		Charizard.MoveIds = {FName(TEXT("Move.Flamethrower"))};
		Charizard.EffortValues.Speed = 252;
		Spec.Battlers.Add(Charizard);
		FBattlerSpec Venusaur;
		Venusaur.TrainerValue = 2;
		Venusaur.BattlerValue = 21;
		Venusaur.SpeciesId = FName(TEXT("Species.Venusaur"));
		Venusaur.NatureId = FName(TEXT("Nature.Hardy"));
		Venusaur.AbilityId = FName(TEXT("Ability.Overgrow"));
		Venusaur.MoveIds = {FName(TEXT("Move.SwordsDance"))};
		Spec.Battlers.Add(Venusaur);
		Spec.Active = {
			{EBattleSide::Player, EBattlePosition::Left, 1, 11},
			{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		return Spec;
	}

	FSetupSpec MakeLevitateBypassSpec(const uint64 BattleValue, const bool bMoldBreaker)
	{
		FSetupSpec Spec;
		Spec.BattleValue = BattleValue;
		Spec.EncounterKind = EBattleEncounterKind::Trainer;
		Spec.Format = EBattleFormat::Single;
		Spec.Policies.bBagAllowed = false;
		Spec.Policies.bShiftPromptEligible = false;
		Spec.Trainers = {
			{1, EBattleSide::Player, EBattleTrainerRole::Player, EBattleDecisionController::Human, {}},
			{2, EBattleSide::Opponent, EBattleTrainerRole::Opponent, EBattleDecisionController::EnemyAI, {}}};
		FBattlerSpec Attacker;
		Attacker.TrainerValue = 1;
		Attacker.BattlerValue = 11;
		Attacker.SpeciesId = bMoldBreaker
			? FName(TEXT("Species.Excadrill")) : FName(TEXT("Species.Venusaur"));
		Attacker.NatureId = FName(TEXT("Nature.Hardy"));
		Attacker.AbilityId = bMoldBreaker
			? FName(TEXT("Ability.MoldBreaker")) : FName(TEXT("Ability.Overgrow"));
		Attacker.MoveIds = {FName(TEXT("Move.Earthquake"))};
		Attacker.EffortValues.Speed = 252;
		FBattlerSpec Defender;
		Defender.TrainerValue = 2;
		Defender.BattlerValue = 21;
		Defender.SpeciesId = FName(TEXT("Species.Rotom"));
		Defender.NatureId = FName(TEXT("Nature.Hardy"));
		Defender.AbilityId = FName(TEXT("Ability.Levitate"));
		Defender.MoveIds = {FName(TEXT("Move.SwordsDance"))};
		Spec.Battlers = {Attacker, Defender};
		Spec.Active = {
			{EBattleSide::Player, EBattlePosition::Left, 1, 11},
			{EBattleSide::Opponent, EBattlePosition::Left, 2, 21}};
		return Spec;
	}

	bool HasSourceDefinition(
		const FBattleReplayRecord& Record,
		const EBattleEventType Type,
		const TCHAR* Id)
	{
		const FDefinitionId DefinitionId = MakeDefinitionId<FDefinitionId>(Id);
		for (const FBattleResolution& Resolution : Record.GetResolutions())
			for (const FBattleEvent& Event : Resolution.GetEvents())
				if (Event.GetType() == Type
					&& Event.GetSource().DefinitionId == DefinitionId) return true;
		return false;
	}

	const FBattleEvent* FindTargetedEvent(
		const FBattleReplayRecord& Record,
		const EBattleEventType Type,
		const uint64 BattlerValue)
	{
		for (const FBattleResolution& Resolution : Record.GetResolutions())
		{
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				if (Event.GetType() == Type
					&& Event.GetTargets().ContainsByPredicate([BattlerValue](const FBattleEventTarget& Target)
					{
						return Target.BattlerId.IsValid() && Target.BattlerId.GetValue() == BattlerValue;
					})) return &Event;
			}
		}
		return nullptr;
	}

	int64 PokeRoundQ12(const int64 Value, const int64 Modifier)
	{
		return (Value * Modifier + 2047) / 4096;
	}

	FBattleDamageModifier MakeModifier(
		const TCHAR* Rule,
		const int32 ModifierQ12,
		const bool bIgnoredByCritical = false)
	{
		FBattleDamageModifier Modifier;
		Modifier.RuleId = MakeDefinitionId<FDefinitionId>(Rule);
		Modifier.ModifierQ12 = ModifierQ12;
		Modifier.bIgnoredByCritical = bIgnoredByCritical;
		return Modifier;
	}

	FBattleDamageTraceEntry TraceEntry(
		const EBattleDamageTraceStep Step,
		const int64 Value,
		const FDefinitionId RuleId = FDefinitionId())
	{
		FBattleDamageTraceEntry Entry;
		Entry.Step = Step;
		Entry.Value = Value;
		Entry.RuleId = RuleId;
		return Entry;
	}

	bool TestExactTrace(
		FAutomationTestBase& Test,
		const FString& Label,
		const FDamageTrace& Actual,
		const TArray<FBattleDamageTraceEntry>& Expected)
	{
		bool bValid = Test.TestEqual(Label + TEXT(" entry count"), Actual.Entries.Num(), Expected.Num());
		for (int32 Index = 0; Index < FMath::Min(Actual.Entries.Num(), Expected.Num()); ++Index)
		{
			bValid &= Test.TestEqual(FString::Printf(TEXT("%s step %d"), *Label, Index),
				Actual.Entries[Index].Step, Expected[Index].Step);
			bValid &= Test.TestEqual(FString::Printf(TEXT("%s value %d"), *Label, Index),
				Actual.Entries[Index].Value, Expected[Index].Value);
			bValid &= Test.TestTrue(FString::Printf(TEXT("%s rule %d"), *Label, Index),
				Actual.Entries[Index].RuleId == Expected[Index].RuleId);
		}
		return bValid;
	}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FC11AB00BModifierOrderTrace,
	"PokemonSolarus.Battle.C11A.Single.Modifiers.B00BOrderTraceHpRevealAndCleanup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FC11AB00BModifierOrderTrace::RunTest(const FString& Parameters)
{
	FCatalogFixture Fixture;
	FString Error;
	if (!TryLoadProductionFixture(*this, Fixture, Error))
	{
		AddError(Error);
		return false;
	}
	FBattleSetup Setup;
	if (!TryBuildSetup(Fixture.Catalog, MakeModifierSpec(), Setup, Error))
	{
		AddError(Error);
		return false;
	}
	const FChoiceProvider Provider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
	{
		Out.Kind = EChoiceKind::Fight;
		Out.DefinitionId = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
			? FName(TEXT("Move.Flamethrower")) : FName(TEXT("Move.SwordsDance"));
		Out.ActiveTarget = MakeActiveSlotId(
			Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1)
				? EBattleSide::Opponent : EBattleSide::Player,
			EBattlePosition::Left);
		return true;
	};
	const FDriveFunction Drive = [Provider](FBattleEngine& Engine, FRunEvidence& Evidence, FString& DriveError)
	{
		return LockTurn(Engine, Provider, Evidence, DriveError)
			&& ExecuteLockedQueue(Engine, Evidence, DriveError);
	};
	FRunEvidence Evidence;
	if (!RunDeterministicTwins(*this, TEXT("B00B public engine match"),
		Fixture.Catalog, Setup, Drive, &Evidence)) return false;

	const FBattlePartyEntrySetup* Attacker = Setup.FindBattler(MakeNumericId<FBattlerId>(11));
	const FBattlePartyEntrySetup* Defender = Setup.FindBattler(MakeNumericId<FBattlerId>(21));
	const FBattleMoveDefinition* Move = Fixture.Catalog.FindMove(
		MakeDefinitionId<FMoveId>(TEXT("Move.Flamethrower")));
	const FBattleSpeciesFormDefinition* DefenderSpecies = Fixture.Catalog.FindSpeciesForm(
		MakeDefinitionId<FSpeciesFormId>(TEXT("Species.Venusaur")));
	if (!TestNotNull(TEXT("Public attacker facts exist"), Attacker)
		|| !TestNotNull(TEXT("Public defender facts exist"), Defender)
		|| !TestNotNull(TEXT("Catalog move facts exist"), Move)
		|| !TestNotNull(TEXT("Catalog defending species facts exist"), DefenderSpecies)) return false;

	const FBattleEvent* Critical = FindTargetedEvent(Evidence.Replay, EBattleEventType::CriticalChecked, 21);
	const FBattleEvent* Damage = FindTargetedEvent(Evidence.Replay, EBattleEventType::Damage, 21);
	const FBattleEvent* HPChanged = FindTargetedEvent(Evidence.Replay, EBattleEventType::HPChanged, 21);
	if (!TestNotNull(TEXT("Engine publishes CriticalChecked"), Critical)
		|| !TestNotNull(TEXT("Engine publishes Damage"), Damage)
		|| !TestNotNull(TEXT("Engine publishes HPChanged"), HPChanged)) return false;
	const bool bCritical = Critical->GetNumericAfter() == TOptional<int64>(1);
	const FBattleRandomDraw* DamageDraw = Evidence.Replay.GetRandomTrace().FindByPredicate(
		[](const FBattleRandomDraw& Draw)
		{
			return Draw.RulePurpose == MakeDefinitionId<FDefinitionId>(TEXT("Rule.C05B.DamageRandom"));
		});
	if (!TestNotNull(TEXT("Engine exposes the exact damage RNG context"), DamageDraw)) return false;

	FBattleFinalDamageInput Input;
	Input.AttackerLevel = Attacker->Level;
	Input.AttackerStats = Attacker->Stats;
	Input.DefenderStats = Defender->Stats;
	Input.MoveCategory = Move->Category;
	Input.MovePower = Move->Power;
	Input.bCritical = bCritical;
	Input.WeatherModifierQ12 = FBattleFinalDamageCalculator::Q12Neutral;
	Input.StabModifierQ12 = 6144;
	if (!Fixture.Catalog.GetTypeChart().TryGetDualEffectiveness(
		Move->Type, DefenderSpecies->PrimaryType, DefenderSpecies->SecondaryType,
		Input.TypeEffectiveness))
	{
		AddError(TEXT("The public catalog did not provide dual-type effectiveness."));
		return false;
	}
	Input.RandomContext.BattleId = DamageDraw->BattleId;
	Input.RandomContext.TurnId = DamageDraw->TurnId;
	Input.RandomContext.ActionId = DamageDraw->ActionId;
	Input.RandomContext.ResolutionId = DamageDraw->ResolutionId;
	Input.RandomContext.RulePurpose = DamageDraw->RulePurpose;
	BattleTest::FStrictBattleRandom Random({{
		DamageDraw->InclusiveMinimum, DamageDraw->InclusiveMaximum,
		DamageDraw->Result, DamageDraw->RulePurpose}});
	FBattleFinalDamageResult Calculated;
	EBattleDamageCalculationError CalculationError = EBattleDamageCalculationError::None;
	if (!TestTrue(TEXT("Public facts calculate the matching final damage"),
		FBattleFinalDamageCalculator::TryCalculateFinalDamage(
			Input, Random, Calculated, CalculationError))) return false;
	TestTrue(TEXT("The pure calculator consumed the exact scripted draw"), Random.IsExact());

	const int64 BaseDamage = 44;
	const int64 CriticalDamage = bCritical ? BaseDamage * 3 / 2 : BaseDamage;
	const int64 RandomDamage = CriticalDamage * (100 - DamageDraw->Result) / 100;
	const int64 StabDamage = PokeRoundQ12(RandomDamage, 6144);
	const int64 ExpectedDamage = StabDamage * 2;
	const int64 LevelTerm = (2 * Attacker->Level) / 5 + 2;
	const int64 PowerTerm = LevelTerm * Move->Power;
	const int64 AttackTerm = PowerTerm * Attacker->Stats.SpecialAttack;
	const int64 Quotient = AttackTerm / Defender->Stats.SpecialDefense;
	const TArray<FBattleDamageTraceEntry> ExpectedEngineTrace = {
		TraceEntry(EBattleDamageTraceStep::InputPower, Move->Power),
		TraceEntry(EBattleDamageTraceStep::EffectivePower, Move->Power),
		TraceEntry(EBattleDamageTraceStep::OffensiveStageInput, 0),
		TraceEntry(EBattleDamageTraceStep::OffensiveStageUsed, 0),
		TraceEntry(EBattleDamageTraceStep::StagedOffensiveStat, Attacker->Stats.SpecialAttack),
		TraceEntry(EBattleDamageTraceStep::DefensiveStageInput, 0),
		TraceEntry(EBattleDamageTraceStep::DefensiveStageUsed, 0),
		TraceEntry(EBattleDamageTraceStep::StagedDefensiveStat, Defender->Stats.SpecialDefense),
		TraceEntry(EBattleDamageTraceStep::EffectiveOffensiveStat, Attacker->Stats.SpecialAttack),
		TraceEntry(EBattleDamageTraceStep::EffectiveDefensiveStat, Defender->Stats.SpecialDefense),
		TraceEntry(EBattleDamageTraceStep::LevelTerm, LevelTerm),
		TraceEntry(EBattleDamageTraceStep::PowerTerm, PowerTerm),
		TraceEntry(EBattleDamageTraceStep::AttackTerm, AttackTerm),
		TraceEntry(EBattleDamageTraceStep::Quotient, Quotient),
		TraceEntry(EBattleDamageTraceStep::BaseDamage, BaseDamage),
		TraceEntry(EBattleDamageTraceStep::SpreadDamage, BaseDamage),
		TraceEntry(EBattleDamageTraceStep::WeatherDamage, BaseDamage),
		TraceEntry(EBattleDamageTraceStep::CriticalDamage, CriticalDamage),
		TraceEntry(EBattleDamageTraceStep::RandomRoll, DamageDraw->Result),
		TraceEntry(EBattleDamageTraceStep::RandomFactor, 100 - DamageDraw->Result),
		TraceEntry(EBattleDamageTraceStep::RandomDamage, RandomDamage),
		TraceEntry(EBattleDamageTraceStep::StabDamage, StabDamage),
		TraceEntry(EBattleDamageTraceStep::TypeEffectivenessNumerator, 2),
		TraceEntry(EBattleDamageTraceStep::TypeEffectivenessDenominator, 1),
		TraceEntry(EBattleDamageTraceStep::TypeDamage, ExpectedDamage),
		TraceEntry(EBattleDamageTraceStep::BurnDamage, ExpectedDamage),
		TraceEntry(EBattleDamageTraceStep::FinalModifierClamped, 4096),
		TraceEntry(EBattleDamageTraceStep::FinalDamage, ExpectedDamage)};
	TestExactTrace(*this, TEXT("B00B engine-derived public trace"),
		Calculated.Trace, ExpectedEngineTrace);
	TestEqual(TEXT("B00B final value follows STAB then dual-type effectiveness"),
		Calculated.Damage, static_cast<int32>(ExpectedDamage));
	TestEqual(TEXT("Calculator damage equals the engine Damage mutation"),
		Calculated.Damage, static_cast<int32>(Damage->GetNumericBefore().GetValue()
			- Damage->GetNumericAfter().GetValue()));
	TestEqual(TEXT("Damage and HPChanged report the same HP delta"),
		Damage->GetNumericDelta(), HPChanged->GetNumericDelta());
	const FBattlePartyEntrySetup* FinalDefender = Evidence.Replay.GetFinalSnapshot().FindBattler(
		MakeNumericId<FBattlerId>(21));
	TestTrue(TEXT("Engine final HP matches public damage"),
		FinalDefender != nullptr
		&& FinalDefender->CurrentHP == Defender->CurrentHP - Calculated.Damage);
	TestTrue(TEXT("Inactive Blaze is not spuriously revealed"),
		CountEvents(Evidence.Replay, EBattleEventType::AbilityActivated) == 0);

	FBattleFinalDamageInput Compound;
	Compound.AttackerLevel = 50;
	Compound.AttackerStats = Attacker->Stats;
	Compound.AttackerStats.Attack = 200;
	Compound.DefenderStats = Defender->Stats;
	Compound.DefenderStats.Defense = 100;
	Compound.AttackerStages.ApplyChange(EBattleStat::Attack, -2);
	Compound.DefenderStages.ApplyChange(EBattleStat::Defense, 2);
	Compound.MoveCategory = EBattleMoveCategory::Physical;
	Compound.MovePower = 100;
	Compound.bSpreadAcrossMultipleTargets = true;
	Compound.WeatherModifierQ12 = 6144;
	Compound.bCritical = true;
	Compound.StabModifierQ12 = 6144;
	Compound.TypeEffectiveness = {1, 2};
	Compound.bAttackerBurned = true;
	const FBattleDamageModifier Terrain = MakeModifier(TEXT("Condition.GrassyTerrain"), 5325);
	const FBattleDamageModifier HelpingHand = MakeModifier(TEXT("Move.HelpingHand"), 6144);
	const FBattleDamageModifier Overgrow = MakeModifier(TEXT("Ability.Overgrow"), 6144);
	const FBattleDamageModifier Snow = MakeModifier(TEXT("Condition.Snow"), 6144);
	const FBattleDamageModifier DefensiveHook = MakeModifier(TEXT("Condition.AuroraVeil"), 2048);
	const FBattleDamageModifier Screen = MakeModifier(TEXT("Condition.Reflect"), 2048, true);
	const FBattleDamageModifier LifeOrb = MakeModifier(TEXT("Item.LifeOrb"), 5324);
	Compound.PowerModifiers = {Terrain, HelpingHand};
	Compound.OffensiveStatModifiers = {Overgrow};
	Compound.DirectDefensiveStatModifiers = {Snow};
	Compound.DefensiveStatModifiers = {DefensiveHook};
	Compound.FinalModifiers = {Screen, LifeOrb};
	Compound.RandomContext = Input.RandomContext;
	BattleTest::FStrictBattleRandom CompoundRandom({{0, 15, 15, DamageDraw->RulePurpose}});
	FBattleFinalDamageResult CompoundResult;
	if (!TestTrue(TEXT("Every B00B modifier phase resolves through the public calculator"),
		FBattleFinalDamageCalculator::TryCalculateFinalDamage(
			Compound, CompoundRandom, CompoundResult, CalculationError))) return false;
	const TArray<FBattleDamageTraceEntry> ExpectedCompoundTrace = {
		TraceEntry(EBattleDamageTraceStep::InputPower, 100),
		TraceEntry(EBattleDamageTraceStep::PowerModifierChain, 5325, Terrain.RuleId),
		TraceEntry(EBattleDamageTraceStep::PowerModifierChain, 7988, HelpingHand.RuleId),
		TraceEntry(EBattleDamageTraceStep::EffectivePower, 195),
		TraceEntry(EBattleDamageTraceStep::OffensiveStageInput, -2),
		TraceEntry(EBattleDamageTraceStep::OffensiveStageUsed, 0),
		TraceEntry(EBattleDamageTraceStep::StagedOffensiveStat, 200),
		TraceEntry(EBattleDamageTraceStep::DefensiveStageInput, 2),
		TraceEntry(EBattleDamageTraceStep::DefensiveStageUsed, 0),
		TraceEntry(EBattleDamageTraceStep::StagedDefensiveStat, 100),
		TraceEntry(EBattleDamageTraceStep::DirectDefensiveStat, 150, Snow.RuleId),
		TraceEntry(EBattleDamageTraceStep::OffensiveModifierChain, 6144, Overgrow.RuleId),
		TraceEntry(EBattleDamageTraceStep::DefensiveModifierChain, 2048, DefensiveHook.RuleId),
		TraceEntry(EBattleDamageTraceStep::EffectiveOffensiveStat, 300),
		TraceEntry(EBattleDamageTraceStep::EffectiveDefensiveStat, 75),
		TraceEntry(EBattleDamageTraceStep::LevelTerm, 22),
		TraceEntry(EBattleDamageTraceStep::PowerTerm, 4290),
		TraceEntry(EBattleDamageTraceStep::AttackTerm, 1287000),
		TraceEntry(EBattleDamageTraceStep::Quotient, 17160),
		TraceEntry(EBattleDamageTraceStep::BaseDamage, 345),
		TraceEntry(EBattleDamageTraceStep::SpreadDamage, 259),
		TraceEntry(EBattleDamageTraceStep::WeatherDamage, 388),
		TraceEntry(EBattleDamageTraceStep::CriticalDamage, 582),
		TraceEntry(EBattleDamageTraceStep::RandomRoll, 15),
		TraceEntry(EBattleDamageTraceStep::RandomFactor, 85),
		TraceEntry(EBattleDamageTraceStep::RandomDamage, 494),
		TraceEntry(EBattleDamageTraceStep::StabDamage, 741),
		TraceEntry(EBattleDamageTraceStep::TypeEffectivenessNumerator, 1),
		TraceEntry(EBattleDamageTraceStep::TypeEffectivenessDenominator, 2),
		TraceEntry(EBattleDamageTraceStep::TypeDamage, 370),
		TraceEntry(EBattleDamageTraceStep::BurnDamage, 185),
		TraceEntry(EBattleDamageTraceStep::FinalModifierIgnored, 2048, Screen.RuleId),
		TraceEntry(EBattleDamageTraceStep::FinalModifierChain, 5324, LifeOrb.RuleId),
		TraceEntry(EBattleDamageTraceStep::FinalModifierClamped, 5324),
		TraceEntry(EBattleDamageTraceStep::FinalDamage, 240)};
	TestExactTrace(*this, TEXT("B00B all-phase public trace"),
		CompoundResult.Trace, ExpectedCompoundTrace);
	TestTrue(TEXT("The all-phase calculator consumed only the scripted damage draw"),
		CompoundRandom.IsExact());

	FBattleSetup LevitateControlSetup;
	FBattleSetup MoldBreakerSetup;
	if (!TryBuildSetup(Fixture.Catalog, MakeLevitateBypassSpec(11302, false), LevitateControlSetup, Error)
		|| !TryBuildSetup(Fixture.Catalog, MakeLevitateBypassSpec(11303, true), MoldBreakerSetup, Error))
	{
		AddError(Error);
		return false;
	}
	const FChoiceProvider LevitateProvider = [](const FBattleDecisionRequest& Request, FChoice& Out, FString&)
	{
		const bool bPlayer = Request.GetDecisionOwnerTrainerId() == MakeNumericId<FTrainerId>(1);
		Out.Kind = EChoiceKind::Fight;
		Out.DefinitionId = bPlayer ? FName(TEXT("Move.Earthquake")) : FName(TEXT("Move.SwordsDance"));
		Out.ActiveTarget = MakeActiveSlotId(
			bPlayer ? EBattleSide::Opponent : EBattleSide::Player, EBattlePosition::Left);
		return true;
	};
	const FDriveFunction LevitateDrive = [LevitateProvider](
		FBattleEngine& Engine, FRunEvidence& RunEvidence, FString& DriveError)
	{
		return LockTurn(Engine, LevitateProvider, RunEvidence, DriveError)
			&& ExecuteLockedQueue(Engine, RunEvidence, DriveError);
	};
	FRunEvidence LevitateControl;
	FRunEvidence MoldBreaker;
	if (!RunDeterministicTwins(*this, TEXT("Levitate Ground immunity control"),
			Fixture.Catalog, LevitateControlSetup, LevitateDrive, &LevitateControl)
		|| !RunDeterministicTwins(*this, TEXT("Mold Breaker ignores Levitate"),
			Fixture.Catalog, MoldBreakerSetup, LevitateDrive, &MoldBreaker)) return false;
	const FBattlePartyEntrySetup* ControlBefore = LevitateControlSetup.FindBattler(
		MakeNumericId<FBattlerId>(21));
	const FBattlePartyEntrySetup* ControlAfter = LevitateControl.Replay.GetFinalSnapshot().FindBattler(
		MakeNumericId<FBattlerId>(21));
	const FBattlePartyEntrySetup* BypassBefore = MoldBreakerSetup.FindBattler(
		MakeNumericId<FBattlerId>(21));
	const FBattlePartyEntrySetup* BypassAfter = MoldBreaker.Replay.GetFinalSnapshot().FindBattler(
		MakeNumericId<FBattlerId>(21));
	TestTrue(TEXT("Levitate publicly blocks the Ground control without HP mutation"),
		ControlBefore != nullptr && ControlAfter != nullptr
		&& ControlBefore->CurrentHP == ControlAfter->CurrentHP
		&& FindTargetedEvent(LevitateControl.Replay, EBattleEventType::Immunity, 21) != nullptr
		&& FindTargetedEvent(LevitateControl.Replay, EBattleEventType::Damage, 21) == nullptr
		&& HasSourceDefinition(LevitateControl.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.Levitate")));
	TestTrue(TEXT("Mold Breaker makes the same Ground path damage the Levitate target"),
		BypassBefore != nullptr && BypassAfter != nullptr
		&& BypassAfter->CurrentHP < BypassBefore->CurrentHP
		&& FindTargetedEvent(MoldBreaker.Replay, EBattleEventType::Damage, 21) != nullptr
		&& FindTargetedEvent(MoldBreaker.Replay, EBattleEventType::Immunity, 21) == nullptr
		&& HasSourceDefinition(MoldBreaker.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.MoldBreaker"))
		&& !HasSourceDefinition(MoldBreaker.Replay, EBattleEventType::AbilityActivated, TEXT("Ability.Levitate")));
	return ValidateGlobalInvariants(*this, Fixture.Catalog, Evidence, TEXT("B00B modifier"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, LevitateControl, TEXT("Levitate Ground control"))
		&& ValidateGlobalInvariants(*this, Fixture.Catalog, MoldBreaker, TEXT("Mold Breaker Levitate bypass"));
}

} // namespace BattleCanonicalModifierPrivate

#endif // WITH_DEV_AUTOMATION_TESTS
