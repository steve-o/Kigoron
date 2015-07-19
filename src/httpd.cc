/* HTTP embedded server.
 */

#ifdef _WIN32
#	include <winsock2.h>
#	include <Ws2tcpip.h>
#else
#	include <sys/types.h>
#	include <sys/socket.h>
#endif
#include <cstdint>

#include "httpd.hh"

#include "chromium/logging.hh"

#ifdef _WIN32
#	define SHUT_WR		SD_SEND
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
	, buf_ (nullptr)
	, buflen_ (0)
	, bufoff_ (0)
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
	if (buflen_ > 0) {
		free (buf_);
		buf_ = nullptr;
		buflen_ = 0;
		bufoff_ = 0;
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
	char buf[net::kReadBufSize + 1];	// +1 for null termination.
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
/* HTTP/1.1 ...
 * Content-Type: text/html; charset=UTF-8
 * Cache-Control: no-cache, no-store, max-age=0, must-revalidate
 * Pragma: no-cache
 * Expires: Fri, 01 Jan 1990 00:00:00 GMT
 * Date: ...
 * X-Content-Type-Options: nosniff
 * X-Frame-Options: SAMEORIGIN
 * X-XSS-Protection: 1; mode=block
 * Content-Length: ...
 * Server: moo
 */

/* Cache-Control:public, max-age=31033761
 * Connection:keep-alive
 */

	std::stringstream str;
	str <<  "HTTP/1.0 200 OK\r\n"
		"Content-Length: 2\r\n"
		"Content-Type: text/html\r\n"
		"Connection: close\r\n"
		"\r\n"
		"hi";
	if (buflen_ > 0) free (buf_);
	buflen_ = str.str().size();
	buf_ = strdup (str.str().c_str());

	state_ = HTTP_STATE_WRITE;
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

/* returns true on success, false to abort connection.
 */
bool
kigoron::http_connection_t::Write()
{
	do {
		const ssize_t bytes_written = send (sock_, buf_ + bufoff_, buflen_ - bufoff_, 0);
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
		}
		bufoff_ += bytes_written;
	} while (bufoff_ < buflen_);

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
