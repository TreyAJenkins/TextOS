#include <stdio.h>

#define CHUNK 16

FILE *hexdump_open(const char *path, const char *mode) {
    FILE *fp;
    if (!(fp = fopen(path, mode))) {
	if (fp == NULL) {
	        printf("error opening '%s'", path);
	        return 0;
	}
    }
    return fp;
}

int main(int argc, char const *argv[]) {
    FILE *fp_in;
    FILE *fp_out;
    unsigned char buf[CHUNK];
    size_t nread;
    int i, c, npos;

    if (argc < 2 || argc > 3) {
        printf("usage: %s <file-in> [file-out]\n", argv[0]);
        return 0;
    }

    /* open the input file */
    fp_in = hexdump_open(argv[1], "r");

    /* redirect output if an output file is defined */
    fp_out = (argc == 3 ? hexdump_open(argv[2], "w") : stdout);

    npos = 0;
    /* display hex data CHUNK bytes at a time */
    while ((nread = fread(buf, 1, sizeof buf, fp_in)) > 0) {
        fprintf(fp_out, "%04x: ", npos);
        npos += CHUNK;

        /* print hex values e.g. 3f 62 ec f0*/
        for (i = 0; i < CHUNK; i++)
            fprintf(fp_out, "%02x ", buf[i]);

        /* print ascii values e.g. ..A6..ó.j...D*/
        for (i = 0; i < CHUNK; i++) {
            c = buf[i];
            fprintf(fp_out, "%c", (c >= 33 && c <= 255 ? c : '.'));
        }
        fprintf(fp_out, "\n");
    }

    fclose(fp_in);

    return 0;
}
