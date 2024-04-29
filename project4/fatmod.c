#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <fcntl.h>
#include <assert.h>
#include <ctype.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <time.h>
#include <errno.h>
#include <linux/msdos_fs.h>

#define FALSE 0
#define TRUE 1

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE 1024 // bytes
#define ROOTDIR_START_CLUSTER 2 // Typical start cluster of root directory

// Define the structure of a directory entry as seen in FAT32
struct dir_entry {
    unsigned char name[11];    // File name + extension
    unsigned char attr;        // File attributes
    unsigned char ntres;       // Reserved for use by Windows NT
    unsigned char crtTimeTenth; // Millisecond stamp at file creation time
    unsigned short crtTime;    // Time file was created
    unsigned short crtDate;    // Date file was created
    unsigned short lstAccDate; // Last access date
    unsigned short fstClusHI;  // High word of this entry's first cluster number
    unsigned short wrtTime;    // Time of last write
    unsigned short wrtDate;    // Date of last write
    unsigned short fstClusLO;  // Low word of this entry's first cluster number
    unsigned int fileSize;     // File size in bytes
};

int readsector(int fd, unsigned char *buf, unsigned int snum);
int writesector(int fd, unsigned char *buf, unsigned int snum);
void ListFiles(int fd);
unsigned int getNextCluster(int fd, unsigned int currentCluster);
void DisplayFileASCII(int fd, const char *filename);
void DisplayFileBinary(int fd, const char *filename);
void CreateFile(int fd, const char *filename);
void DeleteFile(int fd, const char *filename);
void freeCluster(int fd, unsigned int cluster);
void WriteDataToFile(int fd, const char *filename, unsigned int offset, unsigned int n, unsigned char data);
void clearCluster(int fd, unsigned int cluster);
unsigned int allocateNewCluster(int fd);

int main(int argc, char *argv[]) {
    if (argc < 2) {
        printf("Usage: %s <disk_image>\n", argv[0]);
        return 1;
    }

    char diskname[128];
    int fd;
    strcpy(diskname, argv[1]);

    fd = open(diskname, O_SYNC | O_RDWR);
    if (fd < 0) {
        perror("Could not open disk image");
        return 1;
    }

    ListFiles(fd);

    close(fd);
    return 0;
}


void ListFiles(int fd) {
    unsigned char buffer[SECTORSIZE];
    unsigned char fatTable[SECTORSIZE]; // Simplified: assuming one sector of FAT is enough for demonstration
    struct dir_entry *entry;
    int result, i;
    unsigned int currentCluster = ROOTDIR_START_CLUSTER;
    unsigned int nextCluster;

    // Simplified reading of FAT sector (here assuming FAT starts at sector right after reserved sectors)
    readsector(fd, fatTable, 1); // Reading only the first sector of the FAT

    do {
        int rootDirSector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS; // Calculate sector from cluster
        result = readsector(fd, buffer, rootDirSector);
        if (result != 0) {
            printf("Error reading root directory sector\n");
            return;
        }

        // Loop through entries in the root directory sector
        for (i = 0; i < SECTORSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));

            // Check if the filename's first byte is 0x00 (unused entry) or 0xE5 (deleted entry)
            if (entry->name[0] == 0x00) {
                return; // No more entries
            }
            if (entry->name[0] == 0xE5) {
                continue; // Skip deleted entries
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {  // Skip volume label
                printf("%11.11s Size: %u bytes\n", entry->name, entry->fileSize);
            }
        }

        // Get the next cluster in the chain
        nextCluster = getNextCluster(fd, currentCluster, fatTable);
        currentCluster = nextCluster;
    } while (nextCluster < 0x0FFFFFF8); // Continue if not end of cluster chain
}

unsigned int getNextCluster(int fd, unsigned int currentCluster) {
    unsigned char fatSector[SECTORSIZE];
    unsigned int fatOffset = currentCluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int sectorOfFAT = RESERVED_SECTORS + (fatOffset / SECTORSIZE); // FAT starts after reserved sectors
    unsigned int offsetInSector = fatOffset % SECTORSIZE;
    unsigned int nextCluster;

    // Read the sector containing the current cluster's FAT entry
    readsector(fd, fatSector, sectorOfFAT);

    // Extract the next cluster number from the FAT entry
    memcpy(&nextCluster, &fatSector[offsetInSector], sizeof(unsigned int));
    nextCluster &= 0x0FFFFFFF; // FAT32 uses only 28 bits

    return nextCluster;
}


void DisplayFileASCII(int fd, const char *filename) {
    unsigned char buffer[CLUSTERSIZE];  // Increase buffer size to cluster size
    struct dir_entry *entry;
    int i, found = FALSE;
    unsigned int currentCluster = ROOTDIR_START_CLUSTER;

    // Read the root directory cluster by cluster
    do {
        int sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);  // Assumes the first sector of the cluster holds the directory info

        // Search through directory entries in this sector
        for (i = 0; i < CLUSTERSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) break; // End of directory entries
            if (entry->name[0] == 0xE5) continue; // Skip deleted entries

            // Check if the entry matches the filename and is not a directory or volume label
            if (strncmp(entry->name, filename, 11) == 0 && !(entry->attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY))) {
                found = TRUE;
                break;
            }
        }

        if (found) break;

        // Get the next cluster for the directory from the FAT
        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);

    if (!found) {
        printf("File not found.\n");
        return;
    }

    // Now read the file's data
    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);

    do {
        int sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);  // Read the first sector of the cluster

        // Display the contents in ASCII
        for (i = 0; i < CLUSTERSIZE; i++) {
            if (isprint(buffer[i]))
                putchar(buffer[i]);
            else
                putchar('.');  // Non-printable characters are shown as '.'
        }

        // Get the next cluster for the file from the FAT
        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);
}

void DisplayFileBinary(int fd, const char *filename) {
    unsigned char buffer[CLUSTERSIZE];
    struct dir_entry *entry;
    int i, found = FALSE;
    unsigned int currentCluster = ROOTDIR_START_CLUSTER;

    // Read the root directory cluster by cluster
    do {
        int sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);  // Assumes the first sector of the cluster holds the directory info

        // Search through directory entries in this sector
        for (i = 0; i < CLUSTERSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) break; // End of directory entries
            if (entry->name[0] == 0xE5) continue; // Skip deleted entries

            // Check if the entry matches the filename and is not a directory or volume label
            if (strncmp(entry->name, filename, 11) == 0 && !(entry->attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY))) {
                found = TRUE;
                break;
            }
        }

        if (found) break;

        // Get the next cluster for the directory from the FAT
        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);

    if (!found) {
        printf("File not found.\n");
        return;
    }

    // Now read the file's data
    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);

    do {
        int sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);  // Read the first sector of the cluster

        // Display the contents in hexadecimal
        for (i = 0; i < CLUSTERSIZE; i++) {
            printf("%02x ", buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n"); // New line every 16 bytes for better readability
        }

        // Get the next cluster for the file from the FAT
        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);
}

void CreateFile(int fd, const char *filename) {
    unsigned char buffer[CLUSTERSIZE];
    struct dir_entry *entry;
    int i, sector, foundFree = FALSE;
    unsigned int currentCluster = ROOTDIR_START_CLUSTER;

    // First, ensure filename length fits into the 8.3 format
    char name[12]; // Extra space for null-terminator
    snprintf(name, sizeof(name), "%-11.11s", filename); // Format to 11 characters, space-padded

    // Read and search the root directory for an empty or usable entry
    do {
        sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);

        for (i = 0; i < CLUSTERSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));

            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {  // Found an empty or deleted entry
                foundFree = TRUE;
                memset(entry, 0, sizeof(struct dir_entry));  // Clear the entry
                memcpy(entry->name, name, 11);  // Set the file name
                entry->fileSize = 0;
                entry->fstClusHI = 0;
                entry->fstClusLO = 0;
                entry->attr = ATTR_ARCHIVE;  // Mark as a normal file

                // Write back the updated directory entry to the disk
                writesector(fd, buffer, sector);
                printf("File created: %s\n", filename);
                return;
            }
        }

        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);

    if (!foundFree) {
        printf("No free directory entries available.\n");
    }
}

void DeleteFile(int fd, const char *filename) {
    unsigned char buffer[CLUSTERSIZE];
    struct dir_entry *entry;
    int i, sector;
    unsigned int currentCluster = ROOTDIR_START_CLUSTER;

    char name[12];
    snprintf(name, sizeof(name), "%-11.11s", filename);

    // Read and search the root directory for the file entry
    do {
        sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);

        for (i = 0; i < CLUSTERSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));

            if (strncmp(entry->name, name, 11) == 0) {  // Found the file
                entry->name[0] = 0xE5;  // Mark the entry as deleted

                // Free the clusters used by the file
                unsigned int cluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
                while (cluster < 0x0FFFFFF8) {
                    unsigned int nextCluster = getNextCluster(fd, cluster);
                    freeCluster(fd, cluster);  // Function to mark the cluster as free in the FAT
                    cluster = nextCluster;
                }

                // Write back the updated directory entry to the disk
                writesector(fd, buffer, sector);
                printf("File deleted: %s\n", filename);
                return;
            }
        }

        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);

    printf("File not found: %s\n", filename);
}

void freeCluster(int fd, unsigned int cluster) {
    unsigned char fatSector[SECTORSIZE];
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int sectorOfFAT = RESERVED_SECTORS + (fatOffset / SECTORSIZE);
    unsigned int offsetInSector = fatOffset % SECTORSIZE;
    unsigned int zero = 0;

    // Read the sector containing the FAT entry for this cluster
    readsector(fd, fatSector, sectorOfFAT);

    // Mark the cluster as free
    memcpy(&fatSector[offsetInSector], &zero, sizeof(unsigned int));

    // Write the updated FAT sector back to disk
    writesector(fd, fatSector, sectorOfFAT);
}

void WriteDataToFile(int fd, const char *filename, unsigned int offset, unsigned int n, unsigned char data) {
    struct dir_entry *entry;
    unsigned char buffer[CLUSTERSIZE];
    int i, sector, found = FALSE;
    unsigned int currentCluster, clusterOffset, sectorOffset, bytesToWrite;

    // First, find the file's directory entry
    currentCluster = ROOTDIR_START_CLUSTER;
    do {
        sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        readsector(fd, buffer, sector);

        // Search through directory entries in this sector
        for (i = 0; i < CLUSTERSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));
            if (strncmp(entry->name, filename, 11) == 0) {  // Found the file
                found = TRUE;
                break;
            }
        }

        if (found) break;
        currentCluster = getNextCluster(fd, currentCluster);
    } while (currentCluster < 0x0FFFFFF8);

    if (!found) {
        printf("File not found.\n");
        return;
    }

    // Calculate the starting cluster
    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);

    // Find the cluster and sector where the offset lands
    while (offset >= CLUSTERSIZE) {
        offset -= CLUSTERSIZE;
        currentCluster = getNextCluster(fd, currentCluster);
        if (currentCluster >= 0x0FFFFFF8) { // Needs a new cluster
            currentCluster = allocateNewCluster(fd); // Function to allocate a new cluster
            if (currentCluster == 0xFFFFFFFF) {
                printf("Failed to allocate new cluster.\n");
                return;
            }
        }
    }

    // Calculate the exact sector and offset in that sector
    sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
    sectorOffset = offset % SECTORSIZE;
    clusterOffset = (offset / SECTORSIZE) * SECTORSIZE;

    // Read, modify, and write back the sector
    readsector(fd, buffer, sector + clusterOffset / SECTORSIZE);
    while (n > 0) {
        bytesToWrite = SECTORSIZE - sectorOffset;
        if (bytesToWrite > n) bytesToWrite = n;

        memset(buffer + sectorOffset, data, bytesToWrite);
        writesector(fd, buffer, sector + clusterOffset / SECTORSIZE);

        n -= bytesToWrite;
        offset += bytesToWrite;
        sectorOffset = 0;  // Reset for new sector

        // Check if we need to move to the next sector/cluster
        if (offset >= CLUSTERSIZE) {
            offset -= CLUSTERSIZE;
            currentCluster = getNextCluster(fd, currentCluster);
            if (currentCluster >= 0x0FFFFFF8) { // Needs a new cluster
                currentCluster = allocateNewCluster(fd);
                if (currentCluster == 0xFFFFFFFF) {
                    printf("Failed to allocate new cluster.\n");
                    return;
                }
            }
            sector = (currentCluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;
        }
    }

    // Update the file size in the directory entry if the write goes beyond the current file size
    if (entry->fileSize < offset) {
        entry->fileSize = offset;
        writesector(fd, buffer, sector);  // Write the updated directory entry back
    }
}

unsigned int allocateNewCluster(int fd) {
    unsigned char fatSector[SECTORSIZE];
    unsigned int cluster;
    unsigned int totalClusters = NUM_CLUSTERS; // This should be defined based on your disk's parameters
    unsigned int fatEntry, sectorOfFAT, offsetInSector;

    // Iterate over each cluster entry in the FAT to find a free one
    for (cluster = 2; cluster < totalClusters; cluster++) {
        fatEntry = cluster * 4; // Each FAT32 entry is 4 bytes
        sectorOfFAT = RESERVED_SECTORS + (fatEntry / SECTORSIZE);
        offsetInSector = fatEntry % SECTORSIZE;

        // Read the FAT sector containing this cluster's entry
        readsector(fd, fatSector, sectorOfFAT);

        // Check if this cluster is free
        memcpy(&fatEntry, &fatSector[offsetInSector], sizeof(unsigned int));
        if (fatEntry == 0) { // Found a free cluster
            fatEntry = 0x0FFFFFFF; // Mark it as end of chain in the FAT
            memcpy(&fatSector[offsetInSector], &fatEntry, sizeof(unsigned int));
            writesector(fd, fatSector, sectorOfFAT); // Write back the updated FAT sector

            // Clear the new cluster to avoid leftover data causing issues
            clearCluster(fd, cluster);

            return cluster; // Return the newly allocated cluster number
        }
    }

    return 0xFFFFFFFF; // No free cluster found, return an error
}

void clearCluster(int fd, unsigned int cluster) {
    unsigned char zeroBuffer[CLUSTERSIZE];
    memset(zeroBuffer, 0, CLUSTERSIZE); // Set all bytes to zero

    unsigned int sector = (cluster - 2) * (CLUSTERSIZE / SECTORSIZE) + RESERVED_SECTORS;

    // Assuming each cluster is 2 sectors for simplicity
    writesector(fd, zeroBuffer, sector);
    writesector(fd, zeroBuffer, sector + 1);
}

int readsector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset = (off_t)snum * SECTORSIZE;  // Calculate the byte offset from the sector number
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("Error seeking to sector");
        return -1;  // Return -1 on seeking error
    }

    ssize_t bytesRead = read(fd, buf, SECTORSIZE);
    if (bytesRead == SECTORSIZE) {
        return 0;  // Successfully read the sector
    } else {
        perror("Error reading sector");
        return -1;  // Return -1 on read error
    }
}

int writesector(int fd, unsigned char *buf, unsigned int snum) {
    off_t offset = (off_t)snum * SECTORSIZE;  // Calculate the byte offset from the sector number
    if (lseek(fd, offset, SEEK_SET) == (off_t)-1) {
        perror("Error seeking to sector");
        return -1;  // Return -1 on seeking error
    }

    ssize_t bytesWritten = write(fd, buf, SECTORSIZE);
    if (bytesWritten == SECTORSIZE) {
        fsync(fd);  // Force the changes to disk immediately
        return 0;  // Successfully wrote the sector
    } else {
        perror("Error writing sector");
        return -1;  // Return -1 on write error
    }
}
