#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <ctype.h>
#include "disk.h"

#include <sys/types.h>  // has off_t datatype

#define MAX 32

/******************************************************************************/
static int active = 0;  /* is the virtual disk open (active) */
static int handle;      /* file handle to virtual disk       */

dfile* directory[MAX * 2] = { NULL }; // root directory
int file_count = 0; // max is 63

int freespace[MAX] = { 1 }; // list of available file descriptor slots
int desc_count = 0; // max is 31

int blockmem = 4; // size of each block in kb
int maxmem = BLOCK_SIZE * 4; // 16,384 KB, about 16 mb from 4kb per block from 4096 blocks.
off_t currmem = maxmem; // how much available at the moment 

int blocks[BLOCK_SIZE] = { NULL };

/******************************************************************************/

char* strdup(const char *s)   /* make a duplicate of s */
{
	char *p;
	p = (char *)malloc(strlen(s) + 1);  /* +1 for Œ\0Œ */
	if (p != NULL) strcpy(p, s);
	return p;
}

int calc_block(size_t nbyte) { // calculate number of blocks needed from given nbytes and return the number.
	int bread = 0;

	if (nbyte != 0) {
		if (nbyte > blockmem) { // 6 / 4
			bread = nbyte / blockmem;
			if (((double)nbyte / (double)blockmem) > 0) {
				bread = nbyte / blockmem;
				bread += 1;
			}
		}
		else { bread = 1; } // nbyte <= blockmem // 1-4 / 4 
	} // calculate 0 blocks needed if nbyte = 0

	return bread;
}
void block_w(int fildes, void *buf, size_t nbyte) { // only problem is that block_w focuses on writing over unreserved blocks, not including blocks that are already taken up by the current file
	int bread = 0; //all of this is used to try converting nbytes / memory into allocated blocks. So nbytes of 16 = 4 blocks. 18 nbytes = 5 blocks
	bread = calc_block(nbyte);
	bool l = true; // indicates whether a block has just been reserved
	int v = 0; // iterate through blocks list

	bool j = true; // start of new block is true
	bool u = true; // a switch until the first actual unreserved block is found for block_write

	for (size_t i = 0; i < bread; ++i) { // reserve bread more blocks
		l = true;
		v = 0;
		do {
			if (blocks[v] != NULL) { j = true; } // if there's a break in unreserved blocks, indicate new possible start
			else if (blocks[v] == NULL) {
				if (u == true) { // unreserved block found. block_write should automatically skip over reserved blocks while writing
					block_write(v, buf);
					((*(directory + fildes))->data) = buf;
					u = false; // no need to have this be active anymore, since block_write should skip reserved blocks and look for unreserved blocks
				}
				blocks[v] = 1; // reserve a free block on the list
				(*(*(directory + fildes)))->(inblock + blocks_count) = v; // in which blocks this file is in
				(*(directory + fildes))->blocks_count += 1; // number of blocks this file is in

				if (j == true) { // start of new block is true
					int vo = fildes;
					(*(*(directory + fildes)))->(starts + startcount) = (*(*(directory + vo)))->(inblock + v);
					(*(directory + fildes))->startcount += 1;
					j = false; // not a start anymore
				}
				l = false;
			}
			++v;
		} while (l == true || v < BLOCK_SIZE);
	}
	(*(directory + fildes))->descriptor.current_block += bread;
	(*(directory + fildes))->descriptor.offset += nbyte;
}
void block_r(int fildes, void *buf, size_t nbyte) { // ~~only really needs to look for start blocks~~ Maybe only inblocks
	int bread = 0; //all of this is used to try converting nbytes / memory into allocated blocks. So nbytes of 16 = 4 blocks. 18 nbytes = 5 blocks
	bread = calc_block(nbyte); // to read, used to go through bread many indexes in inblock of a particular file
	int it = (*(directory + fildes))->descriptor.current_block;

	block_read((*(*(directory + fildes)))->(inblock + it), buf);

	(*(directory + fildes))->descriptor.current_block += bread;
	buf = ((*(directory + fildes))->data);
	(*(directory + fildes))->descriptor.offset += nbyte;
}
void block_t(int fildes, size_t nbyte, off_t length) { // removes bread blocks of a file from lists
	int bread = 0;
	bread = calc_block(nbyte);

	for (size_t i = 0; i < bread; ++i) {
		//if (i == bread - 1) {
		block_write(((*(directory + fildes))->descriptor.current_block) - i, NULL);
		//((*(directory + fildes))->data) = NULL;
	//}
		int vo = fildes;
		blocks[(*(*(directory + fildes)))->(inblock + ((*(directory + vo))->descriptor.current_block) - i)] = 0; // frees a reserved block on the list
		(*(*(directory + fildes)))->(inblock + blocks_count) = NULL; // in which blocks this file is in
		(*(directory + fildes))->blocks_count -= 1; // number of blocks this file is in
	}

	if (length < (*(directory + fildes))->descriptor.offset) { (*(directory + fildes))->descriptor.offset = length; }
	if (calc_block(length) < (*(directory + fildes))->descriptor.current_block) { (*(directory + fildes))->descriptor.current_block = calc_block(length); }
}
void block_d(int fildes) {
	int bread = 0;
	bread = calc_block((*(directory + fildes))->size);

	for (size_t i = 0; i < bread; ++i) {
		//if (i == bread - 1) {
			block_write(((*(directory + fildes))->descriptor.current_block)-i, NULL);
		//}
			int vo = fildes;
		blocks[(*(*(directory + fildes)))->(inblock + ((*(directory + vo))->descriptor.current_block) - i)] = 0; // frees a reserved block on the list
	}
	((*(directory + fildes))->data) = NULL;
}

int make_fs(char* disk_name) {
	if (make_disk(disk_name) == -1) { return -1; }
	if (open_disk(disk_name) == -1) { return -1; }

	if (close_disk(disk_name) == -1) { return -1; }

	return 0;
}
int mount_fs(char* disk_name) {
	if (open_disk(disk_name) == -1) { return -1; }

	return 0;
}
int unmount_fs(char* disk_name) {
	// write back meta-data
	// not needed since everything is immediate
	if (close_disk(disk_name) == -1) { return -1; }

	for (int i = 0; i < file_count + 1; ++i) { //close any open file descriptors
		if (directory[i] != NULL) {
			if ((*(directory + i))->descriptor.value != -1 ) {
				int e = (*(directory + i))->descriptor.freq;
				for (int d = 0; d < e; ++d) { fs_close(i);}
			}
		}
	}
	return 0;
}
int fs_open(char* name) {
	if (desc_count >= 31) { return -1; }

	int internal_g = 0;
	int file_descriptor = -1;
	do {
		if (freespace[internal_g] == 1) {
			freespace[internal_g] = 0;
			file_descriptor = internal_g;
		}
		++internal_g;

	} while (file_descriptor == -1 || internal_g < 32); // if file_descriptor stays at -1, user is looking for an already open file by this point

	for (int i = 0; i < file_count + 1; ++i) {
		if (strcmp(name, (*(directory + i))->name) == 0) { //file found
			if (file_descriptor != -1) { // open_space available
				if ((*(directory + i))->descriptor.value == -1 ) { // new descriptor
					(*(directory + i))->descriptor.value = file_descriptor;
					++desc_count;
				} // if not a new descriptor
			} // else, just pass the descriptor

			if ((*(directory + i))->descriptor.value == -1) { return (*(directory + i))->descriptor.value; } //error, no open space and found file hasn't been assigned a file descriptor

			(*(directory + i))->descriptor.freq += 1;
			fs_lseek(i, 0); //set seek pointer to zero
			return (*(directory + i))->descriptor.value;
		}
	}

	return -1;
}
int fs_close(int fildes) {
	if ((*(directory + fildes))->descriptor.value != -1) { // file not closed
		(*(directory + i))->descriptor.freq -= 1;
		if ((*(directory + i))->descriptor.freq == 0) { // no more references to this file
			freespace[(*(directory + fildes))->descriptor.value] = 1; // set space as free
			(*(directory + fildes))->descriptor.value = -1; // ultimately close file
			--desc_count; // free up availability of descriptor counts
		}
		return 0;
	} //else the file is already closed

	return -1;
}
int fs_create(char* name) {
	for (int i = 0; i < file_count + 1; ++i) { if (strcmp(name, (*(directory+i))->name ) == 0) { return -1; } }

	if (strlen(name) > 15) { return -1; }
	if (file_count >= 63){ return -1}

	dfile* p = (dfile*)malloc(sizeof(dfile));
	strcpy(p->name, name);

	int i = 0;
	bool s = true;
	do {
		if (directory[i] == NULL) {
			directory[i] = p;
			s = false;
		}
		++i;
	} while (i < (MAX*2) - 1 || s == true);

	++file_count;

	return 0;
}
int fs_delete(char *name) {
	for (int i = 0; i < file_count + 1; ++i) {
		if (strcmp(name, (*(directory + i))->name) == 0) { //file found
			if ((*(directory + i))->descriptor.value == -1) { // file is closed
				currmem += (*(directory + i))->size;
				block_d(i);
				free(directory[i]); // free up actual allocated space from malloc
				(*(directory + i)) = NULL; // free up spot in directory list
				--file_count;
				return 0;
			} // file is not closed
			return -1; // file is found, but file is not closed
		}
	} // file is not found
	return -1;
}
int fs_read(int fildes, void *buf, size_t nbyte) {
	if (fildes > file_count) { return -1; }

	if (nbyte > (((*(directory + fildes))->size) - (*(directory + fildes))->descriptor.offset)) {
		off_t z = (((*(directory + fildes))->size) - (*(directory + fildes))->descriptor.offset);

		block_r(fildes, buf, z);
		return z;
	}

	block_r(fildes, buf, nbyte);
	return nbyte;
}
int fs_write(int fildes, void *buf, size_t nbyte) {
	if (fildes > file_count) { return -1; }
	off_t z = currmem;

	if (nbyte > (((*(directory + fildes))->size) - (*(directory + fildes))->descriptor.offset)) {
		if (currmem < nbyte) {
			(((*(directory + fildes))->size) += z;

			block_w(fildes, buf, z);

			currmem -= z;
			return z;
		}

		//270 > 120 - 30, 270 - 90 = 180 must be added to size
		//increase offset by 270
		(((*(directory + fildes))->size) += (nbyte - (((*(directory + fildes))->size) - (*(directory + fildes))->descriptor.offset));
		currmem -= nbyte;
	}

	block_w(fildes, buf, nbyte);
	return nbyte;
}
int fs_get_filesize(int fildes) {
	if (fildes <= file_count) { return (*(directory+fildes))->size; }
	return -1;
}
int fs_lseek(int fildes, off_t offset) {
	if (fildes > file_count) { return -1; }
	else if (offset > (*(directory + fildes))->size) { return -1; }
	else if (offset < 0) { return -1; }

	int ma = calc_block(offset);
	(*(directory + fildes))->descriptor.current_block = ma;
	(*(directory + fildes))->descriptor.offset = offset;
	return 0;
}
int fs_truncate(int fildes, off_t length) {
	if (fildes > file_count) { return -1; }
	else if (length > (*(directory + fildes))->size) { return -1; }

	if (length < (*(directory + fildes))->size) {
		size_t z = (*(directory + fildes))->size - length; // how much data lost / freed up
		int g = calc_block(z); // how much data lost / freed up in blocks
		block_t(fildes, g, length);
		currmem += z;
		(*(directory + fildes))->size = length;
	}

	return 0;
}

int make_disk(char *name)
{ 
  int f, cnt;
  char buf[BLOCK_SIZE];

  if (!name) {
    fprintf(stderr, "make_disk: invalid file name\n");
    return -1;
  }

  if ((f = open(name, O_WRONLY | O_CREAT | O_TRUNC, 0644)) < 0) {
    perror("make_disk: cannot open file");
    return -1;
  }

  memset(buf, 0, BLOCK_SIZE);
  for (cnt = 0; cnt < DISK_BLOCKS; ++cnt)
    write(f, buf, BLOCK_SIZE);

  close(f);

  return 0;
}
int open_disk(char *name)
{
  int f;

  if (!name) {
    fprintf(stderr, "open_disk: invalid file name\n");
    return -1;
  }  
  
  if (active) {
    fprintf(stderr, "open_disk: disk is already open\n");
    return -1;
  }
  
  if ((f = open(name, O_RDWR, 0644)) < 0) {
    perror("open_disk: cannot open file");
    return -1;
  }

  handle = f;
  active = 1;

  return 0;
}
int close_disk()
{
  if (!active) {
    fprintf(stderr, "close_disk: no open disk\n");
    return -1;
  }
  
  close(handle);

  active = handle = 0;

  return 0;
}
int block_write(int block, char *buf)
{
  if (!active) {
    fprintf(stderr, "block_write: disk not active\n");
    return -1;
  }

  if ((block < 0) || (block >= DISK_BLOCKS)) {
    fprintf(stderr, "block_write: block index out of bounds\n");
    return -1;
  }

  if (lseek(handle, block * BLOCK_SIZE, SEEK_SET) < 0) {
    perror("block_write: failed to lseek");
    return -1;
  }

  if (write(handle, buf, BLOCK_SIZE) < 0) {
    perror("block_write: failed to write");
    return -1;
  }

  return 0;
}
int block_read(int block, char *buf)
{
  if (!active) {
    fprintf(stderr, "block_read: disk not active\n");
    return -1;
  }

  if ((block < 0) || (block >= DISK_BLOCKS)) {
    fprintf(stderr, "block_read: block index out of bounds\n");
    return -1;
  }

  if (lseek(handle, block * BLOCK_SIZE, SEEK_SET) < 0) {
    perror("block_read: failed to lseek");
    return -1;
  }

  if (read(handle, buf, BLOCK_SIZE) < 0) {
    perror("block_read: failed to read");
    return -1;
  }

  return 0;
}

void menu(void) {
	char temp[BUFSIZ];
	char temp2[BUFSIZ];
	bool state = true;
	//memset(temp, NULL, sizeof(temp[BUFLEN])); // for some reason, the first word of the string is always prefixed with an m, if arg > 1.
	char c;

	do {
		printf("1. Make a file system\n");
		printf("2. Mount a file system\n");
		printf("3. Unmount a file system\n");
		printf("4. Open a file\n");
		printf("5. Close a file\n");
		printf("6. Create a file\n");
		printf("7. Delete a file\n");
		printf("8. Read from a file\n");
		printf("9. Write to a file\n");
		printf("10. Get filesize\n");
		printf("11. Set file pointer\n");
		printf("12. Truncate a file\n");
		printf("13. Exit\n");
		printf("Select an option via number: ");
		c = fgetc(stdin);
		printf("\n\n");

		printf("Insert a name: ");
		memset(temp, NULL, sizeof(temp[BUFLEN]));
		fgets(temp, BUFSIZ, stdin);
		printf("\n");

		if (c == '1') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (make_fs(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '2') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (mount_fs(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '3') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (unmount_fs(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '4') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (fs_open(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '5') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (fs_close(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '6') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (fs_create(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '7') {
			printf("Insert a name: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			if (fs_delete(temp) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '8') {
			printf("Insert a fildes: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int a = atoi(temp);
			printf("\n");

			printf("Insert a buf: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			printf("Insert nbytes: ");
			memset(temp2, NULL, sizeof(temp[BUFLEN]));
			fgets(temp2, BUFSIZ, stdin);
			int f = atoi(temp2);
			printf("\n");

			if (fs_read(a, temp, f) != -1) { printf("%s\n", temp); printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '9') {
			printf("Insert a fildes: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int a = atoi(temp);
			printf("\n");

			printf("Insert a buf: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			printf("\n");

			printf("Insert nbytes: ");
			memset(temp2, NULL, sizeof(temp[BUFLEN]));
			fgets(temp2, BUFSIZ, stdin);
			int f = atoi(temp2);
			printf("\n");

			if (fs_write(a, temp, f) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '10') {
			printf("Insert a fildes: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int a = atoi(temp);
			printf("\n");

			if (fs_get_filesize(a) != -1) { printf("File size: %d bytes\n", fs_get_filesize(a)); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '11') {
			printf("Insert a fildes: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int a = atoi(temp);
			printf("\n");

			printf("Insert an offset: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int b = atoi(temp);
			printf("\n");

			if (fs_lseek(a, b) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '12') {
			printf("Insert a fildes: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int a = atoi(temp);
			printf("\n");

			printf("Insert a length: ");
			memset(temp, NULL, sizeof(temp[BUFLEN]));
			fgets(temp, BUFSIZ, stdin);
			int b = atoi(temp);
			printf("\n");

			if (fs_truncate(a, b) != -1) { printf("Operation success.\n"); }
			else { printf("Operation failure.\n"); }
		}
		else if (c == '13') { exit(0); state = false; }
		else { fprintf("Error: Invalid option.\n"); }

		printf("\n\n");
	} while (state == true);
}

int main(int argc, const char* argv[]) {
	menu();
	return 0;
}