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

long getFileSize(FILE *fp);

int main(void){

    char buffer[10000];
    int i_newRecord = 0;
    size_t capacity = 1024;
    size_t length = 0;
    struct Run *compressed = malloc(capacity * sizeof(struct Run));
    size_t capacityQuality = 1024;
    size_t lengthQuality = 0;
    struct Run *compressedQuality = malloc(capacityQuality * sizeof(struct Run));
    char current_char = '\0';
    int current_count = 0;
    int started = 0;
    char current_charQuality = '\0';
    int current_countQuality = 0;
    int startedQuality = 0;
    char *originalSequence = NULL;
    char *originalQuality = NULL;
    int lineInRecord = 0;
    int errorsSequence = 0;
    int errorsQuality = 0;


    size_t originalFileSize = 0;
    size_t totalRuns = 0;

    if(compressed == NULL){
        printf("Memory allocation failed\n");
        return 1;
    }

    if (compressedQuality == NULL){
        printf("Memory allocation failed\n");
        return 1;
    }

 
    FILE *cfPtr = fopen("ERR173280_1.fastq","r");

    if (cfPtr == NULL){
        perror("Error opening file");
        free(compressed);
        free(compressedQuality);
        compressed = NULL;
        compressedQuality = NULL;
        return 1;
    }

    FILE *encodedFile = fopen("encoded_fastq.txt", "w");
    if (encodedFile == NULL) {
        perror("Error opening encoded output file");
        free(compressed);
        free(compressedQuality);
        fclose(cfPtr);
        return 1;
    }

    FILE *decodedFile = fopen("decoded_fastq.txt", "w");
    if (decodedFile == NULL) {
        perror("Error opening decoded output file");
        free(compressed);
        free(compressedQuality);
        fclose(cfPtr);
        fclose(encodedFile);
        return 1;
    }

    FILE *encodedQualityFile = fopen("encoded_fastq_quality.txt", "w");
    if (encodedQualityFile == NULL) {
        perror("Error opening encoded quality output file");
        free(compressed);
        free(compressedQuality);
        fclose(cfPtr);
        fclose(encodedFile);
        fclose(decodedFile);
        return 1;
    }

    FILE *decodedQualityFile = fopen("decoded_fastq_quality.txt", "w");
    if (decodedQualityFile == NULL) {
        perror("Error opening decoded quality output file");
        free(compressed);
        free(compressedQuality);
        fclose(cfPtr);
        fclose(encodedFile);
        fclose(decodedFile);
        fclose(encodedQualityFile);
        return 1;
    }

    FILE *validationFile = fopen("validation_fastq.txt", "w");
    if (validationFile == NULL) {
        perror("Error opening validation file");
        free(compressed);
        free(compressedQuality);
        fclose(cfPtr);
        fclose(encodedFile);
        fclose(decodedFile);
        fclose(encodedQualityFile);
        fclose(decodedQualityFile);
        return 1;
    
    }




    while(fgets(buffer,sizeof(buffer),cfPtr) != NULL){
        originalFileSize += strlen(buffer);
        
        if(lineInRecord == 0){
            //Header
            lineInRecord++;

            fprintf(encodedFile,"Record%d\n", i_newRecord + 1);
            fprintf(decodedFile,"Record%d\n", i_newRecord + 1);
            fprintf(encodedQualityFile,"Record%d\n", i_newRecord + 1);
            fprintf(decodedQualityFile,"Record%d\n", i_newRecord + 1);
            i_newRecord++;
            
            
                
                
        }else if(lineInRecord == 1){
            //RLE sequense
            buffer[strcspn(buffer, "\n")] = '\0';
            convertToUppercase(buffer);
            free(originalSequence);
            originalSequence = malloc((strlen(buffer) + 1) * sizeof(char));
            if (originalSequence == NULL) {
                printf("Memory allocation failed\n");
                free(compressed);
                free(compressedQuality);
                fclose(encodedFile);
                fclose(decodedFile);
                fclose(encodedQualityFile);
                fclose(decodedQualityFile);
                fclose(cfPtr);
                return 1;
            }
            strcpy(originalSequence, buffer);
            int j = 0;
            while (buffer[j] != '\0'){
                char c = buffer[j];
                rleEncodeChar(c,&compressed,&length,&capacity,
                &current_char,&current_count,&started);
                j++;
                    
            }



            lineInRecord++;
                
        }else if(lineInRecord == 2){
            //Plus
            lineInRecord++;
        }else{
            //RLE quality
            buffer[strcspn(buffer, "\n")] = '\0';
            free(originalQuality);
            originalQuality = malloc((strlen(buffer) + 1) * sizeof(char));
            if (originalQuality == NULL) {
                printf("Memory allocation failed\n");
                free(originalSequence);
                free(compressed);
                free(compressedQuality);
                fclose(encodedFile);
                fclose(decodedFile);
                fclose(encodedQualityFile);
                fclose(decodedQualityFile);
                fclose(cfPtr);
                return 1;
            }
            strcpy(originalQuality, buffer);
            int j = 0;
            while (buffer[j] != '\0' ){
                char c = buffer[j];
                rleEncodeChar(c,&compressedQuality,&lengthQuality,&capacityQuality,
                &current_charQuality,&current_countQuality,&startedQuality);
                 j++;
                    

            }

            flushLastRun(&compressed,&length,&capacity,
            &current_char,&current_count,&started);   
            for (size_t i = 0; i < length; i++){
                fprintf(encodedFile,"%c%d", compressed[i].base,compressed[i].count);
        
            }

            fprintf(encodedFile,"\n");

                    
                
            flushLastRun(&compressedQuality,&lengthQuality,&capacityQuality,
            &current_charQuality,&current_countQuality,&startedQuality);
            for (size_t i = 0; i < lengthQuality; i++){

                fprintf(encodedQualityFile,"%c%d", compressedQuality[i].base,compressedQuality[i].count);
            }
            fprintf(encodedQualityFile,"\n");

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
            fprintf(decodedFile,"\nDecoded:%s\n", decoded);
            

            size_t decodedCapacityQuality = 1024;
            size_t decodedLengthQuality = 0;
            char *decodedQuality = malloc(decodedCapacityQuality * sizeof(char));

            if (decodedQuality == NULL){
                printf("Memory allocation failed\n");
                exit(1);
            }

            for (size_t i = 0; i < lengthQuality; i++){
                rleDecodeChar(compressedQuality[i].base,compressedQuality[i].count, &decodedQuality,
                &decodedLengthQuality, &decodedCapacityQuality);
            }

            decodedQuality[decodedLengthQuality] = '\0';
            fprintf(decodedQualityFile,"DecodedQuality: %s\n", decodedQuality);
            if (originalSequence != NULL && strcmp(originalSequence, decoded) == 0) {
                fprintf(validationFile,"Record %d sequence OK\n", i_newRecord);
            }else{
                fprintf(validationFile,"Record %d sequence MISMATCH\n", i_newRecord);
                errorsSequence++;
            }

            if (originalQuality != NULL && strcmp(originalQuality, decodedQuality) == 0) {
                fprintf(validationFile,"Record %d quality OK\n", i_newRecord);
            }else{
                fprintf(validationFile,"Record %d quality MISMATCH\n", i_newRecord);
                errorsQuality++;
            }
            free(decoded);
            free(decodedQuality);
            free(originalSequence);
            originalSequence = NULL;

            free(originalQuality);
            originalQuality = NULL;
            totalRuns += length + lengthQuality;
            




                


            lineInRecord = 0;
            started = 0;
            startedQuality = 0;
            length = 0;
            lengthQuality = 0;
            current_char = '\0';
            current_charQuality = '\0';
            current_count = 0;
            current_countQuality = 0;
        }

            
                
    }


fflush(encodedFile);
fflush(encodedQualityFile);
long encodedSeqSize = getFileSize(encodedFile);
long encodedQualSize = getFileSize(encodedQualityFile);
long compressedSize = encodedSeqSize + encodedQualSize;
double fileCompressionRatio = 0.0;

if (originalFileSize > 0){
    fileCompressionRatio = (double)compressedSize / (double)originalFileSize;
}
printf("Original file size: %zu bytes\n", originalFileSize);
printf("Compressed size: %zu bytes\n", compressedSize);
    
printf("File compression ratio: %.4f\n", fileCompressionRatio);
printf("Sequence Errors: %d\n",errorsSequence);
printf("Quality Errors: %d\n",errorsQuality);

free(compressed);
free(compressedQuality);
fclose(cfPtr);
fclose(encodedFile);
fclose(encodedQualityFile);
fclose(decodedFile);
fclose(decodedQualityFile);
fclose(validationFile);
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

long getFileSize(FILE *fp){
    long currentPos = ftell(fp);
    fseek(fp, 0, SEEK_END);
    long size = ftell(fp);
    fseek(fp, currentPos, SEEK_SET);
    return size;
}


    



