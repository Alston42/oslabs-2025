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
#include <stddef.h>

#define NBUCKETS 13
struct {
  struct spinlock glblock;
  struct spinlock lock[NBUCKETS];
  struct buf buf[NBUF];
  struct buf bucket[NBUCKETS];
} bcache;

static uint
hash(uint dev, uint blockno)
{
  return blockno % NBUCKETS;
}

void
binit(void)
{
  struct buf *b;

  initlock(&bcache.glblock, "bcache");

  for (int i = 0; i < NBUCKETS; i++) {
    initlock(&bcache.lock[i], "bcache");
    bcache.bucket[i].next = &bcache.bucket[i];
    bcache.bucket[i].prev = &bcache.bucket[i];
  }

  for (b = bcache.buf; b < bcache.buf+NBUF; b++) {
    struct buf *head = bcache.bucket + hash(b->dev, b->blockno);
    b->next = head->next;
    b->prev = head;
    initsleeplock(&b->lock, "buffer");
    head->next->prev = b;
    head->next = b;
  }
}

static struct buf*
bsearch(struct buf *head, uint dev, uint blockno)
{
  struct buf *b;
  // Is the block already cached?
  for(b = head->next; b != head; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      return b;
    }
  }
  return NULL;
}

static void
binsert(struct buf *head, struct buf *b)
{
  b->next = head->next;
  b->prev = head;
  head->next->prev = b;
  head->next = b;
}

static struct buf*
balloc(struct buf *head, uint dev, uint blockno)
{
  struct buf *b;
  for(b = head->prev; b != head; b = b->prev){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      return b;
    }
  }
  return NULL;
}
// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  uint id = hash(dev, blockno);
  struct spinlock *lk = &bcache.lock[id];
  struct buf *bkt = &bcache.bucket[id];

  acquire(lk);
  
  struct buf *b = bsearch(bkt, dev, blockno);
  if (b) {
    release(lk);
    acquiresleep(&b->lock);
    return b;
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  release(lk);
  acquire(&bcache.glblock);
  acquire(lk);
  // Search Again
  b = bsearch(bkt, dev, blockno);
  if (b) {
    release(lk);
    release(&bcache.glblock);
    acquiresleep(&b->lock);
    return b;
  }
  // Alloc in BUCKET
  b = balloc(bkt, dev, blockno);
  if (b) {
    release(lk);
    release(&bcache.glblock);
    acquiresleep(&b->lock);
    return b;
  }
  // Alloc in Other BUCKETs
  struct buf *bkt2;
  for (int i = 0; i < NBUCKETS; i++) {
    bkt2 = bcache.bucket + i;
    if (bkt2 == bkt) continue;
    acquire(bcache.lock + i);
    b = balloc(bkt2, dev, blockno);
    if (b) {
      b->prev->next = b->next;
      b->next->prev = b->prev;
      binsert(bkt, b);
      
      release(bcache.lock + i);
      release(lk);
      release(&bcache.glblock);
      acquiresleep(&b->lock);
      return b;
    }
    release(bcache.lock + i);
  }
  panic("bget: no buffers");
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
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);

  int id = hash(b->dev, b->blockno);
  struct spinlock *lk = &bcache.lock[id];
  acquire(lk);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->next->prev = b->prev;
    b->prev->next = b->next;
    binsert(&bcache.bucket[id], b);
  }
  
  release(lk);
}
void
bpin(struct buf *b) {
  struct spinlock *lk = &bcache.lock[hash(b->dev, b->blockno)];
  acquire(lk);
  b->refcnt++;
  release(lk);
}

void
bunpin(struct buf *b) {
  struct spinlock *lk = &bcache.lock[hash(b->dev, b->blockno)];
  acquire(lk);
  b->refcnt--;
  release(lk);
}


