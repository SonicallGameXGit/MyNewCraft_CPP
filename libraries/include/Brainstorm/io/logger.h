#pragma once

namespace Brainstorm {
	class Logger {
	public:
		static void debug(const char* format, ...);
		static void info(const char* format, ...);
		static void warn(const char* format, ...);
		static void error(const char* format, ...);
		static void fatal(const char* format, ...);
		static void notice(const char* format, ...);
	};
}