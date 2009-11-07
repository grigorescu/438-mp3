/*  Steve Lumetta requested that this code NOT be distributed to students or
 *    outside U. Illinois.   --B. Hajek, Sept 2009
 */
/*									tab:8
 *
 * mp3.h - header file for Fall 2001 ECE/CS 338 machine problem 3
 *
 * "Copyright (c) 2001 by Steven S. Lumetta."
 *
 * Permission to use, copy, modify, and distribute this software and its
 * documentation for any purpose, without fee, and without written agreement is
 * hereby granted, provided that the above copyright notice and the following
 * two paragraphs appear in all copies of this software.
 * 
 * IN NO EVENT SHALL THE AUTHOR OR THE UNIVERSITY OF ILLINOIS BE LIABLE TO 
 * ANY PARTY FOR DIRECT, INDIRECT, SPECIAL, INCIDENTAL, OR CONSEQUENTIAL 
 * DAMAGES ARISING OUT  OF THE USE OF THIS SOFTWARE AND ITS DOCUMENTATION, 
 * EVEN IF THE AUTHOR AND/OR THE UNIVERSITY OF ILLINOIS HAS BEEN ADVISED 
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 * 
 * THE AUTHOR AND THE UNIVERSITY OF ILLINOIS SPECIFICALLY DISCLAIM ANY 
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF 
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE.  THE SOFTWARE 
 * PROVIDED HEREUNDER IS ON AN "AS IS" BASIS, AND NEITHER THE AUTHOR NOR
 * THE UNIVERSITY OF ILLINOIS HAS NO OBLIGATION TO PROVIDE MAINTENANCE, 
 * SUPPORT, UPDATES, ENHANCEMENTS, OR MODIFICATIONS."
 *
 * Author:	    Steve Lumetta
 * Version:	    1
 * Creation Date:   Wed Sep 26 22:57:59 2001
 * Filename:	    mp3.h
 * History:
 *	SL	1	Wed Sep 26 22:57:59 2001
 *		First written.
 */

#ifndef _MP3_H_
#define _MP3_H_

#include <sys/types.h>
#include <sys/socket.h>

/* Read the handout for details on the functions. */

void mp3_init (int* argc, char*** argv);
ssize_t mp3_recvfrom (int s, void* buf, size_t len, int flags,
		      struct sockaddr* from, size_t* fromlen);

#endif
