#ifndef EXT2_UTILS
#define EXT2_UTILS

void decrement_free_inode_count(void * disk);

void increment_free_inode_count(void * disk);

void decrement_free_datablocks_count(void * disk);

void increment_free_datablocks_count(void * disk);

void increment_dir_count(void * disk);

int search_inode_for_file(int given_inode, char* wanted_dir_entry_name, void* disk);

int search_inode_for_directory(int given_inode, char* wanted_dir_entry_name, void* disk);

int get_last_dir_entry_inode(char * path, void * disk);
	
void print_directory_contents(int inode, void* disk);

int get_next_free_inode(void * disk);

int get_next_free_datablock(void * disk);
	
void initialize_new_directory(int inode_num, int datablock_num, int parent_inode_num, void * disk);	
	
void update_data_bitmap(int block_num, int value, void * disk);	
	
int update_parent_directory_for_new_dir(int parent_inode_num, int new_dir_inode, char * new_dir_name, void * disk);

void update_inode_bitmap(int inode_num, int value, void * disk) ;

int get_num_free_datablocks(void* disk);

void remove_trailing_slashes(char * path);

#endif
