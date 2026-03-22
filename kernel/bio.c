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

struct bcache {
  struct spinlock lock;
  struct buf* buf;
} bcacheb[BUCKET];


struct global_bcache {
  struct spinlock lock;
  struct buf buf[NBUF];
} bcacheg;

void
binit(void)
{
  int i, bucket;
  struct buf *b;

  initlock(&(bcacheg.lock), "bcache");
  for (i = 0; i < BUCKET; i++) {
    initlock(&(bcacheb[i].lock), "bcache");
  }

  for(b = bcacheg.buf, i = 0; b < bcacheg.buf + NBUF; b++, i++){
    initsleeplock(&b->lock, "buffer");
    bucket = i % BUCKET;
    if(!bcacheb[bucket].buf)
    {
      bcacheb[bucket].buf = b;
    }
    else {
      b->next = bcacheb[bucket].buf;
      bcacheb[bucket].buf = b;
    }
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b;
  int bucket = blockno % BUCKET;
  acquire(&(bcacheb[bucket].lock));

  // Is the block already cached?
  for(b = bcacheb[bucket].buf; b; b = b->next) {
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&(bcacheb[bucket]).lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.
  for(b = bcacheb[bucket].buf; b; b = b->next){
    if(b->refcnt == 0) {
      b->dev = dev;
      b->blockno = blockno;
      b->valid = 0;
      b->refcnt = 1;
      release(&(bcacheb[bucket]).lock);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&(bcacheb[bucket]).lock);
  acquire(&(bcacheg.lock));
  for(int i = 0; i < BUCKET; i++)
  {
    acquire(&(bcacheb[i].lock));
    for(b = bcacheb[i].buf; b; b = b->next){
      if(b->refcnt == 0) {
        b->dev = dev;
        b->blockno = blockno;
        b->valid = 0;
        b->refcnt = 1;
        release(&(bcacheb[i]).lock);
        release(&(bcacheg.lock));
        acquiresleep(&b->lock);
        return b;
      }
    }
    release(&(bcacheb[i].lock));
  }
  release(&(bcacheg.lock));
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

  int bucket = b->blockno % BUCKET;
  acquire(&(bcacheb[bucket].lock));
  b->refcnt--;
  release(&(bcacheb[bucket]).lock);
}

void
bpin(struct buf *b) {
  int bucket = b->blockno % BUCKET;
  acquire(&(bcacheb[bucket].lock));
  b->refcnt++;
  release(&(bcacheb[bucket].lock));
}

void
bunpin(struct buf *b) {
  int bucket = b->blockno % BUCKET;
  acquire(&(bcacheb[bucket].lock));
  b->refcnt--;
  release(&(bcacheb[bucket].lock));
}


