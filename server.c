#include "spec.h"

typedef struct Cache {
    uint16_t data_len;
    char data[MAX_DATA_LEN];
} Cache;

int sockfd;
uint32_t window_base;
uint32_t eof_packet;
Cache cache[WINDOW_SIZE];
bool ackd[WINDOW_SIZE];

void handler(int sig)
{
    close(sockfd);
    printf("INFO: Server successfully shutdown.\n");
    exit(EXIT_SUCCESS);
}

void interact(void)
{
    bool reach_eof = false;
    window_base = 0;
    FILE *fptr = 0;
    struct sockaddr sain;
    socklen_t addrlen = sizeof(sain);

    for(;;) {
        Packet rcvd_packet;
        if(recvfrom(sockfd, &rcvd_packet, sizeof(Packet), 0, &sain, &addrlen) < 0)
            FATAL_MSG("recvfrom");

        Pkt_type type = rcvd_packet.type;
        if(type == PKT_SYNC) {
            fptr = fptr ? fptr : fopen(rcvd_packet.data, "w");
            if(!fptr)
                FATAL_MSG("fopen");
            if(sendto(sockfd, &window_base, sizeof(window_base), 0, &sain, addrlen) < 0)
                FATAL_MSG("sendto");
        }
        else if(type == PKT_DATA) {
            uint32_t rcvd_seq = rcvd_packet.seq;
            uint32_t idx = rcvd_seq % WINDOW_SIZE;

            if(INSIDE_RANGE(window_base, window_base+WINDOW_SIZE-1, rcvd_seq) && !ackd[idx]) {
                ackd[idx] = true;
                cache[idx].data_len = rcvd_packet.data_len;
                memcpy(cache[idx].data, rcvd_packet.data, rcvd_packet.data_len);

                while(ackd[window_base%WINDOW_SIZE]) {
                    uint32_t idx = window_base % WINDOW_SIZE;
                    ackd[idx] = false;
                    fwrite(cache[idx].data, 1, cache[idx].data_len, fptr);
                    ++window_base;
                }
            }
            if(sendto(sockfd, &rcvd_seq, sizeof(rcvd_seq), 0, &sain, addrlen) < 0)
                FATAL_MSG("sendto");
        }
        else if(type == PKT_EOF) {
            uint32_t rcvd_seq = rcvd_packet.seq;

            eof_packet = rcvd_seq;
            reach_eof = true;
            if(sendto(sockfd, &rcvd_seq, sizeof(rcvd_seq), 0, &sain, addrlen) < 0)
                FATAL_MSG("sendto");
        }

        /* If all packets of the file have been received, close the file */
        if(reach_eof && window_base == eof_packet) {
            if(fclose(fptr) == EOF)
                FATAL_MSG("fclose");
            fptr = NULL;

            /* Reset for next round of data transfer */
            reach_eof = false;
            ++window_base;
        }
    }
}

int main(int argc, char **argv)
{
    if(argc < 2) {
        fprintf(stderr, "Usage: receiver <port>\n");
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0)
        FATAL_MSG("socket");

    struct sockaddr_in sain = {0};
    sain.sin_family = AF_INET;
    sain.sin_port = htons((uint16_t)atoi(argv[1]));
    sain.sin_addr.s_addr = htonl(INADDR_ANY);

    if(bind(sockfd, (struct sockaddr *)&sain, sizeof(sain)) < 0)
        FATAL_MSG("bind");

    struct sigaction sa = {0};
    sa.sa_handler = handler;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGINT, &sa, NULL) < 0)
        FATAL_MSG("sigaction");

    interact();

    return 0;
}
