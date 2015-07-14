/* logging.cc
 *
 * C++ stream based logging, forwarding onto Velocity Analytics Engine own logging API.
 *
 * Copyright (c) 2011 The Chromium Authors. All rights reserved.
 */

#include "logging.hh"

#define NOMINMAX
#include <winsock2.h>

#include <algorithm>
#include <cstdint>
#include <ctime>
#include <iomanip>

#include "chromium_switches.hh"
#include "command_line.hh"
#include "debug/stack_trace.hh"
#include "synchronization/lock_impl.hh"
#include "vlog.hh"

namespace logging {

DcheckState g_dcheck_state = DISABLE_DCHECK_FOR_NON_OFFICIAL_RELEASE_BUILDS;

namespace {

VlogInfo* g_vlog_info = nullptr;

const char* const log_severity_names[LOG_NUM_SEVERITIES] = {
	"INFO", "WARNING", "ERROR", "FATAL" };

int min_log_level = 0;

// The default set here for logging_destination will only be used if
// InitLogging is not called.
LoggingDestination logging_destination = LOG_ONLY_TO_SYSTEM_DEBUG_LOG;

// For LOG_ERROR and above, always print to stderr.
const int kAlwaysPrintErrorLevel = LOG_ERROR;

// Which log file to use? This is initialized by InitLogging or
// will be lazily initialized to the default value when it is
// first needed.
std::string* log_file_name = NULL;

// this file is lazily opened and the handle may be NULL
HANDLE log_file = NULL;

// what should be prepended to each message?
bool log_process_id = false;
bool log_thread_id = false;
bool log_timestamp = false;
bool log_tickcount = false;

// A log message handler that gets notified of every log message we process.
LogMessageHandlerFunction log_message_handler = NULL;

// Helper functions to wrap platform differences.

int32_t CurrentProcessId() {
	return GetCurrentProcessId();
}

int32_t CurrentThreadId() {
	return GetCurrentThreadId();
}

uint64_t TickCount() {
	return GetTickCount();
}

void CloseFile (HANDLE log) {
	CloseHandle (log);
}

void DeleteFilePath (const std::string& log_name) {
	DeleteFile (log_name.c_str());
}

std::string GetDefaultLogFile() {
// On Windows we use the same path as the exe.
	std::string log_file = "debug.log";
	return log_file;
}

// This class acts as a wrapper for locking the logging files.
// LoggingLock::Init() should be called from the main thread before any logging
// is done. Then whenever logging, be sure to have a local LoggingLock
// instance on the stack. This will ensure that the lock is unlocked upon
// exiting the frame.
// LoggingLocks can not be nested.
class LoggingLock {
 public:
  LoggingLock() {
    LockLogging();
  }

  ~LoggingLock() {
    UnlockLogging();
  }

  static void Init(LogLockingState lock_log, const char* new_log_file) {
    if (is_initialized)
      return;
    lock_log_file = lock_log;
    if (lock_log_file == LOCK_LOG_FILE) {
	    if (!log_mutex) {
		    std::string safe_name;
		    if (new_log_file)
			    safe_name = new_log_file;
		    else
			    safe_name = GetDefaultLogFile();
		    // \ is not a legal character in mutex names so we replace \ with /
		    std::replace(safe_name.begin(), safe_name.end(), '\\', '/');
		    std::string t("Global\\");
		    t.append(safe_name);
		    log_mutex = ::CreateMutex (NULL, FALSE, t.c_str());
		    if (log_mutex == NULL)
			    return;
	    }
    } else {
	log_lock = new chromium::internal::LockImpl();
    }
    is_initialized = true;
  }

 private:
  static void LockLogging() {
      if (lock_log_file == LOCK_LOG_FILE) {
        ::WaitForSingleObject (log_mutex, INFINITE);
      } else {
        // use the lock
        log_lock->Lock();
      }
  }

  static void UnlockLogging() {
      if (lock_log_file == LOCK_LOG_FILE) {
        ReleaseMutex (log_mutex);
      } else {
        log_lock->Unlock();
      }
  }

  // The lock is used if log file locking is false. It helps us avoid problems
  // with multiple threads writing to the log file at the same time.  Use
  // LockImpl directly instead of using Lock, because Lock makes logging calls.
  static chromium::internal::LockImpl* log_lock;

  static HANDLE log_mutex;

  static bool is_initialized;
  static LogLockingState lock_log_file;
};

// static
bool LoggingLock::is_initialized = false;
// static
chromium::internal::LockImpl* LoggingLock::log_lock = NULL;
// static
LogLockingState LoggingLock::lock_log_file = LOCK_LOG_FILE;
// static
HANDLE LoggingLock::log_mutex = NULL;

// Called by logging functions to ensure that debug_file is initialized
// and can be used for writing. Returns false if the file could not be
// initialized. debug_file will be NULL in this case.
bool InitializeLogFileHandle() {
  if (log_file)
    return true;

  if (!log_file_name) {
    // Nobody has called InitLogging to specify a debug log file, so here we
    // initialize the log file name to a default.
    log_file_name = new std::string (GetDefaultLogFile());
  }

  if (logging_destination == LOG_ONLY_TO_FILE ||
      logging_destination == LOG_TO_BOTH_FILE_AND_SYSTEM_DEBUG_LOG) {
    log_file = CreateFile(log_file_name->c_str(), GENERIC_WRITE,
                          FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                          OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
    if (log_file == INVALID_HANDLE_VALUE || log_file == NULL) {
      // try the current directory
      log_file = CreateFile(".\\debug.log", GENERIC_WRITE,
                            FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                            OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
      if (log_file == INVALID_HANDLE_VALUE || log_file == NULL) {
        log_file = NULL;
        return false;
      }
    }
    SetFilePointer(log_file, 0, 0, FILE_END);
  }

  return true;
}

}  /* anonymous namespace */

bool ChromiumInitLoggingImpl(const char* new_log_file,
				LoggingDestination logging_dest,
				LogLockingState lock_log,
				OldFileDeletionState delete_old,
				DcheckState dcheck_state) {
  CommandLine* command_line = CommandLine::ForCurrentProcess();
  g_dcheck_state = dcheck_state;

  // Don't bother initializing g_vlog_info unless we use one of the
  // vlog switches.
  if (command_line->HasSwitch(switches::kV) ||
      command_line->HasSwitch(switches::kVModule)) {
    g_vlog_info =
        new VlogInfo(command_line->GetSwitchValueASCII(switches::kV),
                     command_line->GetSwitchValueASCII(switches::kVModule),
                     &min_log_level);
  }

  LoggingLock::Init(lock_log, new_log_file);

  LoggingLock logging_lock;

  if (log_file) {
    // calling InitLogging twice or after some log call has already opened the
    // default log file will re-initialize to the new options
    CloseFile (log_file);
    log_file = NULL;
  }

  logging_destination = logging_dest;

  // ignore file options if logging is disabled or only to system
  if (logging_destination == LOG_NONE ||
      logging_destination == LOG_ONLY_TO_SYSTEM_DEBUG_LOG)
    return true;

  if (!log_file_name)
    log_file_name = new std::string;
  *log_file_name = new_log_file;
  if (delete_old == DELETE_OLD_LOG_FILE)
    DeleteFilePath (log_file_name->c_str());

  return InitializeLogFileHandle();
}

void SetMinLogLevel(int level) {
  min_log_level = std::min(LOG_ERROR, level);
}

int GetMinLogLevel() {
  return min_log_level;
}

int GetVlogVerbosity() {
  return std::max(-1, LOG_INFO - GetMinLogLevel());
}

int GetVlogLevelHelper(const char*file, size_t N) {
  DCHECK_GT(N, 0U);
    // Note: g_vlog_info may change on a different thread during startup
  // (but will always be valid or NULL).
  VlogInfo* vlog_info = g_vlog_info;
  return vlog_info ?
      vlog_info->GetVlogLevel(chromium::StringPiece(file, N - 1)) :
      GetVlogVerbosity();
}

void SetLogItems(bool enable_process_id, bool enable_thread_id,
                 bool enable_timestamp, bool enable_tickcount) {
  log_process_id = enable_process_id;
  log_thread_id = enable_thread_id;
  log_timestamp = enable_timestamp;
  log_tickcount = enable_tickcount;
}

void SetLogMessageHandler(LogMessageHandlerFunction handler) {
  log_message_handler = handler;
}

LogMessageHandlerFunction GetLogMessageHandler() {
  return log_message_handler;
}

// MSVC doesn't like complex extern templates and DLLs.
#if !defined(_MSC_VER)
// Explicit instantiations for commonly used comparisons.
template std::string* MakeCheckOpString<int, int>(const int&, const int&, const char* names);
template std::string* MakeCheckOpString<unsigned long, unsigned long>(const unsigned long&, const unsigned long&, const char* names);
template std::string* MakeCheckOpString<unsigned long, unsigned int>(const unsigned long&, const unsigned int&, const char* names);
template std::string* MakeCheckOpString<unsigned int, unsigned long>(const unsigned int&, const unsigned long&, const char* names);
template std::string* MakeCheckOpString<std::string, std::string>(const std::string&, const std::string&, const char* name);
#endif

LogMessage::LogMessage(const char* file, int line, LogSeverity severity, int ctr)
    : severity_(severity), file_(file), line_(line) {
  Init(file, line);
}

LogMessage::LogMessage(const char* file, int line)
    : severity_(LOG_INFO), file_(file), line_(line) {
  Init(file, line);
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity)
    : severity_(severity), file_(file), line_(line) {
  Init(file, line);
}

LogMessage::LogMessage(const char* file, int line, std::string* result)
    : severity_(LOG_FATAL), file_(file), line_(line) {
  Init(file, line);
  stream_ << "Check failed: " << *result;
  delete result;
}

LogMessage::LogMessage(const char* file, int line, LogSeverity severity, std::string* result)
    : severity_(severity), file_(file), line_(line) {
  Init(file, line);
  stream_ << "Check failed: " << *result;
  delete result;
}

LogMessage::~LogMessage() {
#ifndef NDEBUG
	if (severity_ == LOG_FATAL) {
		// Include a stack trace on a fatal.
		chromium::debug::StackTrace trace;
		stream_ << std::endl;  // Newline to separate from log message
		trace.OutputToStream(&stream_);
	}
#endif
	stream_ << std::endl;
	std::string str_newline(stream_.str());

// Give any log message handler first dibs on the message.
	if (log_message_handler && log_message_handler(severity_, file_, line_, message_start_, str_newline)) {
// The handler took care of it, no further processing.
		return;
	}

	if (logging_destination == LOG_ONLY_TO_SYSTEM_DEBUG_LOG ||
	    logging_destination == LOG_TO_BOTH_FILE_AND_SYSTEM_DEBUG_LOG) {
	    OutputDebugStringA (str_newline.c_str());
	    fprintf (stderr, "%s", str_newline.c_str());
	    fflush (stderr);
	} else if (severity_ >= kAlwaysPrintErrorLevel) {
// When we're only outputting to a log file, above a certain log level, we
// should still output to stderr so that we can better detect and diagnose
// problems with unit tests, especially on the buildbots.
		fprintf (stderr, "%s", str_newline.c_str());
		fflush (stderr);
	}

	LoggingLock::Init (LOCK_LOG_FILE, NULL);
	if (logging_destination != LOG_NONE &&
	    logging_destination != LOG_ONLY_TO_SYSTEM_DEBUG_LOG) {
		LoggingLock logging_lock;
		if (InitializeLogFileHandle()) {
			SetFilePointer (log_file, 0, 0, SEEK_END);
			DWORD num_written;
			WriteFile (log_file,
				static_cast<const void*>(str_newline.c_str()),
				static_cast<DWORD>(str_newline.length()),
				&num_written,
				NULL);
		}
	}
}

// writes the common header info to the stream
void LogMessage::Init(const char* file, int line) {
  std::string filename(file);
  size_t last_slash_pos = filename.find_last_of("\\/");
  if (last_slash_pos != std::string::npos)
	  filename.erase (0, last_slash_pos + 1);

  stream_ <<  '[';
  if (log_process_id)
    stream_ << CurrentProcessId() << ':';
  if (log_thread_id)
    stream_ << CurrentThreadId() << ':';
  if (log_timestamp) {
    time_t t = time(NULL);
    struct tm local_time = {0};
#if _MSC_VER >= 1400
    localtime_s(&local_time, &t);
#else
    localtime_r(&t, &local_time);
#endif
    struct tm* tm_time = &local_time;
    stream_ << std::setfill('0')
            << std::setw(2) << 1 + tm_time->tm_mon
            << std::setw(2) << tm_time->tm_mday
            << '/'
            << std::setw(2) << tm_time->tm_hour
            << std::setw(2) << tm_time->tm_min
            << std::setw(2) << tm_time->tm_sec
            << ':';
  }
  if (log_tickcount)
    stream_ << TickCount() << ':';
  if (severity_ >= 0)
    stream_ << log_severity_names[severity_];
  else
    stream_ << "VERBOSE" << -severity_;

  stream_ << ":" << filename << "(" << line << ")] ";

  message_start_ = stream_.tellp();
}

}  /* namespace logging */

/* eof */
