#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_utils.h"

int main(int argc, char *argv[]) {
	
	if (argc != 3) {
		fprintf(stderr, "Usage: ext2_ls <virtual disk name> <absolute path>\n");
		exit(1);
	}
	
	int fd = open(argv[1], O_RDWR);
	unsigned char * disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}

	int inode;
	
	inode = get_last_dir_entry_inode(argv[2], disk);
	if (inode == -1) {
		printf ("No such file or directory\n");
		exit(1);
	}
	
	print_directory_contents(inode, disk); //inode now holds the inode of the last directory in the absolute path passed in
	
	return 0;
}
