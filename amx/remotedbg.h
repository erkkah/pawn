/* Pawn RS232 remote debugging interface
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

/* For the remote debugging capabilities, the "device" is the apparatus/PC on
 * which the script runs and the "host" is the PC on which the debugger runs.
 * Both the device and the host load the script. The device typically does not
 * load the debugging info. The host would not really need to load the opcodes,
 * but its task becomes a lot simpler when it has locally an image of what runs
 * on the device.
 *
 * Handshake
 *
 * The first step, after opening the connection, is the handshake. For that,
 * the host must send a packet with the single character "�" (ASCII 161). Since
 * the device may not be "on-line" immediately and packets may get lost, the
 * host should repeat sending these characters until the device reponds.
 *
 * When the device starts up and loads a script, it should check whether this
 * script contains debugging information (even if it does not load the debugging
 * information). If debugging information is present, it should check for the
 * reception of packets with the character "�". Upon reception, it replies with
 * the character pair "�]". The device should have a time-out on the polling
 * loop for receivng the "�" packets, because the script should run normally
 * when no debugger is attached.
 *
 * Running
 *
 * After entering in debug mode, the device runs the script, but when it drops
 * at a "BREAK" instruction, it sends a packet with the character "�" followed
 * by the new instruction pointer address (CIP) and a terminating "]" character.
 * The device then waits for a response from the host (the script is halted). To
 * continue running, the host simply sends a packet with the single character
 * "!".
 *
 * If, instead of allowing the device to continue running the script, you want
 * to halt execution (e.g. because a breakpoint is reached), the first command
 * that the host should send is the string "?R\n", where \n stands for a
 * newline. Upon reception, the device must reply with a more complete state of
 * the abstract machine: the stack address, the frame pointer and the heap top.
 *
 * With the few registers sent over, the state of the abstract machine (on the
 * device) is now known to the host, but it does not know the contents of the
 * data memory on the device: the values of global and local variables. It was
 * estimated too costly to transfer all of this data memory at every "stop".
 * Instead, the host must query for the range of memory that it wants. It does
 * so by sending a packet with the contents:
 *
 *          ?Maddress,size\n
 *
 * where "address" and "size" are hexadecimal numbers. The size is in cells;
 * the address does not have to be cell-aligned, but it typically is. The
 * device responds to this command by sending a packet with an "�" character
 * followed by one or more hexadecimal values, separated with commas and
 * terminated with a "]".
 *
 * Other (suggested) commands are:
 *
 *          ?M address,size
 *          ?R              (registers) send the status of FRM, STK and HEA
 *
 *          ?G name\n       (get) retrieve file
 *          ?P size,name\n  (put) send over new script (or other file)
 *          ?B value\n      (baud) set baud rate (restarts the connection)
 *          ?L\n            (list) retrieve a list of files on the device
 *
 *          ?U\n            (unhook) close the debugger down
 *          ?U*\n           unhook the debugger and restart (do this after a transfer of an updated script)
 *
 * When sending over scripts (or perhaps other files), the reply of the
 * ?P command carries the block size that the debugger should use to send
 * the data (like all numbers in the debugger interface, this value is in
 * hexadecimal). For example, if the remote host replies with "�100]", the
 * debugger should transfer the file in blocks of 256 bytes. The file itself
 * is then sent as binary data (and in blocks). After each block, the debugger
 * must wait for the reply.
 *
 * Before sending the block, the debugger sends a "start code". This code is
 * either an ACK (ASCII 6) or a NAK (ASCII 21). If the debugger sends an ACK,
 * the block that follows is the next sequential block of data for the file. If
 * it sends a NAK, the block that follows is a repeated send of the preceding
 * block. The debugger should resend a block on a checksum mismatch. After
 * sending a block, the remote host replies. The checksum of the block is in
 * the value that follows the � sign. Due to the way that the checksum is
 * computed, the checksum is never zero. The debugger can now compare the
 * checksum with the one it calculated itself. On a mismatch, the debugger can
 * then send a NAK and resend the block.
 *
 * The checksum is the "Internet checksum", but as an 8-bit variant. This
 * checksum is often called the "one's complement", because it wraps the carry
 * around on overflow.
 *
 *
 * Notes
 *
 * The rationale for the debugger communication protocol is performance, and
 * especially attempting to avoid that the communication link becomes the
 * bottleneck. Therefore, the device and the host send over as little as
 * possible until it is known that the device should be halted. In the case
 * of a serial link, data is sent byte by byte. When the device is halted (on
 * a breakpoint) and waiting for user interaction, performance is no longer
 * as important.
 *
 * There is a special case in the handshaking stage when the device is already
 * running in debug mode (because an earlier session was aborted). In that case,
 * it will respond with "�" and the new instruction pointer (CIP) address.
 */


#include "amx.h"

int remote_rs232(const char *port, int baud);
int remote_tcp(const char *host, int port);

void remote_close();
void remote_read(AMX *amx,cell vaddr,int number);
void remote_write(AMX *amx,cell vaddr,int number);
void remote_resume(void);
void remote_sync(AMX *amx);
int remote_wait(AMX *amx, long retries);
int remote_transfer(const char *filename);

cell *VirtAddressToPhys(AMX *amx,cell amx_addr);
const char *skippath(const char *str);

extern int remote;

enum {
  REMOTE_NONE,  /* this means "not remote" */
  REMOTE_RS232,
  REMOTE_TCP,
  REMOTE_USB,   /* ??? not implemented */
  /* --- */
  REMOTE_TYPES
};
