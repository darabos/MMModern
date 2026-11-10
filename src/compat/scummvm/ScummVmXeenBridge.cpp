#define FORBIDDEN_SYMBOL_ALLOW_ALL

#include "compat/scummvm/ScummVmXeenBridge.h"

#include "compat/scummvm/ScummVmRuntime.h"
#include "formats/xeen/XeenObjectSpriteSafety.h"
#include "formats/xeen/XeenRussianTranslation.h"

#include "common/file.h"
#include "common/path.h"
#include "common/memstream.h"
#include "common/stream.h"
#include "mm/shared/xeen/cc_archive.h"
#include "mm/shared/xeen/sprites.h"
#include "mm/shared/xeen/xsurface.h"

#include <algorithm>

#include <array>
#include <cstdint>
#include <memory>
#include <stdexcept>
#include <unordered_map>
#include <utility>
#include <random>
#include <fstream>

namespace mmodern {
namespace {


using MM::Shared::Xeen::SpriteResource;
using MM::Shared::Xeen::XSurface;

// Read-only initial state, reconstructed as SaveArchive::reset does. No saves,
// Party, or engine are instantiated. BaseCCArchive remains the real index reader.
class InitialCloudsArchive final : public MM::Shared::Xeen::BaseCCArchive {
public:
	explicit InitialCloudsArchive(std::vector<std::uint8_t> data) : _data(std::move(data)) {
		if (_data.size() < 2)
			throw std::runtime_error("Truncated Clouds initial container");
		const std::size_t count = _data[0] | (static_cast<unsigned>(_data[1]) << 8);
		const std::size_t headerSize = 2 + count * 8;
		if (!count || headerSize > _data.size())
			throw std::runtime_error("Invalid Clouds initial index");
		// Validate the reserved byte before the upstream reader's assertion.
		for (std::size_t i = 7; i < count * 8; i += 8) {
			const unsigned b = _data[2 + i];
			if ((((b << 2) | (b >> 6)) + 0xac + i * 0x67) % 256 != 0)
				throw std::runtime_error("Invalid reserved Clouds initial index byte");
		}
		Common::MemoryReadStream stream(_data.data(), static_cast<uint32>(_data.size()));
		loadIndex(stream);
		for (const auto &entry : _index) {
			const auto offset = static_cast<std::size_t>(entry._offset);
			if (offset < headerSize || offset > _data.size() || entry._size > _data.size() - offset)
				throw std::runtime_error("Resource outside Clouds initial container");
		}
	}

	Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override {
		MM::Shared::Xeen::CCEntry entry;
		if (!getHeaderEntry(path, entry))
			return nullptr;
		// The outer CCArchive already removed XOR 0x35. Inner payload is plaintext.
		return new Common::MemoryReadStream(_data.data() + entry._offset, entry._size);
	}

private:
	std::vector<std::uint8_t> _data;
};

std::vector<std::uint8_t> readBytes(Common::SeekableReadStream &stream,
		const std::string &name) {
	const auto length = stream.size();
	if (length < 0 || length > 65535)
		throw std::runtime_error("Invalid CC resource size: " + name);
	std::vector<std::uint8_t> bytes(static_cast<std::size_t>(length));
	if ((!bytes.empty() && stream.read(bytes.data(), static_cast<uint32>(bytes.size())) != bytes.size()) || stream.err())
		throw std::runtime_error("Incomplete resource read: " + name);
	return bytes;
}

class StreamSpriteResource final : public SpriteResource {
public:
	std::shared_ptr<const WorldTexture> texture(std::size_t frame) const {
		if (frame >= size()) throw std::runtime_error("Missing indoor texture frame");
		int left = 65535, top = 65535, right = 0, bottom = 0;
		const auto word = [&](std::size_t p) {
			if (p + 2 > _filesize) throw std::runtime_error("Truncated indoor sprite header");
			return int(_data[p]) | (int(_data[p + 1]) << 8);
		};
		for (const auto offset : {_index[frame]._offset1, _index[frame]._offset2}) {
			if (!offset) continue;
			const int x = word(offset), w = word(offset + 2);
			const int y = word(offset + 4), h = word(offset + 6);
			if (!w || !h) continue;
			left = std::min(left, x); top = std::min(top, y);
			right = std::max(right, x + w); bottom = std::max(bottom, y + h);
		}
		auto result = std::make_shared<WorldTexture>();
		if (right <= left || bottom <= top) return result;
		result->width = right - left; result->height = bottom - top;
		result->originX = left; result->originY = top;
		if (result->width > 1024 || result->height > 1024)
			throw std::runtime_error("Indoor sprite texture exceeds 1024 pixels");
		XSurface black(result->width, result->height), white(result->width, result->height);
		black.clear(0); white.clear(255);
		draw(black, int(frame), Common::Point(-left, -top), 0, 0);
		draw(white, int(frame), Common::Point(-left, -top), 0, 0);
		for (int y = 0; y < result->height; ++y) for (int x = 0; x < result->width; ++x) {
			const auto a = *static_cast<const byte *>(black.getBasePtr(x, y));
			const auto b = *static_cast<const byte *>(white.getBasePtr(x, y));
			result->indices.push_back(a);
			result->alpha.push_back(a == b ? 255 : 0);
		}
		return result;
	}
	bool loadFromStream(const Common::Path &filename, Common::SeekableReadStream &stream) {
		_filename = filename;
		SpriteResource::load(stream);
		return !empty() && !stream.err();
	}
	// EXE mode 8 top clipping (0x17e42..0x17e70, 0x18062..0x18091).
	// Its skip loop decrements height and increments Y BEFORE consuming a
	// row, leaving the last clipped row in the source stream. Keep the pinned
	// decoder for colors/RLE, then select the rows the original publishes.
	void drawProgramClipped(XSurface &dest,int frame,const Common::Point &position,uint flags) {
		const auto first=_index[frame]._offset1,second=_index[frame]._offset2;
		struct Restore {IndexEntry &entry;uint16 a,b;~Restore(){entry._offset1=a;entry._offset2=b;}} restore{_index[frame],first,second};
		const auto word=[&](unsigned p){if(p+2>_filesize)throw std::runtime_error("Truncated program sprite component");return unsigned(_data[p])|(unsigned(_data[p+1])<<8);};
		for(unsigned component=0;component<(second?2u:1u);++component) {
			const auto offset=component?second:first;
			_index[frame]._offset1=offset;_index[frame]._offset2=0;
			const auto headerY=word(offset+4),height=word(offset+6);
			const int top=position.y+int(headerY);
			if(top>=0){SpriteResource::draw(dest,frame,position,flags,0);continue;}
			const auto clipped=unsigned(-top);if(clipped>=height)continue;
			unsigned sourceRow=0,p=offset+8;
			for(unsigned row=1;row<clipped;++row) {
				if(p>=_filesize)throw std::runtime_error("Truncated program sprite clip row");
				const unsigned length=_data[p++];
				if(length){p+=length;++sourceRow;}
				else {if(p>=_filesize)throw std::runtime_error("Truncated program sprite clip skip");sourceRow+=unsigned(_data[p++])+1;}
				if(p>_filesize)throw std::runtime_error("Program sprite clip row exceeds resource");
			}
			if(sourceRow+height-clipped>height)throw std::runtime_error("Program sprite top clip exceeds component");
			XSurface black(dest.w,height),white(dest.w,height);
			for(unsigned y=0;y<height;++y){std::fill_n(static_cast<byte *>(black.getBasePtr(0,y)),dest.w,0);std::fill_n(static_cast<byte *>(white.getBasePtr(0,y)),dest.w,255);}
			const Common::Point origin(position.x,-int(headerY));
			SpriteResource::draw(black,frame,origin,flags,0);SpriteResource::draw(white,frame,origin,flags,0);
			for(unsigned y=0;y<height-clipped && y<unsigned(dest.h);++y)for(int x=0;x<dest.w;++x) {
				const auto pixel=*static_cast<const byte *>(black.getBasePtr(x,sourceRow+y));
				if(pixel==*static_cast<const byte *>(white.getBasePtr(x,sourceRow+y)))*static_cast<byte *>(dest.getBasePtr(x,y))=pixel;
			}
		}
	}
	// EXE normal drawer: a program component whose rows can never be drawn is
	// read and discarded (sc17.eg2 frame 16, hands.eg2 frames 1..15).
	bool invisibleProgramComponent(int frame) const {
		for(const auto offset:{_index[frame]._offset1,_index[frame]._offset2})
			if((offset || offset==_index[frame]._offset1) && xeenProgramComponentInvisible(_data,_filesize,offset))return true;
		return false;
	}
	void drawVisibleComponents(XSurface &dest,int frame,const Common::Point &position,uint flags,int scale) {
		const auto first=_index[frame]._offset1,second=_index[frame]._offset2;
		struct Restore {IndexEntry &entry;uint16 a,b;~Restore(){entry._offset1=a;entry._offset2=b;}} restore{_index[frame],first,second};
		for(const auto offset:{first,second}) {
			if((!offset && offset!=first) || xeenProgramComponentInvisible(_data,_filesize,offset))continue;
			_index[frame]._offset1=offset;_index[frame]._offset2=0;SpriteResource::draw(dest,frame,position,flags,scale);
		}
	}
	// Adapted from the pinned sprites.cpp drawPixel2/3/5 helpers (ScummVM
	// developers, GPL-3.0-or-later). Those upstream modes require g_engine or
	// g_system. Reuse the native decoder/scaler for each component and apply
	// the same pixel operations with the bridge palette and cosmetic sampler.
	void drawEngineFreeEffect(XSurface &dest,int frame,const Common::Point &position,
			uint flags,int scale,const std::array<std::uint8_t,768> &palette,std::uint32_t seed) {
		const unsigned mode=flags&0xf00, index=flags&0x1f;
		if(index>3 || (mode!=0x200&&mode!=0x300&&mode!=0x500))
			throw std::invalid_argument("Invalid engine-free monster effect");
		XSurface black(dest.w,dest.h),white(dest.w,dest.h);
		std::mt19937 random(seed);
		const auto original1=_index[frame]._offset1,original2=_index[frame]._offset2;
		struct Restore {IndexEntry &entry;uint16 first,second;~Restore(){entry._offset1=first;entry._offset2=second;}} restore{_index[frame],original1,original2};
		const bool hasPalette=std::any_of(palette.begin(),palette.end(),[](auto c){return c!=0;});
		const unsigned components=original2&&_index[frame]._override.empty()?2:1;
		for(unsigned component=0;component<components;++component) {
			_index[frame]._offset1=component?original2:original1;_index[frame]._offset2=0;
			for(int y=0;y<dest.h;++y) {
				std::fill_n(static_cast<byte *>(black.getBasePtr(0,y)),dest.w,0);
				std::fill_n(static_cast<byte *>(white.getBasePtr(0,y)),dest.w,255);
			}
			SpriteResource::draw(black,frame,position,flags&~0xfff,scale);
			SpriteResource::draw(white,frame,position,flags&~0xfff,scale);
			for(int y=0;y<dest.h;++y)for(int x=0;x<dest.w;++x) {
				const auto pixel=*static_cast<byte *>(black.getBasePtr(x,y));
				if(pixel!=*static_cast<byte *>(white.getBasePtr(x,y)))continue;
				auto *target=static_cast<byte *>(dest.getBasePtr(x,y));
				if(mode==0x300) {
					if(!hasPalette)continue;
					constexpr unsigned masks[]{1,3,7,15},offsets[]{1,2,4,8};
					const byte level=(pixel&masks[index])-offsets[index]+(*target&15);
					if(level>=0x80)*target&=0xf0;
					else if(level<=15)*target=(*target&0xf0)|level;
					else *target|=15;
					while(*target<255&&!palette[*target*3]&&!palette[*target*3+1]&&!palette[*target*3+2])++*target;
					continue;
				}
				uint16 r1=std::uniform_int_distribution<unsigned>(0,65535)(random),r2=std::uniform_int_distribution<unsigned>(0,65535)(random);
				bool carry=(r1&0x8000)!=0;
				r1=std::uint16_t((std::uint16_t(r1<<1))-r2-(carry?1:0));
				bool next=r2&1;r2=(r2>>1)|(carry?0x8000:0);carry=next;
				r2=(r2>>1)|(carry?0x8000:0);r2^=r1;
				if(mode==0x500) {
					constexpr unsigned thresholds[]{0x3333,0x6666,0x999a,0xcccd};
					if(r2>thresholds[index])*target=pixel;
				} else {
					constexpr int delta[]{-3,3,0,0,0,0,0,0,-5,5,0,0,0,0,0,0,-7,7,0,0,0,0,0,0,-9,9,0,0,0,0,0,0,
						-7,7,0,0,0,0,0,0,-9,9,0,0,0,0,0,0,-11,11,0,0,0,0,0,0,-13,13,0,0,0,0,0,0};
					constexpr unsigned masks1[]{3,0,3,0},masks2[]{0x7e,0x7e,0x7e,0x7e};
					const int shiftedX=x+delta[(r2&masks1[index]&masks2[index])/2];
					const int left=(flags&MM::Shared::Xeen::SPRFLAG_SCENE_CLIPPED)?8:0;
					const int right=(flags&MM::Shared::Xeen::SPRFLAG_SCENE_CLIPPED)?223:dest.w;
					if(shiftedX<left||shiftedX>=right)continue;
					const int shiftedY=y+delta[((r2>>8)&masks1[index]&masks2[index])/2];
					const int bottom=(flags&MM::Shared::Xeen::SPRFLAG_BOTTOM_CLIPPED)?140:dest.h;
					if(shiftedY>=0&&shiftedY<bottom)*static_cast<byte *>(dest.getBasePtr(shiftedX,shiftedY))=pixel;
				}
			}
		}
	}
};

// All installed archive roles use the pinned BaseCCArchive name/index decoder.
// Validate its preconditions first, so malformed resources never reach error().
// M110: this is the one substitution point of the language layer; every
// installed-archive read, New Game block and metadata read passes through it.
class CheckedInstalledArchive : public MM::Shared::Xeen::BaseCCArchive {
public:
 CheckedInstalledArchive(ReadOnlyDataFile file,XeenArchiveRole role,std::shared_ptr<const XeenRussianTranslation> translation):
  _file(std::move(file)),_role(role),_translation(std::move(translation)) {
  auto source=_file.open();_indexBytes.resize(2);
  if(source->read(_indexBytes.data(),2)!=2)throw std::runtime_error("Truncated CC count: "+_file.identity());
  const unsigned count=_indexBytes[0]|unsigned(_indexBytes[1])<<8;
  const std::size_t size=2+count*8;
  if(!count||size>source->size())throw std::runtime_error("Invalid CC index extent: "+_file.identity());
  _indexBytes.resize(size);
  if(source->read(_indexBytes.data()+2,size-2)!=size-2)throw std::runtime_error("Truncated CC index: "+_file.identity());
  for(std::size_t i=7;i<count*8;i+=8){const unsigned b=_indexBytes[2+i];
   if((((b<<2)|(b>>6))+0xac+i*0x67)%256!=0)throw std::runtime_error("Invalid CC reserved index byte: "+_file.identity());}
  Common::MemoryReadStream stream(_indexBytes.data(),static_cast<uint32>(_indexBytes.size()));loadIndex(stream);
  for(std::size_t i=0;i<_index.size();++i) {
   const auto &entry=_index[i];
   // Requested extents are checked below, so an unrelated truncated optional
   // member cannot prevent reading another structurally valid resource.
   for(std::size_t j=0;j<i;++j)if(_index[j]._id==entry._id)throw std::runtime_error("Ambiguous CC member ID: "+_file.identity());
  }
 }
 std::string identity() const{return _file.identity();}
 Common::SeekableReadStream *createReadStreamForMember(const Common::Path &path) const override {
  MM::Shared::Xeen::CCEntry entry;if(!getHeaderEntry(path,entry))return nullptr;
  auto source=_file.open();std::vector<std::uint8_t> index(_indexBytes.size());
  if(source->read(index.data(),index.size())!=index.size()||index!=_indexBytes)throw std::runtime_error("Admitted CC index changed: "+_file.identity());
  if(entry._offset<static_cast<int64>(_indexBytes.size()))throw std::runtime_error("CC member overlaps archive index: "+_file.identity());
  if(static_cast<std::uint64_t>(entry._offset)>source->size() || entry._size>source->size()-entry._offset)throw std::runtime_error("CC member payload is truncated: "+_file.identity());
  if(!source->seek(entry._offset))throw std::runtime_error("Cannot seek CC member: "+_file.identity());
  std::vector<std::uint8_t> bytes(entry._size);
  if(source->read(bytes.data(),bytes.size())!=bytes.size())throw std::runtime_error("Short CC member read: "+_file.identity());
  for(auto &byte:bytes)byte^=0x35;
  if(_translation) {
   auto cached=_substituted.find(entry._id);
   if(cached==_substituted.end())cached=_substituted.emplace(entry._id,_translation->substitute(_role,entry._id,bytes)).first;
   if(cached->second)bytes=*cached->second;
  }
  if(bytes.empty())return new Common::MemoryReadStream(nullptr,0);
  Common::MemoryReadStream input(bytes.data(),static_cast<uint32>(bytes.size()));return input.readStream(bytes.size());
 }
 std::optional<std::vector<std::uint8_t>> readMetadataChecked(const char *name,std::size_t limit) const {
  const Common::Path member(name,Common::Path::kNoSeparator);MM::Shared::Xeen::CCEntry entry;
  if(!getHeaderEntry(member,entry))return std::nullopt;
  // The limit checks the bytes the layer substitutes, not the CD index size.
  std::unique_ptr<Common::SeekableReadStream> stream(createReadStreamForMember(member));
  if(stream->size()<0 || static_cast<std::uint64_t>(stream->size())>limit)throw std::runtime_error(std::string("Oversized DARK.CC/")+name);
  return readBytes(*stream,name);
 }
private:
 ReadOnlyDataFile _file;std::vector<std::uint8_t> _indexBytes;
 XeenArchiveRole _role;std::shared_ptr<const XeenRussianTranslation> _translation;
 // The language is fixed for the archive's lifetime, so merged entries are kept.
 mutable std::unordered_map<std::uint16_t,std::optional<std::vector<std::uint8_t>>> _substituted;
};
class DarkMetadataArchive final:public CheckedInstalledArchive {
public:
 DarkMetadataArchive(ReadOnlyDataFile file,std::shared_ptr<const XeenRussianTranslation> translation):
  CheckedInstalledArchive(std::move(file),XeenArchiveRole::Darkside,std::move(translation)){}
 std::optional<std::vector<std::uint8_t>> readItemMaterialNamesChecked() const{return readMetadataChecked("mae.xen",8192);}
 std::optional<std::vector<std::uint8_t>> readLearnedSpellNamesChecked() const{return readMetadataChecked("spells.xen",77*64);}
 std::optional<std::vector<std::uint8_t>> readMonsterStatisticsChecked() const{return readMetadataChecked("xeen.mon",65535);}
};
using SceneInstalledArchive=CheckedInstalledArchive;

std::unique_ptr<Common::SeekableReadStream> openResource(
		Common::Archive &archive, const std::string &resourceName, const std::string &origin = "xeen.cc") {
	std::unique_ptr<Common::SeekableReadStream> stream(
		archive.createReadStreamForMember(Common::Path(resourceName.c_str(), Common::Path::kNoSeparator)));
	if (!stream)
		throw std::runtime_error("Missing resource in " + origin + ": " + resourceName);
	return stream;
}

} // namespace

struct ScummVmXeenBridge::Impl {
	ScummVmRuntime runtime;
	std::shared_ptr<const XeenRussianTranslation> translation;
	CheckedInstalledArchive archive;
	bool darkAvailable = false;
	std::unique_ptr<DarkMetadataArchive> darkMetadataArchive;
	std::optional<ReadOnlyDataFile> sceneIntroPath, sceneDarkPath;
	std::unique_ptr<SceneInstalledArchive> sceneIntro, sceneDark;
 std::filesystem::path programFolder;
	XSurface surface;
	std::array<std::uint8_t, IndexedFrame::kPaletteSize> palette{};
	struct CachedSprite {
		std::vector<std::uint8_t> bytes;
		std::unique_ptr<StreamSpriteResource> decoded;
	};
	std::unordered_map<std::string, CachedSprite> sprites;
	std::unordered_map<std::string, std::shared_ptr<const WorldTexture>> indoorTextures;
	std::vector<std::uint8_t> interfaceCoverage;
	std::size_t spriteLoads = 0;
	std::unordered_map<std::string,std::vector<std::uint8_t>> admittedSprites;
	bool spriteIntegrityFailed=false;
	std::unique_ptr<InitialCloudsArchive> initialArchive;
	std::unique_ptr<InitialCloudsArchive> initialDarkArchive;

	explicit Impl(const GameInstallation &installation) :
		runtime(installation),translation(installation.translation),
		archive(archiveDataFile(installation,XeenArchiveRole::Clouds),XeenArchiveRole::Clouds,translation) {
		darkAvailable = installation.hasDarkside();
		sceneIntroPath = installation.introData;
  if(installation.uiModule)programFolder=installation.uiModule->physicalPath.parent_path();
		if(installation.hasDarkside()) sceneDarkPath=archiveDataFile(installation,XeenArchiveRole::Darkside);
	}

	Impl(const GameInstallation &installation, int width, int height) : Impl(installation) {
		if (width <= 0 || height <= 0)
			throw std::runtime_error("Invalid framebuffer dimensions");
		surface.create(width, height);
		surface.clear(0);
	}

	InitialCloudsArchive &initial() {
		if (!initialArchive) {
			// engines/mm/xeen/files.cpp: SaveArchive::reset, original order.
			const char *const blocks[] = {"2a0c", "2a1c", "2a2c", "2a3c", "284c", "2a5c"};
			std::vector<std::uint8_t> data;
			for (const char *name : blocks) {
				const Common::Path path(name, Common::Path::kNoSeparator);
				if (!archive.hasFile(path))
					continue;
				auto stream = openResource(archive, name);
				const auto block = readBytes(*stream, name);
				data.insert(data.end(), block.begin(), block.end());
			}
			initialArchive.reset(new InitialCloudsArchive(std::move(data)));
		}
		return *initialArchive;
	}

	InitialCloudsArchive &initialDark() {
		if (!sceneDarkPath) throw std::runtime_error("Missing DARK New Game archive");
		if (!initialDarkArchive) {
			if (!sceneDark) sceneDark.reset(new SceneInstalledArchive(*sceneDarkPath,XeenArchiveRole::Darkside,translation));
			// engines/mm/xeen/files.cpp:167, SaveArchive::reset, same as Clouds.
			const char *const blocks[]{"2a0c", "2a1c", "2a2c", "2a3c", "284c", "2a5c"};
			std::vector<std::uint8_t> data;
			for (const auto *name : blocks) {
				if (!sceneDark->hasFile(Common::Path(name, Common::Path::kNoSeparator))) continue;
				auto stream = openResource(*sceneDark, name, "dark.cc");
				const auto block = readBytes(*stream, name);
				data.insert(data.end(), block.begin(), block.end());
			}
			initialDarkArchive.reset(new InitialCloudsArchive(std::move(data)));
		}
		return *initialDarkArchive;
	}

	struct SceneResource {
		Common::Archive *archive;
		std::string origin;
	};
 std::optional<std::vector<std::uint8_t>> programLoose(const std::string &name) {
  if(programFolder.empty())return {};
  if(std::filesystem::path(name).filename()!=std::filesystem::path(name))throw std::invalid_argument("Invalid program resource name");
  const auto lower=[](std::string s){for(auto &c:s)if(c>='A'&&c<='Z')c+=32;return s;};
  std::filesystem::path found;
  for(const auto &entry:std::filesystem::directory_iterator(programFolder))if(lower(entry.path().filename().u8string())==lower(name)) {
   if(!found.empty() || !entry.is_regular_file())throw std::runtime_error("Ambiguous loose program resource: "+name);found=entry.path();
  }
  if(found.empty())return {};
  auto in=ReadOnlyDataFile::plain(found).open();if(in->size()>1024*1024)throw std::runtime_error("Oversized loose program resource: "+name);
  std::vector<std::uint8_t> b(in->size());if(in->read(b.data(),b.size())!=b.size())throw std::runtime_error("Incomplete loose program resource: "+name);return b;
 }
	SceneResource sceneResource(const std::string &name, XeenSceneArchive selection = XeenSceneArchive::Current) {
		const Common::Path path(name.c_str(), Common::Path::kNoSeparator);
  if(xeenProgramArchive(selection)) {
   if(selection==XeenSceneArchive::DarksideProgram || selection==XeenSceneArchive::DarksideProgramDark) {
    const bool dark=selection==XeenSceneArchive::DarksideProgramDark;
    auto &pathToArchive=dark?sceneDarkPath:sceneIntroPath;
    auto &selected=dark?sceneDark:sceneIntro;
    if(pathToArchive && !selected)selected.reset(new SceneInstalledArchive(*pathToArchive,dark?XeenArchiveRole::Darkside:XeenArchiveRole::Intro,translation));
    if(selected && selected->hasFile(path))return {selected.get(),dark?"dark.cc":"intro.cc"};
    if(programLoose(name))return {&archive,dark?"dark.cc":"intro.cc"};
    return {nullptr,{}};
   }
   if(archive.hasFile(path) || programLoose(name))return {&archive,"xeen.cc"};
   return {nullptr,{}}; // EXE never falls back to INTRO.
  }
		// Pinned File::open: selected/current archive, then registered search
		// sources (INTRO). DARK is an archive selection, never a search source.
		if (selection == XeenSceneArchive::Darkside || selection == XeenSceneArchive::DarksideOnly) {
			if (sceneDarkPath.has_value()) {
				if (!sceneDark) sceneDark.reset(new SceneInstalledArchive(*sceneDarkPath,XeenArchiveRole::Darkside,translation));
				if (sceneDark->hasFile(path)) return {sceneDark.get(), "dark.cc"};
			}
		} else if (selection != XeenSceneArchive::IntroOnly && archive.hasFile(path)) return {&archive, "xeen.cc"};
		if (selection == XeenSceneArchive::DarksideOnly) return {nullptr, {}};
		if (sceneIntroPath.has_value()) {
			if (!sceneIntro) sceneIntro.reset(new SceneInstalledArchive(*sceneIntroPath,XeenArchiveRole::Intro,translation));
			if (sceneIntro->hasFile(path)) return {sceneIntro.get(), "intro.cc"};
		}
		return {nullptr, {}};
	}

	StreamSpriteResource &sprite(const std::string &resourceName,
			std::optional<std::size_t> checkedFrame = std::nullopt, unsigned monsterFrames = 0,
			bool sceneLookup = false, XeenSceneArchive selection = XeenSceneArchive::Current) {
		if(spriteIntegrityFailed) throw std::runtime_error("Sprite resource integrity previously failed");
		const auto source = sceneResource(resourceName,selection);
		if (!source.archive) throw std::runtime_error("Missing scene resource in installed CC archives: " + resourceName);
		// Clouds retains its existing identity; other archives have separate
		// cache/admission identities even when resource names collide.
		const auto key = static_cast<CheckedInstalledArchive *>(source.archive)->identity()+"|"+source.origin+"|"+std::to_string(MM::Shared::Xeen::BaseCCArchive::convertNameToId(Common::Path(resourceName.c_str(),Common::Path::kNoSeparator)))+
   (xeenProgramArchive(selection)?"|program:"+programFolder.u8string():"");
		const auto validate = [&](const std::vector<std::uint8_t> &bytes) {
			const auto known=admittedSprites.find(key);
			if(known!=admittedSprites.end() && known->second!=bytes) {
				spriteIntegrityFailed=true;throw std::runtime_error("Admitted sprite resource changed: "+resourceName);
			}
			if (monsterFrames) {
				if (bytes.size() < 2 || bytes[0] != monsterFrames || bytes[1] != 0)
					throw std::runtime_error("Unexpected monster sprite frame count");
				for (std::size_t i = 0; i < monsterFrames; ++i) validateXeenObjectSprite(bytes, i);
			} else if (checkedFrame) validateXeenObjectSprite(bytes, *checkedFrame, xeenProgramArchive(selection));
		};
		const auto existing = sprites.find(key);
		if (existing != sprites.end()) {
			validate(existing->second.bytes);
			return *existing->second.decoded;
		}

  auto loose=xeenProgramArchive(selection)?programLoose(resourceName):std::nullopt;
  std::vector<std::uint8_t> bytes;
  if(loose)bytes=std::move(*loose);else {auto stream=openResource(*source.archive,resourceName,source.origin);bytes=readBytes(*stream,resourceName);}
		validate(bytes);
		Common::MemoryReadStream input(bytes.data(), static_cast<uint32>(bytes.size()));
		std::unique_ptr<StreamSpriteResource> resource(new StreamSpriteResource());
		const Common::Path path(resourceName.c_str(), Common::Path::kNoSeparator);
		if (!resource->loadFromStream(path, input))
			throw std::runtime_error("Unable to decode sprite: " + resourceName);

		admittedSprites.emplace(key,bytes);
		StreamSpriteResource &result = *resource;
		sprites.emplace(key, CachedSprite{std::move(bytes), std::move(resource)});
		++spriteLoads;
		return result;
	}
};

ScummVmXeenBridge::ScummVmXeenBridge(const GameInstallation &installation) :
	_impl(new Impl(installation)) {
}

ScummVmXeenBridge::ScummVmXeenBridge(const GameInstallation &installation,
		int width, int height) : _impl(new Impl(installation, width, height)) {
}

ScummVmXeenBridge::~ScummVmXeenBridge() = default;

void ScummVmXeenBridge::discardSpriteCache() {
	_impl->sprites.clear();
	_impl->indoorTextures.clear();
}

std::shared_ptr<const WorldTexture> ScummVmXeenBridge::spriteTexture(
		const std::string &name, std::size_t frame, XeenSceneArchive archive) {
	const auto key = std::to_string(int(archive)) + ":" + name + ":" + std::to_string(frame);
	const auto found = _impl->indoorTextures.find(key);
	if (found != _impl->indoorTextures.end()) return found->second;
	auto texture = _impl->sprite(name, std::nullopt, 0, true, archive).texture(frame);
	_impl->indoorTextures.emplace(key, texture);
	return texture;
}

void ScummVmXeenBridge::beginInterfaceCoverage() {
	_impl->interfaceCoverage.assign(std::size_t(_impl->surface.w) * _impl->surface.h, 0);
}
std::vector<std::uint8_t> ScummVmXeenBridge::endInterfaceCoverage() {
	return std::exchange(_impl->interfaceCoverage, {});
}
std::size_t ScummVmXeenBridge::cachedSpriteCount() const { return _impl->sprites.size(); }
std::size_t ScummVmXeenBridge::spriteLoadCount() const { return _impl->spriteLoads; }

std::size_t ScummVmXeenBridge::spriteFrameCount(const std::string &name, XeenSceneArchive selection) {
	const auto count=_impl->sprite(name,std::nullopt,0,true,selection).size();
	if (!count) throw std::runtime_error("Empty sprite: "+name);
	for (std::size_t frame=0;frame<count;++frame) _impl->sprite(name,frame,0,true,selection);
	return count;
}

bool ScummVmXeenBridge::hasSceneResource(const std::string &name, XeenSceneArchive selection) {
	return _impl->sceneResource(name,selection).archive != nullptr;
}
IndexedFrame ScummVmXeenBridge::restDreamImage(XeenSceneArchive archive) {
 const auto source=_impl->sceneResource("scene1.raw",archive);
 if(!source.archive)throw std::runtime_error("Original Rest dream image is absent");
 const auto stream=openResource(*source.archive,"scene1.raw",source.origin);
 auto bytes=readBytes(*stream,"scene1.raw");
 if(bytes.size()!=64000)throw std::runtime_error("Invalid original Rest dream image size");
 IndexedFrame frame{320,200,std::move(bytes)};frame.palette=_impl->palette;return frame;
}

void ScummVmXeenBridge::drawSceneSprite(const std::string &name, std::size_t frame,
		int x, int y, const XeenSpriteDrawOptions &options) {
	drawSpriteImpl(name, frame, x, y, options, true);
}

void ScummVmXeenBridge::validateProjectile(const std::string &name,XeenSceneArchive archive) { _impl->sprite(name,std::nullopt,3,true,archive); }

void ScummVmXeenBridge::validateNormalMonster(const std::string &resourceName,XeenSceneArchive archive) {
	_impl->sprite(resourceName, std::nullopt, 8,true,archive);
}
void ScummVmXeenBridge::validateAttackMonster(const std::string &resourceName,XeenSceneArchive archive) {
	_impl->sprite(resourceName, std::nullopt, 4,true,archive);
}

std::optional<std::vector<std::uint8_t>> ScummVmXeenBridge::readObjectVisualMetadata(XeenSide side) {
	if (!_impl->darkAvailable) return std::nullopt;
	if (!_impl->darkMetadataArchive)
		_impl->darkMetadataArchive.reset(new DarkMetadataArchive(*_impl->sceneDarkPath,_impl->translation));
	const Common::Path path(side==XeenSide::Clouds?"clouds.dat":"dark.dat", Common::Path::kNoSeparator);
	std::unique_ptr<Common::SeekableReadStream> stream(_impl->darkMetadataArchive->createReadStreamForMember(path));
	if (!stream) return std::nullopt;
	return readBytes(*stream, "DARK.CC/clouds.dat");
}

std::optional<std::vector<std::uint8_t>> ScummVmXeenBridge::readItemMaterialNamesFromDarkArchive() {
	if (!_impl->darkAvailable)
		return std::nullopt;
	if (!_impl->darkMetadataArchive)
		_impl->darkMetadataArchive.reset(new DarkMetadataArchive(*_impl->sceneDarkPath,_impl->translation));
	return _impl->darkMetadataArchive->readItemMaterialNamesChecked();
}

std::optional<std::vector<std::uint8_t>> ScummVmXeenBridge::readLearnedSpellNamesFromDarkArchive() {
	if (!_impl->darkAvailable) return std::nullopt;
	if (!_impl->darkMetadataArchive)
		_impl->darkMetadataArchive.reset(new DarkMetadataArchive(*_impl->sceneDarkPath,_impl->translation));
	return _impl->darkMetadataArchive->readLearnedSpellNamesChecked();
}

std::optional<std::vector<std::uint8_t>> ScummVmXeenBridge::readMonsterStatistics(XeenSide side) {
	if (!_impl->darkAvailable) return std::nullopt;
	if (!_impl->darkMetadataArchive)
		_impl->darkMetadataArchive.reset(new DarkMetadataArchive(*_impl->sceneDarkPath,_impl->translation));
	if(side==XeenSide::Clouds)return _impl->darkMetadataArchive->readMonsterStatisticsChecked();
 const auto source=_impl->sceneResource("dark.mon",XeenSceneArchive::DarksideOnly);
 if(!source.archive)return std::nullopt;auto stream=openResource(*source.archive,"dark.mon",source.origin);return readBytes(*stream,"DARK.CC/dark.mon");
}

void ScummVmXeenBridge::drawObjectSprite(const std::string &resourceName,
		std::size_t frame, int x, int y, const XeenSpriteDrawOptions &options) {
	// M16 uses the native framebuffer and normal drawer. In particular, the
	// upstream enlarge path writes a second pixel/row without edge checks.
	if (_impl->surface.w != 320 || _impl->surface.h != 200 || options.enlarge ||
		x < -320 || x > 320 || y < -200 || y > 200 ||
		options.scaleIndex < 0 || options.scaleIndex > 15)
		throw std::runtime_error("M16 sprite: unsupported framebuffer, anchor, or scale/enlargement");
	try {
		_impl->sprite(resourceName,frame,0,true,options.archive);
	} catch (const std::runtime_error &error) {
		throw std::runtime_error(resourceName + ": " + error.what());
	}
	drawSprite(resourceName, frame, x, y, options);
}

bool ScummVmXeenBridge::hasArchiveResource(const std::string &resourceName) {return _impl->sceneResource(resourceName).archive!=nullptr;}

std::vector<std::uint8_t> ScummVmXeenBridge::readArchiveResource(const std::string &resourceName, XeenSceneArchive selection) {
 if(xeenProgramArchive(selection))if(auto loose=_impl->programLoose(resourceName))return std::move(*loose);
	const auto source=(selection==XeenSceneArchive::Clouds) ? Impl::SceneResource{&_impl->archive,"xeen.cc"} : _impl->sceneResource(resourceName,selection);
	if(!source.archive)throw std::runtime_error("Missing resource in selected archive: "+resourceName);
	auto stream = openResource(*source.archive, resourceName, source.origin);
	return readBytes(*stream, resourceName);
}

bool ScummVmXeenBridge::hasInitialResource(const std::string &resourceName, XeenSide side) {
	return (side==XeenSide::Clouds?_impl->initial():_impl->initialDark()).hasFile(
		Common::Path(resourceName.c_str(), Common::Path::kNoSeparator));
}

std::vector<std::uint8_t> ScummVmXeenBridge::readInitialResource(const std::string &resourceName, XeenSide side) {
	std::unique_ptr<Common::SeekableReadStream> stream((side==XeenSide::Clouds?_impl->initial():_impl->initialDark()).createReadStreamForMember(
		Common::Path(resourceName.c_str(), Common::Path::kNoSeparator)));
	if (!stream)
		throw std::runtime_error("Missing New Game resource: " + resourceName);
	return readBytes(*stream, resourceName);
}

std::vector<std::uint8_t> ScummVmXeenBridge::readInitialDarkResource(const std::string &resourceName) {
	std::unique_ptr<Common::SeekableReadStream> stream(_impl->initialDark().createReadStreamForMember(
		Common::Path(resourceName.c_str(), Common::Path::kNoSeparator)));
	if (!stream) throw std::runtime_error("Missing DARK New Game member: " + resourceName);
	return readBytes(*stream, resourceName);
}

void ScummVmXeenBridge::loadPalette(const std::string &resourceName, XeenSceneArchive selection) {
	const auto source=(selection==XeenSceneArchive::Clouds) ? Impl::SceneResource{&_impl->archive,"xeen.cc"} : _impl->sceneResource(resourceName,selection);
	if(!source.archive)throw std::runtime_error("Missing palette in selected archive: "+resourceName);
	std::unique_ptr<Common::SeekableReadStream> stream = openResource(*source.archive, resourceName, source.origin);
	if (stream->size() != static_cast<int64>(IndexedFrame::kPaletteSize))
		throw std::runtime_error("Invalid palette size: " + resourceName);

	if (stream->read(_impl->palette.data(), static_cast<uint32>(_impl->palette.size())) !=
			_impl->palette.size())
		throw std::runtime_error("Incomplete palette read: " + resourceName);

	for (std::uint8_t &component : _impl->palette)
		component = static_cast<std::uint8_t>(component << 2);
}

void ScummVmXeenBridge::loadRawFramebuffer(const std::string &resourceName, XeenSceneArchive selection) {
	const auto source=(selection==XeenSceneArchive::Clouds) ? Impl::SceneResource{&_impl->archive,"xeen.cc"} : _impl->sceneResource(resourceName,selection);
	if(!source.archive)throw std::runtime_error("Missing RAW in selected archive: "+resourceName);
	std::unique_ptr<Common::SeekableReadStream> stream = openResource(*source.archive, resourceName, source.origin);
	const uint32 expectedSize = static_cast<uint32>(_impl->surface.w * _impl->surface.h);
	if (stream->size() != expectedSize)
		throw std::runtime_error("Invalid RAW framebuffer size: " + resourceName);

	for (int y = 0; y < _impl->surface.h; ++y) {
		if (stream->read(_impl->surface.getBasePtr(0, y), _impl->surface.w) !=
				static_cast<uint32>(_impl->surface.w))
			throw std::runtime_error("Incomplete framebuffer read: " + resourceName);
	}
}

void ScummVmXeenBridge::drawSprite(const std::string &resourceName,
		std::size_t frame, int x, int y) {
	drawSprite(resourceName, frame, x, y, XeenSpriteDrawOptions{});
}

void ScummVmXeenBridge::drawSprite(const std::string &resourceName,
		std::size_t frame, int x, int y, const XeenSpriteDrawOptions &options) {
	drawSpriteImpl(resourceName, frame, x, y, options, false);
}

void ScummVmXeenBridge::drawSpriteImpl(const std::string &resourceName,
		std::size_t frame, int x, int y, const XeenSpriteDrawOptions &options, bool sceneLookup) {
	StreamSpriteResource &sprite = _impl->sprite(resourceName, std::nullopt, 0, sceneLookup, options.archive);
	if (frame >= sprite.size())
		throw std::runtime_error("Missing sprite frame in " + resourceName);

	if (options.scaleIndex < 0 || options.scaleIndex > 15)
		throw std::runtime_error("Invalid sprite scale index");
	const unsigned effect=options.monsterEffectFlags, drawer=effect>>8, index=effect&255;
	if (drawer>6 || (drawer==0 && index!=0) ||
		(drawer==1 && index>23) || (drawer>=2 && drawer<=5 && index>3) || (drawer==6 && index>15))
		throw std::runtime_error("Invalid monster effect flags");
	uint flags = options.monsterEffectFlags;
	if (options.horizontalFlip)
		flags |= MM::Shared::Xeen::SPRFLAG_HORIZ_FLIPPED;
	if (options.sceneClipped)
		flags |= MM::Shared::Xeen::SPRFLAG_SCENE_CLIPPED;
	if (options.bottomClipped) {
		SpriteResource::setClippedBottom(140);
		flags |= MM::Shared::Xeen::SPRFLAG_BOTTOM_CLIPPED;
	}
	const int scale = options.enlarge ? MM::Shared::Xeen::SCALE_ENLARGE : options.scaleIndex;
	if (!_impl->interfaceCoverage.empty()) {
		XSurface black(_impl->surface.w, _impl->surface.h), white(_impl->surface.w, _impl->surface.h);
		black.clear(0); white.clear(255);
		sprite.draw(black, int(frame), Common::Point(x, y), flags & ~0xfff, scale);
		sprite.draw(white, int(frame), Common::Point(x, y), flags & ~0xfff, scale);
		for (int row = 0; row < black.h; ++row) for (int col = 0; col < black.w; ++col)
			if (*static_cast<const byte *>(black.getBasePtr(col, row)) ==
					*static_cast<const byte *>(white.getBasePtr(col, row)))
				_impl->interfaceCoverage[std::size_t(row) * black.w + col] = 1;
	}
	const auto mode=flags&0xf00;
	if(mode==0x200 || mode==0x300 || mode==0x500) {
		if(options.enlarge || _impl->surface.w!=320 || _impl->surface.h!=200)
			throw std::invalid_argument("Monster effect requires the native scene surface");
		sprite.drawEngineFreeEffect(_impl->surface,static_cast<int>(frame),Common::Point(x,y),flags,scale,_impl->palette,options.monsterEffectSeed);
	} else sprite.draw(_impl->surface, static_cast<int>(frame), Common::Point(x, y), flags, scale);
}

IndexedFrame ScummVmXeenBridge::snapshot() const {
	IndexedFrame frame;
	frame.width = _impl->surface.w;
	frame.height = _impl->surface.h;
	frame.palette = _impl->palette;
	frame.pixels.resize(static_cast<std::size_t>(frame.width) * frame.height);

	for (int y = 0; y < frame.height; ++y) {
		const std::uint8_t *source = static_cast<const std::uint8_t *>(
			_impl->surface.getBasePtr(0, y));
		std::copy(source, source + frame.width,
			frame.pixels.begin() + static_cast<std::size_t>(y) * frame.width);
	}

	return frame;
}
void ScummVmXeenBridge::blackenScene() {
 if(_impl->surface.w<224 || _impl->surface.h<140)throw std::invalid_argument("Scene surface is too small");
 for(int y=8;y<140;++y) {
  auto *row=static_cast<std::uint8_t *>(_impl->surface.getBasePtr(8,y));
  std::fill(row,row+216,0);
 }
}

void ScummVmXeenBridge::drawTraining(IndexedFrame &frame, XeenSceneArchive archive) {
	if(!frame.isValid() || frame.width!=320 || frame.height!=200)throw std::invalid_argument("Invalid Training draw context");
	struct Resource {const char *name;unsigned frames;};
	constexpr Resource resources[]={{"trng1.twn",8},{"train.icn",4},{"esc.icn",2}};
	for(const auto &r:resources) {
		_impl->sprite(r.name,0,r.frames,true,archive);
	}
	XSurface surface;surface.create(320,200);
	for(int y=0;y<200;++y)std::copy_n(frame.pixels.data()+y*320,320,static_cast<std::uint8_t *>(surface.getBasePtr(0,y)));
	_impl->sprite("trng1.twn",0,8,true,archive).draw(surface,0,Common::Point(8,8));
	_impl->sprite("train.icn",0,4,true,archive).draw(surface,0,Common::Point(8,140));
	_impl->sprite("esc.icn",0,2,true,archive).draw(surface,0,Common::Point(86,140));
	for(int y=0;y<200;++y)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(0,y)),320,frame.pixels.data()+y*320);
}
void ScummVmXeenBridge::drawSmith(IndexedFrame &frame, XeenSceneArchive archive) {
	if (!frame.isValid() || frame.width!=320 || frame.height!=200)
		throw std::invalid_argument("Invalid Ironworks draw context");
	auto &sprite=_impl->sprite("blck1.twn",0,8,true,archive);
	XSurface surface;surface.create(320,200);
	for(int y=0;y<200;++y)std::copy_n(frame.pixels.data()+y*320,320,
		static_cast<std::uint8_t *>(surface.getBasePtr(0,y)));
	sprite.draw(surface,0,Common::Point(8,8));
	for(int y=8;y<140;++y)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(8,y)),216,
		frame.pixels.data()+y*320+8);
}
void ScummVmXeenBridge::drawTavern(IndexedFrame &frame, XeenSceneArchive archive) {
 if(!frame.isValid() || frame.width!=320 || frame.height!=200)
  throw std::invalid_argument("Invalid Tavern draw context");
 // BaseLocation's first background frame; animation and audio are deferred.
 auto &sprite=_impl->sprite("tvrn1.twn",0,8,true,archive);
 XSurface surface;surface.create(320,200);
 for(int y=0;y<200;++y)std::copy_n(frame.pixels.data()+y*320,320,
  static_cast<std::uint8_t *>(surface.getBasePtr(0,y)));
 sprite.draw(surface,0,Common::Point(8,8));
 for(int y=8;y<140;++y)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(8,y)),216,
  frame.pixels.data()+y*320+8);
}
void ScummVmXeenBridge::drawBank(IndexedFrame &frame, XeenSceneArchive archive) {
 if(!frame.isValid() || frame.width!=320 || frame.height!=200)
  throw std::invalid_argument("Invalid Bank draw context");
 // BaseLocation's first background frame; animation and audio are deferred.
 auto &sprite=_impl->sprite("bnkr1.twn",0,8,true,archive);
 XSurface surface;surface.create(320,200);
 for(int y=0;y<200;++y)std::copy_n(frame.pixels.data()+y*320,320,
  static_cast<std::uint8_t *>(surface.getBasePtr(0,y)));
 sprite.draw(surface,0,Common::Point(8,8));
 for(int y=8;y<140;++y)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(8,y)),216,
  frame.pixels.data()+y*320+8);
}
void ScummVmXeenBridge::drawGuild(IndexedFrame &frame, XeenSceneArchive archive) {
 if(!frame.isValid() || frame.width!=320 || frame.height!=200)
  throw std::invalid_argument("Invalid Guild draw context");
 // BaseLocation's first background frame; animation and audio are deferred.
 auto &sprite=_impl->sprite("gild1.twn",0,8,true,archive);
 XSurface surface;surface.create(320,200);
 for(int y=0;y<200;++y)std::copy_n(frame.pixels.data()+y*320,320,
  static_cast<std::uint8_t *>(surface.getBasePtr(0,y)));
 sprite.draw(surface,0,Common::Point(8,8));
 for(int y=8;y<140;++y)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(8,y)),216,
  frame.pixels.data()+y*320+8);
}
void ScummVmXeenBridge::drawTemple(IndexedFrame &frame, XeenSceneArchive archive) {
	if (!frame.isValid() || frame.width!=320 || frame.height!=200)
		throw std::invalid_argument("Invalid Temple draw context");
	XSurface surface;surface.create(320,200);
	for(int y=0;y<200;++y)std::copy_n(frame.pixels.data()+y*320,320,
		static_cast<std::uint8_t *>(surface.getBasePtr(0,y)));
	_impl->sprite("tmpl1.twn",0,8,true,archive).draw(surface,0,Common::Point(8,8));
	_impl->sprite("esc.icn",0,2,true,archive).draw(surface,0,Common::Point(86,140));
	for(int y=8;y<160;++y)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(8,y)),216,
		frame.pixels.data()+y*320+8);
}

void ScummVmXeenBridge::drawNpc(IndexedFrame &frame, std::uint8_t portraitId,
		std::size_t portraitFrame,XeenSceneArchive archive) {
	if (!frame.isValid() || frame.width != 320 || frame.height != 200 || portraitFrame >= 4)
		throw std::invalid_argument("invalid NPC draw context");
	const std::string face = "face" + (portraitId < 10 ? std::string("0") : std::string()) +
		std::to_string(portraitId) + ".fac";
	// Validate every usable frame before any pixels are applied. Shared cells and
	// optional second cells remain decoded/drawn by the pinned SpriteResource.
	for (std::size_t i = 0; i < 4; ++i) _impl->sprite(face, i,0,true,archive);
	auto &border = _impl->sprite("frame.fac",0,0,true,archive);
	auto &portrait = _impl->sprite(face,portraitFrame,0,true,archive);
	XSurface surface;
	surface.create(320, 200);
	for (int y = 0; y < 200; ++y)
		std::copy_n(frame.pixels.data() + y * 320, 320,
			static_cast<std::uint8_t *>(surface.getBasePtr(0, y)));
	border.draw(surface, 0, Common::Point(16, 16));
	portrait.draw(surface, static_cast<int>(portraitFrame), Common::Point(23, 22));
	for (int y = 0; y < 200; ++y)
		std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(0, y)), 320,
			frame.pixels.data() + y * 320);
}

void ScummVmXeenBridge::drawDialogSprite(IndexedFrame &frame,const char *name,unsigned index,int x,int y,XeenSceneArchive selection,int scale,unsigned flags) {
	if(!frame.isValid() || frame.width!=320 || frame.height!=200) throw std::invalid_argument("Invalid dialog sprite frame");
    if(scale<0 || scale>15)throw std::invalid_argument("Invalid scene sprite scale");
 if(flags!=0 && flags!=0x800)throw std::invalid_argument("Invalid presentation sprite flags");
	auto &sprite=_impl->sprite(name,index,0,selection!=XeenSceneArchive::Current,selection);
	XSurface surface;surface.create(320,200);
	for(int row=0;row<200;++row) std::copy_n(frame.pixels.data()+row*320,320,static_cast<std::uint8_t *>(surface.getBasePtr(0,row)));
	if(xeenProgramArchive(selection) && flags==0x800 && scale==0 && y<0)
		sprite.drawProgramClipped(surface,index,Common::Point(x,y),flags);
	else if(xeenProgramArchive(selection) && sprite.invisibleProgramComponent(index))
		sprite.drawVisibleComponents(surface,index,Common::Point(x,y),flags,scale);
	else sprite.draw(surface,index,Common::Point(x,y),flags,scale);
	for(int row=0;row<200;++row) std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(0,row)),320,frame.pixels.data()+row*320);
}

IndexedFrame ScummVmXeenBridge::cursorImage() {
 return cursorImage(XeenSceneArchive::Current);
}
IndexedFrame ScummVmXeenBridge::cursorImage(XeenSceneArchive selection) {
 // EventsManager::setCursor(0): resize, transparent palette index 0, hotspot 0,0.
 auto &sprite=_impl->sprite("mouse.icn",0,0,selection!=XeenSceneArchive::Current,selection);
 XSurface surface;sprite.draw(surface,0,Common::Point(0,0),MM::Shared::Xeen::SPRFLAG_RESIZE);
 auto frame=snapshot();frame.width=surface.w;frame.height=surface.h;
 frame.pixels.resize(std::size_t(surface.w)*surface.h);
 for(int row=0;row<surface.h;++row)std::copy_n(static_cast<const std::uint8_t *>(surface.getBasePtr(0,row)),surface.w,frame.pixels.data()+row*surface.w);
 return frame;
}

} // namespace mmodern
