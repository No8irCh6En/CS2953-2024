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

#define NBUCKET 13

struct {
  struct spinlock lock[NBUCKET];
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  struct buf head[NBUCKET];
} bcache;

int hash(int dev, int blockno) {
  return (dev + 23 * blockno) % NBUCKET;
}

void
binit(void)
{

  // Create linked list of buffers
  for(int i = 0; i < NBUF; i++)
    initsleeplock(&(bcache.buf[i].lock), "buffer");

  int k = 0;
  for(int i = 0; i < NBUCKET; i++){
    initlock(&bcache.lock[i], "bcache");
    bcache.head[i].next = &bcache.buf[k];
    bcache.buf[k].prev = &bcache.head[i];
    bcache.buf[k].next = &bcache.buf[k + 1];
    bcache.buf[k + 1].prev = &bcache.buf[k];

    if(i >= 9){
      bcache.buf[k + 1].next = &bcache.buf[k + 2];
      bcache.buf[k + 2].prev = &bcache.buf[k + 1];
      bcache.buf[k + 2].next = &bcache.head[i];
      bcache.head[i].prev = &bcache.buf[k + 2];
      k += 3;
    } else{
      bcache.buf[k + 1].next = &bcache.head[i];
      bcache.head[i].prev = &bcache.buf[k + 1];
      k += 2;
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

  int bucket_id = hash(dev, blockno);

  acquire(&bcache.lock[bucket_id]);

  // Is the block already cached?
  for(b = bcache.head[bucket_id].next; b != &bcache.head[bucket_id]; b = b->next){
    if(b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache.lock[bucket_id]);
      acquiresleep(&b->lock);
      return b;
    }
  }

  release(&bcache.lock[bucket_id]);

  // Not cached.
  // Recycle the least recently used (LRU) unused buffer.

  struct buf* target = 0;

  for(int i = 0; i < NBUCKET; i++){
    if(target) break;
    acquire(&bcache.lock[i]);
    for(b = bcache.head[i].next; b != &bcache.head[i]; b = b->next){
      if(b->refcnt == 0) {
        target = b;
        b->prev->next = b->next;
        b->next->prev = b->prev;
        break;
      }
    }
    release(&bcache.lock[i]);
  }

  // push back
  acquire(&bcache.lock[bucket_id]);

  if(target){
    target->next = bcache.head[bucket_id].next;
    target->prev = &bcache.head[bucket_id];
    bcache.head[bucket_id].next->prev = target;
    bcache.head[bucket_id].next = target;
  }

  struct buf* real_tar = 0;
  for (b = bcache.head[bucket_id].next; b != &bcache.head[bucket_id];
       b = b->next) {
      if (b->blockno == blockno && b->dev == dev && b->refcnt > 0) {
          // this time b is the block we want
          real_tar = b;
          real_tar->refcnt++;
          break;
      }
  }

  if (!real_tar) {
      real_tar = target;
      real_tar->dev = dev;
      real_tar->blockno = blockno;
      real_tar->valid = 0;
      real_tar->refcnt = 1;
  }
  release(&bcache.lock[bucket_id]);
  acquiresleep(&real_tar->lock);
  return real_tar;

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

  int bucket_id = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket_id]);
  b->refcnt--;
  release(&bcache.lock[bucket_id]);
}

void
bpin(struct buf *b) {
  int bucket_id = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket_id]);
  b->refcnt++;
  release(&bcache.lock[bucket_id]);
}

void
bunpin(struct buf *b) {
  int bucket_id = hash(b->dev, b->blockno);
  acquire(&bcache.lock[bucket_id]);
  b->refcnt--;
  release(&bcache.lock[bucket_id]);
}


