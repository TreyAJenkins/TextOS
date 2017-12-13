#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>


#define STRINGIFY(x) #x
#define MACRO(x)     STRINGIFY(x)


struct Magic {
    char header[10];
    char magic[10];
    char mime[53];
    char human[53];
    unsigned short mlen;
};

const char filename[] = "/usr/magic.mdb";

struct Magic find(char head[10], int MRO) {
    struct stat st;
    stat(filename, &st);
    int size = st.st_size;
//    printf("Size: %i\n", size);
    int elements = size / sizeof(struct Magic);
//    printf("Elements: %i\n\n", elements);

    struct Magic DB[elements];

    FILE *file;
    file = fopen(filename, "rb");
    if (file != NULL) {
        fread(&DB, size, 1, file);
        fclose(file);
    }

    for (int i=0; i<elements; i++) {
        if (strncmp(head, DB[i].magic, DB[i].mlen) == 0) {
            return DB[i];
        }
        //printf("Header: %s | Magic: %s | Mime: %s | Human: %s\n", DB[i].header, DB[i].magic, DB[i].mime, DB[i].human);
    }
    return DB[0];
}

int decode() {
    printf("Loading...\n");

    struct stat st;
    stat(filename, &st);
    int size = st.st_size;
    printf("Size: %i\n", size);
    int elements = size / sizeof(struct Magic);
    printf("Elements: %i\n\n", elements);

    struct Magic DB[elements];

    FILE *file = fopen(filename, "rb");
    if (file != NULL) {
        fread(&DB, size, 1, file);
        fclose(file);
    }

    for (int i=0; i<elements; i++) {
        printf("Header: %s | Magic: %s | Mime: %s | Human: %s\n", DB[i].header, DB[i].magic, DB[i].mime, DB[i].human);
    }
    return 0;
}

int main(int argc, char const *argv[]) {

    if (argc < 2) {
        printf("usage: fileID (options) [file]\n\nOptions:\n-D: Dump DB\n-M: Mime output\n");
        return 1;
    }

    int DBD = 0; //Dump DB
    int MRO = 0; //Machine Readable Output (MIME)

    int FNP = 1; //File Name Position

//    printf("L93\n");

    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-D") == 0)
            DBD = 1;
        else if (strcmp(argv[i], "-M") == 0)
            MRO = 1;
        else
            FNP = i;
    }

    //printf("DBD: %i, MRO: %i\n", DBD, MRO);

    //printf("L104\n");

    if (DBD == 1) {
        //printf("L113");
        decode();
        return 0;
    }

    char head[10];

    FILE *file = fopen(argv[FNP], "rb");
    if (file != NULL) {
        fread(&head, 10, 1, file);
        fclose(file);
    } else {
        printf("Can't open file: %s\n", argv[FNP]);
        return 1;
    }

//    printf("L126\n");
    struct Magic ID = find(head, MRO);

    if (MRO == 1)
        printf("%s: %s\n", argv[FNP], ID.mime);
    else
        printf("%s: %s\n", argv[FNP], ID.human);

    return 0;
}
