/* HTTP embedded server.
 */

#ifndef HTTPD_HH_
#define HTTPD_HH_

#ifdef _WIN32
#	include <winsock2.h>
#endif

#include <string>
#include <list>
#include <memory>

#ifdef _WIN32           
#	define in_port_t	uint16_t
#	define ssize_t		SSIZE_T
#endif

namespace kigoron
{
	enum HttpState { HTTP_STATE_READ,
                          HTTP_STATE_WRITE,
                          HTTP_STATE_FINWAIT };

	class http_connection_t
	{
	public:
		explicit http_connection_t (SOCKET s, const std::string& name);
		~http_connection_t();

		void Close();
		bool OnCanReadWithoutBlocking();
		bool OnCanWriteWithoutBlocking();

		const SOCKET sock() const {
			return sock_;
		}
		const std::string& name() const {
			return name_;
		}

	private:
		bool Read();
		bool Finwait();
		bool Write();

		SOCKET sock_;
		std::string name_;
		HttpState state_;
		char* buf_;
		size_t buflen_;
		size_t bufoff_;
	};

	class provider_t;

	class httpd_t
	{
	public:
		explicit httpd_t();
		~httpd_t();

		bool Initialize();
		void Close();

		std::shared_ptr<http_connection_t> Accept();

		const SOCKET sock() const {
			return listen_sock_;
		}

	private:
		SOCKET CreateAndListen (const std::string& ip, in_port_t port);

		SOCKET listen_sock_;
		std::list<std::shared_ptr<http_connection_t>> connections_;

		friend provider_t;
	};

} /* namespace kigoron */

#endif /* HTTPD_HH_ */

/* eof */
