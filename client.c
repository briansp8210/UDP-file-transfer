#include "spec.h"
#include <time.h>
#include <stddef.h>
#include <sys/stat.h>

#define PACKET_SIZE(data_len) ((offsetof(Packet, data)) + (data_len))
#define BAR "================================"
#define BAR_LEN 32
#define GREEN "\x1b[32m"
#define RESET "\x1b[0m"

int sockfd;
uint32_t window_base;
uint32_t window_next;
timer_t timers[WINDOW_SIZE];
Packet cache[WINDOW_SIZE];
bool ackd[WINDOW_SIZE];
off_t filesize;

void print_progress(off_t sent)
{
    printf("[" GREEN "%-*.*s" RESET "] %3d%%\r",
           BAR_LEN, (filesize) ? (int)(BAR_LEN*sent/filesize) : BAR_LEN, BAR,
           (int)(100*sent/filesize));
}

void send_packet(uint32_t idx)
{
    if(write(sockfd, &cache[idx], PACKET_SIZE(cache[idx].data_len)) < 0)
        FATAL_MSG("write");

    // Set fixed RTT of 0.1 sec
    struct itimerspec its = {0};
    its.it_value.tv_nsec = 100000000;
    if(timer_settime(timers[idx], 0, &its, NULL) < 0)
        FATAL_MSG("timer_settime");
}

void disarm_timer(uint32_t idx)
{
    struct itimerspec its = {0};
    if(timer_settime(timers[idx], 0, &its, NULL) < 0)
        FATAL_MSG("timer_settime");
}

/* Signal handler for SIGALRM, 
 * retransmit the timeout packet fetch from cache. */
void handler(int sig, siginfo_t *info, void *uc)
{
    for(uint32_t idx=0; idx<WINDOW_SIZE; ++idx) {
        if(info->si_value.sival_ptr == &timers[idx]) {
            send_packet(idx);
            break;
        }
    }
}

void send_file(const char *filename)
{
    bool reach_eof = false;
    uint32_t seq, rcvd_seq;
    FILE *fptr = fopen(filename, "r");
    if(!fptr)
        FATAL_MSG("fopen");

    struct stat buf;
    if(stat(filename, &buf) < 0)
        FATAL_MSG("stat");
    filesize = buf.st_size;
    off_t sent = 0;

    /* Synchronize sequence number with server.
     * Meanwhile, inform server of name of file to be transmitted. */
    cache[0].type = PKT_SYNC;
    cache[0].data_len = strlen(filename) + 1;
    strcpy(cache[0].data, filename);
    send_packet(0);

    /* Get sequence number currently used by receiver. */
    if(read(sockfd, &rcvd_seq, sizeof(rcvd_seq)) < 0)
        FATAL_MSG("read");
    disarm_timer(0);
    seq = rcvd_seq;
    window_base = window_next = seq;

    /* Send the first $WINDOW_SIZE(or less) packets. */
    for(int i=0; i<WINDOW_SIZE; ++i) {
        uint32_t idx = window_next % WINDOW_SIZE;

        ackd[idx] = false;
        cache[idx].seq = seq++;
        size_t nbyte = fread(cache[idx].data, 1, MAX_DATA_LEN, fptr);
        if(!nbyte && feof(fptr)) {
            reach_eof = true;
            cache[idx].type = PKT_EOF;
            send_packet(idx);
            ++window_next;
            if(fclose(fptr) == EOF)
                FATAL_MSG("fclose");
            break;
        }
        else {
            cache[idx].type = PKT_DATA;
            cache[idx].data_len = nbyte;
            send_packet(idx);
            ++window_next;
        }
    }

    /* Keep receiving ACK and sending new data,
     * until all ACK of sent packet received. */
    while(window_base != window_next) {
        if(read(sockfd, &rcvd_seq, sizeof(rcvd_seq)) < 0)
            FATAL_MSG("read");

        /* Ignore ACK for sequence number outside window*/
        if(INSIDE_RANGE(window_base, window_next-1, rcvd_seq)) {
            uint32_t idx = rcvd_seq % WINDOW_SIZE;
            if(!ackd[idx]) {
                ackd[idx] = true;
                disarm_timer(idx);
                sent += cache[idx].data_len;
                print_progress(sent);
            }
        }
        /* Check ACK status and slide window. */
        while(ackd[window_base%WINDOW_SIZE]) {
            ackd[window_base%WINDOW_SIZE] = false;
            ++window_base;

            if(!reach_eof) {
                uint32_t idx = window_next % WINDOW_SIZE;
                cache[idx].seq = window_next;

                size_t nbyte = fread(cache[idx].data, 1, MAX_DATA_LEN, fptr);
                if(!nbyte && feof(fptr)) {
                    reach_eof = true;
                    cache[idx].type = PKT_EOF;
                    send_packet(idx);
                    if(fclose(fptr) == EOF)
                        FATAL_MSG("fclose");
                }
                else {
                    cache[idx].type = PKT_DATA;
                    cache[idx].data_len = nbyte;
                    send_packet(idx);
                }
                ++window_next;
            }
        }
    }
}

void interact(void)
{
    char cmdline[MAX_CMD_LEN];

    printf("> ");
    while(fgets(cmdline, sizeof(cmdline), stdin)) {
        char *cmd = strtok(cmdline, " \n\t");

        if(!strcmp(cmd, "put")) {
            char *filename = strtok(NULL, " \n\t");
            if(filename) {
                send_file(filename);
                printf("\n");
            }
            else
                printf("Error: invalid format\n");
        }
        else if(!strcmp(cmd, "exit"))
            return;
        else
            printf("Error: invalid command\n");
        printf("> ");
    }
}

int main(int argc, char **argv)
{
    if(argc < 3) {
        fprintf(stderr, "Usage: client <server_IP> <server_port>\n");
        exit(EXIT_FAILURE);
    }

    sockfd = socket(AF_INET, SOCK_DGRAM, 0);
    if(sockfd < 0)
        FATAL_MSG("socket");

    struct sockaddr_in sain = {0};
    sain.sin_family = AF_INET;
    sain.sin_port = htons((uint16_t)atoi(argv[2]));
    if(inet_pton(AF_INET, argv[1], &sain.sin_addr) < 0)
        FATAL_MSG("inet_pton");

    if(connect(sockfd, (struct sockaddr *)&sain, sizeof(sain)) < 0)
        FATAL_MSG("connect");

    struct sigaction sa = {0};
    sa.sa_flags = SA_SIGINFO | SA_RESTART;
    sa.sa_sigaction = handler;
    sigemptyset(&sa.sa_mask);
    if(sigaction(SIGALRM, &sa, NULL) < 0)
        FATAL_MSG("sigaction");

    struct sigevent sigev = {0};
    sigev.sigev_notify = SIGEV_SIGNAL;
    sigev.sigev_signo = SIGALRM;
    for(int i=0; i<WINDOW_SIZE; ++i) {
        sigev.sigev_value.sival_ptr = &timers[i];
        if(timer_create(CLOCK_REALTIME, &sigev, &timers[i]) < 0)
            FATAL_MSG("timer_create");
    }

    interact();

    close(sockfd);
    return 0;
}
