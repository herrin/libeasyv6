/* test.c -- put multiconnect through it's paces
 */

#include "easyv6.h"
#include <stdio.h>
#include <errno.h>
#include <stdlib.h> /* exit */
#include <unistd.h> /* fcntl */
#include <fcntl.h> /* fcntl */
#include <string.h> /* memset */
#include <sys/socket.h>

long long milliseconds(void);
void nbgai_cancelagain (void);

void drainsocket (int socket) {
  int fcntlflags, count;
  char bytes[100];

  fcntlflags = fcntl(socket,F_GETFL,0);
  fcntl(socket,F_SETFL,fcntlflags & (~O_NONBLOCK));
  shutdown (socket,SHUT_WR);
  while ((count=read(socket,bytes,100))>0) {
    fwrite (bytes,count,1,stdout);
  } 
  shutdown (socket,SHUT_RD);
  close (socket);
  return;
}

int main (int argc, char **argv) {
  int socket;
  char *host, *service;
  int i, l;
  struct CONNECTOPTIONS options;
  char s[200], *address, *p;

  host="addrtest.dirtside.com";
  service="ssh";
  if (argc>1) host=argv[1];
  if (argc>2) service=argv[2];

  memset (&options,0,sizeof(options));
  options.reportpicked = 1;
  for (i=0; i<3; i++) {
    printf ("Now: %lld\n",milliseconds());
    socket = connectbyname (host,service,10000,&options);
    address = addrinfototext(options.picked,s,200);
    if (address) printf ("IP Address: %s\n",address);
    if (options.picked) {
      freeaddrinfo(options.picked);
      /*if (options.picked->ai_canonname) free (options.picked->ai_canonname);
      if (options.picked->ai_addr) free (options.picked->ai_addr);
      free (options.picked);*/
      options.picked=NULL;
    }
    if (socket<0) {
      printf ("Test failed at %lld: socket=%d, errno=%d, error=%s\n",
 	milliseconds(),socket,errno,strerror(errno));
    } else drainsocket(socket);
  }

  nbgai_cancelagain();
 
  l = listenbyname("3000",SOCK_STREAM,10); 
  fprintf (stdout,"Listening on port 3000...\n");
  i = accept (l,NULL,NULL);
  p=getpeernametext(i,s,200);
  if (p) {
    printf ("Connect from: %s port %s\n",p,s);
  } else {
    printf ("getpeernametext failed at %lld: socket=%d, errno=%d, error=%s\n",
 	milliseconds(),i,errno,strerror(errno));
  }
  p="You connected to the tester. Bye!\n";
  write (i,p,strlen(p));
  shutdown (i,SHUT_RDWR);
  close (i);
  close (l);
  nbgai_cancelagain();
  return 0;
}

