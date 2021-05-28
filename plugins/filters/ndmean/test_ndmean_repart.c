/*
    Copyright (C) 2014  Francesc Alted
    http://blosc.org
    License: BSD 3-Clause (see LICENSE.txt)

    Example program demonstrating use of the Blosc filter from C code.

    To compile this program:

    $ gcc -O many_compressors.c -o many_compressors -lblosc2

    To run:

    $ ./test_ndmean
    Blosc version info: 2.0.0a6.dev ($Date:: 2018-05-18 #$)
    Using 4 threads (previously using 1)
    Using blosclz compressor
    Compression: 4000000 -> 57577 (69.5x)
    Succesful roundtrip!
    Using lz4 compressor
    Compression: 4000000 -> 97276 (41.1x)
    Succesful roundtrip!
    Using lz4hc compressor
    Compression: 4000000 -> 38314 (104.4x)
    Succesful roundtrip!
    Using zlib compressor
    Compression: 4000000 -> 21486 (186.2x)
    Succesful roundtrip!
    Using zstd compressor
    Compression: 4000000 -> 10692 (374.1x)
    Succesful roundtrip!

 */

#include <stdio.h>
#include "ndmean.h"
#include <math.h>

#define EPSILON (float) (1e-5)

static bool is_close(double d1, double d2) {

    double aux = 1;
    if (fabs(d1) < fabs(d2)) {
        if (fabs(d1) > 0) {
            aux = fabs(d2);
        }
    } else {
        if (fabs(d2) > 0) {
            aux = fabs(d1);
        }
    }

    return fabs(d1 - d2) < aux * EPSILON;
}


static int test_ndmean(blosc2_schunk* schunk) {

    blosc2_filter ndmean;
    ndmean.id = 165;
    ndmean.forward = ndmean_encoder;
    ndmean.backward = ndmean_decoder;
    blosc2_register_filter(&ndmean);

    int32_t typesize = schunk->typesize;
    int nchunks = schunk->nchunks;
    int32_t chunksize = (int32_t) (schunk->chunksize);
    //   int isize = (int) array->extchunknitems * typesize;
    uint8_t *data_in = malloc(chunksize);
    int decompressed;
    int64_t csize = 0;
    int64_t dsize = 0;
    int64_t csize_f = 0;
    uint8_t *data_out = malloc(chunksize + BLOSC_MAX_OVERHEAD);
    uint8_t *data_dest = malloc(chunksize);

    /* Create a context for compression */
    blosc2_cparams cparams = BLOSC2_CPARAMS_DEFAULTS;
    cparams.splitmode = BLOSC_ALWAYS_SPLIT;
    cparams.typesize = typesize;
    cparams.compcode = BLOSC_ZSTD;
    cparams.filters[4] = 165;
    cparams.filters_meta[4] = 4;
    cparams.filters[BLOSC2_MAX_FILTERS - 1] = BLOSC_SHUFFLE;
    cparams.clevel = 9;
    cparams.nthreads = 1;
    cparams.blocksize = schunk->blocksize;
    cparams.schunk = schunk;
    blosc2_context *cctx;
    cctx = blosc2_create_cctx(cparams);

    blosc2_dparams dparams = BLOSC2_DPARAMS_DEFAULTS;
    dparams.nthreads = 1;
    dparams.schunk = schunk;
    blosc2_context *dctx;
    dctx = blosc2_create_dctx(dparams);

    for (int ci = 0; ci < nchunks; ci++) {

        decompressed = blosc2_schunk_decompress_chunk(schunk, ci, data_in, chunksize);
        if (decompressed < 0) {
            printf("Error decompressing chunk \n");
            return -1;
        }

        /*
        printf("\n data \n");
        for (int i = 0; i < nbytes; i++) {
        printf("%u, ", data2[i]);
        }
        */

        /* Compress with clevel=5 and shuffle active  */
        csize = blosc2_compress_ctx(cctx, data_in, chunksize, data_out, chunksize + BLOSC_MAX_OVERHEAD);
        if (csize == 0) {
            printf("Buffer is uncompressible.  Giving up.\n");
            return 0;
        } else if (csize < 0) {
            printf("Compression error.  Error code: %d\n", csize);
            return csize;
        }
        csize_f += csize;

/*
        printf("data_in: \n");
        for (int i = 0; i < chunksize / typesize; i++) {
            printf("%f, ",((float *) data_in)[i]);
        }
/*
        printf("\n out \n");
        for (int i = 0; i < chunksize; i++) {
            printf("%u, ", data_out[i]);
        }

        /* Decompress  */
        dsize = blosc2_decompress_ctx(dctx, data_out, chunksize + BLOSC_MAX_OVERHEAD, data_dest, chunksize);
        if (dsize <= 0) {
            printf("Decompression error.  Error code: %d\n", dsize);
            return dsize;
        }

        switch (typesize) {
            case 4:
                for (int i = 0; i < chunksize / typesize; i++) {
                    if (!is_close(((float *) data_in)[i], ((float *) data_dest)[i])) {
                        printf("i: %d, data %.9f, dest %.9f", i, ((float *) data_in)[i], ((float *) data_dest)[i]);
                        printf("\n Decompressed data differs from original!\n");
                        return -1;
                    }
                }
                break;
            case 8:
                for (int i = 0; i < chunksize / typesize; i++) {
                    if (!is_close(((double *) data_in)[i], ((double *) data_dest)[i])) {
                        printf("i: %d, data %.9f, dest %.9f", i, ((double *) data_in)[i], ((double *) data_dest)[i]);
                        printf("\n Decompressed data differs from original!\n");
                        return -1;
                    }
                }
                break;
            default :
                break;
        }
    }
    csize_f = csize_f / nchunks;

    free(data_in);
    free(data_out);
    free(data_dest);
    blosc2_free_ctx(cctx);
    blosc2_free_ctx(dctx);

    printf("Succesful roundtrip!\n");
    printf("Compression: %d -> %ld (%.1fx)\n", chunksize, csize_f, (1. * chunksize) / csize_f);
    return chunksize - csize_f;
}


int rand() {
    blosc2_schunk *schunk = blosc2_schunk_open("example_ndmean_repart_rand.caterva");

    /* Run the test. */
    int result = test_ndmean(schunk);
    blosc2_schunk_free(schunk);
    return result;
}

int same_cells() {
    blosc2_schunk *schunk = blosc2_schunk_open("example_ndmean_repart_same_cells.caterva");

    /* Run the test. */
    int result = test_ndmean(schunk);
    blosc2_schunk_free(schunk);
    return result;
}

int some_matches() {
    blosc2_schunk *schunk = blosc2_schunk_open("example_ndmean_repart_some_matches.caterva");

    /* Run the test. */
    int result = test_ndmean(schunk);
    blosc2_schunk_free(schunk);
    return result;
}


int main(void) {

    int result;

    result = rand();
    printf("rand: %d obtained \n \n", result);
    result = same_cells();
    printf("same_cells: %d obtained \n \n", result);
    result = some_matches();
    printf("some_matches: %d obtained \n \n", result);

}