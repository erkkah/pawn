/* Pawn TCP/IP remote debugging
 *
 *  Copyright (c) CompuPhase, 1998-2020
 *
 *  Licensed under the Apache License, Version 2.0 (the "License"); you may not
 *  use this file except in compliance with the License. You may obtain a copy
 *  of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing, software
 *  distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 *  WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied. See the
 *  License for the specific language governing permissions and limitations
 *  under the License.
 *
 */

#if !defined NO_REMOTE

#include "remotedbg.h"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include "osdefs.h"     /* for _MAX_PATH and other macros */
#if defined(__LINUX__)
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <poll.h>
#include <errno.h>
#endif

#include "dbgterm.h"
#include "amx.h"

static int sock = 0;
static char remote_pendingbuf[30];
static int remote_pendingsize=0;

static int send_tcp(const char *buffer, int len)
{
  unsigned long size;

  size=write(sock,buffer,len);
  assert((unsigned long)len==size);
  return size;
}

static int getresponse_tcp(char *buffer, int buffersize, long retries)
{
  int len=0;
  unsigned long size;

  do {

    /* read character by character, so that when we see the ']' we stop
     * reading and keep the rest of the waiting characters in the queue
     */
    size = read(sock,buffer+len,1);
    if (size <= 0) {
      return 0;
    }
    len+=size;

    /* throw away dummy input characters */
    while (buffer[0]!='\xbf' && len>0) {
      int idx;
      for (idx=0; idx<len && buffer[idx]!='\xbf'; idx++)
        /* nothing */;
      memmove(buffer,buffer+idx,len-idx);
      len-=idx;
    } /* while */

    if (len<buffersize)
      buffer[len]='\0'; /* force zero termination */

    if (size==0) {
      usleep(50*1000);
      retries--;
    } /* if */

  } while ((len==0 || strchr(buffer,']')==NULL) && retries>0);

  if (len>0 && strchr(buffer,']')==NULL)
    len=0;
  return len;
}

static int settimestamp_tcp(unsigned long sec1970)
{
  char str[30];

  assert(sock>=0);
  sprintf(str,"?T%lx\n",sec1970);
  send_tcp(str,(int)strlen(str));
  return getresponse_tcp(str,sizeof str,10) && atoi(str+1)>0;
}

void remote_close_tcp() {
  if (sock != 0) {
    char buffer[40];
    sprintf(buffer, "?U\n");
    write(sock, buffer, strlen(buffer));
  }
  close(sock);
  sock = 0;
}

static int connect_with_timeout(int sockfd, const struct sockaddr *addr, socklen_t addrlen, unsigned int timeout_ms) {
    int rc = 0;
    // Set O_NONBLOCK
    int sockfd_flags_before;
    if((sockfd_flags_before=fcntl(sockfd,F_GETFL,0)<0)) return -1;
    if(fcntl(sockfd,F_SETFL,sockfd_flags_before | O_NONBLOCK)<0) return -1;
    // Start connecting (asynchronously)
    do {
        if (connect(sockfd, addr, addrlen)<0) {
            // Did connect return an error? If so, we'll fail.
            if ((errno != EWOULDBLOCK) && (errno != EINPROGRESS)) {
                rc = -1;
            }
            // Otherwise, we'll wait for it to complete.
            else {
                // Set a deadline timestamp 'timeout' ms from now (needed b/c poll can be interrupted)
                struct timespec now;
                if(clock_gettime(CLOCK_MONOTONIC, &now)<0) { rc=-1; break; }
                struct timespec deadline = { .tv_sec = now.tv_sec,
                                             .tv_nsec = now.tv_nsec + timeout_ms*1000000l};
                // Wait for the connection to complete.
                do {
                    // Calculate how long until the deadline
                    if(clock_gettime(CLOCK_MONOTONIC, &now)<0) { rc=-1; break; }
                    int ms_until_deadline = (int)(  (deadline.tv_sec  - now.tv_sec)*1000l
                                                  + (deadline.tv_nsec - now.tv_nsec)/1000000l);
                    if(ms_until_deadline<0) { rc=0; break; }
                    // Wait for connect to complete (or for the timeout deadline)
                    struct pollfd pfds[] = { { .fd = sockfd, .events = POLLOUT } };
                    rc = poll(pfds, 1, ms_until_deadline);
                    // If poll 'succeeded', make sure it *really* succeeded
                    if(rc>0) {
                        int error = 0; socklen_t len = sizeof(error);
                        int retval = getsockopt(sockfd, SOL_SOCKET, SO_ERROR, &error, &len);
                        if(retval==0) errno = error;
                        if(error!=0) rc=-1;
                    }
                }
                // If poll was interrupted, try again.
                while(rc==-1 && errno==EINTR);
                // Did poll timeout? If so, fail.
                if(rc==0) {
                    errno = ETIMEDOUT;
                    rc=-1;
                }
            }
        }
    } while(0);
    // Restore original O_NONBLOCK state
    if(fcntl(sockfd,F_SETFL,sockfd_flags_before)<0) return -1;
    // Success
    return rc;
}

int remote_tcp(const char *host, int port)
{
  if (sock != 0) {
    remote_close_tcp();
  }

  sock = socket(AF_INET, SOCK_STREAM, 0);
  if (sock < 0) {
    return 0;
  }

  struct sockaddr_in host_addr;

  host_addr.sin_addr.s_addr = inet_addr(host);
  host_addr.sin_family = AF_INET;
  host_addr.sin_port = htons(port);

  if (connect_with_timeout(sock, (struct sockaddr *)&host_addr , sizeof(host_addr), 5000) < 0) {
    amx_printf("Connection to %s:%d failed\n", host, port);
    return 0;
  }

  amx_printf("Connected!\n");

  /* handshake, send token and wait for a reply */
  char buffer[40];
  int sync_found = 0;
  int size = 0;

  do {
    write(sock, "\xa1", 1);
    usleep(10*1000);
    for (int count=0; count<4 && !sync_found; count++) {
      usleep(10*1000);
      do {
        size=read(sock, buffer, 1);
        sync_found= (size>0 && buffer[0]=='\xbf');
        if (sync_found) {
          usleep(20*1000);  /* give ample time for ']' to arrive */
          size=read(sock,buffer,1);
          if (size<=0 || buffer[0]!=']')
            sync_found=0;
        } /* if */
      } while (!sync_found && size>0);
    } /* for */
    /* read remaining buffer (if any) */
    size=read(sock,buffer,sizeof buffer);
  } while (!sync_found);
  if (size>0 && size<sizeof remote_pendingbuf) {
    remote_pendingsize=size;
    memcpy(remote_pendingbuf,buffer,remote_pendingsize);
  } /* if */
  /* give a "sync time" command, so that the device has the same time as the computer */
  settimestamp_tcp((unsigned long)time(NULL));

  remote=REMOTE_TCP;
  return 1;
}

int remote_wait_tcp(AMX *amx, long _)
{
  char buffer[50],*ptr;
  unsigned long size;
  long cip;
  int offs,state;
  enum { SCAN, START, FINISH };

  for ( ;; ) {
    offs=0;
    size=0;
    state=SCAN;
    while (state!=FINISH) {
      offs+=size;
      if (offs>=sizeof buffer - 1) {
        amx_printf("%s",buffer);
        offs=0;
        state=SCAN;
      } /* if */
      /* read a buffer, see if we can find the start condition */
      if (remote_pendingsize>0) {
        assert(remote_pendingsize<sizeof buffer);
        size=remote_pendingsize;
        memcpy(buffer,remote_pendingbuf,remote_pendingsize);
        remote_pendingsize=0;
      } else {
        size=read(sock,buffer+offs,sizeof buffer - offs - 1);
      } /* if */
      if (size <= 0) {
        return 0;
      }
      if (size == 0 && remote_pendingsize == 0)
        return 0;
      assert(size+offs<sizeof buffer);
      buffer[size+offs]='\0';   /* force zero-termination */
      if (state==SCAN) {
        for (ptr=buffer; (unsigned long)(ptr-buffer)<size && *ptr!='\xbf'; ptr++)
          /* nothing */;
        if ((unsigned long)(ptr-buffer)>=size) {
          amx_printf("%s",buffer);
        } else {
          if (ptr!=buffer)
            memmove(buffer,ptr,(size+offs)-(ptr-buffer));
          state=START;
        } /* if */
      } /* if */
      if (state==START) {
        for (ptr=buffer; (unsigned long)(ptr-buffer)<(size+offs) && *ptr!=']'; ptr++)
          /* nothing */;
        if (*ptr==']') {
          state=FINISH;
          if (strlen(++ptr)>0)
            amx_printf("%s",ptr);
        } /* if */
      } /* if */
    } /* while */
    amx_fflush();
    /* we found a packet starting with '\xbf' and ending ']'; now check the
     * validity of the packet
     */
    if (sscanf(buffer+1,"%lx",&cip)==1) {
      amx->cip=(cell)cip;
      return 1;
    } else {
      /* unknown buffer format; print and continue */
      amx_printf("%s",buffer);
    } /* if */
  } /* for */
}

int remote_resume_tcp(void)
{
  return write(sock,"!",1) < 0;
}

int remote_sync_tcp(AMX *amx)
{
  char buffer[128];
  long frm,stk,hea;

  for ( ;; ) {
    if (write(sock,"?R\n",3) <= 0) {
      return 0;
    }

    int status = getresponse_tcp(buffer,sizeof buffer,10);
    if (status > 0 && sscanf(buffer+1,"%lx,%lx,%lx",&frm,&stk,&hea)==3)
    {
      amx->frm=(cell)frm;
      amx->stk=(cell)stk;
      amx->hea=(cell)hea;
      return 1;
    } /* if */
    if (status <= 0) {
      return 0;
    }
  } /* for */
}

int remote_read_tcp(AMX *amx,cell vaddr,int number)
{
  char buffer[128];
  char *ptr;
  int len;
  int status;
  cell val;
  cell *cptr;

  while (number>0) {
    sprintf(buffer,"?M%lx,%x\n",(long)vaddr,(number>10) ? 10 : number);
    len=(int)strlen(buffer);
    status = send_tcp(buffer,len);
    if (status <= 0) {
      return 0;
    }
    status = getresponse_tcp(buffer,sizeof buffer,100);
    if (status > 0) {
      ptr=buffer+1;     /* skip '�' */
      while (number>0 && (ptr-buffer)<sizeof buffer && *ptr!=']') {
        val=strtol(ptr,&ptr,16);
        if ((cptr=VirtAddressToPhys(amx,vaddr))!=NULL)
          *cptr=val;
        number--;
        vaddr+=sizeof(cell);
        while ((*ptr!='\0' && *ptr<=' ') || *ptr==',')
          ptr++;        /* skip optional comma (and whitespace) */
      } /* while */
    } /* if */
    if (status <= 0) {
      return 0;
    }
  } /* while */
}

int remote_write_tcp(AMX *amx,cell vaddr,int number)
{
  char buffer[128];
  int len,num;
  int status;
  cell *cptr;

  while (number>0) {
    num=(number>10) ? 10 : number;
    number-=num;
    sprintf(buffer,"?W%lx",(long)vaddr);
    while (num>0) {
      cptr=VirtAddressToPhys(amx,vaddr);
      assert(cptr!=NULL);
      strcat(buffer,",");
      sprintf(buffer+strlen(buffer),"%x",*cptr);
      num--;
      vaddr+=sizeof(cell);
    } /* while */
    strcat(buffer,"\n");
    len=(int)strlen(buffer);
    if (send_tcp(buffer,len) <= 0) {
      return 0;
    }
    
    status = getresponse_tcp(buffer,sizeof buffer,100);
    if (status > 0 && strtol(buffer+1,NULL,16) == 0) {
      return 1;
    }
    if (status <= 0) {
      return 0;
    }
  } /* while */
}

int remote_transfer_tcp(const char *filename)
{
  #define ACK ((unsigned char)6)
  #define NAK ((unsigned char)21)
  unsigned char *buffer;
  char str[128];
  FILE *fp;
  size_t bytes;
  unsigned long size,chksum,block;
  int len,err;

  if (sock<0)
    return 0;

  if ((fp=fopen(filename,"rb"))==NULL)
    return 0;
  /* determine the file size */
  fseek(fp,0,SEEK_END);
  size=ftell(fp);
  fseek(fp,0,SEEK_SET);

  /* set up */
  sprintf(str,"?P %lx,%s\n",size,skippath(filename));
  len=(int)strlen(str);

  int status = send_tcp(str,len);
  if (status <= 0) {
    return 0;
  }
  status = getresponse_tcp(str,sizeof str,100);
  if (status == 0 || sscanf(str+1,"%lx",&block) != 1) {
    block=0;
  }
  if (status <= 0) {
    return 0;
  }
  /* allocate 1 byte more, for the ACK/NAK prefix */
  if (block==0 || (buffer=(unsigned char*)malloc((block+1)*sizeof(char)))==NULL) {
    fclose(fp);
    return 0;
  } /* if */

  /* file transfer acknowledged, transfer data per block */
  amx_printf("Transferring ");
  amx_fflush();
  while (size>0 && (bytes=fread(buffer+1,1,block,fp))!=0) {
    buffer[0]=ACK;
    /* calculate the checksum */
    chksum=1;
    for (len=1; len<=(int)bytes; len++)
      chksum+=buffer[len];
    while (chksum>0xff)
      chksum=(chksum&0xff)+(chksum>>8);
    do {
      /* send block */
      int status = send_tcp((const char*)buffer,(int)bytes+1); /* also send the ACK/NAK prefix */
      if (status <= 0) {
        return 0;
      }
      status = getresponse_tcp(str,sizeof str,100);
      if (status <= 0) {
        return 0;
      }
      err=(str[0]!='\0') ? strtol(str+1,NULL,16) : 0;
      assert(err>=0 && err<=255);
      if (err==0) {
        free(buffer);
        fclose(fp);
        return 0;
      } /* if */
      buffer[0]=NAK;    /* preset for failure (if err!=chksum => failure) */
    } while (err!=(int)chksum);
    size-=block;
    if (size<block)
      block=size;
  } /* while */
  buffer[0]=ACK;
  send_tcp((const char*)buffer,1); /* ACK the last block */

  free(buffer);
  fclose(fp);

  /* reboot the device */
  strcpy(str,"?U*\n");
  len=(int)strlen(str);
  send_tcp(str,len);

  return 1;
}

#endif // !NO_REMOTE
