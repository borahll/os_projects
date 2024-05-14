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
#include <stdint.h>
#include <errno.h>
#include <linux/msdos_fs.h>

#define FALSE 0
#define TRUE 1

#define SECTORSIZE 512   // bytes
#define CLUSTERSIZE 1024 // bytes
#define ROOTDIR_START_CLUSTER 2 // Typical start cluster of root directory
// Specific FAT32 Constants
#define RESERVED_SECTORS 32    // Number of reserved sectors
#define ATTR_ARCHIVE 0x20      // File modified/archive attribute
#define ATTR_DIRECTORY 0x10    // Directory attribute
#define ATTR_VOLUME_ID 0x08    // Volume ID attribute

// Define the structure of a directory entry as seen in FAT32
struct __attribute__((packed)) dir_entry {
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

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;  // Cluster number of root directory start, typically 2
    uint32_t total_clusters; // Total number of clusters, computed dynamically
} BootSector;
BootSector *bs;

int readsector (int fd, unsigned char *buf, uint snum);
int writesector (int fd, unsigned char *buf, uint snum);
void ListFiles(int fd);
unsigned int getNextCluster(int fd, unsigned int currentCluster, unsigned char *fatSector);
void DisplayFileASCII(int fd, const char *filename);
void DisplayFileBinary(int fd, const char *filename);
void CreateFile(int fd, const char *filename);
void DeleteFile(int fd, const char *filename);
void freeCluster(int fd, unsigned int cluster);
void WriteDataToFile(int fd, const char *filename, unsigned int offset, unsigned int n, unsigned char data);
void clearCluster(int fd, unsigned int cluster);
unsigned int allocateNewCluster(int fd);
int read_boot_sector(int fd);

int main(int argc, char *argv[]) {
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <disk_image> <command> [options]\n", argv[0]);
        return 1;
    }

    const char* diskImage = argv[1];

    const char* command = argv[2];
        bs = malloc(sizeof(BootSector));
    if (bs == NULL) {
        fprintf(stderr, "Failed to allocate memory for BootSector\n");
        return 1;  // Exit if memory allocation fails
    }

    int fd = open(diskImage, O_RDWR | O_SYNC);

    if (fd == -1) {
        perror("Failed to open disk image");
        return 1;
    }
    if (read_boot_sector(fd) != 0) {
        close(fd);
        return 1;  // Handle error appropriately
    }
    if (strcmp(command, "-l") == 0) {
        ListFiles(fd);
    } else if (strcmp(command, "-ra") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s <disk_image> -ra <filename>\n", argv[0]);
            close(fd);
            return 1;
        }
        DisplayFileASCII(fd, argv[3]);
    } else if (strcmp(command, "-rb") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s <disk_image> -rb <filename>\n", argv[0]);
            close(fd);
            return 1;
        }
        DisplayFileBinary(fd, argv[3]);
    } else if (strcmp(command, "-c") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s <disk_image> -c <filename>\n", argv[0]);
            close(fd);
            return 1;
        }
        CreateFile(fd, argv[3]);
    } else if (strcmp(command, "-d") == 0) {
        if (argc < 4) {
            fprintf(stderr, "Usage: %s <disk_image> -d <filename>\n", argv[0]);
            close(fd);
            return 1;
        }
        DeleteFile(fd, argv[3]);
    } else if (strcmp(command, "-w") == 0) {
        if (argc < 7) {
            fprintf(stderr, "Usage: %s <disk_image> -w <filename> <offset> <length> <byte>\n", argv[0]);
            close(fd);
            return 1;
        }
        const char* filename = argv[3];
        unsigned int offset = (unsigned int)strtoul(argv[4], NULL, 10);
        unsigned int length = (unsigned int)strtoul(argv[5], NULL, 10);
        unsigned char byte = (unsigned char)strtoul(argv[6], NULL, 10);
        WriteDataToFile(fd, filename, offset, length, byte);
    } else {
        fprintf(stderr, "Unknown command\n");
        close(fd);
        return 1;
    }

    close(fd);
    return 0;
}


void ListFiles(int fd) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];
    struct dir_entry *entry;
    int i, j, k;
    unsigned int currentCluster = bs->root_cluster;
    unsigned int nextCluster;

    // Read the FAT table
    for (i = 0; i < bs->sectors_per_fat; i++) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }

    do {
        int sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;

        // Read all sectors within the current cluster
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }

        // Process each directory entry in the cluster
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                return; // No more entries
            }
            if (entry->name[0] == 0xE5) {
                continue; // Skip deleted entries
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
                // Format the name and extension
                char name[9];
                char ext[4];
                memset(name, 0, sizeof(name));
                memset(ext, 0, sizeof(ext));

                for (k = 0; k < 8 && entry->name[k] != ' '; k++) {
                    name[k] = entry->name[k];
                }
                for (k = 0; k < 3 && entry->name[8 + k] != ' '; k++) {
                    ext[k] = entry->name[8 + k];
                }

                // Print the formatted name and extension
                if (strlen(ext) > 0) {
                    printf("%s%s Size: %u bytes\n", name, ext, entry->fileSize);
                } else {
                    printf("%s Size: %u bytes\n", name, entry->fileSize);
                }
            }
        }

        nextCluster = getNextCluster(fd, currentCluster, fatTable);
        currentCluster = nextCluster;
    } while (nextCluster < 0x0FFFFFF8);
}


unsigned int getNextCluster(int fd, unsigned int currentCluster, unsigned char *fatSector) {
    unsigned int fatOffset = currentCluster * 4;
    unsigned int sectorOfFAT = bs->reserved_sectors + (fatOffset / SECTORSIZE);
    unsigned int offsetInSector = fatOffset % SECTORSIZE;
    unsigned int nextCluster;

    // Read the sector containing the current cluster's FAT entry
    readsector(fd, fatSector, sectorOfFAT);
    memcpy(&nextCluster, &fatSector[offsetInSector], sizeof(unsigned int));
    nextCluster &= 0x0FFFFFFF;

    return nextCluster;
}



void DisplayFileASCII(int fd, const char *filename) {

unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];  // Buffer to hold portions of the FAT
    struct dir_entry *entry;
    int i, j, k, sector;
    unsigned int currentCluster = bs->root_cluster;
    unsigned int nextCluster;


    int found = FALSE;
    

    // Pre-load the initial sectors of the FAT into fatTable for faster access
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }

    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;

        // Read all sectors within the current cluster
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }

        // Process each directory entry in the cluster
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                printf("End of directory entries.\n");
                return; // No more entries
            }
            if (entry->name[0] == 0xE5) {
                continue; // Skip deleted entries
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
                // Format the name and extension
                char name[9];
                char ext[4];
                memset(name, 0, sizeof(name));
                memset(ext, 0, sizeof(ext));

                for (k = 0; k < 8 && entry->name[k] != ' '; k++) {
                    name[k] = entry->name[k];
                }
                for (k = 0; k < 3 && entry->name[8 + k] != ' '; k++) {
                    ext[k] = entry->name[8 + k];
                }

                char fullname[13];
                snprintf(fullname, sizeof(fullname), "%s%s", name, ext);
                // Trim any trailing spaces from fullname
                for (k = strlen(fullname) - 1; k >= 0 && fullname[k] == ' '; k--) {
                    fullname[k] = '\0';
                }

                // Print the formatted name and extension for debugging
                printf("Checking file: %s\n", fullname);

                // Compare formatted name and extension with filename
                if (strcmp(fullname, filename) == 0) {

                found = TRUE;
                break;
                }
            }
        }
        
        if (found) break;
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8);
    
    if (!found) {
        printf("File not found.\n");
        return;
    }


    // Now read the file's data
    
    // Now read the file's data
    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
    
    do {
        int sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        
        for (i = 0; i < CLUSTERSIZE; i++) {
            if (isprint(buffer[i]))
                putchar(buffer[i]);
            else
                putchar('.');
        }


        // Get the next cluster for the file from the FAT using the fatTable
        
        // Get the next cluster for the file from the FAT using the fatTable
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8);
}
void DisplayFileBinary(int fd, const char *filename) {
    unsigned char buffer[CLUSTERSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];
    struct dir_entry *entry;
    int i, found = FALSE;
    unsigned int currentCluster = bs->root_cluster;

    // Pre-load FAT table
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }

    do {
        int sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        readsector(fd, buffer, sector);

        for (i = 0; i < CLUSTERSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) break;
            if (entry->name[0] == 0xE5) continue;
            if (strncmp((const char *)entry->name, filename, 11) == 0 && !(entry->attr & (ATTR_VOLUME_ID | ATTR_DIRECTORY))) {
                found = TRUE;
                break;
            }
        }

        if (found) break;
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8);

    if (!found) {
        printf("File not found.\n");
        return;
    }

    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);

    do {
        int sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }

        for (i = 0; i < CLUSTERSIZE; i++) {
            printf("%02x ", buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n");
        }

        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8);
}

void CreateFile(int fd, const char *filename) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    struct dir_entry *entry;
    int i, sector, foundFree = FALSE;
    unsigned int currentCluster = bs->root_cluster;

    char name[12]; // Extra space for null-terminator
    snprintf(name, sizeof(name), "%-11.11s", filename); // Format to 11 characters, space-padded

    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }

        for (i = 0; i < bs->sectors_per_cluster * SECTORSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00 || entry->name[0] == 0xE5) {
                foundFree = TRUE;
                memset(entry, 0, sizeof(struct dir_entry));
                snprintf((char*)entry->name, 12, "%-11.11s", filename); // Ensures only the first 8 characters are considered
                entry->fileSize = 0;
                entry->fstClusHI = 0;
                entry->fstClusLO = 0;
                entry->attr = ATTR_ARCHIVE;

                for (i = 0; i < bs->sectors_per_cluster; i++) {
                    writesector(fd, buffer + (i * SECTORSIZE), sector + i);
                }
                printf("File created: %s\n", filename);
                return;
            }
        }

        currentCluster = getNextCluster(fd, currentCluster, buffer);
    } while (currentCluster < 0x0FFFFFF8);

    if (!foundFree) {
        printf("No free directory entries available.\n");
    }
}



void DeleteFile(int fd, const char *filename) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];  // Buffer to hold portions of the FAT
    struct dir_entry *entry;
    int i, j, k, sector;
    unsigned int currentCluster = bs->root_cluster;
    unsigned int nextCluster;

    // Pre-load the initial sectors of the FAT into fatTable for faster access
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }

    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;

        // Read all sectors within the current cluster
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }

        // Process each directory entry in the cluster
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                printf("End of directory entries.\n");
                return; // No more entries
            }
            if (entry->name[0] == 0xE5) {
                continue; // Skip deleted entries
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
                // Format the name and extension
                char name[9];
                char ext[4];
                memset(name, 0, sizeof(name));
                memset(ext, 0, sizeof(ext));

                for (k = 0; k < 8 && entry->name[k] != ' '; k++) {
                    name[k] = entry->name[k];
                }
                for (k = 0; k < 3 && entry->name[8 + k] != ' '; k++) {
                    ext[k] = entry->name[8 + k];
                }

                char fullname[13];
                snprintf(fullname, sizeof(fullname), "%s%s", name, ext);
                // Trim any trailing spaces from fullname
                for (k = strlen(fullname) - 1; k >= 0 && fullname[k] == ' '; k--) {
                    fullname[k] = '\0';
                }

                // Print the formatted name and extension for debugging
                printf("Checking file: %s\n", fullname);

                // Compare formatted name and extension with filename
                if (strcmp(fullname, filename) == 0) {
                    entry->name[0] = 0xE5;  // Mark the entry as deleted

                    unsigned int cluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
                    while (cluster < 0x0FFFFFF8) {
                        unsigned int nextCluster = getNextCluster(fd, cluster, fatTable);
                        freeCluster(fd, cluster);
                        cluster = nextCluster;
                    }

                    // Write the updated directory entry back to disk
                    for (i = 0; i < bs->sectors_per_cluster; i++) {
                        writesector(fd, buffer + (i * SECTORSIZE), sector + i);
                    }
                    printf("File deleted: %s\n", filename);
                    return;
                }
            }
        }

        nextCluster = getNextCluster(fd, currentCluster, fatTable);
        currentCluster = nextCluster;
    } while (nextCluster < 0x0FFFFFF8);

    printf("File not found: %s\n", filename);
}




void freeCluster(int fd, unsigned int cluster) {
    unsigned char fatSector[SECTORSIZE];
    unsigned int fatOffset = cluster * 4; // Each FAT32 entry is 4 bytes
    unsigned int sectorOfFAT = bs->reserved_sectors + (fatOffset / SECTORSIZE);
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
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];  // Adjusted to actual FAT size
    struct dir_entry *entry;
    int i, sector, found = FALSE;
    unsigned int currentCluster, sectorOffset, bytesToWrite;

    // Load the entire FAT into memory
    for (i = 0; i < bs->sectors_per_fat; i++) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }

    currentCluster = bs->root_cluster;
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }

        for (i = 0; i < bs->sectors_per_cluster * SECTORSIZE / sizeof(struct dir_entry); i++) {
            entry = (struct dir_entry *)(buffer + i * sizeof(struct dir_entry));
            if (strncmp((const char*)entry->name, filename, 11) == 0) {
                found = TRUE;
                break;
            }
        }

        if (found) break;
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8);

    if (!found) {
        printf("File not found.\n");
        return;
    }

    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
    while (offset >= bs->sectors_per_cluster * SECTORSIZE) {
        offset -= bs->sectors_per_cluster * SECTORSIZE;
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
        if (currentCluster >= 0x0FFFFFF8) {
            currentCluster = allocateNewCluster(fd);
            if (currentCluster == 0xFFFFFFFF) {
                printf("Failed to allocate new cluster.\n");
                return;
            }
        }
    }

    sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
    sectorOffset = offset % SECTORSIZE;
    readsector(fd, buffer, sector);
    bytesToWrite = SECTORSIZE - sectorOffset;
    bytesToWrite = (bytesToWrite > n) ? n : bytesToWrite;

    memset(buffer + sectorOffset, data, bytesToWrite);
    writesector(fd, buffer, sector);

    // Update file size if needed
    if (entry->fileSize < offset + n) {
        entry->fileSize = offset + n;
        writesector(fd, buffer, sector); // Assuming sector where directory entry is stored is properly calculated
    }
}




unsigned int allocateNewCluster(int fd) {
    unsigned char fatSector[SECTORSIZE];
    unsigned int cluster;
    unsigned int fatEntry, sectorOfFAT, offsetInSector;

    for (cluster = 2; cluster < bs->total_clusters + 2; cluster++) {
        fatEntry = cluster * 4; // Each FAT32 entry is 4 bytes
        sectorOfFAT = bs->reserved_sectors + (fatEntry / SECTORSIZE);
        offsetInSector = fatEntry % SECTORSIZE;

        readsector(fd, fatSector, sectorOfFAT);

        memcpy(&fatEntry, &fatSector[offsetInSector], sizeof(unsigned int));
        if (fatEntry == 0) { // Found a free cluster
            fatEntry = 0x0FFFFFFF;
            memcpy(&fatSector[offsetInSector], &fatEntry, sizeof(unsigned int));
            writesector(fd, fatSector, sectorOfFAT);
            clearCluster(fd, cluster);
            return cluster;
        }
    }
    return 0xFFFFFFFF;
}


void clearCluster(int fd, unsigned int cluster) {
    unsigned char zeroBuffer[CLUSTERSIZE];
    memset(zeroBuffer, 0, CLUSTERSIZE); // Set all bytes to zero

    unsigned int sector = (cluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;

    // Assuming each cluster is 2 sectors for simplicity
    for (unsigned int i = 0; i < bs->sectors_per_cluster; i++) {
        writesector(fd, zeroBuffer, sector + i);
    }
}

int readsector(int fd, unsigned char *buf, uint snum) {
    off_t offset = snum * SECTORSIZE;
    if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
        perror("lseek");
        return -1;
    }
    if (read(fd, buf, SECTORSIZE) != SECTORSIZE) {
        perror("read");
        return -1;
    }
    return 0;
}
int writesector(int fd, unsigned char *buf, uint snum) {
    off_t offset = snum * SECTORSIZE;
    if (lseek(fd, offset, SEEK_SET) == (off_t) -1) {
        perror("lseek");
        return -1;
    }
    if (write(fd, buf, SECTORSIZE) != SECTORSIZE) {
        perror("write");
        return -1;
    }
    if (fsync(fd) == -1) {
        perror("fsync");
        return -1;
    }
    return 0;
}
int read_boot_sector(int fd) {
    
    uint8_t buffer[SECTORSIZE];
    if (pread(fd, buffer, SECTORSIZE, 0) != SECTORSIZE) {
        perror("Failed to read boot sector");
        return -1;
    }


    // Assuming little-endian as per FAT32 specification
    bs->bytes_per_sector = *((uint16_t *)(buffer + 11));

    bs->sectors_per_cluster = *(buffer + 13);

    bs->reserved_sectors = *((uint16_t *)(buffer + 14));

    bs->number_of_fats = *(buffer + 16);

    bs->total_sectors = *((uint32_t *)(buffer + 32));
    bs->sectors_per_fat = *((uint32_t *)(buffer + 36));
    bs->root_cluster = *((uint32_t *)(buffer + 44));
    bs->total_clusters = (bs->total_sectors - bs->reserved_sectors - (bs->sectors_per_fat * bs->number_of_fats)) / bs->sectors_per_cluster;

    return 0;
}
