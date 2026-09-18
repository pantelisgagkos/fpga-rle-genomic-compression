#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>


void convertToUppercase( char *sPtr);

void rleEncodeChar(char c,char *current_char,int *current_count,int *started,size_t *optimizedCompressBytes,size_t *run_count);

void flushLastRun(int current_count,int started,size_t *optimizedCompressBytes,size_t *run_count);


int main(void){

    char buffer[10000];
    char current_char = '\0';
    int current_count = 0;
    int started = 0;
    size_t run_count = 0;
    size_t optimizedCompressedBytes = 0;
    size_t originalSequenceBytes = 0;
    size_t inputBytes = 0;

    FILE *cfPtr = fopen("hg19.fa","r");

    if (cfPtr == NULL){
        perror("Error opening file");
        return 1;
    }


    while(fgets(buffer,sizeof(buffer),cfPtr) != NULL){
        inputBytes += strlen(buffer);
        if(buffer[0] == '>'){
            continue;
        }

        buffer[strcspn(buffer, "\n")] = '\0';
        buffer[strcspn(buffer, "\r")] = '\0';
        convertToUppercase(buffer);
        
        

        int j = 0;
        while (buffer[j] != '\0'){
            char c = buffer[j];
            originalSequenceBytes++;

            rleEncodeChar(c,&current_char,&current_count,&started,
                &optimizedCompressedBytes,&run_count);

            
            
            j++;
        }

    }

    flushLastRun(current_count,started,&optimizedCompressedBytes,&run_count);
    
    double fileCompressionRatio = 0.0;
    double OriginalCompressionRatio = 0.0;

    

    if (originalSequenceBytes > 0){
        OriginalCompressionRatio = optimizedCompressedBytes / (double)originalSequenceBytes;
    }
    if (inputBytes > 0){
        fileCompressionRatio = optimizedCompressedBytes / (double)inputBytes;
    }

    printf("Input bytes: %zu bytes\n", inputBytes);
    printf("Original sequence bytes: %zu bytes\n", originalSequenceBytes);
    printf("Compressed size: %zu bytes\n", optimizedCompressedBytes);
    printf("Compression ratio vs input bytes: %.4f\n", fileCompressionRatio);
    printf("Compression ratio vs sequence bytes: %.4f\n", OriginalCompressionRatio);



    fclose(cfPtr);
    return 0;

}

void convertToUppercase(char *sPtr){
    while (*sPtr != '\0'){
        *sPtr = toupper((unsigned char)*sPtr);
        ++sPtr;
    }
}

void rleEncodeChar(char c,char *current_char,int *current_count,int *started,
    size_t *optimizedCompressedBytes,size_t *run_count){

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

        if (*current_count > 5){
            *optimizedCompressedBytes +=5;
        }else{
            *optimizedCompressedBytes += *current_count;
        }

        (*run_count)++;

        *current_char = c;
        *current_count = 1;

        

}

void flushLastRun(int current_count,int started,
    size_t *optimizedCompressedBytes,size_t *run_count){
        if(started == 0){
            return;
        }

        if (current_count > 5) {
            *optimizedCompressedBytes += 5;
        } else {
            *optimizedCompressedBytes += current_count;
        }

         (*run_count)++;

}


