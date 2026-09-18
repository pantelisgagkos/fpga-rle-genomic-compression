#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>

struct Run{
    char base;
    int count;
};

void convertToUppercase( char *sPtr);

void rleEncodeChar(char c,struct Run **compressed,size_t *length,size_t *capacity,
    char *current_char,int *current_count,int *started);

void flushLastRun(struct Run **compressed,size_t *length,size_t *capacity,
    char *current_char,int *current_count,int *started);

void rleDecodeChar(char base,int count,char **decoded,size_t *decodedLength,
    size_t *decodedCapacity);

void appendToOriginal(char **originalSequence, size_t *originalLength, 
    size_t *originalCapacity, const char *buffer);

int main(void){

    char buffer[10000];
    int i_newRecord = 0;
    size_t capacity = 1024;
    size_t length = 0;     
    struct Run *compressed = malloc(capacity * sizeof(struct Run)); 
    char current_char = '\0';
    int current_count = 0;
    int started = 0;
    size_t originalCapacity = 1024;
    size_t originalLength = 0;
    char *originalSequence = malloc(originalCapacity * sizeof(char));
    int errors = 0;
    size_t optimizedCompressedBytes = 0;
    size_t originalSequenceBytes = 0;
    
    size_t originalFileSize = 0;
    size_t totalRuns = 0;
   
    if(compressed == NULL){
        printf("Memory allocstion failed\n");
        return 1;
    }

    if (originalSequence == NULL) {
        printf("Memory allocation failed\n");
        free(compressed);
        return 1;
    }
    originalSequence[0] = '\0';

    FILE *cfPtr = fopen("hg19.fa","r");

    if (cfPtr == NULL){
        perror("Error opening file");
        free(compressed);
        free(originalSequence);
        compressed = NULL;
        originalSequence = NULL;
        return 1;
    }

    FILE *encodedFile = fopen("encoded_fasta.txt", "w");
    if (encodedFile == NULL) {
        perror("Error opening encoded output file");
        free(compressed);
        free(originalSequence);
        fclose(cfPtr);
        return 1;
    }

    FILE *decodedFile = fopen("decoded_fasta.txt", "w");
    if (decodedFile == NULL) {
        perror("Error opening decoded output file");
        free(compressed);
        free(originalSequence);
        fclose(cfPtr);
        fclose(encodedFile);
        return 1;
    }

    FILE *validationFile = fopen("validation_fasta.txt", "w");
    if (validationFile == NULL) {
        perror("Error opening validation file");
        free(compressed);
        free(originalSequence);
        fclose(cfPtr);
        fclose(encodedFile);
        fclose(decodedFile);
        return 1;
    
    }

    while(fgets(buffer,sizeof(buffer),cfPtr) != NULL){
        originalFileSize += strlen(buffer);
        if(buffer[0] == '>'){
            if (i_newRecord > 0){
                flushLastRun(&compressed,&length,&capacity,&current_char,
                &current_count,&started);
                for (size_t i = 0; i < length; i++){
                    if (compressed[i].count > 5){
                        optimizedCompressedBytes += 5;
                        fprintf(encodedFile,"%c%d", compressed[i].base,compressed[i].count);
                    }else{
                        optimizedCompressedBytes += compressed[i].count;
                        for (size_t k = 0; k < compressed[i].count; k++){
                            fprintf(encodedFile, "%c",compressed[i].base);

                        }

                    }
                    
                }
                
                fprintf(encodedFile,"\n");

                size_t decodedCapacity = 1024;
                size_t decodedLength = 0;
                char *decoded = malloc(decodedCapacity * sizeof(char));

                if (decoded == NULL){
                    printf("Memory allocation failed\n");
                    exit(1);
                }

                for (size_t i = 0; i < length; i++){
                    rleDecodeChar(compressed[i].base,compressed[i].count, &decoded,
                    &decodedLength, &decodedCapacity);
                }

                decoded[decodedLength] = '\0';
                fprintf(decodedFile,"Decoded:%s\n", decoded);

                if(strcmp(originalSequence, decoded) == 0){
                    fprintf(validationFile, "Record %d OK\n", i_newRecord);
                }else{
                    fprintf(validationFile, "Record %d MISMATCH\n", i_newRecord);
                    errors++;
                }

                free(decoded);
                totalRuns += length;

                length = 0;
                current_char = '\0';
                current_count = 0;
                started = 0;
                originalLength = 0;
                originalSequence[0] = '\0';

            }
            fprintf(encodedFile,"Record%d",i_newRecord+1);
            fprintf(decodedFile,"Record%d",i_newRecord+1);
            i_newRecord++;
            
            continue;
            
            
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        convertToUppercase(buffer);
        appendToOriginal(&originalSequence,&originalLength,&originalCapacity,buffer);
        originalSequenceBytes += strlen(buffer);

        int j = 0;
        while (buffer[j] != '\0'){
            char c = buffer[j];

            rleEncodeChar(c,&compressed,&length,&capacity,
                &current_char,&current_count,&started);
            
            
            j++;
        }

    }

    flushLastRun(&compressed,&length,&capacity,
                &current_char,&current_count,&started);
    

    for (size_t i = 0; i < length; i++){
        if (compressed[i].count > 5){
            optimizedCompressedBytes += 5;
            fprintf(encodedFile,"%c%d", compressed[i].base,compressed[i].count);
        }else{
            optimizedCompressedBytes += compressed[i].count;
            for (size_t k = 0; k < compressed[i].count; k++){
                fprintf(encodedFile, "%c",compressed[i].base);

            }

        }
                    
    }

    fprintf(encodedFile,"\n");
    
    size_t decodedCapacity  = 1024;
    size_t decodedLength = 0;
    char *decoded = malloc(decodedCapacity * sizeof(char));

    if (decoded == NULL){
        printf("Memory allocation failed\n");
        exit(1);
    }

    for (size_t i = 0; i < length; i++){
        rleDecodeChar(compressed[i].base,compressed[i].count, &decoded,
        &decodedLength, &decodedCapacity);
    }

    decoded[decodedLength] = '\0';
    fprintf(decodedFile,"Decoded:%s\n", decoded);

    if(strcmp(originalSequence, decoded) == 0){
        fprintf(validationFile, "Record %d OK\n", i_newRecord);
    }else{
        fprintf(validationFile, "Record %d MISMATCH\n", i_newRecord);
        errors++;
    }



    free(decoded);
    totalRuns += length;
    
    fflush(encodedFile);
    double fileCompressionRatio = 0.0;
    

    if (originalSequenceBytes > 0){
        fileCompressionRatio = optimizedCompressedBytes / (double)originalSequenceBytes;
    }
    printf("Original file size: %zu bytes\n", originalFileSize);
    printf("Original sequence bytes: %zu bytes\n", originalSequenceBytes);
    printf("Compressed size: %zu bytes\n", optimizedCompressedBytes);
    printf("File compression ratio: %.4f\n", fileCompressionRatio);
    printf("Validation errors: %d\n", errors);



    free(compressed);
    fclose(cfPtr);
    fclose(encodedFile);
    fclose(decodedFile);
    fclose(validationFile);
    free(originalSequence);
    return 0;

}


void convertToUppercase(char *sPtr){
    while (*sPtr != '\0'){
        *sPtr = toupper((unsigned char)*sPtr);
        ++sPtr;
    }
}

void rleEncodeChar(char c,struct Run **compressed,size_t *length,size_t *capacity,
    char *current_char,int *current_count,int *started){

        if (*started == 0 ){
            *current_char = c;
            *current_count = 1;
            *started = 1;
            return;
        }

        if (c == *current_char ){
            (*current_count)++;
            return;
        }

        if (*length + 1 >= *capacity){
            *capacity *= 2;
            struct Run *temp = realloc(*compressed, *capacity * sizeof(struct Run));
            if (temp == NULL){
                printf("Realloc failed\n");
                exit(1);
            }
            *compressed = temp;

        }

        (*compressed)[*length].base = *current_char;
        (*compressed)[*length].count = *current_count;


        (*length)++;

        *current_char = c;
        *current_count = 1;

        

}

void flushLastRun(struct Run **compressed,size_t *length,size_t *capacity,
    char *current_char,int *current_count,int *started){
        if(*started == 0){
            return;
        }

        if (*length + 1 >= *capacity){
            *capacity *= 2;
            struct Run *temp = realloc(*compressed, *capacity * sizeof(struct Run));
            if (temp == NULL){
                printf("Realloc failed\n");
                exit(1);
            }
            *compressed = temp;

        }

        (*compressed)[*length].base = *current_char;
        (*compressed)[*length].count = *current_count;

        (*length)++;

}

void rleDecodeChar(char base,int count,char **decoded,size_t *decodedLength,
    size_t *decodedCapacity){

    while ((*decodedLength + count + 1 ) >= *decodedCapacity){
    *decodedCapacity *= 2;
        char *temp = realloc(*decoded,*decodedCapacity * sizeof(char));
        if(temp == NULL){
            printf("Realloc failed\n");
            exit(1);
        }
        *decoded = temp;
    }

    for (int i = 0; i < count; i++){
        (*decoded)[*decodedLength] = base;
        (*decodedLength)++;
    }



}

void appendToOriginal(char **originalSequence, size_t *originalLength, size_t *originalCapacity, const char *buffer) {
    size_t lineLen = strlen(buffer);

    while (*originalLength + lineLen + 1 > *originalCapacity) {
        *originalCapacity *= 2;
        char *temp = realloc(*originalSequence, *originalCapacity * sizeof(char));
        if (temp == NULL) {
            printf("Realloc failed\n");
            exit(1);
        }
        *originalSequence = temp;
    }

    memcpy(*originalSequence + *originalLength, buffer, lineLen);
    *originalLength += lineLen;
    (*originalSequence)[*originalLength] = '\0';
}