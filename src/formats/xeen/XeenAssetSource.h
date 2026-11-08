#ifndef MMODERN_FORMATS_XEEN_ASSET_SOURCE_H
#define MMODERN_FORMATS_XEEN_ASSET_SOURCE_H

#include "formats/xeen/XeenLanguage.h"
#include "core/GameInstallation.h"
#include "core/IndexedFrame.h"
#include "core/IndoorRenderScene.h"
#include "formats/xeen/XeenSpriteDrawOptions.h"
#include "formats/xeen/XeenMonsterAppearance.h"
#include "formats/xeen/XeenDosText.h"
#include "games/xeen/XeenMapIdentity.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mmodern {

struct XeenObjectVisual;

class XeenAssetSource {
public:
	// M110: the language of archive text and of the font, fixed by the installation.
	XeenLanguage language() const;
	explicit XeenAssetSource(const GameInstallation &installation);
	XeenAssetSource(const GameInstallation &installation, int width, int height);
	~XeenAssetSource();

	XeenAssetSource(const XeenAssetSource &) = delete;
	XeenAssetSource &operator=(const XeenAssetSource &) = delete;

	void loadPalette(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	void loadRawFramebuffer(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	void drawSprite(const std::string &resourceName, std::size_t frame, int x, int y);
	void drawSprite(const std::string &resourceName, std::size_t frame, int x, int y,
		const XeenSpriteDrawOptions &options);
	IndexedFrame snapshot() const;
	std::shared_ptr<const IndoorTexture> spriteTexture(const std::string &, std::size_t, XeenSceneArchive);
	void beginInterfaceCoverage();
	std::vector<std::uint8_t> endInterfaceCoverage();
 void blackenScene();
	// Transient NPC composition uses the existing cache, never the world surface.
	void drawNpc(IndexedFrame &frame, std::uint8_t portraitId, std::size_t portraitFrame,XeenSceneArchive archive=XeenSceneArchive::Current);
 void drawDialogSprite(IndexedFrame &,const char *,unsigned,int,int, XeenSceneArchive selection = XeenSceneArchive::Current,int scale=0,unsigned flags=0);
 IndexedFrame cursorImage();
 IndexedFrame cursorImage(XeenSceneArchive selection);
 IndexedFrame restDreamImage(XeenSceneArchive archive=XeenSceneArchive::Current);
	void drawSmith(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawTraining(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawTemple(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	std::string tavernFoodOffer(unsigned index, XeenSceneArchive archive = XeenSceneArchive::Clouds);
    std::string tavernString(std::size_t index, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawTavern(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawBank(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawGuild(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void discardSpriteCache();
	std::size_t cachedSpriteCount() const;
	// Successful resource reads + SpriteResource constructions, not draw calls.
	std::size_t spriteLoadCount() const;
	std::size_t spriteFrameCount(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	// Presentation lookup across installed CC archives, independent of gameplay reads.
	bool hasSceneResource(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	void drawSceneSprite(const std::string &resourceName, std::size_t frame,
		int x, int y, const XeenSpriteDrawOptions &options = {});
	static std::string normalMonsterResource(std::uint8_t image);
	void validateProjectile(bool enemy);
	void drawProjectile(bool enemy,unsigned row,int x,int y,const XeenSpriteDrawOptions &);
 void drawProjectile(unsigned pow,bool enemy,unsigned row,int x,int y,const XeenSpriteDrawOptions &);
	void validateNormalMonster(std::uint8_t image,XeenSceneArchive archive=XeenSceneArchive::Current);
	void validateAttackMonster(std::uint8_t image,XeenSceneArchive archive=XeenSceneArchive::Current);
	static std::string attackMonsterResource(std::uint8_t image);
	void drawMonster(std::uint8_t image, XeenMonsterAppearance appearance, int x, int y,
		const XeenSpriteDrawOptions &options);
	void drawNormalMonster(std::uint8_t image, std::size_t frame, int x, int y,
		const XeenSpriteDrawOptions &options);
	// Explicit physical origin; nullopt means archive/member absent, not empty.
	std::optional<std::vector<std::uint8_t>> readObjectVisualMetadata(XeenSide side=XeenSide::Clouds);
	// Optional commercial English material names from exactly DARK.CC/mae.xen.
	std::optional<std::vector<std::uint8_t>> readItemMaterialNamesFromDarkArchive();
	std::optional<std::vector<std::uint8_t>> readLearnedSpellNamesFromDarkArchive();
	// Missing archive/member: nullopt; extent/read failures: exception. Parsing is separate.
	std::optional<std::vector<std::uint8_t>> readMonsterStatistics(XeenSide side=XeenSide::Clouds);
	void drawObjectVisual(const XeenObjectVisual &visual, int x, int y,
		const XeenSpriteDrawOptions &options = {});
	bool hasArchiveResource(const std::string &resourceName);
	std::vector<std::uint8_t> readArchiveResource(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	bool hasInitialResource(const std::string &resourceName, XeenSide side=XeenSide::Clouds);
	std::vector<std::uint8_t> readInitialResource(const std::string &resourceName, XeenSide side=XeenSide::Clouds);
 const XeenDosText &uiText();
 std::string mapName(XeenMapIdentity);

private:
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

} // namespace mmodern

#endif
