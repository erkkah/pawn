/* Pawn RS232 remote debugging
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
#include <fcntl.h>
#include <termios.h>
#include <unistd.h>
#endif

#include "dbgterm.h"
#include "amx.h"

//#define amx_printf      printf
//#define amx_fflush()    fflush(stdout)

#if defined __WIN32__
  HANDLE hCom=INVALID_HANDLE_VALUE;
#elif !defined __MSDOS__
  int fdCom=-1;
  struct termios oldtio, newtio;
#endif
static char remote_pendingbuf[30];
static int remote_pendingsize=0;

static int send_rs232(const char *buffer, int len)
{
  unsigned long size;

  #if defined __WIN32__
    WriteFile(hCom,buffer,len,&size,NULL);
    FlushFileBuffers(hCom);
  #else
    size=write(fdCom,buffer,len);
  #endif
  assert((unsigned long)len==size);
  return size;
}

static int getresponse_rs232(char *buffer, int buffersize, long retries)
{
  int len=0;
  unsigned long size;

  do {

    /* read character by character, so that when we see the ']' we stop
     * reading and keep the rest of the waiting characters in the queue
     */
    #if defined __WIN32__
      ReadFile(hCom,buffer+len,1,&size,NULL);
    #else
      size=read(fdCom,buffer+len,1);
    #endif
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
      #if defined __WIN32__
        Sleep(50);
      #else
        usleep(50*1000);
      #endif
      retries--;
    } /* if */

  } while ((len==0 || strchr(buffer,']')==NULL) && retries>0);

  if (len>0 && strchr(buffer,']')==NULL)
    len=0;
  return len;
}

static int settimestamp_rs232(unsigned long sec1970)
{
  char str[30];

  #if defined __WIN32__
    assert(hCom!=INVALID_HANDLE_VALUE);
  #else
    assert(fdCom>=0);
  #endif
  sprintf(str,"?T%lx\n",sec1970);
  send_rs232(str,(int)strlen(str));
  return getresponse_rs232(str,sizeof str,10) && atoi(str+1)>0;
}

int remote_rs232(const char *port,int baud)
{
  #if defined __WIN32__
    DCB    dcb;
    COMMTIMEOUTS commtimeouts;
    DWORD size;
  #else
    size_t size;
  #endif
  char buffer[40];
  int sync_found,count;

  /* optionally issue a "close-down" request for the remote host */
  #if defined __WIN32__
  if (baud==0 && hCom!=INVALID_HANDLE_VALUE) {
  #else
  if (baud==0 && fdCom>=0) {
  #endif
    sprintf(buffer,"?U\n");
    #if defined __WIN32__
      WriteFile(hCom,buffer,(DWORD)strlen(buffer),&size,NULL);
    #else
      write(fdCom,buffer,strlen(buffer));
    #endif
    /* do not wait for a reply */
  } /* if */

  /* set up the connection */
  #if defined __WIN32__
    if (hCom!=INVALID_HANDLE_VALUE)
      CloseHandle(hCom);
    if (baud==0)
      return 0;
    hCom=CreateFile(port,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
    if (hCom==INVALID_HANDLE_VALUE) {
      sprintf(buffer,"\\\\.\\%s",port);
      hCom=CreateFile(buffer,GENERIC_READ|GENERIC_WRITE,0,NULL,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,NULL);
      if (hCom==INVALID_HANDLE_VALUE)
        return 0;
    }
    GetCommState(hCom,&dcb);
    dcb.BaudRate=baud;
    dcb.ByteSize=8;
    dcb.StopBits=ONESTOPBIT;
    dcb.Parity=NOPARITY;
    dcb.fBinary=TRUE;
    dcb.fDtrControl=DTR_CONTROL_DISABLE;
    dcb.fOutX=FALSE;
    dcb.fInX=FALSE;
    dcb.fNull=FALSE;
    dcb.fRtsControl=RTS_CONTROL_DISABLE;
    SetCommState(hCom,&dcb);
    SetCommMask(hCom,EV_RXCHAR|EV_TXEMPTY);
    commtimeouts.ReadIntervalTimeout        =0x7fffffff;
    commtimeouts.ReadTotalTimeoutMultiplier =0;
    commtimeouts.ReadTotalTimeoutConstant   =1;
    commtimeouts.WriteTotalTimeoutMultiplier=0;
    commtimeouts.WriteTotalTimeoutConstant  =0;
    SetCommTimeouts(hCom,&commtimeouts);
  #else
    if (fdCom>=0) {
      tcflush(fdCom,TCOFLUSH);
      tcflush(fdCom,TCIFLUSH);
      tcsetattr(fdCom,TCSANOW,&oldtio);
      close(fdCom);
    } /* if */
    if (baud==0)
      return 0;
    fdCom = open(port,O_RDWR|O_NOCTTY|O_NONBLOCK);
    if (fdCom<0)
      return 0;
  	/* clear input & output buffers, then switch to "blocking mode" */
    tcflush(fdCom,TCOFLUSH);
    tcflush(fdCom,TCIFLUSH);
    fcntl(fdCom,F_SETFL,fcntl(fdCom,F_GETFL) & ~O_NONBLOCK);

    tcgetattr(fdCom,&oldtio); /* save current port settings */
    bzero(&newtio,sizeof newtio);
    newtio.c_cflag=CS8 | CLOCAL | CREAD;

    switch (baud) {
    #if defined B1152000
    case 1152000: newtio.c_cflag |= B1152000; break;
    #endif
    #if defined B576000
    case  576000: newtio.c_cflag |=  B576000; break;
    #endif
    case  230400: newtio.c_cflag |=  B230400; break;
    case  115200: newtio.c_cflag |=  B115200; break;
    case   57600: newtio.c_cflag |=   B57600; break;
    case   38400: newtio.c_cflag |=   B38400; break;
    case   19200: newtio.c_cflag |=   B19200; break;
    case    9600: newtio.c_cflag |=    B9600; break;
    default:      return 0;
    } /* switch */
    newtio.c_iflag=IGNPAR | IGNBRK | IXON | IXOFF;
    newtio.c_oflag=0;

    /* set input mode (non-canonical, no echo,...) */
    newtio.c_lflag=0;

    cfmakeraw(&newtio);
    newtio.c_cc[VTIME]=1;   /* inter-character timer used */
    newtio.c_cc[VMIN] =0;   /* blocking read until 0 chars received */
    tcflush(fdCom,TCIFLUSH);
    tcsetattr(fdCom,TCSANOW,&newtio);
  #endif

  /* handshake, send token and wait for a reply */
  sync_found=0;
  do {
    #if defined __WIN32__
      WriteFile(hCom,"\xa1",1,&size,NULL);
      Sleep(10);
      for (count=0; count<4 && !sync_found; count++) {
        Sleep(10);
        do {
          ReadFile(hCom,buffer,1,&size,NULL);
          sync_found= (size>0 && buffer[0]=='\xbf');
          if (sync_found) {
            Sleep(20);  /* give ample time for ']' to arrive */
            ReadFile(hCom,buffer,1,&size,NULL);
            if (size==0 || buffer[0]!=']')
              sync_found=0;
          } /* if */
        } while (!sync_found && size>0);
      } /* for */
      /* read remaining buffer (if any) */
      ReadFile(hCom,buffer,sizeof buffer,&size,NULL);
    #else
      write(fdCom,"\xa1",1);
      usleep(10*1000);
      for (count=0; count<4 && !sync_found; count++) {
        usleep(10*1000);
        do {
          size=read(fdCom,buffer,1);
          sync_found= (size>0 && buffer[0]=='\xbf');
          if (sync_found) {
            usleep(20*1000);  /* give ample time for ']' to arrive */
            size=read(fdCom,buffer,1);
            if (size<=0 || buffer[0]!=']')
              sync_found=0;
          } /* if */
        } while (!sync_found && size>0);
      } /* for */
      /* read remaining buffer (if any) */
      size=read(fdCom,buffer,sizeof buffer);
    #endif
  } while (!sync_found);
  if (size>0 && size<sizeof remote_pendingbuf) {
    remote_pendingsize=size;
    memcpy(remote_pendingbuf,buffer,remote_pendingsize);
  } /* if */
  /* give a "sync time" command, so that the device has the same time as the computer */
  settimestamp_rs232((unsigned long)time(NULL));

  remote=REMOTE_RS232;
  return 1;
}

int remote_wait_rs232(AMX *amx, long retries)
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
      do {
        if (remote_pendingsize>0) {
          assert(remote_pendingsize<sizeof buffer);
          size=remote_pendingsize;
          memcpy(buffer,remote_pendingbuf,remote_pendingsize);
          remote_pendingsize=0;
        } else {
          #if defined __WIN32__
            ReadFile(hCom,buffer+offs,sizeof buffer - offs - 1,&size,NULL);
          #else
            size=read(fdCom,buffer+offs,sizeof buffer - offs - 1);
          #endif
        } /* if */
        if (size==0 && remote_pendingsize==0 && retries>0) {
          #if defined __WIN32__
            Sleep(50);
          #else
            usleep(50*1000);
          #endif
          retries--;
        }
      } while (size==0 && remote_pendingsize==0 && retries!=0);
      if (size==0 && remote_pendingsize==0)
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

void remote_resume_rs232(void)
{
  #if defined __WIN32__
    unsigned long size;
    WriteFile(hCom,"!",1,&size,NULL);
  #else
    write(fdCom,"!",1);
  #endif
}

void remote_sync_rs232(AMX *amx)
{
  char buffer[128];
  long frm,stk,hea;
  #if defined __WIN32__
    unsigned long size;
  #endif

  for ( ;; ) {
    #if defined __WIN32__
      WriteFile(hCom,"?R\n",3,&size,NULL);
    #else
      write(fdCom,"?R\n",3);
    #endif

    if (getresponse_rs232(buffer,sizeof buffer,10)
        && sscanf(buffer+1,"%lx,%lx,%lx",&frm,&stk,&hea)==3)
    {
      amx->frm=(cell)frm;
      amx->stk=(cell)stk;
      amx->hea=(cell)hea;
      return;
    } /* if */
  } /* for */
}

void remote_read_rs232(AMX *amx,cell vaddr,int number)
{
  char buffer[128];
  char *ptr;
  int len;
  cell val;
  cell *cptr;

  while (number>0) {
    sprintf(buffer,"?M%lx,%x\n",(long)vaddr,(number>10) ? 10 : number);
    len=(int)strlen(buffer);
    send_rs232(buffer,len);
    if (getresponse_rs232(buffer,sizeof buffer,100)) {
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
  } /* while */
}

void remote_write_rs232(AMX *amx,cell vaddr,int number)
{
  char buffer[128];
  int len,num;
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
    send_rs232(buffer,len);
    if (getresponse_rs232(buffer,sizeof buffer,100) && strtol(buffer+1,NULL,16)==0)
      return;
  } /* while */
}

int remote_transfer_rs232(const char *filename)
{
  #define ACK ((unsigned char)6)
  #define NAK ((unsigned char)21)
  unsigned char *buffer;
  char str[128];
  FILE *fp;
  size_t bytes;
  unsigned long size,chksum,block;
  int len,err;

  #if defined __WIN32__
    if (hCom==INVALID_HANDLE_VALUE)
      return 0;
  #else
    if (fdCom<0)
      return 0;
  #endif

  if ((fp=fopen(filename,"rb"))==NULL)
    return 0;
  /* determine the file size */
  fseek(fp,0,SEEK_END);
  size=ftell(fp);
  fseek(fp,0,SEEK_SET);

  /* set up */
  sprintf(str,"?P %lx,%s\n",size,skippath(filename));
  len=(int)strlen(str);
  send_rs232(str,len);
  if (!getresponse_rs232(str,sizeof str,100) || sscanf(str+1,"%lx",&block)!=1)
    block=0;
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
      send_rs232((const char*)buffer,(int)bytes+1); /* also send the ACK/NAK prefix */
      getresponse_rs232(str,sizeof str,100);
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
  send_rs232((const char*)buffer,1); /* ACK the last block */

  free(buffer);
  fclose(fp);

  /* reboot the device */
  strcpy(str,"?U*\n");
  len=(int)strlen(str);
  send_rs232(str,len);

  return 1;
}

#endif // !NO_REMOTE