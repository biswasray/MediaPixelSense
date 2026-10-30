// Operations.h : The registry of media edit operations. Each operation
// declares its parameters (used for validation and for the LLM schema) and
// knows how to translate itself into an FFmpeg invocation.

#pragma once

#include "Media.h"

#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace pse {

enum class ParamType { Number, Integer, String, Enum };

struct ParamSpec {
	std::string name;
	ParamType type = ParamType::Number;
	std::string description;
	bool required = false;
	std::optional<std::string> defaultValue;
	std::optional<double> min;
	std::optional<double> max;
	std::vector<std::string> choices; // Enum only
};

// Which inputs an operation accepts.
enum class Target { Visual /* image or video */, Video };

// What the output file should be. The caller picks the output path; this
// tells it which extension to use.
enum class OutputKind {
	Same,        // same container as the input
	Image,       // a still image (png)
	Audio,       // an audio file (mp3)
	Gif,         // an animated gif
	FormatParam, // the extension given by the "format" parameter
};

using Params = std::map<std::string, std::string>;

struct BuildContext {
	std::string input;
	std::string output;
	std::string outputExt;
	MediaInfo info;
	Params params; // validated, defaults applied

	bool has(const std::string& name) const { return params.count(name) > 0; }
	double number(const std::string& name) const;
	const std::string& text(const std::string& name) const;
};

struct Operation {
	std::string name;
	std::string description;
	Target target = Target::Visual;
	OutputKind output = OutputKind::Same;
	std::vector<ParamSpec> params;
	// Returns the FFmpeg arguments after "ffmpeg -y" (inputs, filters, output).
	std::function<std::vector<std::string>(const BuildContext&)> build;
};

const std::vector<Operation>& operations();
const Operation* findOperation(const std::string& name);

bool acceptsKind(const Operation& op, MediaKind kind);

// Applies defaults, checks required/typed values and clamps numbers into
// range. Throws std::invalid_argument with a user-facing message on error.
// Warnings about adjusted values are appended to `warnings`.
Params validateParams(const Operation& op, const Params& raw, std::vector<std::string>& warnings);

std::string operationsToJson();

} // namespace pse
