#ifndef SPEC_H
#define SPEC_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <stdbool.h>

#define FATAL_MSG(msg) do {perror(msg); exit(EXIT_FAILURE);} while(0)

#define INSIDE_RANGE(base, top, num) \
    ((base<=top && base<=num && num<=top) || (base>top && (num<=base || top<=num)))

#define MAX_DATA_LEN 1024
#define MAX_CMD_LEN 1024
#define WINDOW_SIZE (1 << 6)

typedef enum Pkt_type {
    PKT_SYNC,
    PKT_DATA,
    PKT_EOF
} Pkt_type;

typedef struct Packet {
    Pkt_type type;
    uint32_t seq;
    uint16_t data_len;
    char data[MAX_DATA_LEN];
} Packet;

#endif
