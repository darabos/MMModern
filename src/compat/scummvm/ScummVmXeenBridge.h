#ifndef MMODERN_COMPAT_SCUMMVM_XEEN_BRIDGE_H
#define MMODERN_COMPAT_SCUMMVM_XEEN_BRIDGE_H

#include "core/GameInstallation.h"
#include "core/IndexedFrame.h"
#include "core/IndoorRenderScene.h"
#include "formats/xeen/XeenSpriteDrawOptions.h"

#include "games/xeen/XeenMapIdentity.h"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace mmodern {

class ScummVmXeenBridge {
public:
	explicit ScummVmXeenBridge(const GameInstallation &installation);
	ScummVmXeenBridge(const GameInstallation &installation, int width, int height);
	~ScummVmXeenBridge();

	ScummVmXeenBridge(const ScummVmXeenBridge &) = delete;
	ScummVmXeenBridge &operator=(const ScummVmXeenBridge &) = delete;

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
 IndexedFrame cursorImage();
 IndexedFrame cursorImage(XeenSceneArchive selection);
 IndexedFrame restDreamImage(XeenSceneArchive archive=XeenSceneArchive::Current);
	void drawNpc(IndexedFrame &frame, std::uint8_t portraitId, std::size_t portraitFrame,XeenSceneArchive archive=XeenSceneArchive::Current);
	void drawDialogSprite(IndexedFrame &,const char *,unsigned,int,int, XeenSceneArchive selection = XeenSceneArchive::Current,int scale=0,unsigned flags=0);
	void drawSmith(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawTraining(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawTemple(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawTavern(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawBank(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void drawGuild(IndexedFrame &frame, XeenSceneArchive archive = XeenSceneArchive::Clouds);
	void discardSpriteCache();
	std::size_t cachedSpriteCount() const;
	// Successful resource reads + SpriteResource constructions, not draw calls.
	std::size_t spriteLoadCount() const;
	std::size_t spriteFrameCount(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	// Scene-only archive resolution; gameplay archive reads remain Clouds-only.
	bool hasSceneResource(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	void drawSceneSprite(const std::string &resourceName, std::size_t frame,
		int x, int y, const XeenSpriteDrawOptions &options);
	void validateProjectile(const std::string &resourceName,XeenSceneArchive archive=XeenSceneArchive::Current);
	void validateNormalMonster(const std::string &resourceName,XeenSceneArchive archive=XeenSceneArchive::Current);
	void validateAttackMonster(const std::string &resourceName,XeenSceneArchive archive=XeenSceneArchive::Current);
	std::optional<std::vector<std::uint8_t>> readObjectVisualMetadata(XeenSide side=XeenSide::Clouds);
	std::optional<std::vector<std::uint8_t>> readItemMaterialNamesFromDarkArchive();
	std::optional<std::vector<std::uint8_t>> readLearnedSpellNamesFromDarkArchive();
	std::optional<std::vector<std::uint8_t>> readMonsterStatistics(XeenSide side=XeenSide::Clouds);
	void drawObjectSprite(const std::string &resourceName, std::size_t frame,
		int x, int y, const XeenSpriteDrawOptions &options);
	bool hasArchiveResource(const std::string &resourceName);
	std::vector<std::uint8_t> readArchiveResource(const std::string &resourceName, XeenSceneArchive selection = XeenSceneArchive::Current);
	bool hasInitialResource(const std::string &resourceName, XeenSide side=XeenSide::Clouds);
	std::vector<std::uint8_t> readInitialResource(const std::string &resourceName, XeenSide side=XeenSide::Clouds);
	// Read-only DARK New Game members, in the pinned SaveArchive::reset order.
	std::vector<std::uint8_t> readInitialDarkResource(const std::string &resourceName);

private:
	void drawSpriteImpl(const std::string &resourceName, std::size_t frame,
		int x, int y, const XeenSpriteDrawOptions &options, bool sceneLookup);
	struct Impl;
	std::unique_ptr<Impl> _impl;
};

} // namespace mmodern

#endif
