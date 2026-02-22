#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <math.h>
#include <time.h>

#define RECORD_SIZE 512
#define BLAST_SIZE 200
#define MAX_RECORDS_PER_PACKET 16

#define TYPE_DATA 1
#define TYPE_BLAST_OVER 0
#define TYPE_COMPLETE 2

double LOSS_PROB = 0.0;  // Set 0.1 for testing loss

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

struct sockaddr_in server_addr;
socklen_t addr_len = sizeof(server_addr);

int simulate_loss() {
    double r = (double)rand() / RAND_MAX;
    return (r > LOSS_PROB);
}

void sendPacket(int sockfd, Packet *pkt) {
    if(simulate_loss())
        sendto(sockfd, pkt, sizeof(Packet), 0,
               (struct sockaddr*)&server_addr, addr_len);
}

void sendBlastOver(int sockfd, int blast_no, int total_packets) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.TYPE = TYPE_BLAST_OVER;
    pkt.blast_no = blast_no;
    pkt.total_packets = total_packets;
    sendPacket(sockfd, &pkt);
}

void sendComplete(int sockfd) {
    Packet pkt;
    memset(&pkt, 0, sizeof(pkt));
    pkt.TYPE = TYPE_COMPLETE;
    sendPacket(sockfd, &pkt);
}

void blastFile(int sockfd, FILE *fp, int total_records) {

    int blast_no = 0;
    int global_record = 0;

    while(global_record < total_records) {

        int records_left = total_records - global_record;
        int records_in_blast =
            (records_left > BLAST_SIZE) ?
            BLAST_SIZE : records_left;

        int packets_in_blast =
            ceil((double)records_in_blast / MAX_RECORDS_PER_PACKET);

        Packet packets[packets_in_blast];

        int record_counter = 0;

        // Create packets
        for(int p = 0; p < packets_in_blast; p++) {

            packets[p].TYPE = TYPE_DATA;
            packets[p].blast_no = blast_no;
            packets[p].packet_no = p;
            packets[p].start_record = global_record + record_counter;

            int count = 0;

            for(int i = 0; i < MAX_RECORDS_PER_PACKET &&
                           record_counter < records_in_blast; i++) {

                size_t r = fread(packets[p].data[i],
                                 1, RECORD_SIZE, fp);

                if(r <= 0) break;

                count++;
                record_counter++;
            }

            packets[p].record_count = count;
        }

        // Send full blast once
        for(int i = 0; i < packets_in_blast; i++)
            sendPacket(sockfd, &packets[i]);

        int completed = 0;
        int retry = 0;
        int MAX_RETRY = 50;

        while(!completed && retry < MAX_RETRY) {

            sendBlastOver(sockfd, blast_no, packets_in_blast);

            Packet response;
            int n = recvfrom(sockfd, &response,
                             sizeof(response), 0, NULL, NULL);

            if(n < 0) {
                retry++;
                continue;
            }

            retry = 0;

            if(response.record_count == 0) {
                completed = 1;
            }
            else {
                for(int i = 0; i < response.record_count; i++) {
                    int missing = response.missing_list[i];
                    sendPacket(sockfd, &packets[missing]);
                }
            }
        }

        global_record += records_in_blast;
        blast_no++;
    }
}

int main(int argc, char *argv[]) {

    if(argc != 4) {
        printf("Usage: %s <ReceiverIP> <Port> <FileName>\n", argv[0]);
        return -1;
    }

    char *receiver_ip = argv[1];
    int port = atoi(argv[2]);
    char *filename = argv[3];

    srand(time(NULL));

    FILE *fp = fopen(filename, "rb");
    if(!fp) {
        printf("File open error\n");
        return -1;
    }

    fseek(fp, 0, SEEK_END);
    int file_size = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    int total_records =
        ceil((double)file_size / RECORD_SIZE);

    int sockfd = socket(AF_INET, SOCK_DGRAM, 0);

    struct timeval timeout = {0,100000}; // 100ms
    setsockopt(sockfd, SOL_SOCKET,
               SO_RCVTIMEO, &timeout, sizeof(timeout));

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(port);

    if(inet_pton(AF_INET, receiver_ip,
                 &server_addr.sin_addr) <= 0) {
        printf("Invalid IP address\n");
        return -1;
    }

    // Phase 1
    FILE_HEADER header;
    strcpy(header.file_name, filename);
    header.file_size = file_size;
    header.record_size = RECORD_SIZE;

    sendto(sockfd, &header, sizeof(header), 0,
           (struct sockaddr*)&server_addr, addr_len);

    int ack;
    recvfrom(sockfd, &ack, sizeof(int), 0, NULL, NULL);

    blastFile(sockfd, fp, total_records);
    sendComplete(sockfd);

    fclose(fp);
    close(sockfd);

    printf("File Transfer Completed.\n");
}