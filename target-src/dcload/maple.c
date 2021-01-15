
/*
 * Simple Maple Bus implementation
 *
 * NOTE: The functions here are designed for simplicity, not
 *       efficiency.  For good performance, requests should be
 *       parallelized (each DMA burst can contain one message to
 *       each device), and interrupts should be used to detect
 *       DMA completion rather than the busy-polling shown here.
 */

#include "maple.h"
//#include <string.h>
#include "memfuncs.h"

#define MAPLE(x) (*((volatile unsigned long *)(0xa05f6c00+(x))))


/*
 * Initialize the Maple Bus to reasonable defaults.
 * No end-DMA interrupts are registered.
 */
void maple_init()
{
  /* Reset hardware */
  MAPLE(0x8c) = 0x6155404f;
  MAPLE(0x10) = 0;
  /* Select 2Mbps bitrate, and a timeout of 50000 */
  MAPLE(0x80) = (50000<<16)|0;
  /* Enable bus */
  MAPLE(0x14) = 1;
}


/*
 * Wait for Maple DMA to finish
 */
void maple_wait_dma()
{
  while(MAPLE(0x18) & 1)
    ;
}


/* Since we're only going to do one request at a time in this
   simple design, the buffer need only to be large enough to
   hold one maximal request frame (1024 bytes), one maximal
   response frame (1024 bytes), and the two control longwords
   and header for the single transfer. In addition, DMA
   addresses need to be aligned to a 32 byte boundary.
*/

// Make GCC 32-byte align it in the .data section since GCC aligns the binary relative to 0x8c010000.
__attribute__((aligned(32))) volatile unsigned char dmabuffer[MAPLE_DMA_SIZE]; // Here's a global array


/*
 * Send a command to a device and wait for the response.
 *
 * port    - controller port (0-3)
 * unit    - unit number on port (0 = main unit, 1-5 = sub units)
 * cmd     - command number
 * datalen - number of longwords of parameter data
 * data    - parameter data (NB: big endian!)
 *
 */
void *maple_docmd(int port, int unit, int cmd, int datalen, void *data)
{
  unsigned int *sendbuf, *recvbuf;
  int to, from;

  port &= 3;

  /* Compute sender and recipient address */
  from = port << 6;
  to = (port << 6) | (unit>0? ((1<<(unit-1))&0x1f) : 0x20);

  /* Max data length = 255 longs = 1020 bytes */
  if(datalen > 255)
    datalen = 255;
  else if(datalen < 0)
    datalen = 0;

  /* Allocate a 1024 byte receieve buffer at the beginning of
     dmabuffer, with proper alignment.  Also mark the buffer as
     uncacheable.                                               */
  recvbuf =
    (unsigned int *) ((unsigned int)dmabuffer | 0xa0000000);

  /* Place the send buffer right after the receive buffer.  This
     automatically gives proper alignment and uncacheability.    */
  sendbuf =
    (unsigned int *) ((unsigned int)recvbuf + 1024);

  /* Make sure no DMA operation is currently in progress */
  maple_wait_dma();

  /* Set hardware DMA pointer to beginning of send buffer */
  MAPLE(0x04) = (unsigned int)sendbuf & 0x0fffffff;

  /* Setup DMA data.  Each message consists of two control words followed
     by the request frame.  The first control word determines the port,
     the length of the request transmission, and a flag bit marking the
     last message in the burst.  The second control word specifies the
     address where the response frame will be stored.  If no response is
     received within the timeout period, -1 will be written to this address. */

  /* Here we know only one frame should be send and received, so
     the final message control bit will always be set...          */
  *sendbuf++ = datalen | (port << 16) | 0x80000000; // NOTE: These 3 writes use the uncacheable area

  /* Write address to receive buffer where the response frame should be put */
  *sendbuf++ = ((unsigned int)recvbuf & 0x0fffffff);

  /* Create the frame header.  The fields are assembled "backwards"
     because of the Maple Bus big-endianness.                       */
  *sendbuf++ = (cmd & 0xff) | (to << 8) | (from << 16) | (datalen << 24);

  /* Copy parameter data, if any */
  if(datalen > 0)
  {
//    memcpy(sendbuf, data, datalen << 2); // sendbuf is 32-byte aligned, offset by 12. data is 8-byte aligned, offset by 4 due to port, unit, cmd, & datalen
    // So memcpy_32bit the first 4 bytes to make it all 8-byte aligned (remaining sendbuf will be 16-byte aligned and remaining data will be 8-byte aligned)
    memcpy_32bit(sendbuf, data, 4/4);
    SH4_aligned_memcpy((void*) (((unsigned int)sendbuf + 4) & 0x1fffffff), (void*) (((unsigned int)data + 4) & 0x1fffffff), datalen - 1); // use copy-back memory area for speed boost
    CacheBlockWriteBack((unsigned char*) ((unsigned int)sendbuf & 0x1fffffe0), ((datalen * 4) + 31)/32); // Synchronize memory with opcache in 32-byte blocks, sendbuf is already 32-byte aligned
    // Need to do that so DMA sees the data in memory
  }

  /* Frame is finished, and DMA list is terminated with the flag bit.
     Time to activate the DMA channel.                                */
  MAPLE(0x18) = 1;

  /* Wait for the complete cycle to finish, so that the response
     buffer is valid.                                            */
  maple_wait_dma();

  /* Return a pointer to the response frame */
  return recvbuf;
}
