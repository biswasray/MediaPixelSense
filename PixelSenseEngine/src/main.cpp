// main.cpp : PixelSenseEngine command line interface.
//
//   PixelSenseEngine ops
//   PixelSenseEngine probe <file>
//   PixelSenseEngine apply <operation> <input> <output> [key=value ...] [--dry-run]
//
// Every command prints a single JSON document to stdout.

#include "Json.h"
#include "Media.h"
#include "Operations.h"
#include "Process.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <shellapi.h>
#endif

using namespace pse;

namespace {

// Command line arguments as UTF-8, regardless of the console code page.
std::vector<std::string> utf8Args(int argc, char** argv)
{
#ifdef _WIN32
	(void)argc;
	(void)argv;
	int count = 0;
	LPWSTR* wargs = CommandLineToArgvW(GetCommandLineW(), &count);
	std::vector<std::string> out;
	for (int i = 0; i < count; ++i) {
		int len = WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, nullptr, 0, nullptr, nullptr);
		std::string s(len > 0 ? len - 1 : 0, '\0');
		WideCharToMultiByte(CP_UTF8, 0, wargs[i], -1, s.data(), len, nullptr, nullptr);
		out.push_back(std::move(s));
	}
	LocalFree(wargs);
	return out;
#else
	return std::vector<std::string>(argv, argv + argc);
#endif
}

int fail(const std::string& message, int code = 1)
{
	std::cout << json::Object().boolean("ok", false).str("error", message).build() << std::endl;
	return code;
}

// Keeps the last lines of FFmpeg output, which is where the error is.
std::string tail(const std::string& text, size_t maxLines = 6)
{
	size_t pos = text.size();
	size_t lines = 0;
	while (pos > 0 && lines <= maxLines) {
		pos = text.find_last_of('\n', pos - 1);
		if (pos == std::string::npos) return text;
		++lines;
	}
	return text.substr(pos + 1);
}

int cmdApply(const std::vector<std::string>& args)
{
	if (args.size() < 5) return fail("usage: apply <operation> <input> <output> [key=value ...] [--dry-run]", 2);

	const Operation* op = findOperation(args[2]);
	if (!op) return fail("unknown operation '" + args[2] + "'");

	const std::string& input = args[3];
	const std::string& output = args[4];
	if (!std::filesystem::exists(std::filesystem::path(reinterpret_cast<const char8_t*>(input.c_str())))) return fail("input file not found: " + input);

	Params raw;
	bool dryRun = false;
	for (size_t i = 5; i < args.size(); ++i) {
		if (args[i] == "--dry-run") {
			dryRun = true;
			continue;
		}
		auto eq = args[i].find('=');
		if (eq == std::string::npos) return fail("expected key=value, got '" + args[i] + "'", 2);
		raw[args[i].substr(0, eq)] = args[i].substr(eq + 1);
	}

	try {
		BuildContext ctx;
		ctx.input = input;
		ctx.output = output;
		ctx.outputExt = extensionOf(output);
		ctx.info = probe(input);
		if (!acceptsKind(*op, ctx.info.kind)) {
			return fail(op->name + " does not work on " + toString(ctx.info.kind) + " files");
		}

		std::vector<std::string> warnings;
		ctx.params = validateParams(*op, raw, warnings);

		std::vector<std::string> command{ ffmpegPath(), "-hide_banner", "-nostdin", "-y" };
		auto opArgs = op->build(ctx);
		command.insert(command.end(), opArgs.begin(), opArgs.end());

		std::vector<std::string> warningJson;
		for (const auto& w : warnings) warningJson.push_back(json::quote(w));

		json::Object result;
		result.boolean("ok", true)
			.str("operation", op->name)
			.str("output", output)
			.str("command", formatCommandLine(command))
			.raw("warnings", json::array(warningJson));

		if (!dryRun) {
			auto run = runProcess(command);
			if (run.exitCode != 0) return fail("ffmpeg failed: " + tail(run.output));
			result.raw("info", toJson(probe(output)));
		}
		std::cout << result.build() << std::endl;
		return 0;
	} catch (const std::exception& e) {
		return fail(e.what());
	}
}

void printHelp()
{
	std::cout <<
		"PixelSenseEngine - media edit engine\n\n"
		"  PixelSenseEngine ops                         List operations as JSON\n"
		"  PixelSenseEngine probe <file>                Describe a media file\n"
		"  PixelSenseEngine apply <op> <in> <out> [key=value ...] [--dry-run]\n\n"
		"Environment: PSE_FFMPEG, PSE_FFPROBE, PSE_FONT\n";
}

} // namespace

int main(int argc, char** argv)
{
	auto args = utf8Args(argc, argv);
	if (args.size() < 2 || args[1] == "help" || args[1] == "--help") {
		printHelp();
		return args.size() < 2 ? 2 : 0;
	}

	const std::string& command = args[1];
	if (command == "ops") {
		std::cout << operationsToJson() << std::endl;
		return 0;
	}
	if (command == "probe") {
		if (args.size() < 3) return fail("usage: probe <file>", 2);
		try {
			std::cout << json::Object().boolean("ok", true).raw("info", toJson(probe(args[2]))).build() << std::endl;
			return 0;
		} catch (const std::exception& e) {
			return fail(e.what());
		}
	}
	if (command == "apply") return cmdApply(args);

	return fail("unknown command '" + command + "'", 2);
}
