#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <libgen.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <errno.h>
#include "ext2.h"
#include "ext2_utils.h"

int main(int argc, char *argv[]) {
	
	if (argc != 3) {
		fprintf(stderr, "Usage: ext2_mkdir <virtual disk name> <absolute path>\n");
		exit(1);
	}
	
	int fd = open(argv[1], O_RDWR);
	unsigned char * disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	
	char * path = malloc(strlen(argv[2]) + 1);	
	char * parent_directory_path;
	char * path_copy;
	char * base_name = malloc(EXT2_NAME_LEN + 1);
	
	strcpy(path, argv[2]);
	remove_trailing_slashes(path);

	if (strcmp(path, "") == 0) { //just slashes were passed in as absolute path ie. root
		free(path);
		fprintf(stderr, "ERROR: %s\n", strerror(EEXIST));
		exit(EEXIST);
	}
	
	parent_directory_path = strdup(path);
	path_copy = strdup(path);

	dirname(parent_directory_path);
	strcpy(base_name, basename(path_copy));
	
	int parent_directory_inode;
	int desired_directory_inode;
	
	parent_directory_inode = get_last_dir_entry_inode(parent_directory_path, disk); 
	if (parent_directory_inode == -1) { //check if path is invalid
		free(path);
		free(parent_directory_path);
		free(path_copy);
		free(base_name);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
		exit(ENOENT);
	}
	
	desired_directory_inode = search_inode_for_file(parent_directory_inode, base_name, disk);
	if (desired_directory_inode != -1) { //file with desired name already exists
		free(path);
		free(parent_directory_path);
		free(path_copy);
		free(base_name);
		fprintf(stderr, "ERROR: %s\n", strerror(EEXIST));
		exit(EEXIST);
	}
		
	desired_directory_inode = search_inode_for_directory(parent_directory_inode, base_name, disk);
	if (desired_directory_inode != -1) { //desired directory already exists
		free(path);
		free(parent_directory_path);
		free(path_copy);
		free(base_name);
		fprintf(stderr, "ERROR: %s\n", strerror(EEXIST));
		exit(EEXIST);
	}
	
	int next_free_inode = get_next_free_inode(disk);

	//all bits in inode bitmap were 1
	if (next_free_inode == -1) {
		free(path);
		free(parent_directory_path);
		free(path_copy);
		free(base_name);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOSPC));
		exit(ENOSPC);
	}
	
	//update parent directory for the new directory we're making
	int next_free_data_block = update_parent_directory_for_new_dir(parent_directory_inode, next_free_inode, base_name, disk);
	
	//there was not enough space
	if (next_free_data_block == -1) {
		free(path);
		free(parent_directory_path);
		free(path_copy);
		free(base_name);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOSPC));
		exit(ENOSPC);
	}
	
	initialize_new_directory(next_free_inode, next_free_data_block, parent_directory_inode, disk);
	
	free(path);
	free(parent_directory_path);
	free(base_name);
	return 0;
}
