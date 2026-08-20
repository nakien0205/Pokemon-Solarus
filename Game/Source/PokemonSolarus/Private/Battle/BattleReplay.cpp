#include "Battle/BattleReplay.h"

#include "Containers/StringConv.h"

namespace
{
	class FCanonicalReplayWriter
	{
	public:
		explicit FCanonicalReplayWriter(TArray<uint8>& InBytes)
			: Bytes(InBytes)
		{
		}

		bool IsValid() const { return bValid; }

		void WriteU8(const uint8 Value)
		{
			Bytes.Add(Value);
		}

		void WriteBool(const bool Value)
		{
			WriteU8(Value ? 1 : 0);
		}

		void WriteU16(const uint16 Value)
		{
			WriteU8(static_cast<uint8>((Value >> 8U) & 0xFFU));
			WriteU8(static_cast<uint8>(Value & 0xFFU));
		}

		void WriteU32(const uint32 Value)
		{
			for (int32 Shift = 24; Shift >= 0; Shift -= 8)
			{
				WriteU8(static_cast<uint8>((Value >> Shift) & 0xFFU));
			}
		}

		void WriteI32(const int32 Value)
		{
			WriteU32(static_cast<uint32>(Value));
		}

		void WriteU64(const uint64 Value)
		{
			for (int32 Shift = 56; Shift >= 0; Shift -= 8)
			{
				WriteU8(static_cast<uint8>((Value >> Shift) & 0xFFULL));
			}
		}

		void WriteI64(const int64 Value)
		{
			WriteU64(static_cast<uint64>(Value));
		}

		void WriteCount(const int32 Count)
		{
			if (Count < 0)
			{
				bValid = false;
				return;
			}
			WriteU32(static_cast<uint32>(Count));
		}

		void WriteDefinitionId(const FDefinitionId& Id)
		{
			WriteBool(Id.IsValid());
			if (!Id.IsValid())
			{
				return;
			}

			const FString Canonical = Id.GetName().ToString().ToLower();
			const FTCHARToUTF8 Converted(*Canonical);
			if (Converted.Length() < 0)
			{
				bValid = false;
				return;
			}
			WriteU32(static_cast<uint32>(Converted.Length()));
			Bytes.Append(reinterpret_cast<const uint8*>(Converted.Get()), Converted.Length());
		}

		template <typename TypedId>
		void WriteTypedDefinitionId(const TypedId& Id)
		{
			WriteDefinitionId(Id.GetDefinitionId());
		}

		template <typename NumericId>
		void WriteNumericId(const NumericId& Id)
		{
			WriteU64(Id.GetValue());
		}

		void WritePartySlot(const FPartySlotId Slot)
		{
			WriteU8(Slot.GetIndex());
		}

		void WriteActiveSlot(const FActiveSlotId Slot)
		{
			WriteBool(Slot.IsValid());
			if (Slot.IsValid())
			{
				WriteU8(static_cast<uint8>(Slot.GetSide()));
				WriteU8(static_cast<uint8>(Slot.GetPosition()));
			}
		}

		void WriteReference(const FBattleSnapshotReference& Reference)
		{
			WriteDefinitionId(Reference.SnapshotId);
			WriteU32(Reference.SchemaVersion);
		}

		void WriteStats(const FPokemonBattleStats& Stats)
		{
			WriteI32(Stats.MaxHP);
			WriteI32(Stats.Attack);
			WriteI32(Stats.Defense);
			WriteI32(Stats.SpecialAttack);
			WriteI32(Stats.SpecialDefense);
			WriteI32(Stats.Speed);
		}

		void WriteBagItem(const FBattleBagItemCount& Item)
		{
			WriteTypedDefinitionId(Item.ItemId);
			WriteI32(Item.Count);
		}

		void WriteTrainer(const FBattleTrainerSetup& Trainer)
		{
			WriteNumericId(Trainer.TrainerId);
			WriteU8(static_cast<uint8>(Trainer.Side));
			WriteU8(static_cast<uint8>(Trainer.Role));
			WriteU8(static_cast<uint8>(Trainer.Controller));
			WriteDefinitionId(Trainer.SelectorProfileId);
			WriteCount(Trainer.Bag.Num());
			for (const FBattleBagItemCount& Item : Trainer.Bag)
			{
				WriteBagItem(Item);
			}
		}

		void WriteMoveSlot(const FBattleMoveSlotSetup& Move)
		{
			WriteU8(Move.SlotIndex);
			WriteTypedDefinitionId(Move.MoveId);
			WriteI32(Move.CurrentPP);
			WriteI32(Move.MaxPP);
		}

		void WritePartyEntry(const FBattlePartyEntrySetup& Entry)
		{
			WriteNumericId(Entry.TrainerId);
			WriteNumericId(Entry.BattlerId);
			WriteNumericId(Entry.SourcePokemonId);
			WritePartySlot(Entry.PartySlotId);
			WriteTypedDefinitionId(Entry.SpeciesFormId);
			WriteI32(Entry.Level);
			WriteStats(Entry.Stats);
			WriteI32(Entry.CurrentHP);
			WriteBool(Entry.bEgg);
			WriteTypedDefinitionId(Entry.AbilityId);
			WriteTypedDefinitionId(Entry.OriginalHeldItemId);
			WriteTypedDefinitionId(Entry.CurrentHeldItemId);
			WriteCount(Entry.Moves.Num());
			for (const FBattleMoveSlotSetup& Move : Entry.Moves)
			{
				WriteMoveSlot(Move);
			}
		}

		void WriteActiveAssignment(const FBattleActiveAssignment& Assignment)
		{
			WriteActiveSlot(Assignment.ActiveSlotId);
			WriteNumericId(Assignment.TrainerId);
			WriteNumericId(Assignment.BattlerId);
		}

		void WriteKnowledge(const FBattleKnowledgeFact& Fact)
		{
			WriteNumericId(Fact.ObserverTrainerId);
			WriteNumericId(Fact.SubjectBattlerId);
			WriteU8(static_cast<uint8>(Fact.Kind));
			WriteDefinitionId(Fact.DefinitionId);
			WriteU8(static_cast<uint8>(Fact.Visibility));
		}

		void WriteObedience(const FBattleObedienceInput& Input)
		{
			WriteNumericId(Input.BattlerId);
			WriteBool(Input.bSubjectToPlayerCap);
			WriteU8(Input.ReferenceLevel);
			WriteU8(Input.BadgeCount);
		}

		void WritePolicies(const FBattleEncounterPolicies& Policies)
		{
			WriteBool(Policies.bRunAllowed);
			WriteBool(Policies.bCaptureAllowed);
			WriteBool(Policies.bBagAllowed);
			WriteBool(Policies.bShiftPromptEligible);
			WriteU8(static_cast<uint8>(Policies.WildFleeMode));
			WriteU32(Policies.WildFleeNumerator);
			WriteU32(Policies.WildFleeDenominator);
		}

		void WriteSetup(const FBattleSetup& Setup)
		{
			WriteBool(Setup.IsValid());
			WriteNumericId(Setup.GetBattleId());
			WriteReference(Setup.GetSettingsReference());
			WriteReference(Setup.GetCatalogReference());
			WriteU8(static_cast<uint8>(Setup.GetEncounterKind()));
			WriteU8(static_cast<uint8>(Setup.GetFormat()));

			WriteCount(Setup.GetTrainers().Num());
			for (const FBattleTrainerSetup& Trainer : Setup.GetTrainers())
			{
				WriteTrainer(Trainer);
			}
			WriteCount(Setup.GetPartyEntries().Num());
			for (const FBattlePartyEntrySetup& Entry : Setup.GetPartyEntries())
			{
				WritePartyEntry(Entry);
			}
			WriteCount(Setup.GetStartingActive().Num());
			for (const FBattleActiveAssignment& Assignment : Setup.GetStartingActive())
			{
				WriteActiveAssignment(Assignment);
			}

			WriteI32(Setup.GetCaptureCapacity().PartySlotsRemaining);
			WriteI32(Setup.GetCaptureCapacity().StorageSlotsRemaining);
			WriteCount(Setup.GetKnowledgeFacts().Num());
			for (const FBattleKnowledgeFact& Fact : Setup.GetKnowledgeFacts())
			{
				WriteKnowledge(Fact);
			}
			WriteCount(Setup.GetObedienceInputs().Num());
			for (const FBattleObedienceInput& Obedience : Setup.GetObedienceInputs())
			{
				WriteObedience(Obedience);
			}
			WritePolicies(Setup.GetPolicies());
		}

		void WriteDecision(const FBattleDecision& Decision)
		{
			WriteBool(Decision.IsValid());
			WriteU64(Decision.GetStateVersion());
			WriteU8(static_cast<uint8>(Decision.GetRequestKind()));
			WriteNumericId(Decision.GetDecisionOwnerTrainerId());
			WriteNumericId(Decision.GetActingBattlerId());
			WriteU8(static_cast<uint8>(Decision.GetActionKind()));
			WriteTypedDefinitionId(Decision.GetMoveId());
			WritePartySlot(Decision.GetSwitchPartySlotId());
			WriteTypedDefinitionId(Decision.GetItemId());
			WritePartySlot(Decision.GetItemPartyTargetId());
			WriteActiveSlot(Decision.GetActiveTargetId());
		}

		void WriteRefresh(const FBattleBetweenActionsStatRefresh& Refresh)
		{
			WriteU64(Refresh.StateVersion);
			WriteU64(Refresh.OpponentRemovalCheckpointEventOrdinal);
			WriteNumericId(Refresh.BattlerId);
			WriteI32(Refresh.NewLevel);
			WriteStats(Refresh.NewStats);
			WriteI32(Refresh.NewCurrentHP);
		}

		void WriteRejection(const FBattleRejection& Rejection)
		{
			WriteU8(static_cast<uint8>(Rejection.Reason));
			WriteNumericId(Rejection.TrainerId);
			WriteNumericId(Rejection.BattlerId);
			WriteNumericId(Rejection.ActionId);
			WriteTypedDefinitionId(Rejection.MoveId);
			WriteTypedDefinitionId(Rejection.ItemId);
			WritePartySlot(Rejection.PartySlotId);
			WriteActiveSlot(Rejection.ActiveSlotId);
		}

		void WriteRequest(const FBattleDecisionRequest& Request)
		{
			WriteBool(Request.IsValid());
			WriteU64(Request.GetStateVersion());
			WriteU8(static_cast<uint8>(Request.GetRequestKind()));
			WriteNumericId(Request.GetDecisionOwnerTrainerId());
			WriteNumericId(Request.GetActingBattlerId());
			WriteActiveSlot(Request.GetActingSlotId());
			WriteCount(Request.GetLegalActionKinds().Num());
			for (const EBattleActionKind Action : Request.GetLegalActionKinds())
			{
				WriteU8(static_cast<uint8>(Action));
			}
			WriteCount(Request.GetLegalMoveIds().Num());
			for (const FMoveId& MoveId : Request.GetLegalMoveIds())
			{
				WriteTypedDefinitionId(MoveId);
			}
			WriteCount(Request.GetLegalSwitchPartySlots().Num());
			for (const FPartySlotId PartySlot : Request.GetLegalSwitchPartySlots())
			{
				WritePartySlot(PartySlot);
			}
			WriteCount(Request.GetLegalItemIds().Num());
			for (const FItemId& ItemId : Request.GetLegalItemIds())
			{
				WriteTypedDefinitionId(ItemId);
			}
			WriteCount(Request.GetLegalActiveTargets().Num());
			for (const FActiveSlotId ActiveSlot : Request.GetLegalActiveTargets())
			{
				WriteActiveSlot(ActiveSlot);
			}
			WriteCount(Request.GetLegalPartyTargets().Num());
			for (const FPartySlotId PartySlot : Request.GetLegalPartyTargets())
			{
				WritePartySlot(PartySlot);
			}
		}

		void WriteOptionalI64(const TOptional<int64>& Value)
		{
			WriteBool(Value.IsSet());
			if (Value.IsSet())
			{
				WriteI64(Value.GetValue());
			}
		}

		void WriteOptionalU64(const TOptional<uint64>& Value)
		{
			WriteBool(Value.IsSet());
			if (Value.IsSet())
			{
				WriteU64(Value.GetValue());
			}
		}

		void WriteOptionalU16(const TOptional<uint16>& Value)
		{
			WriteBool(Value.IsSet());
			if (Value.IsSet())
			{
				WriteU16(Value.GetValue());
			}
		}

		void WriteEvent(const FBattleEvent& Event)
		{
			WriteBool(Event.IsValid());
			WriteU64(Event.GetEventOrdinal());
			WriteNumericId(Event.GetBattleId());
			WriteNumericId(Event.GetTurnId());
			WriteNumericId(Event.GetActionId());
			WriteNumericId(Event.GetResolutionId());
			WriteU8(static_cast<uint8>(Event.GetType()));
			WriteU8(static_cast<uint8>(Event.GetCause()));
			WriteU8(static_cast<uint8>(Event.GetCauseActionKind()));
			WriteU8(static_cast<uint8>(Event.GetOutcomeCause()));
			WriteNumericId(Event.GetSource().TrainerId);
			WriteNumericId(Event.GetSource().BattlerId);
			WriteActiveSlot(Event.GetSource().ActiveSlotId);
			WriteDefinitionId(Event.GetSource().DefinitionId);
			WriteCount(Event.GetTargets().Num());
			for (const FBattleEventTarget& Target : Event.GetTargets())
			{
				WriteNumericId(Target.TrainerId);
				WriteNumericId(Target.BattlerId);
				WriteActiveSlot(Target.ActiveSlotId);
			}
			WriteOptionalI64(Event.GetNumericBefore());
			WriteOptionalI64(Event.GetNumericAfter());
			WriteOptionalI64(Event.GetNumericDelta());
			WriteOptionalU64(Event.GetSimultaneousGroupId());
			WriteOptionalU16(Event.GetHitIndex());
			WriteOptionalU16(Event.GetHitCount());
			WriteU8(static_cast<uint8>(Event.GetVisibility().Level));
			WriteNumericId(Event.GetVisibility().OwningTrainerId);
			WriteU8(static_cast<uint8>(Event.GetVisibility().OwningSide));
			WriteBool(Event.GetVisibility().bHasOwningSide);
			WriteBool(Event.GetVisibility().bRevealSourceDefinition);
		}

		void WriteResolution(const FBattleResolution& Resolution)
		{
			WriteBool(Resolution.IsValid());
			WriteNumericId(Resolution.GetResolutionId());
			WriteU64(Resolution.GetBeforeStateVersion());
			WriteU64(Resolution.GetAfterStateVersion());
			WriteBool(Resolution.WasAccepted());
			WriteRejection(Resolution.GetRejection());
			WriteCount(Resolution.GetEvents().Num());
			for (const FBattleEvent& Event : Resolution.GetEvents())
			{
				WriteEvent(Event);
			}
		}

		void WriteRandomDraw(const FBattleRandomDraw& Draw)
		{
			WriteU32(Draw.InclusiveMinimum);
			WriteU32(Draw.InclusiveMaximum);
			WriteU64(Draw.Bound);
			WriteU64(Draw.RawValue);
			WriteU32(Draw.Result);
			WriteU64(Draw.CallOrdinal);
			WriteNumericId(Draw.BattleId);
			WriteNumericId(Draw.TurnId);
			WriteNumericId(Draw.ActionId);
			WriteNumericId(Draw.ResolutionId);
			WriteDefinitionId(Draw.RulePurpose);
		}

		void WriteSnapshot(const FBattleSnapshot& Snapshot)
		{
			WriteBool(Snapshot.IsValid());
			WriteU64(Snapshot.GetStateVersion());
			WriteNumericId(Snapshot.GetBattleId());
			WriteNumericId(Snapshot.GetTurnId());
			WriteU8(static_cast<uint8>(Snapshot.GetPhase()));
			WriteU8(static_cast<uint8>(Snapshot.GetOutcome()));
			WriteU8(static_cast<uint8>(Snapshot.GetOutcomeCause()));
			WriteReference(Snapshot.GetSettingsReference());
			WriteReference(Snapshot.GetCatalogReference());
			WriteCount(Snapshot.GetTrainers().Num());
			for (const FBattleTrainerSetup& Trainer : Snapshot.GetTrainers())
			{
				WriteTrainer(Trainer);
			}
			WriteCount(Snapshot.GetPartyEntries().Num());
			for (const FBattlePartyEntrySetup& Entry : Snapshot.GetPartyEntries())
			{
				WritePartyEntry(Entry);
			}
			WriteCount(Snapshot.GetActiveAssignments().Num());
			for (const FBattleActiveAssignment& Assignment : Snapshot.GetActiveAssignments())
			{
				WriteActiveAssignment(Assignment);
			}
			const TOptional<FBattleDecisionRequest> Pending = Snapshot.GetPendingDecision();
			WriteBool(Pending.IsSet());
			if (Pending.IsSet())
			{
				WriteRequest(Pending.GetValue());
			}
		}

	private:
		TArray<uint8>& Bytes;
		bool bValid = true;
	};
}

bool FBattleReplayRecord::TryCreate(
	const uint32 InSchemaVersion,
	const FBattleReplayInputs& InInputs,
	const TConstArrayView<FBattleResolution> InResolutions,
	const TConstArrayView<FBattleRandomDraw> InRandomTrace,
	const FBattleSnapshot& InFinalSnapshot,
	FBattleReplayRecord& OutRecord)
{
	OutRecord = FBattleReplayRecord();
	if (InSchemaVersion != CurrentSchemaVersion
		|| !InInputs.Setup.IsValid()
		|| !InFinalSnapshot.IsValid()
		|| InFinalSnapshot.GetBattleId() != InInputs.Setup.GetBattleId())
	{
		return false;
	}
	for (const FBattleDecision& Decision : InInputs.Decisions)
	{
		if (!Decision.IsValid())
		{
			return false;
		}
	}
	for (const FBattleBetweenActionsStatRefresh& Refresh : InInputs.StatRefreshes)
	{
		if (!Refresh.IsValid())
		{
			return false;
		}
	}
	for (const FBattleResolution& Resolution : InResolutions)
	{
		if (!Resolution.IsValid())
		{
			return false;
		}
	}
	uint64 PreviousCallOrdinal = 0;
	for (const FBattleRandomDraw& Draw : InRandomTrace)
	{
		if (Draw.CallOrdinal == 0
			|| Draw.CallOrdinal <= PreviousCallOrdinal
			|| Draw.BattleId != InInputs.Setup.GetBattleId())
		{
			return false;
		}
		PreviousCallOrdinal = Draw.CallOrdinal;
	}

	OutRecord.bValid = true;
	OutRecord.SchemaVersion = InSchemaVersion;
	OutRecord.Inputs = InInputs;
	for (const FBattleResolution& Resolution : InResolutions)
	{
		OutRecord.Resolutions.Add(Resolution);
	}
	for (const FBattleRandomDraw& Draw : InRandomTrace)
	{
		OutRecord.RandomTrace.Add(Draw);
	}
	OutRecord.FinalSnapshot = InFinalSnapshot;
	return true;
}

bool FBattleReplaySerializer::TrySerializeCanonical(
	const FBattleReplayRecord& Record,
	TArray<uint8>& OutBytes,
	FBattleRejection& OutRejection)
{
	OutBytes.Reset();
	OutRejection = FBattleRejection();
	if (!Record.IsValid() || Record.GetSchemaVersion() != FBattleReplayRecord::CurrentSchemaVersion)
	{
		OutRejection.Reason = EBattleRejectionReason::SerializationFailure;
		return false;
	}

	FCanonicalReplayWriter Writer(OutBytes);
	Writer.WriteU8(static_cast<uint8>('P'));
	Writer.WriteU8(static_cast<uint8>('S'));
	Writer.WriteU8(static_cast<uint8>('B'));
	Writer.WriteU8(static_cast<uint8>('R'));
	Writer.WriteU32(Record.GetSchemaVersion());
	Writer.WriteSetup(Record.GetInputs().Setup);
	Writer.WriteCount(Record.GetInputs().Decisions.Num());
	for (const FBattleDecision& Decision : Record.GetInputs().Decisions)
	{
		Writer.WriteDecision(Decision);
	}
	Writer.WriteCount(Record.GetInputs().StatRefreshes.Num());
	for (const FBattleBetweenActionsStatRefresh& Refresh : Record.GetInputs().StatRefreshes)
	{
		Writer.WriteRefresh(Refresh);
	}
	Writer.WriteCount(Record.GetResolutions().Num());
	for (const FBattleResolution& Resolution : Record.GetResolutions())
	{
		Writer.WriteResolution(Resolution);
	}
	Writer.WriteCount(Record.GetRandomTrace().Num());
	for (const FBattleRandomDraw& Draw : Record.GetRandomTrace())
	{
		Writer.WriteRandomDraw(Draw);
	}
	Writer.WriteSnapshot(Record.GetFinalSnapshot());

	if (!Writer.IsValid())
	{
		OutBytes.Reset();
		OutRejection.Reason = EBattleRejectionReason::SerializationFailure;
		return false;
	}
	return true;
}
