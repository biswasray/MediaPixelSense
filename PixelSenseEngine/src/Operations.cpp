// Operations.cpp : Operation definitions and their FFmpeg translations.

#include "Operations.h"

#include "Json.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>

namespace pse {

// ---------------------------------------------------------------------------
// BuildContext

double BuildContext::number(const std::string& name) const
{
	auto it = params.find(name);
	if (it == params.end()) throw std::invalid_argument("missing parameter '" + name + "'");
	return std::strtod(it->second.c_str(), nullptr);
}

const std::string& BuildContext::text(const std::string& name) const
{
	auto it = params.find(name);
	if (it == params.end()) throw std::invalid_argument("missing parameter '" + name + "'");
	return it->second;
}

namespace {

// ---------------------------------------------------------------------------
// Helpers

std::string fmt(double v)
{
	std::ostringstream ss;
	ss << std::setprecision(6) << v;
	return ss.str();
}

std::string lower(std::string s)
{
	std::transform(s.begin(), s.end(), s.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
	return s;
}

bool isImageExt(const std::string& ext) { return kindFromExtension(ext) == MediaKind::Image; }
bool isAudioExt(const std::string& ext) { return kindFromExtension(ext) == MediaKind::Audio; }
bool isH264Container(const std::string& ext) { return ext == "mp4" || ext == "mov" || ext == "mkv" || ext == "m4v"; }

// Escapes a value for use inside a filter option, then for the filtergraph
// itself (FFmpeg applies two levels of unescaping).
std::string escapeFilterValue(const std::string& value)
{
	auto escapeChars = [](const std::string& s, const std::string& special) {
		std::string out;
		for (char c : s) {
			if (special.find(c) != std::string::npos) out += '\\';
			out += c;
		}
		return out;
	};
	return escapeChars(escapeChars(value, "\\':"), "\\'[],;");
}

std::string defaultFontFile()
{
	if (const char* env = std::getenv("PSE_FONT"); env && *env) return env;
#ifdef _WIN32
	std::string windir = std::getenv("WINDIR") ? std::getenv("WINDIR") : "C:/Windows";
	std::replace(windir.begin(), windir.end(), '\\', '/');
	return windir + "/Fonts/arial.ttf";
#elif defined(__APPLE__)
	return "/System/Library/Fonts/Supplemental/Arial.ttf";
#else
	return "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
#endif
}

std::string atempoChain(double factor)
{
	std::vector<std::string> parts;
	while (factor > 2.0) {
		parts.push_back("atempo=2.0");
		factor /= 2.0;
	}
	while (factor < 0.5) {
		parts.push_back("atempo=0.5");
		factor /= 0.5;
	}
	parts.push_back("atempo=" + fmt(factor));
	std::string out;
	for (size_t i = 0; i < parts.size(); ++i) out += (i ? "," : "") + parts[i];
	return out;
}

struct Job {
	std::string vf;
	std::string af;
	std::vector<std::string> beforeInput;
	std::vector<std::string> afterInput;
	bool dropAudio = false;
};

// Re-encodes ctx.input into ctx.output, applying the given filters and
// picking browser-friendly codecs for the output container.
std::vector<std::string> transcode(const BuildContext& ctx, Job job)
{
	std::vector<std::string> args = job.beforeInput;
	args.insert(args.end(), { "-i", ctx.input });
	args.insert(args.end(), job.afterInput.begin(), job.afterInput.end());

	const std::string& ext = ctx.outputExt;
	std::string vf = job.vf;
	auto appendVf = [&vf](const std::string& f) { vf += (vf.empty() ? "" : ",") + f; };

	if (isImageExt(ext)) {
		if (!vf.empty()) args.insert(args.end(), { "-vf", vf });
		args.insert(args.end(), { "-frames:v", "1", "-update", "1" });
		if (ext == "jpg" || ext == "jpeg") args.insert(args.end(), { "-q:v", "2" });
		if (ext == "webp") args.insert(args.end(), { "-quality", "90" });
		args.push_back(ctx.output);
		return args;
	}

	if (isAudioExt(ext)) {
		args.push_back("-vn");
		if (!job.af.empty()) args.insert(args.end(), { "-af", job.af });
		if (ext == "mp3") args.insert(args.end(), { "-c:a", "libmp3lame", "-q:a", "2" });
		args.push_back(ctx.output);
		return args;
	}

	if (ext == "gif") {
		appendVf("split[a][b];[a]palettegen[p];[b][p]paletteuse");
		args.insert(args.end(), { "-vf", vf, "-loop", "0", "-an", ctx.output });
		return args;
	}

	if (isH264Container(ext)) {
		appendVf("scale=trunc(iw/2)*2:trunc(ih/2)*2"); // x264 + yuv420p need even dimensions
		args.insert(args.end(), { "-vf", vf, "-c:v", "libx264", "-preset", "veryfast", "-crf", "20", "-pix_fmt", "yuv420p" });
		if (ext != "mkv") args.insert(args.end(), { "-movflags", "+faststart" });
	} else if (ext == "webm") {
		if (!vf.empty()) args.insert(args.end(), { "-vf", vf });
		args.insert(args.end(), { "-c:v", "libvpx-vp9", "-crf", "32", "-b:v", "0", "-deadline", "good", "-cpu-used", "4", "-row-mt", "1" });
	} else if (!vf.empty()) {
		args.insert(args.end(), { "-vf", vf });
	}

	if (job.dropAudio || !ctx.info.hasAudio) {
		args.push_back("-an");
	} else {
		if (!job.af.empty()) args.insert(args.end(), { "-af", job.af });
		if (ext == "webm") args.insert(args.end(), { "-c:a", "libopus", "-b:a", "128k" });
		else args.insert(args.end(), { "-c:a", "aac", "-b:a", "160k" });
	}

	args.push_back(ctx.output);
	return args;
}

std::vector<std::string> filterOnly(const BuildContext& ctx, const std::string& vf)
{
	return transcode(ctx, Job{ vf });
}

// ---------------------------------------------------------------------------
// Parameter spec shorthands

ParamSpec num(std::string name, std::string desc, std::optional<double> def, double min, double max, bool required = false)
{
	ParamSpec p;
	p.name = std::move(name);
	p.type = ParamType::Number;
	p.description = std::move(desc);
	p.required = required;
	if (def) p.defaultValue = fmt(*def);
	p.min = min;
	p.max = max;
	return p;
}

ParamSpec integer(std::string name, std::string desc, std::optional<double> def, double min, double max, bool required = false)
{
	ParamSpec p = num(std::move(name), std::move(desc), def, min, max, required);
	p.type = ParamType::Integer;
	return p;
}

ParamSpec choice(std::string name, std::string desc, std::vector<std::string> choices, std::optional<std::string> def)
{
	ParamSpec p;
	p.name = std::move(name);
	p.type = ParamType::Enum;
	p.description = std::move(desc);
	p.choices = std::move(choices);
	p.defaultValue = std::move(def);
	p.required = !p.defaultValue;
	return p;
}

ParamSpec text(std::string name, std::string desc, std::optional<std::string> def)
{
	ParamSpec p;
	p.name = std::move(name);
	p.type = ParamType::String;
	p.description = std::move(desc);
	p.defaultValue = std::move(def);
	p.required = !p.defaultValue;
	return p;
}

// ---------------------------------------------------------------------------
// Operation builders that need more than a single filter

std::vector<std::string> buildResize(const BuildContext& ctx)
{
	// -2 keeps aspect ratio and an even size, which video encoders require.
	std::string keep = ctx.info.kind == MediaKind::Video ? "-2" : "-1";
	if (ctx.has("percent")) {
		std::string p = fmt(ctx.number("percent") / 100.0);
		return filterOnly(ctx, "scale=trunc(iw*" + p + "/2)*2:" + keep);
	}
	if (!ctx.has("width") && !ctx.has("height")) {
		throw std::invalid_argument("resize needs width, height or percent");
	}
	std::string w = ctx.has("width") ? ctx.text("width") : keep;
	std::string h = ctx.has("height") ? ctx.text("height") : keep;
	return filterOnly(ctx, "scale=" + w + ":" + h + ":flags=lanczos");
}

std::vector<std::string> buildCrop(const BuildContext& ctx)
{
	int iw = ctx.info.width, ih = ctx.info.height;
	int w = static_cast<int>(ctx.number("width"));
	int h = static_cast<int>(ctx.number("height"));
	if (iw > 0) w = std::min(w, iw);
	if (ih > 0) h = std::min(h, ih);
	int x = static_cast<int>(ctx.number("x"));
	int y = static_cast<int>(ctx.number("y"));
	std::string xs = x < 0 ? "(iw-ow)/2" : std::to_string(iw > 0 ? std::min(x, iw - w) : x);
	std::string ys = y < 0 ? "(ih-oh)/2" : std::to_string(ih > 0 ? std::min(y, ih - h) : y);
	return filterOnly(ctx, "crop=" + std::to_string(w) + ":" + std::to_string(h) + ":" + xs + ":" + ys);
}

std::vector<std::string> buildRotate(const BuildContext& ctx)
{
	double deg = std::fmod(ctx.number("degrees"), 360.0);
	if (deg < 0) deg += 360.0;
	if (std::abs(deg - 90) < 1e-6) return filterOnly(ctx, "transpose=clock");
	if (std::abs(deg - 180) < 1e-6) return filterOnly(ctx, "hflip,vflip");
	if (std::abs(deg - 270) < 1e-6) return filterOnly(ctx, "transpose=cclock");
	if (deg < 1e-6) return filterOnly(ctx, "null");
	std::string rad = fmt(deg * 3.14159265358979 / 180.0);
	return filterOnly(ctx, "rotate=" + rad + ":ow=rotw(" + rad + "):oh=roth(" + rad + "):c=black");
}

std::vector<std::string> buildText(const BuildContext& ctx)
{
	const std::string& pos = ctx.text("position");
	std::string x = "(w-text_w)/2", y = "(h-text_h)/2";
	const std::string margin = "h*0.05";
	if (pos.find("top") != std::string::npos) y = margin;
	if (pos.find("bottom") != std::string::npos) y = "h-text_h-" + margin;
	if (pos.find("left") != std::string::npos) x = margin;
	if (pos.find("right") != std::string::npos) x = "w-text_w-" + margin;

	std::string filter = "drawtext=fontfile=" + escapeFilterValue(defaultFontFile()) +
		":expansion=none:text=" + escapeFilterValue(ctx.text("text")) +
		":fontsize=" + ctx.text("size") +
		":fontcolor=" + escapeFilterValue(ctx.text("color")) +
		":x=" + x + ":y=" + y;
	if (ctx.text("box") == "yes") filter += ":box=1:boxcolor=black@0.5:boxborderw=12";
	return filterOnly(ctx, filter);
}

std::vector<std::string> buildConvert(const BuildContext& ctx)
{
	bool fromImage = ctx.info.kind == MediaKind::Image;
	if (fromImage && !isImageExt(ctx.outputExt)) {
		throw std::invalid_argument("an image can only be converted to another image format");
	}
	if (!fromImage && isImageExt(ctx.outputExt)) {
		throw std::invalid_argument("use extract_frame to turn a video into an image");
	}
	if (isAudioExt(ctx.outputExt) && !ctx.info.hasAudio) {
		throw std::invalid_argument("this video has no audio track");
	}
	return transcode(ctx, Job{});
}

std::vector<std::string> buildTrim(const BuildContext& ctx)
{
	double start = ctx.number("start");
	Job job;
	job.afterInput = { "-ss", fmt(start) };
	if (ctx.has("end")) {
		double end = ctx.number("end");
		if (end <= start) throw std::invalid_argument("trim end must be after start");
		job.afterInput.insert(job.afterInput.end(), { "-to", fmt(end) });
	} else if (ctx.has("duration")) {
		job.afterInput.insert(job.afterInput.end(), { "-t", fmt(ctx.number("duration")) });
	}
	if (ctx.info.duration > 0 && start >= ctx.info.duration) {
		throw std::invalid_argument("trim start is past the end of the video (" + fmt(ctx.info.duration) + "s)");
	}
	return transcode(ctx, job);
}

std::vector<std::string> buildSpeed(const BuildContext& ctx)
{
	double f = ctx.number("factor");
	Job job;
	job.vf = "setpts=PTS/" + fmt(f);
	job.af = atempoChain(f);
	return transcode(ctx, job);
}

std::vector<std::string> buildReverse(const BuildContext& ctx)
{
	if (ctx.info.duration > 60) {
		throw std::invalid_argument("reverse is limited to clips of 60 seconds or less; trim first");
	}
	Job job;
	job.vf = "reverse";
	job.af = "areverse";
	return transcode(ctx, job);
}

std::vector<std::string> buildMute(const BuildContext& ctx)
{
	return { "-i", ctx.input, "-c:v", "copy", "-an", ctx.output };
}

std::vector<std::string> buildVolume(const BuildContext& ctx)
{
	if (!ctx.info.hasAudio) throw std::invalid_argument("this video has no audio track");
	Job job;
	job.af = "volume=" + fmt(ctx.number("factor"));
	return transcode(ctx, job);
}

std::vector<std::string> buildFade(const BuildContext& ctx)
{
	double in = ctx.number("in"), out = ctx.number("out");
	double dur = ctx.info.duration;
	std::vector<std::string> v, a;
	if (in > 0) {
		v.push_back("fade=t=in:st=0:d=" + fmt(in));
		a.push_back("afade=t=in:st=0:d=" + fmt(in));
	}
	if (out > 0 && dur > 0) {
		std::string st = fmt(std::max(0.0, dur - out));
		v.push_back("fade=t=out:st=" + st + ":d=" + fmt(out));
		a.push_back("afade=t=out:st=" + st + ":d=" + fmt(out));
	}
	if (v.empty()) throw std::invalid_argument("fade needs a positive 'in' or 'out' duration");
	auto join = [](const std::vector<std::string>& parts) {
		std::string s;
		for (size_t i = 0; i < parts.size(); ++i) s += (i ? "," : "") + parts[i];
		return s;
	};
	return transcode(ctx, Job{ join(v), join(a) });
}

std::vector<std::string> buildExtractFrame(const BuildContext& ctx)
{
	double t = ctx.number("time");
	if (ctx.info.duration > 0) t = std::min(t, std::max(0.0, ctx.info.duration - 0.05));
	Job job;
	job.beforeInput = { "-ss", fmt(t) };
	return transcode(ctx, job);
}

std::vector<std::string> buildExtractAudio(const BuildContext& ctx)
{
	if (!ctx.info.hasAudio) throw std::invalid_argument("this video has no audio track");
	return transcode(ctx, Job{});
}

std::vector<std::string> buildGif(const BuildContext& ctx)
{
	Job job;
	if (ctx.has("start")) job.beforeInput = { "-ss", ctx.text("start") };
	if (ctx.has("duration")) job.afterInput = { "-t", ctx.text("duration") };
	job.vf = "fps=" + ctx.text("fps") + ",scale=" + ctx.text("width") + ":-1:flags=lanczos";
	return transcode(ctx, job);
}

std::vector<Operation> makeOperations()
{
	auto simple = [](std::string name, std::string desc, std::vector<ParamSpec> params,
		std::function<std::string(const BuildContext&)> filter) {
		Operation op;
		op.name = std::move(name);
		op.description = std::move(desc);
		op.params = std::move(params);
		op.build = [filter](const BuildContext& ctx) { return filterOnly(ctx, filter(ctx)); };
		return op;
	};
	auto custom = [](std::string name, std::string desc, Target target, OutputKind output,
		std::vector<ParamSpec> params, std::function<std::vector<std::string>(const BuildContext&)> build) {
		return Operation{ std::move(name), std::move(desc), target, output, std::move(params), std::move(build) };
	};

	const std::vector<std::string> positions{ "center", "top", "bottom", "top-left", "top-right", "bottom-left", "bottom-right" };

	return {
		// --- Image and video ---
		custom("resize", "Resize. Give width and/or height in pixels (the other keeps aspect ratio), or percent.",
			Target::Visual, OutputKind::Same,
			{ integer("width", "Target width in pixels", std::nullopt, 16, 8192),
			  integer("height", "Target height in pixels", std::nullopt, 16, 8192),
			  num("percent", "Scale as a percentage of the current size, e.g. 50 for half", std::nullopt, 1, 1000) },
			buildResize),
		custom("crop", "Crop a rectangle. Omit x/y to crop from the center.",
			Target::Visual, OutputKind::Same,
			{ integer("width", "Crop width in pixels", std::nullopt, 1, 8192, true),
			  integer("height", "Crop height in pixels", std::nullopt, 1, 8192, true),
			  integer("x", "Left edge in pixels (-1 = centered)", -1, -1, 8192),
			  integer("y", "Top edge in pixels (-1 = centered)", -1, -1, 8192) },
			buildCrop),
		custom("rotate", "Rotate clockwise by degrees (negative = counter-clockwise).",
			Target::Visual, OutputKind::Same,
			{ num("degrees", "Clockwise rotation in degrees", 90, -360, 360) },
			buildRotate),
		simple("flip", "Mirror the picture.",
			{ choice("direction", "Mirror axis", { "horizontal", "vertical" }, "horizontal") },
			[](const BuildContext& c) { return c.text("direction") == "vertical" ? "vflip" : "hflip"; }),
		simple("brightness", "Change brightness. Positive is brighter, negative is darker.",
			{ num("amount", "Brightness offset from -1 to 1 (0.1 is a small boost)", 0.1, -1, 1) },
			[](const BuildContext& c) { return "eq=brightness=" + c.text("amount"); }),
		simple("contrast", "Change contrast. 1 is unchanged, above 1 is more contrast.",
			{ num("amount", "Contrast multiplier", 1.2, 0, 3) },
			[](const BuildContext& c) { return "eq=contrast=" + c.text("amount"); }),
		simple("saturation", "Change color saturation. 1 is unchanged, 0 is gray, above 1 is more vivid.",
			{ num("amount", "Saturation multiplier", 1.4, 0, 3) },
			[](const BuildContext& c) { return "eq=saturation=" + c.text("amount"); }),
		simple("hue", "Shift all colors around the color wheel.",
			{ num("degrees", "Hue shift in degrees", 90, -180, 180) },
			[](const BuildContext& c) { return "hue=h=" + c.text("degrees"); }),
		simple("grayscale", "Convert to black and white.", {},
			[](const BuildContext&) { return std::string("hue=s=0"); }),
		simple("sepia", "Apply a warm vintage sepia tone.", {},
			[](const BuildContext&) { return std::string("colorchannelmixer=.393:.769:.189:0:.349:.686:.168:0:.272:.534:.131"); }),
		simple("invert", "Invert colors (negative image).", {},
			[](const BuildContext&) { return std::string("negate"); }),
		simple("blur", "Blur the picture.",
			{ num("radius", "Blur strength", 5, 0.5, 50) },
			[](const BuildContext& c) { return "gblur=sigma=" + c.text("radius"); }),
		simple("sharpen", "Sharpen details.",
			{ num("amount", "Sharpening strength", 1, 0.1, 5) },
			[](const BuildContext& c) { return "unsharp=5:5:" + c.text("amount"); }),
		simple("denoise", "Reduce noise and grain.",
			{ num("strength", "Denoise strength", 4, 1, 20) },
			[](const BuildContext& c) { return "hqdn3d=" + c.text("strength"); }),
		simple("vignette", "Darken the corners.",
			{ num("strength", "Vignette angle in radians; larger is stronger", 0.6, 0.1, 1.5) },
			[](const BuildContext& c) { return "vignette=angle=" + c.text("strength"); }),
		custom("text", "Draw a text caption on the picture.",
			Target::Visual, OutputKind::Same,
			{ text("text", "The caption to draw", std::nullopt),
			  choice("position", "Where to place the text", positions, "bottom"),
			  integer("size", "Font size in pixels", 48, 8, 400),
			  text("color", "Text color name or #RRGGBB", "white"),
			  choice("box", "Draw a translucent box behind the text", { "yes", "no" }, "yes") },
			buildText),
		custom("convert", "Convert to another file format.",
			Target::Visual, OutputKind::FormatParam,
			{ choice("format", "Target format",
				{ "png", "jpg", "webp", "bmp", "mp4", "webm", "mov", "mkv", "gif", "mp3" }, std::nullopt) },
			buildConvert),

		// --- Video only ---
		custom("trim", "Keep only part of a video, between start and end (seconds).",
			Target::Video, OutputKind::Same,
			{ num("start", "Start time in seconds", 0, 0, 86400),
			  num("end", "End time in seconds", std::nullopt, 0, 86400),
			  num("duration", "Length in seconds, used when end is not given", std::nullopt, 0.05, 86400) },
			buildTrim),
		custom("speed", "Speed up or slow down a video. 2 is twice as fast, 0.5 is half speed.",
			Target::Video, OutputKind::Same,
			{ num("factor", "Speed multiplier", 2, 0.25, 4) },
			buildSpeed),
		custom("reverse", "Play a video backwards (clips up to 60s).",
			Target::Video, OutputKind::Same, {}, buildReverse),
		custom("mute", "Remove the audio track from a video.",
			Target::Video, OutputKind::Same, {}, buildMute),
		custom("volume", "Change audio volume. 2 is twice as loud, 0.5 is half.",
			Target::Video, OutputKind::Same,
			{ num("factor", "Volume multiplier", 1.5, 0, 5) },
			buildVolume),
		custom("fade", "Fade in from black at the start and/or out to black at the end.",
			Target::Video, OutputKind::Same,
			{ num("in", "Fade-in duration in seconds (0 = none)", 1, 0, 30),
			  num("out", "Fade-out duration in seconds (0 = none)", 1, 0, 30) },
			buildFade),
		custom("extract_frame", "Save a single frame of a video as an image.",
			Target::Video, OutputKind::Image,
			{ num("time", "Timestamp in seconds", 0, 0, 86400) },
			buildExtractFrame),
		custom("extract_audio", "Save the audio track of a video as an MP3.",
			Target::Video, OutputKind::Audio, {}, buildExtractAudio),
		custom("to_gif", "Turn a video (or part of it) into an animated GIF.",
			Target::Video, OutputKind::Gif,
			{ integer("fps", "Frames per second", 12, 1, 30),
			  integer("width", "GIF width in pixels", 480, 32, 1280),
			  num("start", "Start time in seconds", std::nullopt, 0, 86400),
			  num("duration", "Length in seconds", std::nullopt, 0.1, 60) },
			buildGif),
	};
}

const char* toString(ParamType t)
{
	switch (t) {
	case ParamType::Number: return "number";
	case ParamType::Integer: return "integer";
	case ParamType::String: return "string";
	default: return "enum";
	}
}

const char* toString(OutputKind k)
{
	switch (k) {
	case OutputKind::Image: return "image";
	case OutputKind::Audio: return "audio";
	case OutputKind::Gif: return "gif";
	case OutputKind::FormatParam: return "format";
	default: return "same";
	}
}

} // namespace

// ---------------------------------------------------------------------------
// Public API

const std::vector<Operation>& operations()
{
	static const std::vector<Operation> ops = makeOperations();
	return ops;
}

const Operation* findOperation(const std::string& name)
{
	std::string key = lower(name);
	for (const auto& op : operations()) {
		if (op.name == key) return &op;
	}
	return nullptr;
}

bool acceptsKind(const Operation& op, MediaKind kind)
{
	if (op.target == Target::Video) return kind == MediaKind::Video;
	return kind == MediaKind::Image || kind == MediaKind::Video;
}

Params validateParams(const Operation& op, const Params& raw, std::vector<std::string>& warnings)
{
	Params out;
	std::set<std::string> known;

	for (const auto& spec : op.params) {
		known.insert(spec.name);
		auto it = raw.find(spec.name);
		if (it == raw.end() || it->second.empty()) {
			if (spec.defaultValue) out[spec.name] = *spec.defaultValue;
			else if (spec.required) throw std::invalid_argument(op.name + ": missing required parameter '" + spec.name + "'");
			continue;
		}

		const std::string& value = it->second;
		switch (spec.type) {
		case ParamType::Number:
		case ParamType::Integer: {
			char* end = nullptr;
			double v = std::strtod(value.c_str(), &end);
			if (end == value.c_str() || *end != '\0' || !std::isfinite(v)) {
				throw std::invalid_argument(op.name + ": parameter '" + spec.name + "' must be a number, got '" + value + "'");
			}
			if (spec.type == ParamType::Integer) v = std::round(v);
			double clamped = std::clamp(v, spec.min.value_or(v), spec.max.value_or(v));
			if (clamped != v) {
				warnings.push_back(op.name + ": " + spec.name + " " + fmt(v) + " adjusted to " + fmt(clamped));
			}
			out[spec.name] = fmt(clamped);
			break;
		}
		case ParamType::Enum: {
			std::string v = lower(value);
			if (std::find(spec.choices.begin(), spec.choices.end(), v) == spec.choices.end()) {
				std::string list;
				for (const auto& c : spec.choices) list += (list.empty() ? "" : ", ") + c;
				throw std::invalid_argument(op.name + ": parameter '" + spec.name + "' must be one of: " + list);
			}
			out[spec.name] = v;
			break;
		}
		case ParamType::String:
			out[spec.name] = value;
			break;
		}
	}

	for (const auto& [key, value] : raw) {
		if (!known.count(key)) warnings.push_back(op.name + ": ignored unknown parameter '" + key + "'");
	}
	return out;
}

std::string operationsToJson()
{
	std::vector<std::string> items;
	for (const auto& op : operations()) {
		std::vector<std::string> params;
		for (const auto& p : op.params) {
			json::Object o;
			o.str("name", p.name).str("type", toString(p.type)).str("description", p.description).boolean("required", p.required);
			if (p.defaultValue) o.str("default", *p.defaultValue);
			if (p.min) o.num("min", *p.min);
			if (p.max) o.num("max", *p.max);
			if (!p.choices.empty()) {
				std::vector<std::string> choices;
				for (const auto& c : p.choices) choices.push_back(json::quote(c));
				o.raw("choices", json::array(choices));
			}
			params.push_back(o.build());
		}
		items.push_back(json::Object()
			.str("name", op.name)
			.str("description", op.description)
			.str("target", op.target == Target::Video ? "video" : "visual")
			.str("output", toString(op.output))
			.raw("params", json::array(params))
			.build());
	}
	return json::array(items);
}

} // namespace pse
