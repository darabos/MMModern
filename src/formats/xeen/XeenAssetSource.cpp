#include "formats/xeen/XeenAssetSource.h"

#include "formats/xeen/XeenRussianTranslation.h"
#include "compat/scummvm/ScummVmXeenBridge.h"
#include "games/xeen/XeenObjectVisual.h"
#include <stdexcept>
#include <algorithm>
#include <cstdio>

namespace mmodern {

struct XeenAssetSource::Impl {
	ScummVmXeenBridge bridge;
 GameInstallation installation;
 std::unique_ptr<XeenDosText> text;

	explicit Impl(const GameInstallation &source) : bridge(source),installation(source) {}

	Impl(const GameInstallation &installation, int width, int height) :
		bridge(installation, width, height),installation(installation) {
	}
};

XeenAssetSource::XeenAssetSource(const GameInstallation &installation) :
	_impl(new Impl(installation)) {
 if(installation.uiModule)uiText();
}

XeenAssetSource::XeenAssetSource(const GameInstallation &installation,
		int width, int height) :
	_impl(new Impl(installation, width, height)) {
 if(installation.uiModule)uiText();
}

XeenAssetSource::~XeenAssetSource() = default;
std::shared_ptr<const IndoorTexture> XeenAssetSource::spriteTexture(
		const std::string &name, std::size_t frame, XeenSceneArchive archive) {
	return _impl->bridge.spriteTexture(name, frame, archive);
}
void XeenAssetSource::beginInterfaceCoverage() { _impl->bridge.beginInterfaceCoverage(); }
std::vector<std::uint8_t> XeenAssetSource::endInterfaceCoverage() {
	return _impl->bridge.endInterfaceCoverage();
}
const XeenDosText &XeenAssetSource::uiText() {
 if(!_impl->text) {
  if(!_impl->installation.uiModule)throw std::runtime_error("Missing installed uncompressed DOS UI module; supply --ui-data");
  auto stream=_impl->installation.uiModule->open();
  if(stream->size()>1024*1024)throw std::runtime_error("DOS UI module exceeds bounded reader capacity");
  std::vector<std::uint8_t> bytes(static_cast<std::size_t>(stream->size()));
  if(stream->read(bytes.data(),bytes.size())!=bytes.size())throw std::runtime_error("Incomplete DOS UI module read");
  // M110 Part B: program text comes from the identified Russian program, over
  // the CD program for the CD-only fields.
  if(const auto &translation=_impl->installation.translation) {
   const auto russian=ReadOnlyDataFile::plain(translation->program(false)).open();
   if(russian->size()>1024*1024)throw std::runtime_error("Russian DOS UI module exceeds bounded reader capacity");
   std::vector<std::uint8_t> program(static_cast<std::size_t>(russian->size()));
   if(russian->read(program.data(),program.size())!=program.size())throw std::runtime_error("Incomplete Russian DOS UI module read");
   _impl->text=std::make_unique<XeenDosText>(bytes,program);
  } else _impl->text=std::make_unique<XeenDosText>(bytes);
 }
 return *_impl->text;
}
XeenLanguage XeenAssetSource::language() const { return xeenLanguage(_impl->installation); }
std::string XeenAssetSource::mapName(XeenMapIdentity mapId) {
 if(!mapId || mapId.number>9999)throw std::invalid_argument("Invalid map name identity");
 char name[16];
 std::snprintf(name,sizeof(name),mapId.number>=100?"%sx%03u.txt":"%s%04u.txt",
     xeenMapResources(mapId).mapArchive==XeenSide::Clouds?"xeen":"dark",unsigned(mapId.number));
 const auto bytes=readArchiveResource(name,XeenSceneArchive::DarksideOnly);
 const auto limit=bytes.begin()+std::min<std::size_t>(bytes.size(),32);
 const auto end=std::find(bytes.begin(),limit,0);
 if(end==bytes.end() && bytes.size()<32)throw std::runtime_error("Unterminated map name resource");
 return std::string(bytes.begin(),end);
}
IndexedFrame XeenAssetSource::cursorImage() { return _impl->bridge.cursorImage(); }
IndexedFrame XeenAssetSource::cursorImage(XeenSceneArchive selection) { return _impl->bridge.cursorImage(selection); }
IndexedFrame XeenAssetSource::restDreamImage(XeenSceneArchive archive) { return _impl->bridge.restDreamImage(archive);}

std::size_t XeenAssetSource::spriteFrameCount(const std::string &name, XeenSceneArchive selection) {
	return _impl->bridge.spriteFrameCount(name,selection);
}

bool XeenAssetSource::hasSceneResource(const std::string &name, XeenSceneArchive selection) {
	return _impl->bridge.hasSceneResource(name,selection);
}

void XeenAssetSource::drawSceneSprite(const std::string &name, std::size_t frame,
		int x, int y, const XeenSpriteDrawOptions &options) {
	_impl->bridge.drawSceneSprite(name, frame, x, y, options);
}

std::string XeenAssetSource::normalMonsterResource(std::uint8_t image) {
	const auto number = std::to_string(image);
	return std::string(3 - number.size(), '0') + number + ".mon";
}
void XeenAssetSource::validateProjectile(bool enemy) { _impl->bridge.validateProjectile(enemy?"pow12.icn":"pow11.icn"); }
void XeenAssetSource::drawProjectile(bool enemy,unsigned row,int x,int y,const XeenSpriteDrawOptions &options) {
 drawProjectile(enemy?12u:11u,enemy,row,x,y,options);
}
void XeenAssetSource::drawProjectile(unsigned pow,bool enemy,unsigned row,int x,int y,const XeenSpriteDrawOptions &options) {
 if(row>3) throw std::invalid_argument("Invalid projectile row");
 if(pow>15)throw std::invalid_argument("Invalid projectile resource");
 const auto name="pow"+std::to_string(pow)+".icn";
 _impl->bridge.validateProjectile(name,options.archive);constexpr unsigned frames[2][4]{{0,1,2,0},{0,2,1,0}};
 _impl->bridge.drawObjectSprite(name,frames[enemy][row],x,y,options);
}
void XeenAssetSource::validateNormalMonster(std::uint8_t image,XeenSceneArchive archive) {
	_impl->bridge.validateNormalMonster(normalMonsterResource(image),archive);
}
std::string XeenAssetSource::attackMonsterResource(std::uint8_t image) {
	auto resource = normalMonsterResource(image);
	resource.replace(4, 3, "att");
	return resource;
}
void XeenAssetSource::validateAttackMonster(std::uint8_t image,XeenSceneArchive archive) {
	_impl->bridge.validateAttackMonster(attackMonsterResource(image),archive);
}
void XeenAssetSource::drawMonster(std::uint8_t image, XeenMonsterAppearance appearance, int x, int y,
		const XeenSpriteDrawOptions &options) {
	if (!appearance.valid() || options.horizontalFlip || options.enlarge || !options.sceneClipped)
		throw std::invalid_argument("Unsupported monster drawing");
	const bool attack = appearance.kind == XeenMonsterSpriteKind::Attack;
	if (attack) validateAttackMonster(image,options.archive); else validateNormalMonster(image,options.archive);
	_impl->bridge.drawObjectSprite(attack ? attackMonsterResource(image) : normalMonsterResource(image),
		appearance.frame, x, y, options);
}
void XeenAssetSource::drawNormalMonster(std::uint8_t image, std::size_t frame, int x, int y,
		const XeenSpriteDrawOptions &options) {
	if (frame >= 8 || options.horizontalFlip || options.enlarge || !options.sceneClipped)
		throw std::invalid_argument("Unsupported normal monster drawing");
	const auto resource = normalMonsterResource(image);
	_impl->bridge.validateNormalMonster(resource,options.archive);
	_impl->bridge.drawObjectSprite(resource, frame, x, y, options);
}

void XeenAssetSource::discardSpriteCache() { _impl->bridge.discardSpriteCache(); }
std::size_t XeenAssetSource::cachedSpriteCount() const { return _impl->bridge.cachedSpriteCount(); }
std::size_t XeenAssetSource::spriteLoadCount() const { return _impl->bridge.spriteLoadCount(); }

std::optional<std::vector<std::uint8_t>> XeenAssetSource::readObjectVisualMetadata(XeenSide side) {
	return _impl->bridge.readObjectVisualMetadata(side);
}

std::optional<std::vector<std::uint8_t>> XeenAssetSource::readItemMaterialNamesFromDarkArchive() {
	return _impl->bridge.readItemMaterialNamesFromDarkArchive();
}

std::optional<std::vector<std::uint8_t>> XeenAssetSource::readLearnedSpellNamesFromDarkArchive() {
	return _impl->bridge.readLearnedSpellNamesFromDarkArchive();
}

std::optional<std::vector<std::uint8_t>> XeenAssetSource::readMonsterStatistics(XeenSide side) {
	return _impl->bridge.readMonsterStatistics(side);
}

void XeenAssetSource::drawObjectVisual(const XeenObjectVisual &visual, int x, int y,
		const XeenSpriteDrawOptions &options) {
	if ((visual.status != XeenObjectVisualStatus::SupportedStatic &&
		visual.status != XeenObjectVisualStatus::SupportedAnimated) ||
		!visual.identity.mapId)
		throw std::runtime_error("cannot draw unsupported object visual: " + visual.diagnostic);
	auto resolvedOptions = options;
	resolvedOptions.horizontalFlip = visual.horizontalFlip;
	_impl->bridge.drawObjectSprite(visual.spriteName, visual.frame, x, y, resolvedOptions);
}

bool XeenAssetSource::hasArchiveResource(const std::string &resourceName) {
	return _impl->bridge.hasArchiveResource(resourceName);
}

std::vector<std::uint8_t> XeenAssetSource::readArchiveResource(const std::string &resourceName, XeenSceneArchive selection) {
	return _impl->bridge.readArchiveResource(resourceName,selection);
}

bool XeenAssetSource::hasInitialResource(const std::string &resourceName, XeenSide side) {
	return _impl->bridge.hasInitialResource(resourceName,side);
}

std::vector<std::uint8_t> XeenAssetSource::readInitialResource(const std::string &resourceName, XeenSide side) {
	return _impl->bridge.readInitialResource(resourceName,side);
}

void XeenAssetSource::loadPalette(const std::string &resourceName, XeenSceneArchive selection) {
	_impl->bridge.loadPalette(resourceName,selection);
}

void XeenAssetSource::loadRawFramebuffer(const std::string &resourceName, XeenSceneArchive selection) {
	_impl->bridge.loadRawFramebuffer(resourceName,selection);
}

void XeenAssetSource::drawSprite(const std::string &resourceName,
		std::size_t frame, int x, int y) {
	_impl->bridge.drawSprite(resourceName, frame, x, y);
}

void XeenAssetSource::drawSprite(const std::string &resourceName,
		std::size_t frame, int x, int y, const XeenSpriteDrawOptions &options) {
	_impl->bridge.drawSprite(resourceName, frame, x, y, options);
}

IndexedFrame XeenAssetSource::snapshot() const {
	return _impl->bridge.snapshot();
}
void XeenAssetSource::blackenScene() { _impl->bridge.blackenScene(); }

void XeenAssetSource::drawSmith(IndexedFrame &frame, XeenSceneArchive archive) {
	_impl->bridge.drawSmith(frame,archive);
}
void XeenAssetSource::drawTraining(IndexedFrame &frame, XeenSceneArchive archive) {
	_impl->bridge.drawTraining(frame,archive);
}
std::string XeenAssetSource::tavernFoodOffer(unsigned index, XeenSceneArchive archive) {return tavernString(index,archive);}
std::string XeenAssetSource::tavernString(std::size_t wanted, XeenSceneArchive archive) {
 if(wanted>77)throw std::out_of_range("Tavern string index");
 const auto bytes=readArchiveResource("tavern.bin",archive);
 if(bytes.size()>65536)throw std::runtime_error("Tavern strings exceed bounded reader capacity");
 std::size_t begin=0;
 for(std::size_t index=0;index<=wanted;++index) {
  auto end=begin;while(end<bytes.size() && bytes[end])++end;
  if(end==bytes.size())throw std::runtime_error("Missing or unterminated Tavern string");
  if(index==wanted) {
   if(end-begin>4096)throw std::runtime_error("Tavern string exceeds bounded reader capacity");
   std::string offer(bytes.begin()+begin,bytes.begin()+end);XeenDosText::validateControls(offer,language());return offer;
  }
  begin=end+1;
 }
 throw std::runtime_error("Missing Tavern string");
}
void XeenAssetSource::drawTavern(IndexedFrame &frame, XeenSceneArchive archive) { _impl->bridge.drawTavern(frame,archive); }
void XeenAssetSource::drawBank(IndexedFrame &frame, XeenSceneArchive archive) { _impl->bridge.drawBank(frame,archive); }
void XeenAssetSource::drawGuild(IndexedFrame &frame, XeenSceneArchive archive) { _impl->bridge.drawGuild(frame,archive); }
void XeenAssetSource::drawTemple(IndexedFrame &frame, XeenSceneArchive archive) {
	_impl->bridge.drawTemple(frame,archive);
}

void XeenAssetSource::drawNpc(IndexedFrame &frame, std::uint8_t portraitId,
		std::size_t portraitFrame,XeenSceneArchive archive) {
	_impl->bridge.drawNpc(frame,portraitId,portraitFrame,archive);
}

void XeenAssetSource::drawDialogSprite(IndexedFrame &frame,const char *name,unsigned index,int x,int y,XeenSceneArchive selection,int scale,unsigned flags) {
	_impl->bridge.drawDialogSprite(frame,name,index,x,y,selection,scale,flags);
}

} // namespace mmodern
