/*									tab:8
 *
 * crc.c - function to calculate CRC checkbits for message using CRC-8 
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
 * Version:	    2
 * Creation Date:   Wed Sep 26 22:59:48 2001
 * Filename:	    crc.c
 * History:
 *	SL	2	Tue Oct  9 10:08:50 2001
 *		Added constant qualifier to buffer argument and included
 *              type definitions necessary for compilation.
 *	SL	1	Wed Sep 26 22:59:48 2001
 *		First written.
 */

#include <sys/types.h>

/* 
   Calculate CRC checkbits for <length> bytes in <buf>.  The use of
   a return value of the machine's "natural" integer size allows the 
   compiler to produce slightly more efficient code.
*/
unsigned 
calculate_crc8 (const char* buf, size_t length)
{
    unsigned bits = 0;
    int i;

    /* The CRC-8 generator polynomial is x^8 + x^2 + x + 1. */
    while (length-- > 0) {
        for (i = 8; --i >= 0; ) {
            bits = (bits << 1) | ((((unsigned)*buf) & (1 << i)) >> i);
            if (bits >= 0x100)
                bits ^= 0x107;
        }
        buf++;
    }

    /* Add eight zeroes to the end of the message (multiply by x^8). */
    for (i = 8; --i >= 0; ) {
        bits <<= 1;
        if (bits >= 0x100)
            bits ^= 0x107;
    }

    return bits;
}

