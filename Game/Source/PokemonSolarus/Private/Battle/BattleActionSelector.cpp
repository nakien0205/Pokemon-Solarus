#include "Battle/BattleActionSelector.h"

bool FBattleActionSelectorInput::TryCreate(
	const FBattleSnapshot& FilteredObservation,
	const int32 RequestIndex,
	FBattleActionSelectorInput& OutInput,
	FBattleRejection& OutRejection)
{
	OutInput = FBattleActionSelectorInput();
	OutRejection = FBattleRejection();

	if (!FilteredObservation.IsValid()
		|| !FilteredObservation.IsObserverFiltered()
		|| !FilteredObservation.GetObserverTrainerId().IsValid())
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}

	const TConstArrayView<FBattleDecisionRequest> Requests =
		FilteredObservation.GetPendingDecisionRequests();
	if (!Requests.IsValidIndex(RequestIndex))
	{
		OutRejection.Reason = EBattleRejectionReason::NoPendingDecision;
		return false;
	}

	const FBattleDecisionRequest& Request = Requests[RequestIndex];
	if (!Request.IsValid())
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		return false;
	}
	if (Request.GetDecisionOwnerTrainerId() != FilteredObservation.GetObserverTrainerId())
	{
		OutRejection.Reason = EBattleRejectionReason::WrongDecisionOwner;
		OutRejection.TrainerId = Request.GetDecisionOwnerTrainerId();
		return false;
	}
	if (Request.GetStateVersion() != FilteredObservation.GetStateVersion())
	{
		OutRejection.Reason = EBattleRejectionReason::StaleStateVersion;
		return false;
	}

	OutInput.bValid = true;
	OutInput.Observation = FilteredObservation;
	OutInput.LegalActionRequestIndex = RequestIndex;
	return true;
}

const FBattleDecisionRequest& FBattleActionSelectorInput::GetLegalActions() const
{
	check(bValid);
	check(Observation.GetPendingDecisionRequests().IsValidIndex(LegalActionRequestIndex));
	return Observation.GetPendingDecisionRequests()[LegalActionRequestIndex];
}

bool FBattleActionSelectorBoundary::TrySelectLegalAction(
	IBattleActionSelector& Selector,
	const FBattleActionSelectorInput& Input,
	FBattleDecision& OutDecision,
	FBattleRejection& OutRejection)
{
	OutDecision = FBattleDecision();
	OutRejection = FBattleRejection();
	if (!Input.IsValid())
	{
		OutRejection.Reason = EBattleRejectionReason::InvalidSetup;
		return false;
	}

	if (!Selector.TrySelectAction(Input, OutDecision, OutRejection))
	{
		if (!OutRejection.IsRejected())
		{
			OutRejection.Reason = EBattleRejectionReason::InvalidDecision;
		}
		OutDecision = FBattleDecision();
		return false;
	}

	if (!Input.GetLegalActions().Allows(OutDecision, OutRejection))
	{
		OutDecision = FBattleDecision();
		return false;
	}
	return true;
}
