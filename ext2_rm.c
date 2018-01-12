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
#include <time.h>
int main(int argc, char *argv[]) {
	
	if (argc != 3) { //check input
		fprintf(stderr, "Usage: ext2_rm <virtual disk name> <absolute path>\n");
		exit(1);
	}
	
	char path [strlen(argv[2])]  ;
	strcpy(path, argv[2]);
	int fd = open(argv[1], O_RDWR);
	char *disk = mmap(NULL, 128 * 1024, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
	
	if (disk == MAP_FAILED) {
		perror("mmap");
		exit(1);
	}
	
	//gets inode of the directory containing the file.
	int directory_inode;
	directory_inode = get_last_dir_entry_inode(dirname(argv[2]), disk); 
	
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
		dir_entry = (struct ext2_dir_entry *)(disk + 1024 * inode->i_block[block_counter]);
		
		while (block_offset < 1024) {
			
			strncpy(entry_name, dir_entry->name, dir_entry->name_len);
			entry_name[dir_entry->name_len] = '\0';
			
			//Checks if directory entry is the one associated with the file we want to delete
            if (strcmp(filename, entry_name) == 0){
				int file_inode_index;
				file_inode_index = dir_entry->inode -1;
				struct ext2_inode * file_inode = (struct ext2_inode *)(disk + (group_descriptor->bg_inode_table * EXT2_BLOCK_SIZE) + (128 *file_inode_index));
				
				//If it is a directory, exit with the appropriate error
				if ((file_inode->i_mode & EXT2_S_IFDIR) == EXT2_S_IFDIR){ 
					fprintf(stderr, "ERROR: %s\n", strerror(EISDIR));
					exit(EISDIR);
				}
			
				//Decrement the links to the inode associated with this file
				file_inode->i_links_count -= 1;
				
				 //if it is the first in a block group set the inode to 0 so that it can be recognized as a blank record
				if (previous_rec_len == 0){
					dir_entry->inode = 0; 
				}
				
				else {
					int temp_rec = dir_entry->rec_len;
					dir_entry = ((void *) dir_entry) - previous_rec_len;
					//make the previous directory entry point to the next entry after this one.
					dir_entry->rec_len += temp_rec;
				}
				
				//If there are other links, we don't want to delete the Inode.
				if ( file_inode->i_links_count > 0){ 
					//hardlink is deleted, more links exist to the inode
					return 0;
				}
				
				//free the inode associated with the file
				increment_free_inode_count(disk);
				update_inode_bitmap(file_inode_index+1,0,disk);
				
				//set the inode deletion time
				file_inode->i_dtime = time(NULL);
				
				int i_block_count;
							
				//free the data blocks associated with the file
				for(i_block_count = 0; i_block_count < 15; i_block_count++){
					if(file_inode->i_block[i_block_count] != 0){
						update_data_bitmap(file_inode->i_block[i_block_count],0,disk);
						increment_free_datablocks_count(disk);						
					}	
				}
				//file is deleted
				return 0;
	
			} 
			previous_rec_len = dir_entry->rec_len;
			block_offset = block_offset + previous_rec_len;
			dir_entry = ((void *) dir_entry) + previous_rec_len;
		}
		block_counter++;
	}
	//the file path specified does not exist.
	fprintf(stderr, "ERROR: %s\n", strerror(ENOENT));
	exit(ENOENT);
}
