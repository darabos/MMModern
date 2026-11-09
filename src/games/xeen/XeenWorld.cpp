#include "games/xeen/XeenWorld.h"
#include "games/xeen/XeenScriptedRules.h"
#include "games/xeen/XeenEventValues.h"
#include "games/xeen/XeenRestoreGuard.h"
#include "games/xeen/XeenJourneyCapture.h"
#include "games/xeen/XeenActorApproach.h"
#include "games/xeen/XeenEventPublication.h"
#include "games/xeen/XeenStateEquality.h"
#include "games/xeen/XeenGameFlags.h"
#include "games/xeen/XeenSkills.h"
#include "games/xeen/XeenCombatRules.h"

#include <atomic>
#include <stdexcept>
#include <limits>
#include <utility>
#include <algorithm>
#include <exception>
#include <map>

namespace mmodern {
XeenWorld::GameplayBorrow::GameplayBorrow(XeenWorld &w, XeenPartyState &p,
		XeenCamera &c, const XeenGameFlags &f) : owners{w._gameplayBorrow.retain(),
		p._gameplayBorrow.retain(), p.roster._gameplayBorrow.retain(),
		c.gameplayBorrow.retain(), f._gameplayBorrow.retain()} {
	for (const auto &state : owners) { ++state->references; ++state->revision; }
}
namespace {
template<class Visit> void resourceEntityRanges(const XeenMapEntities &entities,const Visit &visit) {
	visit(entities.objects.data(),entities.objects.size()*sizeof(XeenMapEntity));
	visit(entities.monsters.data(),entities.monsters.size()*sizeof(XeenMapEntity));
	visit(entities.wallItems.data(),entities.wallItems.size()*sizeof(XeenMapEntity));
}
template<class Visit> void resourceRanges(const XeenMap &map,const Visit &visit) {
	visit(&map,sizeof(map));resourceEntityRanges(map.entities,visit);
	visit(map.instructions.data(),map.instructions.size()*sizeof(XeenEventInstruction));
	for(const auto &instruction:map.instructions)
		visit(instruction.parameters.data(),instruction.parameters.size()*sizeof(XeenMutable<std::uint8_t>));
}
template<class Visit> void resourceRanges(const XeenObjectFile &objects,const Visit &visit) {
	visit(&objects,sizeof(objects));resourceEntityRanges(objects.entities,visit);
}
std::uint64_t nextWorldIncarnation() {
	static std::atomic<std::uint64_t> next{1};
	auto value = next.load(std::memory_order_relaxed);
	for (;;) {
		if (value == std::numeric_limits<std::uint64_t>::max())
			throw std::overflow_error("world incarnation exhausted");
		if (next.compare_exchange_weak(value, value + 1,
				std::memory_order_relaxed, std::memory_order_relaxed)) return value;
	}
}
// The open chest that replaces a closed one (DOS 0x28dbc..0x28e7c): the first
// of the map's 16 picture-table slots holding sprite 62 for 16 (57 for 15 on
// Darkside), or 119 for 73. A closed 16 or 15 never falls through to 73.
std::optional<std::uint8_t> openChestIndex(const XeenMapEntities &entities,int sprite,XeenSide side) {
	const bool dark=side==XeenSide::Darkside;
	const int open=sprite==(dark?15:16)?(dark?57:62):sprite==73?119:-1;
	if(open<0)return std::nullopt;
	for(unsigned i=0;i<16;++i)if(entities.objectTable[i]==open)return static_cast<std::uint8_t>(i);
	return std::nullopt;
}
using namespace xeen_state;
}

std::size_t XeenSessionWorldState::disabledObjectCount() const noexcept {
	std::size_t count=0;for (const auto &entry:_mapStates) count+=entry.second.removedObjects.size();return count;
}
std::size_t XeenSessionWorldState::disabledEventCount() const noexcept {
	std::size_t count=0;for (const auto &entry:_mapStates) count+=entry.second.disabledEvents.size();return count;
}
std::set<XeenObjectIdentity> XeenSessionWorldState::disabledObjects() const {
	std::set<XeenObjectIdentity> values;
	for (const auto &entry:_mapStates) values.insert(entry.second.removedObjects.begin(),entry.second.removedObjects.end());
	return values;
}
std::set<XeenEventIdentity> XeenSessionWorldState::disabledEvents() const {
	std::set<XeenEventIdentity> values;
	for (const auto &entry:_mapStates) values.insert(entry.second.disabledEvents.begin(),entry.second.disabledEvents.end());
	return values;
}
std::vector<XeenBarrierOverride> XeenSessionWorldState::barriers() const {
	std::vector<XeenBarrierOverride> values;
	for (const auto &entry:_mapStates) values.insert(values.end(),entry.second.cells.begin(),entry.second.cells.end());
	return values;
}
std::vector<XeenObjectOverride> XeenSessionWorldState::objectOverrides() const {
	std::vector<XeenObjectOverride> values;
	for (const auto &entry:_mapStates)
		values.insert(values.end(),entry.second.objectOverrides.begin(),entry.second.objectOverrides.end());
	return values;
}
std::vector<XeenExploredTile> XeenSessionWorldState::exploredTiles() const {
	std::vector<XeenExploredTile> values;
	for (const auto &entry:_mapStates) if (entry.second.explored) values.push_back({entry.first,*entry.second.explored});
	return values;
}
void XeenSessionWorldState::stageMonsters(XeenMapIdentity mapId, std::vector<XeenActor> actors,
		std::size_t originalCount, bool outdoors) {
	// The table is exactly the MOB records; Spawn never extends it (DOS 0x9c59).
	if (originalCount!=actors.size() || originalCount>std::numeric_limits<std::uint16_t>::max())
		throw std::invalid_argument("Staged monster table differs from its MOB records");
	auto &state=_mapStates.state(mapId);
	state.monsters.emplace(std::move(actors));
	state.originalMonsterCount=static_cast<std::uint16_t>(originalCount);state.outdoors=outdoors;
	state.originalMonstersFromRestore=false;
}
void XeenSessionWorldState::reserveTiles(const std::vector<XeenExploredTile> &values) {
	for (const auto &tile:values) static_cast<void>(_mapStates.state(tile.mapId));
}
void XeenSessionWorldState::publishExploration(const std::vector<XeenExploredTile> &values) noexcept {
	// Every value names a reserved entry; entries absent from the complete
	// values have no explored bits.
	for (const auto &tile:values) if (!_mapStates.find(tile.mapId)) std::terminate();
	auto next=values.begin();
	for (const auto &entry:_mapStates) {
		auto &state=*_mapStates.find(entry.first);
		if (next!=values.end() && next->mapId==entry.first) state.explored=next++->cells;
		else state.explored.reset();
	}
}
void XeenSessionWorldState::assignCells(const std::vector<XeenBarrierOverride> &values) {
	std::map<XeenMapIdentity,std::vector<XeenBarrierOverride>> tiles;
	for (const auto &cell:values) {
		tiles[cell.tile].push_back(cell);
		static_cast<void>(_mapStates.state(cell.tile));
	}
	for (const auto &entry:_mapStates) {
		auto &state=*_mapStates.find(entry.first);
		const auto found=tiles.find(entry.first);
		if (found!=tiles.end()) state.cells.swap(found->second);else state.cells.clear();
	}
}

XeenWorld::XeenWorld(MapLoader loader, ObjectLoader objectLoader) :
	_incarnation(nextWorldIncarnation()), _baseLoader(loader), _baseObjectLoader(objectLoader),
	_loader(std::move(loader)), _objectLoader(std::move(objectLoader)) {
	if (!_loader)
		throw std::invalid_argument("XeenWorld requires a map loader");
}

bool XeenWorld::eventCameraAdmitted(const XeenCamera &camera) const noexcept {
	if (!camera.mapId || camera.x<0 || camera.y<0 || unsigned(camera.direction)>3) return false;
	const auto *state=_sessionState.mapState(camera.mapId);
	const bool indoor=regionalJourney() && state && state->monsters && !state->outdoors;
	return camera.x<(indoor?32:16) && camera.y<(indoor?32:16);
}


void XeenWorld::restoreSessionState(const std::vector<XeenObjectIdentity> &objects,
		const std::vector<XeenEventIdentity> &events, const EventLoader &eventLoader) {
	if (hasEncounterState()) throw std::logic_error("cannot restore an encounter world");
	XeenSessionWorldState prepared;
	for (const auto id : objects) {
		static_cast<void>(map(id.mapId));
		if (hasEncounterState()) throw std::logic_error("encounter appeared during overlay map loading");
		validateObject(id);
		if (hasEncounterState()) throw std::logic_error("encounter appeared during overlay resource loading");
		if (!prepared._mapStates.state(id.mapId).removedObjects.insert(id).second)
			throw std::invalid_argument("duplicate restored object identity");
	}
	std::map<XeenMapIdentity, XeenEventFile> files;
	for (const auto id : events) {
		static_cast<void>(map(id.mapId));
		if (hasEncounterState()) throw std::logic_error("encounter appeared during overlay map loading");
		if (!eventLoader) throw std::invalid_argument("restoration requires an event loader");
		auto found = files.find(id.mapId);
		if (found == files.end()) found = files.emplace(id.mapId, eventLoader(id.mapId)).first;
		if (hasEncounterState()) throw std::logic_error("encounter appeared during overlay event loading");
		const auto &file = found->second;
		if (file.mapId != id.mapId || !file.resourcePresent || id.recordIndex >= file.records.size())
			throw std::invalid_argument("restored original event identity does not exist");
		if (!prepared._mapStates.state(id.mapId).disabledEvents.insert(id).second)
			throw std::invalid_argument("duplicate restored event identity");
	}
	// Replace both sets on every entry, keeping the entries' other values.
	for (const auto &entry:_sessionState._mapStates) static_cast<void>(prepared._mapStates.state(entry.first));
	for (const auto &entry:prepared._mapStates) static_cast<void>(_sessionState._mapStates.state(entry.first));
	if (hasEncounterState()) throw std::logic_error("encounter appeared before overlay publication");
	XeenMutationWatch::write(this);
	for (const auto &entry:prepared._mapStates) {
		auto &live=*_sessionState._mapStates.find(entry.first);auto &value=*prepared._mapStates.find(entry.first);
		live.removedObjects.swap(value.removedObjects);live.disabledEvents.swap(value.disabledEvents);
		live.eventOverrides.erase(std::remove_if(live.eventOverrides.begin(),live.eventOverrides.end(),
			[&](const auto &v){return live.disabledEvents.count(v.id)!=0;}),live.eventOverrides.end());
	}
	++_ownerRevision;
}

void XeenWorld::borrowTreasureStaging(XeenTreasureStagingState &state) {
	if (_treasureStaging == &state) return;
	// One running program owns a world's pending treasure for its lifetime.
	if (_treasureStaging != &_ownTreasureStaging)
		throw std::logic_error("World already borrows another pending treasure owner");
	if (_ownTreasureStaging.value() != XeenTreasureStaging{} || _ownTreasureStaging.revision())
		throw std::logic_error("World pending treasure changed before the session borrowed it");
	_treasureStaging=&state;
	// A restore that preceded the session hands its binding preimage over with the world.
	if (_journeyRestoration && _journeyRestoration->guard) _journeyRestoration->guard->adoptStagingOwner(state);
}

void XeenWorld::swapPreparedState(XeenWorld &candidate) noexcept {
 _scenePresentation.completeLightingDraw(); // Exact restore never replays a pending step.
	_scenePresentation.gpuCameraState.continuity.reset();
	XeenMutationWatch::write(this);XeenMutationWatch::write(&candidate);
	// Private to guarded save publication. Never swaps/clears encounter authority.
	// The whole per-map table, monster tables included, moves as one value.
	_sessionState._mapStates.swap(candidate._sessionState._mapStates);
	_sessionState._headCache.swap(candidate._sessionState._headCache);
	_scenePresentation.objectFlips.swap(candidate._scenePresentation.objectFlips);
	_scenePresentation.objectFrames.swap(candidate._scenePresentation.objectFrames);
    std::swap(_mapRuleView,candidate._mapRuleView);
    std::swap(_pendingLoadSide,candidate._pendingLoadSide);
	_maps.swap(candidate._maps);
	_objects.swap(candidate._objects);
	++_ownerRevision; ++candidate._ownerRevision;
}

const XeenObjectFile &XeenWorld::objectFile(XeenMapIdentity mapId) {
	if (!mapId) throw std::invalid_argument("invalid object map identity");
	const auto found = _objects.find(mapId);
	if (found != _objects.end()) return found->second;
	// Geometry-only clients may omit the provider; this represents no MOB source.
	XeenObjectFile loaded = _objectLoader ? _objectLoader(mapId) :
		XeenObjectFile{mapId, {}, false, {}};
	if (_combatCheck) _combatCheck();
	XeenRestoreGuard::admitLoadedResource(*this,mapId,loaded,loaded.mapId==mapId,
		[](XeenRestoreGuard &g)->auto & {return g.objects;},xeen_state::sameObjectFile);
	XeenMutationWatch::prepareOwned(this,4);
	const auto result = _objects.emplace(mapId, std::move(loaded));
	++_cacheRevision;
	XeenRestoreGuard::loadedResourceInserted(*this);
	resourceRanges(result.first->second,[this](const void *p,std::size_t n) { XeenMutationWatch::addOwned(this,p,n); });
	return result.first->second;
}

void XeenWorld::validateObject(XeenObjectIdentity id) {
	const auto &file = objectFile(id.mapId);
	if (!file.resourcePresent || id.recordIndex >= file.entities.objects.size())
		throw std::invalid_argument("selected original object index does not exist");
}

bool XeenWorld::isObjectDisabled(XeenObjectIdentity id) {
	validateObject(id);
	return effectiveObject(id).isDisabled() ||
		_sessionState.isObjectDisabled(id);
}

XeenMapEntity XeenWorld::effectiveObject(XeenObjectIdentity id) {
	validateObject(id);
	const auto &file=objectFile(id.mapId);
	auto value=file.entities.objects[id.recordIndex];
	if(const auto *state=_sessionState.mapState(id.mapId))
		for(const auto &v:state->objectOverrides)if(v.id==id) {
			value.x=v.x;value.y=v.y;
			// The rule's index also names the sprite (DOS 0x28e1f, 0x28e77; the
			// loader rebuilds it from the index, 0xf9b1), before the map rules.
			if(v.tableIndex) {
				const auto sprite=file.entities.objectTable[*v.tableIndex];
				value.tableIndex=*v.tableIndex;value.resourceId=sprite==0xff?-1:int(sprite);
			}
			break;
		}
	return xeenMapRuleObject(value,id,_mapRuleView);
}

bool XeenWorld::effectiveObjectFlip(XeenObjectIdentity id) const {
	const auto drawn=_scenePresentation.objectFlips.find(id);
	if(drawn!=_scenePresentation.objectFlips.end())return drawn->second;
	if(const auto *state=_sessionState.mapState(id.mapId))
		for(const auto &v:state->objectOverrides)if(v.id==id)return v.flipped;
	return false; // MazeObject constructor, before the first drawScene.
}

void XeenWorld::applyMoveObject(XeenObjectIdentity id,int x,int y) {
	if(!detachedEventCandidate() || x<-128 || x>127 || y<-128 || y>127)
		throw std::invalid_argument("MoveObj requires a detached Event and signed-byte coordinates");
	// Remove only wrote the position (128,128), which no signed-byte operand
	// matches; MoveObj replaces it, so the removed object shows up again.
	const bool removed=_sessionState.isObjectDisabled(id);
	const auto before=effectiveObject(id);
	bool flip=effectiveObjectFlip(id);
	if(!removed && before.x==x && before.y==y)flip=!flip;
	auto &state=_sessionState._mapStates.state(id.mapId);
	auto prepared=state.objectOverrides;
	const auto at=std::lower_bound(prepared.begin(),prepared.end(),id,[](const auto &v,auto key){return v.id<key;});
	// MoveObj writes x and y only (DOS 0x27913..0x27937); the table index stays.
	if(at!=prepared.end() && at->id==id) {at->x=x;at->y=y;at->flipped=flip;}
	else prepared.insert(at,{id,x,y,flip});
	if(_sessionState.objectOverrides().size()-state.objectOverrides.size()+prepared.size()>65536)
		throw std::invalid_argument("Too many object overrides");
	// Allocate the disposable draw-state entry before the owned stores.
	_scenePresentation.objectFlips[id]=flip;
	XeenMutationWatch::write(this);state.objectOverrides.swap(prepared);
	if(removed)state.removedObjects.erase(id);
}

void XeenWorld::applyExchangeObjects(XeenMapIdentity mapId,unsigned first,unsigned second) {
	if(!detachedEventCandidate())throw std::invalid_argument("ExchObj requires a detached Event");
	const auto &file=objectFile(mapId);
	if(!file.resourcePresent || first>=file.entities.objects.size() || second>=file.entities.objects.size())
		throw std::invalid_argument("ExchObj index outside the live object count is not supported yet");
	if(first==second)return;
	// DOS 0x28a86..0x28ad6: exchange the stored positions, before map rules.
	// MoveObj's self-move flip and its draw are deliberately absent here.
	auto &state=_sessionState._mapStates.state(mapId);
	auto prepared=state.objectOverrides;
	const auto retain=[&](unsigned index) -> XeenObjectOverride {
		const XeenObjectIdentity id{mapId,index};
		for(const auto &v:prepared)if(v.id==id)return v;
		const auto &record=file.entities.objects[index];
		return {id,_sessionState.isObjectDisabled(id)?-128:int(record.x),
			_sessionState.isObjectDisabled(id)?-128:int(record.y),effectiveObjectFlip(id)};
	};
	auto a=retain(first),b=retain(second);std::swap(a.x,b.x);std::swap(a.y,b.y);
	for(const auto &value:{a,b}) {
		const auto at=std::lower_bound(prepared.begin(),prepared.end(),value.id,[](const auto &v,auto id){return v.id<id;});
		if(at!=prepared.end() && at->id==value.id)*at=value;else prepared.insert(at,value);
	}
	if(_sessionState.objectOverrides().size()-state.objectOverrides.size()+prepared.size()>65536)
		throw std::invalid_argument("Too many object overrides");
	XeenMutationWatch::write(this);state.objectOverrides.swap(prepared);
	state.removedObjects.erase({mapId,first});state.removedObjects.erase({mapId,second});
}

XeenObjectFile XeenWorld::originalObjects(XeenMapIdentity mapId) const {
	const auto found=_objects.find(mapId);
	if(found!=_objects.end())return found->second;
	if(!_baseObjectLoader)throw std::logic_error("Object state has no MOB resource");
	auto loaded=_baseObjectLoader(mapId);
	if(loaded.mapId!=mapId || !loaded.resourcePresent)throw std::logic_error("Object state has no MOB resource");
	return loaded;
}

bool XeenWorld::applyOpenChest(XeenObjectIdentity id) {
	if(!detachedEventCandidate())throw std::invalid_argument("The chest rule requires a detached Event");
	// The selected object's sprite byte (0x28da6..0x28dba), map rules included.
	const auto sprite=effectiveObject(id).resourceId;
	const auto &file=objectFile(id.mapId);
	const auto index=openChestIndex(file.entities,sprite,id.mapId.side);
	if(!index)return false;
	auto &state=_sessionState._mapStates.state(id.mapId);
	auto prepared=state.objectOverrides;
	const auto at=std::lower_bound(prepared.begin(),prepared.end(),id,[](const auto &v,auto key){return v.id<key;});
	// Only the index and sprite change: x and y come from the override or the
	// MOB record (never the map rules' view), the flip as MoveObj reads it.
	const bool flip=effectiveObjectFlip(id);
	if(at!=prepared.end() && at->id==id) {at->flipped=flip;at->tableIndex=*index;}
	else {
		const auto &record=file.entities.objects[id.recordIndex];
		prepared.insert(at,{id,int(record.x),int(record.y),flip,*index});
	}
	if(_sessionState.objectOverrides().size()-state.objectOverrides.size()+prepared.size()>65536)
		throw std::invalid_argument("Too many object overrides");
	XeenMutationWatch::write(this);state.objectOverrides.swap(prepared);
	// The new sprite's frame owes draw3d's update before it is composed.
	if(_scenePresentation.objectFrames.frames.count(id))_scenePresentation.objectFrames.due=true;
	return true;
}

void XeenWorld::restoreObjectOverrides(const std::vector<XeenObjectOverride> &values) {
	if(hasEncounterState() || values.size()>65536)throw std::invalid_argument("Invalid object restoration owner/count");
	auto canonical=values;
	for(unsigned n=0;n<values.size();++n) {
		const auto &v=values[n];validateObject(v.id);
		if(hasEncounterState() || v.x<-128 || v.x>127 || v.y<-128 || v.y>127 ||
			(n && !(values[n-1].id<v.id)))throw std::invalid_argument("Invalid or duplicate object override");
		// A record keeps its own index (absent, even on a 0xFF slot) or the open
		// chest the rule gives its own closed sprite, at any position.
		if(v.tableIndex) {
			const auto &file=objectFile(v.id.mapId);const auto &record=file.entities.objects[v.id.recordIndex];
			if(*v.tableIndex==record.tableIndex)canonical[n].tableIndex.reset();
			else if(openChestIndex(file.entities,record.resourceId,v.id.mapId.side)!=std::optional<std::uint8_t>(*v.tableIndex))
				throw std::invalid_argument("Invalid object picture-table index");
		}
	}
	std::map<XeenMapIdentity,std::vector<XeenObjectOverride>> prepared;
	for(const auto &v:canonical) {prepared[v.id.mapId].push_back(v);static_cast<void>(_sessionState._mapStates.state(v.id.mapId));}
	XeenMutationWatch::write(this);
	for(const auto &entry:_sessionState._mapStates) {
		auto &state=*_sessionState._mapStates.find(entry.first);const auto found=prepared.find(entry.first);
		if(found!=prepared.end())state.objectOverrides.swap(found->second);else state.objectOverrides.clear();
	}
}

XeenMapEntity XeenWorld::effectiveWallItem(XeenWallItemIdentity id) {
	const auto &file=objectFile(id.mapId);
	if(!file.resourcePresent || id.recordIndex>=file.entities.wallItems.size())
		throw std::invalid_argument("Original wall-item identity does not exist");
	auto value=file.entities.wallItems[id.recordIndex];
	if(const auto *state=_sessionState.mapState(id.mapId))
		for(const auto &v:state->wallItemOverrides)if(v.id==id) {value.x=v.x;value.y=v.y;break;}
	return value;
}

void XeenWorld::restoreWallItemOverrides(const std::vector<XeenWallItemOverride> &values) {
	if(hasEncounterState() || values.size()>65536)throw std::invalid_argument("Invalid wall-item restoration owner/count");
	for(std::size_t n=0;n<values.size();++n) {
		const auto &v=values[n];static_cast<void>(map(v.id.mapId));
		const auto &file=objectFile(v.id.mapId);
		if(hasEncounterState() || !file.resourcePresent || v.id.recordIndex>=file.entities.wallItems.size())
			throw std::invalid_argument("Restored original wall-item identity does not exist");
		if(v.x<-128 || v.x>127 || v.y<-128 || v.y>127 || (n && !(values[n-1].id<v.id)))
			throw std::invalid_argument("Invalid or duplicate wall-item override");
	}
	std::map<XeenMapIdentity,std::vector<XeenWallItemOverride>> prepared;
	for(const auto &v:values) {prepared[v.id.mapId].push_back(v);static_cast<void>(_sessionState._mapStates.state(v.id.mapId));}
	XeenMutationWatch::write(this);
	for(const auto &entry:_sessionState._mapStates) {
		auto &state=*_sessionState._mapStates.find(entry.first);const auto found=prepared.find(entry.first);
		if(found!=prepared.end())state.wallItemOverrides.swap(found->second);else state.wallItemOverrides.clear();
	}
}

void XeenWorld::restoreEventOverrides(const std::vector<XeenEventOverride> &values,const EventLoader &loader) {
	if(hasEncounterState() || values.size()>65536)throw std::invalid_argument("Invalid Event restoration owner/count");
	std::map<XeenMapIdentity,XeenEventFile> files;
	for(std::size_t n=0;n<values.size();++n) {
		const auto &v=values[n];static_cast<void>(map(v.id.mapId));
		if(!loader)throw std::invalid_argument("Event restoration requires an event loader");
		auto found=files.find(v.id.mapId);
		if(found==files.end())found=files.emplace(v.id.mapId,loader(v.id.mapId)).first;
		const auto &file=found->second;
		if(hasEncounterState() || file.mapId!=v.id.mapId || !file.resourcePresent || v.id.recordIndex>=file.records.size())
			throw std::invalid_argument("Restored original Event identity does not exist");
		if(!v.opcode || _sessionState.isEventDisabled(v.id) || (n && !(values[n-1].id<v.id)))
			throw std::invalid_argument("Invalid or duplicate Event replacement");
	}
	std::map<XeenMapIdentity,std::vector<XeenEventOverride>> prepared;
	for(const auto &v:values) {prepared[v.id.mapId].push_back(v);static_cast<void>(_sessionState._mapStates.state(v.id.mapId));}
	XeenMutationWatch::write(this);
	for(const auto &entry:_sessionState._mapStates) {
		auto &state=*_sessionState._mapStates.find(entry.first);const auto found=prepared.find(entry.first);
		if(found!=prepared.end())state.eventOverrides.swap(found->second);else state.eventOverrides.clear();
	}
}

std::optional<XeenObjectIdentity> XeenWorld::selectObject(const XeenCamera &camera) {
	// An admitted indoor map (its monster table is staged) spans 32x32 cells
	// through its neighbour tiles.
	if (!eventCameraAdmitted(camera))
		throw std::invalid_argument("invalid physical object-selection cell");
	const auto &file = objectFile(camera.mapId);
	if (!file.resourcePresent) return std::nullopt;
	// The first record on the party's cell, with or without a picture (DOS
	// indoor 0x156f0..0x15836, outdoor 0x170c3..0x171d1). A removed or
	// map-rule-hidden record stands at -128, which no cell matches.
	for (std::size_t i = 0; i < file.entities.objects.size(); ++i) {
		XeenObjectIdentity id{camera.mapId, i};
		const auto object = effectiveObject(id);
		if (object.x == camera.x && object.y == camera.y &&
				!_sessionState.isObjectDisabled(id)) return id;
	}
	return std::nullopt;
}

XeenEventRecord XeenWorld::effectiveEvent(XeenEventIdentity id, const XeenEventRecord &base) const {
	XeenEventRecord result = base;
	if(const auto *state=_sessionState.mapState(id.mapId))
		for(const auto &v:state->eventOverrides)if(v.id==id) {result.opcode=v.opcode;break;}
	if (isEventDisabled(id)) result.opcode = 0;
	return result;
}

void XeenWorld::disableObject(XeenObjectIdentity id) {
	validateObject(id);
	auto &state=_sessionState._mapStates.state(id.mapId);
	XeenMutationWatch::write(this);
	state.removedObjects.insert(id);
}

void XeenWorld::validateEventCell(const XeenCamera &physical, const XeenEventFile &events) {
	if (!eventCameraAdmitted(physical) ||
			events.mapId != physical.mapId)
		throw std::invalid_argument("event mutation requires the physical map/cell");
	static_cast<void>(map(physical.mapId));
}

void XeenWorld::disableEventsAtCell(const XeenCamera &physical, const XeenEventFile &events) {
	validateEventCell(physical, events);
	auto &state=_sessionState._mapStates.state(physical.mapId);
	XeenMutationWatch::write(this);
	for (std::size_t i = 0; i < events.records.size(); ++i) {
		const auto &record = events.records[i];
		if (record.x == physical.x && record.y == physical.y)
			state.disabledEvents.insert({physical.mapId, i});
	}
	state.eventOverrides.erase(std::remove_if(state.eventOverrides.begin(),state.eventOverrides.end(),
		[&](const auto &v){return state.disabledEvents.count(v.id)!=0;}),state.eventOverrides.end());
}

void XeenWorld::applyRemove(const XeenCamera &physical,
		std::optional<XeenObjectIdentity> selected, const XeenEventFile &events, const XeenEventPublication *publication) {
	if (_sessionState.journey() && !publication) throw std::logic_error("Journey Remove requires live event authority");
	if (publication) publication->check();
	// Validate and allocate both overlays before publishing either one. Remove is
	// atomic on its own; an earlier script grant remains independently published.
	validateEventCell(physical, events);
	if (selected) {
		if (selected->mapId != physical.mapId)
			throw std::invalid_argument("selected object belongs to another physical map/side");
		validateObject(*selected);
		// The removed object keeps its flip in memory, as the original's does.
		_scenePresentation.objectFlips[*selected] = effectiveObjectFlip(*selected);
	}
	auto &state = _sessionState._mapStates.state(physical.mapId);
	auto disabledObjects = state.removedObjects;
	auto disabledEvents = state.disabledEvents;
	auto objectOverrides = state.objectOverrides;
	if (selected) {
		disabledObjects.insert(*selected);
		xeenRemoveObjectOverride(objectOverrides,*selected);
	}
	for (std::size_t i = 0; i < events.records.size(); ++i) {
		const auto &record = events.records[i];
		if (record.x == physical.x && record.y == physical.y)
			disabledEvents.insert({physical.mapId, i});
	}
	if (publication) publication->prepareRemove(physical, selected, events);
	static_assert(noexcept(state.removedObjects.swap(disabledObjects)));
	static_assert(noexcept(state.disabledEvents.swap(disabledEvents)));
	XeenMutationWatch::write(this);
	state.removedObjects.swap(disabledObjects);
	state.objectOverrides.swap(objectOverrides);
	state.disabledEvents.swap(disabledEvents);
	state.eventOverrides.erase(std::remove_if(state.eventOverrides.begin(),state.eventOverrides.end(),
		[&](const auto &v){return state.disabledEvents.count(v.id)!=0;}),state.eventOverrides.end());
	if (publication) publication->removed();
}

const XeenMap &XeenWorld::map(XeenMapIdentity mapId) {
	if (!mapId)
		throw std::invalid_argument("ID zero does not represent a Xeen map");
	const auto cached = _maps.find(mapId);
	if (cached != _maps.end())
		return cached->second;

	XeenMap loaded = _loader(mapId);
	if (_combatCheck) _combatCheck();
	XeenRestoreGuard::admitLoadedResource(*this,mapId,loaded,loaded.identity()==mapId,
		[](XeenRestoreGuard &g)->auto & {return g.maps;},xeen_state::sameMap);
	XeenMutationWatch::prepareOwned(this,5+loaded.instructions.size());
	const auto result = _maps.emplace(mapId, std::move(loaded));
	++_cacheRevision;
	XeenRestoreGuard::loadedResourceInserted(*this);
	resourceRanges(result.first->second,[this](const void *p,std::size_t n) { XeenMutationWatch::addOwned(this,p,n); });
	return result.first->second;
}

void XeenWorld::discardMapCache() {
	// The immutable value preimages remain in guards. Retire only the addresses
	// being freed, preserving any mutation already observed before eviction.
	const auto retire=[](const void *p,std::size_t n) { XeenMutationWatch::retire(p,n); };
	for(const auto &entry:_maps) resourceRanges(entry.second,retire);
	for(const auto &entry:_objects) resourceRanges(entry.second,retire);
	_maps.clear();_objects.clear();_sceneActors.clear();++_cacheRevision;
}

std::optional<XeenCellSample> XeenWorld::sampleCell(
		XeenMapIdentity mapId, int x, int y) {
	// Gameplay, scene projection and rays share Map::getCell's Y-before-X
	// neighbor resolution. The sample retains its physical tile identity; the
	// caller's logical camera/actor map is never changed by this lookup.
	if(x < -16 || x >=32 || y < -16 || y>=32) return std::nullopt;
	const auto *current=&map(mapId);
	if(y<0 || y>=16) {
		const auto next=current->geometry.neighbors[y<0?2:0];
		if(!next) return std::nullopt;
		y+=y<0?16:-16;current=&map({mapId.side,next});
	}
	if(x<0 || x>=16) {
		const auto next=current->geometry.neighbors[x<0?3:1];
		if(!next) return std::nullopt;
		x+=x<0?16:-16;current=&map({mapId.side,next});
	}
	XeenCellSample result{current->identity(),x,y,&current->geometry,&current->geometry.cells[y*16+x],{}};
	const auto *tile=_sessionState.mapState(result.mapId);
	if(tile)for(const auto &v:tile->cells) if(v.cell==y*16+x) {
		auto cell=std::make_shared<XeenMapCell>(*result.cell);
		if(cell->rawWord!=v.originalWord || cell->rawAttributes!=v.originalAttributes)
			throw std::logic_error("Barrier original resource preimage changed");
		cell->rawWord=v.word();cell->rawAttributes=v.attributes();cell->flags=cell->rawAttributes&0xf8;
		if(result.geometry->isOutdoors()) {
			const unsigned word=cell->rawWord;
			cell->geometry=XeenOutdoorLayers{std::uint8_t(word&15),std::uint8_t((word>>4)&15),
				std::uint8_t((word>>8)&15),std::uint8_t((word>>12)&15)};
			cell->surfaceIndex=word&15;
		} else {
			auto &walls=xeenGet<XeenIndoorWalls>(cell->geometry).walls;
			for(unsigned d=0;d<4;++d)walls[d]=(cell->rawWord>>(12-4*d))&15;
			cell->surfaceIndex=cell->rawAttributes&7;
		}
		result.effectiveCell=std::move(cell);result.cell=result.effectiveCell.get();break;
	}
    if(xeenDrainedCave(result.mapId,_mapRuleView)) {
        auto cell=std::make_shared<XeenMapCell>(*result.cell);cell->rawAttributes&=0xf8;cell->surfaceIndex=0;
        result.effectiveCell=std::move(cell);result.cell=result.effectiveCell.get();
    }
	const unsigned index=y*16+x;
	if(!result.cell->stepped && tile && tile->explored && ((*tile->explored)[index/8]&(1u<<(index%8)))) {
		auto cell=std::make_shared<XeenMapCell>(*result.cell);cell->stepped=true;
		result.effectiveCell=std::move(cell);result.cell=result.effectiveCell.get();
	}
	return result;
}

std::vector<XeenExploredTile> XeenWorld::explorationForOccupiedCell(const XeenPartyState &party,const XeenCamera &camera) {
	auto values=_sessionState.exploredTiles();
	if(!xeenPartyHasSkill(party,4))return values;
	const auto cell=sampleCell(camera.mapId,camera.x,camera.y);
	if(!cell)throw std::invalid_argument("Exploration has no physical occupied cell");
	// Resource defaults already participate in effective sampling. Never copy
	// seen flags or fill a visibility radius into the persistent overlay.
	if(cell->cell->stepped)return values;
	const auto at=std::lower_bound(values.begin(),values.end(),cell->mapId,[](const auto &v,auto id){return v.mapId<id;});
	const auto tile=at!=values.end() && at->mapId==cell->mapId ? at : values.insert(at,{cell->mapId,{}});
	const unsigned index=cell->y*16+cell->x;tile->cells[index/8]|=1u<<(index%8);
	if(values.size()>512)throw std::invalid_argument("Too many explored tiles");
	_sessionState.reserveTiles(values); // Publication of these values never allocates.
	return values;
}

void XeenWorld::recordExploration(std::vector<XeenExploredTile> &values) {
	if(values==_sessionState.exploredTiles())return;
	if(_combatCheck)_combatCheck();
	XeenMutationWatch::write(this);_sessionState.publishExploration(values);
}

void XeenWorld::restoreExploredTiles(const std::vector<XeenExploredTile> &values) {
	if(hasEncounterState() || values.size()>512)throw std::invalid_argument("Invalid exploration restoration owner/count");
	for(std::size_t n=0;n<values.size();++n) {
		const auto &v=values[n];
		if(!v.mapId || (n && !(values[n-1].mapId<v.mapId)))
			throw std::invalid_argument("Invalid explored tile identity/order/bitset");
		if(map(v.mapId).identity()!=v.mapId || hasEncounterState())
			throw std::invalid_argument("Explored tile original resource mismatch");
	}
	_sessionState.reserveTiles(values);XeenMutationWatch::write(this);_sessionState.publishExploration(values);
}

void XeenWorld::setBarrier(const XeenCamera &camera,std::uint8_t wall,bool unlock) {
	if(!detachedEventCandidate() || unsigned(camera.direction)>3 || wall>15)
		throw std::logic_error("Barrier changes require detached capability");
	constexpr int dx[]{0,1,0,-1},dy[]{1,0,-1,0};const unsigned direction=unsigned(camera.direction);
	const auto a=sampleCell(camera.mapId,camera.x,camera.y);
	const auto b=sampleCell(camera.mapId,camera.x+dx[direction],camera.y+dy[direction]);
	if(!a || !b || a->geometry->isOutdoors() || b->geometry->isOutdoors())
		throw std::invalid_argument("Barrier has no two indoor faces");
	auto values=_sessionState.barriers();
	const auto set=[&](const XeenCellSample &s,unsigned d) {
		auto i=std::find_if(values.begin(),values.end(),[&](const auto &v){return v.tile==s.mapId && v.cell==s.y*16+s.x;});
		if(i==values.end()) {
			const auto &base=map(s.mapId).geometry.cells[s.y*16+s.x];
			XeenBarrierOverride v;v.tile=s.mapId;v.cell=s.y*16+s.x;
			v.originalWord=base.rawWord;v.originalAttributes=base.rawAttributes;
			values.push_back(v);i=std::prev(values.end());
		}
		i->mask|=1u<<d;i->walls[d]=wall;i->unlocked=i->unlocked || unlock;
	};
	set(*a,direction);set(*b,direction^2);
	std::sort(values.begin(),values.end(),[](const auto &a,const auto &b){return a.tile<b.tile || (a.tile==b.tile && a.cell<b.cell);});
	XeenMutationWatch::write(this);_sessionState.assignCells(values);
}

void XeenWorld::restoreBarriers(const std::vector<XeenBarrierOverride> &values) {
	if(_sessionState._journeyOwner || _sessionState.journey())throw std::logic_error("Barrier restore requires unpublished World");
	// Each value binds to its own physical tile, whatever logical map reaches it.
	for(unsigned n=0;n<values.size();++n) {
		const auto &v=values[n];
		if(!v.tile || (!v.mask && !v.unlocked && !v.surfaceChanged) || v.mask>15 || v.visitMask || v.visitMap ||
			(v.surfaceChanged ? v.surface>7 : v.surface!=0) ||
			(n && !(values[n-1].tile<v.tile || (values[n-1].tile==v.tile && values[n-1].cell<v.cell))))
			throw std::invalid_argument("Invalid barrier tile/cell/order");
		const auto &tile=map(v.tile);const auto &base=tile.geometry.cells[v.cell];
		if(tile.identity()!=v.tile || (tile.geometry.isOutdoors() && v.surfaceChanged) || base.rawWord!=v.originalWord || base.rawAttributes!=v.originalAttributes)
			throw std::invalid_argument("Saved barrier original resource mismatch");
	}
	for(const auto &v:values) {
		for(unsigned d=0;d<4;++d) {
			if(!(v.mask&(1u<<d))) {if(v.walls[d])throw std::invalid_argument("Noncanonical unused barrier face");continue;}
			if(v.walls[d]>15)throw std::invalid_argument("Invalid saved wall nibble");
			if(v.visitWalls[d])throw std::invalid_argument("Saved visit-only wall");
		}
	}
	XeenMutationWatch::write(this);_sessionState.assignCells(values);
}

std::unique_ptr<XeenWorld> XeenWorld::transitionCandidate() const {
	if (!regionalJourney())
		throw std::logic_error("Vertigo candidate requires admitted city content");
	auto candidate=std::make_unique<XeenWorld>(_baseLoader,_baseObjectLoader);
	candidate->_sessionState=_sessionState;
	// The interpreter operates only on detached values; live Journey authority
	// remains with the source world and its Flow lease.
	candidate->_sessionState._entry=XeenEncounterEntry::Ordinary;
	candidate->_detachedEventCandidate=true;
	candidate->_developerEventMap=_developerEventMap;
    candidate->_inputMapLoadGeneration=_inputMapLoadGeneration;
    candidate->_pendingLoadSide=_pendingLoadSide;
	candidate->_maps=_maps;candidate->_objects=_objects;
	candidate->_cityStatistics=_cityStatistics;candidate->_darksideStatistics=_darksideStatistics;
	candidate->_scenePresentation=_scenePresentation;candidate->_mapRuleView=_mapRuleView;
	// Detached work stages on a copy; publishTransition returns it.
	candidate->_ownTreasureStaging.publish(treasureStaging().value());
	candidate->_transitionTreasureBase=treasureStaging().revision();
	candidate->_transitionTreasureSnapshot=treasureStaging().value();
	candidate->_transitionTreasureCopy=candidate->_ownTreasureStaging.revision();
	return candidate;
}

void XeenWorld::copyEventParty(const XeenPartyState &source, XeenPartyState &candidate) const {
	if(!_detachedEventCandidate || &source==&candidate || candidate._gameplayBorrow.borrowed() ||
		candidate.roster._gameplayBorrow.borrowed() || candidate.roster._combatMarked || candidate.party.size())
		throw std::logic_error("Event party preparation requires fresh detached storage");
	for(unsigned owner=0;owner<XeenRoster::kCharacterCount;++owner) {
		if(candidate.roster._combatInputs[owner])throw std::logic_error("Event party supplements already exist");
		candidate.roster._characters[owner]=source.roster._characters[owner];
		candidate.roster._combatInputs[owner]=source.roster._combatInputs[owner];
	}
	candidate.roster._combatMarked=source.roster._combatMarked;
	candidate.party=source.party;
	candidate.encounterContext=source.encounterContext;candidate.purse=source.purse;
	candidate.serviceEconomy=source.serviceEconomy;candidate.questItems=source.questItems;
	candidate.questFlags=source.questFlags;candidate.worldFlags=source.worldFlags;
	candidate.food=source.food;candidate.soundOptions=source.soundOptions;
    candidate.playTimeSeconds=source.playTimeSeconds;candidate.cloudsCompleted=source.cloudsCompleted;candidate.darksideCompleted=source.darksideCompleted;candidate.worldCompleted=source.worldCompleted;
	candidate.insideInn=source.insideInn;candidate.firstSerializedCount=source.firstSerializedCount;candidate.effectiveSerializedCount=source.effectiveSerializedCount;
	candidate.diagnostics=source.diagnostics;
}

void XeenWorld::applyAlterEvent(const XeenCamera &physical, std::uint8_t line,
		std::uint8_t replacement, const XeenEventFile &events) {
	XeenMutationWatch::write(this);
	if (!detachedEventCandidate() || !regionalJourney() || _sessionState._entry!=XeenEncounterEntry::Ordinary ||
		physical.mapId!=events.mapId)
		throw std::invalid_argument("AlterEvent replacement is unsupported");
	validateEventCell(physical,events);
	// DOS compares the sign-extended operand (0x2827f..0x28287) with the index
	// line, stored zero-extended (0x21e75..0x21e7e): lines 128-255 never match.
	if(line>=128)return;
	auto &state=_sessionState._mapStates.state(physical.mapId);
	const auto replace=[&](std::size_t i) {
		const XeenEventIdentity id{physical.mapId,i};
		state.eventOverrides.erase(std::remove_if(state.eventOverrides.begin(),state.eventOverrides.end(),
			[&](const auto &v){return v.id==id;}),state.eventOverrides.end());
		state.disabledEvents.erase(id);
		if(!replacement)state.disabledEvents.insert(id);
		else state.eventOverrides.push_back({id,replacement});
	};
	for (std::size_t i=0;i<events.records.size();++i) {
		const auto &r=events.records[i];
		if (r.x==physical.x && r.y==physical.y && r.line==line &&
			(r.direction==static_cast<std::uint8_t>(physical.direction) || r.direction==4)) {
			// DOS also replaces the following physical record when the current
			// effective opcode is PlayCD (0x282c0..0x28304).
			if(effectiveEvent({physical.mapId,i},r).opcode==60 && i+1<events.records.size())replace(i+1);
			replace(i);
		}
	}
	// No matching physical line is a DOS no-op (0x28189..0x28356).
	std::sort(state.eventOverrides.begin(),state.eventOverrides.end(),[](const auto &a,const auto &b){return a.id<b.id;});
}

void XeenWorld::applyMoveWallObject(XeenWallItemIdentity id,int x,int y) {
	if(!detachedEventCandidate() || x<-128 || x>127 || y<-128 || y>127)
		throw std::invalid_argument("MoveWallObj requires a detached Event and signed-byte coordinates");
	const auto &file=objectFile(id.mapId);
	if(!file.resourcePresent || id.recordIndex>=kWallItemCapacity)
		throw std::invalid_argument("MoveWallObj index is outside the allocated wall slots");
	// DOS writes allocated but unused slots. Rendering, animation and saves
	// stop at the live count, and map load clears these slots (0x2860e).
	if(id.recordIndex>=file.entities.wallItems.size())return;
	static_cast<void>(effectiveWallItem(id));
	auto &values=_sessionState._mapStates.state(id.mapId).wallItemOverrides;
	auto at=std::lower_bound(values.begin(),values.end(),id,[](const auto &v,auto key){return v.id<key;});
	XeenMutationWatch::write(this);
	if(at!=values.end() && at->id==id)*at={id,x,y};
	else {values.push_back({id,x,y});std::sort(values.begin(),values.end(),[](const auto &a,const auto &b){return a.id<b.id;});}
}

void XeenWorld::applyMakeNothingHere(const XeenCamera &physical,const XeenEventFile &events) {
	if(!detachedEventCandidate())throw std::logic_error("MakeNothingHere requires a detached Event");
	disableEventsAtCell(physical,events);
}

namespace {
XeenBarrierOverride &eventCell(std::vector<XeenBarrierOverride> &values,const XeenCellSample &sample,const XeenMapCell &base) {
	const unsigned cell=sample.y*16+sample.x;
	auto at=std::lower_bound(values.begin(),values.end(),cell,[](const auto &v,unsigned n){return v.cell<n;});
	if(at==values.end() || at->cell!=cell) {
		XeenBarrierOverride value;value.tile=sample.mapId;value.cell=cell;
		value.originalWord=base.rawWord;value.originalAttributes=base.rawAttributes;
		at=values.insert(at,value);
	}
	return *at;
}
}

void XeenWorld::applyAlterMap(XeenMapIdentity logical,int x,int y,unsigned direction,unsigned wall) {
	if(!detachedEventCandidate() || direction>4 || wall>15)throw std::invalid_argument("Invalid AlterMap owner or operands");
	auto sample=sampleCell(logical,x,y);
	if(!sample && x>=-16 && x<32 && y>=-16 && y<32) {
		// setWall ignores mazeLookup's missing-neighbour return. Its DAT index
		// still names the last resolved tile, then x/y are masked (DOS
		// 0x11db7..0x11def, 0x11e1e..0x11e3c, 0x234f5..0x23549).
		auto physical=logical;
		bool resolved=true;
		if(y<0 || y>=16) {
			const auto next=map(physical).geometry.neighbors[y<0?2:0];
			if(next)physical={logical.side,next};else resolved=false;
		}
		if(resolved && (x<0 || x>=16)) {
			const auto next=map(physical).geometry.neighbors[x<0?3:1];
			if(next)physical={logical.side,next};
		}
		sample=sampleCell(physical,x&15,y&15);
	}
	if(!sample)throw std::invalid_argument("AlterMap has no physical DAT cell");
	const bool visit=map(logical).geometry.isOutdoors() && sample->mapId!=logical;
	const auto &base=map(sample->mapId).geometry.cells[sample->y*16+sample->x];
	XeenMutationWatch::write(this);
	auto &value=eventCell(_sessionState._mapStates.state(sample->mapId).cells,*sample,base);
	for(unsigned d=0;d<4;++d)if(direction==4 || direction==d) {
		if(visit) {value.visitMap=logical;value.visitMask|=1u<<d;value.visitWalls[d]=wall;}
		else {value.mask|=1u<<d;value.walls[d]=wall;}
	}
}

void XeenWorld::applyAlterCellFlag(XeenMapIdentity logical,int x,int y,unsigned surface) {
	if(!detachedEventCandidate())throw std::logic_error("AlterCellFlag requires a detached Event");
	const bool outdoor=map(logical).geometry.isOutdoors();
	// DOS does not mask outdoor x/y after lookup. No valid original operand
	// witness establishes neighbour writes; do not invent an aliased address.
	if(outdoor && (x<0 || x>=16 || y<0 || y>=16))
		throw std::invalid_argument("Outdoor AlterCellFlag neighbour operand is unverified");
	if(surface>(outdoor?15u:7u))throw std::invalid_argument("AlterCellFlag surface is outside its cell encoding");
	if(outdoor) {applyAlterMap(logical,x,y,3,surface);return;}
	const auto sample=sampleCell(logical,x,y);
	if(!sample)throw std::invalid_argument("AlterCellFlag has no physical DAT cell");
	const auto &base=map(sample->mapId).geometry.cells[sample->y*16+sample->x];
	XeenMutationWatch::write(this);
	auto &value=eventCell(_sessionState._mapStates.state(sample->mapId).cells,*sample,base);
	value.surfaceChanged=true;value.surface=surface;
}

void XeenWorld::publishTransition(XeenWorld &candidate) noexcept {
 _scenePresentation.lightingStepped=candidate._scenePresentation.lightingStepped;
 _scenePresentation.lightingPrepared=candidate._scenePresentation.lightingPrepared;
	XeenMutationWatch::write(this);XeenMutationWatch::write(&candidate);
    _mapRuleView=candidate._mapRuleView;
    _pendingLoadSide=candidate._pendingLoadSide;
    _inputMapLoadGeneration=candidate._inputMapLoadGeneration;
	_sessionState._journeyRandom.swap(candidate._sessionState._journeyRandom);
	_sessionState._darksideSong.swap(candidate._sessionState._darksideSong);
	// The whole per-map table moves as one value.
	_sessionState._mapStates.swap(candidate._sessionState._mapStates);
	_sessionState._accountedMonsters.swap(candidate._sessionState._accountedMonsters);
	_sessionState._headCache.swap(candidate._sessionState._headCache);
	_cityStatistics.swap(candidate._cityStatistics);_darksideStatistics.swap(candidate._darksideStatistics);
	_maps.swap(candidate._maps);_objects.swap(candidate._objects);
	// Map transitions keep pending treasure; only a changed copy is published.
	if(candidate.transitionTreasureChanged()) treasureStaging().publish(candidate.treasureStaging().value());
	++_ownerRevision;
}

void XeenWorld::retainEventRandom(const XeenJourneyRandomState &random) {
	if(!detachedEventCandidate() || !_sessionState._journeyRandom)
		throw std::logic_error("Event RNG requires retained detached Journey state");
	_sessionState._journeyRandom=random;
}

void XeenWorld::publishEventParty(XeenPartyState &live,const XeenPartyState &candidate) noexcept {
	for(unsigned owner=0;owner<30;++owner) {
		xeenEventCopyOwnedCharacter(live.roster._characters[owner],candidate.roster._characters[owner]);
		if(live.roster._combatInputs[owner])*live.roster._combatInputs[owner]=*candidate.roster._combatInputs[owner];
	}
	live.encounterContext=candidate.encounterContext;live.worldFlags=candidate.worldFlags;
	live.food=candidate.food;live.soundOptions=candidate.soundOptions;live.questItems=candidate.questItems;
	if(live.serviceEconomy && candidate.serviceEconomy)*live.serviceEconomy=*candidate.serviceEconomy;
	if(live.purse && candidate.purse)*live.purse=*candidate.purse;
}

XeenHeadIndicators XeenWorld::headIndicators(const XeenCamera &camera) {
 if(camera.x<0 || camera.y<0)throw std::invalid_argument("Negative head coordinate");
 const unsigned cell=(camera.y%16)*16+camera.x%16;
 const auto &cache=_sessionState._headCache;
 if(cache && cache->mapId==camera.mapId && cache->overrides[cell])return *cache->overrides[cell];
 const auto &heads=map(camera.mapId).heads;
 if(!heads)throw std::runtime_error("Primary map has no HED resource");
 return (*heads)[cell];
}
void XeenWorld::alterHeads(const XeenCamera &camera,XeenHeadIndicators heads) {
 if(!detachedEventCandidate() || camera.x<0 || camera.y<0)throw std::logic_error("Head changes require a detached Event");
 XeenMutationWatch::write(this);
 auto &cache=_sessionState._headCache;
 if(!cache || cache->mapId!=camera.mapId)cache=XeenHeadCache{camera.mapId,{}};
 cache->overrides[(camera.y%16)*16+camera.x%16]=heads;
}
void XeenWorld::reloadHeads(XeenMapIdentity destination) {
 if(!detachedEventCandidate())throw std::logic_error("Head reload requires a detached map transition");
 XeenMutationWatch::write(this);
 _sessionState._headCache.reset();
 static_cast<void>(map(destination));
}
void XeenWorld::restoreHeads(const std::optional<XeenHeadCache> &heads,const XeenCamera &camera) {
 if(hasEncounterState() || (heads && heads->mapId!=camera.mapId))throw std::invalid_argument("Invalid restored head cache");
 XeenMutationWatch::write(this);_sessionState._headCache=heads;
}

std::uint8_t XeenWorld::tavernTipIndex(XeenMapIdentity id) {
 const auto *state=_sessionState.mapState(id);
 return state && state->tavernTips ? std::uint8_t(*state->tavernTips) : std::uint8_t(map(id).geometry.tavernTips);
}
void XeenWorld::setTavernTipIndex(XeenMapIdentity id,std::uint8_t index) {
 static_cast<void>(map(id));
 XeenMutationWatch::write(this);
 _sessionState._mapStates.state(id).tavernTips.emplace(index);
}

} // namespace mmodern
