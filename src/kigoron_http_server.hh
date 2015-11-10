/* HTTP embedded server.
 */

#ifndef KIGORON_HTTP_SERVER_HH_
#define KIGORON_HTTP_SERVER_HH_

#ifdef _WIN32
#	include <winsock2.h>
#endif

#include <string>
#include <list>
#include <memory>

#include "net/io_buffer.hh"
#include "net/http/http_status_code.hh"
#include "net/server/http_connection.hh"
#include "net/server/http_server_request_info.hh"
#include "net/server/http_server_response_info.hh"
#include "net/socket/socket_descriptor.hh"
#include "net/socket/stream_listen_socket.hh"

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
	class provider_t;

	class KigoronHttpServer
		: public net::StreamListenSocket::Delegate
	{
	public:
		void OnHttpRequest (int connection_id, const net::HttpServerRequestInfo& info);
		void OnWebSocketRequest(int connection_id, const net::HttpServerRequestInfo& info);
		void OnWebSocketMessage(int connection_id, const std::string& data);
		void OnClose(int connection_id);

		explicit KigoronHttpServer (provider_t* message_loop_for_io);
		~KigoronHttpServer();

		void AcceptWebSocket(int connection_id, const net::HttpServerRequestInfo& request);
		void SendOverWebSocket(int connection_id, const std::string& data);
// Sends the provided data directly to the given connection. No validation is
// performed that data constitutes a valid HTTP response. A valid HTTP
// response may be split across multiple calls to SendRaw.
		void SendRaw(int connection_id, const std::string& data);
		void SendResponse(int connection_id, const net::HttpServerResponseInfo& response);
		void Send(int connection_id, net::HttpStatusCode status_code, const std::string& data, const std::string& mime_type);
		void Send200(int connection_id, const std::string& data, const std::string& mime_type);
		void Send404(int connection_id);
		void Send500(int connection_id, const std::string& message);

		void Close(int connection_id);

// Copies the local address to |address|. Returns a network error code.
		int GetLocalAddress (net::IPEndPoint* address);

		bool Start (in_port_t port);
		std::string GetIndexPageHTML();

// ListenSocketDelegate
		virtual void DidAccept (net::StreamListenSocket* server, std::shared_ptr<net::StreamListenSocket> socket) override;
		virtual void DidRead (net::StreamListenSocket* socket, const char* data, int len) override;
		virtual void DidClose (net::StreamListenSocket* socket) override;

	private:
		provider_t* message_loop_for_io_;

		friend class provider_t;
		friend class net::HttpConnection;
// Expects the raw data to be stored in recv_data_. If parsing is successful,
// will remove the data parsed from recv_data_, leaving only the unused
// recv data.
		static bool ParseHeaders (net::HttpConnection* connection, net::HttpServerRequestInfo* info, size_t* pos);

		net::HttpConnection* FindConnection(int connection_id);
		net::HttpConnection* FindConnection(net::StreamListenSocket* socket);

		std::shared_ptr<net::StreamListenSocket> server_;
		typedef std::map<int, net::HttpConnection*> IdToConnectionMap;
		IdToConnectionMap id_to_connection_;
		typedef std::map<net::StreamListenSocket*, net::HttpConnection*> SocketToConnectionMap;
		SocketToConnectionMap socket_to_connection_;
	};

} /* namespace kigoron */

#endif /* KIGORON_HTTP_SERVER_HH_ */

/* eof */
