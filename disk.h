#ifndef _DISK_H_
#define _DISK_H_

/******************************************************************************/
#define DISK_BLOCKS  8192      /* number of blocks on the disk                */
#define BLOCK_SIZE   4096      /* block size on "disk"                        */

/******************************************************************************/

typedef struct desc desc;
struct desc {
	int value = -1; // which of the 32 file descriptors has the file been assigned. Closed if -1, open if >-1
	int freq = 0; // indicates how many file descriptors to the file is open
	int offset = 0; // offset / seek pointer
	int current_block = 0; // offset pointer for blocks, used to iterate through inblock. Primarily for reading blocks, not writing
};

typedef struct dfile dfile;
struct dfile {
	char name[15];
	desc descriptor;
	void* data;
	off_t size = 0; //size of file in kb
	int inblock[BLOCK_SIZE] = { NULL }; // list of which blocks this file occupies
	int blocks_count = 0;
	
	int starts[BLOCK_SIZE] = { NULL }; // start of particular block series in inblock. Why do I have this in the first place again?
	int startcount = 0; // count of how many starts of a particular series
};

char* strdup(const char *s);
int calc_block(size_t nbyte);
void block_w(int fildes, void *buf, size_t nbyte);
void block_r(int fildes, void *buf, size_t nbyte);
void block_t(int fildes, size_t nbyte, off_t length);
void block_d(int fildes);

int make_fs(char* disk_name);
int mount_fs(char* disk_name);
int unmount_fs(char* disk_name);
int fs_open(char* name);
int fs_close(int fildes);
int fs_create(char* name);
int fs_delete(char *name);
int fs_read(int fildes, void *buf, size_t nbyte);
int fs_write(int fildes, void *buf, size_t nbyte);
int fs_get_filesize(int fildes);
int fs_lseek(int fildes, off_t offset);
int fs_truncate(int fildes, off_t length);

int make_disk(char *name);     /* create an empty, virtual disk file          */
int open_disk(char *name);     /* open a virtual disk (file)                  */
int close_disk();              /* close a previously opened disk (file)       */

int block_write(int block, char *buf);
                               /* write a block of size BLOCK_SIZE to disk    */
int block_read(int block, char *buf);
                               /* read a block of size BLOCK_SIZE from disk   */
/******************************************************************************/

#endif