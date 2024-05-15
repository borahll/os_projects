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
#define SECTORSIZE 512
#define CLUSTERSIZE 1024
#define ROOTDIR_START_CLUSTER 2
#define RESERVED_SECTORS 32
#define ATTR_ARCHIVE 0x20
#define ATTR_DIRECTORY 0x10
#define ATTR_VOLUME_ID 0x08
struct __attribute__((packed)) dir_entry {
    unsigned char name[11];
    unsigned char attr;
    unsigned char ntres;
    unsigned char crtTimeTenth;
    unsigned short crtTime;
    unsigned short crtDate;
    unsigned short lstAccDate;
    unsigned short fstClusHI;
    unsigned short wrtTime;
    unsigned short wrtDate;
    unsigned short fstClusLO;
    unsigned int fileSize;
};

typedef struct {
    uint16_t bytes_per_sector;
    uint8_t sectors_per_cluster;
    uint16_t reserved_sectors;
    uint8_t number_of_fats;
    uint32_t total_sectors;
    uint32_t sectors_per_fat;
    uint32_t root_cluster;
    uint32_t total_clusters;
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
        return 1;
    }
    int fd = open(diskImage, O_RDWR | O_SYNC);
    if (fd == -1) {
        perror("Failed to open disk image");
        return 1;
    }
    if (read_boot_sector(fd) != 0) {
        close(fd);
        return 1;
    }
    if (strcmp(command, "-l") == 0) {
        ListFiles(fd);
    } else if (strcmp(command, "-r") == 0) {
        if (argc < 5) {
            fprintf(stderr, "Usage: %s <disk_image> -r -a/-b <filename>\n", argv[0]);
            close(fd);
            free(bs);
            return 1;
        }
        if (strcmp(argv[3], "-a") == 0) {
            DisplayFileASCII(fd, argv[4]);
        } else if (strcmp(argv[3], "-b") == 0) {
            DisplayFileBinary(fd, argv[4]);
        } else {
            fprintf(stderr, "Invalid option for -r command\n");
            close(fd);
            free(bs);
            return 1;
        }
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
    for (i = 0; i < bs->sectors_per_fat; i++) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }
    do {
        int sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                return;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
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
    readsector(fd, fatSector, sectorOfFAT);
    memcpy(&nextCluster, &fatSector[offsetInSector], sizeof(unsigned int));
    nextCluster &= 0x0FFFFFFF;
    return nextCluster;
}
void DisplayFileASCII(int fd, const char *filename) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];
    struct dir_entry *entry;
    int i, j, k, sector;
    unsigned int currentCluster = bs->root_cluster;
    int found = FALSE;
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                printf("End of directory entries.\n");
                return;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
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
                for (k = strlen(fullname) - 1; k >= 0 && fullname[k] == ' '; k--) {
                    fullname[k] = '\0';
                }
                printf("Checking file: %s\n", fullname);
                if (strcmp(fullname, filename) == 0) {
                    found = TRUE;
                    printf("Found file: %s\n", fullname);
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
    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
    unsigned int fileSize = entry->fileSize;
    unsigned int bytesRead = 0;
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (i = 0; i < bs->sectors_per_cluster * SECTORSIZE && bytesRead < fileSize; i++) {
            if (isprint(buffer[i]))
                putchar(buffer[i]);
            else
                putchar('.');
            bytesRead++;
        }
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8 && bytesRead < fileSize);
}
void DisplayFileBinary(int fd, const char *filename) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];
    struct dir_entry *entry;
    int i, j, k, sector;
    unsigned int currentCluster = bs->root_cluster;
    int found = FALSE;
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                printf("End of directory entries.\n");
                return;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
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
                for (k = strlen(fullname) - 1; k >= 0 && fullname[k] == ' '; k--) {
                    fullname[k] = '\0';
                }
                printf("Checking file: %s\n", fullname);
                if (strcmp(fullname, filename) == 0) {
                    found = TRUE;
                    printf("Found file: %s\n", fullname);
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
    currentCluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
    unsigned int fileSize = entry->fileSize;
    unsigned int bytesRead = 0;
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (i = 0; i < bs->sectors_per_cluster * SECTORSIZE && bytesRead < fileSize; i++) {
            printf("%02x ", buffer[i]);
            if ((i + 1) % 16 == 0) printf("\n");
            bytesRead++;
        }
        currentCluster = getNextCluster(fd, currentCluster, fatTable);
    } while (currentCluster < 0x0FFFFFF8 && bytesRead < fileSize);
}
void CreateFile(int fd, const char *filename) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    struct dir_entry *entry;
    int i, sector, foundFree = FALSE;
    unsigned int currentCluster = bs->root_cluster;
    char name[12];
    snprintf(name, sizeof(name), "%-11.11s", filename);
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
                snprintf((char*)entry->name, 12, "%-11.11s", filename);
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
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];
    struct dir_entry *entry;
    int i, j, k, sector;
    unsigned int currentCluster = bs->root_cluster;
    unsigned int nextCluster;
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                printf("End of directory entries.\n");
                return;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }
            if (!(entry->attr & ATTR_VOLUME_ID)) {
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
                for (k = strlen(fullname) - 1; k >= 0 && fullname[k] == ' '; k--) {
                    fullname[k] = '\0';
                }
                printf("Checking file: %s\n", fullname);
                if (strcmp(fullname, filename) == 0) {
                    entry->name[0] = 0xE5;
                    unsigned int cluster = ((entry->fstClusHI << 16) | entry->fstClusLO);
                    while (cluster < 0x0FFFFFF8) {
                        unsigned int nextCluster = getNextCluster(fd, cluster, fatTable);
                        freeCluster(fd, cluster);
                        cluster = nextCluster;
                    }
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
    unsigned int fatOffset = cluster * 4;
    unsigned int sectorOfFAT = bs->reserved_sectors + (fatOffset / SECTORSIZE);
    unsigned int offsetInSector = fatOffset % SECTORSIZE;
    unsigned int zero = 0;
    readsector(fd, fatSector, sectorOfFAT);
    memcpy(&fatSector[offsetInSector], &zero, sizeof(unsigned int));
    writesector(fd, fatSector, sectorOfFAT);
}
unsigned int allocateNewCluster(int fd) {
    unsigned char fatSector[SECTORSIZE];
    unsigned int cluster;
    unsigned int fatEntry, sectorOfFAT, offsetInSector;

    for (cluster = 2; cluster < bs->total_clusters + 2; cluster++) {
        fatEntry = cluster * 4;
        sectorOfFAT = bs->reserved_sectors + (fatEntry / SECTORSIZE);
        offsetInSector = fatEntry % SECTORSIZE;
        readsector(fd, fatSector, sectorOfFAT);
        memcpy(&fatEntry, &fatSector[offsetInSector], sizeof(unsigned int));
        if (fatEntry == 0) {
            fatEntry = 0x0FFFFFFF;
            memcpy(&fatSector[offsetInSector], &fatEntry, sizeof(unsigned int));
            writesector(fd, fatSector, sectorOfFAT);
            clearCluster(fd, cluster);
            return cluster;
        }
    }
    return 0xFFFFFFFF;
}

void WriteDataToFile(int fd, const char *filename, unsigned int offset, unsigned int n, unsigned char data) {
    unsigned char buffer[bs->sectors_per_cluster * SECTORSIZE];
    unsigned char dirBuffer[SECTORSIZE];
    unsigned char fatTable[bs->sectors_per_fat * SECTORSIZE];
    struct dir_entry *entry;
    int i, j, k, sector, found = FALSE;
    unsigned int sectorOffset, bytesToWrite, dirSector, entryOffset;
    unsigned int currentCluster = bs->root_cluster;
    for (i = 0; i < bs->sectors_per_fat; ++i) {
        readsector(fd, fatTable + (i * SECTORSIZE), bs->reserved_sectors + i);
    }
    do {
        sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
        for (i = 0; i < bs->sectors_per_cluster; i++) {
            readsector(fd, buffer + (i * SECTORSIZE), sector + i);
        }
        for (j = 0; j < (bs->sectors_per_cluster * SECTORSIZE) / sizeof(struct dir_entry); j++) {
            entry = (struct dir_entry *)(buffer + j * sizeof(struct dir_entry));
            if (entry->name[0] == 0x00) {
                printf("End of directory entries.\n");
                return;
            }
            if (entry->name[0] == 0xE5) {
                continue;
            }

            if (!(entry->attr & ATTR_VOLUME_ID)) {
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
                for (k = strlen(fullname) - 1; k >= 0 && fullname[k] == ' '; k--) {
                    fullname[k] = '\0';
                }
                printf("Checking file: %s\n", fullname);
                if (strcmp(fullname, filename) == 0) {
                    found = TRUE;
                    printf("Found file: %s\n", fullname);
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
    dirSector = sector + (j * sizeof(struct dir_entry)) / SECTORSIZE;
    entryOffset = (j * sizeof(struct dir_entry)) % SECTORSIZE;
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
            entry->fstClusHI = (currentCluster >> 16) & 0xFFFF;
            entry->fstClusLO = currentCluster & 0xFFFF;
        }
    }
    sector = (currentCluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
    sectorOffset = offset % SECTORSIZE;
    readsector(fd, buffer, sector);
    bytesToWrite = SECTORSIZE - sectorOffset;
    bytesToWrite = (bytesToWrite > n) ? n : bytesToWrite;
    memset(buffer + sectorOffset, data, bytesToWrite);
    writesector(fd, buffer, sector);
    if (entry->fileSize < offset + n) {
        entry->fileSize = offset + n;
        printf("Updated file size: %u\n", entry->fileSize);
        readsector(fd, dirBuffer, dirSector);
        memcpy(dirBuffer + entryOffset, entry, sizeof(struct dir_entry));
        writesector(fd, dirBuffer, dirSector);
    }
}
void clearCluster(int fd, unsigned int cluster) {
    unsigned char zeroBuffer[CLUSTERSIZE];
    memset(zeroBuffer, 0, CLUSTERSIZE);
    unsigned int sector = (cluster - 2) * bs->sectors_per_cluster + bs->reserved_sectors;
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