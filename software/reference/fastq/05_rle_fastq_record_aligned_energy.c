#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define TARGET_CHUNK_SIZE 61440

void rleEncodeChar(char c, char *current_char, int *current_count, int *started,
                   size_t *optimizedCompressedBytes, size_t *run_count);

void flushLastRun(int current_count, int started,
                  size_t *optimizedCompressedBytes, size_t *run_count);

void processFastqChunk(const char *chunk, size_t chunkLen,
                       size_t *originalSeqQuaBytes,
                       size_t *optimizedCompressedBytes,
                       size_t *run_count);

uint64_t readRaplEnergyUj(int fd) {
    char buffer[64];

    ssize_t n = pread(fd, buffer, sizeof(buffer) - 1, 0);

    if (n <= 0) {
        perror("Error reading RAPL energy_uj");
        exit(1);
    }

    buffer[n] = '\0';

    return strtoull(buffer, NULL, 10);
}


int main(void) {
    FILE *cfPtr = fopen("ERR173280_1.fastq", "rb");

    if (cfPtr == NULL) {
        perror("Error opening file");
        return 1;
    }

    size_t chunkCapacity = TARGET_CHUNK_SIZE * 2;
    char *chunk = malloc(chunkCapacity);

    if (chunk == NULL) {
        printf("Memory allocation failed\n");
        fclose(cfPtr);
        return 1;
    }

    char line[10000];

    size_t chunkLen = 0;
    size_t inputBytes = 0;
    size_t originalSeqQuaBytes = 0;
    size_t optimizedCompressedBytes = 0;
    size_t run_count = 0;
    size_t chunk_count = 0;

    struct timespec start_time, end_time;
    double total_processing_time = 0.0;

    const char *rapl_path =
        "/sys/class/powercap/intel-rapl:0/energy_uj";

    const uint64_t max_energy_range_uj = 262143328850ULL;

    uint64_t energy_start_uj;
    uint64_t energy_end_uj;
    uint64_t total_energy_uj = 0;

    int rapl_fd = open(rapl_path, O_RDONLY);

    if (rapl_fd < 0) {
        perror("Error opening RAPL energy counter");
        free(chunk);
        fclose(cfPtr);
        return 1;
    }

    int fastq_state = 0;  // 0=header, 1=sequence, 2=plus, 3=quality

    while (fgets(line, sizeof(line), cfPtr) != NULL) {
        size_t lineLen = strlen(line);

        inputBytes += lineLen;

        while (chunkLen + lineLen > chunkCapacity) {
            chunkCapacity *= 2;
            char *tmp = realloc(chunk, chunkCapacity);

            if (tmp == NULL) {
                printf("Realloc failed\n");
                free(chunk);
                fclose(cfPtr);
                return 1;
            }

            chunk = tmp;
        }

        memcpy(chunk + chunkLen, line, lineLen);
        chunkLen += lineLen;

        /*
         * FASTQ state update:
         * 0 -> header
         * 1 -> sequence
         * 2 -> plus
         * 3 -> quality
         *
         * Επειδή διαβάζουμε με fgets, κάθε line αντιστοιχεί σε μία FASTQ γραμμή.
         */
        fastq_state++;
        if (fastq_state == 4) {
            fastq_state = 0;
        }

        /*
         * Flush chunk μόνο αφού περάσουμε το target size
         * ΚΑΙ βρισκόμαστε σε όριο πλήρους FASTQ record.
         */
        if (chunkLen >= TARGET_CHUNK_SIZE && fastq_state == 0) {

            energy_start_uj = readRaplEnergyUj(rapl_fd);

            clock_gettime(CLOCK_MONOTONIC, &start_time);

            processFastqChunk(chunk, chunkLen,
                              &originalSeqQuaBytes,
                              &optimizedCompressedBytes,
                              &run_count);
            
            clock_gettime(CLOCK_MONOTONIC, &end_time);

            energy_end_uj = readRaplEnergyUj(rapl_fd);

            total_processing_time +=
                (end_time.tv_sec - start_time.tv_sec) +
                (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
            

            if (energy_end_uj >= energy_start_uj) {

                total_energy_uj +=
                    energy_end_uj - energy_start_uj;

            }else{

                total_energy_uj +=
                    (max_energy_range_uj - energy_start_uj)
                    + energy_end_uj;
            }

            chunk_count++;
            chunkLen = 0;
        }
    }

    /*
     * Τελευταίο chunk, αν έχει μείνει κάτι.
     */
    if (chunkLen > 0) {
        if (fastq_state != 0) {
            printf("Warning: final chunk ends inside incomplete FASTQ record, fastq_state=%d\n",
                   fastq_state);
        }

        energy_start_uj = readRaplEnergyUj(rapl_fd);

        clock_gettime(CLOCK_MONOTONIC, &start_time);

        processFastqChunk(chunk, chunkLen,
                          &originalSeqQuaBytes,
                          &optimizedCompressedBytes,
                          &run_count);
        
        clock_gettime(CLOCK_MONOTONIC, &end_time);

        energy_end_uj = readRaplEnergyUj(rapl_fd);

        total_processing_time +=
            (end_time.tv_sec - start_time.tv_sec) +
            (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

        if (energy_end_uj >= energy_start_uj) {

            total_energy_uj +=
                energy_end_uj - energy_start_uj;

        }else{

                total_energy_uj +=
                    (max_energy_range_uj - energy_start_uj)
                    + energy_end_uj;
        }
        
        chunk_count++;
    }

    close(rapl_fd);

    free(chunk);
    fclose(cfPtr);

    double ratioInput = 0.0;
    double ratioSeqQual = 0.0;

    if (inputBytes > 0) {
        ratioInput = optimizedCompressedBytes / (double)inputBytes;
    }

    if (originalSeqQuaBytes > 0) {
        ratioSeqQual = optimizedCompressedBytes / (double)originalSeqQuaBytes;
    }

    double total_energy_j =
        total_energy_uj / 1000000.0;

    double average_package_power_w =
        total_energy_j / total_processing_time;

    printf("Input bytes: %zu bytes\n", inputBytes);
    printf("Original sequence+quality bytes: %zu bytes\n", originalSeqQuaBytes);
    printf("Compressed size: %zu bytes\n", optimizedCompressedBytes);
    printf("Chunks: %zu\n", chunk_count);
    printf("Run count: %zu\n", run_count);
    printf("Compression ratio vs input bytes: %.4f\n", ratioInput);
    printf("Compression ratio vs sequence+quality bytes: %.4f\n", ratioSeqQual);
    printf("Processing time: %.6f seconds\n", total_processing_time);
    printf("Package energy: %.6f Joules\n", total_energy_j);
    printf("Average package power: %.6f Watts\n", average_package_power_w);

    return 0;
}

void processFastqChunk(const char *chunk, size_t chunkLen,
                       size_t *originalSeqQuaBytes,
                       size_t *optimizedCompressedBytes,
                       size_t *run_count) {
    int lineInRecord = 0;

    char current_char = '\0';
    int current_count = 0;
    int started = 0;

    char current_charQuality = '\0';
    int current_countQuality = 0;
    int startedQuality = 0;

    for (size_t i = 0; i < chunkLen; i++) {
        char c = chunk[i];

        if (c == '\r') {
            continue;
        }

        if (c == '\n') {
            /*
             * Τέλος sequence line.
             */
            if (lineInRecord == 1) {
                flushLastRun(current_count, started,
                             optimizedCompressedBytes, run_count);

                current_char = '\0';
                current_count = 0;
                started = 0;
            }

            /*
             * Τέλος quality line.
             */
            if (lineInRecord == 3) {
                flushLastRun(current_countQuality, startedQuality,
                             optimizedCompressedBytes, run_count);

                current_charQuality = '\0';
                current_countQuality = 0;
                startedQuality = 0;
            }

            lineInRecord++;
            if (lineInRecord == 4) {
                lineInRecord = 0;
            }

            continue;
        }

        if (lineInRecord == 0) {
            /*
             * Header line: αγνόηση
             */
            continue;
        } else if (lineInRecord == 1) {
            /*
             * Sequence line: uppercase/normalize όπως FPGA.
             */
            char nc = (char)toupper((unsigned char)c);

            if (nc == 'A' || nc == 'C' || nc == 'G' || nc == 'T' || nc == 'N') {
                (*originalSeqQuaBytes)++;

                rleEncodeChar(nc, &current_char, &current_count, &started,
                              optimizedCompressedBytes, run_count);
            }
        } else if (lineInRecord == 2) {
            /*
             * Plus line: αγνόηση
             */
            continue;
        } else {
            /*
             * Quality line: raw chars, χωρίς uppercase.
             */
            (*originalSeqQuaBytes)++;

            rleEncodeChar(c, &current_charQuality, &current_countQuality, &startedQuality,
                          optimizedCompressedBytes, run_count);
        }
    }
}

void rleEncodeChar(char c, char *current_char, int *current_count, int *started,
                   size_t *optimizedCompressedBytes, size_t *run_count) {
    if (*started == 0) {
        *current_char = c;
        *current_count = 1;
        *started = 1;
        return;
    }

    if (c == *current_char) {
        (*current_count)++;
        return;
    }

    if (*current_count > 5) {
        *optimizedCompressedBytes += 5;
    } else {
        *optimizedCompressedBytes += *current_count;
    }

    (*run_count)++;

    *current_char = c;
    *current_count = 1;
}

void flushLastRun(int current_count, int started,
                  size_t *optimizedCompressedBytes, size_t *run_count) {
    if (started == 0) {
        return;
    }

    if (current_count > 5) {
        *optimizedCompressedBytes += 5;
    } else {
        *optimizedCompressedBytes += current_count;
    }

    (*run_count)++;
}