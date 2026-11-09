#include "compat/scummvm/ScummVmXeenBridge.h"
#include "compat/scummvm/XeenOpl3.h"
#include "games/xeen/PromusDriver.h"
#include "games/xeen/XeenInstallationDetector.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cctype>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <vector>

namespace {
namespace fs = std::filesystem;
using namespace mmodern;
using Bytes = std::vector<std::uint8_t>;

struct End { std::uint64_t tick; bool loop; };

struct Score {
	struct Note { std::uint64_t start, end; int pitch; };
	std::array<std::vector<Note>, 7> voices;
	std::array<int, 7> low{}, high{};
	std::array<bool, 7> keyed{};
	void write(const PromusDriver::Write &w) {
		if (w.reg >= 0xa0 && w.reg <= 0xa6) low[w.reg - 0xa0] = w.value;
		if (w.reg < 0xb0 || w.reg > 0xb6) return;
		const auto v = w.reg - 0xb0;
		const bool on = (w.value & 32) != 0;
		if (keyed[v] && !on) voices[v].back().end = w.tick;
		high[v] = w.value;
		if (!keyed[v] && on) {
			const int fnum = low[v] | ((high[v] & 3) << 8), block = (high[v] >> 2) & 7;
			if (!fnum) throw std::runtime_error("Cannot transcribe zero-frequency note");
			const double hz = std::ldexp(double(fnum) * XeenOpl3::rate, block - 20);
			voices[v].push_back({w.tick, w.tick, int(std::lround(69 + 12 * std::log2(hz / 440)))});
		}
		keyed[v] = on;
	}
	void save(const fs::path &path, const std::string &title, std::uint64_t end) {
		std::ofstream out(path);
		out << "X:1\nT:" << title << "\n"
			<< "% Approximate OPL transcription: seven melodic channels; no timbres or slides.\n"
			<< "% Timing quantized to 1/256 notes at quarter=120 (7.8125 ms).\n"
			<< "M:none\nL:1/256\nQ:1/4=120\nK:C\n";
		const auto position = [](std::uint64_t tick) {
			return std::uint64_t(std::llround(double(tick) * PromusDriver::divisor / PromusDriver::pit * 128));
		};
		for (unsigned v = 0; v < voices.size(); ++v) {
			if (voices[v].empty()) continue;
			if (keyed[v]) voices[v].back().end = end;
			out << "V:" << v + 1 << " name=\"OPL " << v + 1 << "\"\n";
			std::uint64_t cursor = 0;
			unsigned tokens = 0;
			for (const auto &note : voices[v]) {
				const auto start = position(note.start), finish = position(note.end);
				if (start > cursor) out << 'z' << start - cursor << ' ';
				if (finish > start) {
					static const char *names[]{"=C","^C","=D","^D","=E","=F","^F","=G","^G","=A","^A","=B"};
					const int pitchClass = (note.pitch % 12 + 12) % 12;
					const int octave = (note.pitch - pitchClass) / 12;
					std::string name = names[pitchClass];
					if (octave >= 6) {
						name.back() = char(name.back() - 'A' + 'a');
						name.append(std::size_t(octave - 6), '\'');
					} else if (octave < 5) name.append(std::size_t(5 - octave), ',');
					out << name << finish - start << ' ';
				}
				cursor = finish;
				if (++tokens % 12 == 0) out << '\n';
			}
			if (position(end) > cursor) out << 'z' << position(end) - cursor;
			out << " |]\n";
		}
		out.close();
		if (!out) throw std::runtime_error("Cannot write ABC score");
	}
};

// Walk only sequence control flow to find its first top-level return/end.
// Audio and instrument interpretation remain owned by PromusDriver.
End firstEnd(const Bytes &song) {
	std::size_t pos = 0;
	std::vector<std::size_t> stack;
	std::uint64_t tick = 1;
	const auto byte = [&]() {
		if (pos >= song.size()) throw std::runtime_error("Truncated music sequence");
		return unsigned(song[pos++]);
	};
	const auto skip = [&](unsigned count) {
		if (count > song.size() - pos) throw std::runtime_error("Truncated music operands");
		pos += count;
	};
	for (unsigned budget = 0; budget < 1000000; ++budget) {
		const auto command = byte(), op = command >> 4, voice = command & 15;
		switch (op) {
		case 0: {
			const auto low = byte(), high = byte();
			if (stack.size() >= 16) throw std::runtime_error("Music call stack overflow");
			stack.push_back(pos); pos = low | (high << 8);
			if (pos >= song.size()) throw std::runtime_error("Invalid music call");
			break;
		}
		case 1: tick += std::max(1u, voice ? voice : byte()); break;
		case 2: skip(26); break;
		case 4: case 5: case 9: case 10: skip(2); break;
		case 6: case 8: case 12: skip(1); break;
		case 11: while (byte() != 0xf7) {} break;
		case 14: skip(3); break;
		case 15:
			if (voice != 15) return {tick, false};
			if (stack.empty()) return {tick, true};
			pos = stack.back(); stack.pop_back(); break;
		default: break;
		}
	}
	throw std::runtime_error("Music control flow exceeded analysis budget");
}

void little(std::ostream &out, std::uint32_t value, unsigned bytes) {
	for (unsigned i = 0; i < bytes; ++i) out.put(char(value >> (i * 8)));
}

void raw(const fs::path &path, const Bytes &bytes) {
	std::ofstream out(path, std::ios::binary);
	out.write(reinterpret_cast<const char *>(bytes.data()), std::streamsize(bytes.size()));
	out.close();
	if (!out) throw std::runtime_error("Cannot write sequence: " + path.u8string());
}

void render(const fs::path &path, const fs::path &abc, const std::string &title, PromusDriver::Bytes driverBytes,
		PromusDriver::Bytes song, End end) {
	const auto frames = PromusDriver::sampleBoundary(end.tick);
	if (frames > PromusDriver::rate * 60 * 20)
		throw std::runtime_error("Song exceeds 20-minute safety limit");
	std::ofstream out(path, std::ios::binary);
	out.write("RIFF", 4); little(out, std::uint32_t(36 + frames * 4), 4);
	out.write("WAVEfmt ", 8); little(out, 16, 4); little(out, 1, 2); little(out, 2, 2);
	little(out, XeenOpl3::rate, 4); little(out, XeenOpl3::rate * 4, 4);
	little(out, 4, 2); little(out, 16, 2); out.write("data", 4); little(out, std::uint32_t(frames * 4), 4);
	XeenOpl3 chip;
	Score score;
	PromusDriver driver(std::move(driverBytes), [&](const auto &write) {
		chip.write(write.reg, write.value); score.write(write);
	});
	driver.load(std::move(song));
	bool audible = false;
	for (std::uint64_t sample = 0; sample < frames; ++sample) {
		if (sample >= PromusDriver::sampleBoundary(driver.ticks() + 1)) driver.tick();
		std::int16_t stereo[2];
		chip.sample(stereo);
		for (auto value : stereo) { little(out, std::uint16_t(value), 2); audible |= value != 0; }
	}
	out.close();
	if (!out) throw std::runtime_error("Cannot write WAV: " + path.u8string());
	if (!audible) throw std::runtime_error("Rendered music is silent: " + path.u8string());
	score.save(abc, title, end.tick);
}

void extract(const fs::path &game, const fs::path &output) {
	const auto repository = fs::weakly_canonical(MMODERN_REPOSITORY_ROOT);
	const auto destination = fs::weakly_canonical(output);
	if (std::mismatch(repository.begin(), repository.end(), destination.begin(), destination.end()).first == repository.end())
		throw std::runtime_error("Original music must remain outside the repository");
	if (fs::exists(output)) throw std::runtime_error("Output already exists; choose a new directory");
	const auto installation = XeenInstallationDetector().detect(game);
	if (!installation) throw std::runtime_error("Game installation not found");
	ScummVmXeenBridge assets(*installation);
	auto driver = std::make_shared<const Bytes>(assets.readArchiveResource("promus", XeenSceneArchive::IntroOnly));
	fs::create_directories(output);
	std::ofstream manifest(output / "tracks.csv");
	manifest << "archive,resource,raw,wav,abc,ticks,duration_seconds,ending\n";
	const char *names[] = {
		"bank.m", "bigtheme.m", "cantheme.m", "castle.m", "cavern.m", "cavern1.m",
		"cavern2.m", "cavern3a.m", "cstl1rev.m", "cstl2rev.m", "cstl3rev.m",
		"daydesrt.m", "dngon1.m", "dngon2.m", "dngon3.m", "dungeon.m", "dungeon2.m",
		"endgame.m", "grounds.m", "guild.m", "inn.m", "mm4theme.m", "newbrigh.m",
		"nwblksmt.m", "outday1.m", "outday2.m", "outday3.m", "outday4.m", "outdoors.m",
		"outnght1.m", "outnght2.m", "outnght4.m", "sf05.m", "sf09.m", "sf17.m",
		"smith.m", "tavern.m", "temple.m", "town.m", "townday1.m", "twnnitea.m",
		"twnniteb.m", "twnwlk.m"
	};
	unsigned count = 0;
	for (unsigned side = 0; side < 3; ++side) {
		const auto archive = side == 0 ? XeenSceneArchive::Clouds :
			side == 1 ? XeenSceneArchive::DarksideOnly : XeenSceneArchive::IntroOnly;
		const fs::path folder = side == 0 ? "clouds" : side == 1 ? "darkside" : "intro";
		const auto data = archiveDataFile(*installation, side == 0 ? XeenArchiveRole::Clouds :
			side == 1 ? XeenArchiveRole::Darkside : XeenArchiveRole::Intro);
		auto stream = data.open();
		std::uint8_t header[2];
		if (stream->read(header, 2) != 2) throw std::runtime_error("Truncated CC header");
		Bytes index((unsigned(header[0]) | (unsigned(header[1]) << 8)) * 8);
		if (stream->read(index.data(), index.size()) != index.size())
			throw std::runtime_error("Truncated CC index");
		for (std::size_t i = 0; i < index.size(); ++i)
			index[i] = std::uint8_t(((index[i] << 2) | (index[i] >> 6)) + 0xac + i * 0x67);
		std::set<unsigned> ids;
		for (std::size_t i = 0; i < index.size(); i += 8)
			ids.insert(unsigned(index[i]) | (unsigned(index[i + 1]) << 8));
		for (const auto *name : names) {
			std::string upper = name;
			for (auto &c : upper) c = char(std::toupper(static_cast<unsigned char>(c)));
			unsigned id = static_cast<unsigned char>(upper.front());
			for (std::size_t i = 1; i < upper.size(); ++i)
				id = ((((id & 127) << 9) | ((id & 65408) >> 7)) + unsigned(upper[i])) & 65535;
			if (!ids.count(id)) continue;
			const auto song = std::make_shared<const Bytes>(assets.readArchiveResource(name, archive));
			const auto end = firstEnd(*song);
			const auto sequence = folder / "raw" / name;
			const auto wav = folder / "opl" / (std::string(name) + ".wav");
			const auto abc = folder / "abc" / (std::string(name) + ".abc");
			fs::create_directories((output / sequence).parent_path());
			fs::create_directories((output / wav).parent_path());
			fs::create_directories((output / abc).parent_path());
			raw(output / sequence, *song);
			render(output / wav, output / abc, folder.generic_u8string() + " / " + name, driver, song, end);
			manifest << folder.generic_u8string() << ',' << name << ',' << sequence.generic_u8string() << ','
				<< wav.generic_u8string() << ',' << abc.generic_u8string() << ',' << end.tick << ','
				<< double(PromusDriver::sampleBoundary(end.tick)) / PromusDriver::rate << ','
				<< (end.loop ? "first-loop" : "end") << '\n';
			std::cout << folder.generic_u8string() << '/' << name << ": "
				<< double(PromusDriver::sampleBoundary(end.tick)) / PromusDriver::rate << " seconds\n";
			++count;
		}
	}
	manifest.close();
	if (!manifest) throw std::runtime_error("Cannot write music manifest");
	std::cout << "Exported " << count << " sequences and single-pass OPL WAVs.\n";
}
}

int main(int argc, char **argv) {
	try {
		if (argc != 3) throw std::invalid_argument("Usage: mmodern_extract_music <game-directory> <new-output-directory>");
		extract(fs::u8path(argv[1]), fs::u8path(argv[2]));
		return 0;
	} catch (const std::exception &error) {
		std::cerr << "Music extraction failed: " << error.what() << '\n';
		return 1;
	}
}
