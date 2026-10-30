// Process.h : Runs an external program (ffmpeg/ffprobe) without a shell,
// capturing its combined stdout/stderr.

#pragma once

#include <string>
#include <vector>

namespace pse {

struct ProcessResult {
	int exitCode = -1;
	std::string output; // stdout and stderr interleaved
};

// args[0] is the program; it is resolved through PATH when not a full path.
// All strings are UTF-8.
ProcessResult runProcess(const std::vector<std::string>& args);

// Human-readable command line, for logging and --dry-run.
std::string formatCommandLine(const std::vector<std::string>& args);

} // namespace pse
