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
#include <math.h>
#include <time.h>
#include "ext2.h"
#include "ext2_utils.h"

#define INDIRECT_POINTER_BLOCK 13

int main(int argc, char *argv[]) {
	
	if (argc != 4) {
		fprintf(stderr, "Usage: ext2_cp <virtual disk name> <path to file> <absolute path>\n");
		exit(1);
	}
	
	char * file_path = malloc(strlen(argv[2]) + 1);
	
	strcpy(file_path, argv[2]);
	
	//file doesn't exist
	if (access(file_path, F_OK) == -1) {
		free(file_path);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
		exit(ENOENT);
	}
	
	//don't have read permission for file
	if (access(file_path, R_OK) == -1) {
		free(file_path);
		fprintf(stderr, "ERROR: %s\n", strerror(EPERM));
		exit(EPERM);
	}
	
	struct stat path_stat;
    stat(file_path, &path_stat);
	
	
	if(S_ISREG(path_stat.st_mode) != 1) {
		free(file_path);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
		exit(ENOENT);
	}
	
	FILE * file = fopen(file_path, "r");
	if (file == NULL) {
		free(file_path);
		fprintf(stderr, "ERROR: %s\n", strerror(EIO));
		exit(EIO);
	}
	
	int fd = open(argv[1], O_RDWR);
	unsigned char * disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	if (disk == MAP_FAILED) {
		free(file_path);
		perror("mmap");
		exit(1);
	}
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	
	char * destination_path = malloc(strlen(argv[3]) + 1);
	char * base_name = malloc(EXT2_NAME_LEN + 1);
	char * destination_path_copy;
	char * parent_directory_path;

	strcpy(destination_path, argv[3]);
	destination_path_copy = strdup(destination_path);
	parent_directory_path = strdup(destination_path);
	
	dirname(parent_directory_path);
	
	remove_trailing_slashes(destination_path_copy);
	strcpy(base_name, basename(destination_path_copy));

	int parent_inode = get_last_dir_entry_inode(parent_directory_path, disk);
	if (parent_inode == -1) { //check if path is invalid
		free(file_path);
		free(destination_path);
		free(base_name);
		free(destination_path_copy);
		free(parent_directory_path);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
		exit(ENOENT);
	}
	int parent_inode_index = parent_inode - 1;
	
	
	int desired_file_inode = search_inode_for_file(parent_inode, base_name, disk);
	if (desired_file_inode != -1) { //file with desired name already exists
		free(file_path);
		free(destination_path);
		free(base_name);
		free(destination_path_copy);
		free(parent_directory_path);
		fprintf(stderr, "ERROR: %s\n", strerror(EEXIST));
		exit(EEXIST);
	}
	
	desired_file_inode = search_inode_for_directory(parent_inode, base_name, disk);
	if (desired_file_inode != -1) { //directory already exists with same name as file
		free(file_path);
		free(destination_path);
		free(base_name);
		free(destination_path_copy);
		free(parent_directory_path);
		fprintf(stderr, "ERROR: %s\n", strerror(EEXIST));
		exit(EEXIST);
	}
	
	int next_free_inode = get_next_free_inode(disk);
	
	//all bits in inode bitmap are 1
	if (next_free_inode == -1) {
		free(file_path);
		free(destination_path);
		free(base_name);
		free(destination_path_copy);
		free(parent_directory_path);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOSPC));
		exit(ENOSPC);
	}
	
	int file_size = path_stat.st_size;
	int num_free_datablocks = get_num_free_datablocks(disk);
	
	//basically integer division that always rounds up
	int num_datablocks_needed = (file_size + (EXT2_BLOCK_SIZE-1)) / EXT2_BLOCK_SIZE;
	
	//file is too big for the number of datablocks left
	if (num_free_datablocks < num_datablocks_needed) { 
		free(file_path);
		free(destination_path);
		free(base_name);
		free(destination_path_copy);
		free(parent_directory_path);
		fprintf(stderr, "ERROR: %s\n", strerror(ENOSPC));
		exit(ENOSPC);
	}
	
	//covers the case where you need an extra datablock for the indirect pointer
	if (num_datablocks_needed > 12) {
		if ((num_free_datablocks < (num_datablocks_needed + 1))) {
			free(file_path);
			free(destination_path);
			free(base_name);
			free(destination_path_copy);
			free(parent_directory_path);
			fprintf(stderr, "ERROR: %s\n", strerror(ENOSPC));
			exit(ENOSPC);
		}
	
	}
	
	struct ext2_inode * inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * parent_inode_index));
	
	//find last data block
	int last_block_index = 0;
	while ((last_block_index < 15) && (inode->i_block[last_block_index] != 0)) {
		last_block_index++;
	}
	last_block_index--;
				
	int actual_rec_len = 0;
	int new_actual_rec_len = 0;
	int block_offset = 0;
	
	struct ext2_dir_entry * dir_entry = (struct ext2_dir_entry *)(disk + 1024 * inode->i_block[last_block_index]);
	
	while (block_offset < 1024) {

		int previous_rec_len = dir_entry->rec_len;

		//looking for last directory entry in the block
		if (dir_entry->rec_len + block_offset == 1024) {
			int namesize = strlen(dir_entry->name);
			actual_rec_len =  (sizeof(struct ext2_dir_entry) + namesize);
					
			if (actual_rec_len % 4 != 0 ){
				actual_rec_len += (4 - actual_rec_len % 4); //padding 
			}
						
						
			int new_namesize = strlen(base_name);
			new_actual_rec_len =  (sizeof(struct ext2_dir_entry) + new_namesize);
						
			if (new_actual_rec_len % 4 != 0 ){
				new_actual_rec_len += (4 - new_actual_rec_len % 4); //padding 
			}
						

						
			//there is space in block
			if (dir_entry->rec_len - actual_rec_len >= new_actual_rec_len) { 
				dir_entry->rec_len = actual_rec_len;
				dir_entry = ((void *) dir_entry) + actual_rec_len;
				dir_entry->rec_len = previous_rec_len - actual_rec_len;
			}
			else {
				
				//already on last block of parent directory, so no more space
				if (last_block_index == 14) {
					free(file_path);
					free(destination_path);
					free(base_name);
					free(destination_path_copy);
					free(parent_directory_path);
					fprintf(stderr, "ERROR: %s\n", strerror(ENOSPC));
					exit(ENOSPC);
				}
				
				int next_free_datablock = get_next_free_datablock(disk);

				update_data_bitmap(next_free_datablock, 1, disk);
				decrement_free_datablocks_count(disk);
				
				inode->i_block[last_block_index + 1] = next_free_datablock;	
				
				dir_entry = (struct ext2_dir_entry *)(disk + 1024 * inode->i_block[last_block_index + 1]);
				dir_entry->rec_len = 1024;
			}
			
			dir_entry->inode = next_free_inode;
			dir_entry->name_len = strlen(base_name);
			dir_entry->file_type = EXT2_FT_REG_FILE;
			strncpy(dir_entry->name, base_name, strlen(base_name));
			break;	
		}
	
		block_offset = block_offset + dir_entry->rec_len;
		dir_entry = ((void *) dir_entry) + dir_entry->rec_len;
	}
	
	
	int datablocks_needed_index;
	int new_datablock;
	int new_file_inode_index = next_free_inode - 1;
	char file_contents[file_size];
	int file_contents_index = 0;
	char c;
	int char_counter = 0;
	int left_to_copy = file_size;
	char * file_contents_block;
	int char_index = 0;
	struct ext2_inode * new_file_inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * new_file_inode_index));
	
	while(1) {
		c = fgetc(file);
		if (c == EOF) {
			break;
		}
		file_contents[char_counter] = c;
		char_counter++;
	}
	
	//don't need to use indirect pointers
	if (num_datablocks_needed <= 12) {
		for (datablocks_needed_index = 0; datablocks_needed_index < num_datablocks_needed; datablocks_needed_index++) {
			new_datablock = get_next_free_datablock(disk);
			update_data_bitmap(new_datablock, 1, disk);
			decrement_free_datablocks_count(disk);
			new_file_inode->i_block[datablocks_needed_index] = new_datablock;
			char_index = 0;
			file_contents_block = (char *) (disk + (new_datablock * EXT2_BLOCK_SIZE));
			
			while (left_to_copy > 0) {
				file_contents_block[char_index] = file_contents[file_contents_index];

				left_to_copy--;
				file_contents_index++;
				char_index++;
				
				//every 1024 characters, move to a new block
				if ((file_contents_index % EXT2_BLOCK_SIZE) == 0) {
					break;
				}
			}
			

		}
		
		//set next i_block index to 0 to show that there are no more datablocks used after
		if (datablocks_needed_index < 15) {
			new_file_inode->i_block[datablocks_needed_index] = 0;
		}
	}
	
	
	//file is big enough to warrant using an indirect pointer
	else {
		
		//fill up first 12 datablocks first
		for (datablocks_needed_index = 0; datablocks_needed_index < (INDIRECT_POINTER_BLOCK - 1); datablocks_needed_index++) {
			new_datablock = get_next_free_datablock(disk);
			update_data_bitmap(new_datablock, 1, disk);
			decrement_free_datablocks_count(disk);
			new_file_inode->i_block[datablocks_needed_index] = new_datablock;
			char_index = 0;
			file_contents_block = (char *) (disk + (new_datablock * EXT2_BLOCK_SIZE));
			
			//only write enough to fill first 12 datablocks
			while (char_index < (EXT2_BLOCK_SIZE * (INDIRECT_POINTER_BLOCK - 1))) {
				file_contents_block[char_index] = file_contents[file_contents_index];
	
				left_to_copy--;
				file_contents_index++;
				char_index++;
				
				//every 1024 characters, move to a new block
				if ((file_contents_index % EXT2_BLOCK_SIZE) == 0) {
					break;
				}
			}
			

		}
		
		//use indirect pointer to finish copying file contents
		int indirect_block = get_next_free_datablock(disk);
		update_data_bitmap(indirect_block, 1, disk);
		decrement_free_datablocks_count(disk);
	
		new_file_inode->i_block[INDIRECT_POINTER_BLOCK - 1] = indirect_block;
			
		int indirect_block_index = 0;
		unsigned int * indirect_datablock = (unsigned int *) (disk + (indirect_block * EXT2_BLOCK_SIZE));
		
		int new_indirect_block = 0;
		
		for (datablocks_needed_index = 0; datablocks_needed_index < (num_datablocks_needed - (INDIRECT_POINTER_BLOCK - 1)); datablocks_needed_index++) {
			new_indirect_block = get_next_free_datablock(disk);
			update_data_bitmap(new_indirect_block, 1, disk);
			decrement_free_datablocks_count(disk);
			indirect_datablock[indirect_block_index] = new_indirect_block;
			char_index = 0;
			file_contents_block = (char *) (disk + (new_indirect_block * EXT2_BLOCK_SIZE));
		
			while (left_to_copy > 0) {
				file_contents_block[char_index] = file_contents[file_contents_index];
				
				left_to_copy--;
				file_contents_index++;
				char_index++;
				
				//every 1024 characters, move to a new block
				if ((file_contents_index % EXT2_BLOCK_SIZE) == 0) {
					break;
				}
			}
			indirect_block_index++;
		}
		
	}

	new_file_inode->i_mode = EXT2_S_IFREG;
	new_file_inode->i_uid = 0;
	new_file_inode->i_size = file_size;
	new_file_inode->i_ctime = (unsigned int) time(NULL);
	new_file_inode->i_dtime = 0;
	new_file_inode->i_gid = 0;
	new_file_inode->i_links_count = 1;
	new_file_inode->osd1 = 0;
	new_file_inode->i_generation = 0;
	new_file_inode->i_file_acl = 0;
	new_file_inode->i_faddr = 0;
	new_file_inode->extra[0] = 0;
	new_file_inode->extra[1] = 0;
	new_file_inode->extra[2] = 0;	
	new_file_inode->i_blocks = num_datablocks_needed * 2;
	
	//accounts for datablock to store pointers to more datablocks
	if (num_datablocks_needed > INDIRECT_POINTER_BLOCK) {
		new_file_inode->i_blocks = new_file_inode->i_blocks + 2;
	}
	
	int i;
	for (i = INDIRECT_POINTER_BLOCK; i < (INDIRECT_POINTER_BLOCK + 2); i++) {
		inode->i_block[i] = 0;
	}
	
	update_inode_bitmap(next_free_inode, 1, disk);
	decrement_free_inode_count(disk);
	
	free(file_path);
	free(destination_path);
	free(base_name);
	free(destination_path_copy);
	free(parent_directory_path);
	
	return 0;
}


/*
 This program takes three command line arguments. The first is the name of an ext2 formatted virtual 
 disk. The second is the path to a file on your native operating system, and the third is an absolute
 path on your ext2 formatted disk. The program should work like cp, copying the file on your native 
 file system onto the specified location on the disk. If the specified file or target location does
 not exist, then your program should return the appropriate error (ENOENT). If the target is a file with 
 the same name that already exists, you should not overwrite it (as cp would), just return EEXIST instead.*/

 
 