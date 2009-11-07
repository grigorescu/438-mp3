/*									tab:8
 *
 * fq.h - header file for FIFO queue abstraction for ECE/CS 338 F01 MP3
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
 * Creation Date:   Wed Sep 26 23:00:35 2001
 * Filename:	    fq.h
 * History:
 *	SL	2	Tue Oct  9 10:07:35 2001
 *		Removed superfluous error codes and finished annotations of
 *		error codes.
 *	SL	1	Wed Sep 26 23:00:35 2001
 *		First written.
 */

#if !defined (FQ_H)
#define FQ_H

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

#include <pthread.h>

#ifdef  __cplusplus
extern "C" {
#endif

#define FQ_MAX_QUEUE_LEN     256  /* limit on queue length (number of items) */
#define FQ_MAX_ITEM_LEN    32768  /* limit on queue element length (bytes)   */

typedef struct fq_t fq_t;         /* opaque queue structure                  */

typedef enum {                    /* error messages defined by FQ module     */
    FQ_OK = 0,                    /* operation suceeded                      */
    FQ_BAD_PARAMETER,             /* bad parameter passed to FQ routine      */
    FQ_OUT_OF_MEMORY,             /* memory allocation failed                */
    FQ_ITEM_DISCARDED,            /* queue full; item not enqueued           */
    FQ_POSIX_MUTEX_FAILURE,       /* Posix mutex call failed                 */
    FQ_POSIX_COND_FAILURE,        /* Posix condition variable call failed    */
    FQ_QUEUE_EMPTY,               /* queue empty; nothing to dequeue         */
    FQ_INADEQUATE_SPACE,          /* buffer inadequate for dequeue operation */
    FQ_NO_SUCH_ERR                /* limit on possible error codes           */
} fq_err_t;


/* 
   Create a new FQ holding up to <queue_len> items of up to <item_len> 
   bytes.  Possible return values and meanings include:
     FQ_OK                  success; <new_fq> points to a pointer to 
				 the new FQ
     FQ_BAD_PARAMETER       one or mores parameters passed were invalid
     FQ_OUT_OF_MEMORY       inadequate memory to create FQ requested
*/
fq_err_t fq_create (fq_t** new_fq, int queue_len, int item_len);


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
fq_err_t fq_enqueue (fq_t* fq, const unsigned char* buf, int buf_len,
		     pthread_cond_t* cond, pthread_mutex_t* lock);


/* 
   Dequeue an item from the FQ <fq> into the buffer <buf>.  The <buf_len> 
   is a value-result argument that specifies the amount of buffer space 
   available and returns the amount written on success.  If the item 
   intended for dequeueing does not fit into the allocated space, an 
   error is returned.  An error is also returned if the queue was empty
   when checked.  Possible return values and meanings include:
     FQ_OK                  success
     FQ_BAD_PARAMETER       one or mores parameters passed were invalid
     FQ_QUEUE_EMPTY         not dequeued (queue empty)
     FQ_INADEQUATE_SPACE    buffer too small for next item to be dequeued
*/
fq_err_t fq_dequeue (fq_t* fq, unsigned char* buf, int* buf_len);


/*
   Destroy the FQ <fq> and free all memory associated with it.  Possible 
   return values and meanings include:
     FQ_BAD_PARAMETER       parameter passed was invalid
     FQ_OK                  success
*/
fq_err_t fq_destroy (fq_t* fq);


/*
    Print a human-readable error message for the condition corresponding
    to error <err> to stderr, prefixed by the string <msg> and a colon.
*/
void fq_error (const char* msg, fq_err_t err);


#ifdef  __cplusplus
}
#endif

#endif /* FQ_H */
