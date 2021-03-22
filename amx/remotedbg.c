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
#include "dbgterm.h"

void remote_read_rs232(AMX *amx,cell vaddr,int number);
void remote_write_rs232(AMX *amx,cell vaddr,int number);
void remote_resume_rs232(void);
void remote_sync_rs232(AMX *amx);
int remote_wait_rs232(AMX *amx, long retries);
int remote_transfer_rs232(const char *filename);

void remote_read_tcp(AMX *amx,cell vaddr,int number);
void remote_write_tcp(AMX *amx,cell vaddr,int number);
void remote_resume_tcp(void);
void remote_sync_tcp(AMX *amx);
int remote_wait_tcp(AMX *amx, long retries);
int remote_transfer_tcp(const char *filename);
void remote_close_tcp();

int remote=REMOTE_NONE;

void remote_close() {
    switch(remote) {
        case REMOTE_RS232:
            remote_rs232(NULL, 0);
            break;
        case REMOTE_TCP:
            remote_close_tcp();
            break;
        default:
            return;
    }
}

void remote_read(AMX *amx,cell vaddr,int number) {
    switch(remote) {
        case REMOTE_RS232:
            remote_read_rs232(amx, vaddr, number);
            break;
        case REMOTE_TCP:
            remote_read_tcp(amx, vaddr, number);
            break;
        default:
            return;
    }
}

void remote_write(AMX *amx,cell vaddr,int number) {
    switch(remote) {
        case REMOTE_RS232:
            remote_write_rs232(amx, vaddr, number);
            break;
        case REMOTE_TCP:
            remote_write_tcp(amx, vaddr, number);
            break;
        default:
            return;
    }
}

void remote_resume(void) {
    switch(remote) {
        case REMOTE_RS232:
            remote_resume_rs232();
            break;
        case REMOTE_TCP:
            remote_resume_tcp();
            break;
        default:
            return;
    }
}

void remote_sync(AMX *amx) {
    switch(remote) {
        case REMOTE_RS232:
            remote_sync_rs232(amx);
            break;
        case REMOTE_TCP:
            remote_sync_tcp(amx);
            break;
        default:
            return;
    }
}

int remote_wait(AMX *amx, long retries) {
    switch(remote) {
        case REMOTE_RS232:
            return remote_wait_rs232(amx, retries);
            break;
        case REMOTE_TCP:
            return remote_wait_tcp(amx, retries);
            break;
        default:
            return 1;
    }
}

int remote_transfer(const char *filename) {
    switch(remote) {
        case REMOTE_RS232:
            return remote_transfer_rs232(filename);
            break;
        case REMOTE_TCP:
            return remote_transfer_tcp(filename);
            break;
        default:
            amx_printf("\tRemote file transfer is not supported.\n");
            return 0;
    }
}

#endif // NO_REMOTE
