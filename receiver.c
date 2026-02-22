#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <math.h>

#define RECORD_SIZE 512
#define MAX_RECORDS_PER_PACKET 16

#define TYPE_DATA 1
#define TYPE_BLAST_OVER 0
#define TYPE_COMPLETE 2

typedef struct {
    char file_name[50];
    int file_size;
    int record_size;
} FILE_HEADER;

typedef struct {
    int TYPE;
    int blast_no;
    int packet_no;
    int start_record;
    int record_count;
    int total_packets;
    int missing_list[256];
    char data[MAX_RECORDS_PER_PACKET][RECORD_SIZE];
} Packet;

void recvBlast(int sockfd, FILE *fp,
               struct sockaddr_in *client,
               socklen_t *len,
               int file_size,
               long *total_written) {

    Packet packets[256];
    int received[256];
    memset(received, 0, sizeof(received));

    int total_packets = 0;

    while(1) {

        Packet pkt;
        int n = recvfrom(sockfd, &pkt,
                         sizeof(pkt), 0,
                         (struct sockaddr*)client, len);

        if(n <= 0) continue;

        if(pkt.TYPE == TYPE_COMPLETE) {
            fclose(fp);
            close(sockfd);
            printf("File Transfer Completed.\n");
            exit(0);
        }

        if(pkt.TYPE == TYPE_DATA) {
            packets[pkt.packet_no] = pkt;
            received[pkt.packet_no] = 1;
        }

        else if(pkt.TYPE == TYPE_BLAST_OVER) {

            total_packets = pkt.total_packets;

            Packet response;
            memset(&response, 0, sizeof(response));

            for(int i = 0; i < total_packets; i++) {
                if(!received[i])
                    response.missing_list[response.record_count++] = i;
            }

            sendto(sockfd, &response,
                   sizeof(response), 0,
                   (struct sockaddr*)client, *len);

            if(response.record_count == 0) {

                for(int i = 0; i < total_packets; i++) {
                    for(int r = 0; r < packets[i].record_count; r++) {

                        long remaining =
                            file_size - *total_written;

                        long to_write =
                            (remaining > RECORD_SIZE) ?
                            RECORD_SIZE : remaining;

                        fwrite(packets[i].data[r],
                               1, to_write, fp);

                        *total_written += to_write;
                    }
                }

                break;
            }
        }
    }
}


int main(int argc, char *argv[]) {

    if(argc != 3) {
        printf("Usage: %s <BindIP> <Port>\n", argv[0]);
        return -1;
    }

    char *bind_ip = argv[1];
    int port = atoi(argv[2]);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    struct sockaddr_in server, client;
    socklen_t len = sizeof(client);

    server.sin_family = AF_INET;
    server.sin_port = htons(port);

    if(inet_pton(AF_INET, bind_ip,
                 &server.sin_addr) <= 0) {
        printf("Invalid Bind IP\n");
        return -1;
    }

    if(bind(sockfd, (struct sockaddr*)&server,
            sizeof(server)) < 0) {
        perror("Bind failed");
        return -1;
    }

    FILE_HEADER header;
    recvfrom(sockfd, &header,
             sizeof(header), 0,
             (struct sockaddr*)&client, &len);

    FILE *fp = fopen("RecvVideo.mp4", "wb");

    int ack = 1;
    sendto(sockfd, &ack, sizeof(int), 0,
           (struct sockaddr*)&client, len);

    long total_written = 0;

    while(1)
        recvBlast(sockfd, fp, &client,
                  &len, header.file_size,
                  &total_written);
}