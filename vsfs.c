#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <fcntl.h>
#include "vsfs.h"
#include <string.h>

// globals  =======================================
int vs_fd; // file descriptor of the Linux file that acts as virtual disk.
             // this is not visible to an application.
typedef struct {
    int total_blocks;          // Total number of blocks in the disk
    int fat_start_block;       // Starting block number of the FAT
    int fat_blocks;            // Number of blocks used by the FAT
    int root_dir_start_block;  // Starting block number of the root directory
    int root_dir_blocks;       // Number of blocks used by the root directory
} Superblock;

typedef struct {
    char filename[30];  // File name (up to 30 characters)
    int size;           // Size of the file in bytes
    int start_block;    // Starting block number of the file's data
    int is_used;        // Indicator if the directory entry is in use (1 for used, 0 for free)
} DirectoryEntry;

Superblock superblock; // Global superblock

int *g_fat;              // Global FAT
DirectoryEntry *g_root_dir; // Global root directory

typedef struct {
    int used;          // Flag to indicate if the entry is in use
    char filename[30]; // Filename
    int mode;          // Mode in which the file is opened (read or append)
    int file_size;     // Size of the file
    int file_pos;      // Current position in the file
    int start_block;   // Starting block of the file in the FAT
} OpenFileEntry;

OpenFileEntry open_file_table[16];
// ========================================================


// read block k from disk (virtual disk) into buffer block.
// size of the block is BLOCKSIZE.
// space for block must be allocated outside of this function.
// block numbers start from 0 in the virtual disk. 
int read_block (void *block, int k)
{
    int n;
    int offset;
    printf("in here");
    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = read (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("read error\n");
	return -1;
    }
    return (0); 
}

// write block k into the virtual disk. 
int write_block (void *block, int k)
{
    int n;
    int offset;

    offset = k * BLOCKSIZE;
    lseek(vs_fd, (off_t) offset, SEEK_SET);
    n = write (vs_fd, block, BLOCKSIZE);
    if (n != BLOCKSIZE) {
	printf ("write error\n");
	return (-1);
    }
    return 0; 
}


/**********************************************************************
   The following functions are to be called by applications directly. 
***********************************************************************/
// this function is partially implemented.
int vsformat (char *vdiskname, unsigned int m)
{
    char command[1000];
    int size;
    int num = 1;
    int count;
    size  = num << m;
    count = size / BLOCKSIZE;
    //printf ("%d %d", m, size);
    sprintf (command, "dd if=/dev/zero of=%s bs=%d count=%d",
             vdiskname, BLOCKSIZE, count);
    printf ("executing command = %s\n", command);
    system (command);

    vs_fd = open(vdiskname, O_RDWR);
    if (vs_fd == -1) {
        printf("Error opening virtual disk\n");
        return -1;
    }

    Superblock sb;
    sb.total_blocks = count;
    sb.fat_start_block = 1; // Superblock is at block 0
    sb.fat_blocks = 32;     // Number of blocks for FAT
    sb.root_dir_start_block = sb.fat_start_block + sb.fat_blocks;
    sb.root_dir_blocks = 8;
    
    if (write_block(&sb, 0) == -1) {
        printf("Error writing superblock\n");
        close(vs_fd);
        return -1;
    }

    int fat_entries = (2048 / 4) * 32; // Number of entries in the FAT
    int fat[fat_entries];
    memset(fat, 0, sizeof(fat)); // Set all entries to 0 (free)
    // Write FAT blocks to disk
    for (int i = 0; i < 32; ++i) {
        if (write_block(fat + (i * 512), i + 1) == -1) {
            printf("Error writing FAT block %d\n", i);
            close(vs_fd);
            return -1;
        }
    }

    DirectoryEntry root_dir[128]; // Assuming 128 entries, each 128 bytes
    memset(root_dir, 0, sizeof(root_dir)); // Set all entries to 0 (empty)
    
    // Write Root Directory blocks to disk
    for (int i = 0; i < sb.root_dir_blocks; ++i) {
        if (write_block(root_dir + (i * 16), sb.root_dir_start_block + i) == -1) {
            printf("Error writing root directory block %d\n", i);
            close(vs_fd);
            return -1;
        }
    }

    close(vs_fd);
    return (0); 
}


// this function is partially implemented.
int  vsmount (char *vdiskname)
{
    // open the Linux file vdiskname and in this
    // way make it ready to be used for other operations.
    // vs_fd is global; hence other function can use it.

    vs_fd = open("disk1", O_RDWR);

    if (vs_fd == -1) {
        printf("Error opening virtual disk\n");
        return -1;
    }
    // load (cache) the superblock info from disk (Linux file) into memory
    Superblock sb;
    if (read_block(&sb, 0) == -1) {
        printf("Error reading superblock\n");
        close(vs_fd);
        return -1;
    }

    // // superblock = sb;
    
     // load the FAT table from disk into memory
     int fat_entries = (BLOCKSIZE / sizeof(int)) * sb.fat_blocks; // Calculate number of FAT entries
     int *fat = malloc(fat_entries * sizeof(int));
     if (fat == NULL) {
         printf("Error allocating memory for FAT\n");
         close(vs_fd);
         return -1;
     }
     for (int i = 0; i < sb.fat_blocks; ++i) {
         if (read_block(fat + (i * (BLOCKSIZE / sizeof(int))), sb.fat_start_block + i) == -1) {
             printf("Error reading FAT block %d\n", i);
             free(fat);
             close(vs_fd);
             return -1;
         }
     }
     g_fat = malloc(fat_entries * sizeof(int));

     // load root directory from disk into memory
     g_root_dir = malloc(sb.root_dir_blocks * BLOCKSIZE);
     DirectoryEntry *root_dir = malloc(sb.root_dir_blocks * BLOCKSIZE);
     if (root_dir == NULL) {
         printf("Error allocating memory for root directory\n");
         free(fat);
         close(vs_fd);
         return -1;
     }
     for (int i = 0; i < sb.root_dir_blocks; ++i) {
         if (read_block((char *)root_dir + (i * BLOCKSIZE), sb.root_dir_start_block + i) == -1) {
             printf("Error reading root directory block %d\n", i);
             free(root_dir);
             free(fat);
             close(vs_fd);
             return -1;
         }
     }
     return(0);
}


// this function is partially implemented.
 int vsumount ()
 {
     if (vs_fd == -1) {
         printf("Virtual disk not mounted\n");
         return -1;
     }
     // write superblock to virtual disk file
     if (write_block(&superblock, 0) == -1) {
         printf("Error writing superblock\n");
         return -1;
     }
     // write FAT to virtual disk file
     for (int i = 0; i < superblock.fat_blocks; ++i) {
     if (write_block(g_fat + (i * (BLOCKSIZE / sizeof(int))), superblock.fat_start_block + i) == -1) {
             printf("Error writing FAT block %d\n", i);
             return -1;
         }
     }
     // write root directory to virtual disk file
     for (int i = 0; i < superblock.root_dir_blocks; ++i) {
     if (write_block((char *)g_root_dir + (i * BLOCKSIZE), superblock.root_dir_start_block + i) == -1) {
             printf("Error writing root directory block %d\n", i);
             return -1;
         }
     }
     fsync (vs_fd); // synchronize kernel file cache with the disk
     close (vs_fd);
     return (0);
 }


 int vscreate(char *filename)
 {

     if (strlen(filename) >= 30) {
         printf("Error: Filename is too long\n");
         return -1;
     }
     int free_entry = -1;
     for (int i = 0; i < 128; i++) { // Assuming 128 directory entries
         if (g_root_dir[i].is_used == 0) {
             free_entry = i;
             break;
         }
     }

     if (free_entry == -1) {
         printf("Error: No free directory entries\n");
         return -1;
     }


     int free_block = -1;
     for (int i = 0; i < (BLOCKSIZE / 4) * 32; i++) { // Assuming FAT has (BLOCKSIZE / 4) * 32 entries
         if (g_fat[i] == 0) { // 0 indicates a free block
             free_block = i;
             break;
         }
     }

     if (free_block == -1) {
         printf("Error: No free blocks available\n");
         return -1;
     }

     strncpy(g_root_dir[free_entry].filename, filename, 30);
     g_root_dir[free_entry].size = 0; // Initial size is 0
     g_root_dir[free_entry].start_block = free_block;
     g_root_dir[free_entry].is_used = 1; // Mark the entry as used

     // Update the FAT to mark the block as used
     g_fat[free_block] = -1; // -1 or another non-zero value to indicate the block is used

     return (0);
 }


 int vsopen(char *filename, int mode)
 {
     if (mode != MODE_READ && mode != MODE_APPEND) {
         printf("Invalid mode\n");
         return -1;
     }

     int dir_index = -1;
     for (int i = 0; i < 128; i++) { // Assuming 128 directory entries
         if (g_root_dir[i].is_used && strcmp(g_root_dir[i].filename, filename) == 0) {
             dir_index = i;
             break;
         }
     }

     if (dir_index == -1) {
         printf("File not found\n");
         return -1;
     }

     int fd = -1;
     for (int i = 0; i < 16; i++) { // Assuming 16 open file entries
         if (open_file_table[i].used == 0) {
             fd = i;
             break;
         }
     }

     if (fd == -1) {
         printf("No free file descriptors available\n");
         return -1;
     }

     open_file_table[fd].used = 1;
     strncpy(open_file_table[fd].filename, filename, 30);
     open_file_table[fd].mode = mode;
     open_file_table[fd].file_size = g_root_dir[dir_index].size;
     open_file_table[fd].file_pos = (mode == MODE_APPEND) ? g_root_dir[dir_index].size : 0;
     open_file_table[fd].start_block = g_root_dir[dir_index].start_block;

     return fd;
 }

 int vsclose(int fd){
     if (fd < 0 || fd >= 16) { // Assuming 16 open file entries
         printf("Invalid file descriptor\n");
         return -1;
     }

     if (open_file_table[fd].used == 0) {
         printf("File descriptor not in use\n");
         return -1;
     }

     open_file_table[fd].used = 0;
     open_file_table[fd].filename[0] = '\0'; // Optional: clear the filename
     open_file_table[fd].file_size = 0;
     open_file_table[fd].file_pos = 0;
     open_file_table[fd].start_block = -1;

     return (0);
 }

 int vssize (int  fd)
 {
     if (fd < 0 || fd >= 16) { // Assuming 16 open file entries
         printf("Invalid file descriptor\n");
         return -1;
     }

     if (open_file_table[fd].used == 0) {
         printf("File descriptor not in use\n");
         return -1;
     }

     return open_file_table[fd].file_size;;
 }

 int vsread(int fd, void *buf, int n){
     if (fd < 0 || fd >= 16 || open_file_table[fd].used == 0) {
         printf("Invalid file descriptor or file not open\n");
         return -1;
     }

     if (open_file_table[fd].mode != MODE_READ) {
         printf("File not opened in read mode\n");
         return -1;
     }

     OpenFileEntry *file_entry = &open_file_table[fd];

     if (file_entry->file_pos + n > file_entry->file_size) {
         n = file_entry->file_size - file_entry->file_pos; // Adjust n to read only up to the end of the file
     }

     if (n <= 0) {
         return 0; // No data to read
     }



     int current_block = file_entry->start_block;
     int block_offset = file_entry->file_pos % BLOCKSIZE;
     int bytes_read = 0;
     char block[BLOCKSIZE];

     while (bytes_read < n) {
         if (current_block == -1) { // End of file reached in FAT
             break;
         }

         // Read the current block from disk
         if (read_block(block, current_block) == -1) {
             printf("Error reading block\n");
             return -1;
         }

         // Calculate the number of bytes to copy from the current block
         int bytes_to_copy = BLOCKSIZE - block_offset;
         if (bytes_to_copy > n - bytes_read) {
             bytes_to_copy = n - bytes_read;
         }

         // Copy data from block to buffer
         memcpy((char*)buf + bytes_read, block + block_offset, bytes_to_copy);
         bytes_read += bytes_to_copy;

         // Move to the next block
         current_block = g_fat[current_block]; // Navigate through the FAT
         block_offset = 0; // Reset offset for the next block
     }

     file_entry->file_pos += n;
     return n;
 }


 int vsappend(int fd, void *buf, int n)
 {
     // Check if the file descriptor is valid and the file is open
     if (fd < 0 || fd >= 16 || open_file_table[fd].used == 0) {
         printf("Invalid file descriptor or file not open\n");
         return -1;
     }

     // Check if the file is opened in append mode
     if (open_file_table[fd].mode != MODE_APPEND) {
         printf("File not opened in append mode\n");
         return -1;
     }

     OpenFileEntry *file_entry = &open_file_table[fd];
     int current_block = file_entry->start_block;
     int last_block = -1;
     int bytes_appended = 0;
     char block[BLOCKSIZE];

     // Navigate to the last block of the file
     while (current_block != -1) {
         last_block = current_block;
         current_block = g_fat[current_block];
     }

     // If the file is not empty, read the last block and check how much space is left
     int block_offset = (file_entry->file_size % BLOCKSIZE);
     if (last_block != -1 && block_offset != 0) {
         // Read the last block from disk
         if (read_block(block, last_block) == -1) {
             printf("Error reading block\n");
             return -1;
         }
     }

     while (bytes_appended < n) {
         int bytes_to_copy = BLOCKSIZE - block_offset;
         if (bytes_to_copy > n - bytes_appended) {
             bytes_to_copy = n - bytes_appended;
         }

         // Copy data from buffer to block
         memcpy(block + block_offset, (char*)buf + bytes_appended, bytes_to_copy);
         bytes_appended += bytes_to_copy;

         // Write the modified block back to disk
         if (write_block(block, last_block) == -1) {
             printf("Error writing block\n");
             return -1;
         }
         // Check if more data needs to be appended
         if (bytes_appended < n) {
             // Allocate a new block
             int new_block = -1;
             for (int i = 0; i < (BLOCKSIZE / 4) * 32; i++) {
                 if (g_fat[i] == 0) { // 0 indicates a free block
                     new_block = i;
                     break;
                 }
             }

             if (new_block == -1) {
                 printf("No free blocks available\n");
                 return -1;
             }

             // Mark the new block as used in the FAT
             g_fat[new_block] = -1; // -1 or another marker to indicate the end of the file

             // Link the new block in the FAT
             if (last_block != -1) {
                 g_fat[last_block] = new_block;
             } else {
                 file_entry->start_block = new_block; // If it was the first block in the file
             }

             last_block = new_block;
             memset(block, 0, BLOCKSIZE); // Clear the new block
             block_offset = 0; // Reset offset for the next block
         }
     }
     return (0);
 }

 int vsdelete(char *filename)
 {
     int dir_index = -1;
     for (int i = 0; i < 128; i++) { // Assuming 128 directory entries
         if (g_root_dir[i].is_used && strcmp(g_root_dir[i].filename, filename) == 0) {
             dir_index = i;
             break;
         }
     }

     if (dir_index == -1) {
         printf("File not found\n");
         return -1;
     }

     // Free the blocks in the FAT
     int current_block = g_root_dir[dir_index].start_block;
     while (current_block != -1) {
         int next_block = g_fat[current_block];
         g_fat[current_block] = 0; // Mark the block as free
         current_block = next_block;
     }

     // Mark the directory entry as unused
     g_root_dir[dir_index].is_used = 0;
     g_root_dir[dir_index].filename[0] = '\0'; // Optionally clear the filename
     g_root_dir[dir_index].size = 0;
     g_root_dir[dir_index].start_block = -1;

     return 0;
 }

