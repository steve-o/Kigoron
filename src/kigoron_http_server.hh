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

#include "net/io_buffer.hh"
#include "net/server/http_connection.hh"
#include "net/server/http_server_request_info.hh"
#include "net/server/http_server_response_info.hh"

#ifdef _WIN32           
#	define in_port_t	uint16_t
#	define ssize_t		SSIZE_T
#endif

namespace net
{

class IPEndPoint;
class HttpConnection;

}

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
		void DidRead (const char* data, int length);
		void OnHttpRequest (const net::HttpServerRequestInfo& info);
		bool Finwait();
		void DidClose();
		void SendResponse (const net::HttpServerResponseInfo& response);
		void Send (net::HttpStatusCode status_code, const std::string& data, const std::string& content_type);
		void Send200 (const std::string& data, const std::string& mime_type);
		void Send404();
		void Send500 (const std::string& message);
		bool Write();

		std::string GetIndexPageHTML();

		SOCKET sock_;
		std::string name_;
		std::shared_ptr<net::HttpConnection> connection_;
		HttpState state_;
		std::shared_ptr<net::DrainableIOBuffer> write_buf_;
	};

	class provider_t;

	class httpd_t
	{
	public:
		explicit httpd_t();
		~httpd_t();

		bool Start (in_port_t port);
		void Close();

		std::shared_ptr<http_connection_t> Accept();

		const SOCKET sock() const {
			return listen_sock_;
		}

	private:
		SOCKET CreateAndListen (const std::string& ip, in_port_t port);
		int GetLocalAddress (net::IPEndPoint* address);

		SOCKET listen_sock_;
		std::list<std::shared_ptr<http_connection_t>> connections_;

		friend class provider_t;
		friend class http_connection_t;
// Expects the raw data to be stored in recv_data_. If parsing is successful,
// will remove the data parsed from recv_data_, leaving only the unused
// recv data.
		static bool ParseHeaders (net::HttpConnection* connection, net::HttpServerRequestInfo* info, size_t* pos);

	};

} /* namespace kigoron */

#endif /* HTTPD_HH_ */

/* eof */
