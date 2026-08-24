#include "UI/BattlePresentationAdapter.h"

#include "Battle/BattleDisplayNameResolver.h"
#include "Battle/BattleSnapshot.h"
#include "UI/BattleCommandWidget.h"
#include "UI/BattleHUDDisplayState.h"

#define LOCTEXT_NAMESPACE "BattlePresentationAdapter"

namespace
{
	bool IsCommandAction(const EBattleActionKind ActionKind)
	{
		return ActionKind == EBattleActionKind::Fight
			|| ActionKind == EBattleActionKind::Bag
			|| ActionKind == EBattleActionKind::Switch
			|| ActionKind == EBattleActionKind::Run;
	}

	const TCHAR* GetCommandName(const EBattleActionKind ActionKind)
	{
		switch (ActionKind)
		{
		case EBattleActionKind::Fight:
			return TEXT("Fight");
		case EBattleActionKind::Bag:
			return TEXT("Bag");
		case EBattleActionKind::Switch:
			return TEXT("Pokemon");
		case EBattleActionKind::Run:
			return TEXT("Run");
		default:
			return TEXT("Unknown");
		}
	}

	bool TryMapFightReason(
		const EBattleOptionUnavailableReason Reason,
		FText& OutText)
	{
		if (Reason == EBattleOptionUnavailableReason::NoPP)
		{
			OutText = LOCTEXT("FightNoPP", "No moves can be used.");
			return true;
		}
		if (Reason == EBattleOptionUnavailableReason::NoLegalTarget)
		{
			OutText = LOCTEXT("FightNoLegalTarget", "There is no target for a move.");
			return true;
		}
		return false;
	}

	bool TryMapBagReason(
		const EBattleOptionUnavailableReason Reason,
		FText& OutText)
	{
		switch (Reason)
		{
		case EBattleOptionUnavailableReason::NoItemRemaining:
			OutText = LOCTEXT("BagNoItemRemaining", "There are no usable items.");
			return true;
		case EBattleOptionUnavailableReason::BagRestricted:
			OutText = LOCTEXT("BagRestricted", "The Bag cannot be used right now.");
			return true;
		case EBattleOptionUnavailableReason::CaptureRestricted:
			OutText = LOCTEXT("BagCaptureRestricted", "Poké Balls cannot be used in this battle.");
			return true;
		case EBattleOptionUnavailableReason::NoLegalTarget:
			OutText = LOCTEXT("BagNoLegalTarget", "No item can be used right now.");
			return true;
		default:
			return false;
		}
	}

	bool TryMapSwitchReason(
		const EBattleOptionUnavailableReason Reason,
		FText& OutText)
	{
		switch (Reason)
		{
		case EBattleOptionUnavailableReason::Trapped:
			OutText = LOCTEXT("SwitchTrapped", "This Pokémon cannot switch out.");
			return true;
		case EBattleOptionUnavailableReason::SwitchRestricted:
			OutText = LOCTEXT("SwitchRestricted", "Pokémon cannot be switched right now.");
			return true;
		case EBattleOptionUnavailableReason::NoLegalTarget:
			OutText = LOCTEXT("SwitchNoLegalTarget", "There is no Pokémon available to switch.");
			return true;
		default:
			return false;
		}
	}

	bool TryValidateObserverSnapshot(
		const FBattleSnapshot& Snapshot,
		FString& OutError)
	{
		if (!Snapshot.IsValid())
		{
			OutError = TEXT("Battle snapshot is invalid.");
			return false;
		}
		if (!Snapshot.IsObserverFiltered()
			|| !Snapshot.GetObserverTrainerId().IsValid())
		{
			OutError = TEXT("Battle snapshot is not observer-filtered.");
			return false;
		}
		if (Snapshot.GetPhase() != EBattlePhase::Selecting
			|| Snapshot.GetOutcome() != EBattleOutcome::InProgress)
		{
			OutError = TEXT("Battle snapshot is not awaiting an action selection.");
			return false;
		}
		return true;
	}

	bool TryFindMatchingRequest(
		const FBattleSnapshot& Snapshot,
		const FActiveSlotId ActingSlotId,
		const FBattleDecisionRequest*& OutRequest,
		FString& OutError)
	{
		OutRequest = nullptr;
		for (const FBattleDecisionRequest& Request : Snapshot.GetPendingDecisionRequests())
		{
			if (Request.GetActingSlotId() != ActingSlotId)
			{
				continue;
			}
			if (OutRequest != nullptr)
			{
				OutError = TEXT("Multiple pending requests target the same active slot.");
				return false;
			}
			OutRequest = &Request;
		}

		if (OutRequest == nullptr)
		{
			OutError = TEXT("No pending decision request matches the acting active slot.");
			return false;
		}
		return true;
	}

	bool TryValidateActionRequest(
		const FBattleSnapshot& Snapshot,
		const FBattleDecisionRequest& Request,
		FString& OutError)
	{
		if (!Request.IsValid()
			|| Request.GetRequestKind() != EBattleDecisionRequestKind::Action
			|| Request.GetStateVersion() != Snapshot.GetStateVersion()
			|| Request.GetDecisionOwnerTrainerId() != Snapshot.GetObserverTrainerId())
		{
			OutError = TEXT("Pending action request does not match the observer snapshot.");
			return false;
		}
		return true;
	}

	bool IsMatchingActingSlot(
		const FBattleObservedActiveSlot* Slot,
		const FBattleDecisionRequest& Request,
		const FTrainerId ObserverId)
	{
		return Slot != nullptr
			&& Slot->bAvailable
			&& Slot->TrainerId == ObserverId
			&& Slot->BattlerId == Request.GetActingBattlerId();
	}

	bool IsValidObservedHealthBattler(
		const FBattleObservedActiveSlot& Slot,
		const FBattleObservedBattler* Battler)
	{
		return Slot.TrainerId.IsValid()
			&& Slot.BattlerId.IsValid()
			&& Battler != nullptr
			&& Battler->TrainerId == Slot.TrainerId
			&& Battler->SpeciesFormId.IsValid()
			&& Battler->MaxHP > 0
			&& Battler->CurrentHP >= 0
			&& Battler->CurrentHP <= Battler->MaxHP;
	}

	bool TryValidateActingBattler(
		const FBattleSnapshot& Snapshot,
		const FBattleDecisionRequest& Request,
		FString& OutError)
	{
		const FBattleObservedActiveSlot* Slot =
			Snapshot.GetObservedActiveSlots().FindByPredicate(
				[&Request](const FBattleObservedActiveSlot& Candidate)
				{
					return Candidate.ActiveSlotId == Request.GetActingSlotId();
				});
		const FBattleObservedBattler* Battler =
			Snapshot.FindObservedBattler(Request.GetActingBattlerId());
		const FTrainerId ObserverId = Snapshot.GetObserverTrainerId();
		if (!IsMatchingActingSlot(Slot, Request, ObserverId))
		{
			OutError = TEXT("Pending action request does not identify a valid observed active battler.");
			return false;
		}
		if (!IsValidObservedHealthBattler(*Slot, Battler)
			|| Battler->bFainted
			|| Battler->CurrentHP <= 0)
		{
			OutError = TEXT("Pending action request does not identify a valid observed active battler.");
			return false;
		}
		return true;
	}

	bool TryValidateCommandOptionFamilies(
		const FBattleDecisionRequest& Request,
		FString& OutError)
	{
		for (const EBattleActionKind LegalAction : Request.GetLegalActionKinds())
		{
			if (!IsCommandAction(LegalAction))
			{
				OutError = TEXT("Pending action request contains a legal action outside the command menu.");
				return false;
			}
		}
		for (const FBattleUnavailableDecisionOption& Option : Request.GetUnavailableOptions())
		{
			if (Option.Kind == EBattleDecisionOptionKind::Action
				&& !IsCommandAction(Option.ActionKind))
			{
				OutError = TEXT("Pending action request contains an unavailable action outside the command menu.");
				return false;
			}
		}
		return true;
	}

	bool TryGetCommandRequest(
		const FBattleSnapshot& Snapshot,
		const FActiveSlotId ActingSlotId,
		const FBattleDecisionRequest*& OutRequest,
		FString& OutError)
	{
		OutRequest = nullptr;
		if (!ActingSlotId.IsValid())
		{
			OutError = TEXT("Acting active slot is invalid.");
			return false;
		}
		return TryValidateObserverSnapshot(Snapshot, OutError)
			&& TryFindMatchingRequest(Snapshot, ActingSlotId, OutRequest, OutError)
			&& TryValidateActionRequest(Snapshot, *OutRequest, OutError)
			&& TryValidateActingBattler(Snapshot, *OutRequest, OutError)
			&& TryValidateCommandOptionFamilies(*OutRequest, OutError);
	}

	const FBattleUnavailableDecisionOption* FindUnavailableAction(
		const FBattleDecisionRequest& Request,
		const EBattleActionKind ActionKind,
		bool& bOutDuplicate)
	{
		const FBattleUnavailableDecisionOption* Match = nullptr;
		bOutDuplicate = false;
		for (const FBattleUnavailableDecisionOption& Option : Request.GetUnavailableOptions())
		{
			if (Option.Kind != EBattleDecisionOptionKind::Action
				|| Option.ActionKind != ActionKind)
			{
				continue;
			}
			if (Match != nullptr)
			{
				bOutDuplicate = true;
				return nullptr;
			}
			Match = &Option;
		}
		return Match;
	}

	bool TryFindSingleOccupiedSlot(
		const FBattleSnapshot& Snapshot,
		const EBattleSide Side,
		const TCHAR* DisplayLabel,
		const FBattleObservedActiveSlot*& OutSlot,
		FString& OutError)
	{
		OutSlot = nullptr;
		for (const FBattleObservedActiveSlot& Slot : Snapshot.GetObservedActiveSlots())
		{
			if (!Slot.ActiveSlotId.IsValid()
				|| Slot.ActiveSlotId.GetSide() != Side
				|| !Slot.bAvailable)
			{
				continue;
			}
			if (OutSlot != nullptr)
			{
				OutError = FString::Printf(
					TEXT("%s side has more than one occupied active slot."),
					DisplayLabel);
				return false;
			}
			OutSlot = &Slot;
		}

		if (OutSlot == nullptr)
		{
			OutError = FString::Printf(
				TEXT("%s side has no occupied active slot."), DisplayLabel);
			return false;
		}
		return true;
	}

	bool TryBuildHealthState(
		const FBattleSnapshot& Snapshot,
		const FBattleObservedActiveSlot& Slot,
		const IBattleDisplayNameResolver& Resolver,
		const TCHAR* DisplayLabel,
		FBattleHUDHealthDisplayState& OutState,
		FString& OutError)
	{
		OutState = FBattleHUDHealthDisplayState();
		const FBattleObservedBattler* Battler = Snapshot.FindObservedBattler(Slot.BattlerId);
		if (!IsValidObservedHealthBattler(Slot, Battler))
		{
			OutError = FString::Printf(TEXT("%s active battler is invalid."), DisplayLabel);
			return false;
		}

		FBattleHUDHealthDisplayState Candidate;
		if (!Resolver.TryResolveSpeciesName(Battler->SpeciesFormId, Candidate.PokemonName)
			|| Candidate.PokemonName.IsEmptyOrWhitespace())
		{
			OutError = FString::Printf(TEXT("%s species display name is unavailable."), DisplayLabel);
			return false;
		}
		Candidate.CurrentHP = Battler->CurrentHP;
		Candidate.MaxHP = Battler->MaxHP;
		OutState = MoveTemp(Candidate);
		return true;
	}

	EBattleSide GetOpposingSide(const EBattleSide Side)
	{
		return Side == EBattleSide::Player
			? EBattleSide::Opponent
			: EBattleSide::Player;
	}

	bool TryResolveHUDSlots(
		const FBattleSnapshot& Snapshot,
		const FActiveSlotId ActingSlotId,
		const FBattleObservedActiveSlot*& OutPlayerSlot,
		const FBattleObservedActiveSlot*& OutOpponentSlot,
		FString& OutError)
	{
		OutPlayerSlot = nullptr;
		OutOpponentSlot = nullptr;
		if (Snapshot.GetFormat() != EBattleFormat::Single)
		{
			OutError = TEXT("The Battle HUD currently requires a Single Battle snapshot.");
			return false;
		}

		const EBattleSide LocalSide = ActingSlotId.GetSide();
		if (!TryFindSingleOccupiedSlot(
			Snapshot, LocalSide, TEXT("Player"), OutPlayerSlot, OutError))
		{
			return false;
		}
		if (OutPlayerSlot->ActiveSlotId != ActingSlotId
			|| OutPlayerSlot->TrainerId != Snapshot.GetObserverTrainerId())
		{
			OutError = TEXT("Player active slot does not match the pending request.");
			return false;
		}
		return TryFindSingleOccupiedSlot(
			Snapshot, GetOpposingSide(LocalSide), TEXT("Opponent"),
			OutOpponentSlot, OutError);
	}
}

bool FBattlePresentationAdapter::TryMapUnavailableReason(
	const EBattleActionKind ActionKind,
	const EBattleOptionUnavailableReason Reason,
	FText& OutText)
{
	OutText = FText::GetEmpty();
	switch (ActionKind)
	{
	case EBattleActionKind::Fight:
		return TryMapFightReason(Reason, OutText);
	case EBattleActionKind::Bag:
		return TryMapBagReason(Reason, OutText);
	case EBattleActionKind::Switch:
		return TryMapSwitchReason(Reason, OutText);
	case EBattleActionKind::Run:
		if (Reason == EBattleOptionUnavailableReason::RunRestricted)
		{
			OutText = LOCTEXT("RunRestricted", "You cannot run from this battle.");
			return true;
		}
		return false;
	default:
		return false;
	}
}

bool FBattlePresentationAdapter::TryBuildCommandAvailability(
	const FBattleDecisionRequest& Request,
	const EBattleActionKind ActionKind,
	FBattleCommandAvailability& OutAvailability,
	FString& OutError)
{
	OutAvailability = FBattleCommandAvailability();
	bool bDuplicate = false;
	const FBattleUnavailableDecisionOption* Unavailable =
		FindUnavailableAction(Request, ActionKind, bDuplicate);
	if (bDuplicate)
	{
		OutError = FString::Printf(TEXT("%s has duplicate unavailable action records."),
			GetCommandName(ActionKind));
		return false;
	}

	const bool bLegal = Request.GetLegalActionKinds().Contains(ActionKind);
	if (bLegal == (Unavailable != nullptr))
	{
		OutError = FString::Printf(TEXT("%s must be exactly one of legal or unavailable."),
			GetCommandName(ActionKind));
		return false;
	}
	if (bLegal)
	{
		OutAvailability.bAvailable = true;
		return true;
	}
	if (!TryMapUnavailableReason(
		ActionKind, Unavailable->Reason, OutAvailability.UnavailableReason))
	{
		OutError = FString::Printf(TEXT("%s has unsupported unavailable reason %d."),
			GetCommandName(ActionKind), static_cast<int32>(Unavailable->Reason));
		return false;
	}
	return true;
}

bool FBattlePresentationAdapter::TryBuildCommandAvailabilities(
	const FBattleDecisionRequest& Request,
	FBattleCommandDisplayState& OutDisplayState,
	FString& OutError)
{
	return TryBuildCommandAvailability(
		Request, EBattleActionKind::Fight, OutDisplayState.Fight, OutError)
		&& TryBuildCommandAvailability(
			Request, EBattleActionKind::Bag, OutDisplayState.Bag, OutError)
		&& TryBuildCommandAvailability(
			Request, EBattleActionKind::Switch, OutDisplayState.Pokemon, OutError)
		&& TryBuildCommandAvailability(
			Request, EBattleActionKind::Run, OutDisplayState.Run, OutError);
}

bool FBattlePresentationAdapter::TryBuildCommandDisplayState(
	const FBattleSnapshot& ObserverSnapshot,
	const FActiveSlotId ActingSlotId,
	FBattleCommandDisplayState& OutDisplayState,
	FString& OutError)
{
	OutDisplayState = FBattleCommandDisplayState();
	OutError.Reset();
	const FBattleDecisionRequest* Request = nullptr;
	if (!TryGetCommandRequest(
		ObserverSnapshot, ActingSlotId, Request, OutError))
	{
		return false;
	}

	FBattleCommandDisplayState Candidate;
	Candidate.NormalPrompt = LOCTEXT("ChooseCommandPrompt", "Choose a command.");
	if (!TryBuildCommandAvailabilities(*Request, Candidate, OutError))
	{
		return false;
	}

	OutDisplayState = MoveTemp(Candidate);
	OutError.Reset();
	return true;
}

bool FBattlePresentationAdapter::TryBuildHUDDisplayState(
	const FBattleSnapshot& ObserverSnapshot,
	const FActiveSlotId ActingSlotId,
	const IBattleDisplayNameResolver& DisplayNameResolver,
	FBattleHUDDisplayState& OutDisplayState,
	FString& OutError)
{
	OutDisplayState = FBattleHUDDisplayState();
	OutError.Reset();
	FBattleHUDDisplayState Candidate;
	if (!TryBuildCommandDisplayState(
		ObserverSnapshot, ActingSlotId, Candidate.Command, OutError))
	{
		return false;
	}

	const FBattleObservedActiveSlot* PlayerSlot = nullptr;
	const FBattleObservedActiveSlot* OpponentSlot = nullptr;
	if (!TryResolveHUDSlots(
		ObserverSnapshot, ActingSlotId, PlayerSlot, OpponentSlot, OutError)
		|| !TryBuildHealthState(
			ObserverSnapshot, *PlayerSlot, DisplayNameResolver,
			TEXT("Player"), Candidate.Player, OutError)
		|| !TryBuildHealthState(
			ObserverSnapshot, *OpponentSlot, DisplayNameResolver,
			TEXT("Opponent"), Candidate.Opponent, OutError))
	{
		return false;
	}
	if (!Candidate.IsValid())
	{
		OutError = TEXT("The complete Battle HUD display state is invalid.");
		return false;
	}

	OutDisplayState = MoveTemp(Candidate);
	OutError.Reset();
	return true;
}

#undef LOCTEXT_NAMESPACE
