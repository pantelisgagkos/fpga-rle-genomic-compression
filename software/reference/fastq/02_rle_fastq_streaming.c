#include <stdio.h>
#include <ctype.h>
#include <string.h>
#include <stdlib.h>


void convertToUppercase( char *sPtr);

void rleEncodeChar(char c, char *current_char,int *current_count,int *started, size_t *optimizedCompressBytes, size_t *run_count);

void flushLastRun(int current_count,int started,size_t *optimizedCompressBytes,size_t *run_count);

int main(void){

    char buffer[10000];
    char current_char = '\0';
    int current_count = 0;
    int started = 0;
    char current_charQuality = '\0';
    int current_countQuality = 0;
    int startedQuality = 0;
    int lineInRecord = 0;
    size_t run_count = 0;
    size_t optimizedCompressedBytes = 0;
    size_t originalSeqQuaBytes = 0;
    size_t inputBytes = 0;

 
    FILE *cfPtr = fopen("ERR173280_1.fastq","r");

    if (cfPtr == NULL){
        perror("Error opening file");
        return 1;
    }

    while(fgets(buffer,sizeof(buffer),cfPtr) != NULL){
        inputBytes += strlen(buffer);
        
        if(lineInRecord == 0){
            //Header
            lineInRecord++;        
        }else if(lineInRecord == 1){
            //RLE sequense
            buffer[strcspn(buffer, "\n")] = '\0';
            buffer[strcspn(buffer, "\r")] = '\0';
            convertToUppercase(buffer);
            int j = 0;
            while (buffer[j] != '\0'){
                char c = buffer[j];
                originalSeqQuaBytes++;
                rleEncodeChar(c,&current_char,&current_count,&started,
                &optimizedCompressedBytes,&run_count);
                j++;
                    
            }



            lineInRecord++;
                
        }else if(lineInRecord == 2){
            //Plus
            lineInRecord++;
        }else{
            //RLE quality
            buffer[strcspn(buffer, "\n")] = '\0';
            buffer[strcspn(buffer, "\r")] = '\0';
            int j = 0;
            while (buffer[j] != '\0' ){
                char c = buffer[j];
                originalSeqQuaBytes++;
                rleEncodeChar(c,&current_charQuality,&current_countQuality,&startedQuality,
                &optimizedCompressedBytes,&run_count);
                 j++;
                    

            }

            flushLastRun(current_count,started,&optimizedCompressedBytes,&run_count);
            flushLastRun(current_countQuality,startedQuality,&optimizedCompressedBytes,&run_count);

            lineInRecord = 0;
            started = 0;
            startedQuality = 0;
            current_char = '\0';
            current_charQuality = '\0';
            current_count = 0;
            current_countQuality = 0;
        }

            
                
    }



    double fileCompressionRatio = 0.0;
    double OriginalCompressionRatio = 0.0;

    

    if (originalSeqQuaBytes > 0){
        OriginalCompressionRatio = optimizedCompressedBytes / (double)originalSeqQuaBytes;
    }
    if (inputBytes > 0){
        fileCompressionRatio = optimizedCompressedBytes / (double)inputBytes;
    }

    printf("Input bytes: %zu bytes\n", inputBytes);
    printf("Original sequence+quality bytes: %zu bytes\n", originalSeqQuaBytes);
    printf("Compressed size: %zu bytes\n", optimizedCompressedBytes);
    printf("Compression ratio vs input bytes: %.4f\n", fileCompressionRatio);
    printf("Compression ratio vs sequence+quality bytes: %.4f\n", OriginalCompressionRatio);



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



    



