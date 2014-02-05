#include "inode_manager.h"

// disk layer -----------------------------------------

disk::disk()
{
  bzero(blocks, sizeof(blocks));
}

void disk::read_block(blockid_t id, char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_block(blockid_t id, const char *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
}

void
disk::read_indirect_block(blockid_t id, uint32_t *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(buf, blocks[id], BLOCK_SIZE);
}

void
disk::write_indirect_block(blockid_t id, const uint32_t *buf)
{
  if (id < 0 || id >= BLOCK_NUM || buf == NULL)
    return;

  memcpy(blocks[id], buf, BLOCK_SIZE);
}

// block layer -----------------------------------------

// Allocate a free disk block.
blockid_t
block_manager::alloc_block()
{
  /*
   * your lab1 code goes here.
   * note: you should mark the corresponding bit in block bitmap when alloc.
   * you need to think about which block you can start to be allocated.
   */
  char buf[BLOCK_SIZE];
  d->read_block(BBLOCK(++count), buf);
  char* buf_bit = (char*)buf;
  int byte_pos = count % BLOCK_SIZE;
  int bit_pos = count % 8;
  buf_bit += byte_pos;

  bitset<8> char_bit(*buf_bit);
  char_bit.set(bit_pos);
  *buf_bit = char_bit.to_ulong();
  d->write_block(BBLOCK(count), buf);

  return count;
}

void
block_manager::free_block(uint32_t id)
{
  /* 
   * your lab1 code goes here.
   * note: you should unmark the corresponding bit in the block bitmap when free.
   */
  char buf[BLOCK_SIZE];
  d->read_block(BBLOCK(id), buf);
  char* buf_bit = (char*)buf;
  int byte_pos = id % BLOCK_SIZE;
  int bit_pos = id % 8;
  buf_bit += byte_pos;

  bitset<8> char_bit(*buf_bit);
  char_bit.set(bit_pos, 0);
  *buf_bit = char_bit.to_ulong();
  d->write_block(BBLOCK(id), buf);

  bzero(buf, BLOCK_SIZE);
  d->write_block(id, buf);
  
  return;
}

// The layout of disk should be like this:
// |<-sb->|<-free block bitmap->|<-inode table->|<-data->|
block_manager::block_manager()
{
  d = new disk();

  // format the disk
  sb.size = BLOCK_SIZE * BLOCK_NUM;
  sb.nblocks = BLOCK_NUM;
  sb.ninodes = INODE_NUM;

  // root, sb and a empty block between bitmap and inode table for the three
  count = (BLOCK_NUM / BPB + 3) + ceil(INODE_NUM / IPB);

}

void
block_manager::read_block(uint32_t id, char *buf)
{
  d->read_block(id, buf);
}

void
block_manager::write_block(uint32_t id, const char *buf)
{
  d->write_block(id, buf);
}

void
block_manager::read_indirect_block(uint32_t id, uint32_t *buf)
{
  d->read_indirect_block(id, buf);
}

void
block_manager::write_indirect_block(uint32_t id, const uint32_t *buf)
{
  d->write_indirect_block(id, buf);
}

// inode layer -----------------------------------------

inode_manager::inode_manager()
{
  count = 0;
  bm = new block_manager();
  uint32_t root_dir = alloc_inode(extent_protocol::T_DIR);
  if (root_dir != 1) {
    printf("\tim: error! alloc first inode %d, should be 1\n", root_dir);
    exit(0);
  }
}

/* Create a new file.
 * Return its inum. */
uint32_t
inode_manager::alloc_inode(uint32_t type)
{
  /* 
   * your lab1 code goes here.
   * note: the normal inode block should begin from the 2nd inode block.
   * the 1st is used for root_dir, see inode_manager::inode_manager().
   */
  inode* ino = (inode*)malloc(sizeof(inode));
  ino->type = type;
  put_inode(++count, ino);
  return count;
}

void
inode_manager::free_inode(uint32_t inum)
{
  /* 
   * your lab1 code goes here.
   * note: you need to check if the inode is already a freed one;
   * if not, clear it, and remember to write back to disk.
   */
  inode* ino = get_inode(inum);
  uint32_t buf_indir[BLOCK_SIZE];
  if (ino->type != 0) {
    ino->type = 0;
	for (int i = 0; i < ceil( float(ino->size) / (float)BLOCK_SIZE ); i++ ) {
	  if (i < NDIRECT) {
		if (ino->blocks[i] > 0) {
		  bm->free_block(ino->blocks[i]);
		}
	  }
	  else if (i == NDIRECT) {
		bm->read_indirect_block(ino->blocks[i], buf_indir);
	  }
	  if (i >= NDIRECT) {
		bm->free_block(buf_indir[i - NDIRECT]);
	  }
	}
	bzero(ino->blocks, NDIRECT + 1);
	put_inode(inum, ino);
  }

  return;
}


/* Return an inode structure by inum, NULL otherwise.
 * Caller should release the memory. */
struct inode* 
inode_manager::get_inode(uint32_t inum)
{
  struct inode *ino, *ino_disk;
  char buf[BLOCK_SIZE];

  printf("\tim: get_inode %d\n", inum);

  if (inum < 0 || inum >= INODE_NUM) {
    printf("\tim: inum out of range\n");
    return NULL;
  }

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  // printf("%s:%d\n", __FILE__, __LINE__);

  ino_disk = (struct inode*)buf + inum%IPB;

  ino = (struct inode*)malloc(sizeof(struct inode));
  *ino = *ino_disk;

  return ino;
}

void
inode_manager::put_inode(uint32_t inum, struct inode *ino)
{
  char buf[BLOCK_SIZE];
  struct inode *ino_disk;

  printf("\tim: put_inode %d\n", inum);
  if (ino == NULL)
    return;

  bm->read_block(IBLOCK(inum, bm->sb.nblocks), buf);
  ino_disk = (struct inode*)buf + inum%IPB;
  *ino_disk = *ino;
  bm->write_block(IBLOCK(inum, bm->sb.nblocks), buf);
}

#define MIN(a,b) ((a)<(b) ? (a) : (b))

/* Get all the data of a file by inum. 
 * Return alloced data, should be freed by caller. */
void
inode_manager::read_file(uint32_t inum, char **buf_out, int *size)
{
  /*
   * your lab1 code goes here.
   * note: read blocks related to inode number inum,
   * and copy them to buf_Out
   */
  inode* ino = get_inode(inum);
  if( ino == NULL ) return;

  *buf_out = (char*)malloc(ino->size);
  bzero(*buf_out, ino->size);

  char* buf_read = *buf_out;
  char buf_tmp[BLOCK_SIZE];
  uint32_t indirId_tmp[BLOCK_SIZE];

  int l = ceil((float)(ino->size)/(float)BLOCK_SIZE);
  for (int i = 0; i < l; i++) {
	if (i < NDIRECT) {
  	  bm->read_block(ino->blocks[i], buf_tmp);
	}
	else if (i == NDIRECT) {
	  bm->read_indirect_block(ino->blocks[NDIRECT], indirId_tmp);
	}

	if (i >= NDIRECT) {
  	  bm->read_block(indirId_tmp[i - NDIRECT], buf_tmp);
	}

  	int read_size = MIN(BLOCK_SIZE, ino->size - *size);
	memcpy(buf_read, buf_tmp, read_size);

	buf_read += BLOCK_SIZE;
	*size += read_size;
  }
  
  return;
}

/* alloc/free blocks if needed */
void
inode_manager::write_file(uint32_t inum, const char *buf, int size)
{
  /*
   * your lab1 code goes here.
   * note: write buf to blocks of inode inum.
   * you need to consider the situation when the size of buf 
   * is larger or smaller than the size of original inode
   */
  inode* ino = get_inode(inum);
  if( ino == NULL || size < 0 || size > (int)MAXFILE * BLOCK_SIZE ) {
	  return;
  }

  char* buf_cpy = (char*)buf;
  char buf_tmp[BLOCK_SIZE];
  uint32_t indir[BLOCK_SIZE]; 
  uint32_t indirId;

  ino->size = size;

  int l = ceil((float)size/(float)BLOCK_SIZE);
  for (int i = 0; i < l; i++) {
	uint32_t id;
	if (i < NDIRECT) {
	  id = bm->alloc_block();
	  ino->blocks[i] = id;
	} 
	else if (i == NDIRECT) {
	  indirId = bm->alloc_block();
	  ino->blocks[NDIRECT] = indirId;
	}

	if (i >= NDIRECT) {
	  id = bm->alloc_block();
	  indir[i - NDIRECT] = id; 
	}

	memcpy(buf_tmp, buf_cpy, BLOCK_SIZE);
	bm->write_block(id, buf_tmp);

	buf_cpy += BLOCK_SIZE;
  }

  if (l > NDIRECT) {
	bm->write_indirect_block(indirId, indir);
  }

  put_inode(inum, ino);
  
  return;
}

void
inode_manager::getattr(uint32_t inum, extent_protocol::attr &a)
{
  /*
   * your lab1 code goes here.
   * note: get the attributes of inode inum.
   * you can refer to "struct attr" in extent_protocol.h
   */
  inode* i = get_inode(inum);
  a.type = i->type;
  a.size = i->size;
  a.atime = i->atime;
  a.mtime = i->mtime;
  a.ctime = i->ctime;
  
  return;
}

void
inode_manager::remove_file(uint32_t inum)
{
  /*
   * your lab1 code goes here
   * note: you need to consider about both the data block and inode of the file
   */
  free_inode(inum);
  
  return;
}
