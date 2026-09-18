#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

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

    int inHeader = 0;

    FILE *cfPtr = fopen("hg19.fa", "rb");

    if (cfPtr == NULL) {
        perror("Error opening file");
        return 1;
    }

    while ((bytesRead = fread(buffer, 1, CHUNK_SIZE, cfPtr)) > 0) {
        inputBytes += bytesRead;

        /*
         * Reset RLE state per chunk, ώστε να μιμείται chunked FPGA/DMA processing.
         */
        started = 0;
        current_count = 0;
        current_char = '\0';

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

        /*
         * Flush στο τέλος κάθε chunk.
         * Αυτό είναι το σημαντικό για να ταιριάξει με chunked FPGA behavior.
         */
        flushLastRun(current_count, started,
                     &optimizedCompressedBytes, &run_count);
    }

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