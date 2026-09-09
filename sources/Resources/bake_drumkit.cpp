/* Bakes the drum kit at BUILD time and writes it as a blob.

   The kit is synthesised, deterministic and identical every run, so
   the device was spending seconds of every boot arriving at bytes
   that could have been decided once. Worse than the time: it did the
   work on the heap, twenty four blocks that are kept, immediately
   before the project's samples folder is read -- which is how a
   project opened by the autoload could come up with its samples
   missing while the same project opened a moment later was fine.

   This is the SAME synthesis. It includes DrumKit.cpp rather than
   copying anything out of it, so the two cannot drift: change a drum
   and the next build bakes the change. The app keeps the code for
   every other platform; only the PSP build reads the blob.

   Usage:  bakedrums <out.bin>

   Format, all little endian, which is what both ends are:

     int32  magic  'DKB1'
     int32  count            -- DRUMKIT_TOTAL
     int32  frames[count]    -- mono 16 bit frames per drum
     int16  pcm[...]         -- the drums, concatenated, in order
*/

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// Stand in for the app's allocator. The synthesis asks for two
// buffers and gives one back; nothing here outlives main.
#define DRUMKIT_BAKE_HOST 1
#define SYS_MALLOC(n) malloc(n)
#define SYS_FREE(p) free(p)

#include "Application/Instruments/DrumKit.cpp"

#define DK_MAGIC 0x31424B44   /* 'DKB1' little endian */

int main(int argc, char **argv) {

    if (argc < 2) {
        fprintf(stderr, "usage: bakedrums <out.bin>\n");
        return 2;
    }

    short *pcm[DRUMKIT_TOTAL];
    int frames[DRUMKIT_TOTAL];
    long total = 0;

    for (int i = 0; i < DRUMKIT_TOTAL; i++) {
        int n = 0;
        pcm[i] = DrumKit::BakePcm(i, &n);
        if (!pcm[i]) {
            fprintf(stderr, "bakedrums: %s would not bake\n", DrumKit::Name(i));
            return 1;
        }
        frames[i] = n;
        total += n;
    }
    DrumKit::ReleaseWork();

    FILE *f = fopen(argv[1], "wb");
    if (!f) {
        fprintf(stderr, "bakedrums: cannot write %s\n", argv[1]);
        return 1;
    }
    int header[2] = {DK_MAGIC, DRUMKIT_TOTAL};
    fwrite(header, sizeof(int), 2, f);
    fwrite(frames, sizeof(int), DRUMKIT_TOTAL, f);
    for (int i = 0; i < DRUMKIT_TOTAL; i++) {
        fwrite(pcm[i], sizeof(short), frames[i], f);
        free(pcm[i]);
    }
    fclose(f);

    printf("bakedrums: %d drums, %ld frames, %ld KB -> %s\n",
           DRUMKIT_TOTAL, total,
           (long)((8 + 4 * DRUMKIT_TOTAL + total * 2) / 1024), argv[1]);
    return 0;
}
