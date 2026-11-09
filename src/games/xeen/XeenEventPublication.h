#ifndef MMODERN_XEEN_EVENT_PUBLICATION_H
#define MMODERN_XEEN_EVENT_PUBLICATION_H
#include "games/xeen/XeenRestoreGuard.h"
#include "games/xeen/XeenEventInterpreter.h"
#include "games/xeen/XeenRegionalRules.h"
#include "games/xeen/XeenEventContinuation.h"
#include "games/xeen/XeenEventValues.h"
#include <limits>
namespace mmodern {
// A stack-bound publication capability, issued only to the live Journey continuation.
// It anticipates known effects in retained guard storage, never adopts callback state.
class XeenEventPublication {
public:
 std::function<void(unsigned)> fmEffect;
 std::function<void(unsigned)> eventVoice;
 mutable std::array<std::optional<unsigned>,30> damageEffects{};
 mutable std::uint32_t portraitEffectOwners=0; // Published spellFX recipients, presentation only.
	~XeenEventPublication() {if(retainedHistory)retainedHistory->swap(dispatchedRecords);}
	XeenEventPublication(const XeenEventPublication &) = delete;
	XeenEventPublication &operator=(const XeenEventPublication &) = delete;
	void check() const { authority(); guard.check(); }
    unsigned npcAnimationFrame() const {
        check();
        auto &world=const_cast<XeenWorld &>(guard.w);
        if(!world._sessionState._journeyRandom)integrity("NPC Journey random continuation is absent");
        XeenCombatRandom random(*world._sessionState._journeyRandom);
        const auto frame=random.draw(0,3);
        if(!frame)integrity("NPC frame conversion was rejected");
        const auto ending=random.continuation();
        guard.s._journeyRandom=ending;
        world._sessionState._journeyRandom=ending;
        guard.adoptMutationBoundary();
        return *frame;
    }
	void publishCandidate(XeenWorld &world,XeenPartyState &party,XeenCamera &camera,XeenGameFlags &flags,
			XeenWorld &candidateWorld,const XeenRestoreGuard &candidate) const {
		check();candidate.check();
		if(&world!=&guard.w || &party!=&guard.p || &camera!=&guard.c || &flags!=&guard.f ||
			&candidateWorld!=&candidate.w || !candidateWorld._detachedEventCandidate ||
			candidateWorld._sessionState._entry!=XeenEncounterEntry::Ordinary)
			integrity("Event publication owner identity changed");
		// All allocation and provider work precede the callback-free stores.
		auto presentation=world.prepareSpawnPresentation(candidateWorld);
		if (!xeen_state::sameCamera(camera, candidate.cameraValue)) {
			presentation.gpuCameraState.continuity.reset();
			presentation.gpuCameraState.observe(candidate.cameraValue);
		}
  for(unsigned owner=0;owner<30;++owner) {
   const auto &effect=candidateWorld.scenePresentation().portraits[owner];
   if(effect.damageTicks && !effect.damageDeadline)damageEffects[owner]=effect.damageFrame;
   if(effect.spellFrame<4 && !effect.spellDeadline)portraitEffectOwners|=1u<<owner;
  }
		const XeenQuestFlags questFlags(candidate.questFlags);
		world.checkTransitionTreasure(candidateWorld);
		check();candidate.check();
		guard.prepareVertigoPublication(candidate);
		world.publishTransition(candidateWorld);
		world.publishEventParty(party,candidate.p);
		party.questFlags=questFlags;
		camera=candidate.cameraValue;flags=XeenGameFlags(candidate.flagValues);
		world.scenePresentation()=std::move(presentation);
		guard.adoptMutationBoundary();
	}
	void script(const XeenEventFile &file) const {
		check();
		if (file.mapId != original.mapId || file.resourceName!=original.resourceName || file.resourcePresent != original.resourcePresent || file.records.size() != original.records.size())
			integrity("Journey event topology changed");
		for (std::size_t i=0;i<file.records.size();++i) {
			const auto &a=file.records[i]; const auto &b=original.records[i];
			if (a.x!=b.x || a.y!=b.y || a.direction!=b.direction || a.line!=b.line || a.opcode!=b.opcode || a.parameters!=b.parameters || a.fileOffset!=b.fileOffset || a.lengthField!=b.lengthField || a.followingByte!=b.followingByte)
				integrity("Journey event record changed");
		}
	}
	void continuation(const XeenEventExecutionState &state,const XeenEventContinuation &retained,bool detachedTravel=false) const {
        check();
        try {
            retained.check(state);
            // A detached Continue may suspend in its destination script. The
            // interpreter-issued preimage binds that entire script and state;
            // the live publication still validates its original entry resource.
            if(!detachedTravel || state.currentScript->file().mapId==original.mapId)
                script(state.currentScript->file());
        }
		catch(const std::logic_error &) {integrity("Event continuation or resource preimage changed");}
		// Remove may hide the initially selected object before treasure is shown.
		// Its identity belongs to the checked continuation, not a fresh selection.
		selectedObject=state.selectedObject;
	}
	void execution(const XeenEventExecutionState &state) const {
		check();
		{
			const auto interaction=xeenRegionalInteraction(original,guard.cameraValue);
            const auto &address=state.logicalAddress;
            const auto terminalSite=state.currentScript && address.x>=0 && address.x<=255 &&
                address.y>=0 && address.y<=255 && address.line>=0 && address.line<=255 ?
                state.currentScript->findInstructionIndex(address.x,address.y,state.lookupDirection,address.line):std::nullopt;
            if(terminalSite && guard.w.effectiveEvent({original.mapId,*terminalSite},
                state.currentScript->records()[*terminalSite]).opcode==0x11) {
                // The Flow retained and checked the exact interpreter-issued
                // suspension before this terminate-only settlement. A prelude
                // may have called subroutines; cmdDoTownEvent exits all of them.
                if(!xeen_state::sameCamera(state.workingCamera,guard.cameraValue) ||
                    state.workingGameFlags.values()!=guard.flagValues || address.mapId!=guard.cameraValue.mapId ||
                    !state.instructionCount ||
                    state.instructionsSinceInput>XeenEventInterpreter::kMaximumInstructions || state.treasureChanged() ||
                    (state.pendingPresentation && state.pendingPresentation->continuation!=XeenEventPendingContinuation::Terminate))
                    integrity("Service terminal continuation changed");
                script(state.currentScript->file());currentSite=terminalSite;return;
            }
			if ((interaction==XeenRegionalInteraction::None && state.instructionCount) || !xeen_state::sameCamera(state.workingCamera,guard.cameraValue) ||
				state.workingGameFlags.values()!=guard.flagValues || state.logicalAddress.mapId!=original.mapId ||
				state.logicalAddress.x!=guard.cameraValue.x || state.logicalAddress.y!=guard.cameraValue.y ||
				state.logicalAddress.line<0 || state.logicalAddress.line>255 ||
				state.lookupDirection!=guard.cameraValue.direction || state.instructionsSinceInput>XeenEventInterpreter::kMaximumInstructions ||
				state.instructionCount!=dispatchedRecords.size() || !state.callStack.empty() ||
				!(state.selectedObject==selectedObject) || !state.currentScript)
				integrity("Regional event continuation changed");
			script(state.currentScript->file());
			const auto site=state.currentScript->findInstructionIndex(static_cast<std::uint8_t>(state.logicalAddress.x),
				static_cast<std::uint8_t>(state.logicalAddress.y),state.lookupDirection,static_cast<std::uint8_t>(state.logicalAddress.line));
			if(!site && state.missingInstructionPolicy!=XeenEventMissingInstructionPolicy::NaturalCompletion)
				integrity("Regional event closure is not natural");
			if(state.reward.phase==XeenRewardPhase::Running || state.reward.phase==XeenRewardPhase::Warning)
				validateRewardPrefix(state,dispatchedRecords.size());
			currentSite=site;
		}
	}
	// Interpreter-issued dispatch, after source decoding and before any side effect.
	// Flow retains this history across suspensions; callback state cannot issue records.
	void dispatched(const XeenEventExecutionState &state,const XeenDecodedEventInstruction &decoded) const {
		check();
		if(!currentSite || decoded.source.recordIndex!=currentSite || state.instructionCount!=dispatchedRecords.size()+1 ||
			state.instructionsSinceInput>XeenEventInterpreter::kMaximumInstructions)
			integrity("Event dispatch history changed");
		// Retain the effective dispatch, before a later Remove can disable its
		// resource record. A None dispatch contributes to instruction count only.
		dispatchedRecords.push_back({std::holds_alternative<XeenEventNone>(decoded.operation)?std::nullopt:currentSite,
			decoded.source.opcode});
	}

	void text(const XeenEventTextFile &file) const {
		guard.admitEventText(file);
	}
    void playFx() const {
        check();const auto operation=decodedOperation();
        const auto *cue=std::get_if<XeenEventSoundCue>(&operation);
        if(!cue)integrity("FM effect has no dispatched PlayFX instruction");
        consume();xeenEmitFmEffect(fmEffect,cue->index);
    }
	void deferredAudio() const {
		check();
		const auto operation=decodedOperation();
		if(!std::holds_alternative<XeenEventVoiceCue>(operation) &&
			!std::holds_alternative<XeenEventCdSpeech>(operation))
			integrity("Deferred audio has no dispatched audio instruction");
		consume();
	}
    void playEventVoice() const {
        check();const auto operation=decodedOperation();
        const auto *cue=std::get_if<XeenEventVoiceCue>(&operation);
        if(!cue)integrity("Voice has no dispatched PlayEventVoc instruction");
        consume();if(eventVoice)eventVoice(cue->index);
        check();
    }
	void prepareRewardStaging(const XeenEventExecutionState &state,const XeenItem &item) const {
		check();
		const auto operation=decodedOperation();
		const auto *effect=std::get_if<XeenEventGiveEnchanted>(&operation);
		if(!effect || item.material!=effect->itemCode-60 || item.id!=effect->specialId || item.state!=1 ||
			state.reward.phase!=XeenRewardPhase::Running || dispatchedRecords.empty())
			integrity("Regional reward production changed");
		validateRewardPrefix(state,dispatchedRecords.size()-1);
		consume();
		if (beforeRewardStaging) { beforeRewardStaging(); check(); }
	}
	// One delivery step on a detached copy of the observed party, then the
	// anticipated inventory stores (the shared delivery 0x24128).
	void advanceRewardDelivery(XeenEventExecutionState &state,XeenPartyState &party,bool begin) const {
		check();validateRewardPrefix(state,dispatchedRecords.size());
		XeenPartyState detached;detached.party=XeenParty::fromRosterIds(guard.membership);
		for(auto id:guard.membership)detached.roster.at(id)=guard.characters[id];
		auto reward=state.reward;
		if(begin)xeenBeginTreasureDelivery(reward,state.treasure,detached,state.treasure.preferredRecipient(),fmEffect);
		else xeenAdvanceTreasureDelivery(reward,detached,state.treasure.preferredRecipient(),fmEffect);
		check();
		for(auto id:guard.membership)for(unsigned category=0;category<4;++category) {
			const auto kind=static_cast<XeenRewardCategory>(category);
			xeenRewardItems(guard.characters[id],kind)=xeenRewardItems(detached.roster.at(id),kind);
			xeenRewardItems(party.roster.at(id),kind)=xeenRewardItems(detached.roster.at(id),kind);
		}
		state.reward=reward;guard.adoptMutationBoundary();
	}
	void creditRewardCurrency(XeenEventExecutionState &state,XeenPartyState &party) const {
		check();validateRewardPrefix(state,dispatchedRecords.size());
		if(party.purse) {
			*guard.purse=xeenCreditTreasure(*guard.purse,state.treasure);
			party.purse->gold=guard.purse->gold;party.purse->gems=guard.purse->gems;
		}
		guard.adoptMutationBoundary();
	}
	// The Event's final staging (its restored stash, or the emptied delivery).
	void publishTreasure(XeenWorld &world,const XeenEventExecutionState &state,const XeenTreasureStaging &value) const {
		check();validateRewardPrefix(state,dispatchedRecords.size());
		if(&world!=&guard.w)integrity("Event treasure owner changed");
		guard.prepareStagingPublication(value);world.treasureStaging().publish(value);guard.adoptMutationBoundary();
	}
	void prepareRemove(const XeenCamera &physical, std::optional<XeenObjectIdentity> selected,
		const XeenEventFile &file) const {
		check(); script(file);
		if(!std::holds_alternative<XeenEventRemove>(decodedOperation()))integrity("Regional Remove operands changed");
		consume();
		if (!xeen_state::sameCamera(physical,guard.cameraValue) || !(selected==selectedObject) || removal)
			throw std::logic_error("Journey Remove publication unavailable");
		// The whole per-map table, with this map's Remove applied.
		mapStates=guard.s._mapStates;
		auto &state=mapStates.state(physical.mapId);
		if (selected) {
			state.removedObjects.insert(*selected);
			xeenRemoveObjectOverride(state.objectOverrides,*selected);
		}
		for (std::size_t i=0;i<original.records.size();++i)
			if(original.records[i].x==physical.x && original.records[i].y==physical.y) state.disabledEvents.insert({physical.mapId,i});
		state.eventOverrides.erase(std::remove_if(state.eventOverrides.begin(),state.eventOverrides.end(),
			[&](const auto &v){return state.disabledEvents.count(v.id)!=0;}),state.eventOverrides.end());
		removal=true;
	}
	void removed() const noexcept { guard.s._mapStates.swap(mapStates);guard.adoptMutationBoundary(); }
private:
	static bool neutral(const XeenEventTakeOrGivePair &pair) noexcept {return pair.mode==0 && pair.value==0;}
	XeenDecodedEventOperation decodedOperation() const {
		if(!currentSite || dispatchedRecords.empty() || dispatchedRecords.back().record!=*currentSite ||
			guard.s.isEventDisabled({original.mapId,*currentSite}))integrity("Event effect has no dispatched source");
		const auto result=XeenEventDecoder::decode(dispatchedRecord(*currentSite,dispatchedRecords.back().opcode),
			{original.mapId,original.resourceName,*currentSite,true,dispatchedRecords.back().opcode!=original.records.at(*currentSite).opcode});
		const auto *decoded=std::get_if<XeenDecodedEventInstruction>(&result);
		if(!decoded)integrity("Event effect source is unsupported");
		return decoded->operation;
	}
	void consume() const {
		if(!consumed.insert(dispatchedRecords.size()).second)integrity("Event effect already published");
	}
	XeenEventRecord dispatchedRecord(std::size_t site,std::uint8_t opcode) const {
		auto record=original.records.at(site);record.opcode=opcode;return record;
	}
	// Live staging = the Event's entry snapshot plus its dispatched effects, each
	// at the first empty slot of its category, in dispatch order.
	void validateRewardPrefix(const XeenEventExecutionState &state,std::size_t before) const {
		auto expected=state.treasureBase();bool selection=false;
		for(std::size_t i=0;i<before;++i) {
			const auto &dispatch=dispatchedRecords.at(i);const auto retained=dispatch.record;if(!retained)continue;
			const auto site=*retained;
			const auto result=XeenEventDecoder::decode(dispatchedRecord(site,dispatch.opcode),
				{original.mapId,original.resourceName,site,true,dispatch.opcode!=original.records.at(site).opcode});
			const auto *instruction=std::get_if<XeenDecodedEventInstruction>(&result);if(!instruction)continue;
			selection=selection || std::holds_alternative<XeenEventWhoWill>(instruction->operation);
			const auto *effect=std::get_if<XeenEventGiveEnchanted>(&instruction->operation);
			const auto *give=std::get_if<XeenEventTakeOrGive>(&instruction->operation);
			const bool quest=give && neutral(give->first) && give->second.mode==21 &&
				give->second.value>=82 && neutral(give->third);
			if(quest)expected.insert(XeenRewardCategory::Weapons,{0,static_cast<std::uint8_t>(give->second.value),0,0});
			else if(effect)expected.insertEnchanted(XeenRewardCategory::Miscellaneous,
				{static_cast<std::uint8_t>(effect->itemCode-60),effect->specialId,1,0});
		}
		expected.gold=state.treasure.gold;expected.gems=state.treasure.gems;
		// WhoWill: cleared at entry; nonzero only after a dispatched WhoWill
		// instruction, naming a visible party slot.
		if(state.treasure.whoWill && (!selection || std::size_t(state.treasure.whoWill)>XeenParty::kMaximumVisibleMembers))
			integrity("Regional WhoWill differs from its dispatched selections");
		expected.whoWill=state.treasure.whoWill;
		if(expected!=state.treasure)integrity("Regional reward staging differs from its dispatched effects");
	}
	[[noreturn]] void integrity(const char *message) const { guard.failed=true; throw std::logic_error(message); }
	friend class XeenEventFlow;
	XeenEventPublication(XeenRestoreGuard &guard, const XeenEventFile &original, std::function<void()> authority,
		std::function<void()> beforeRewardStaging={}, std::vector<XeenEventDispatchRecord> *history=nullptr) :
		guard(guard), original(original), authority(std::move(authority)),
		beforeRewardStaging(std::move(beforeRewardStaging)), retainedHistory(history),
		dispatchedRecords(history ? *history : std::vector<XeenEventDispatchRecord>{}) {
		check();selectedObject=const_cast<XeenWorld &>(guard.w).selectObject(guard.cameraValue);check();
	}
	XeenRestoreGuard &guard;
	const XeenEventFile &original;
	std::function<void()> authority;
	std::function<void()> beforeRewardStaging;
	mutable bool removal=false, delivered=false;
	std::vector<XeenEventDispatchRecord> *retainedHistory;
	mutable std::vector<XeenEventDispatchRecord> dispatchedRecords;
	mutable std::set<std::size_t> consumed;
	mutable std::optional<XeenObjectIdentity> selectedObject;
	mutable std::optional<std::size_t> currentSite;
	mutable XeenMapTable mapStates;
};
}
#endif
