/*									tab:8
 *
 * fq.c - source file for FIFO queue abstraction for ECE/CS 338 F01 MP3
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
 * Creation Date:   Wed Sep 26 23:01:03 2001
 * Filename:	    fq.c
 * History:
 *	SL	2	Tue Oct  9 10:10:56 2001
 *		Removed error messages for superfluous error codes.
 *	SL	1	Wed Sep 26 23:01:03 2001
 * 		First written.
 */

#include <pthread.h>

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include<string.h>

#include "fq.h"
 
/* 
    The FQ module defines a non-blocking queue abstraction for transfer 
    of chunks of data between threads in a process.  FQ stands for FIFO 
    (first-in, first-out) Queue.  Each FQ supports concurrent operations 
    by one writer and one reader.  Higher levels of concurrency are not 
    supported, and behavior is undefined for such uses.

    Although blocking versions of the enqueue and dequeue routines are
    not provided, the enqueue operation does signal an external condition
    variable if the queue might have been empty prior to the enqueue.
    Readers can thus choose to sleep on the queue empty condition.
*/


/* FQ structure definition */
struct fq_t {
    int queue_len;       /* number of items in each queue           */
    int item_len;        /* number of bytes allowed per item        */
    /* queue is empty when head == tail */
    int head;            /* head index for queue; updated by reader */ 
    int tail;            /* tail index for queue; updated by writer */
    int* length;         /* length of items in queue                */
    unsigned char* data; /* data for items in queue                 */
};

/*
   Memory barriers are architecture-dependent, but are generally implied 
   by any lock construct, etc.  If you want to use another architecture,
   but don't know the memory barrier instruction, or are not using gcc
   for compilation, use a Posix lock instead. 

   (You could also remove it without really running into problems given 
   the packet sizes in MP3, and of course one can always remove such things
   on uniprocessors, but it becomes a latent bug.)
*/
   #define STORE_STORE_BARRIER()  __asm__ volatile ("":::"memory")



/* 
   Create a new FQ holding up to <queue_len> items of up to <item_len> 
   bytes.  Possible return values and meanings include:
     FQ_OK                  success; <new_fq> points to a pointer to 
                                 the new FQ
     FQ_BAD_PARAMETER       one or mores parameters passed were invalid
     FQ_OUT_OF_MEMORY       inadequate memory to create FQ requested
*/
fq_err_t 
fq_create (fq_t** new_fq, int queue_len, int item_len)
{
    fq_t* fq = NULL;
    unsigned char* data_block = NULL;

    /* Check parameters. */
    if (new_fq == NULL || queue_len < 1 || queue_len > FQ_MAX_QUEUE_LEN ||
	item_len < 1 || item_len > FQ_MAX_ITEM_LEN)
	return FQ_BAD_PARAMETER;

    /* Allocate necessary memory. */
    if ((fq = malloc (sizeof (fq_t))) == NULL ||
	(data_block = malloc ((queue_len + 1) * item_len)) == NULL ||
        (fq->length = malloc ((queue_len + 1) * sizeof (int))) == NULL) {
	if (fq != NULL) {
	    free (fq);
	    if (data_block != NULL)
		free (data_block);
	}
	return FQ_OUT_OF_MEMORY;
    }

    /* Initialize FQ values.  Queue length is incremented to allow a
       queue to hold the requested number of items, as the head and tail
       are kept modulo the length, and equality indicates an empty queue
       rather than a full one. */
    fq->queue_len = queue_len + 1;
    fq->item_len = item_len;
    fq->head = fq->tail = 0;
    fq->data = data_block;

    *new_fq = fq;
    return FQ_OK;
}


/*
   Enqueue the item held in <buf> and consisting of <buf_len> bytes in
   FQ queue <fq>, then wake up the reader if the queue might have been
   empty by signalling the condition variable <cond> under the 
   mutex <lock>.  If the queue is full when checked, the routine returns 
   an error.  Possible return values and meanings include:
     FQ_OK                  success
     FQ_BAD_PARAMETER       one or mores parameters passed were invalid
     FQ_ITEM_DISCARDED      not enqueued (queue full)
     FQ_POSIX_MUTEX_FAILURE a Posix mutex call failed
     FQ_POSIX_COND_FAILURE  a Posix condition variable call failed
*/
fq_err_t 
fq_enqueue (fq_t* fq, const unsigned char* buf, int buf_len,
	    pthread_cond_t* cond, pthread_mutex_t* lock)
{
    /* Check parameters. */
    if (fq == NULL || buf == NULL || buf_len < 0 || buf_len > fq->item_len)
	return FQ_BAD_PARAMETER;

    /* Check for queue full condition.  False negatives cannot occur, 
       as the head of the queue only moves forward (and only up to the
       tail), and the tail cannot be moved by any other thread.  False
       positives result in packet discard--a more complex implementation
       uses a second condition variable to block on enqueue to a full
       queue pending dequeue of some item by the reader. */
    if ((fq->tail + 1) % fq->queue_len == fq->head) {
	/* Queue appears to be full.  Return an error message. */
	return FQ_ITEM_DISCARDED;
    }
         
    /* Enqueue the item.  No lock is necessary. */
    memcpy (fq->data +  fq->tail * fq->item_len, buf, buf_len);
    fq->length[fq->tail] = buf_len;

    /* Need a memory barrier here to prevent the tail increment from
       becoming visible before the copied data and its length. */
    STORE_STORE_BARRIER ();

    /* Tail increment must only occur after data are copied to avoid
       race condition with reader. */
    fq->tail = (fq->tail + 1) % fq->queue_len;

    /* Wake up the reader thread, if necessary.  The first check needs
       no lock; again, false negatives cannot occur (false positives
       can, but don't matter). */
    if (cond != NULL && (fq->head + 1) % fq->queue_len == fq->tail) {
	if (lock != NULL && pthread_mutex_lock (lock) != 0)
	    return FQ_POSIX_MUTEX_FAILURE;
	if (pthread_cond_signal (cond) != 0) {
	    if (lock != NULL)
		(void)pthread_mutex_unlock (lock);
	    return FQ_POSIX_COND_FAILURE;
	}
	if (lock != NULL && pthread_mutex_unlock (lock) != 0)
	    return FQ_POSIX_MUTEX_FAILURE;
    }

    return FQ_OK;
}


/* 
   Dequeue an item from the FQ <fq> into the buffer <buf>.  The <buf_len> 
   is a value-result argument that specifies the amount of buffer space 
   available and returns the amount written on success.  If the item 
   intended for dequeueing does not fit into the allocated space, an 
   error is returned.  An error is also returned if the queue was empty
   when checked.  Possible return values and meanings include:
     FQS_OK                  success
     FQS_BAD_PARAMETER       one or mores parameters passed were invalid
     FQ_QUEUE_EMPTY         not dequeued (queue empty)
     FQS_INADEQUATE_SPACE    buffer too small for next item to be dequeued
*/
fq_err_t 
fq_dequeue (fq_t* fq, unsigned char* buf, int* buf_len)
{
    int len;

    /* Check parameters. */
    if (fq == NULL || buf == NULL || 
        (buf_len == NULL && *buf_len != 0) || *buf_len < 0)
	return FQ_BAD_PARAMETER;

    /* Check for queue empty condition.  False negatives cannot occur, 
       as the head of the queue only moves forward (and only up to the
       tail), and the tail cannot be moved by any other thread. */
    if (fq->head == fq->tail) {
	/* Return without dequeueing. */
	return FQ_QUEUE_EMPTY;
    }

    /* Dequeue an item from queue i.  No lock is necessary. */
    if ((len = fq->length[fq->head]) > *buf_len)
	return FQ_INADEQUATE_SPACE;
    memcpy (buf, fq->data + fq->head * fq->item_len, len);
    *buf_len = len;

    /* Need a memory barrier here to prevent the head increment from
       becoming visible before the copied data and its length.  A
       slightly weaker barrier (stores can't pass loads) barrier
       would suffice in this case, but for simplicity I'm using the 
       same barrier necessary for enqueue.  */
    STORE_STORE_BARRIER ();

    /* Head increment must only occur after data are copied to avoid
       race condition with writer. */
    fq->head = (fq->head + 1) % fq->queue_len;

    return FQ_OK;
}


/*
   Destroy the FQ <fq> and free all memory associated with it.  Possible 
   return values and meanings include:
     FQ_BAD_PARAMETER       parameter passed was invalid
     FQ_OK                  success
*/
fq_err_t 
fq_destroy (fq_t* fq)
{
    /* Check parameter. */
    if (fq == NULL)
	return FQ_BAD_PARAMETER;

    /* Free space. */
    free (fq->data);
    free (fq->length);
    free (fq);

    return FQ_OK;
}


/*
    Print a human-readable error message for the condition corresponding
    to error <err> to stderr, prefixed by the string <msg> and a colon.
*/
void 
fq_error (const char* msg, fq_err_t err)
{
    static const char* const fq_err_str[FQ_NO_SUCH_ERR] = {
	"no error reported",
	"bad parameter passed to FQ function",
	"memory allocation failed",
	"enqueue item discarded",
	"POSIX mutex function failed",
	"POSIX condition variable function failed",
	"dequeue from empty queue", 
	"dequeue buffer of inadequate length for packet",
    };

    if (msg == NULL)
	fputs ("NULL message passed to fq_error.\n", stderr);
    else if (err < 0 || err >= FQ_NO_SUCH_ERR)
	fprintf (stderr, "%s: invalid error code passed to fq_error.\n", msg);
    else
	fprintf (stderr, "%s: %s\n", msg, fq_err_str[err]);
}

