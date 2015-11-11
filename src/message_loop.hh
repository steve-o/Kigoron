#ifndef CHROMIUM_MESSAGE_LOOP_HH_
#define CHROMIUM_MESSAGE_LOOP_HH_

#include "net/socket/socket_descriptor.hh"

namespace kigoron
{

class provider_t;

}

namespace chromium
{
	class MessageLoopForIO
	{
	public:
// Used with WatchFileDescriptor to asynchronously monitor the I/O readiness
// of a file descriptor.
		class Watcher {
		public:
// Called from MessageLoop::Run when an FD can be read from/written to
// without blocking
			virtual void OnFileCanReadWithoutBlocking(net::SocketDescriptor fd) = 0;
			virtual void OnFileCanWriteWithoutBlocking(net::SocketDescriptor fd) = 0;

		protected:
			virtual ~Watcher() {}
		};

		enum Mode;

// Object returned by WatchFileDescriptor to manage further watching.
		class FileDescriptorWatcher {
		public:
			explicit FileDescriptorWatcher();
			~FileDescriptorWatcher();  // Implicitly calls StopWatchingFileDescriptor.

// Stop watching the FD, always safe to call.  No-op if there's nothing
// to do.
			bool StopWatchingFileDescriptor();

		private:
			friend class kigoron::provider_t;

			typedef std::pair<net::SocketDescriptor, Mode> event;

// Called by MessagePumpLibevent, ownership of |e| is transferred to this
// object.
			void Init(event* e);

// Used by MessagePumpLibevent to take ownership of event_.
			event* ReleaseEvent();

			void set_pump(kigoron::provider_t* pump) { pump_ = pump; }
			kigoron::provider_t* pump() const { return pump_; }

			void set_watcher(Watcher* watcher) { watcher_ = watcher; }

			void OnFileCanReadWithoutBlocking(net::SocketDescriptor fd, kigoron::provider_t* pump);
			void OnFileCanWriteWithoutBlocking(net::SocketDescriptor fd, kigoron::provider_t* pump);

/* pretend fd is a libevent event object */
			event* event_;
			Watcher* watcher_;
			kigoron::provider_t* pump_;
			std::shared_ptr<FileDescriptorWatcher> weak_factory_;
		};

		enum Mode {
			WATCH_READ = 1 << 0,
			WATCH_WRITE = 1 << 1,
			WATCH_READ_WRITE = WATCH_READ | WATCH_WRITE
		};

		virtual bool WatchFileDescriptor (net::SocketDescriptor fd, bool persistent, Mode mode, FileDescriptorWatcher* controller, Watcher* delegate) = 0;
	};

} /* namespace chromium */

#endif /* CHROMIUM_MESSAGE_LOOP_HH_ */

/* eof */
