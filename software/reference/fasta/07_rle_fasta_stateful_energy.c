#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <stdint.h>
#include <fcntl.h>
#include <unistd.h>

#define CHUNK_SIZE 65536

void rleEncodeChar(char c, char *current_char, int *current_count, int *started,
                   size_t *optimizedCompressedBytes, size_t *run_count);

void flushLastRun(int current_count, int started,
                  size_t *optimizedCompressedBytes, size_t *run_count);

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

    char buffer[CHUNK_SIZE];

    char current_char = '\0';
    int current_count = 0;
    int started = 0;

    size_t run_count = 0;
    size_t optimizedCompressedBytes = 0;
    size_t originalSequenceBytes = 0;
    size_t inputBytes = 0;
    size_t bytesRead = 0;

    struct timespec start_time, end_time;
    double total_processing_time = 0.0;

    const char *rapl_path =
    "/sys/class/powercap/intel-rapl:0/energy_uj";

    const uint64_t max_energy_range_uj = 262143328850ULL;

    uint64_t energy_start_uj;
    uint64_t energy_end_uj;
    uint64_t total_energy_uj = 0;

    int rapl_fd = open(rapl_path, O_RDONLY);


    int inHeader = 0;

    FILE *cfPtr = fopen("hg19.fa", "rb");

    if (cfPtr == NULL) {
        perror("Error opening file");
        return 1;
    }

    if (rapl_fd < 0) {
        perror("Error opening RAPL energy counter");
        fclose(cfPtr);
        return 1;
    }


    while ((bytesRead = fread(buffer, 1, CHUNK_SIZE, cfPtr)) > 0) {
        inputBytes += bytesRead;

        energy_start_uj = readRaplEnergyUj(rapl_fd);

        clock_gettime(CLOCK_MONOTONIC, &start_time);
        

        for (size_t i = 0; i < bytesRead; i++) {
            char c = buffer[i];

            if (c == '>') {
                inHeader = 1;
                continue;
            }

            if (inHeader) {
                if (c == '\n') {
                    inHeader = 0;
                }
                continue;
            }

            if (c == '\n' || c == '\r') {
                continue;
            }

            c = (char)toupper((unsigned char)c);

            originalSequenceBytes++;

            rleEncodeChar(c, &current_char, &current_count, &started,
                          &optimizedCompressedBytes, &run_count);
        }

        clock_gettime(CLOCK_MONOTONIC, &end_time);

        energy_end_uj = readRaplEnergyUj(rapl_fd);

        total_processing_time +=
            (end_time.tv_sec - start_time.tv_sec) +
            (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

        if (energy_end_uj >= energy_start_uj) {

            total_energy_uj +=
                energy_end_uj - energy_start_uj;

        } else {

            /* RAPL counter wrap-around */
            total_energy_uj +=
                (max_energy_range_uj - energy_start_uj)
                + energy_end_uj;
        }

    }

    energy_start_uj = readRaplEnergyUj(rapl_fd);

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    flushLastRun(current_count, started,
                     &optimizedCompressedBytes, &run_count);

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    energy_end_uj = readRaplEnergyUj(rapl_fd);

    total_processing_time +=
        (end_time.tv_sec - start_time.tv_sec) +
        (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;
    
    if (energy_end_uj >= energy_start_uj) {

        total_energy_uj +=
            energy_end_uj - energy_start_uj;

    } else {

        total_energy_uj +=
            (max_energy_range_uj - energy_start_uj)
            + energy_end_uj;
    }

    close(rapl_fd);

    fclose(cfPtr);

    double ratioInput = 0.0;
    double ratioSequence = 0.0;

    if (inputBytes > 0) {
        ratioInput = optimizedCompressedBytes / (double)inputBytes;
    }

    if (originalSequenceBytes > 0) {
        ratioSequence = optimizedCompressedBytes / (double)originalSequenceBytes;
    }

    double total_energy_j =
        total_energy_uj / 1000000.0;

    double average_package_power_w =
        total_energy_j / total_processing_time;

    printf("Input bytes: %zu bytes\n", inputBytes);
    printf("Original sequence bytes: %zu bytes\n", originalSequenceBytes);
    printf("Compressed size: %zu bytes\n", optimizedCompressedBytes);
    printf("Run count: %zu\n", run_count);
    printf("Compression ratio vs input bytes: %.4f\n", ratioInput);
    printf("Compression ratio vs sequence bytes: %.4f\n", ratioSequence);
    printf("Processing time: %.6f seconds\n", total_processing_time);
    printf("Package energy: %.6f Joules\n", total_energy_j);
    printf("Average package power: %.6f Watts\n",
       average_package_power_w);

    return 0;
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