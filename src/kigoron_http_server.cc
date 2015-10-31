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
#include "chromium/strings/string_number_conversions.hh"
#include "chromium/strings/string_util.hh"
#include "chromium/strings/stringprintf.hh"
#include "net/base/ip_endpoint.hh"
#include "net/base/net_util.hh"
#include "net/server/web_socket.hh"

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

kigoron::http_connection_t::http_connection_t (
	net::SocketDescriptor s,
	const std::string& name
	)
	: sock_ (s)
	, name_ (name)
	, state_ (HTTP_STATE_READ)
{
	connection_ = std::make_shared<net::HttpConnection>();
}

kigoron::http_connection_t::~http_connection_t()
{
	DLOG(INFO) << "~http_connection_t";
	Close();
}

void
kigoron::http_connection_t::Close()
{
	if (net::kInvalidSocket != sock_) {
		closesocket (sock_);
		sock_ = net::kInvalidSocket;
	}
}

bool
kigoron::http_connection_t::OnCanReadWithoutBlocking()
{
	switch (state_) {
	case HTTP_STATE_READ:
		return Read();
	case HTTP_STATE_FINWAIT:
		return Finwait();
	default:
		break;
	}
	return true;
}

bool
kigoron::http_connection_t::OnCanWriteWithoutBlocking()
{
	switch (state_) {
	case HTTP_STATE_WRITE:
		return Write();
	default:
		break;
	}
	return true;
}

/* returns true on success, false to abort connection.
 */
bool
kigoron::http_connection_t::Read()
{
	char buf[net::kReadBufSize];
	int len;
	do {
		len = recv (sock_, buf, net::kReadBufSize, 0);
		if (len < 0) {
			const int save_errno = WSAGetLastError();
			if (WSAEINTR == save_errno || WSAEWOULDBLOCK == save_errno)
				return true;
			char errbuf[1024];
			FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
					NULL,            /* source */
       	                		save_errno,      /* message id */
       	                		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
       	                		(LPTSTR)errbuf,
       	                		sizeof (errbuf),
       	                		NULL);           /* arguments */
			LOG(ERROR) << "recv: { "
				  "\"errno\": " << save_errno << ""
				", \"text\": \"" << errbuf << "\""
				" }";
			return false;
		} else {
			DidRead (buf, len);
		}
	} while (len == net::kReadBufSize);
	return true;
}

void
kigoron::http_connection_t::DidRead (
	const char* data,
	int len
	)
{
/* temporary 1:1 mapping */
	net::HttpConnection* connection = connection_.get();
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
//				Close(connection->id());
				Close();
				break;
			}
			LOG(INFO) << "ws:" << connection->id() << ": " << message;
			continue;
		}

		net::HttpServerRequestInfo request;
		size_t pos = 0;
		if (!KigoronHttpServer::ParseHeaders(connection, &request, &pos))
			break;

// Sets peer address if exists.
/*		socket->GetPeerAddress(&request.peer); */

		if (request.HasHeaderValue("connection", "upgrade")) {
			connection->web_socket_.reset(net::WebSocket::CreateWebSocket(connection,
                                                               request,
                                                               &pos));

			if (!connection->web_socket_.get())  // Not enough data was received.
				break;
			LOG(INFO) << "ws:" << connection->id() << ": request";
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
/*				connection->Send(net::HttpServerResponseInfo::CreateFor500(
					"request content-length too big or unknown: " +
					request.GetHeaderValue(kContentLength))); */
//				DidClose(socket);
				DidClose();
				break;
			}

			if (connection->recv_data_.length() - pos < content_length)
				break;  // Not enough data was received yet.
			request.data = connection->recv_data_.substr(pos, content_length);
			pos += content_length;
		}

//		OnHttpRequest(connection->id(), request);
		OnHttpRequest(request);
		connection->Shift(pos);
	}
}

void
kigoron::http_connection_t::DidClose()
{
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

void
kigoron::http_connection_t::OnHttpRequest (
	const net::HttpServerRequestInfo& info
	)
{
	if (info.path == "" || info.path == "/") {
		std::string response = GetIndexPageHTML();
		Send200 (response, "text/html; charset=UTF-8");
		return;
	}

	Send404();
}

void
kigoron::http_connection_t::Send (
	net::HttpStatusCode status_code,
	const std::string& data,
	const std::string& mime_type
	)
{
	net::HttpServerResponseInfo response (status_code);
	response.SetBody (data, mime_type);
	SendResponse (response);
}

void
kigoron::http_connection_t::Send200 (
	const std::string& data,
	const std::string& mime_type
	)
{
	Send (net::HTTP_OK, data, mime_type);
}

void
kigoron::http_connection_t::Send404()
{
	SendResponse (net::HttpServerResponseInfo::CreateFor404());
}

void
kigoron::http_connection_t::Send500 (
	const std::string& message
	)
{
	SendResponse (net::HttpServerResponseInfo::CreateFor500 (message));
}

std::string
kigoron::http_connection_t::GetIndexPageHTML()
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

	ss   << "<table>"
		"<tr>"
			"<th>host name:</th><td>" << http_hostname << "</td>"
		"</tr><tr>"
			"<th>user name:</th><td>" << http_username << "</td>"
		"</tr><tr>"
			"<th>process ID:</th><td>" << http_pid << "</td>"
		"</tr>"
		"</table>\n";
	return ss.str();
}

/* returns true on success, false to abort connection.
 */
bool
kigoron::http_connection_t::Finwait()
{
	char buf[1024];
	const ssize_t bytes_read = recv (sock_, buf, sizeof (buf), 0);
	if (bytes_read < 0) {
		const int save_errno = WSAGetLastError();
		if (WSAEINTR == save_errno || WSAEWOULDBLOCK == save_errno)
			return true;
	}
	return false;
}

void
kigoron::http_connection_t::SendResponse (
	const net::HttpServerResponseInfo& response
	)
{
	const auto& response_string = response.Serialize();
	net::IOBuffer* raw_send_buf = new net::StringIOBuffer (response_string);
	net::DrainableIOBuffer* send_buf = new net::DrainableIOBuffer (raw_send_buf, response_string.size());
	write_buf_.reset (send_buf);
	state_ = HTTP_STATE_WRITE;
}

/* returns true on success, false to abort connection.
 */
bool
kigoron::http_connection_t::Write()
{
	do {
		const ssize_t bytes_written = send (sock_, write_buf_->data(), write_buf_->BytesRemaining(), 0);
		if (bytes_written < 0) {
			const int save_errno = WSAGetLastError();
			if (WSAEINTR == save_errno || WSAEWOULDBLOCK == save_errno)
				return true;
			char errbuf[1024];
			FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
					NULL,            /* source */
       	                		save_errno,      /* message id */
       	                		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
       	                		(LPTSTR)errbuf,
       	                		sizeof (errbuf),
       	                		NULL);           /* arguments */
			LOG(ERROR) << "send: { "
				  "\"errno\": " << save_errno << ""
				", \"text\": \"" << errbuf << "\""
				" }";
			return false;
		} else {
			write_buf_->DidConsume (bytes_written);
		}
	} while (write_buf_->BytesRemaining() > 0);

	if (0 == shutdown (sock_, SHUT_WR)) {
		return false;
	} else {
		DLOG(INFO) << "HTTP socket entering finwait state.";
		state_ = HTTP_STATE_FINWAIT;
		return true;
	}
}

kigoron::KigoronHttpServer::KigoronHttpServer (
	) :
	listen_sock_ (net::kInvalidSocket)
{
}

kigoron::KigoronHttpServer::~KigoronHttpServer()
{
	DLOG(INFO) << "~KigoronHttpServer";
	Close();
/* Summary output */
	VLOG(3) << "Httpd summary: {"
		" }";
}

/* Open HTTP port and listen for incoming connection attempts.
 */
bool
kigoron::KigoronHttpServer::Start (
	in_port_t port
	)
{
	listen_sock_ = CreateAndListen ("::", port);
	if (net::kInvalidSocket == listen_sock_)
		return false;

	net::IPEndPoint address;
	if (0 != GetLocalAddress (&address)) {
		NOTREACHED() << "Cannot start HTTP server";
		return false;
	}

	LOG(INFO) << "Address of HTTP server: " << address.ToString();
	return true;
}

int
kigoron::KigoronHttpServer::GetLocalAddress (
	net::IPEndPoint* address
	)
{
	net::SockaddrStorage storage;
	if (getsockname (listen_sock_, storage.addr, &storage.addr_len)) {
		const int save_errno = WSAGetLastError();
		char errbuf[1024];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		LOG(ERROR) << "getsockname: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			" }";
		return -1;
	}
	if (!address->FromSockAddr (storage.addr, storage.addr_len))
		return -1;

	return 0;
}

net::SocketDescriptor
kigoron::KigoronHttpServer::CreateAndListen (
	const std::string& ip,
	in_port_t port
	)
{
	int rc;
	int backlog = SOMAXCONN;
	sockaddr_in6 addr;
	net::SocketDescriptor sock = net::kInvalidSocket;
	addrinfo hints, *result = nullptr;

/* Resolve ip */
	memset (&hints, 0, sizeof (hints));
	hints.ai_family		= AF_UNSPEC;
	hints.ai_socktype	= SOCK_STREAM;
	hints.ai_protocol	= IPPROTO_TCP;
	hints.ai_flags		= AI_PASSIVE | AI_ADDRCONFIG | AI_V4MAPPED;
	rc = getaddrinfo (ip.c_str(), nullptr, &hints, &result);
	if (0 == rc) {
		memcpy (&addr, result->ai_addr, result->ai_addrlen);
		freeaddrinfo (result);
	} else {
		const int save_errno = WSAGetLastError();
		char errbuf[1024];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		LOG(ERROR) << "getaddrinfo: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			", \"node\": \"" << ip << "\""
			", \"service\": null"
			", \"hints\": { "
				  "\"ai_family\": \"AF_UNSPEC\""
				", \"ai_socktype\": \"SOCK_STREAM\""
				", \"ai_protocol\": 0"
				," \"ai_flags\": \"AI_NUMERICHOST\""
				" }";
			" }";
		return sock;
	}
	
/* Create socket */
	sock = socket (AF_INET6, SOCK_STREAM, 0 /* unspecified */);
	if (net::kInvalidSocket == sock) {
		const int save_errno = WSAGetLastError();
		char errbuf[1024];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		LOG(ERROR) << "socket: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			", \"family\": \"AF_INET6\""
			", \"type\": \"SOCK_STREAM\""
			", \"protocol\": 0"
			" }";
		return sock;
	}

/* IP-any */
	{
		DWORD optval = 0;
		rc = setsockopt (sock, IPPROTO_IPV6, IPV6_V6ONLY, (const char*)&optval, sizeof (optval));
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
			LOG(WARNING) << "setsockopt: { "
				  "\"errno\": " << save_errno << ""
				", \"text\": \"" << errbuf << "\""
				", \"level\": \"IPPROTO_IPV6\""
				", \"optname\": \"IPV6_V6ONLY\""
				", \"optval\": " << optval << ""
				" }";
		}
	}

/* Bind */
	addr.sin6_port = htons (port);
	rc = bind (sock, reinterpret_cast<sockaddr*> (&addr), sizeof (addr));
	if (0 != rc) {
		const int save_errno = WSAGetLastError();
		char errbuf[1024];
		char ip[INET6_ADDRSTRLEN];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		getnameinfo ((struct sockaddr*)&addr, sizeof (addr),
                            ip, sizeof (ip),
                            NULL, 0,
                            NI_NUMERICHOST);
		LOG(ERROR) << "bind: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			", \"family\": \"AF_INET6\""
			", \"addr\": \"" << ip << "\""
			", \"port\": " << port << ""
			" }";
		goto cleanup;
	}

/* Listen */
	rc = listen (sock, backlog);
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
		LOG(ERROR) << "listen: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			", \"backlog\": \"SOMAXCONN\""
			" }";
		goto cleanup;
	}

/* Non-blocking */
	{
		u_long mode = 1;
		rc = ioctlsocket (sock, FIONBIO, &mode);
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
			LOG(ERROR) << "ioctlsocket: { "
				  "\"errno\": " << save_errno << ""
				", \"text\": \"" << errbuf << "\""
				", \"cmd\": \"FIONBIO\""
				", \"nonblockingMode\": \"" << (mode ? "Enabled" : "Disabled") << "\""
				" }";
			goto cleanup;
		}
	}
	return sock;
cleanup:
	closesocket (sock);
	return net::kInvalidSocket;
}

void
kigoron::KigoronHttpServer::Close()
{
	if (net::kInvalidSocket != listen_sock_) {
		closesocket (listen_sock_);
		listen_sock_ = net::kInvalidSocket;
	}
}

std::shared_ptr<kigoron::http_connection_t>
kigoron::KigoronHttpServer::Accept()
{
	net::SocketDescriptor new_sock;
	char name[INET6_ADDRSTRLEN];
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof (addr);
	int rc;

	DLOG(INFO) << "OnConnection";

/* Accept new socket */
	new_sock = accept (listen_sock_, (struct sockaddr*)&addr, &addrlen);
	if (net::kInvalidSocket == new_sock) {
		const int save_errno = WSAGetLastError();
		if (WSAEWOULDBLOCK == save_errno)
			goto return_empty;
		char errbuf[1024];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		LOG(ERROR) << "accept: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			" }";
		goto cleanup;
	}

/* Non-blocking */
	{
		u_long mode = 1;
		rc = ioctlsocket (new_sock, FIONBIO, &mode);
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
			LOG(ERROR) << "ioctlsocket: { "
				  "\"errno\": " << save_errno << ""
				", \"text\": \"" << errbuf << "\""
				", \"cmd\": \"FIONBIO\""
				", \"nonblockingMode\": \"" << (mode ? "Enabled" : "Disabled") << "\""
				" }";
			goto cleanup;
		}
	}

/* Display connection name */
	rc = getnameinfo ((struct sockaddr*)&addr, addrlen,
				name, sizeof (name),
				nullptr, 0,
				NI_NUMERICHOST);
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
		LOG(ERROR) << "getnameinfo: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			" }";
		goto cleanup;
	} else {
		auto connection = std::make_shared<http_connection_t> (new_sock, name);
		if (!(bool)connection) {
			LOG(ERROR) << "Http connection initialization failed, aborting connection.";
			goto cleanup;
		}
		return connection;
	}

cleanup:
	closesocket (new_sock);
return_empty:
	return std::shared_ptr<http_connection_t>();
}

/* eof */
