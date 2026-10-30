// Process.cpp : Platform-specific process spawning.

#include "Process.h"

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/wait.h>
#include <unistd.h>
#endif

namespace pse {

namespace {

bool needsQuoting(const std::string& arg)
{
	return arg.empty() || arg.find_first_of(" \t\n\v\"") != std::string::npos;
}

// Quotes one argument following the MSVCRT CommandLineToArgvW rules.
std::string quoteArg(const std::string& arg)
{
	if (!needsQuoting(arg)) return arg;

	std::string out = "\"";
	size_t backslashes = 0;
	for (char c : arg) {
		if (c == '\\') {
			++backslashes;
			continue;
		}
		if (c == '"') {
			out.append(backslashes * 2 + 1, '\\');
		} else {
			out.append(backslashes, '\\');
		}
		backslashes = 0;
		out += c;
	}
	out.append(backslashes * 2, '\\');
	out += '"';
	return out;
}

} // namespace

std::string formatCommandLine(const std::vector<std::string>& args)
{
	std::string line;
	for (size_t i = 0; i < args.size(); ++i) {
		if (i) line += ' ';
		line += quoteArg(args[i]);
	}
	return line;
}

#ifdef _WIN32

namespace {

std::wstring widen(const std::string& s)
{
	if (s.empty()) return {};
	int len = MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), nullptr, 0);
	std::wstring out(len, L'\0');
	MultiByteToWideChar(CP_UTF8, 0, s.data(), static_cast<int>(s.size()), out.data(), len);
	return out;
}

} // namespace

ProcessResult runProcess(const std::vector<std::string>& args)
{
	ProcessResult result;

	SECURITY_ATTRIBUTES sa{ sizeof(sa), nullptr, TRUE };
	HANDLE readPipe = nullptr;
	HANDLE writePipe = nullptr;
	if (!CreatePipe(&readPipe, &writePipe, &sa, 0)) {
		result.output = "CreatePipe failed";
		return result;
	}
	SetHandleInformation(readPipe, HANDLE_FLAG_INHERIT, 0);

	STARTUPINFOW si{};
	si.cb = sizeof(si);
	si.dwFlags = STARTF_USESTDHANDLES;
	si.hStdInput = GetStdHandle(STD_INPUT_HANDLE);
	si.hStdOutput = writePipe;
	si.hStdError = writePipe;

	std::wstring cmdLine = widen(formatCommandLine(args));
	PROCESS_INFORMATION pi{};
	BOOL ok = CreateProcessW(nullptr, cmdLine.data(), nullptr, nullptr, TRUE,
		CREATE_NO_WINDOW, nullptr, nullptr, &si, &pi);
	CloseHandle(writePipe);

	if (!ok) {
		CloseHandle(readPipe);
		result.output = "Failed to start '" + args[0] + "' (error " + std::to_string(GetLastError()) +
			"). Is it installed and on PATH?";
		return result;
	}

	char buffer[4096];
	DWORD bytesRead = 0;
	while (ReadFile(readPipe, buffer, sizeof(buffer), &bytesRead, nullptr) && bytesRead > 0) {
		result.output.append(buffer, bytesRead);
	}
	CloseHandle(readPipe);

	WaitForSingleObject(pi.hProcess, INFINITE);
	DWORD exitCode = 0;
	GetExitCodeProcess(pi.hProcess, &exitCode);
	result.exitCode = static_cast<int>(exitCode);
	CloseHandle(pi.hProcess);
	CloseHandle(pi.hThread);
	return result;
}

#else

ProcessResult runProcess(const std::vector<std::string>& args)
{
	ProcessResult result;
	int fds[2];
	if (pipe(fds) != 0) {
		result.output = "pipe failed";
		return result;
	}

	pid_t pid = fork();
	if (pid == 0) {
		dup2(fds[1], STDOUT_FILENO);
		dup2(fds[1], STDERR_FILENO);
		close(fds[0]);
		close(fds[1]);
		std::vector<char*> argv;
		for (const auto& a : args) argv.push_back(const_cast<char*>(a.c_str()));
		argv.push_back(nullptr);
		execvp(argv[0], argv.data());
		_exit(127);
	}
	close(fds[1]);
	if (pid < 0) {
		close(fds[0]);
		result.output = "fork failed";
		return result;
	}

	char buffer[4096];
	ssize_t n;
	while ((n = read(fds[0], buffer, sizeof(buffer))) > 0) {
		result.output.append(buffer, static_cast<size_t>(n));
	}
	close(fds[0]);

	int status = 0;
	waitpid(pid, &status, 0);
	result.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
	if (result.exitCode == 127 && result.output.empty()) {
		result.output = "Failed to start '" + args[0] + "'. Is it installed and on PATH?";
	}
	return result;
}

#endif

} // namespace pse
