/* multiconnect.h
 */

#ifndef _EASYV6_H
#define _EASYV6_H

#include <sys/types.h> /* addrinfo */
#include <sys/socket.h> /* addrinfo */
#include <netdb.h> /* addrinfo */

struct CONNECTBYNAMERESULT {
  const struct addrinfo *address; /* addrinfo component */
  int error;                      /* errno from connect() to this address */
};

struct CONNECTBYNAMEDETAILS {
  const struct addrinfo *addresslist;
  struct CONNECTBYNAMERESULT results[1];
};

struct CONNECTOPTIONS {
  const struct addrinfo *like; /* Try to connect to addresses in this order
                                * first. If I succeeded with these in the
                                * past, this will likely speed future
                                * attempts. */
  const struct addrinfo *skip; /* Do not attempt to connect to addresses in
                                * this list even if they are returned by the
                                * name lookup. If I want to retry all possible
                                * addresses after a connection failure above
                                * the TCP layer (e.g. SMTP banner timed out)
                                * this will exclude those addresses from
                                * future attempts. Just repeat the connect
                                * excluding all prior addresses until the
                                * connect itself fails. */
  struct addrinfo *picked;     /* Fill in with the address I actually
                                * connected to if reportpicked is non-zero */
  struct CONNECTBYNAMEDETAILS *details;
  int getaddrinfoerror;        /* error from getaddrinfo() */
  int numaddresses;            /* Number of candidate addresses for the name */
  char dnspinning;             /* Attempt connections only to liked addresses
                                */
  char reportpicked;           /* Supply an addrinfo in picked */
  char reportdetails;          /* Fill in the details structure if non-zero */
};

/* Note: to free *details: 
 * freeaddrinfo(details->addresslist);
 * free(details);
 * To free picked:
 * freeaddrinfo(picked);
 */

char *addrinfototext (
/* Return the IP address of the first addrinfo structure as a text
 * string */
  const struct addrinfo *address
, char *buf
, size_t bytes
);

char *getpeernametext (
/* Return the IP address of the remote end of the connected socket.
 * Return the service name (normally a numeric port) of the remote socket
 * in *buf.
 * If buf is NULL, malloc memory for the IP address (caller must free)
 * On failure, set errno and return NULL.
 */
  int socket
, char *buf
, size_t bytes
);


int timeoutgetaddrinfo (
/* getaddrinfo with *any* Internet address family and protocol of type
 * socktype. Abort if an answer cannot be found within timeout milliseconds. 
 * Return value: 0 on success. On failure, any of the errors from
 * getaddrinfo(3) with the caveat that EAI_AGAIN could also mean timed out
 */
  const char *node,     /* hostname, e.g. www.whitehouse.gov */
  const char *service,  /* service name, e.g. "http" or "80" */
  const struct addrinfo *hints,
  struct addrinfo **res,  /* output: result */
  long long *timeout /* milliseconds */
);

int connectbyaddrinfo (
/* given an addrinfo chain from getaddrinfo, connect a stream to any one
 * of the available addresses. Abort if not successful within timeout
 * seconds. Returns the connected socket number or returns -1 and sets 
 * errno. 
 * If no options are required, pass NULL.
 * connectbyaddrinfo parallelizes connection attempts to each address, 
 * starting each connection attempt after a brief wait for a prior one
 * to complete and then waiting until any one completes or they all fail.
 * The wait before trying the next address scales based on the timeout but
 * is not more than 1 second and not not less than 100ms.
 * Return value: connected socket or -1 and set errno.
 * errno is set to the highest numbered return from connect()
 */
  const struct addrinfo *addresses /* *res from timeoutgetaddrinfo() */
, long long timeout /* milliseconds */
, struct CONNECTOPTIONS *options
);

int connectbyname (
/* Connect a SOCK_STREAM via any Internet protocol to name:service.
 * Abort if not successful within timeout seconds. 
 * If the name lookup is successful within the timeout, connectbyname will
 * give itself at least 1 second to attempt a connection regardless of the
 * timeout setting.
 * If no options are required, pass NULL.
 * connectbyname parallelizes connection attempts to each address, 
 * starting each connection attempt after a brief wait for a prior one
 * to complete and then waiting until any one completes or they all fail.
 * The wait before trying the next address scales based on the timeout but
 * is not more than 1 second and not not less than 100ms.
 * Return value: connected socket or -1 and set errno.
 * errno is set to the highest numbered return from connect() or to
 * EFAULT if the address lookup failed/timed out
 */
  const char *name
, const char *service
, long long timeout /* milliseconds */
, struct CONNECTOPTIONS *options
);

int listenbyname (
/* Open listener sockets for all address families supporting *service
 * and return them as a -1 terminated array. Will listen on the wildcard
 * address for all of the protocol famlies.
 */
  const char *service
, int socktype  /* SOCK_STREAM or SOCK_SEQPACKET */
, int backlog
);

#endif
