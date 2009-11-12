/*									Tab:8
 *
 * relay.c - source file for TCP relay distributed for ECE/CS 338 F01 MP3
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
 * Creation Date:   Wed Sep 26 23:01:51 2001
 * Filename:	    relay.c
 * History:
 *      Bruce Hajek      3       Sunday Sept 27 21:25:00 2009
                Minor changes to adapt to EWS linux machines
 *	SL	2	Tue Oct  9 10:12:28 2001
 *		Quelled compiler confusion about uninitialized variable.
 *	SL	1	Wed Sep 26 23:01:51 2001
 *		First written.
 */

#include <pthread.h>

#include <errno.h>
#include <netdb.h>
#include <poll.h>
#include <semaphore.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/socket.h>
#include <sys/stropts.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>
#include <string.h>

#include "fq.h"
#include "relay.h"
#include "mp3.h"
#include "crc.c"

#define SWP_BUFFER_SIZE 32  // Sliding Window Protocol buffer size

udp_channel_t* udpchans [2*MAX_CHANNELS];

/* A few useful wrapper functions for Posix calls.  They kill the process
   when an error occurs.   */
static void condition_signal (pthread_cond_t* cond);
static int condition_timedwait (pthread_cond_t* cond, pthread_mutex_t* lock);
static void condition_wait (pthread_cond_t* cond, pthread_mutex_t* lock);
static void get_lock (pthread_mutex_t* lock);
static void release_lock (pthread_mutex_t* lock);

/* An empty signal handler that allows us to interrupt the poll call in the
   TCP helper thread.  */
static void sig_empty (int ignore);

/* Syntax printing routine. */
static void usage (const char* exec_name);

/* A few utility functions. */
static int create_udp_socket (int port, struct sockaddr_in* peer_addr);
static void deactivate_channel (channel_t* ct, channel_state_t flag);
static void init_channels (pthread_attr_t* attr, int base_port,
			   struct sockaddr_in* peer_addr);
static int my_write (int fd, const void* buf, size_t n);
static void open_and_activate_channel (channel_t* ct);
static int set_up_target_socket (short int target_port);
static void udp_init (udp_channel_t* uct, int filedes);
static void wake_threads (channel_t* ct, channel_state_t flag);

/* Thread main functions. */
static void* tcp_helper (void* v_ct);
static void* tcp_receiver (void* v_ct);
static void* tcp_sender (void* v_ct);
static void* udp_receiver (void* v_uct);


/* mode of operation: either MODE_TCP_TARGET or MODE_TCP_FORWARD */
relay_mode_t mode;

/* semaphore counting unbound (inactive) channels in target mode */
sem_t channel_semaphore;            

/* channel table */
channel_t chan_tab[MAX_CHANNELS];

/* forwarding address in forward mode */
struct sockaddr_in fwd_addr;


int
main (int argc, char** argv)
{
    int fd = -1, cli_fd, i;
    socklen_t addr_size;
    struct sockaddr_in peer_addr;
    pthread_attr_t attr;
    unsigned short tcp_port, base_port;
    struct sockaddr_in cli_addr;
    struct hostent* he;

    /* Allow MP3 adversary code to extract its parameters from command line. */
    mp3_init (&argc, &argv);

    /* Remaining arguments must be the executable name, peer domain name,
       base UDP port, "target" or forwarding target domain name, and an 
       optional TCP port number. */
    if (argc < 4 || argc > 5) {
	usage (argv[0]);
	return EXIT_PARSE_OPTS;
    }

    /* Find relay peer address. */
    if ((he = gethostbyname (argv[1])) == NULL) {
        fprintf (stderr, "peer \"%s\" unknown\n", argv[1]);
	usage (argv[0]);
        return EXIT_PARSE_OPTS;
    }
    peer_addr.sin_family = AF_INET;
    peer_addr.sin_addr = *(struct in_addr*)he->h_addr;

    /* Read base UDP port number. */
    base_port = atoi (argv[2]);

    /* Set mode and do mode-specific parsing. */
    if (strcmp (argv[3], "target") == 0) {
	mode = MODE_TCP_TARGET;
	tcp_port = (argc == 5 ? atoi (argv[4]) : RELAY_SERVER_PORT);
	fd = set_up_target_socket (tcp_port);
    } else {
	mode = MODE_TCP_FORWARD;
	tcp_port = (argc == 5 ? atoi (argv[4]) : WEB_SERVER_PORT);
	/* Find forwarding target address. */
	if ((he = gethostbyname (argv[3])) == NULL) {
	    fprintf (stderr, "forwarding target \"%s\" unknown\n", argv[3]);
	    usage (argv[0]);
	    return EXIT_PARSE_OPTS;
	}
	fwd_addr.sin_family = AF_INET;
	fwd_addr.sin_addr = *(struct in_addr*)he->h_addr;
	fwd_addr.sin_port = htons (tcp_port);
    }

    /* Ignore broken pipes. */
    signal (SIGPIPE, SIG_IGN);

    /* User signal sent internally to break tcp_helper threads out
       of poll operation.  Results are unpredictable if such a signal
       is sent by another process.   [ sigset is obsolete and should
       be replaced by sigaction as described in Stevens in future.
       It causes a warning on compilation but still works.  --BH Sept. 2009 ] */
   sigset (SIGUSR1, sig_empty);

    /* Prepare to spawn threads. */
    if (pthread_attr_init (&attr) != 0 ||
        pthread_attr_setdetachstate (&attr, PTHREAD_CREATE_DETACHED) != 0) {
        fputs ("pthread attribute initialization failed\n", stderr);
	return EXIT_PANIC;
    }

    /* Initialize channels. */
    init_channels (&attr, base_port, &peer_addr);

    /* The main thread serves no purpose in forward mode; exit now. */
    if (mode == MODE_TCP_FORWARD)
	pthread_exit (0);

    while (1) {
	/* Accept a new connection. */
	addr_size = sizeof (struct sockaddr_in);
	if ((cli_fd = accept (fd, (struct sockaddr*)&cli_addr,
			      &addr_size)) == -1) {
	    perror ("accept");
	    return EXIT_PANIC;
	}

	/* Wait for a channel if necessary. */
	if (sem_wait (&channel_semaphore) == -1) {
	    perror ("sem_wait for new channel");
	    return EXIT_PANIC;
	}

	/* Find an inactive channel (one must exist). */
	for (i = 0; chan_tab[i].active == 1; i++);

	/* Lock should not be contended, but lock guarantees proper 
	   memory ordering between channel state update and updates
	   of other data values (threads are always running). */
	get_lock (&chan_tab[i].channel_lock);
	chan_tab[i].fd = cli_fd;
	chan_tab[i].need_help = 0;
	chan_tab[i].has_data = 0;
	chan_tab[i].active = 1;
	release_lock (&chan_tab[i].channel_lock);
	chan_tab[i].channel_state = CLOSE_CHANNEL_NONE;

	/* Wake up sleeping threads. */
	wake_threads (&chan_tab[i], CLOSE_CHANNEL_NONE);
    }
}


/*
    Translate errors in pthread_cond_signal to a printed message and
    process exit.
*/
static void
condition_signal (pthread_cond_t* cond)
{
    int rv;

    if ((rv = pthread_cond_signal (cond)) != 0) {
	fprintf (stderr, "condition signal failed with code %d\n", rv);
	exit (EXIT_PANIC);
    }
}


/*
    Perform a timed wait of TIMEOUT_IN_SECONDS seconds on condition
    variable <cond> using mutex <lock>.  Translate errors other than
    ETIMEDOUT (timeout) in pthread_cond_timedwait to a printed message 
    and process exit.  Note that a timeout in seconds is not appropriate 
    for a sliding window protocol over a local area network, and you
    probably want the timeout to be a parameter, not a fixed value.
*/
static int
condition_timedwait (pthread_cond_t* cond, pthread_mutex_t* lock)
{
    struct timespec ts;
    int rv;

    if (clock_gettime (CLOCK_REALTIME, &ts) == -1) {
	perror ("clock_gettime");
	exit (EXIT_PANIC);
    }
    ts.tv_sec += TIMEOUT_IN_SECONDS;
    if ((rv = pthread_cond_timedwait (cond, lock, &ts)) != 0) {
	if (rv == ETIMEDOUT)
	    return ETIMEDOUT;
	fprintf (stderr, "condition wait failed with code %d\n", rv);
	exit (EXIT_PANIC);
    }
    return 0;
}


/*
    Translate errors in pthread_cond_wait to a printed message and
    process exit.
*/
static void
condition_wait (pthread_cond_t* cond, pthread_mutex_t* lock)
{
    int rv;

    if ((rv = pthread_cond_wait (cond, lock)) != 0) {
	fprintf (stderr, "condition wait failed with code %d\n", rv);
	exit (EXIT_PANIC);
    }
}


/*
    Translate errors in pthread_mutex_lock to a printed message and
    process exit.
*/
static void
get_lock (pthread_mutex_t* lock)
{
    int rv;

    if ((rv = pthread_mutex_lock (lock)) != 0) {
	fprintf (stderr, "lock function failed with code %d\n", rv);
	exit (EXIT_PANIC);
    }
}


/*
    Translate errors in pthread_mutex_unlock to a printed message and
    process exit.
*/
static void
release_lock (pthread_mutex_t* lock)
{
    int rv;

    if ((rv = pthread_mutex_unlock (lock)) != 0) {
        fprintf (stderr, "unlock failed with code %d\n", rv);
	exit (EXIT_PANIC);
    }
}


/* 
   Print a log line to stderr, preceded by a timestamp.
   Older version used cftime so I edited this.  --BH
*/
static void
printlog (const char *fmt, ...)
{
    if (1)  {
         char buf[100];
         int len;
         va_list args;
         time_t tim = time (NULL);
         struct tm *now=localtime(&tim);

          /* Check format parameter. */
          if (fmt == NULL)
          return;

          /* Read formated string to buffer. */
          va_start (args, fmt);
          len = vsnprintf (buf , sizeof (buf) , fmt, args);
          va_end (args);

          /* Send time and formated string to stderr. */
          fprintf (stderr, "%02d:%02d:%02d  %s\n", now->tm_hour, now->tm_min, now->tm_sec, buf);
    } /* eo if() */
}





/*
   Empty signal handler.  Allows system calls to return -1 and to set 
   errno to EINTR.
*/
static void
sig_empty (int ignore)
{
}


/*
    Print proper command line syntax to stderr, given executable name 
    <exec_name>.
*/
static void
usage (const char* exec_name)
{
    fprintf (stderr, "syntax: %s <peer> <base UDP port> target|<forward "
	     "target> [<TCP port>]\n", exec_name);
    fprintf (stderr, "   (TCP port defaults to %d for target, %d for "
	     "forwarding target)\n", RELAY_SERVER_PORT, WEB_SERVER_PORT);
}


/*
   Main body of the TCP helper threads.
*/
static void* 
tcp_helper (void* v_ct)
{
    channel_t* ct = v_ct;
    udp_channel_t* uct = &ct->udp[0];
    struct pollfd pfds[1];
    int pval;

    printlog ("%#08X INIT TCP_HELPER", (unsigned int)ct);

    pfds[0].events = POLLIN;

    while (1) {
	/* Wait for channel to become active. */
	get_lock (&ct->help_lock);
	while ((ct->channel_state & CLOSE_CHANNEL_HELPER) != 0)
	    condition_wait (&ct->help, &ct->help_lock);
	release_lock (&ct->help_lock);
	printlog ("%#08X ACTIVATE TCP_HELPER", (unsigned int)ct);

	/* Copy new connection file descriptor into poll structure. */
	pfds[0].fd = ct->fd;

	while (1) {
	    /* Check for another thread requesting shutdown. */
	    if (ct->channel_state != CLOSE_CHANNEL_NONE) {
		deactivate_channel (ct, CLOSE_CHANNEL_HELPER);
		printlog ("%#08X DEACTIVATE TCP_HELPER", (unsigned int)ct);
		break;
	    }

	    /* If data requested, wait for some to arrive. */
	    if (ct->need_help) {
		if ((pval = poll (pfds, 1, INFTIM)) < 1) {
		    /* A return value of 0 is impossible. */
		    if (errno != EINTR) {
			perror ("poll");
			exit (EXIT_PANIC);
		    }
		    printlog ("%#08X POLL INTERRUPTED IN TCP_HELPER",
			  (unsigned int)ct);
		    continue;
		}

		/* Data available from TCP--wake up the sender thread. */
		if (pval == 1 && (pfds[0].revents & POLLIN) != 0) {
		    get_lock (&uct->recv_lock);
		    ct->need_help = 0;
		    ct->has_data = 1;
		    printlog ("%#08X WAKING TCP_SENDER FROM TCP_HELPER", 
			  (unsigned int)ct);
		    condition_signal (&uct->recv_cond);
		    release_lock (&uct->recv_lock);
		}
	    }

	    /* Wait for request for more data or deactivation of channel. */
	    get_lock (&ct->help_lock);
	    while (!ct->need_help &&
		   ct->channel_state == CLOSE_CHANNEL_NONE)
		condition_wait (&ct->help, &ct->help_lock);
	    release_lock (&ct->help_lock);
	}
    }
}


/*
   Main body of the TCP sender threads.
*/
static void* 
tcp_sender (void* v_ct)
{
    channel_t* ct = v_ct;
    udp_channel_t* uct = &ct->udp[0];
    unsigned char packet[MAX_PKT_LEN];
    int len, LAR = 0, SEQ = 0, seq_num, epoch;
    int is_active = 0, tcp_closed = 0, timeout = 0;
    fq_err_t rv;

    unsigned char buffer[MAX_PKT_LEN*SWP_BUFFER_SIZE];
    unsigned char bufferValid[SWP_BUFFER_SIZE] = {0};
    int i;

    printlog ("%#08X INIT TCP_SENDER", (unsigned int)ct);

    while (1) {
	/* Check for changes in channel state. */
	if (!is_active) {
	    if ((ct->channel_state & CLOSE_CHANNEL_SENDER) == 0) {
		printlog ("%#08X ACTIVATE TCP_SENDER", (unsigned int)ct);
		is_active = 1;
		/* Reset sequence number and LAR. */
		SEQ = 0;
		LAR = PREV_SEQ_NUM (0);
		tcp_closed = 0;
		timeout = 0;

		for (i=0; i < SWP_BUFFER_SIZE; i+=1){
		  bufferValid[i] = 0;
		}

		continue;
	    }
	} else if (ct->channel_state != CLOSE_CHANNEL_NONE) {
	    deactivate_channel (ct, CLOSE_CHANNEL_SENDER);
	    printlog ("%#08X DEACTIVATE TCP_SENDER", (unsigned int)ct);
	    is_active = 0;
	    continue;
	}

	/* Read any available data and send it out.  This code should
	   try to drain the socket, or at least pull out more than one
	   packet if possible.  Calling fcntl FIONREAD would let us
	   check for additional data without the possibility of blocking;
	   calling poll or select also works.  Instead of a reasonable
	   approach, the code here reads a packet and waits for the
	   code below to wake up the tcp_helper and for that thread to
	   mark data as available.  */
	if (is_active && ct->has_data) {
	    ct->has_data = 0;

	    /* Read data from the TCP connection.  Deactivate channel
	       if any error occurs. */
	    if ((len = read (ct->fd, packet + 4, MAX_PKT_LEN - 5)) < 0) {
		deactivate_channel (ct, CLOSE_CHANNEL_SENDER);
		printlog ("%#08X READ FAILED IN TCP_SENDER", (unsigned int)ct);
		is_active = 0;
		continue;
	    }

	    /* Check for TCP connection closure. */
	    if (len == 0)
		tcp_closed = 1;
	    printlog(" ^^^^^^^^^^ ZEROING OUT REST OF PACKET, FROM %d TO %d", len+4, MAX_PKT_LEN-1);
	    /* Fill in the header. */
	    for (i = len+4; i<MAX_PKT_LEN-1; i+=1) 
	      packet[i] = 0;                       //zero out the rest
	    PKT_MAKE_HEADER (packet, 0, tcp_closed, ct->number, SEQ, ct->epoch, len);
	    SEQ = NEXT_SEQ_NUM (SEQ);
	    /* Send the packet, ignoring errors. */
	    (void)send (uct->fd, packet, 256, 0);
	    printlog ("%#08X TCP_SENDER SENT PACKET %02X:%02X%s(%d bytes)",
		  (unsigned int)ct, PKT_EPOCH (packet), PKT_SEQ_NUM (packet), 
		  (PKT_IS_LAST (packet) ? " LAST " : " "), 256);
	}

	/* Check for incoming ACK on queue. */
	len = MAX_PKT_LEN;
	if ((rv = fq_dequeue (uct->recv, packet, &len)) != FQ_OK) {

	  if (rv == FQ_QUEUE_EMPTY/* && !bufferValid[0]*/) {
		/* Empty queue; may need to wake tcp_helper to make data 
		   available. */
		if (!tcp_closed) {
		    get_lock (&ct->help_lock);
		    ct->need_help = 1;
		    condition_signal (&ct->help);
		    release_lock (&ct->help_lock);
		}
		
		/* Wait for an ACK or other wakeup event. */
		get_lock (&uct->recv_lock);
		len = MAX_PKT_LEN;
		while (((is_active && 
			 ct->channel_state == CLOSE_CHANNEL_NONE) ||
			(!is_active && 
			 (ct->channel_state & CLOSE_CHANNEL_SENDER) != 0)) &&
		       !ct->has_data &&
		       (rv = fq_dequeue (uct->recv, packet, &len)) == 
			       FQ_QUEUE_EMPTY) {
		    if (!is_active || LAR == PREV_SEQ_NUM (SEQ))
			condition_wait (&uct->recv_cond, &uct->recv_lock);
		    else if (condition_timedwait (&uct->recv_cond, 
						  &uct->recv_lock) != 0) {
			timeout = 1;
			break;
		    }
		    len = MAX_PKT_LEN;
		}
		release_lock (&uct->recv_lock);
		if (is_active && timeout) {
		    deactivate_channel (ct, CLOSE_CHANNEL_SENDER);
		    printlog ("%#08X TIMEOUT IN TCP_SENDER", (unsigned int)ct);
		    is_active = 0;
		    continue;
		}
	    }

	    /* Still no packet?  Check for errors, or restart loop for
	       data arrival and channel activation changes. */
	  if (rv != FQ_OK /*&& !bufferValid[0]*/) {
		/* Check for failure caused by something besides an 
		   empty queue. */
		if (rv != FQ_QUEUE_EMPTY) {
		    fq_error ("fq_dequeue failed in tcp_sender", rv);
		    exit (EXIT_PANIC);
		}
		/* No packet, no failure; restart loop. */
		continue;
	    }
	}

	/*	if (bufferValid[0]){
	  for (i=0; i<MAX_PKT_LEN; i++)
	    packet[i] = buffer[i];
	  len = PKT_LENGTH(packet);
	  }*/
		
	/* Discard if too short (should never happen). */
	if (len < 2) 
	    continue;

	/* We've got an ACK. */
	printlog ("%#08X TCP_SENDER GOT ACK %02X:%02X%s(%d bytes)",
	      (unsigned int)ct, PKT_EPOCH (packet), PKT_SEQ_NUM (packet), 
	      (PKT_IS_LAST (packet) ? " LAST " : " "), len);

	/* Discard silently when inactive and when packets have bad epoch. */
	if (!is_active || (epoch = PKT_EPOCH (packet)) != ct->epoch)
	    continue;
	
	/* Advance LAR by 1 to the value we are expecting to receive. */
	LAR = NEXT_SEQ_NUM (LAR);

        // Move buffers down

	for (i=0; i < SWP_BUFFER_SIZE-1; i+=1){
	  buffer[MAX_PKT_LEN*i] = buffer[MAX_PKT_LEN*(i+1)];
	  bufferValid[i] = bufferValid[i+1];
	}
	bufferValid[SWP_BUFFER_SIZE-1] = 0;

        /* Out of order packets can be handled more aggressively. */
	if (((seq_num = PKT_SEQ_NUM (packet)) < LAR) || (seq_num > LAR+SWP_BUFFER_SIZE)){
	    deactivate_channel (ct, CLOSE_CHANNEL_SENDER);
	    printlog ("%#08X OUT OF ORDER OR DUPLICATE ACK IN TCP_SENDER",
		  (unsigned int)ct);
	    is_active = 0;
	    continue;
	}

	if ((seq_num >= LAR) || (seq_num < LAR+SWP_BUFFER_SIZE))
	  {
	    printlog("%#08X PUTTING A PACKET INTO SEND BUFFER SLOT %d", (unsigned int)ct, LAR-seq_num);
	    buffer[(LAR-seq_num)*MAX_PKT_LEN] = packet;
	    bufferValid[(LAR-seq_num)] = 1;
	  }

	/* Finally, if we've gotten the ACK for the last packet,
	   we're done. */
	if (PKT_IS_LAST (packet) && (LAR == seq_num)) {
	    deactivate_channel (ct, CLOSE_CHANNEL_SENDER);
	    printlog ("%#08X STREAM SEND COMPLETED IN TCP_SENDER",
		  (unsigned int)ct);
	    is_active = 0;
	    continue;
	}
    }
}


/*
   Main body of the TCP receiver threads.
*/
static void* 
tcp_receiver (void* v_ct)
{
    channel_t* ct = v_ct;
    udp_channel_t* uct = &ct->udp[1];
    unsigned char packet[MAX_PKT_LEN];
    int len, NFE = 0, seq_num, epoch;
    int is_active = 0;
    fq_err_t rv;

    unsigned char buffer[SWP_BUFFER_SIZE*MAX_PKT_LEN];
    unsigned char bufferValid[SWP_BUFFER_SIZE] = {0};
    int i;
    

    printlog ("%#08X INIT TCP_RECEIVER", (unsigned int)ct);

    while (1) {
	/* Check for changes in channel state. */
	if (!is_active) {
	    /* The main thread at the target end of the relay activates 
	       channels in response to accepted TCP connections.  The 
	       forwarding end does so in response to the first packet
	       with the current epoch number. */
	    if ((ct->channel_state & CLOSE_CHANNEL_RECEIVER) == 0) {
		if (mode != MODE_TCP_TARGET) {
		    fputs ("channel activated incorrectly in tcp_receiver\n",
			   stderr);
		    exit (EXIT_PANIC);
		}
		printlog ("%#08X ACTIVATE TCP_RECEIVER", (unsigned int)ct);
		is_active = 1;
		/* Reset NFE. */
		NFE = 0;

		for (i=0; i < SWP_BUFFER_SIZE; i+=1){
		  bufferValid[i] = 0;
		}
		continue;
	    }
	} else if (ct->channel_state != CLOSE_CHANNEL_NONE) {
	    deactivate_channel (ct, CLOSE_CHANNEL_RECEIVER);
	    printlog ("%#08X DEACTIVATE TCP_RECEIVER", (unsigned int)ct);
	    is_active = 0;
	    continue;
	}

	/* Check for incoming message on queue. */
	len = MAX_PKT_LEN;
	if ((rv = fq_dequeue (uct->recv, packet, &len)) != FQ_OK) {

	  if (rv == FQ_QUEUE_EMPTY/* && !bufferValid[0]*/) {
		/* Empty queue: wait for a packet or other wakeup event. */
		get_lock (&uct->recv_lock);
		len = MAX_PKT_LEN;
		while (((is_active && 
			 ct->channel_state == CLOSE_CHANNEL_NONE) ||
			(!is_active && 
			 (ct->channel_state & CLOSE_CHANNEL_RECEIVER) != 0)) &&
		       (rv = fq_dequeue (uct->recv, packet, &len)) == 
			       FQ_QUEUE_EMPTY) {
		    condition_wait (&uct->recv_cond, &uct->recv_lock);
		    len = MAX_PKT_LEN;
		}
		release_lock (&uct->recv_lock);
	    }

	    /* Still no packet?  Check for errors, or restart loop for
	       channel activation changes. */
	  if (rv != FQ_OK/* && !bufferValid[0]*/) {
		/* Check for failure caused by something besides an 
		   empty queue. */
		if (rv != FQ_QUEUE_EMPTY) {
		    fq_error ("fq_dequeue failed in tcp_receiver", rv);
		    exit (EXIT_PANIC);
		}

		/* No packet, no failure; restart loop. */
		continue;
	    }
	}

	/*	if (bufferValid[0]){
	  for (i=0; i<MAX_PKT_LEN; i++)
	    packet[i] = buffer[i];
 	  len = PKT_LENGTH(packet);
	  }*/

	/* Discard if too short (should never happen). */
	if (len < 2) 
	    continue;

	/* We've got a packet. */
	printlog ("%#08X TCP_RECEIVER GOT PACKET %02X:%03X ON CHANNEL %02X %s(%d bytes)",
		  (unsigned int)ct, PKT_EPOCH (packet), PKT_SEQ_NUM (packet), PKT_CHAN_NUM(packet),
	      (PKT_IS_LAST (packet) ? " LAST " : " "), len);
        if (mode == MODE_TCP_TARGET) {
	    /* Discard packets received when inactive, and discard packets
	       with the incorrect epoch number.  The response when inactive
	       permits an unlikely scenario in which a new channel is
	       activated and the end-to-end connection is fully established 
	       and receives data (i.e., a packet) before the tcp_receiver 
	       thread manages to activate itself.  As this problem (the 
	       lost packet) becomes irrelevant once reliable delivery is 
	       added, it's not worth adding another synchronization round 
	       to verify channel activation. */
	  
	  if (!is_active || (epoch = PKT_EPOCH (packet)) != ct->epoch)
		continue;
	} else {
	    /* Forwarding mode: the first packet received for this epoch,
	       and any packet received for a subsequent epoch, should
	       create a new TCP connection (deactivate and reactivate
	       the channel). */
	    if ((epoch = PKT_EPOCH (packet)) != ct->epoch) {

		/* Discard packets from earlier epochs. */
		if (EPOCH_IS_EARLIER (epoch, ct->epoch))
		    continue;

		/* Newer epoch received.  Deactivate channel if necessary. */
		if (is_active) {
		    printlog ("%#08X NEW EPOCH DEACTIVATION IN TCP_RECEIVER",
			  (unsigned int)ct);
		    deactivate_channel (ct, CLOSE_CHANNEL_RECEIVER);
		    is_active = 0;

		    /* Wait for deactivation to finish. */
		    get_lock (&uct->recv_lock);
		    while (ct->channel_state != CLOSE_CHANNEL_ALL)
			condition_wait (&uct->recv_cond, &uct->recv_lock);
		    release_lock (&uct->recv_lock);

		    /* Update epoch number. */
		    ct->epoch = epoch;
		}
	    } 

	    /* If the channel is inactive, open a TCP connection and mark
	       the channel as active. */
	    if (!is_active) {
		printlog ("%#08X FIRST EPOCH PACKET ACTIVATION IN TCP_RECEIVER",
		      (unsigned int)ct);
		open_and_activate_channel (ct);
		is_active = 1;
		/* Reset NFE. */
		NFE = 0;
	    }
	}

	/* Is the packet the one we are expecting? */
	if (((seq_num = PKT_SEQ_NUM (packet)) >= NFE) && (seq_num < NFE+SWP_BUFFER_SIZE)) {
	  if (NFE == seq_num)
	    {
	      NFE = NEXT_SEQ_NUM (seq_num);
	      
	      // Move buffers down
	      
	      for (i=0; i < SWP_BUFFER_SIZE-1; i+=1){
		buffer[MAX_PKT_LEN*i] = buffer[MAX_PKT_LEN*(i+1)];
		bufferValid[i] = bufferValid[i+1];
	      }
	      bufferValid[SWP_BUFFER_SIZE-1] = 0;

	      /* If so, write packet to TCP socket. */
	      if (my_write (ct->fd, packet + 4, len - 5) != len - 5) {
		/* Write failed!  Close the connection. */
		printlog ("%#08X WRITE FAILED IN TCP_RECEIVER", (unsigned int)ct);
		deactivate_channel (ct, CLOSE_CHANNEL_RECEIVER);
		is_active = 0;
		continue;
	      }
	    }
	  else
	    {
	      printlog("%#08X PUTTING A PACKET INTO RECV BUFFER SLOT %d", (unsigned int)ct, NFE-seq_num);
	      buffer[(NFE-seq_num)*MAX_PKT_LEN] = packet;
	      bufferValid[(NFE-seq_num)] = 1;
	    }
	} else {
	    /* Out of order packet!  Construct an ACK for the previous
	       sequence number. */
	  PKT_MAKE_HEADER (packet, 1, 0, ct->number, PREV_SEQ_NUM (seq_num), epoch, len);

	}

	/* Send an ACK.  With our structures, we can simply return
	   the first three bytes of the packet.  We ignore errors. */
	PKT_MAKE_HEADER (packet, 1, 0, ct->number, seq_num, epoch, len);

	(void)send (uct->fd, packet, 256, 0);
	printlog ("%#08X TCP_RECEIVER SENT ACK %02X:%02X%s(256 bytes)",
	      (unsigned int)ct, PKT_EPOCH (packet), PKT_SEQ_NUM (packet), 
	      (PKT_IS_LAST (packet) ? " LAST " : " "));

	/* Was the packet received the last? */
	if (PKT_IS_LAST (packet) && NFE == seq_num) {
	    printlog ("%#08X RECEIVED LAST PACKET IN TCP_RECEIVER",
		  (unsigned int)ct);
	    deactivate_channel (ct, CLOSE_CHANNEL_RECEIVER);
	    is_active = 0;
	    continue;
	}
    }
}


/*
   Main body of the UDP receiver threads.
*/
static void* 
udp_receiver (void* v_uct)
{
    udp_channel_t* uct = v_uct;
    unsigned char packet[MAX_PKT_LEN];
    int len;
    fq_err_t rv;
    int trash;
    socklen_t tlen;

    int chanNum;

    printlog ("%#08X INIT UDP_RECEIVER", (unsigned int)uct);

    while (1) {
	/* Ignore errors. */
	tlen = sizeof (trash);
	if ((len = mp3_recvfrom (uct->fd, packet, MAX_PKT_LEN, 0,(struct sockaddr*)&trash, &tlen)) >= 0) 
	  {
	    chanNum = PKT_CHAN_NUM(packet);
	    printlog("XXXX Received packet of length %02X, chanNum %02X, CRC %02X (actual: %02X)", len, chanNum, PKT_CRC(packet), calculate_crc8(packet,255));
	      if ((rv = fq_enqueue (udpchans[chanNum]->recv, packet, len, &udpchans[chanNum]->recv_cond,
				    &udpchans[chanNum]->recv_lock)) != FQ_OK &&
		  rv != FQ_ITEM_DISCARDED) {
		fq_error ("fq_enqueue failed in udp_receiver", rv);
		exit (EXIT_PANIC);
	    }
	    else
	      printlog("*** CRC FAIL!  Expected %02X, received %02X.", calculate_crc8(packet,255), PKT_CRC(packet));
	}
    }
}


/* 
   Create a UDP socket, bind it to port <port>, and connect it to
   <peer_addr>.  Return the new socket file descriptor.  Notice the
   expansion of the send and receive buffers from the default size.
*/
static int
create_udp_socket (int port, struct sockaddr_in* peer_addr)
{
    int fd, bsize;
    struct sockaddr_in bind_addr;

    /* Create socket. */
    if ((fd = socket (AF_INET, SOCK_DGRAM, 0)) == -1) {
        perror ("socket");
	exit (EXIT_PANIC);
    }

    /* Increase default send and receive buffer sizes. */
    bsize = 40000;
    if (setsockopt (fd, SOL_SOCKET, SO_SNDBUF, (char*)&bsize, 
                    sizeof (bsize)) == -1 ||
        setsockopt (fd, SOL_SOCKET, SO_RCVBUF, (char*)&bsize,
                    sizeof (bsize)) == -1) {
        perror ("setsockopt");
        return EXIT_PANIC;
    }

    /* Bind to the appropriate port number under the server's default 
       IP address. */
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    bind_addr.sin_port = htons (port);
    if (bind (fd, (struct sockaddr*)&bind_addr, sizeof (bind_addr)) == -1) {
        perror ("bind");
	exit (EXIT_PANIC);
    }

    /* Connect to the corresponding port on the peer.  The peer need
       not be active for this call to work--UDP "connections" are handled
       locally. */
    if (connect (fd, (struct sockaddr*)peer_addr, sizeof (*peer_addr)) == -1) {
	perror ("connect");
	exit (EXIT_PANIC);
    }

    return fd;
}


/*
   Indicate that the TCP thread indicated by <flag> and associated with
   the channel <ct> has recognized the deactivation of the channel and
   guarantees not to use any channel information associated with the 
   TCP connection until reactivated.  The last channel thread to call 
   this function also closes the TCP socket and increments the epoch
   number for the channel, ensuring that further packets from the other
   side of the relay are ignored (we ignore wraparound; it can't be
   solved per se, and 256 epochs should be adequate for our purposes).
   If this thread is the first to deactivate, it also wakes the other
   threads from sleep.
*/
static void
deactivate_channel (channel_t* ct, channel_state_t flag)
{
    int was_first;

    /* Changes to channel state must be mutually exclusive. */
    get_lock (&ct->channel_lock);
    was_first = (ct->channel_state == CLOSE_CHANNEL_NONE);
    ct->channel_state |= flag;

    /* Have all threads deactivated? */
    if (ct->channel_state == CLOSE_CHANNEL_ALL) {

	/* If so, close the TCP connection and bump up the epoch number. */
	close (ct->fd);
	ct->epoch++;

	/* If acting as the relay target, let the main thread know that
	   another channel is inactive. */
	if (mode == MODE_TCP_TARGET) {
	    ct->active = 0;
	    if (sem_post (&channel_semaphore) == -1) {
		perror ("sem_post");
		exit (EXIT_PANIC);
	    }
	}
    }

    /* All done changing channel state. */
    release_lock (&ct->channel_lock);

    /* Wake up other (possibly sleeping) threads. */
    if (was_first)
	wake_threads (ct, flag);
}


/*
   Initialize all channel data, including the UDP sockets associated with
   them.  UDP sockets are assigned consecutive port numbers beginning
   at <base_port>, and pairs of sockets are connected between this machine
   and the peer at <peer_addr>.
*/

static void
init_channels (pthread_attr_t* attr, int base_port,
	       struct sockaddr_in* peer_addr)
{
    int i;
    pthread_t trash;

    /* The target end of the relay uses a channel semaphore to indicate
       the availability of inactive channels to the main thread, which
       accepts new TCP connections and assigns them to channels. */
    if (mode == MODE_TCP_TARGET &&
	sem_init (&channel_semaphore, 0, MAX_CHANNELS) == -1) {
	perror ("sem_init");
	exit (EXIT_PANIC);
    }

    //We will only need one file descriptor open.  We are multiplexing on one port.
    peer_addr->sin_port = htons (base_port);
    int filedes = create_udp_socket (base_port, peer_addr);

    for (i = 0; i < MAX_CHANNELS; i++) {
	chan_tab[i].epoch           = 0;
	chan_tab[i].fd              = -1;
	chan_tab[i].active          = 0;
	chan_tab[i].need_help       = 0;
	chan_tab[i].channel_state   = CLOSE_CHANNEL_ALL;
	if (pthread_mutex_init (&chan_tab[i].channel_lock, NULL) != 0 ||
	    pthread_mutex_init (&chan_tab[i].help_lock, NULL) != 0 ||
	    pthread_cond_init (&chan_tab[i].help, NULL) != 0) {
	    fputs ("pthread mutex or cond init failed\n", stderr);
	    exit (EXIT_PANIC);
	}

	//Pass our single file descriptor to each udp process that we create
	udp_init (&chan_tab[i].udp[0], filedes);
	udp_init (&chan_tab[i].udp[1], filedes);

	udpchans[2*i] = &chan_tab[i].udp[1];
	udpchans[2*i+1] = &chan_tab[i].udp[0];

	if (pthread_create (&chan_tab[i].helper_id, attr, tcp_helper,&chan_tab[i]) != 0 ||
	    pthread_create (&trash, attr, tcp_receiver,	&chan_tab[i]) != 0 ||
	    pthread_create (&trash, attr, tcp_sender, &chan_tab[i]) != 0 ){
	  fputs ("pthread create failed\n", stderr);
	  exit (EXIT_PANIC);
	    }
    }
    if (pthread_create (&trash, attr, udp_receiver, &chan_tab[0].udp[0]) != 0) {
      	    fputs ("pthread create failed\n", stderr);
	    exit (EXIT_PANIC);
    }

}


/*
   Write <n> bytes from <buf> to file descriptor <fd>.  Block until 
   <n> bytes are received or read returns 0 or an error besides
   interruption by a signal.
*/
static int 
my_write (int fd, const void* buf, size_t n)
{
    size_t written = 0;
    ssize_t once;

    while (written < n) {
	if ((once = write (fd, buf + written, n - written)) == -1) {
	    /* Try again if interrupted. */
	    if (once == EINTR)
		continue;
	    /* Otherwise, fail. */
	    return -1;
	}
	written += once;
    }

    return n;
}


/*
   Open a TCP connection to the forwarding target and active the
   channel <ct>, waking the TCP helper and sender threads to recognize
   channel activation.
*/
static void
open_and_activate_channel (channel_t* ct)
{
    int fd;

    /* Open a TCP connection to the forwarding target (fwd_addr).
       Print error messages, but ignore errors. */
    if ((fd = socket (AF_INET, SOCK_STREAM, 0)) == -1)
        perror ("socket");
    else if (connect (fd, (struct sockaddr*)&fwd_addr,
	     sizeof (fwd_addr)) == -1) {
	perror ("connect to forwarding address");
	close (fd);
	fd = -1;
    }

    /* Lock should not be contended (this routine should not be called
       unless all TCP threads associated with a channel are known to
       consider the channel inactive, but the lock guarantees that new 
       file descriptor value is visible to other threads before the 
       change in channel state becomes visible, and is not an unreasonable
       practice for the channel_state (only access it under a lock, even
       if you know the access is uncontended). */
    ct->fd = fd;
    get_lock (&ct->channel_lock);
    ct->channel_state = CLOSE_CHANNEL_NONE;
    release_lock (&ct->channel_lock);

    /* No need to notify tcp_receiver (all threads not mentioned in 
       argument are awoken): it alone calls this function. */
    wake_threads (ct, CLOSE_CHANNEL_RECEIVER);
}


/*
   Create and bind the target TCP socket for the relay at port
   <target_port>, then put it in the passive state.  Ignore leftover
   connections from any previous bindings.  Return the file descriptor 
   for the socket.
*/
static int
set_up_target_socket (short int target_port)
{
    int fd, yes = 1;
    struct sockaddr_in bind_addr;

    /* Create main socket for listening for incoming TCP connections. */
    if ((fd = socket (AF_INET, SOCK_STREAM, 0)) == -1) {
        perror ("socket");
	exit (EXIT_PANIC);
    }

    /* Ignore the presence of previous connections in the TIME_WAIT state. */
    if (setsockopt (fd, SOL_SOCKET, SO_REUSEADDR, (char*)&yes, 
                    sizeof (yes)) == -1) {
        perror ("setsockopt");
	exit (EXIT_PANIC);
    }

    /* Bind to the appropriate port number under the server's default 
       IP address. */
    bind_addr.sin_family = AF_INET;
    bind_addr.sin_addr.s_addr = htonl (INADDR_ANY);
    bind_addr.sin_port = htons (target_port);
    if (bind (fd, (struct sockaddr*)&bind_addr, sizeof (bind_addr)) == -1) {
        perror ("bind");
	exit (EXIT_PANIC);
    }

    /* Place socket in passive state. */
    if (listen (fd, SERVER_QUEUE) == -1) {
	perror ("listen");
	exit (EXIT_PANIC);
    }

    return fd;
}


/*
   Initialize the unidirectional UDP channel <uct>.  The UDP socket is
   bound to port <port> and connected to <peer_addr>.
*/
static void
udp_init (udp_channel_t* uct, int filedes)
{
    fq_err_t rv;

    uct->fd = filedes;
    if (pthread_mutex_init (&uct->recv_lock, NULL) != 0 ||
        pthread_cond_init (&uct->recv_cond, NULL) != 0) {
	fputs ("pthread mutex or cond init failed\n", stderr);
	exit (EXIT_PANIC);
    }
    if ((rv = fq_create (&uct->recv, 32, MAX_PKT_LEN)) != FQ_OK) {
        fq_error ("fq_create failed", rv);
        exit (EXIT_PANIC);
    }
}


/*
   Wake up all TCP threads associated with channel <ct> and not 
   specified to ignore by the <ignore flag>.  Typically called
   when a channel is activated or deactivated to ensure that
   all threads recognize the change of state.
*/
static void
wake_threads (channel_t* ct, channel_state_t ignore)
{
    /* Wake up tcp_helper. */
    if (ignore != CLOSE_CHANNEL_HELPER) {
	get_lock (&ct->help_lock);
	condition_signal (&ct->help);
	release_lock (&ct->help_lock);
	/* Must also send signal in case tcp_helper is in poll. */
	pthread_kill (ct->helper_id, SIGUSR1);
    }

    /* Wake up tcp_receiver. */
    if (ignore != CLOSE_CHANNEL_RECEIVER) {
	get_lock (&ct->udp[1].recv_lock);
	condition_signal (&ct->udp[1].recv_cond);
	release_lock (&ct->udp[1].recv_lock);
    }

    /* Wake up tcp_sender. */
    if (ignore != CLOSE_CHANNEL_SENDER) {
	get_lock (&ct->udp[0].recv_lock);
	condition_signal (&ct->udp[0].recv_cond);
	release_lock (&ct->udp[0].recv_lock);
    }
}

