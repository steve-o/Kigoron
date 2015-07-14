// Copyright (c) 2011 The Chromium Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef CHROMIUM_LOGGING_HH__
#define CHROMIUM_LOGGING_HH__
#pragma once

#include <sstream>

/* Boost noncopyable base class */
#include <boost/utility.hpp>

/* Boost current function macro */
#include <boost/current_function.hpp>

/* Instructions
 * ------------
 *
 * Make a bunch of macros for logging.  The way to log things is to stream
 * things to LOG(<a particular severity level>).  E.g.,
 *
 *   LOG(INFO) << "Found " << num_cookies << " cookies";
 *
 * You can also do conditional logging:
 *
 *   LOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
 *
 * The above will cause log messages to be output on the 1st, 11th, 21st, ...
 * times it is executed.  Note that the special COUNTER value is used to
 * identify which repetition is happening.
 *
 * The CHECK(condition) macro is active in both debug and release builds and
 * effectively performs a LOG(FATAL) which terminates the process and
 * generates a crashdump unless a debugger is attached.
 *
 * There are also "debug mode" logging macros like the ones above:
 *
 *   DLOG(INFO) << "Found cookies";
 *
 *   DLOG_IF(INFO, num_cookies > 10) << "Got lots of cookies";
 *
 * All "debug mode" logging is compiled away to nothing for non-debug mode
 * compiles.  LOG_IF and development flags also work well together
 * because the code can be compiled away sometimes.
 *
 * We also have
 *
 *   LOG_ASSERT(assertion);
 *   DLOG_ASSERT(assertion);
 *
 * which is syntactic sugar for {,D}LOG_IF(FATAL, assert fails) << assertion;
 *
 * There are "verbose level" logging macros.  They look like
 *
 *   VLOG(1) << "I'm printed when you run the program with --v=1 or more";
 *   VLOG(2) << "I'm printed when you run the program with --v=2 or more";
 *
 * These always log at the INFO log level (when they log at all).
 *
 * There's also VLOG_IS_ON(n) "verbose level" condition macro. To be used as
 *
 *   if (VLOG_IS_ON(2)) {
 *     // do some logging preparation and logging
 *     // that can't be accomplished with just VLOG(2) << ...;
 *   }
 *
 * There is also a VLOG_IF "verbose level" condition macro for sample
 * cases, when some extra computation and preparation for logs is not
 * needed.
 *
 *   VLOG_IF(1, (size > 1024))
 *      << "I'm printed when size is more than 1024 and when you run the "
 *         "program with --v=1 or more";
 *
 * We also override the standard 'assert' to use 'DLOG_ASSERT'.
 *
 * The supported severity levels for macros that allow you to specify one
 * are (in increasing order of severity) INFO, WARNING, ERROR,
 * and FATAL.
 *
 * Very important: logging a message at the FATAL severity level causes
 * the program to terminate (after the message is logged).
 *
 * There is also the special severity of DFATAL, which logs FATAL in
 * debug mode, ERROR in normal mode.
 */

namespace logging {

// Where to record logging output? A flat file and/or system debug log.
enum LoggingDestination { LOG_NONE,
                          LOG_ONLY_TO_FILE,
                          LOG_ONLY_TO_SYSTEM_DEBUG_LOG,
                          LOG_TO_BOTH_FILE_AND_SYSTEM_DEBUG_LOG };

// Indicates that the log file should be locked when being written to.
// Often, there is no locking, which is fine for a single threaded program.
// If logging is being done from multiple threads or there can be more than
// one process doing the logging, the file should be locked during writes to
// make each log outut atomic. Other writers will block.
//
// All processes writing to the log file must have their locking set for it to
// work properly. Defaults to DONT_LOCK_LOG_FILE.
enum LogLockingState { LOCK_LOG_FILE, DONT_LOCK_LOG_FILE };

// On startup, should we delete or append to an existing log file (if any)?
// Defaults to APPEND_TO_OLD_LOG_FILE.
enum OldFileDeletionState { DELETE_OLD_LOG_FILE, APPEND_TO_OLD_LOG_FILE };

enum DcheckState {
  DISABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS,
  ENABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS
};

// Define different names for the ChromiumInitLoggingImpl() function depending on
// whether NDEBUG is defined or not so that we'll fail to link if someone tries
// to compile logging.cc with NDEBUG but includes logging.h without defining it,
// or vice versa.
#if NDEBUG
#define ChromiumInitLoggingImpl ChromiumInitLoggingImpl_built_with_NDEBUG
#else
#define ChromiumInitLoggingImpl ChromiumInitLoggingImpl_built_without_NDEBUG
#endif

// Implementation of the InitLogging() method declared below.  We use a
// more-specific name so we can #define it above without affecting other code
// that has named stuff "InitLogging".
bool ChromiumInitLoggingImpl(const char* log_file,
			     LoggingDestination logging_dest,
			     LogLockingState lock_log,
			     OldFileDeletionState delete_old,
			     DcheckState dcheck_state);

// Sets the global logging state. Calling this function
// is recommended, and is normally done at the beginning of application init.
// If you don't call it, all the flags will be initialized to their default
// values, and there is a race condition that may leak a critical section
// object if two threads try to do the first log at the same time.
// See the definition of the enums above for descriptions and default values.
//
// This function may be called a second time to re-direct logging (e.g after
// loging in to a user partition), however it should never be called more than
// twice.
inline bool InitLogging(const char* log_file,
			LoggingDestination logging_dest,
			LogLockingState lock_log,
			OldFileDeletionState delete_old,
			DcheckState dcheck_state) {
  return ChromiumInitLoggingImpl(log_file, logging_dest, lock_log,
				 delete_old, dcheck_state);
}

	void SetMinLogLevel (int level);
	int GetMinLogLevel();

	int GetVlogVerbosity();
	int GetVlogLevelHelper (const char* file_start, size_t N);

	template <size_t N>
	int GetVlogLevel (const char (&file)[N]) {
		return GetVlogLevelHelper (file, N);
	}

// Sets the common items you want to be prepended to each log message.
// process and thread IDs default to off, the timestamp defaults to on.
// If this function is not called, logging defaults to writing the timestamp
// only.
	void SetLogItems(bool enable_process_id, bool enable_thread_id,
                             bool enable_timestamp, bool enable_tickcount);

// Sets the Log Message Handler that gets passed every log message before
// it's sent to other log destinations (if any).
// Returns true to signal that it handled the message and the message
// should not be sent to other log destinations.
	typedef bool (*LogMessageHandlerFunction)(int severity,
		const char* file, int line, size_t message_start, const std::string& str);
	void SetLogMessageHandler(LogMessageHandlerFunction handler);
	LogMessageHandlerFunction GetLogMessageHandler();

	typedef int LogSeverity;
	const LogSeverity LOG_VERBOSE = -1;
/* Note: the log severities are used to index into the array of names,
 * see log_severity_names.
 */
	const LogSeverity LOG_INFO = 0;
	const LogSeverity LOG_WARNING = 1;
	const LogSeverity LOG_ERROR = 2;
	const LogSeverity LOG_FATAL = 3;
	const LogSeverity LOG_NUM_SEVERITIES = 4;

/* LOG_DFATAL is LOG_FATAL in debug mode, ERROR in normal mode */
	#ifdef NDEBUG
	const LogSeverity LOG_DFATAL = LOG_ERROR;
	#else
	const LogSeverity LOG_DFATAL = LOG_FATAL;
	#endif

/* A few definitions of macros that don't generate much code. These are used
 * by LOG() and LOG_IF, etc. Since these are used all over our code, it's
 * better to have compact code for these operations.
 */
	#define COMPACT_LOG_EX_INFO(ClassName, ...) \
		logging::ClassName(__FILE__, __LINE__, logging::LOG_INFO , ##__VA_ARGS__)
	#define COMPACT_LOG_EX_WARNING(ClassName, ...) \
		logging::ClassName(__FILE__, __LINE__, logging::LOG_WARNING , ##__VA_ARGS__)
	#define COMPACT_LOG_EX_ERROR(ClassName, ...) \
		logging::ClassName(__FILE__, __LINE__, logging::LOG_ERROR , ##__VA_ARGS__)
	#define COMPACT_LOG_EX_FATAL(ClassName, ...) \
		logging::ClassName(__FILE__, __LINE__, logging::LOG_FATAL , ##__VA_ARGS__)
	#define COMPACT_LOG_EX_DFATAL(ClassName, ...) \
		logging::ClassName(__FILE__, __LINE__, logging::LOG_DFATAL , ##__VA_ARGS__)

	#define COMPACT_LOG_INFO \
		COMPACT_LOG_EX_INFO(LogMessage)
	#define COMPACT_LOG_WARNING \
		COMPACT_LOG_EX_WARNING(LogMessage)
	#define COMPACT_LOG_ERROR \
		COMPACT_LOG_EX_ERROR(LogMessage)
	#define COMPACT_LOG_FATAL \
		COMPACT_LOG_EX_FATAL(LogMessage)
	#define COMPACT_LOG_DFATAL \
		COMPACT_LOG_EX_DFATAL(LogMessage)

/* wingdi.h defines ERROR to be 0. When we call LOG(ERROR), it gets
 * substituted with 0, and it expands to COMPACT_LOG_0. To allow us
 * to keep using this syntax, we define this macro to do the same thing
 * as COMPACT_LOG_ERROR, and also define ERROR the same way that
 * the Windows SDK does for consistency.
 */
	#define ERROR 0
	#define COMPACT_LOG_EX_0(ClassName, ...) \
		COMPACT_LOG_EX_ERROR(ClassName , ##__VA_ARGS__)
	#define COMPACT_LOG_0 COMPACT_LOG_ERROR
/* Needed for LOG_IS_ON(ERROR). */
	const LogSeverity LOG_0 = LOG_ERROR;

	#define LOG_IS_ON(severity) \
		((::logging::LOG_ ## severity) >= ::logging::GetMinLogLevel())

/* Using the v-logging functions in conjunction with --vmodule
 * may be slow.
 */
	#define VLOG_IS_ON(verboselevel) \
		((verboselevel) <= ::logging::GetVlogLevel(__FILE__))

/* Helper macro which avoids evaluating the arguments to a stream if
 * the condition doesn't hold.
 */
	#define LAZY_STREAM(stream, condition)                                  \
		!(condition) ? (void) 0 : ::logging::LogMessageVoidify() & (stream)

/* Redirect LOG(INFO) to COMPACT_LOG_INFO.
 */
	#define LOG_STREAM(severity) COMPACT_LOG_ ## severity.stream()

	#define LOG(severity) LAZY_STREAM(LOG_STREAM(severity), LOG_IS_ON(severity))
	#define LOG_IF(severity, condition) \
		LAZY_STREAM(LOG_STREAM(severity), LOG_IS_ON(severity) && (condition))

/* The VLOG macros log with negative verbosities.
 */
	#define VLOG_STREAM(verbose_level) \
		logging::LogMessage(__FILE__, __LINE__, -verbose_level).stream()

	#define VLOG(verbose_level) \
		LAZY_STREAM(VLOG_STREAM(verbose_level), VLOG_IS_ON(verbose_level))

	#define VLOG_IF(verbose_level, condition) \
		LAZY_STREAM(VLOG_STREAM(verbose_level), \
			VLOG_IS_ON(verbose_level) && (condition))

	#define LOG_ASSERT(condition)  \
		LOG_IF(FATAL, !(condition)) << "Assert failed: " #condition ". "

/* The actual stream used isn't important. */
	#define EAT_STREAM_PARAMETERS \
		true ? (void) 0 : ::logging::LogMessageVoidify() & LOG_STREAM(FATAL)

	#define CHECK(condition)                       \
		LAZY_STREAM(LOG_STREAM(FATAL), !(condition)) \
		<< "Check failed: " #condition ". "

/* Helper macro for binary operators.
/* Don't use this macro directly in your code, use CHECK_EQ et al below.
 */
	#define CHECK_OP(name, op, val1, val2) \
		if (std::string* _result = \
			logging::Check##name##Impl((val1), (val2), \
				#val1 " " #op " " #val2)) \
		logging::LogMessage(__FILE__, __LINE__, _result).stream()

/* Build the error message string.  This is separate from the "Impl"
 * function template because it is not performance critical and so can
 * be out of line, while the "Impl" code should be inline.  Caller
 * takes ownership of the returned string.
 */
	template<class t1, class t2>
	std::string* MakeCheckOpString (const t1& v1, const t2& v2, const char* names) {
		std::ostringstream ss;
		ss << names << " (" << v1 << " vs. " << v2 << ")";
		std::string* msg = new std::string (ss.str());
		return msg;
	}

// MSVC doesn't like complex extern templates and DLLs.
#if !defined(_MSC_VER)
// Commonly used instantiations of MakeCheckOpString<>. Explicitly instantiated
// in logging.cc.
	extern template std::string* MakeCheckOpString<int, int>(const int&, const int&, const char* names);
	extern template std::string* MakeCheckOpString<unsigned long, unsigned long>(const unsigned long&, const unsigned long&, const char* names);
	extern template std::string* MakeCheckOpString<unsigned long, unsigned int>(const unsigned long&, const unsigned int&, const char* names);
	extern template std::string* MakeCheckOpString<unsigned int, unsigned long>(const unsigned int&, const unsigned long&, const char* names);
	extern template std::string* MakeCheckOpString<std::string, std::string>(const std::string&, const std::string&, const char* name);
#endif

/* Helper functions for CHECK_OP macro.
 * The (int, int) specialization works around the issue that the compiler
 * will not instantiate the template version of the function on values of
 * unnamed enum type - see comment below.
 */
	#define DEFINE_CHECK_OP_IMPL(name, op) \
		template <class t1, class t2> \
		inline std::string* Check##name##Impl (const t1& v1, const t2& v2, const char* names) { \
			if (v1 op v2) return NULL; \
			else return MakeCheckOpString (v1, v2, names); \
		} \
		inline std::string* Check##name##Impl (int v1, int v2, const char* names) { \
			if (v1 op v2) return NULL; \
			else return MakeCheckOpString (v1, v2, names); \
		}
	DEFINE_CHECK_OP_IMPL(EQ, ==)
	DEFINE_CHECK_OP_IMPL(NE, !=)
	DEFINE_CHECK_OP_IMPL(LE, <=)
	DEFINE_CHECK_OP_IMPL(LT, < )
	DEFINE_CHECK_OP_IMPL(GE, >=)
	DEFINE_CHECK_OP_IMPL(GT, > )
	#undef DEFINE_CHECK_OP_IMPL

	#define CHECK_EQ(val1, val2) CHECK_OP(EQ, ==, val1, val2)
	#define CHECK_NE(val1, val2) CHECK_OP(NE, !=, val1, val2)
	#define CHECK_LE(val1, val2) CHECK_OP(LE, <=, val1, val2)
	#define CHECK_LT(val1, val2) CHECK_OP(LT, < , val1, val2)
	#define CHECK_GE(val1, val2) CHECK_OP(GE, >=, val1, val2)
	#define CHECK_GT(val1, val2) CHECK_OP(GT, > , val1, val2)

	#if LOGGING_IS_OFFICIAL_BUILD
/* In order to have optimized code for official builds, remove DLOGs and
 * DCHECKs.
 */
	#	define ENABLE_DLOG 0
	#	define ENABLE_DCHECK 0

	#elif defined(NDEBUG)
/* Otherwise, if we're a release build, remove DLOGs but not DCHECKs
 * (since those can still be turned on via a command-line flag).
 */
	#	define ENABLE_DLOG 0
	#	define ENABLE_DCHECK 1

	#else
/* Otherwise, we're a debug build so enable DLOGs and DCHECKs.
 */
	#	define ENABLE_DLOG 1
	#	define ENABLE_DCHECK 1
	#endif

/* Definitions for DLOG et al. */

	#if ENABLE_DLOG

	#	define DLOG_IS_ON(severity) LOG_IS_ON(severity)
	#	define DLOG_IF(severity, condition) LOG_IF(severity, condition)
	#	define DLOG_ASSERT(condition) LOG_ASSERT(condition)
	#	define DVLOG_IF(verboselevel, condition) VLOG_IF(verboselevel, condition)

	#else  // ENABLE_DLOG

/* If ENABLE_DLOG is off, we want to avoid emitting any references to
 * |condition| (which may reference a variable defined only if NDEBUG
 * is not defined).  Contrast this with DCHECK et al., which has
 * different behavior.
 */
	#	define DLOG_IS_ON(severity) false
	#	define DLOG_IF(severity, condition) EAT_STREAM_PARAMETERS
	#	define DLOG_ASSERT(condition) EAT_STREAM_PARAMETERS
	#	define DVLOG_IF(verboselevel, condition) EAT_STREAM_PARAMETERS

	#endif  // ENABLE_DLOG

/* DEBUG_MODE is for uses like
 *   if (DEBUG_MODE) foo.CheckThatFoo();
 * instead of
 *   #ifndef NDEBUG
 *     foo.CheckThatFoo();
 *   #endif
 *
 * We tie its state to ENABLE_DLOG.
 */
	enum { DEBUG_MODE = ENABLE_DLOG };

	#undef ENABLE_DLOG

	#define DLOG(severity)                                          \
		LAZY_STREAM(LOG_STREAM(severity), DLOG_IS_ON(severity))

	#define DVLOG(verboselevel) DVLOG_IF(verboselevel, VLOG_IS_ON(verboselevel))

/* Definitions for DCHECK et al. */

	#if ENABLE_DCHECK

	#	if defined(NDEBUG)

	extern DcheckState g_dcheck_state;

	#		if defined(DCHECK_ALWAYS_ON)

	#			define DCHECK_IS_ON() true
	#			define COMPACT_LOG_EX_DCHECK(ClassName, ...) \
					COMPACT_LOG_EX_FATAL(ClassName , ##__VA_ARGS__)
	#			define COMPACT_LOG_DCHECK COMPACT_LOG_FATAL
	const LogSeverity LOG_DCHECK = LOG_FATAL;

	#		else

	#			define COMPACT_LOG_EX_DCHECK(ClassName, ...) \
					COMPACT_LOG_EX_ERROR(ClassName , ##__VA_ARGS__)
	#			define COMPACT_LOG_DCHECK COMPACT_LOG_ERROR
	const LogSeverity LOG_DCHECK = LOG_ERROR;
	#			define DCHECK_IS_ON() \
					((::logging::g_dcheck_state == \
					  ::logging::ENABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS) && \
					LOG_IS_ON(DCHECK))

	#		endif  /* defined(DCHECK_ALWAYS_ON) */

	#	else  /* defined(NDEBUG) */

/* On a regular debug build, we want to have DCHECKs enabled. */
	#		define COMPACT_LOG_EX_DCHECK(ClassName, ...) \
				COMPACT_LOG_EX_FATAL(ClassName , ##__VA_ARGS__)
	#		define COMPACT_LOG_DCHECK COMPACT_LOG_FATAL
	const LogSeverity LOG_DCHECK = LOG_FATAL;
	#		define DCHECK_IS_ON() true

	#	endif  /* defined(NDEBUG) */

	#else  /* ENABLE_DCHECK */

/* These are just dummy values since DCHECK_IS_ON() is always false in
 * this case.
 */
	#	define COMPACT_LOG_EX_DCHECK(ClassName, ...) \
			COMPACT_LOG_EX_INFO(ClassName , ##__VA_ARGS__)
	#	define COMPACT_LOG_DCHECK COMPACT_LOG_INFO
	const LogSeverity LOG_DCHECK = LOG_INFO;
	#	define DCHECK_IS_ON() false

	#endif  /* ENABLE_DCHECK */
	#undef ENABLE_DCHECK

/* DCHECK et al. make sure to reference |condition| regardless of
 * whether DCHECKs are enabled; this is so that we don't get unused
 * variable warnings if the only use of a variable is in a DCHECK.
 * This behavior is different from DLOG_IF et al.
 */
	#define DCHECK(condition) \
		LAZY_STREAM(LOG_STREAM(DCHECK), DCHECK_IS_ON() && !(condition)) \
		<< "Check failed: " #condition ". "

/* Helper macro for binary operators.
 * Don't use this macro directly in your code, use DCHECK_EQ et al below.
 */
	#define DCHECK_OP(name, op, val1, val2) \
		if (DCHECK_IS_ON()) \
			if (std::string* _result = \
				logging::Check##name##Impl((val1), (val2), \
					#val1 " " #op " " #val2)) \
		logging::LogMessage( \
			__FILE__, __LINE__, ::logging::LOG_DCHECK, \
			_result).stream()

/* Equality/Inequality checks - compare two values, and log a
 * LOG_DCHECK message including the two values when the result is not
 * as expected.  The values must have operator<<(ostream, ...)
 * defined.
 *
 * You may append to the error message like so:
 *   DCHECK_NE(1, 2) << ": The world must be ending!";
 *
 * We are very careful to ensure that each argument is evaluated exactly
 * once, and that anything which is legal to pass as a function argument is
 * legal here.  In particular, the arguments may be temporary expressions
 * which will end up being destroyed at the end of the apparent statement,
 * for example:
 *   DCHECK_EQ(string("abc")[1], 'b');
 *
 * WARNING: These may not compile correctly if one of the arguments is a pointer
 * and the other is NULL. To work around this, simply static_cast NULL to the
 * type of the desired pointer.
 */
	#define DCHECK_EQ(val1, val2) DCHECK_OP(EQ, ==, val1, val2)
	#define DCHECK_NE(val1, val2) DCHECK_OP(NE, !=, val1, val2)
	#define DCHECK_LE(val1, val2) DCHECK_OP(LE, <=, val1, val2)
	#define DCHECK_LT(val1, val2) DCHECK_OP(LT, < , val1, val2)
	#define DCHECK_GE(val1, val2) DCHECK_OP(GE, >=, val1, val2)
	#define DCHECK_GT(val1, val2) DCHECK_OP(GT, > , val1, val2)

	#define NOTREACHED() DCHECK(false)

/* Redefine the standard assert to use our nice log files. */
	#undef assert
	#define assert(x) DLOG_ASSERT(x)

/* This class more or less represents a particular log message.  You
 * create an instance of LogMessage and then stream stuff to it.
 * When you finish streaming to it, ~LogMessage is called and the
 * full message gets streamed to the appropriate destination.
 *
 * You shouldn't actually use LogMessage's constructor to log things,
 * though.  You should use the LOG() macro (and variants thereof)
 * above.
 */
	class LogMessage :
		boost::noncopyable
	{
	public:
		LogMessage (const char* file, int line, LogSeverity severity, int ctr);

/* Two special constructors that generate reduced amounts of code at
 * LOG call sites for common cases.
 *
 * Used for LOG(INFO): Implied are:
 * severity = LOG_INFO, ctr = 0
 *
 * Using this constructor instead of the more complex constructor above
 * saves a couple of bytes per call site.
 */
		LogMessage (const char* file_, int line_);

/* Used for LOG(severity) where severity != INFO.  Implied
 * are: ctr = 0
 *
 * Using this constructor instead of the more complex constructor above
 * saves a couple of bytes per call site.
 */
		LogMessage (const char* file, int line, LogSeverity severity);

/* A special constructor used for check failures.  Takes ownership
 * of the given string.
 * Implied severity = LOG_FATAL
 */
		LogMessage (const char* file, int line, std::string* result);

/* A special constructor used for check failures, with the option to
 * specify severity.  Takes ownership of the given string.
 */
		LogMessage (const char* file, int line, LogSeverity severity, std::string* result);

		~LogMessage();

		std::ostream& stream() { return stream_; }

	private:
		void Init (const char* file_, int line_);

		LogSeverity severity_;
		std::ostringstream stream_;
		size_t message_start_;  // Offset of the start of the message (past prefix
					// info).
/* The file and line information passed in to the constructor. */
		const char* file_;
		const int line_;
	};

/* This class is used to explicitly ignore values in the conditional
 * logging macros.  This avoids compiler warnings like "value computed
 * is not used" and "statement has no effect".
 */
	class LogMessageVoidify {
	public:
		LogMessageVoidify() { }
/* This has to be an operator with a precedence lower than << but
 * higher than ?:
 */
		void operator& (std::ostream&) { }
	};

} /* namespace logging */

/* The NOTIMPLEMENTED() macro annotates codepaths which have
 * not been implemented yet.
 *
 * The implementation of this macro is controlled by NOTIMPLEMENTED_POLICY:
 *   0 -- Do nothing (stripped by compiler)
 *   1 -- Warn at compile time
 *   2 -- Fail at compile time
 *   3 -- Fail at runtime (DCHECK)
 *   4 -- [default] LOG(ERROR) at runtime
 *   5 -- LOG(ERROR) at runtime, only once per call-site
 */
#ifndef NOTIMPLEMENTED_POLICY
/* Select default policy: LOG(ERROR) */
#	define NOTIMPLEMENTED_POLICY 4
#endif

#define NOTIMPLEMENTED_MSG "Not implemented reached in " << BOOST_CURRENT_FUNCTION

#if NOTIMPLEMENTED_POLICY == 0
#	define NOTIMPLEMENTED() EAT_STREAM_PARAMETERS
#elif NOTIMPLEMENTED_POLICY == 1
// TODO, figure out how to generate a warning
#	define NOTIMPLEMENTED() static_assert(false, NOT_IMPLEMENTED)
#elif NOTIMPLEMENTED_POLICY == 2
#	define NOTIMPLEMENTED() static_assert(false, NOT_IMPLEMENTED)
#elif NOTIMPLEMENTED_POLICY == 3
#	define NOTIMPLEMENTED() NOTREACHED()
#elif NOTIMPLEMENTED_POLICY == 4
#	define NOTIMPLEMENTED() LOG(ERROR) << NOTIMPLEMENTED_MSG
#elif NOTIMPLEMENTED_POLICY == 5
#	define NOTIMPLEMENTED() do {\
		static bool logged_once = false;\
		LOG_IF(ERROR, !logged_once) << NOTIMPLEMENTED_MSG;\
		logged_once = true;\
		} while(0)\
		EAT_STREAM_PARAMETERS
#endif

#endif /* CHROMIUM_LOGGING_HH__ */

/* eof */
