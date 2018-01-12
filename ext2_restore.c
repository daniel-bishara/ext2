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
#include <libgen.h>

int main(int argc, char *argv[]) {
	
	if (argc != 3) {
		fprintf(stderr, "Usage: ext2_restore <virtual disk name> <absolute path>\n");
		exit(1);
	}
	
	int fd = open(argv[1], O_RDWR);
	char * disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	
	char path [strlen(argv[2]) + 1]  ;
	strcpy(path, argv[2]);

	int directory_inode;
	char dir_name[strlen(argv[2] + 1)];
	strcpy(dir_name, argv[2]);
	
	//gets inode of the directory containing the file.
	directory_inode = get_last_dir_entry_inode(dirname(dir_name), disk); 
	
	//if the directory containing the file does not exist.
	if (directory_inode == -1) {
		fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
		exit(ENOENT);
	}
	
	remove_trailing_slashes(path);
	
	char * filename = basename(path);

	struct ext2_group_desc * group_descriptor = (struct ext2_group_desc *)(disk + (2 * EXT2_BLOCK_SIZE));
	
	int inode_index = directory_inode - 1;
	struct ext2_inode * inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * inode_index));
          
    int block_counter = 0;
	struct ext2_dir_entry * dir_entry;
	char entry_name[EXT2_NAME_LEN + 1];
	
	
	while (inode->i_block[block_counter] != 0) {
		int previous_rec_len = 0;
		int block_offset = 0;
		int offset_tracker = 0;
		int last_ls_rec_len = 0;
		dir_entry = (struct ext2_dir_entry *)(disk + (1024 * inode->i_block[block_counter]));
		while (block_offset < 1024) {

			strncpy(entry_name, dir_entry->name, dir_entry->name_len);
			entry_name[dir_entry->name_len] = '\0';
			
			//attempt to restore if we find the file
            if (strcmp(filename, entry_name)==0){
			
				//if the deleted file is the first in the block, we cannot restore it
				if (block_offset==0){
					exit(ENOENT);
					return -1;
				}
				
				//checks if it is a directory
				if(dir_entry->file_type==EXT2_FT_DIR){
					fprintf(stderr, "ERROR: %s\n", strerror(EISDIR));
					exit(EISDIR);
				}
								
				 unsigned int * inode_map= (unsigned int *) (disk + (group_descriptor->bg_inode_bitmap * EXT2_BLOCK_SIZE));
				 unsigned int * data_map= (unsigned int *) (disk + (group_descriptor->bg_block_bitmap * EXT2_BLOCK_SIZE));
				
				//if the inode has been replaced
				if (inode_map[0] & (1 << (dir_entry->inode -1))){
					fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
					exit(ENOENT);
				}
				
				struct ext2_inode * deleted_inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 * (dir_entry->inode -1)));
				int x = 0;
				for(x=0; x< 15; x++){
					//check if each data block needed has been reallocated
					if(deleted_inode->i_block[x] !=0){
						int block_index = deleted_inode->i_block[x] - 1;
						int data_map_row= block_index /32;
						int data_map_row_index = block_index % 32;
							
						//if data block is reallocated, it is unrecoverable
						if (data_map[data_map_row] & (1 << data_map_row_index)) {
							fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
							exit(ENOENT);
							return -1;
						}
					}
				}
					
				//restore the inode
				update_inode_bitmap(dir_entry->inode,1,disk);
				decrement_free_inode_count(disk);
				
				//restore all the data blocks
				for(x=0; x < 15; x++){
					if(deleted_inode->i_block[x] !=0){
						update_data_bitmap(deleted_inode->i_block[x],1,disk);
						decrement_free_datablocks_count(disk);
					}
				}
				
				deleted_inode->i_dtime = 0;
				dir_entry->rec_len = offset_tracker;
				deleted_inode->i_links_count++;
				dir_entry = ((void *) dir_entry) - last_ls_rec_len + offset_tracker ;
				dir_entry->rec_len = last_ls_rec_len - offset_tracker ;
				char entry_name2[EXT2_NAME_LEN + 1];
				strncpy(entry_name2, dir_entry->name, dir_entry->name_len);
				entry_name2[dir_entry->name_len] = '\0';
				return 0;

			} 
			
			//Find the actual size of the previous directory entry
			int namesize = strlen(dir_entry->name);
			previous_rec_len =  (sizeof(struct ext2_dir_entry) + namesize);
			
			if (previous_rec_len % 4 != 0 ){
				previous_rec_len += (4 - (previous_rec_len % 4)); //padding 
			}
			
			if(offset_tracker == 0){
				//store the distance until the next actual directory_entry
				offset_tracker = dir_entry->rec_len - previous_rec_len;
				//store the last actual rec_len
				last_ls_rec_len = dir_entry->rec_len;
			}
			
			else{
				//the distance until the next actual directory_entry gets smaller
				offset_tracker -= previous_rec_len;
			}

			//go to the next entry, whether it exists or not.
			block_offset = block_offset + previous_rec_len;
			dir_entry = ((void *) dir_entry) + previous_rec_len;
		}
		
		block_counter++;
	}
	
	fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
	exit(ENOENT);
}

