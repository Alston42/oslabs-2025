// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"
#include <limits.h>

#define NBUCKETS 13
struct {
  // struct spinlock lock[NBUCK ];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  // struct buf head;
} bcache;

void
binit(void)
{
  struct buf *b;

  // Create linked list of buffers
  for (int i = 1; i < NBUF-1; i++) {
    b = bcache.buf + i;
    b->next = b+1;
    b->prev = b-1;
    b->timestamp = ticks;
    initsleeplock(&b->lock, "buffer");
  }
  b = bcache.buf;
  b->next = b + 1;
  b->prev = b + NBUF - 1;
  b->timestamp = ticks;
  initsleeplock(&b->lock, "buffer");
  b = bcache.buf + NBUF - 1;
  b->next = b - NBUF + 1;
  b->prev = b - 1;
  b->timestamp = ticks;
  initsleeplock(&b->lock, "buffer");
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b = bcache.buf;

  // Is the block already cached?
  do
  {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      acquiresleep(&b->lock);
      b->timestamp = ticks;
      return b;
    }
    b = b->next;
  } while (b != bcache.buf);
  
  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  b = bcache.buf + NBUF-1; // if all refered the last one is the least recently used
  int i = NBUF-1;
  int index = NBUF-1;
  _Bool found = 0;
  uint min_timestamp = UINT_MAX;
  do
  {
    if(b->refcnt == 0 && b->timestamp < min_timestamp) {
      min_timestamp = b->timestamp;
      index = i;
      found = 1;
    }
    b = b->prev;
    i--;
  } while (b != bcache.buf + NBUF-1);

  if (!found)
    panic("bget: no buffers");
  
  b = bcache.buf + index;
  b->dev = dev;
  b->timestamp = ticks;
  b->blockno = blockno;
  b->valid = 0;
  b->refcnt = 1;
  acquiresleep(&b->lock);
  return b;

}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  b->timestamp = ticks;
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  b->refcnt--;
  releasesleep(&b->lock);
}

void
bpin(struct buf *b) {
  b->refcnt++;
}

void
bunpin(struct buf *b) {
  b->refcnt--;
}


