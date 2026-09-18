#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>
#include <time.h>

#define CHUNK_SIZE 65536

void rleEncodeChar(char c, char *current_char, int *current_count, int *started,
                   size_t *optimizedCompressedBytes, size_t *run_count);

void flushLastRun(int current_count, int started,
                  size_t *optimizedCompressedBytes, size_t *run_count);

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

    int inHeader = 0;

    FILE *cfPtr = fopen("hg19.fa", "rb");

    if (cfPtr == NULL) {
        perror("Error opening file");
        return 1;
    }


    while ((bytesRead = fread(buffer, 1, CHUNK_SIZE, cfPtr)) > 0) {
        inputBytes += bytesRead;

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

        total_processing_time +=
            (end_time.tv_sec - start_time.tv_sec) +
            (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

    }

    clock_gettime(CLOCK_MONOTONIC, &start_time);

    flushLastRun(current_count, started,
                     &optimizedCompressedBytes, &run_count);

    clock_gettime(CLOCK_MONOTONIC, &end_time);

    total_processing_time +=
        (end_time.tv_sec - start_time.tv_sec) +
        (end_time.tv_nsec - start_time.tv_nsec) / 1000000000.0;

    fclose(cfPtr);

    double ratioInput = 0.0;
    double ratioSequence = 0.0;

    if (inputBytes > 0) {
        ratioInput = optimizedCompressedBytes / (double)inputBytes;
    }

    if (originalSequenceBytes > 0) {
        ratioSequence = optimizedCompressedBytes / (double)originalSequenceBytes;
    }

    printf("Input bytes: %zu bytes\n", inputBytes);
    printf("Original sequence bytes: %zu bytes\n", originalSequenceBytes);
    printf("Compressed size: %zu bytes\n", optimizedCompressedBytes);
    printf("Run count: %zu\n", run_count);
    printf("Compression ratio vs input bytes: %.4f\n", ratioInput);
    printf("Compression ratio vs sequence bytes: %.4f\n", ratioSequence);
    printf("Processing time: %.6f seconds\n", total_processing_time);

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