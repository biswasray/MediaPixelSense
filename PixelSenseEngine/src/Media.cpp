// Media.cpp : Media classification and probing via ffprobe.

#include "Media.h"

#include "Json.h"
#include "Process.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pse {

const char* toString(MediaKind kind)
{
	switch (kind) {
	case MediaKind::Image: return "image";
	case MediaKind::Video: return "video";
	case MediaKind::Audio: return "audio";
	default: return "unknown";
	}
}

std::string extensionOf(const std::string& path)
{
	auto dot = path.find_last_of('.');
	auto sep = path.find_last_of("/\\");
	if (dot == std::string::npos || (sep != std::string::npos && dot < sep)) return {};
	std::string ext = path.substr(dot + 1);
	std::transform(ext.begin(), ext.end(), ext.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return ext;
}

MediaKind kindFromExtension(const std::string& ext)
{
	static const std::set<std::string> images{ "png", "jpg", "jpeg", "webp", "bmp", "tif", "tiff" };
	static const std::set<std::string> videos{ "mp4", "mov", "mkv", "webm", "avi", "m4v", "gif" };
	static const std::set<std::string> audio{ "mp3", "wav", "aac", "m4a", "ogg", "flac" };
	if (images.count(ext)) return MediaKind::Image;
	if (videos.count(ext)) return MediaKind::Video;
	if (audio.count(ext)) return MediaKind::Audio;
	return MediaKind::Unknown;
}

namespace {

std::string envOr(const char* name, const char* fallback)
{
	const char* v = std::getenv(name);
	return (v && *v) ? v : fallback;
}

} // namespace

std::string ffmpegPath() { return envOr("PSE_FFMPEG", "ffmpeg"); }
std::string ffprobePath() { return envOr("PSE_FFPROBE", "ffprobe"); }

MediaInfo probe(const std::string& path)
{
	MediaInfo info;
	info.kind = kindFromExtension(extensionOf(path));

	// key=value output, one line per field; streams are separated by their codec_type line.
	auto result = runProcess({ ffprobePath(), "-v", "error",
		"-show_entries", "stream=codec_type,width,height:format=duration",
		"-of", "default=noprint_wrappers=1", path });
	if (result.exitCode != 0) {
		throw std::runtime_error("ffprobe failed: " + result.output);
	}

	std::istringstream lines(result.output);
	std::string line;
	bool inVideoStream = false;
	while (std::getline(lines, line)) {
		if (!line.empty() && line.back() == '\r') line.pop_back();
		auto eq = line.find('=');
		if (eq == std::string::npos) continue;
		std::string key = line.substr(0, eq);
		std::string value = line.substr(eq + 1);

		if (key == "codec_type") {
			inVideoStream = value == "video";
			if (inVideoStream) info.hasVideo = true;
			if (value == "audio") info.hasAudio = true;
		} else if (inVideoStream && key == "width" && info.width == 0) {
			info.width = std::atoi(value.c_str());
		} else if (inVideoStream && key == "height" && info.height == 0) {
			info.height = std::atoi(value.c_str());
		} else if (key == "duration" && value != "N/A") {
			info.duration = std::atof(value.c_str());
		}
	}

	if (info.kind == MediaKind::Unknown) {
		info.kind = info.hasVideo ? MediaKind::Video : (info.hasAudio ? MediaKind::Audio : MediaKind::Unknown);
	}
	if (info.kind == MediaKind::Image) {
		info.duration = 0.0;
		info.hasAudio = false;
	}
	return info;
}

std::string toJson(const MediaInfo& info)
{
	return json::Object()
		.str("kind", toString(info.kind))
		.num("width", info.width)
		.num("height", info.height)
		.num("duration", info.duration)
		.boolean("hasAudio", info.hasAudio)
		.build();
}

} // namespace pse
