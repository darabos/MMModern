#ifndef MMODERN_XEEN_SCENE_PRESENTATION_H
#define MMODERN_XEEN_SCENE_PRESENTATION_H

#include "games/xeen/XeenActor.h"
#include "games/xeen/XeenLearnedSpellRules.h"
#include "core/NavigationAction.h"
#include "core/PresentationClock.h"
#include "core/IndexedFrame.h"
#include "core/IndoorRenderScene.h"
#include "games/xeen/XeenNavigation.h"
#include "formats/xeen/XeenMonsterAppearance.h"
#include <map>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <random>
#include <stdexcept>
#include <tuple>

namespace mmodern {
// Adapted from ScummVM's interface_scene.cpp and constants.cpp at
// 6814ee9ba54582f5b5adcffab49efbbd8f589edd, GPL-3.0-or-later.
// Unsaved presentation state. Lighting flags participate in the containing
// World's mutation guard; the other fields remain disposable cosmetic state.
struct XeenMonsterAnimation {
	unsigned frame=0, postAttackDelay=0, effect1=0, effect2=0, effect3=0;
	bool reverse=false;
	void attack(unsigned delay) { frame=8; postAttackDelay=delay; }
	void hit(unsigned delay) { frame=11; postAttackDelay=delay; }
	void advance(const XeenMonsterRecord &data, XeenActorStatus status=XeenActorStatus::Physical) {
		if (status==XeenActorStatus::Physical) {
		if (frame<8) {
			if (!data.loopAnimation()) frame=(frame+1)%8;
			else if (!reverse) {frame=(frame+1)%8; if (!frame) {reverse=true;frame=6;}}
			else if (frame && !--frame) reverse=false;
		} else if (frame==11) {
			if (postAttackDelay && !--postAttackDelay) frame=0;
		} else if (++frame==11) {
			if (postAttackDelay) --postAttackDelay;
			frame=postAttackDelay?10:0;
		}
		}
		if (effect2) {
			if (effect1) {
				if (effect1&0x80) {if (effect3) --effect3; if (!effect3) effect1^=0x80;}
				else {effect3=(effect3+1)%3; if (!effect3) {effect1^=0x80;effect3=2;}}
			} else if (!(effect3=(effect3+1)%8)) effect1=effect2=data.animationEffect();
		}
	}
	unsigned flags() const {
		static constexpr unsigned table[15][8]{
			{0x104,0x105,0x106,0x107,0x108,0x109,0x10a,0x10b},
			{0x10c,0x10d,0x10e,0x10f}, {0x110,0x111,0x112,0x113}, {0x114,0x115,0x116,0x117},
			{0x200,0x201,0x202,0x203}, {0x300,0x301,0x302,0x303,0x400,0x401,0x402,0x403},
			{0x500,0x501,0x502,0x503}, {0x600,0x601,0x602,0x603},
			{0x604,0x605,0x606,0x607,0x608,0x609,0x60a,0x60b}, {0x60c,0x60d,0x60e,0x60f},
			{0x100,0x100,0x100,0x100,0x100,0x100,0x100,0x100},
			{0x101,0x101,0x101,0x101,0x101,0x101,0x101,0x101},
			{0x102,0x102,0x102,0x102,0x102,0x102,0x102,0x102},
			{0x103,0x103,0x103,0x103,0x103,0x103,0x103,0x103},
			{0x108,0x108,0x108,0x108,0x108,0x108,0x108,0x108}};
		if (effect2>15 || effect3>7) throw std::invalid_argument("Invalid monster effect phase");
		return effect2?table[effect2-1][effect3]:0;
	}
};

// An object's effective sprite and its view's start and end frames (clouds.dat)
// for the party's facing (DOS 0x4c27 relative view).
struct XeenObjectFrameView {int sprite=-1;unsigned start=0,end=0;};
// giveExt's frame byte (DOS object +3: 0x235f6 opens, 0x23703 closes) of the
// objects whose frame differs from their ordinary one. Like every object in
// DOS, each draw3d updates it before the composition (0x10c32..0x10dc7) until
// it shows its sprite's ordinary frame. Leaving the map and Load drop it, and
// it is not saved, as in DOS.
struct XeenObjectFrames {
 std::map<XeenObjectIdentity,unsigned> frames;
 // The selection while a picked lock's counter is positive (0x9755, 0x98d9).
 std::optional<XeenObjectIdentity> locked;
 // One update is owed before the next composition: each draw3d's, and the
 // first after the chest rule changed a framed sprite (0x28e1f, 0x28e77).
 bool due=false;
 void swap(XeenObjectFrames &other) noexcept {frames.swap(other.frames);locked.swap(other.locked);std::swap(due,other.due);}
};

class XeenScenePresentation {
public:
	struct GpuCameraState {
		IndoorCameraPose pose;
		std::shared_ptr<const std::uint8_t> continuity;
		static IndoorCameraPose cameraPose(const XeenCamera &camera) {
			return {std::uint32_t(camera.mapId.number) | (std::uint32_t(camera.mapId.side) << 16),
				double(int(camera.x)), double(int(camera.y)), double(static_cast<unsigned>(XeenDirection(camera.direction)))};
		}
		void observe(const XeenCamera &camera) {
			const auto next = cameraPose(camera);
			const auto facing = static_cast<unsigned>(XeenDirection(camera.direction));
			const auto previousFacing = (static_cast<int>(std::fmod(pose.turns, 4.0)) + 4) % 4;
			if (!continuity || pose.map != next.map || pose.x != next.x ||
					pose.y != next.y || previousFacing != int(facing)) {
				pose = next;
				continuity = std::make_shared<const std::uint8_t>(0);
			}
		}
		void navigate(const XeenCamera &before, const XeenCamera &after, NavigationAction action) {
			observe(before);
			const auto next = cameraPose(after);
			auto expected = cameraPose(before);
			const int facing = static_cast<int>(XeenDirection(before.direction));
			if (action == NavigationAction::TurnLeft) expected.turns = (facing + 3) % 4;
			else if (action == NavigationAction::TurnRight) expected.turns = (facing + 1) % 4;
			else {
				const int direction = (facing + (action == NavigationAction::MoveBackward ? 2 :
					action == NavigationAction::StrafeLeft ? 3 : action == NavigationAction::StrafeRight ? 1 : 0)) % 4;
				constexpr int dx[]{0, 1, 0, -1}, dy[]{1, 0, -1, 0};
				expected.x += dx[direction]; expected.y += dy[direction];
			}
			if (expected.map != next.map || expected.x != next.x || expected.y != next.y ||
					expected.turns != next.turns) {
				observe(after);
				return;
			}
			pose.x = next.x; pose.y = next.y;
			if (action == NavigationAction::TurnLeft) pose.turns -= 1;
			if (action == NavigationAction::TurnRight) pose.turns += 1;
		}
	};
	GpuCameraState gpuCameraState;
	void publishGpuCamera(const XeenCamera &camera) { gpuCameraState.observe(camera); }
	void navigateGpuCamera(const XeenCamera &before, const XeenCamera &after, NavigationAction action) {
		gpuCameraState.navigate(before, after, action);
	}
 // DS:3067 is an unsaved pending lighting check, separate from exploration.
 // A failed composition may retry its prepared light value without spending again.
 XeenMutable<bool> lightingStepped=false, lightingPrepared=false;
 void markLightingStep() noexcept {lightingStepped=true;lightingPrepared=false;}
 void completeLightingDraw() noexcept {lightingStepped=lightingPrepared=false;}
 static bool blackView(unsigned flags2,unsigned light) noexcept {
     return (flags2&0x4000) && !light;
 }
 static std::uint16_t checkedLight(unsigned light,unsigned attributes,bool pending) noexcept {
     return static_cast<std::uint16_t>(light-(pending && (attributes&0x20) && light ? 1u:0u));
 }
 std::map<XeenObjectIdentity,bool> objectFlips;
 XeenObjectFrames objectFrames;
 // The frame byte a composition draws: the retained one in a reconstruction.
 std::optional<unsigned> drawnObjectFrame(XeenObjectIdentity id) const {
  const auto &frames=drawingState().objectFrames.frames;const auto found=frames.find(id);
  return found==frames.end()?std::nullopt:std::optional<unsigned>(found->second);
 }
 // draw3d's object update (DOS 0x10c32..0x10dc7) for the framed objects of
 // `map`, when one is owed, before the composition and never in a
 // reconstruction. `view` gives an object's current view, absent when unknown.
 void advanceObjectFrames(XeenMapIdentity map,
   const std::function<std::optional<XeenObjectFrameView>(XeenObjectIdentity)> &view) {
  if(!objectFrames.due || _reconstruct)return;
  objectFrames.due=false;
  auto &frames=objectFrames.frames;const auto &locked=objectFrames.locked;
  // DOS updates every object: the selected lock object cycles from its
  // ordinary frame even after its entry ended.
  if(locked && locked->mapId==map && !frames.count(*locked))
   if(const auto v=view(*locked))frames.emplace(*locked,v->start);
  const int closed=XeenSide(map.side)==XeenSide::Darkside?15:16;
  for(auto i=frames.begin();i!=frames.end();) {
   if(i->first.mapId!=map) {++i;continue;}
   const auto v=view(i->first);
   if(!v) {i=frames.erase(i);continue;}
   auto frame=static_cast<std::uint8_t>(i->second+1); // 0x10c83 increments the byte.
   // 0x10c8a..0x10cf6: the selected 15/16 (by side), 58 or 73 cycles 1..4; 58 holds 1.
   const bool cycles=locked && *locked==i->first && (v->sprite==closed || v->sprite==58 || v->sprite==73);
   if(cycles) {if(frame>4 || v->sprite==58)frame=1;}
   else if(frame>v->end)frame=static_cast<std::uint8_t>(v->start); // 0x10cf9..0x10d55
   if(!cycles && frame==v->start) {i=frames.erase(i);continue;}
   i->second=frame;++i;
  }
 }
 struct PortraitEffect {unsigned spellFrame=4, damageFrame=0, damageTicks=0;std::uint64_t spellDeadline=0,damageDeadline=0;std::optional<unsigned> spellCue;};
 // One physical (frame 0) or spell splat per row, plus the original's separate
 // elemental weapon splat beside it (_elemFrame/_elemScale, M67).
 struct Splat {unsigned frame=0, duration=0;int damage=0;bool alternatePosition=false;std::uint64_t deadline=0;
  unsigned elementalFrame=0;int elementalDamage=0;};
 std::array<PortraitEffect,30> portraits{};
 std::array<unsigned,4> buffFrames{};
 // Border counters 0x974d (Levitate), 0x974e (Spot Secret Doors) and 0x974f
 // (Danger Sense): each advances once per border assembly, even while its
 // indicator is inactive, so the three stay in phase (sv-danger-sense-frame).
 unsigned levitateFrame=0, spotDoorsFrame=0, dangerSenseFrame=0;
 // Temporary draw3d door flag (DOS DS:9c6e), never a map/save mutation.
 bool openDoor=false;
 void resetServiceBorder() noexcept {levitateFrame=spotDoorsFrame=dangerSenseFrame=0;}
 // DOS 0x116a8..0x11782: 15 accelerated scroll images and four impact
 // images, each copied on a VGA retrace. Both views are original scene pixels.
 static constexpr std::array<unsigned,19> fallOffsets{0,3,7,12,18,25,33,42,52,63,75,88,102,117,132,127,132,129,132};
 static IndexedFrame fallFrame(const IndexedFrame &top,const IndexedFrame &bottom,unsigned image) {
     if(image>=fallOffsets.size() || !top.isValid() || !bottom.isValid() ||
        top.width!=320 || top.height!=200 || bottom.width!=320 || bottom.height!=200)
         throw std::invalid_argument("Invalid falling views");
     auto frame=top;frame.palette=bottom.palette;
     for(unsigned y=0;y<132;++y) {
         const auto source=y+fallOffsets[image];
         const auto &view=source<132?top:bottom;
         const auto row=source<132?source:source-132;
         std::copy_n(view.pixels.begin()+(row+8)*320+8,216,frame.pixels.begin()+(y+8)*320+8);
     }
     return frame;
 }
 // Indicator sources the border reads (M77). DOS keeps them in memory only,
 // so they are never saved; a load starts them false until their writers run.
 // 0x974c: set by the monster-movement pass for any monster within three
 // cells on both axes; kept while the pass returns early (0x1fdc1).
 bool dangerSense=false;
 // 0xf496: written only by the indoor scene setup, so outdoors it keeps its
 // last indoor value (0x12303..0x1231e).
 bool thinWall=false;
 // 0x8c52: a text or number prompt is open; hides Direction Sense.
 bool textPrompt=false;
 static unsigned animatedBorderFrame(bool active,unsigned inactive,unsigned &phase) noexcept {
     const auto frame=active?inactive+phase:inactive;phase=(phase+1)%12;return frame;
 }
 static unsigned levitateBorderFrame(bool active,unsigned &phase) noexcept {
     return animatedBorderFrame(active,16,phase);
 }
 static std::pair<unsigned,unsigned> clairvoyanceFrames(unsigned left,unsigned right,unsigned phase,bool overlap=false) noexcept {
     phase%=4;
     return {left==2?0:phase+(left==0?4:0),overlap?52:right==2?8:phase+(right==0?8:12)};
 }
 std::array<Splat,3> splats{};
 bool feedbackActive() const noexcept {
  for(const auto &p:portraits)if(p.spellFrame<4 || p.damageTicks || p.spellCue)return true;
  for(const auto &s:splats)if(s.duration)return true;
  return false;
 }
 void spellEffect(unsigned owner,std::uint64_t=0,bool reset=false) noexcept {
  // Character::addHitPoints clears _charFX for each invocation, including
  // zero-HP Cure Poison; giveTake's duplicate recipients remain suppressed.
  if(owner<portraits.size() && (reset || portraits[owner].spellFrame==4)) {portraits[owner].spellFrame=0;portraits[owner].spellDeadline=0;}
 }
 void spellCue(unsigned owner,unsigned id) noexcept {if(owner<portraits.size())portraits[owner].spellCue=id;}
 void portraitDamage(unsigned owner,unsigned frame,std::uint64_t=0) noexcept {
  if(owner<portraits.size()) {portraits[owner].damageFrame=frame;portraits[owner].damageTicks=1;portraits[owner].damageDeadline=0;}
 }
 void hitSplat(unsigned row,int damage,unsigned frame,bool alternatePosition,std::uint64_t=0,
   unsigned elementalFrame=0,int elementalDamage=0) noexcept {
  if(row<3 && damage>0)splats[row]={frame,3,damage,alternatePosition,0,elementalFrame,elementalDamage};
 }
 // Flow admits one retained frame at a time and owns its clock/acquisition.
 // Repaint/polling never advances a recipient or consumes a hold.
 bool advanceFeedback(std::uint64_t) noexcept {return false;}
 void spellFrame(unsigned owner,unsigned frame) {
     if(owner>=portraits.size() || frame>4)throw std::invalid_argument("Invalid retained portrait sparkle");
     portraits[owner].spellFrame=frame;portraits[owner].spellDeadline=0;
 }
 void clearDamage() noexcept {for(auto &portrait:portraits){portrait.damageTicks=0;portrait.damageDeadline=0;}}

	explicit XeenScenePresentation(std::uint32_t seed=std::random_device{}()) : _random(seed) {}
	bool ground=false, defaultGround=false, sky=false, water=false;
	unsigned overallFrame=0, floatPhase=0;
	std::uint64_t wallPhase=0;
	void enterMap(XeenMapIdentity map) {
		const auto key=std::make_pair(XeenSide(map.side),std::uint16_t(map.number));
		if (_map && *_map==key) return;
		// MonsterObjectData reconstructs normal/effect phases and starts wall
		// frames at zero on map entry. Cache reconstruction is not map entry.
		for(auto i=_actors.begin();i!=_actors.end();) {
			if(i->first.mapId==map) i=_actors.erase(i);else ++i;
		}
		_map=key;_wallOrigin=wallPhase;_lastAttack.reset();
	}
	std::size_t wallFrame(std::size_t count) const {
		if(!count) throw std::invalid_argument("Empty wall-item animation");
		return (wallPhase-_wallOrigin)%count;
	}
	void navigation(NavigationAction action, bool moved, bool combat=false) {
		const bool turn=action==NavigationAction::TurnLeft || action==NavigationAction::TurnRight;
		const bool strafe=action==NavigationAction::StrafeLeft || action==NavigationAction::StrafeRight;
		if (turn || (!combat && moved && strafe)) sky=!sky;
		if (!combat && (turn || moved)) {ground=!ground;defaultGround=!defaultGround;}
	}
	// perform's Wait flips cancel the flips in stepTime.
	void wait() {}
	void include(const std::vector<XeenActor> &actors) {
		for (const auto &a:actors) if (a.statistics && a.statistics->supportsRendering()) {
			if (auto i=_actors.find(a.id);i!=_actors.end()) {i->second.status=a.status;continue;}
			XeenMonsterAnimation animation;
			// Original normal-frame initialization and the approved effect-phase
			// initialization both consume only disposable cosmetic randomness.
			animation.frame=std::uniform_int_distribution<unsigned>(0,7)(_random);
			animation.effect1=animation.effect2=a.statistics->animationEffect();
			// Approved M48 deviation: random initial phases use only this cosmetic RNG.
			if (animation.effect2) animation.effect3=std::uniform_int_distribution<unsigned>(0,7)(_random);
			_actors.emplace(a.id, Entry{*a.statistics,animation,a.status});
		}
	}
	const XeenMonsterAnimation *animation(XeenMonsterIdentity id) const {
		const auto i=_actors.find(id);return i==_actors.end()?nullptr:&i->second.animation;
	}
    // attack2's RT_GROUP path assigns the transient monster tint even when
    // physical resistance reduces the accepted spell to zero damage. Call once
    // after an accepted magic gate and nonzero base damage, never on redraw.
    void spellImpact(const XeenActor &actor,XeenLearnedSpell spell,unsigned type=0) {
        unsigned effect;
        switch(spell) {
        case XeenLearnedSpell::FingerOfDeath: case XeenLearnedSpell::MassDistortion:
        case XeenLearnedSpell::EnergyBlast: effect=5;break;
        // Combat::DAMAGE_TYPE_EFFECTS; spell-handler FX14/17/18 are deferred audio.
        case XeenLearnedSpell::HolyWord: case XeenLearnedSpell::TurnUndead: effect=14;break;
        case XeenLearnedSpell::PoisonVolley: case XeenLearnedSpell::InsectSpray: case XeenLearnedSpell::ToxicCloud: effect=2;break;
        case XeenLearnedSpell::Sparks: effect=11;break;
        case XeenLearnedSpell::GolemStopper: case XeenLearnedSpell::MagicArrow: case XeenLearnedSpell::Shrapmetal:
        case XeenLearnedSpell::FlyingFist: case XeenLearnedSpell::Pain: effect=3;break;
        case XeenLearnedSpell::AcidSpray: case XeenLearnedSpell::ColdRay:
        case XeenLearnedSpell::DancingSword: case XeenLearnedSpell::DeadlySwarm:
        case XeenLearnedSpell::ElementalStorm: case XeenLearnedSpell::FantasticFreeze:
        case XeenLearnedSpell::FieryFlail: case XeenLearnedSpell::Fireball:
        case XeenLearnedSpell::Frostbite: case XeenLearnedSpell::Implosion:
        case XeenLearnedSpell::Incinerate: case XeenLearnedSpell::Inferno:
        case XeenLearnedSpell::LightningBolt: case XeenLearnedSpell::MegaVolts:
        case XeenLearnedSpell::MoonRay: case XeenLearnedSpell::PrismaticLight:
        case XeenLearnedSpell::StarBurst: case XeenLearnedSpell::SunRay: {
            constexpr unsigned effects[]{3,10,4,11,1,2,5};
            if(type>=std::size(effects))throw std::invalid_argument("Invalid spell damage tint");
            effect=effects[type];break;
        }
        default: throw std::invalid_argument("Unsupported spell impact presentation");
        }
        include({actor});
        const auto entry=_actors.find(actor.id);
        if(entry==_actors.end())return;
        entry->second.animation.effect1=0;
        entry->second.animation.effect2=effect;
        // attack2 leaves effect3 intact; advance restores the resource's own
        // animationEffect at the next original eight-phase wrap.
    }
	void spawn(const XeenActor &actor) {
		const bool present=_actors.count(actor.id)!=0;
		include({actor});
		auto &animation=_actors.at(actor.id).animation;
		if(present)animation.frame=std::uniform_int_distribution<unsigned>(0,7)(_random);
		animation.postAttackDelay=0;animation.reverse=false;
	}
	void copySpawn(const XeenActor &actor,const XeenScenePresentation &candidate) {
		const auto found=candidate._actors.find(actor.id);
		if(found==candidate._actors.end())throw std::logic_error("Spawn animation is absent");
		_actors.insert_or_assign(actor.id,found->second);
	}
    void arenaMonster(const XeenActor &actor,const XeenMonsterAnimation &animation) {
        _actors.insert_or_assign(actor.id,Entry{*actor.statistics,animation,actor.status});
    }
	void copySpawnRandom(const XeenScenePresentation &candidate) {
		// Publish the consumed cosmetic cursor without replacing live animation phases.
		_random=candidate._random;
	}
	void appearance(const XeenMonsterAppearance &value) {
		if (!value.identity || value.kind!=XeenMonsterSpriteKind::Attack) {_lastAttack.reset();return;}
		const auto key=std::make_tuple(value.identity->mapId.side,
			std::uint16_t(value.identity->mapId.number),std::size_t(value.identity->recordIndex),unsigned(value.frame));
		if (_lastAttack && *_lastAttack==key) return;
		_lastAttack=key;
		const auto i=_actors.find(*value.identity);
		if (i!=_actors.end()) {i->second.animation.frame=8+value.frame;i->second.animation.postAttackDelay=value.frame==3?5:1;}
	}
	// draw3d updates UI/water before composition (DAT 0x10bda..0x10bf4), then
	// every object's frame (0x10c32..0x10dc7, advanceObjectFrames).
	void beginDraw() noexcept {
		if (!(_uiPhase=(_uiPhase+1)%4)) water=!water;
		objectFrames.due=true;
	}
	// animate3d runs after the chosen scene has been composed (DAT 0x107b6).
	void advanceAfterDraw(XeenMapIdentity map) {
		overallFrame=(overallFrame+1)%5;floatPhase=(floatPhase+1)%8;++wallPhase;
		for (auto &[id,entry]:_actors) if (id.mapId==map) entry.animation.advance(entry.data,entry.status);
        for(auto &s:splats)if(s.duration)--s.duration;
	}
	void advance(XeenMapIdentity map) {composeFromRetained(false);beginDraw();advanceAfterDraw(map);}
	// Reconstruction reads the values that produced the retained frame, while
	// the live values already describe the next draw. Neither view owns gameplay.
	void composeFromRetained(bool reconstruct) noexcept {_reconstruct=reconstruct && bool(_drawn);}
	const XeenScenePresentation &drawingState() const noexcept {return _reconstruct?*_drawn:*this;}
	void retainDrawingState() {
		if(_reconstruct)return;
		auto drawn=std::make_shared<XeenScenePresentation>(*this);
		drawn->_drawn.reset();drawn->_reconstruct=false; // No snapshot chain.
		_drawn=std::move(drawn);
	}
	static int floatX(unsigned phase) {static constexpr int values[]{-2,-1,0,1,2,1,0,-1};return values[phase%8];}
	static int floatY(unsigned phase) {static constexpr int values[]{-2,0,2,0,-1,0,2,0};return values[phase%8];}
private:
	std::shared_ptr<const XeenScenePresentation> _drawn;
	bool _reconstruct=false;
	struct Entry {XeenMonsterRecord data;XeenMonsterAnimation animation;XeenActorStatus status;};
	std::mt19937 _random;
	unsigned _uiPhase=0;
	std::uint64_t _wallOrigin=0;
	std::optional<std::pair<XeenSide,std::uint16_t>> _map;
	std::map<XeenMonsterIdentity,Entry> _actors;
	// Plain values: no tracked gameplay fields in this transient key.
	std::optional<std::tuple<XeenSide,std::uint16_t,std::size_t,unsigned>> _lastAttack;
};
}
#endif
