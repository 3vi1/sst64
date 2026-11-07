#ifndef SST64_GAME_FLOATSCAN_H
#define SST64_GAME_FLOATSCAN_H

typedef struct {
    const unsigned char *begin;
    const unsigned char *cursor;
} SST64_FLOAT_STREAM;

long double __floatscan(SST64_FLOAT_STREAM *stream, int precision, int partial);

#define shgetc(stream) (*(stream)->cursor++)
#define shunget(stream) (--(stream)->cursor)
#define shlim(stream, limit) ((stream)->cursor = (stream)->begin)

#endif