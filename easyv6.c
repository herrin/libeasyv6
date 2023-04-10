/* easyv6.c - connect via IPv6 or IPv4 with a timeout
 */

#define _GNU_SOURCE         /* getaddrinfo_a, gai_suspend */
#include <netdb.h>	/* getaddrinfo_a, gai_suspend */

#include "easyv6.h"

#include <errno.h>
#include <time.h>
#include <stdlib.h> /* malloc */
#include <string.h> /* memset */
#include <unistd.h> /* fcntl */
#include <fcntl.h> /* fcntl */
#include <sys/time.h> /* select */
#include <sys/select.h> /* select */
#include <stdio.h> /* printf */

#include <arpa/inet.h> /* inet_ntop */
#include <pthread.h> /* mutexes */

#include <sys/types.h>      /* setsockopt */
#include <sys/socket.h>     /* setsockopt */


/*
           struct addrinfo {
               int              ai_flags;
               int              ai_family;
               int              ai_socktype;
               int              ai_protocol;
               size_t           ai_addrlen;
               struct sockaddr *ai_addr;
               char            *ai_canonname;
               struct addrinfo *ai_next;
           };
           struct timeval {
               long    tv_sec;
               long    tv_usec;        microseconds
           };
*/

struct SOCKETINPROGRESS {
  int socket;
  const struct addrinfo *address;
  int error;
};

struct CONNECTIONPROGRESS {
  int topsocket; /* numerically largest socket number recorded in sockets */
  int nextsocket; /* index of the next socket to attempt a connect to */
  int totaladdresses;
  long long finishby;
  long long nextconnectafter;
  long long firstwait; /* how long to wait after starting the first connection
                     * before starting the second */
  long long nextwait;  /* how long to wait after starting later connections
	             * before starting next one */
  struct CONNECTBYNAMEDETAILS *details;
  fd_set *writefds;
  size_t fdsetbytes;
  const struct addrinfo *addresslist;
  struct SOCKETINPROGRESS sockets[1];
};

char *getpeernametext (
/* Return the IP address of the remote end of the connected socket.
 * Return the service name (normally a numeric port) of the remote socket
 * in *buf if buf is not NULL.
 * If buf is NULL, malloc memory for the IP address (caller must free)
 */
  int socket
, char *buf
, size_t bytes
) {
  char sockaddrbuf[200];
  socklen_t addrlen = 200, i;
  struct sockaddr_in *si4 = (struct sockaddr_in *) sockaddrbuf;
  struct sockaddr_in6 *si6 = (struct sockaddr_in6 *) sockaddrbuf;

  if (getpeername(socket,(struct sockaddr *) &sockaddrbuf, &addrlen)) 
    return NULL; /* errno contains error */
  if (addrlen>200) { /*unexpectedly large socket? Can't be one I know about.*/
    errno=ENOBUFS;
    return NULL;
  }
  if ((si4->sin_family!=AF_INET)&&(si6->sin6_family!=AF_INET6)) {
    errno = ENOTSOCK; /* not a socket we know about */
    return NULL;
  }
  if (!buf) {
    buf=(char*) malloc (50);
    if (!buf) return NULL; /* errno=ENOMEM */
    bytes=50;
  } else { /* service name only if *buf provided to us */
    i=snprintf (buf,bytes,"%d",(int) ntohs((si4->sin_family==AF_INET)?
	si4->sin_port:si6->sin6_port));
    if (i>(bytes-20)) { /* Buffer too small */
      errno = ENOMEM;
      return NULL;
    }
    bytes -= i+1;
    buf += i+1;
  }

  inet_ntop (si4->sin_family, (si4->sin_family==AF_INET)?
        (void*)&(si4->sin_addr):(void*)&(si6->sin6_addr),
        buf,bytes);
  if (memcmp(buf,"::ffff:",7)==0) { /* IPv4 address. Use IPv4 semantics. */
    for (i=7; (buf[i]!=0)&&(buf[i]!='.'); i++) ;
    if (buf[i]=='.') {
      char *p=buf, *q=buf+7;
      for (; *q; p++,q++) *p=*q;
      *p=0;
    }
  }

  return buf;
}

char *addrinfototext (
/* Return the IP address of the first addrinfo structure as a text
 * string */
  const struct addrinfo *address
, char *s
, size_t bytes
) {
  struct sockaddr_in *si4;
  struct sockaddr_in6 *si6;

  if (!address) return NULL ;
  si4 = (struct sockaddr_in*) address->ai_addr;
  si6 = (struct sockaddr_in6*) address->ai_addr;
  if (!s) {
    s=(char*) malloc (50);
    if (!s) return NULL;
    bytes=50;
  }
  s[0]=0;
  if ((address->ai_family==AF_INET)||(address->ai_family==AF_INET6)) {
    inet_ntop (address->ai_family, (address->ai_family==AF_INET)?
        (void*)&(si4->sin_addr):(void*)&(si6->sin6_addr),
        s,bytes);
  } else {
    snprintf (s,bytes,"unknown_family_%d",address->ai_family);
  }
  return s;
}

void printaddrinfo (
  const struct addrinfo *addresses
, int onlyone
) {
  char s[200];
  int i = 0;

  while (addresses) {
    s[0]=0;
    if (addrinfototext(addresses,s,200)) {
      fprintf (stdout,"Address %d (%d): %s\n",i,addresses->ai_family,s);
    } else {
      fprintf (stdout,"Address %d (%d): unknown\n",i,addresses->ai_family);
    }
    fflush (stdout);
    i++;
    if (onlyone) return;
    addresses=addresses->ai_next;
  }
  return;
}


long long milliseconds (void) {
  struct timespec now;
  long long dnow;

  if (clock_gettime(CLOCK_REALTIME,&now)) return -1;
  dnow = ((long long) now.tv_nsec)/ 1000000LL;
  dnow += ((long long) now.tv_sec) * 1000LL;

  return dnow;
}


int getsocketerrno (int sock) {
/* per http://cr.yp.to/docs/connect.html 
 *
 * Situation: You set up a non-blocking socket and do a connect() that returns
 * -1/EINPROGRESS or -1/EWOULDBLOCK. You select() the socket for writability.
 * This returns as soon as the connection succeeds or fails.
 *
 * Question: What do you do after select() returns writability? Did the
 * connection fail? If so, how did it fail?
 *
 * If the connection failed, the reason is hidden away inside something called
 * so_error in the socket. Modern systems let you see so_error with
 * getsockopt(,,SO_ERROR,,), but this isn't portable---in fact, getsockopt()
 * can crash old systems. A different way to see so_error is through error
 * slippage: any attempt to read or write data on the socket will return
 * -1/so_error. 
 *
 * If the socket is connected, getpeername() will return 0. If the socket is
 * not connected, getpeername() will return ENOTCONN, and read(fd,&ch,1) will
 * produce the right errno through error slippage. This is a combination of
 * suggestions from Douglas C. Schmidt and Ken Keys. 
 */
  socklen_t addrlen = 100;
  char p[100];
  int r;

  r = getpeername (sock,(struct sockaddr*) p,&addrlen);
  if (!r) return 0;
  if (errno!=ENOTCONN) return errno;
  read (sock,p,1);
  return errno; 
}

int compareaddrinfo (const struct addrinfo *a, const struct addrinfo *b) {
/* Are these two addrinfo entries the same? 1=yes, 0=no*/
  if (!a || !b) return 0;
  if (a->ai_flags!=b->ai_flags) return 0;
  if (a->ai_family!=b->ai_family) return 0;
  if (a->ai_socktype!=b->ai_socktype) return 0;
  if (a->ai_protocol!=b->ai_protocol) return 0;
  if (a->ai_addrlen!=b->ai_addrlen) return 0;
  if (a->ai_addr && !b->ai_addr) return 0;
  if (!a->ai_addr && b->ai_addr) return 0;
  if (!a->ai_addr) return 1;
  if (memcmp(a->ai_addr,b->ai_addr,a->ai_addrlen)) return 0;
  return 1;
}

struct CONNECTIONPROGRESS * allocconnectionstruct (
  const struct addrinfo *addresses /* candidate addresses */
, const struct addrinfo *like /* try these addresses first if present in
                               * addresses */
, const struct addrinfo *skip /* don't try these addresses even if present in
                               * addresses */
, long long timeout
, char reportdetails
, char dnspinning
) {
/* Initialize the data structure for making my parallelize connects */
/* Order by liked removing skip is not implemented. */
/* Order by alternating protocol (v4/v6) is not implemented. */
  struct CONNECTIONPROGRESS *c;
  const struct addrinfo *a;
  const struct addrinfo **candidates;
  int numaddresses,slot,i;
  size_t bytes;
  long long firstwait;

  for (numaddresses=0, a=addresses; a!=NULL; a=a->ai_next) numaddresses++;
  if (numaddresses<1) return NULL;
  bytes = sizeof(struct CONNECTIONPROGRESS) + 
          (sizeof(struct SOCKETINPROGRESS)*numaddresses);
  c = (struct CONNECTIONPROGRESS*) malloc (bytes);
  if (!c) return NULL; /* critical failure */
  candidates = malloc(sizeof(struct addrinfo *)*numaddresses);
  if (!candidates) {
    free (c);
    return NULL; /* critical failure */
  }
  for (i=0, a=addresses; a!=NULL; a=a->ai_next,i++) {
    candidates[i]=a;
  }
  memset ((void*) c, 0, bytes);
  c->topsocket = -1;
  c->addresslist = addresses;
  while (skip) { /* Do not attempt to connect to these addresses */
    for (i=0; i<numaddresses; i++) {
      if (compareaddrinfo (candidates[i],skip)) candidates[i]=NULL;
    }
    skip = skip->ai_next;
  }
  slot=0;
  while (like) {
    for (i=0; i<numaddresses; i++) {
      if (compareaddrinfo (candidates[i],like)) {
        c->sockets[slot].address = candidates[i];
        c->sockets[slot].socket = -1;
        slot++;
        candidates[i]=NULL;
      }
    }
    like = like->ai_next;
  }
  for (i=0; (i<numaddresses) && (!dnspinning); i++) {
    if (candidates[i]) {
      c->sockets[slot].address = candidates[i];
      c->sockets[slot].socket = -1;
      slot++;
    }
  }
  free (candidates);
  c->totaladdresses = slot;
  if (slot<1) {
    free(c);
    return NULL;
  }
  if (reportdetails) { /* will tell the caller all about the addresses
                        * we tried and the particular error on each */
    c->details = (struct CONNECTBYNAMEDETAILS*) malloc (
	sizeof(struct CONNECTBYNAMEDETAILS)+
	(sizeof(struct CONNECTBYNAMERESULT)*c->totaladdresses));
    if (!c->details) {
      free(c);
      return NULL;
    }
    c->details->addresslist = addresses;
  }

  /* Set up the time outs */  
  c->finishby = milliseconds()+timeout;
  firstwait = timeout / ((long long)numaddresses);
  if (firstwait > 1000LL) firstwait = 1000LL; /* 1 second */
  if (firstwait < 100LL) firstwait = 100LL;   /* 0.1 seconds */
  c->firstwait = firstwait;
  c->nextwait = firstwait / 2LL;

  /* fprintf (stdout,"made connect struct at %f. finishby=%f, firstwait=%f, "
	"nextwait=%f, addresses=%d\n", milliseconds(), c->finishby,
	c->firstwait, c->nextwait, c->totaladdresses); */
  return c;
}

#define NEXTCONNECT_NOMORE	-1
#define NEXTCONNECT_STARTED	-2

int nextconnect (struct CONNECTIONPROGRESS *c) {
/* Start a non-blocking connect to the next address in the list */
  int s, fcntlflags;
  const struct addrinfo *ap;
  /* char buf[200]; */

  if (c->nextsocket >= c->totaladdresses) return NEXTCONNECT_NOMORE;
    /* in progress to all possible addresses */
  ap = c->sockets[c->nextsocket].address;
  /*fprintf (stdout,"Enter nextconnect: %d, %lld, %s\n",
    c->nextsocket,milliseconds(),addrinfototext(ap,buf,200)); */
  s = socket(ap->ai_family, ap->ai_socktype, ap->ai_protocol);
  if (s<0) {
    /* Can't seem to get a socket of that type! Move on to the next one. */
    c->sockets[c->nextsocket].error = errno;
    c->nextsocket ++;
    /* fprintf (stdout,"nextconnect fail 1\n"); */
    return nextconnect (c);
  }
  fcntlflags = fcntl(s,F_GETFL,0);
  if (fcntlflags<0) {
    /* Something weird wrong with the socket... Move on. */
    c->sockets[c->nextsocket].error = errno;
    c->nextsocket ++;
    /* fprintf (stdout,"nextconnect fail 2\n"); */
    return nextconnect (c);
  }
  /* put the socket in non-blocking mode */
  if (fcntl(s,F_SETFL,fcntlflags|O_NONBLOCK)<0) {
    /* Something weird wrong with the socket... Move on. */
    c->sockets[c->nextsocket].error = errno;
    c->nextsocket ++;
    /* fprintf (stdout,"nextconnect fail 3\n"); */
    return nextconnect (c);
  }
  c->sockets[c->nextsocket].socket = s;
  if (c->topsocket<s) c->topsocket = s;
  /* fprintf (stdout,"nextconnect have socket %d\n",s);
     printaddrinfo (ap,1); */
  if (connect(s,ap->ai_addr,ap->ai_addrlen)==0) {
    /* Got an immediate connect. */
    /* This really shouldn't happen, but just in case it does... */
    /* fprintf (stdout,"nextconnect connected\n"); */
    return c->nextsocket;
  }
  if (errno == EINPROGRESS) {
    /* started the connection attempt. */
    c->nextsocket ++;
    /* fprintf (stdout,"nextconnect done: started nonblocking\n"); */
    return NEXTCONNECT_STARTED;
  }
  /* unexpected error, cancel the socket and try the next one */
  c->sockets[c->nextsocket].error = errno;
  /* fprintf (stdout,"nextconnect fail 4: %d,%s\n",errno,strerror(errno)); */
  shutdown (s, SHUT_RDWR);
  close (s);
  c->sockets[c->nextsocket].socket = -1;
  c->nextsocket ++;
  return nextconnect (c);
}

int connectdonetrying (
  struct CONNECTIONPROGRESS *c
, int sock /* connected socket or negative number */
, int error /* error to assign to pending connect()s other than sock */
) {
  int i, sockindex=-1;

  /* fprintf (stdout,"connectdonetrying enter\n"); */
  if (sock<0) sock=-2;
  for (i=0; i<c->totaladdresses; i++) {
    if (c->sockets[i].socket == sock) {
      sockindex = i;
      continue;
    }
    if (c->sockets[i].socket>=0) {
      shutdown (c->sockets[i].socket,SHUT_RDWR);
      close (c->sockets[i].socket);
      c->sockets[i].socket=-1;
    }
    if (c->sockets[i].error == 0)
      c->sockets[i].error = error;
    if (c->details) {
      c->details->results[i].address = c->sockets[i].address;
      c->details->results[i].error = c->sockets[i].error;
    }
  }
  if (c->writefds) {
    /* fprintf (stdout,"connectdonetrying free writefds\n"); */
    free (c->writefds);
    c->writefds = NULL;
    c->fdsetbytes = 0;
  }
  return sockindex;
}

fd_set *fdsetalloc (fd_set **set, size_t *bytes, int topsocket) {
  size_t fdsbytes;

  if (topsocket<0) return *set;
  fdsbytes = (topsocket/8) + 2;
  if (fdsbytes<sizeof(fd_set)) fdsbytes=sizeof(fd_set);
  if (*bytes>=fdsbytes) { /* block is reusable */
    memset (*set,0,*bytes);
    return *set;
  }
  if (*set==NULL) { /* fetch or enlarge block */
    *set = (fd_set *) malloc (fdsbytes);
  } else {
    *set = (fd_set *) realloc((void*) *set, fdsbytes);
  }
  if (! (*set)) return NULL;
  memset (*set,0,fdsbytes);
  *bytes = fdsbytes;
  return *set;
}

#define WAITFORCONNECT_NOMORE -1
#define WAITFORCONNECT_DONEXT -3
#define WAITFORCONNECT_CRITFAIL -4

int waitforconnect (
/* Wait in a select for one of the pending sockets to connect or for
 * the time out until the next action to expire 
 * return WAITFORCONNECT_DONEXT or the index of the successfully
 * connected socket */
  struct CONNECTIONPROGRESS *c
) {
  long long now, wait;
  int i, r, somethingfailed;
  struct timeval selecttimeout;

  /* fprintf (stdout,"Enter waitforconnect at %f\n",milliseconds()); */
  /* Figure out the longest we should wait before taking another action
   * such as giving up or starting another connect() */
  now = milliseconds();
  wait = c->firstwait;
  if (c->nextsocket>1) {
    if (wait>c->nextwait) wait = c->nextwait;
  } 
  if (c->nextsocket>=c->totaladdresses) wait = c->finishby-now;
  /* fprintf (stdout,"nextsocket=%d, total=%d, wait=%f\n", 
     c->nextsocket,c->totaladdresses,wait);*/
  while (wait>0LL) {
    /* fetch memory for an fd_set and flag the pending sockets */
    if (!fdsetalloc (&(c->writefds),&(c->fdsetbytes),c->topsocket)) 
      return WAITFORCONNECT_CRITFAIL;
    for (r=i=0; i<c->nextsocket; i++) 
      if (c->sockets[i].socket>=0) {
        FD_SET(c->sockets[i].socket,c->writefds);
        r=1;
      }
    if ((!r)&&(c->nextsocket<c->totaladdresses)) return WAITFORCONNECT_DONEXT;
    if (!r) return WAITFORCONNECT_NOMORE;

    /* Stuff wait into a timeval structure for select */
    selecttimeout.tv_sec = (time_t) (wait/1000LL);
    selecttimeout.tv_usec = (suseconds_t) ((wait%1000LL)*1000LL);

    /* wait until a socket connects or fails, or until the time out
     * expires. */
    r = select (c->topsocket+1, NULL, c->writefds, NULL, &selecttimeout);
    if ((r<0)&&(errno!=EINTR)) return WAITFORCONNECT_CRITFAIL;

    /* any sockets which are writable are either connected or failed */
    for (somethingfailed=i=0; i<c->nextsocket; i++) {
      if (!FD_ISSET(c->sockets[i].socket,c->writefds)) continue;
      c->sockets[i].error = getsocketerrno (c->sockets[i].socket);
      if (!c->sockets[i].error) { /* Connected! */
        /*fprintf (stdout,"waitforconnect Connected! socket=%d, "
	"index=%d\n", c->sockets[i].socket,i); */
        return connectdonetrying (c,c->sockets[i].socket,0);
      }
        /*fprintf (stdout,"waitforconnect socket %d index %d failed "
	"with %d(%s)\n", c->sockets[i].socket,i,c->sockets[i].error,
	strerror(c->sockets[i].error));*/
      somethingfailed = 1;
      shutdown (c->sockets[i].socket,SHUT_RDWR);
      close (c->sockets[i].socket);
      c->sockets[i].socket=-1;
    }
    if (somethingfailed) return WAITFORCONNECT_DONEXT;
    wait -= milliseconds() - now;
  }
  return WAITFORCONNECT_DONEXT;
}

int connectbyaddrinfo (
  const struct addrinfo *addresses
, long long timeout
, struct CONNECTOPTIONS *options
) {
  struct CONNECTIONPROGRESS *c;
  int sockindex,i;
  long long now;
  struct CONNECTOPTIONS nooptions;

  /* fprintf (stdout,"connectbyaddrinfo enter\n"); */
  if (!options) {
    options = &nooptions;
    memset ((void*) options,0,sizeof(struct CONNECTOPTIONS));
  }

  if (timeout<100) timeout=100; /* give myself at least 100 ms to finish */
  c = allocconnectionstruct(addresses,options->like,options->skip,timeout,
	options->reportdetails,options->dnspinning);
  if (!c) {
    errno = ENOMEM;
    return -1;
  }
  options->numaddresses=c->totaladdresses;
  if (c->details) options->details = c->details;
  now = milliseconds();
  while (now<c->finishby) {
    sockindex = nextconnect(c);
    if (sockindex<0) sockindex = waitforconnect(c);
    if (sockindex>=0) { /* connected */
      int sock = c->sockets[sockindex].socket;
      /* fprintf (stdout,"connectbyaddrinfo connected: %d(%d) ",
	 sock,sockindex);
         printaddrinfo (c->sockets[sockindex].address,1); */
      errno=0;
      if (options->reportpicked) 
	options->picked= (struct addrinfo*) c->sockets[sockindex].address;
      free(c);
      return sock;
    }
    if (sockindex==WAITFORCONNECT_CRITFAIL) {
      connectdonetrying(c,-1,ENOMEM);
      options->picked=NULL;
      errno = ENOMEM;
      free(c);
      return -1;
    }
    if (sockindex==WAITFORCONNECT_NOMORE) {
      /* Tried and failed on all candidate addresses */
      errno=0;
      connectdonetrying(c,-1,0);
      for (i=0; i<c->totaladdresses; i++) 
        if (c->sockets[i].error>errno) errno=c->sockets[i].error;
      if (errno<=0) errno=EBADF;
      options->picked=NULL;
      free(c);
      return -1;
    }
    now = milliseconds();
  }
  /* No connection within the allotted timeout */
  connectdonetrying(c,-1,ETIMEDOUT);
  errno=0;
  for (i=0; i<c->totaladdresses; i++)
    if (c->sockets[i].error>errno) errno=c->sockets[i].error;
  if (errno<=0) errno=ETIMEDOUT;
  options->picked=NULL;
  free(c);
  return -1;
}

/* GNU libc's gai_cancel() looks to see if the thread finished. If so,
 * it accepts the cancellation. If not, it rejects. If it rejects, we
 * must try again later to recover the memory or else leak it. So, we
 * stash the knowledge in nbgai_pleasecancelme and try again the next
 * time a timeoutgetaddrinfo() is called. */
struct NBGAI_PENDING {
  struct gaicb *req;
  struct NBGAI_PENDING *next;
};

struct NBGAI_PENDING *nbgai_pleasecancelme = NULL;
pthread_mutex_t nbgai_pleasecancelme_mutex = PTHREAD_MUTEX_INITIALIZER;

void nbgai_cancelagain (void) {
/* Try again to cancel the no-longer-useful getaddrinfo_a()'s that we failed
 * to cancel before. */
  int r;

  struct NBGAI_PENDING *thisone, **parent;

  pthread_mutex_lock (&nbgai_pleasecancelme_mutex);
  parent=&nbgai_pleasecancelme;
  thisone = nbgai_pleasecancelme;
  while (thisone) {
    r = gai_cancel(thisone->req);
    if (r!=EAI_NOTCANCELED) {
      /* fprintf (stdout,"Cancel later: %s:%s\n",
         thisone->req->ar_name,thisone->req->ar_service); */
      if (thisone->req->ar_name) free ((void*) thisone->req->ar_name);
      if (thisone->req->ar_service) free ((void*) thisone->req->ar_service);
      if (thisone->req->ar_request) free ((void*) thisone->req->ar_request);
      if (thisone->req->ar_result) freeaddrinfo (thisone->req->ar_result);
      free (thisone->req);
      *parent=thisone->next;
      free (thisone);
      thisone=*parent;
      continue;
    }
    /* fprintf (stdout,"Still can't cancel: %s:%s\n",
       thisone->req->ar_name,thisone->req->ar_service); */
    parent=&(thisone->next);
    thisone=thisone->next;
  }
  pthread_mutex_unlock (&nbgai_pleasecancelme_mutex);
}

void nbgai_cancellater (
/* We failed to gai_cancel() a getaddrinfo_a() request, so queue it for a
 * later cancellation attempt */
  struct gaicb *req
) {
  struct NBGAI_PENDING *p;

  p = malloc (sizeof(*p));
  if (!p) return;
  pthread_mutex_lock (&nbgai_pleasecancelme_mutex);
  p->next = nbgai_pleasecancelme;
  p->req= req;
  nbgai_pleasecancelme = p;
  pthread_mutex_unlock (&nbgai_pleasecancelme_mutex);
  return;
}


int nbgai_freeandreturn (
/* cancel and release the memory for a pending or recently completed
 * getaddrinfo_a() request and then pass through rcode. */
  struct gaicb *reqs[]
, int rcode
) {
  int r;
  if (reqs[0]) {
    r = gai_cancel(reqs[0]);
    if (r!=EAI_NOTCANCELED) {
      if (reqs[0]->ar_name) free ((void*) reqs[0]->ar_name);
      if (reqs[0]->ar_service) free ((void*) reqs[0]->ar_service);
      if (reqs[0]->ar_request) free ((void*) reqs[0]->ar_request);
      if (reqs[0]->ar_result && (r==EAI_CANCELED))
	freeaddrinfo (reqs[0]->ar_result);
      free (reqs[0]);
    } else {
      nbgai_cancellater (reqs[0]);
      /* fprintf (stdout,"getaddrinfo could not cancel %s:%s\n",
         reqs[0]->ar_name,reqs[0]->ar_service);*/
    }
  }
  return rcode;
}

int timeoutgetaddrinfo (
/* See header */
  const char *node,
  const char *service,
  const struct addrinfo *hints,
  struct addrinfo **res,
  long long *timeout
) {
  long long dstartat,dendat;
  struct addrinfo *hintsm;
  int r;
  struct gaicb *reqs[1];
  struct timespec to;

  if (!timeout) return EAI_SYSTEM;
  if (*timeout < 50) *timeout = 50; /* give myself at least 50 ms */

  /* When did I start? */
  if ((dstartat = milliseconds()) < 0LL) return EAI_SYSTEM;

  reqs[0] = malloc(sizeof(*reqs[0]));
  if (reqs[0]==NULL) return EAI_MEMORY;
  memset(reqs[0], 0, sizeof(*reqs[0]));
  /*       struct gaicb {
   *           const char            *ar_name;
   *           const char            *ar_service;
   *           const struct addrinfo *ar_request;
   *           struct addrinfo       *ar_result;
   *       };
   */
  reqs[0]->ar_name = strdup (node);
  reqs[0]->ar_service = strdup (service);
  hintsm = (struct addrinfo*) malloc (sizeof (struct addrinfo));
  if (!hintsm) return nbgai_freeandreturn (reqs,EAI_MEMORY);
  if (hints) {
    memcpy (hintsm,hints,sizeof(struct addrinfo));
    hintsm->ai_addr = NULL;
    hintsm->ai_canonname = NULL;
    hintsm->ai_next = NULL;
  } else {
    memset (hintsm,0,sizeof(struct addrinfo));
  }
  reqs[0]->ar_request = hintsm;
  if (!reqs[0]->ar_name || !reqs[0]->ar_service || !reqs[0]->ar_request) 
    return nbgai_freeandreturn (reqs,EAI_MEMORY);

  /* Would not use getaddrinfo here but will come back to that problem
   * later on. */
  /* fprintf (stdout,"About to call getaddrinfo on %s:%s at %lld\n",
	node,service,dstartat);
     fflush (stdout);  */
  nbgai_cancelagain();
  r = getaddrinfo_a(GAI_NOWAIT,reqs,1,NULL);
  if (r) return nbgai_freeandreturn (reqs,r);
  while (1) {
    to.tv_sec = (time_t) ((*timeout)/1000LL);
    to.tv_nsec = (suseconds_t) (((*timeout)%1000LL)*1000000LL);
    r = gai_suspend ((const struct gaicb * const*)reqs,1,&to);
    if ((dendat = milliseconds()) < 0)
      return nbgai_freeandreturn (reqs,EAI_SYSTEM);
    (*timeout) -= (dendat - dstartat);
    dstartat = dendat;
    if (r==EAI_AGAIN) return nbgai_freeandreturn (reqs,r); /* timeout */
    if (r) continue;
    r=gai_error(reqs[0]);
    if (r) return nbgai_freeandreturn (reqs,r); /* failed request */
    *res = reqs[0]->ar_result;
    /* printaddrinfo (*res,0); */
    return nbgai_freeandreturn (reqs,0); /* finished lookup on time */
  }

  /* not reached */
  return EAI_SYSTEM;
}

struct addrinfo *dupeaddrinfo (const struct addrinfo *address) {
/* duplicate the first entry in *address */
  struct addrinfo *a;
  void *p1, *p2;


  if (!address || !address->ai_addr) return NULL;
  if (address->ai_addrlen<=0) return NULL;
  p1 = (void*) address;
  p2 = (void*) address->ai_addr;
  
  if ( (size_t) (((void*)address->ai_addr) - ((void*) address)) ==
	sizeof(*address) ) { /* GNU libc mallocs one block of memory
                              * for address and address->ai_addr, pointing
			      * ai_addr just beyond the addrinfo structure.
                              * Does everybody else? */
    a = (struct addrinfo*) malloc (sizeof(*a)+address->ai_addrlen);
    if (!a) return NULL;
    memcpy (a,address,sizeof(*a)+address->ai_addrlen);
    a->ai_addr=(struct sockaddr*) (a+1);
  } else { /* I hope they haven't composed the addrinfo struct in a way
            * that causes this to leak in freeaddrinfo() like it would in
            * GNU libc... */
    a = (struct addrinfo*) malloc (sizeof(*a));
    if (!a) return NULL;
    memcpy (a,address,sizeof(*a));
    a->ai_addr = (struct sockaddr*) malloc (address->ai_addrlen);
    if (a->ai_addr==NULL) {
      free (a);
      return NULL;
    }
    memcpy (a->ai_addr,address->ai_addr,address->ai_addrlen);
  }
  a->ai_next=NULL;
  /* fprintf (stdout,"Pointers: a=%X,\n a->ai_addr=%X,\n a->ai_cannonname=%x,\n"
	" sizeof(a)=%X\n",
	(unsigned)address, (unsigned)address->ai_addr, 
	(unsigned)address->ai_canonname,sizeof(*address));
     fprintf (stdout,"Pointers: a=%X,\n a->ai_addr=%X,\n a->ai_cannonname=%x,\n"
	" sizeof(a)=%X\n",
	(unsigned)a, (unsigned)a->ai_addr, 
	(unsigned)a->ai_canonname,sizeof(*a)); */
  if (address->ai_canonname) a->ai_canonname = strdup (a->ai_canonname);
  return a;
}

int connectbyname (
  const char *name
, const char *service
, long long timeout
, struct CONNECTOPTIONS *options
) {
  struct addrinfo *addresses;
  struct addrinfo hints;
  int r;

  /* fprintf (stdout,"Enter connectbyname %s:%s(%lld)\n",
     name,service,timeout); */
  /* Fetch candidate IP addresses from the name + service */
  memset (&hints,0,sizeof(hints));
  hints.ai_family = AF_UNSPEC;
  hints.ai_socktype=SOCK_STREAM;
  hints.ai_flags |= AI_ADDRCONFIG;
  hints.ai_flags &= (~AI_V4MAPPED);
  r = timeoutgetaddrinfo (name,service,&hints,&addresses,&timeout);
  if (options) options->getaddrinfoerror = r;
  if (r) { /* if I couldn't get addresses, fail */
    errno = EFAULT; /* Bad address (POSIX.1) */
    return -1;
  }

  /* give myself at least a second to connect, even if getaddrinfo ate
   * too much of my timeout. */
  if (timeout<1000LL) timeout=1000LL; 

  /*fprintf (stdout,"connectbyaddrinfo (%lld)\n",timeout);*/
  /* Try to connect with the retrieved addresses */
  r = connectbyaddrinfo (addresses,timeout,options);

  /* One way or another, done. */
  if (options && options->picked) 
    options->picked = dupeaddrinfo (options->picked);

  /* If addresses is not consumed by the details option, free their RAM. */
  if (!options || !options->details) freeaddrinfo (addresses);

  return r;
}

int listenbyaddrinfo (
  struct addrinfo *address
, int backlog
) {
  int s;
  int reuseaddr=1;

  if (!address) return -1;

  s = socket(address->ai_family, address->ai_socktype, address->ai_protocol);
  if (s<0) return s; /* Can't seem to get a socket of that type! */
  /*if (address->ai_family==AF_INET) {
    struct sockaddr_in *si4;
    si4 = (struct sockaddr_in*) address->ai_addr;
    fprintf (stdout,"listenbyaddrinfo family %d port %d\n",address->ai_family,
	(int) ntohs(si4->sin_port));
  } else {
    struct sockaddr_in6 *si6;
    si6 = (struct sockaddr_in6*) address->ai_addr;
    fprintf (stdout,"listenbyaddrinfo family %d port %d\n",address->ai_family,
	(int) ntohs(si6->sin6_port));
  }*/
  /*    SO_REUSEADDR
          Indicates that the rules used in validating  addresses  supplied
          in  a  bind(2)  call should allow reuse of local addresses.  For
          AF_INET sockets this means that a socket may bind,  except  when
          there  is an active listening socket bound to the address.  When
          the listening socket is bound to INADDR_ANY with a specific port
          then  it  is  not  possible  to  bind to this port for any local
          address.  Argument is an integer boolean flag.
   */
  if (setsockopt(s,SOL_SOCKET,SO_REUSEADDR,&reuseaddr,sizeof(reuseaddr))) {
    /* Keep trying. But this really shouldn't fail! */
    /* close (s);
    return -1; */
  }

  if (bind(s,address->ai_addr,address->ai_addrlen)) {
    /* That port is not available */
    close(s);
    return -1;
  }

  if (listen(s,backlog)) { /* listen failed */
    close (s);
    return -1;
  }
  return s;
}


int listenbyname (
/* Open listener sockets for all address families supporting *service
 * and return them as a -1 terminated array. Will listen on the wildcard
 * address for all of the protocol famlies.
 */
  const char *service
, int socktype  /* SOCK_STREAM or SOCK_SEQPACKET */
, int backlog
) {
  int l;
  struct addrinfo hints, *res;
  int r;

  memset (&hints,0,sizeof(hints));
  hints.ai_family = AF_INET6; /* accepts IPv4 *and* IPv6 connections */
  hints.ai_socktype = socktype;
  hints.ai_flags = AI_PASSIVE;
  r=getaddrinfo (NULL,service,&hints,&res);
  if (!r) { /* got an address */
    l = listenbyaddrinfo (res,backlog);
    freeaddrinfo (res);
    return l;
  }
  errno = EFAULT; /* Bad address (POSIX.1) */
  return -1;
}

