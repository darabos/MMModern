#ifndef MMODERN_XEEN_REGIONAL_RULES_H
#define MMODERN_XEEN_REGIONAL_RULES_H
#include "games/xeen/XeenActorApproach.h"
#include "games/xeen/XeenCombatRules.h"
#include "games/xeen/XeenTerrainRules.h"
#include "games/xeen/XeenJourneyProgression.h"
#include <bitset>

namespace mmodern {
// The original monster terrain test (canMonsterMove, WORLD/XEEN.DAT 0x1fc91).
// Outdoors the destination's middle value decides. Middle {0,2,3,4,5,6,8,11,
// 13,14}: the resolved surface type admits Water and Deep Water to flyers and
// species 59, Space to flyers, and any other surface to every species on
// Darkside and all but species 59 on Clouds. Middle {1,7,9,10,12} always
// blocks; middle 15 uses the wall threshold. Unsupported for values >= 16.
XeenMonsterTerrain xeenOutdoorMonsterTerrain(unsigned middle, unsigned surfaceType, unsigned wallNoPass,
	bool flying, int species, XeenSide side) noexcept;
// The wall threshold for every monster, flying or not: a wall passes only
// below the map's no-pass value (DOS 0x1fd8f, the player's own rule).
bool xeenMonsterPassesWall(unsigned wall, unsigned wallNoPass) noexcept;
// Disposable queries over checked immutable geometry; these own no live state.
XeenMonsterTerrain xeenRegionalActorTerrain(const XeenMap &, const XeenActor &, int x, int y);
XeenMonsterTerrain xeenIndoorActorTerrain(XeenWorld &, const XeenActor &, int x, int y);
std::bitset<256> xeenActorClosure(const XeenMap &, const XeenActor &);
// First excluded center row (edge or obstruction), or four for all rows.
// Player missiles admit middle 15; non-east enemy rays do not.
unsigned xeenPlayerRayRows(const XeenMap &, const XeenCamera &);
bool xeenOutdoorRangedRay(const XeenMap &, const XeenCamera &, const XeenActor &);
unsigned xeenPlayerRayRows(XeenWorld &, const XeenCamera &);
// First real blocking row, or four for a clear ray or a map edge.
unsigned xeenPlayerRayWall(XeenWorld &, const XeenCamera &,unsigned rows);
std::array<std::optional<XeenMonsterIdentity>,12> xeenSpellTargets(XeenWorld &,
 const XeenCamera &,const std::vector<XeenActor> &,std::optional<XeenMonsterIdentity>,XeenSpellRange);
bool xeenIndoorRangedRay(XeenWorld &, const XeenCamera &, const XeenActor &);
std::optional<std::size_t> xeenRegionalEvent(const XeenEventFile &, const XeenCamera &);
bool xeenRegionalSign(const XeenEventFile &, const XeenCamera &);
enum class XeenRegionalInteraction { None, Ironworks, Training, Temple, Event, Tavern, Bank, Guild };
// Classify direct entries for presentation only; entries with preludes use the
// generic Event path. Only the interpreter issues a terminal Service request.
bool xeenTownServiceKnown(XeenMapIdentity,std::uint8_t action) noexcept;
std::optional<std::uint8_t> xeenRegionalService(const XeenEventFile &, const XeenCamera &);
XeenRegionalInteraction xeenRegionalInteraction(const XeenEventFile &, const XeenCamera &);
void xeenValidateRegionalActors(const XeenMap &, const XeenObjectFile &, const std::vector<XeenActor> &,
	const std::set<XeenMonsterIdentity> &accounted, const std::vector<XeenMonsterRecord> &statistics);
// Validates an indoor map's complete monster table against its MOB and MON.
void xeenValidateIndoorActors(XeenWorld &, XeenMapIdentity, const std::vector<XeenActor> &);
using XeenRegionalManifest = std::function<void(const XeenMap &, const XeenObjectFile &,
	const XeenEventFile &, const std::vector<XeenMonsterRecord> &)>;
void xeenValidateRegionalManifest(const XeenMap &, const XeenObjectFile &, const XeenEventFile &,
	const std::vector<XeenMonsterRecord> &, const std::vector<std::uint8_t> &dat,
	const std::vector<std::uint8_t> &mob, const std::vector<std::uint8_t> &evt);
struct XeenRegionalRangedShot {
	XeenMonsterIdentity source;
	int x=0, y=0;
	unsigned distance=0;
	XeenDirection direction=XeenDirection::North;
	XeenCombatResult attack;
};
struct XeenRegionalObservation {
 enum class Stage { Published, Travel, Portrait };
 Stage stage=Stage::Published;
 std::optional<XeenMonsterIdentity> impactSource;
 std::optional<std::uint8_t> impactOwner;
 // At most three owed opportunities, each with the reference's 36 sources.
 std::array<XeenRegionalRangedShot,108> shots{};
 unsigned count=0;
 XeenConsequenceCharacters after;
};
// Complete detached movement opportunity. No live owner or publication authority.
struct XeenRegionalOpportunityCandidate {
 bool staged=false,travelStarted=false,travelPublished=false,travelPresented=false,
     portraitPublished=false,impactPresented=false,impactApplied=false;
 bool noTargets=false;
 std::optional<XeenMonsterIdentity> impactSource;
 std::optional<std::uint8_t> impactOwner;
	std::vector<XeenActor> actors;
	XeenConsequenceCharacters characters;
	std::array<XeenRegionalRangedShot,36> shots{};
	unsigned shotCount=0;
	XeenActorView view;
	// The movement pass's Danger Sense result; empty when the pass returned
	// early. Published with the moved actors to the scene presentation.
	std::optional<bool> dangerSense;
	XeenRegionalOpportunityCandidate(const XeenMap &, const std::vector<XeenActor> &,
		const XeenCamera &, const XeenConsequenceCharacters &, const XeenConsequenceInputs &,
		unsigned year, unsigned participantMask, const std::array<bool,6> &blocked = {}, XeenActorOpportunityContext context = {},const XeenGameplayContext &partyContext = {});
	XeenRegionalOpportunityCandidate(XeenWorld &, const std::vector<XeenActor> &,
		const XeenCamera &, const XeenConsequenceCharacters &, const XeenConsequenceInputs &,
		unsigned year, unsigned participantMask, const std::array<bool,6> &blocked = {}, XeenActorOpportunityContext context = {},const XeenGameplayContext &partyContext = {});
	bool service(XeenConsequenceDraw &);
 std::shared_ptr<const XeenRegionalObservation> presentation() const;
    std::optional<std::pair<unsigned,bool>> portrait() const {
        if(!staged || !attack || (!attack->injuryReady && !impactApplied))return std::nullopt;
        return std::make_pair(unsigned(attack->impactOwner),impactApplied);
    }
private:
	XeenCamera camera;
	XeenConsequenceInputs inputs;
	unsigned year, participantMask, cursor=0;
	std::size_t recoveryCursor=0;
	bool recoveryEnabled=true;
	std::array<bool,6> blocked;
	std::optional<XeenEnemyAttackCandidate> attack;
	XeenWorld *indoorWorld = nullptr;
    XeenGameplayContext partyContext;
};
// Retained preparation of one existing action/pulse publication. Flow holds the
// continuation while Approach retains sole authority to publish world deltas.
struct XeenRegionalActionCandidate {
 std::optional<NavigationAction> gpuNavigation;
 bool fmStep=false;
 bool openDoor=false;
	// An outdoor border load stays detached until the whole step can publish.
	std::shared_ptr<XeenWorld> destinationWorld;
	std::optional<XeenEventFile> destinationEvents;
    std::optional<XeenGameFlags> destinationFlags;
	bool lightingStep=false;
	XeenPartyOrder owners{};
	XeenCamera camera;
	XeenGameplayContext context;
	XeenConsequenceCharacters characters;
	XeenConsequenceInputs inputs;
	std::vector<XeenActor> actors;
	XeenCombatRandom random;
	XeenEncounterResult result;
	std::optional<XeenConditionTimeCandidate> time;
	std::optional<XeenTerrainArrivalCandidate> terrain;
	std::optional<XeenTerrainArrival> terrainRule;
	std::optional<XeenRegionalOpportunityCandidate> opportunity;
	std::array<XeenRegionalRangedShot,108> shots{};
	unsigned shotCount=0, remaining=0, pending=0;
	bool classify=false, timeDone=false, timeNoticePublished=false, sleeping=false, noTargets=false;
    bool stepTimePending=false;
	std::optional<bool> dangerSense; // The last completed movement pass's result.
	std::uint64_t revision=0;
};

struct XeenShootCandidate {
	std::optional<XeenTerrainArrivalCandidate> terrain;
	std::optional<XeenLearnedSpell> spell;
	std::optional<XeenSpellDamageCandidate> spellAttack;
	std::optional<XeenSpellDamagePreparation> spellPreparation;
	XeenMoonRayHealing healing;
	bool spellTargetsPrepared=false;
	unsigned itemPower=0;
	unsigned spellCaster=0;
	XeenPartyOrder owners{};
	 enum class Stage { Travel, Impact, PostImpact };
	 Stage stage=Stage::Travel;
	 bool presented=false;
	 bool advanceDraw=false, suffixPrepared=false,deathFx=false;
	 unsigned row=0,rows=4,charge=10;
	 std::uint64_t deadline=0;
	 std::optional<XeenActor> bound;
	 std::optional<XeenJourneyRandomState> impactRandom;
	 std::optional<XeenJourneyLethal> lethal;
	 std::string feedback;
	 std::optional<unsigned> retireLane;
	std::array<std::optional<XeenMonsterIdentity>,12> targets{};
	XeenPartyVolley volley;
	std::array<unsigned,6> projectileEnd{};
	unsigned target=0,blockedRow=4;
	XeenCombatRandom random;
	std::optional<XeenPhysicalPlayerCandidate> attack;
	std::optional<XeenMonsterDropCandidate> drop;
	std::optional<XeenConditionTimeCandidate> time;
	bool attackDone=false,volleyDone=false;
};

}
#endif
