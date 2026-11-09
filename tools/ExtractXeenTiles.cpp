#include "compat/scummvm/ScummVmXeenBridge.h"
#include "games/xeen/XeenInstallationDetector.h"
#include "games/xeen/XeenOutdoorSceneTables.h"

#include <zlib.h>
#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using Bytes = std::vector<std::uint8_t>;
using Palette = std::array<std::uint8_t, 768>;
namespace fs = std::filesystem;

void word(Bytes &bytes, std::uint32_t value) {
	for (int shift : {24, 16, 8, 0}) bytes.push_back(std::uint8_t(value >> shift));
}

void chunk(Bytes &png, const char *type, const Bytes &data) {
	word(png, std::uint32_t(data.size()));
	const auto start = png.size();
	png.insert(png.end(), type, type + 4);
	png.insert(png.end(), data.begin(), data.end());
	word(png, std::uint32_t(crc32(0, png.data() + start, uInt(data.size() + 4))));
}

void writePng(const fs::path &path, const mmodern::IndoorTexture &image, const Palette &palette) {
	Bytes rows;
	for (int y = 0; y < image.height; ++y) {
		rows.push_back(0);
		for (int x = 0; x < image.width; ++x) {
			const auto i = std::size_t(y) * image.width + x;
			const auto p = std::size_t(image.indices.at(i)) * 3;
			rows.insert(rows.end(), palette.begin() + p, palette.begin() + p + 3);
			rows.push_back(image.alpha.at(i));
		}
	}
	uLongf size = compressBound(uLong(rows.size()));
	Bytes compressed(size);
	if (compress2(compressed.data(), &size, rows.data(), uLong(rows.size()), Z_BEST_COMPRESSION) != Z_OK)
		throw std::runtime_error("PNG compression failed");
	compressed.resize(size);
	Bytes png{137, 80, 78, 71, 13, 10, 26, 10}, header;
	word(header, image.width); word(header, image.height);
	header.insert(header.end(), {8, 6, 0, 0, 0});
	chunk(png, "IHDR", header); chunk(png, "IDAT", compressed); chunk(png, "IEND", {});
	fs::create_directories(path.parent_path());
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(png.data()), std::streamsize(png.size()));
	out.close();
	if (!out) throw std::runtime_error("Cannot write PNG: " + path.u8string());
}

mmodern::IndoorTexture crop(const mmodern::IndoorTexture &source, int x, int y, int width, int height) {
	if (x < 0 || y < 0 || x + width > source.width || y + height > source.height)
		throw std::runtime_error("Tile crop exceeds original bounds");
	mmodern::IndoorTexture result;
	result.width = width; result.height = height;
	for (int row = y; row < y + height; ++row) {
		const auto start = std::size_t(row) * source.width + x;
		result.indices.insert(result.indices.end(), source.indices.begin() + start, source.indices.begin() + start + width);
		result.alpha.insert(result.alpha.end(), source.alpha.begin() + start, source.alpha.begin() + start + width);
	}
	return result;
}

mmodern::IndoorTexture mirror(mmodern::IndoorTexture image) {
	for (int y = 0; y < image.height; ++y) {
		const auto start = std::size_t(y) * image.width;
		std::reverse(image.indices.begin() + start, image.indices.begin() + start + image.width);
		std::reverse(image.alpha.begin() + start, image.alpha.begin() + start + image.width);
	}
	return image;
}

void blit(mmodern::IndoorTexture &dest, const mmodern::IndoorTexture &source, int x, int y) {
	for (int row = 0; row < source.height; ++row) for (int col = 0; col < source.width; ++col) {
		const int dx = x + source.originX + col, dy = y + source.originY + row;
		if (dx < 0 || dy < 0 || dx >= dest.width || dy >= dest.height)
			throw std::runtime_error("Background component exceeds original bounds");
		const auto s = std::size_t(row) * source.width + col;
		const auto d = std::size_t(dy) * dest.width + dx;
		if (source.alpha.at(s)) {
			dest.indices.at(d) = source.indices.at(s);
			dest.alpha.at(d) = source.alpha.at(s);
		}
	}
}

void extract(const fs::path &game, const fs::path &output) {
	const auto repository = fs::weakly_canonical(MMODERN_REPOSITORY_ROOT);
	const auto destination = fs::weakly_canonical(output);
	if (std::mismatch(repository.begin(), repository.end(), destination.begin(), destination.end()).first == repository.end())
		throw std::runtime_error("Original artwork must be exported outside the repository");
	const auto installation = mmodern::XeenInstallationDetector().detect(game);
	if (!installation) throw std::runtime_error("Game installation not found");
	if (fs::exists(output)) throw std::runtime_error("Output already exists; choose a new directory");
	fs::create_directories(output);
	mmodern::ScummVmXeenBridge assets(*installation);
	std::ofstream manifest(output / "tiles.csv");
	manifest << "file,resource,frame,depth,phase,origin_x,origin_y,width,height\n";
	unsigned count = 0, materials = 0;
	for (bool dark : {false, true}) {
		const auto archive = dark ? mmodern::XeenSceneArchive::DarksideOnly : mmodern::XeenSceneArchive::Clouds;
		const fs::path side = dark ? "darkside" : "clouds";
		assets.loadPalette("mm4.pal", archive);
		const auto palette = assets.snapshot().palette;
		const auto save = [&](const fs::path &relative, const std::string &resource, const std::string &frame,
				int depth, int phase, int originX, int originY, const mmodern::IndoorTexture &image) {
			writePng(output / relative, image, palette);
			manifest << relative.generic_u8string() << ',' << resource << ',' << frame << ',' << depth << ','
				<< phase << ',' << originX << ',' << originY << ',' << image.width << ',' << image.height << '\n';
			++count;
		};
		for (const char *theme : {"town", "cave", "towr", "cstl", "dung", "scfi"}) {
			for (bool ceiling : {false, true}) {
				const std::string resource = std::string(theme) + (ceiling ? ".sky" : ".gnd");
				if (!assets.hasSceneResource(resource, archive)) continue;
				const auto frames = assets.spriteFrameCount(resource, archive);
				if (frames != (ceiling ? 2u : 1u))
					throw std::runtime_error("Unexpected background frame count: " + resource);
				++materials;
				mmodern::IndoorTexture background;
				background.width = 216; background.height = ceiling ? 59 : 73;
				background.indices.resize(std::size_t(background.width) * background.height);
				background.alpha.resize(background.indices.size());
				for (std::size_t frame = 0; frame < frames; ++frame)
					blit(background, *assets.spriteTexture(resource, frame, archive), 0, frame == 1 ? 17 : 0);
				for (int depth : {0, 1}) {
					const int y = ceiling ? (depth == 0 ? 0 : 16) : (depth == 0 ? 42 : 26);
					const int height = depth == 0 && !ceiling ? 31 : 16;
					for (int phase : {0, 1}) {
						const auto image = crop(phase ? mirror(background) : background, 0, y, 216, height);
						const auto relative = side / (ceiling ? "ceilings" : "floors") / theme /
							("depth-" + std::to_string(depth) + "-phase-" + std::to_string(phase) + ".png");
						save(relative, resource, ceiling ? "0+1" : "0", depth, phase, 8, y + (ceiling ? 8 : 67), image);
					}
				}
			}
		}
		for (const char *name : mmodern::xeen_scene_tables::kSurfaceNames) {
			const std::string resource = name;
			if (!assets.hasSceneResource(resource, archive)) continue;
			if (assets.spriteFrameCount(resource, archive) != 25)
				throw std::runtime_error("Unexpected surface frame count: " + resource);
			++materials;
			const auto material = fs::path(resource).stem();
			// Ground rows: 0..2 are nearest, 3..5 are the next row.
			for (int frame = 0; frame < 6; ++frame) {
				const auto original = assets.spriteTexture(resource, frame, archive);
				for (int phase : {0, 1}) {
					const int source = phase ? (frame / 3 * 3 + 2 - frame % 3) : frame;
					const auto image = phase ? mirror(*assets.spriteTexture(resource, source, archive)) : *original;
					const auto relative = side / "surfaces" / material /
						("depth-" + std::to_string(frame / 3) + "-piece-" + std::to_string(frame % 3) +
							"-phase-" + std::to_string(phase) + ".png");
					save(relative, resource, std::to_string(source), frame / 3, phase,
						image.originX, image.originY, image);
				}
			}
		}
	}
	manifest.close();
	if (!manifest) throw std::runtime_error("Cannot write tile manifest");
	std::cout << "Exported " << count << " PNGs from " << materials << " materials to " << output << '\n';
}
} // namespace

int main(int argc, char **argv) {
	try {
		if (argc != 3) throw std::invalid_argument("Usage: mmodern_extract_tiles <game-directory> <new-output-directory>");
		extract(fs::u8path(argv[1]), fs::u8path(argv[2]));
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "Tile extraction failed: " << error.what() << '\n';
		return 1;
	}
}
