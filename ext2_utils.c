#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdlib.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/mman.h>
#include <time.h>
#include "ext2.h"

int get_num_free_datablocks(void* disk) {

	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	unsigned int * data_map = (unsigned int *) (disk + (group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE));
	
	int data_map_row;
	int data_map_row_index;
	int total_free_blocks = 0;
	
	//there are 4 data map "rows" since unsigned int can only store 32 bits and data bitmap is 128 bits
	for (data_map_row = 0; data_map_row < 4; data_map_row++){
		for (data_map_row_index = 0; data_map_row_index < 32; data_map_row_index++){
			
			//if bit in data bitmap is 0 then it's free
			if (!(data_map[data_map_row] & (1 << data_map_row_index))) {
				total_free_blocks++;
			}
		}
	}
	
	return total_free_blocks;
	
}

void decrement_free_inode_count(void * disk) {
	struct ext2_super_block * super_block = (struct ext2_super_block*)(disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	super_block->s_free_inodes_count--;
	group_descriptor->bg_free_inodes_count--;
	
}

void increment_free_inode_count(void * disk) {
	struct ext2_super_block * super_block = (struct ext2_super_block*)(disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	super_block->s_free_inodes_count++;
	group_descriptor->bg_free_inodes_count++;
	
}

void decrement_free_datablocks_count(void * disk) {
	struct ext2_super_block * super_block = (struct ext2_super_block*)(disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	super_block->s_free_blocks_count--;
	group_descriptor->bg_free_blocks_count--;
}

void increment_free_datablocks_count(void * disk) {
	struct ext2_super_block * super_block = (struct ext2_super_block*)(disk + EXT2_BLOCK_SIZE);
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	super_block->s_free_blocks_count++;
	group_descriptor->bg_free_blocks_count++;
}

void increment_dir_count(void * disk) {
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	group_descriptor->bg_used_dirs_count++;
}


/*search given_inode for file of the given name*/
int search_inode_for_file(int given_inode, char* wanted_file_name, void * disk) {

	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));

	unsigned int * inode_map= (unsigned int *) (disk + (group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE));

	int inode_index = given_inode - 1;
	
	//if inode bitmap says that the inode is valid
	if (inode_map[0] & (1 << inode_index)) {
		struct ext2_inode * inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * inode_index));
		char dir_entry_name[EXT2_NAME_LEN + 1];
		
		//if the inode is for a directory
		if ((inode->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) { 
			int block_counter = 0;
			struct ext2_dir_entry * dir_entry;

			while (inode->i_block[block_counter] != 0) {
				int block_offset = 0;
				dir_entry = (struct ext2_dir_entry *)(disk + (EXT2_BLOCK_SIZE * inode->i_block[block_counter]));
				while (block_offset < EXT2_BLOCK_SIZE) {
					strncpy(dir_entry_name, dir_entry->name, dir_entry->name_len);
					dir_entry_name[dir_entry->name_len] = '\0';
					
					if ((strcmp(dir_entry_name, wanted_file_name) == 0) && ((dir_entry->file_type == EXT2_FT_REG_FILE))) {
						return dir_entry->inode;
					}
					block_offset = block_offset + dir_entry->rec_len;
					dir_entry = ((void *) dir_entry) + dir_entry->rec_len;
				}
				block_counter++;
			}
		}
	}
	
	return -1;
}


/*search given_inode for directory of the given name*/
int search_inode_for_directory(int given_inode, char* wanted_dir_name, void* disk) {

	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));

	unsigned int * inode_map = (unsigned int *) (disk + (group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE));

	int inode_index = given_inode - 1;
	
	//if inode bitmap says that the inode is valid
	if (inode_map[0] & (1 << inode_index)) {
		struct ext2_inode * inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * inode_index));
		char dir_entry_name[EXT2_NAME_LEN + 1];
		
		//if the inode is for a directory
		if ((inode->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR) {
			int block_counter = 0;
			struct ext2_dir_entry * dir_entry;

			while (inode->i_block[block_counter] != 0) {
				int block_offset = 0;
				dir_entry = (struct ext2_dir_entry *)(disk + 1024 * inode->i_block[block_counter]);
				while (block_offset < 1024) {
					strncpy(dir_entry_name, dir_entry->name, dir_entry->name_len);
					dir_entry_name[dir_entry->name_len] = '\0';
					
					if ((strcmp(dir_entry_name, wanted_dir_name) == 0) && ((dir_entry->file_type == EXT2_FT_DIR))) {
						return dir_entry->inode;
					}
					block_offset = block_offset + dir_entry->rec_len;
					dir_entry = ((void *) dir_entry) + dir_entry->rec_len;
				}
				block_counter++;
			}
		}
	}
	
	return -1;
}


/*returns inode of last directory entry in the path given, if possible*/
int get_last_dir_entry_inode(char * path, void * disk) {
	
		char * path_copy = malloc(strlen(path) + 1);	
		char * token;
		
		//always starts at root
		int inode = EXT2_ROOT_INO;
		
		strcpy(path_copy, path);
		
		//if just root was passed in
		if (strcmp(path_copy, "/") == 0) {
			free(path_copy);
			
			//root is stored in inode by default, so return that
			return inode;
		}
		
		//all valid paths start with "/"
		if (path[0] == '/') {
			memmove(path_copy, path_copy + 1, strlen(path_copy)); //remove first '/' from path
		}
		else {
			free(path_copy);
			return -1;
		}
		token = strtok(path_copy, "/");
		
		while (token != NULL) {
			
			//search for token in inode's data
			inode = search_inode_for_directory(inode, token, disk); 
			
			//couldn't find token in inode's data
			if (inode == -1) {
				free(path_copy);
				return -1;
			}
			
			//token now contains the next directory name in the path
			token = strtok(NULL, "/"); 
		}
		
		free(path_copy);
		return inode;
}


void print_directory_contents(int directory_inode, void * disk) {
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	int inode_index = directory_inode - 1;
	struct ext2_inode * inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * inode_index));
          
    int block_counter = 0;
	struct ext2_dir_entry * dir_entry;
	char dir_entry_name[EXT2_NAME_LEN + 1];
		
	while ((inode->i_block[block_counter] != 0) && (block_counter < 12)) {
		int block_offset = 0;
		dir_entry = (struct ext2_dir_entry *)(disk + 1024 * inode->i_block[block_counter]);
		while (block_offset < 1024) {
			strncpy(dir_entry_name, dir_entry->name, dir_entry->name_len);
			dir_entry_name[dir_entry->name_len] = '\0';
            printf("%s\n", dir_entry_name); 
			block_offset = block_offset + dir_entry->rec_len;
			dir_entry = ((void *) dir_entry) + dir_entry->rec_len;
		}
		block_counter++;
	}
	
}

int get_next_free_inode(void * disk) {
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	unsigned int * inode_map = (unsigned int *) (disk + (group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE));
	
	int inode_map_index;
	
	//loop starts at inode_map_index = 11 since the first 11 inodes are reserved
	for (inode_map_index = EXT2_GOOD_OLD_FIRST_INO; inode_map_index < 32; inode_map_index++){
		
		//returns first inode number that is not being used
		if (!(inode_map[0] & (1 << inode_map_index))) {
			
			//+1 since inode number starts at 1
			return inode_map_index + 1;
		}
	}
	
	//only get here if all bits in inode bitmap are 1
	return -1;
}

int get_next_free_datablock(void * disk) {
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	unsigned int * data_map = (unsigned int *) (disk + (group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE));
	
	int data_map_row;
	int data_map_row_index;
	
	//there are 4 data map "rows" since unsigned int can only store 32 bits and data bitmap is 128 bits
	for (data_map_row = 0; data_map_row < 4; data_map_row++){
		for (data_map_row_index = 0; data_map_row_index < 32; data_map_row_index++){
			
			//returns first block number not being used (block number starts at 1)
			if (!(data_map[data_map_row] & (1 << data_map_row_index))) {
				int block_num;
				
				//+1 since block number starts at 1
				block_num = (data_map_row * 32) + data_map_row_index + 1;
				return block_num;
			}
		}
	}
	
	//only get here if all bits in data bitmap are 1
	return -1;
}

void update_inode_bitmap(int inode_num, int value, void * disk) {
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	unsigned int * inode_map = (unsigned int *) (disk + (group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE));
	
	int inode_index = inode_num - 1;
	
	if (value == 0) {
		inode_map[0] = inode_map[0] & ~(1 << inode_index);
	}
	else if (value == 1) {
		inode_map[0] = inode_map[0] | (1 << inode_index);
	}
	else {
		fprintf(stderr, "ERROR: %s\n", strerror(EINVAL));
		exit(EINVAL);
	}
	
}

void update_data_bitmap(int block_num, int value, void * disk) {
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	unsigned int * data_map = (unsigned int *) (disk + (group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE));
	
	int block_index = block_num - 1;
	int data_map_row = block_index / 32;
	int data_map_row_index = block_index % 32;
	
	if (value == 0) {
		data_map[data_map_row] = data_map[data_map_row] & ~(1 << data_map_row_index);
	}
	else if (value == 1) {
		data_map[data_map_row] = data_map[data_map_row] | (1 << data_map_row_index);
	}
	else {
		fprintf(stderr, "ERROR: %s\n", strerror(EINVAL));
		exit(EINVAL);
	}
	
}

void initialize_new_directory(int inode_num, int datablock_num, int parent_inode_num, void * disk) {
	
	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	int inode_index = inode_num - 1;
	
	//initialize inode in inode table
	struct ext2_inode * inode = (struct ext2_inode *)(disk + (EXT2_BLOCK_SIZE * group_descriptor->bg_inode_table) + 128 * inode_index);
	inode->i_mode = EXT2_S_IFDIR;
	inode->i_uid = 0;
	inode->i_size = EXT2_BLOCK_SIZE;
	inode->i_ctime = (unsigned int) time(NULL);
	inode->i_dtime = 0;
	inode->i_gid = 0;
	inode->i_links_count = 2;
	inode->i_blocks = 2;
	inode->osd1 = 0;
	inode->i_block[0] = datablock_num;
	
	int i;
	for (i = 1; i < 15; i++) {
		inode->i_block[i] = 0;
	}
	
	inode->i_generation = 0;
	inode->i_file_acl = 0;
	inode->i_faddr = 0;
	inode->extra[0] = 0;
	inode->extra[1] = 0;
	inode->extra[2] = 0;
	
	//initialize data block that inode points to
	struct ext2_dir_entry * dir_entry = (struct ext2_dir_entry *)(disk + (EXT2_BLOCK_SIZE * datablock_num));
	
	//initialize current directory "." directory entry
	int exact_rec_len = sizeof(dir_entry->inode) + sizeof(dir_entry->rec_len) + sizeof(dir_entry->name_len) + sizeof(dir_entry->file_type) + strlen(".");
	
	if ((exact_rec_len % 4) == 1) {
		dir_entry->rec_len = exact_rec_len + 3;
	}
	else if ((exact_rec_len % 4) == 2) { 
		dir_entry->rec_len = exact_rec_len + 2;
	}
	else if ((exact_rec_len % 4) == 3) {
		dir_entry->rec_len = exact_rec_len + 1;
	}
	//exact_rec_len is already a multiple of 4
	else {
		dir_entry->rec_len = exact_rec_len;
	}
	
	int curr_dir_rec_len = dir_entry->rec_len;

	dir_entry->inode = inode_num;
	dir_entry->name_len = strlen(".");
	dir_entry->file_type = EXT2_FT_DIR;
	strncpy(dir_entry->name, ".", strlen("."));
	
	//initialize parent directory ".." directory entry
	dir_entry = ((void *) dir_entry) + curr_dir_rec_len;
	dir_entry->inode = parent_inode_num;
	dir_entry->rec_len = EXT2_BLOCK_SIZE - curr_dir_rec_len;
	dir_entry->name_len = strlen("..");
	dir_entry->file_type = EXT2_FT_DIR;
	strncpy(dir_entry->name, "..", strlen(".."));
	
	//update parent link count
	int parent_inode_index = parent_inode_num - 1;
	inode = (struct ext2_inode *)(disk + (EXT2_BLOCK_SIZE * group_descriptor->bg_inode_table) + 128 * parent_inode_index);	
	inode->i_links_count++;
	
	update_inode_bitmap(inode_num, 1, disk);
	decrement_free_inode_count(disk);
	increment_dir_count(disk);
	
}

/* in here, check for space in parent inode, allocate new datablock for parent if needed, then look for new datablock*/
int update_parent_directory_for_new_dir(int parent_inode_num, int new_dir_inode, char * new_dir_name, void * disk) {

	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	int parent_inode_index = parent_inode_num - 1;
	struct ext2_inode * inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * parent_inode_index));
	
	int i_block_index;
	int next_free_i_block_index = -1;
	for (i_block_index = 0; i_block_index < 12; i_block_index++) {
		if (inode->i_block[i_block_index] == 0) {
			next_free_i_block_index = i_block_index;
			break;
		}
	}
	
	//all i_block indices of parent directory already point to used datablocks
	if (next_free_i_block_index == -1) {
		return -1;
	}
	
	int num_free_datablocks = get_num_free_datablocks(disk);
		
	//not enough space for new datablock in parent and new datablock for new directory
	if (num_free_datablocks < 2) {
		return -1;
	}
	
	//get a new datablock for parent directory and mark it as used
	int new_parent_datablock = get_next_free_datablock(disk);
	inode->i_block[next_free_i_block_index] = new_parent_datablock;
	update_data_bitmap(new_parent_datablock, 1, disk);
	decrement_free_datablocks_count(disk);	
		
	//get a new datablock for new directory and mark it as used
	int new_directory_datablock = get_next_free_datablock(disk);
	update_data_bitmap(new_directory_datablock, 1, disk);	
	decrement_free_datablocks_count(disk);
	
	//now update new_parent_datablock with a new dir_entry 
	struct ext2_dir_entry * dir_entry = (struct ext2_dir_entry *)(disk + (EXT2_BLOCK_SIZE * new_parent_datablock));
	dir_entry->inode = new_dir_inode;
	dir_entry->rec_len = EXT2_BLOCK_SIZE;
	dir_entry->name_len = strlen(new_dir_name);
	dir_entry->file_type = EXT2_FT_DIR;
	strncpy(dir_entry->name, new_dir_name, strlen(new_dir_name));
	
	//update other parent directory inode members
	inode->i_size = inode->i_size + EXT2_BLOCK_SIZE;
	inode->i_blocks = inode->i_blocks + 2;
	
	
	return new_directory_datablock;
}

void remove_trailing_slashes(char * path) {
	int curr_char;
	for (curr_char = strlen(path) - 1; curr_char >= 0 ; curr_char--) {
		if (path[curr_char] != '/') {
			break;
		}
		path[curr_char] = '\0';
	}
	
}
