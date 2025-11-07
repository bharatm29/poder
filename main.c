#include "zlib/include/zconf.h"
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
#define CHAR sizeof(uint8_t)
#define CRC 4

enum ColorType {
    COLOR_GRAYSCALE = 0,
    COLOR_TRUE_RGB = 2,
    COLOR_INDEXED = 3,
    COLOR_GRAYSCALE_ALPHA = 4,
    COLOR_TRUEALPHA_RGBA = 6
};

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
        panic("error with inflate init");
    }

    inflate(&d_stream, Z_FINISH);
    if (err != Z_OK && err != Z_STREAM_END) {
        panic("error with inflate");
    }

    err = inflateEnd(&d_stream);
    if (err != Z_OK) {
        panic("error with inflate after ending");
    }

    return d_stream.total_out;
}

uint convert_uint(uint8_t *buff) {
    uint32_t n = 0;
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

bool validate_signature(FILE *file) {
    // 89 PNG(504E47) 0D 0A 1A 0A
    uint64_t sig = 0x89504E470D0A1A0A;

    uint8_t buff[8];

    if (!fread(buff, CHAR, 8, file))
        panic("Couldn't read signature");

    uint64_t orig;
    memcpy(&orig, buff, 8);
    orig = __builtin_bswap64(orig); // big endian to little endian

    if (sig != orig)
        return false;

    return true;
}

void render(uint width, uint height, Image image) {
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

int main() {
    const char* pngfile = "pngs/chart.png";
    FILE *file = fopen(pngfile, "rb");
    if (file == NULL) {
        perror("fopen");
        exit(69);
    }

    if (!validate_signature(file))
        panic("Invalid PNG signature");

    size_t data_cap = sizeof(uint8_t) * 2048;
    size_t data_t = 0;
    uint8_t *data = malloc(data_cap); // contains entire compressed IDAT data

    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t bit_depth = 0;
    uint32_t color_type = 0;

    char type[5];            // chunk type
    uint8_t buff[128] = {0}; // reading file bytes into
    while (true) {
        uint32_t length = 0;
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
            uint8_t chunk[length];
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
            panic("PLTE not handled");
        } else {
            printf("Auxillary chunk(%s) or some error!: %u\n", type, length);
            fseek(file, length,
                  SEEK_CUR); // FIXME: right now just skipping auxillary chunks
        }

        fseek(file, CRC, SEEK_CUR); // FIXME: skip CRC bytes
    }

    printf("%s: %ux%u, %u depth and %u color type with %zu bytes data\n",
           pngfile, width, height, bit_depth, color_type, data_t);

    uint32_t bpp = (color_type == 6) ? 4 : 3; // byte per pixel

    // uncompress IDAT chunks
    uLong uncomprLen = (width * bpp + 1) * height; // + 1 for filter byte
    uint8_t *uncompressed = malloc(uncomprLen);
    uLong bytes = zinflate(data, uncompressed, data_t, uncomprLen);

    // apply filtering to uncompressed
    uint8_t *idat = malloc(width * height * bpp);
    recon(uncompressed, idat, width, height, bpp);

    uint8_t *raw = idat;
    Image image = GenImageColor(width, height, BLACK); // write to image

    for (int j = 0; j < height; j++) {
        for (int i = 0; i < width; i++) {
            if (!raw)
                panic("raw is NULL");
            Color color;
            // FIXME: only supporting color_type 6 and 4
            if (color_type == COLOR_TRUEALPHA_RGBA) {
                color = (Color){raw[0], raw[1], raw[2], raw[3]};
                raw += 4; // one pixel
            } else if (color_type == COLOR_TRUE_RGB) {
                color = (Color){raw[0], raw[1], raw[2], 255};
                raw += 3; // one pixel
            } else {
                panic("Poder does not support color type");
            }
            ImageDrawPixel(&image, i, j, color);
        }
    }

    fclose(file);
    free(uncompressed);
    free(data);
    free(idat);

    render(width, height, image);
}
