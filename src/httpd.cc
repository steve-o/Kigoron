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

#include "httpd.hh"

#include "chromium/basictypes.hh"
#include "chromium/logging.hh"

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
	SOCKET s,
	const std::string& name
	)
	: sock_ (s)
	, name_ (name)
	, state_ (HTTP_STATE_READ)
{
}

kigoron::http_connection_t::~http_connection_t()
{
	DLOG(INFO) << "~http_connection_t";
	Close();
}

void
kigoron::http_connection_t::Close()
{
	if (INVALID_SOCKET != sock_) {
		closesocket (sock_);
		sock_ = INVALID_SOCKET;
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
	int length)
{
	request_parser_.ProcessChunk (chromium::StringPiece (data, length));
	if (net::HttpRequestParser::ACCEPTED == request_parser_.ParseRequest())
		OnRequest (request_parser_.GetRequest());
}

void
kigoron::http_connection_t::OnRequest (
	std::shared_ptr<net::HttpRequest> request
	)
{
	if (1 == request->relative_url.length()) {
		return OnIndex (request);
	}

	LOG(WARNING) << "Request not handled. Returning 404: "
			<< request->relative_url;
	auto not_found_response = std::make_shared<net::BasicHttpResponse>();
	not_found_response->set_code (net::HTTP_NOT_FOUND);
	SendResponse (std::move (not_found_response));
}

void
kigoron::http_connection_t::OnIndex (
	std::shared_ptr<net::HttpRequest> request
	)
{
	std::stringstream ss;
	char http_hostname[NI_MAXHOST];
	char http_username[LOGIN_NAME_MAX + 1];
	int http_pid;
	int rc;

	auto index_response = std::make_shared<net::BasicHttpResponse>();

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
	index_response->set_content (ss.str());
	SendResponse (std::move (index_response));
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
	std::shared_ptr<net::HttpResponse> response
	)
{
	const auto response_string = response->ToResponseString();
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

kigoron::httpd_t::httpd_t (
	) :
	listen_sock_ (INVALID_SOCKET)
{
}

kigoron::httpd_t::~httpd_t()
{
	DLOG(INFO) << "~httpd_t";
	Close();
/* Summary output */
	VLOG(3) << "Httpd summary: {"
		" }";
}

/* Open HTTP port and listen for incoming connection attempts.
 */
bool
kigoron::httpd_t::Initialize()
{
	char hostname[NI_MAXHOST];
	int rc;

	listen_sock_ = CreateAndListen ("::", 7580);
	if (INVALID_SOCKET == listen_sock_)
		return false;
/* Resolve hostname for display */
	rc = gethostname (hostname, sizeof (hostname));
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
		return false;
	}
	hostname[NI_MAXHOST - 1] = '\0';
	LOG(INFO) << "Web interface http://" << hostname << ":" << 7580 << "/";
	return true;
}

SOCKET
kigoron::httpd_t::CreateAndListen (
	const std::string& ip,
	in_port_t port
	)
{
	int rc;
	int backlog = SOMAXCONN;
	sockaddr_in6 addr;
	SOCKET sock = INVALID_SOCKET;
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
	if (INVALID_SOCKET == sock) {
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
	return INVALID_SOCKET;
}

void
kigoron::httpd_t::Close()
{
	if (INVALID_SOCKET != listen_sock_) {
		closesocket (listen_sock_);
		listen_sock_ = INVALID_SOCKET;
	}
}

std::shared_ptr<kigoron::http_connection_t>
kigoron::httpd_t::Accept()
{
	SOCKET new_sock;
	char name[INET6_ADDRSTRLEN];
	struct sockaddr_storage addr;
	socklen_t addrlen = sizeof (addr);
	int rc;

	DLOG(INFO) << "OnConnection";

/* Accept new socket */
	new_sock = accept (listen_sock_, (struct sockaddr*)&addr, &addrlen);
	if (INVALID_SOCKET == new_sock) {
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
