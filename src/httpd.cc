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
#	define in_port_t	uint16_t
#endif

kigoron::httpd_t::httpd_t (
	) :
	http_sock_ (INVALID_SOCKET)
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
	int rc;
	int backlog = SOMAXCONN;
	struct sockaddr_in6 http_addr;
	in_port_t http_port = 7580;
	char hostname[NI_MAXHOST];

/* Create socket */
	http_sock_ = socket (AF_INET6, SOCK_STREAM, 0 /* unspecified */);
	if (INVALID_SOCKET == http_sock_) {
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
		return false;
	}

/* Bind */
	memset (&http_addr, 0, sizeof (http_addr));
	http_addr.sin6_family = AF_INET6;
	http_addr.sin6_addr = in6addr_any;
	http_addr.sin6_port = htons (http_port);
	rc = bind (http_sock_, (struct sockaddr*)&http_addr, sizeof(http_addr));
	if (0 != rc) {
		const int save_errno = WSAGetLastError();
		char errbuf[1024];
		char addr[INET6_ADDRSTRLEN];
		FormatMessage (FORMAT_MESSAGE_FROM_SYSTEM,
				NULL,            /* source */
                       		save_errno,      /* message id */
                       		MAKELANGID (LANG_NEUTRAL, SUBLANG_DEFAULT),      /* language id */
                       		(LPTSTR)errbuf,
                       		sizeof (errbuf),
                       		NULL);           /* arguments */
		getnameinfo ((struct sockaddr*)&http_addr, sizeof (http_addr),
                            addr, sizeof (addr),
                            NULL, 0,
                            NI_NUMERICHOST);
		LOG(ERROR) << "bind: { "
			  "\"errno\": " << save_errno << ""
			", \"text\": \"" << errbuf << "\""
			", \"family\": \"AF_INET6\""
			", \"addr\": \"" << addr << "\""
			", \"port\": " << http_port << ""
			" }";
		goto cleanup;
	}

/* Listen */
	rc = listen (http_sock_, backlog);
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
		ioctlsocket (http_sock_, FIONBIO, &mode);
	}

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
		goto cleanup;
	}
	hostname[NI_MAXHOST - 1] = '\0';

	LOG(INFO) << "Web interface http://" << hostname << ":" << http_port << "/";

	return true;

cleanup:
	closesocket (http_sock_);
	http_sock_ = INVALID_SOCKET;
	return false;
}

void
kigoron::httpd_t::Close()
{
	if (INVALID_SOCKET != http_sock_) {
		closesocket (http_sock_);
		http_sock_ = INVALID_SOCKET;
	}
}

/* eof */
