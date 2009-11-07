/*									tab:8
 *
 * relay.h - header file for TCP relay code distributed with ECE/CS F01 MP3
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
 * Creation Date:   Wed Sep 26 22:58:55 2001
 * Filename:	    relay.h
 * History:
 *	SL	2	Tue Oct  9 10:04:37 2001
 *		Added C++ wrappers.  Oops!
 *	SL	1	Wed Sep 26 22:58:55 2001
 *		First written.
 */

#ifndef RELAY_H
#define RELAY_H

#include <pthread.h>

#ifdef  __cplusplus
extern "C" {
#endif

/* process return values */
enum {EXIT_NORMAL, EXIT_ABNORMAL, EXIT_PARSE_OPTS, EXIT_PANIC};

#define MAX_PKT_LEN    256  /* limit on UDP packet length                */
#define MAX_CHANNELS   16   /* number of UDP channels supported in relay */

#define RELAY_SERVER_PORT  4321   /* default relay target port             */
#define WEB_SERVER_PORT    80     /* default forwarding target port (HTTP) */
#define SERVER_QUEUE       10     /* target TCP listen queue parameter     */
#define TIMEOUT_IN_SECONDS 5      /* timeout for reference implementation  */

#define INFTIM -1   /*  BH  I added this Sept. 2009 */ 


/* possible modes for relay (both sides use the same code) */
typedef enum {MODE_TCP_TARGET, MODE_TCP_FORWARD} relay_mode_t;

/* flags used to synchronize channel activation and deactivation between
   threads operating on the same channel */
typedef enum {
    CLOSE_CHANNEL_NONE     = 0,
    CLOSE_CHANNEL_HELPER   = 1,
    CLOSE_CHANNEL_RECEIVER = 2,
    CLOSE_CHANNEL_SENDER   = 4,
    CLOSE_CHANNEL_ALL      = 7
} channel_state_t;


/* unidirectional UDP channel data (each TCP connection uses two) */
typedef struct udp_channel_t udp_channel_t;
struct udp_channel_t {
    int fd;
    fq_t* recv;
    pthread_mutex_t recv_lock;
    pthread_cond_t recv_cond;
};


/* TCP relay channel data */
typedef struct channel_t channel_t;
struct channel_t {
    int epoch;  /* epoch number for channel; avoids confusion between
		   reuses (same effect as TCP's WAIT_STATE, but not timed) */
    int fd;     /* file descriptor for TCP connection */
    int active; /* used to signal channel activation between main thread
		   and tcp threads in relay target mode. */

    pthread_t helper_id;   /* thread id for tcp_helper thread; needed to
			      send signal to break out of poll call */

    channel_state_t channel_state;  /* deactivation synchronization state */
    pthread_mutex_t channel_lock;   /* lock on channel_state variable     */

    int need_help;             /* condition: helper should read from TCP */
    int has_data;              /* condition: data available on TCP       */
    pthread_cond_t help;       /* helper condition variable              */
    pthread_mutex_t help_lock; /* helper condition variable lock         */

    /* UDP channel 0 supports TCP send, UDP channel 1 supports TCP receive. */
    udp_channel_t udp[2];
};


/* 
   packet frame format access macros

   ---------------------------------------------------------
   | LAST(1b)/SEQ_NUM(7b) | EPOCH(1B) | up to 254B of data |
   ---------------------------------------------------------

   You'll need to add things like CRC and channel number and will
   probably want to expand the space of sequence numbers.
*/
#define PKT_IS_LAST(p) (((p)[0] & 0x80) == 0x80)
#define PKT_SEQ_NUM(p) ((int)((p)[0] & 0x7F))
#define PKT_EPOCH(p)   ((int)((p)[1]))
/* The braces make the macro into a single compound command. */
#define PKT_MAKE_HEADER(p,is_last,seq_num,epoch) \
{                                                \
    (p)[0] = ((is_last) << 7) | (seq_num);       \
    (p)[1] = (epoch);                            \
}
#define PREV_SEQ_NUM(n) (((n) + 0x7F) & 0x7F)
#define NEXT_SEQ_NUM(n) (((n) + 0x01) & 0x7F)
#define EPOCH_IS_EARLIER(e,f) \
	(((((unsigned)(f)) - ((unsigned)(e))) & 0xFF) <= 0x80)


#ifdef  __cplusplus
}
#endif

#endif /* WINDOW_H */
