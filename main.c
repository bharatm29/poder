#include "zlib/include/zlib.h"
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include <raylib.h>
#include <raymath.h>

/*
 * Critical Chunks
 * IDHR(49 48 44 52)
 * PLTE(50 4c 54 45)
 * IDAT(49 44 41 54)
 * IEND(49 45 4e 44)
 *
 * Auxillary chuncks
 * bKGD: 62 4b 47 44
 * cHRM: 63 48 52 4d
 * cICP: 63 49 43 50
 * dSIG: 64 53 49 47
 * eXIf: 65 58 49 66
 * gAMA: 67 41 4d 41
 * hIST: 68 49 53 54
 * iCCP: 69 43 43 50
 * iTXt: 69 54 58 74
 * pHYs: 70 48 59 73
 * sBIT: 73 42 49 54
 * sPLT: 73 50 4c 54
 * sRGB: 73 52 47 42
 * sTER: 73 54 45 52
 * tEXt: 74 45 58 74
 * tIME: 74 49 4d 45
 * tRNS: 74 52 4e 53
 * zTXt: 7a 54 58 74
 */

#define LENGTH 4
#define CHAR sizeof(u_char)
#define CRC 4

void panic(const char *message) {
    printf("%s\n", message);
    exit(69);
}

uLong zinflate(unsigned char *in, unsigned char *out, uLong comprLen,
               uLong uncomprLen) {
    int err;
    z_stream d_stream; /* decompression stream */

    strcpy((char *)out, "garbage");

    d_stream.zalloc = (alloc_func)0;
    d_stream.zfree = (free_func)0;
    d_stream.opaque = (voidpf)0;

    d_stream.next_in = in;
    d_stream.avail_in = comprLen;
    d_stream.next_out = out;
    d_stream.avail_out = uncomprLen;

    err = inflateInit(&d_stream);
    if (err != Z_OK) {
        panic("error with inflate init\n");
    }

    inflate(&d_stream, Z_FINISH);
    if (err != Z_OK && err != Z_STREAM_END) {
        panic("error with inflate\n");
    }

    err = inflateEnd(&d_stream);
    if (err != Z_OK) {
        panic("error with inflate after ending\n");
    }

    return d_stream.total_out;
}

uint convert_uint(u_char *buff) {
    uint n = 0;
    memcpy(&n, buff, 4);
    return __builtin_bswap32(n); // big endian to little endian
}

// required for recon
static inline uint8_t paeth_predictor(uint8_t a, uint8_t b, uint8_t c) {
    int p = a + b - c;
    int pa = abs(p - a);
    int pb = abs(p - b);
    int pc = abs(p - c);
    if (pa <= pb && pa <= pc)
        return a;
    else if (pb <= pc)
        return b;
    else
        return c;
}

// i don't know where I got this from, but I do know that I didn't write it
void recon(uint8_t *data, uint8_t *out, int width, int height, int bpp) {
    int stride = width * bpp;
    uint8_t *prev_row = calloc(stride, 1);
    uint8_t *cur_row = data;
    uint8_t *dst = out;

    for (int y = 0; y < height; y++) {
        uint8_t filter = *cur_row++;
        for (int x = 0; x < stride; x++) {
            uint8_t raw = cur_row[x];
            uint8_t recon;

            uint8_t left = (x >= bpp) ? dst[x - bpp] : 0;
            uint8_t above = prev_row[x];
            uint8_t upper_left = (x >= bpp) ? prev_row[x - bpp] : 0;

            switch (filter) {
            case 0:
                recon = raw;
                break;
            case 1:
                recon = raw + left;
                break;
            case 2:
                recon = raw + above;
                break;
            case 3:
                recon = raw + ((left + above) >> 1);
                break;
            case 4:
                recon = raw + paeth_predictor(left, above, upper_left);
                break;
            default:
                recon = raw;
                break;
            }

            dst[x] = recon;
        }

        memcpy(prev_row, dst, stride);
        dst += stride;
        cur_row += stride;
    }

    free(prev_row);
}

int main() {
    FILE *file = fopen("pngs/juliaSet.png", "rb");
    if (file == NULL) {
        perror("fopen");
        exit(69);
    }

    fseek(file, 8, SEEK_SET); // FIXME: skip PNG signature

    size_t data_cap = sizeof(u_char) * 2048;
    u_char *data = malloc(data_cap); // contains entire IDAT data
    size_t data_t = 0;

    uint width = 0;
    uint height = 0;
    uint bit_depth = 0;
    uint color_type = 0;

    char type[5];           // chunk type
    u_char buff[128] = {1}; // reading file bytes into
    while (1) {
        uint length = 0;
        if (fread(buff, CHAR, LENGTH, file) < 1)
            break; // terminate if EOF
        length = convert_uint(buff);

        fread(buff, CHAR, CHAR * 4, file);
        memmove(type, buff, 4);
        type[4] = '\0';

        if (strcmp(type, "IHDR") == 0) {
            fread(buff, CHAR, LENGTH, file);
            width = convert_uint(buff);

            fread(buff, CHAR, LENGTH, file);
            height = convert_uint(buff);

            fread(buff, CHAR, CHAR, file);
            bit_depth = buff[0];

            fread(buff, CHAR, CHAR, file);
            color_type = buff[0];

            // skipping compression method, filter method and interlace because
            // they are always the same
            fseek(file, 3, SEEK_CUR);

        } else if (strcmp(type, "IDAT") == 0) {
            u_char chunk[length];
            fread(chunk, CHAR, length, file);

            if (data_cap - data_t < length) {
                data_cap = (data_cap + length);
                data = realloc(data, data_cap);
            }

            memmove(data + data_t, chunk, length);
            data_t += length;
        } else if (strcmp(type, "IEND") == 0) {
            // everything already done
        } else if (strcmp(type, "PLTE") == 0) {
            panic("PLTE not handled\n");
        } else {
            printf("Auxillary chunk(%s) or some error!: %u\n", type, length);
            fseek(file, length,
                  SEEK_CUR); // FIXME: right now just skipping auxillary chunks
        }

        fseek(file, CRC, SEEK_CUR); // FIXME: skip CRC bytes
    }

    printf("image: %ux%u, %u depth and %u color type with %zu bytes data\n",
           width, height, bit_depth, color_type, data_t);

    int bpp = (color_type == 6) ? 4 : 3; // byte per pixel

    // uncompress IDAT chunks
    uLong uncomprLen = (width * bpp + 1) * height; // + 1 for filter byte
    u_char *uncompressed = malloc(uncomprLen);
    int bytes = zinflate(data, uncompressed, data_t, uncomprLen);

    // apply filtering to uncompressed
    uint8_t *idat = malloc(width * height * bpp);
    recon(uncompressed, idat, width, height, bpp);

    fclose(file);
    free(data);

    u_char *raw = idat;
    Image image = GenImageColor(width, height, BLACK); // write to image

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            if (!raw)
                panic("raw is NULL\n");
            Color color;
            // FIXME: only supporting color_type 6 and 4
            if (color_type == 6) {
                color = (Color){raw[0], raw[1], raw[2], raw[3]};
                raw += 4; // one pixel
            } else {
                color = (Color){raw[0], raw[1], raw[2], 255};
                raw += 3; // one pixel
            }
            ImageDrawPixel(&image, i, j, color);
        }
    }

    // Raylib shit
    SetTraceLogLevel(LOG_ERROR);
    InitWindow(width, height, "Poder");
    SetTargetFPS(60);

    Camera2D camera = {0};
    camera.target = (Vector2){width / 2., height / 2.};
    camera.offset = (Vector2){width / 2., height / 2.};
    camera.rotation = 0.0f;
    camera.zoom = 1.0f;

    Texture2D texture = LoadTextureFromImage(image);

    while (!WindowShouldClose()) {
        BeginDrawing();
        BeginMode2D(camera);

        camera.zoom =
            expf(logf(camera.zoom) + ((float)GetMouseWheelMove() * 0.1f));
        camera.offset = GetMousePosition(); // no idea if both are required
        camera.target = GetMousePosition(); // no idea if both are required

        if (camera.zoom > 3.0f)
            camera.zoom = 3.0f;
        else if (camera.zoom < 1.f)
            camera.zoom = 1.f;

        DrawTexture(texture, 0, 0, WHITE);

        EndMode2D();
        EndDrawing();
    }

    UnloadImage(image);
    UnloadTexture(texture);

    CloseWindow();
}
