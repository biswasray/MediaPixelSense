// Media.h : Media classification and probing via ffprobe.

#pragma once

#include <string>

namespace pse {

enum class MediaKind { Image, Video, Audio, Unknown };

struct MediaInfo {
	MediaKind kind = MediaKind::Unknown;
	int width = 0;
	int height = 0;
	double duration = 0.0; // seconds; 0 for still images
	bool hasVideo = false;
	bool hasAudio = false;
};

const char* toString(MediaKind kind);

// Lower-case extension without the dot, e.g. "mp4".
std::string extensionOf(const std::string& path);

MediaKind kindFromExtension(const std::string& ext);

// Executables, overridable with PSE_FFMPEG / PSE_FFPROBE.
std::string ffmpegPath();
std::string ffprobePath();

// Throws std::runtime_error when the file cannot be probed.
MediaInfo probe(const std::string& path);

std::string toJson(const MediaInfo& info);

} // namespace pse
