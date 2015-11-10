/* HTTP embedded server.
 */

#ifdef _WIN32
#	include <winsock2.h>
#	include <Ws2tcpip.h>
#	include <Lmcons.h>
#	include <process.h>
#else
#	include <sys/types.h>
#	include <sys/socket.h>
#endif
#include <cstdint>

#include "kigoron_http_server.hh"

#include "chromium/basictypes.hh"
#include "chromium/logging.hh"
#include "chromium/stl_util.hh"
#include "chromium/strings/string_number_conversions.hh"
#include "chromium/strings/string_util.hh"
#include "chromium/strings/stringprintf.hh"
#include "net/base/ip_endpoint.hh"
#include "net/base/net_errors.hh"
#include "net/base/net_util.hh"
#include "net/server/web_socket.hh"
#include "net/socket/tcp_listen_socket.hh"

#ifdef _WIN32
#	define SHUT_WR		SD_SEND
#	define LOGIN_NAME_MAX	(UNLEN + 1)
#	define getpid		_getpid
#endif

namespace net {

namespace {

const int kReadBufSize = 4096;

}  // namespace

}  // namespace net


kigoron::KigoronHttpServer::KigoronHttpServer (kigoron::provider_t* message_loop_for_io)
	: message_loop_for_io_ (message_loop_for_io)
{
}

/* Open HTTP port and listen for incoming connection attempts.
 */
bool
kigoron::KigoronHttpServer::Start (
	in_port_t port
	)
{
	if ((bool)server_)
		return true;

	net::TCPListenSocketFactory factory (message_loop_for_io_, "::", port);
	server_ = factory.CreateAndListen (this);
	net::IPEndPoint address;

	if (net::OK != GetLocalAddress (&address)) {
		NOTREACHED() << "Cannot start HTTP server";
		return false;
	}

	LOG(INFO) << "Address of HTTP server: " << address.ToString();
	return true;
}

void
kigoron::KigoronHttpServer::OnHttpRequest (
	int connection_id,
	const net::HttpServerRequestInfo& info
	)
{
	if (info.path == "" || info.path == "/") {
		std::string response = GetIndexPageHTML();
		Send200 (connection_id, response, "text/html; charset=UTF-8");
		return;
	}

	Send404 (connection_id);
}

void
kigoron::KigoronHttpServer::OnWebSocketRequest (
	int connection_id,
	const net::HttpServerRequestInfo& info
	)
{
	LOG(INFO) << "ws request: " << connection_id;
	AcceptWebSocket(connection_id, info);
}

#include "upaostream.hh"

void
kigoron::KigoronHttpServer::OnWebSocketMessage (
	int connection_id,
	const std::string& data
	)
{
//	LOG(INFO) << "ws msg: " << connection_id << ": " << data;
	std::stringstream client_hostname, client_ip;
	std::ostringstream clients;
	clients << message_loop_for_io_->clients_.size();
	SendOverWebSocket(connection_id, clients.str());
}

void
kigoron::KigoronHttpServer::OnClose (
	int connection_id
	)
{
	LOG(INFO) << "close: " << connection_id;
}

std::string
kigoron::KigoronHttpServer::GetIndexPageHTML()
{
	std::stringstream ss;
	char http_hostname[NI_MAXHOST];
	char http_username[LOGIN_NAME_MAX + 1];
	int http_pid;
	int rc;

/* hostname */
	rc = gethostname (http_hostname, sizeof (http_hostname));
	if (0 != rc) {
		const int save_errno = WSAGetLastError();
		char errbuf[1024];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		LOG(ERROR) << "gethostname: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			" }";
// fallback value
		strcpy (http_hostname, "");
	} else {
		http_hostname[NI_MAXHOST - 1] = '\0';
	}

/* username */
	wchar_t wusername[UNLEN + 1];
	DWORD nSize = arraysize( wusername );
	if (!GetUserNameW (wusername, &nSize)) {
		const DWORD save_errno = GetLastError();
		char errbuf[1024];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		LOG(ERROR) << "GetUserNameW: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			" }";
// fallback value
		strcpy (http_username, "");
	} else {
		WideCharToMultiByte (CP_UTF8, 0, wusername, nSize + 1, http_username, sizeof (http_username), NULL, NULL);
	}

/* pid */
	http_pid = getpid();

	ss   << "<!DOCTYPE html>"
		"<html>"
		"<head>"
			"<meta charset=\"UTF-8\">"
			"<script type=\"text/javascript\">"
			"(function() {"
				"var sock = new WebSocket(\"ws://\" + window.location.host + \"/ws\");"
				"var id = undefined;"
				"sock.onopen = function() {"
					"id = window.setInterval(function() {"
						"sock.send(\"Ping\");"
					"}, 100);"
				"};"
				"sock.onclose = function() {"
					"if (typeof id === \"number\") {"
						"window.clearInterval(id);"
						"id = undefined;"
					"}"
				"};"
				"sock.onmessage = function(msg) {"
					"document.getElementById(\"clients\").textContent = msg.data;"
				"};"
			"})();"
			"</script>"
		"</head>"
		"<body>"
		"<table>"
		"<tr>"
			"<th>host name:</th><td>" << http_hostname << "</td>"
		"</tr><tr>"
			"<th>user name:</th><td>" << http_username << "</td>"
		"</tr><tr>"
			"<th>process ID:</th><td>" << http_pid << "</td>"
		"</tr><tr>"
			"<th>clients:</th><td id=\"clients\"></td>"
		"</tr>"
		"</table>"
		"</body>"
		"</html>"
		"\n";
	return ss.str();
}

void
kigoron::KigoronHttpServer::AcceptWebSocket (
	int connection_id,
	const net::HttpServerRequestInfo& request
	)
{
	net::HttpConnection* connection = FindConnection(connection_id);
	if (nullptr == connection)
		return;

	DCHECK(connection->web_socket_.get());
	connection->web_socket_->Accept(request);
}

void
kigoron::KigoronHttpServer::SendOverWebSocket (
	int connection_id,
	const std::string& data
	)
{
	net::HttpConnection* connection = FindConnection(connection_id);
	if (nullptr == connection)
		return;
	DCHECK(connection->web_socket_.get());
	connection->web_socket_->Send(data);
}

void
kigoron::KigoronHttpServer::SendRaw (
	int connection_id,
	const std::string& data
	)
{
	net::HttpConnection* connection = FindConnection(connection_id);
	if (nullptr == connection)
		return;
	connection->Send(data);
}

void
kigoron::KigoronHttpServer::SendResponse (
	int connection_id,
	const net::HttpServerResponseInfo& response
	)
{
	net::HttpConnection* connection = FindConnection(connection_id);
	if (nullptr == connection)
		return;
	connection->Send(response);
}

void
kigoron::KigoronHttpServer::Send (
	int connection_id,
	net::HttpStatusCode status_code,
	const std::string& data,
	const std::string& mime_type
	)
{
	net::HttpServerResponseInfo response (status_code);
	response.SetBody (data, mime_type);
	SendResponse (connection_id, response);
}

void
kigoron::KigoronHttpServer::Send200 (
	int connection_id,
	const std::string& data,
	const std::string& mime_type
	)
{
	Send (connection_id, net::HTTP_OK, data, mime_type);
}

void
kigoron::KigoronHttpServer::Send404 (
	int connection_id
	)
{
	SendResponse (connection_id, net::HttpServerResponseInfo::CreateFor404());
}

void
kigoron::KigoronHttpServer::Send500 (
	int connection_id,
	const std::string& message
	)
{
	SendResponse (connection_id, net::HttpServerResponseInfo::CreateFor500 (message));
}

void
kigoron::KigoronHttpServer::Close (
	int connection_id
	)
{
	net::HttpConnection* connection = FindConnection(connection_id);
	if (nullptr == connection)
		return;

// Initiating close from server-side does not lead to the DidClose call.
// Do it manually here.
	DidClose(connection->socket_.get());
}

int
kigoron::KigoronHttpServer::GetLocalAddress (
	net::IPEndPoint* address
	)
{
	if (!server_)
		return net::ERR_SOCKET_NOT_CONNECTED;
	return server_->GetLocalAddress(address);
}

void
kigoron::KigoronHttpServer::DidAccept (
	net::StreamListenSocket* server,
	std::shared_ptr<net::StreamListenSocket> socket
	)
{
	net::HttpConnection* connection = new net::HttpConnection(this, std::move(socket));
	id_to_connection_[connection->id()] = connection;
	socket_to_connection_[connection->socket_.get()] = connection;
}

void
kigoron::KigoronHttpServer::DidRead (
	net::StreamListenSocket* socket,
	const char* data,
	int len
	)
{
	net::HttpConnection* connection = FindConnection(socket);
	DCHECK(nullptr != connection);
	if (nullptr == connection)
		return;

	connection->recv_data_.append(data, len);
	while (connection->recv_data_.length()) {
		if (connection->web_socket_.get()) {
			std::string message;
			net::WebSocket::ParseResult result = connection->web_socket_->Read(&message);
			if (result == net::WebSocket::FRAME_INCOMPLETE)
				break;

			if (result == net::WebSocket::FRAME_CLOSE ||
				result == net::WebSocket::FRAME_ERROR) {
				Close(connection->id());
				break;
			}
			OnWebSocketMessage(connection->id(), message);
			continue;
		}

		net::HttpServerRequestInfo request;
		size_t pos = 0;
		if (!ParseHeaders(connection, &request, &pos))
			break;

// Sets peer address if exists.
		socket->GetPeerAddress(&request.peer);

		if (request.HasHeaderValue("connection", "upgrade")) {
			connection->web_socket_.reset(net::WebSocket::CreateWebSocket(connection,
                                                               request,
                                                               &pos));

			if (!connection->web_socket_.get())  // Not enough data was received.
				break;
			OnWebSocketRequest(connection->id(), request);
			connection->Shift(pos);
			continue;
		}

		const char kContentLength[] = "content-length";
		if (request.headers.count(kContentLength)) {
			size_t content_length = 0;
			const size_t kMaxBodySize = 100 << 20;
			if (!chromium::StringToSizeT(request.GetHeaderValue(kContentLength),
							&content_length) ||
				content_length > kMaxBodySize)
			{
				connection->Send(net::HttpServerResponseInfo::CreateFor500(
					"request content-length too big or unknown: " +
					request.GetHeaderValue(kContentLength)));
				DidClose(socket);
				break;
			}

			if (connection->recv_data_.length() - pos < content_length)
				break;  // Not enough data was received yet.
			request.data = connection->recv_data_.substr(pos, content_length);
			pos += content_length;
		}

		OnHttpRequest(connection->id(), request);
		connection->Shift(pos);
	}
}

void
kigoron::KigoronHttpServer::DidClose (
	net::StreamListenSocket* socket
	)
{
	net::HttpConnection* connection = FindConnection(socket);
	DCHECK(connection != NULL);
	id_to_connection_.erase(connection->id());
	socket_to_connection_.erase(connection->socket_.get());
	delete connection;
}

kigoron::KigoronHttpServer::~KigoronHttpServer()
{
	DLOG(INFO) << "~KigoronHttpServer";
	STLDeleteContainerPairSecondPointers(id_to_connection_.begin(), id_to_connection_.end());
/* Summary output */
	VLOG(3) << "Httpd summary: {"
		" }";
}

//
// HTTP Request Parser
// This HTTP request parser uses a simple state machine to quickly parse
// through the headers.  The parser is not 100% complete, as it is designed
// for use in this simple test driver.
//
// Known issues:
//   - does not handle whitespace on first HTTP line correctly.  Expects
//     a single space between the method/url and url/protocol.

// Input character types.
enum header_parse_inputs {
  INPUT_LWS,
  INPUT_CR,
  INPUT_LF,
  INPUT_COLON,
  INPUT_DEFAULT,
  MAX_INPUTS,
};

// Parser states.
enum header_parse_states {
  ST_METHOD,     // Receiving the method
  ST_URL,        // Receiving the URL
  ST_PROTO,      // Receiving the protocol
  ST_HEADER,     // Starting a Request Header
  ST_NAME,       // Receiving a request header name
  ST_SEPARATOR,  // Receiving the separator between header name and value
  ST_VALUE,      // Receiving a request header value
  ST_DONE,       // Parsing is complete and successful
  ST_ERR,        // Parsing encountered invalid syntax.
  MAX_STATES
};

// State transition table
const int parser_state[MAX_STATES][MAX_INPUTS] = {
/* METHOD    */ { ST_URL,       ST_ERR,     ST_ERR,   ST_ERR,       ST_METHOD },
/* URL       */ { ST_PROTO,     ST_ERR,     ST_ERR,   ST_URL,       ST_URL },
/* PROTOCOL  */ { ST_ERR,       ST_HEADER,  ST_NAME,  ST_ERR,       ST_PROTO },
/* HEADER    */ { ST_ERR,       ST_ERR,     ST_NAME,  ST_ERR,       ST_ERR },
/* NAME      */ { ST_SEPARATOR, ST_DONE,    ST_ERR,   ST_VALUE,     ST_NAME },
/* SEPARATOR */ { ST_SEPARATOR, ST_ERR,     ST_ERR,   ST_VALUE,     ST_ERR },
/* VALUE     */ { ST_VALUE,     ST_HEADER,  ST_NAME,  ST_VALUE,     ST_VALUE },
/* DONE      */ { ST_DONE,      ST_DONE,    ST_DONE,  ST_DONE,      ST_DONE },
/* ERR       */ { ST_ERR,       ST_ERR,     ST_ERR,   ST_ERR,       ST_ERR }
};

// Convert an input character to the parser's input token.
int charToInput(char ch) {
  switch(ch) {
    case ' ':
    case '\t':
      return INPUT_LWS;
    case '\r':
      return INPUT_CR;
    case '\n':
      return INPUT_LF;
    case ':':
      return INPUT_COLON;
  }
  return INPUT_DEFAULT;
}

bool
kigoron::KigoronHttpServer::ParseHeaders(net::HttpConnection* connection,
                              net::HttpServerRequestInfo* info,
                              size_t* ppos) {
  size_t& pos = *ppos;
  size_t data_len = connection->recv_data_.length();
  int state = ST_METHOD;
  std::string buffer;
  std::string header_name;
  std::string header_value;
  while (pos < data_len) {
    char ch = connection->recv_data_[pos++];
    int input = charToInput(ch);
    int next_state = parser_state[state][input];

    bool transition = (next_state != state);
    net::HttpServerRequestInfo::HeadersMap::iterator it;
    if (transition) {
      // Do any actions based on state transitions.
      switch (state) {
        case ST_METHOD:
          info->method = buffer;
          buffer.clear();
          break;
        case ST_URL:
          info->path = buffer;
          buffer.clear();
          break;
        case ST_PROTO:
          // TODO(mbelshe): Deal better with parsing protocol.
          DCHECK(buffer == "HTTP/1.1");
          buffer.clear();
          break;
        case ST_NAME:
          header_name = chromium::StringToLowerASCII(buffer);
          buffer.clear();
          break;
        case ST_VALUE:
          chromium::TrimWhitespaceASCII(buffer, chromium::TRIM_LEADING, &header_value);
          it = info->headers.find(header_name);
          // See last paragraph ("Multiple message-header fields...")
          // of www.w3.org/Protocols/rfc2616/rfc2616-sec4.html#sec4.2
          if (it == info->headers.end()) {
            info->headers[header_name] = header_value;
          } else {
            it->second.append(",");
            it->second.append(header_value);
          }
          buffer.clear();
          break;
        case ST_SEPARATOR:
          break;
      }
      state = next_state;
    } else {
      // Do any actions based on current state
      switch (state) {
        case ST_METHOD:
        case ST_URL:
        case ST_PROTO:
        case ST_VALUE:
        case ST_NAME:
          buffer.append(&ch, 1);
          break;
        case ST_DONE:
          DCHECK(input == INPUT_LF);
          return true;
        case ST_ERR:
          return false;
      }
    }
  }
  // No more characters, but we haven't finished parsing yet.
  return false;
}

net::HttpConnection*
kigoron::KigoronHttpServer::FindConnection (
	int connection_id
	)
{
	IdToConnectionMap::iterator it = id_to_connection_.find(connection_id);
	if (it == id_to_connection_.end())
		return NULL;
	return it->second;
}

net::HttpConnection*
kigoron::KigoronHttpServer::FindConnection(net::StreamListenSocket* socket) {
	SocketToConnectionMap::iterator it = socket_to_connection_.find(socket);
	if (it == socket_to_connection_.end())
		return NULL;
	return it->second;
}

/* eof */
