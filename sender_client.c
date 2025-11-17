#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <errno.h>
#include <math.h>

#define RECORD_SIZE 512 // Bytes
#define BLAST_SIZE 200 // Records 
#define MAX_RECORDS_PER_PACKET 16

int PACKETS_PER_BLAST = ceil((double)BLAST_SIZE / MAX_RECORDS_PER_PACKET);
size_t FILE_SIZE;
size_t TOTAL_RECORDS;

char RECEIVER_IP[20];
int PORT;

struct sockaddr_in server_addr;
socklen_t addr_len = sizeof(server_addr);

struct FILE_HEADER {
    char file_name[30];
    int file_size;
    int record_size;
};

struct Packet {
    int packetList[13]; // REC_MISS
    int recordList[2]; //(start_record, end_record)
    char storedRecords[16][RECORD_SIZE];
    int PACKET_NUMBER;
    int PACKET_LEN;
    int TYPE; //1 -> Data, 0 -> IS_BLAST_OVER
    int SEND_PACKETS; // 0 -> List Is Empty, 1 -> Packet Lost On Way
};

void sendPackets(int sockfd, struct Packet packets[], int totalPackets, int *toSendList) {
    for(int i = 0;i < totalPackets; i++) {
        if(toSendList[i] == 1) {
            printf("Packet Sent : %d\n", i);
            sendto(sockfd, &packets[i], sizeof(packets[i]), 0, (struct sockaddr *)&server_addr, addr_len);
            usleep(50000);
        }
        
    }
}

void isBlastOver(int sockfd, int totalPackets) {
    struct Packet packet;

    packet.PACKET_NUMBER = 123; // SPECIAL PACKET
    packet.PACKET_LEN = totalPackets;
    packet.TYPE = 0; // BLAST OVER CHECK
    packet.SEND_PACKETS = 1;
    
    for(int i = 0;i < totalPackets; i++) {
        packet.packetList[i] = 1;
    }

    usleep(50000);
    
    sendto(sockfd, &packet, sizeof(packet), 0, (struct sockaddr *)&server_addr, addr_len);
    printf("Blast Packet with TYPE %d\n", packet.TYPE);
}


//Max <= 16 Records in Packet
void createPacket(struct Packet *packet, FILE *fp, int maxRecords) {
    char buffer[RECORD_SIZE];
    int bufferRead;
    int recordNo = 0;

    while(recordNo < maxRecords && (bufferRead = fread(buffer, 1, RECORD_SIZE, fp)) > 0) {
        memcpy(packet -> storedRecords[recordNo], buffer, bufferRead);
        recordNo++;
    }

    packet -> recordList[0] = 0; // start
    packet -> recordList[1] = recordNo - 1; // end
    packet -> TYPE = 1; //Data 
    packet -> SEND_PACKETS = 1;
}


void blastFile(int sockfd, int totalRecords, FILE *fp) {
    
    printf("File Size : %d Bytes\n", (int)FILE_SIZE);
    printf("Disc Block Size/Records : %d\n", (int)BLAST_SIZE);
    printf("Packets per Blast : %d\n", (int)PACKETS_PER_BLAST);
    
    while(totalRecords > 0) {
        printf("Total Records to Transfer : %d\n", (int)totalRecords);

        int PACKETS_BLAST;
        int maxRecordsInBlast;

        if(totalRecords >= BLAST_SIZE) {
            PACKETS_BLAST = ceil((double)BLAST_SIZE/MAX_RECORDS_PER_PACKET);
            maxRecordsInBlast = BLAST_SIZE;
        }
        else {
            PACKETS_BLAST = ceil((double)totalRecords/MAX_RECORDS_PER_PACKET);
            maxRecordsInBlast = totalRecords;
        }

        //Blasting
        struct Packet packets[PACKETS_BLAST];
        int currentPacket = 0;
        
        //Enquiry Packet
        struct Packet packetsToSend;
        packetsToSend.SEND_PACKETS = 1;

        int copyOfmaxRecordsInBlast = maxRecordsInBlast;

        //Packets Created
        for(int currentPacket = 0;currentPacket < PACKETS_BLAST; currentPacket++) {
            if(copyOfmaxRecordsInBlast >= MAX_RECORDS_PER_PACKET) {
                createPacket(&packets[currentPacket], fp, MAX_RECORDS_PER_PACKET);
                packets[currentPacket].PACKET_NUMBER = currentPacket;
                copyOfmaxRecordsInBlast -= MAX_RECORDS_PER_PACKET;
            }
            else {
                createPacket(&packets[currentPacket], fp, copyOfmaxRecordsInBlast);
                packets[currentPacket].PACKET_NUMBER = currentPacket;
                copyOfmaxRecordsInBlast -= copyOfmaxRecordsInBlast;
            }

            packetsToSend.packetList[currentPacket] = 1;
        }
         

        while(packetsToSend.SEND_PACKETS == 1) {
            sendPackets(sockfd, packets, PACKETS_BLAST, packetsToSend.packetList);
            printf("%s", "All Packets sent\n");
            
            isBlastOver(sockfd, PACKETS_BLAST);
            printf("%s", "Is Blast Over Sent\n");

            struct Packet recvdPacket;
            recvfrom(sockfd, &recvdPacket, sizeof(recvdPacket), 0, (struct sockaddr*)&server_addr, &addr_len);

            packetsToSend.SEND_PACKETS = recvdPacket.SEND_PACKETS;

            packetsToSend.SEND_PACKETS != packetsToSend.SEND_PACKETS;
        }
        
        //Blast Over
        totalRecords -= maxRecordsInBlast;
    }
}

size_t findFileSize(FILE *fp) {
    fseek(fp, 0L, SEEK_END);
    size_t fileSize = ftell(fp);
    fseek(fp, 0, SEEK_SET);    

    return fileSize;
}

int main() {
    FILE *fp;
    fp = fopen("KiBanu.mp4", "rb");
    FILE_SIZE = findFileSize(fp);

    struct timeval timeout;
    timeout.tv_sec = 10; // 10 second 
    timeout.tv_usec = 0; // 0 micro

    int sockfd = socket(AF_INET, SOCK_DGRAM ,0);

    if(sockfd < 0) {
        printf("Socket Creation Error\n");
        return - 1;
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout)) < 0) {
        printf("Set Timeout Error\n");
        return -1;
    }

    printf("[+] Socket is Created\n");

    printf("Enter the Receiver's IP : ");
    scanf("%s", RECEIVER_IP);

    printf("Enter the PORT Number : ");
    scanf("%d", &PORT);

    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(PORT);
    //IP address of Receiver IP
    server_addr.sin_addr.s_addr = inet_addr(RECEIVER_IP);

    socklen_t sock_len = sizeof(server_addr);

    //PHASE-1
    struct FILE_HEADER send_header;
    strcpy(send_header.file_name, "KiBanu.mp4");
    send_header.file_size = FILE_SIZE;
    send_header.record_size = RECORD_SIZE;
    
    int retry_count = 0;
    int ack = 0;

    while(retry_count < 5) {
        sendto(sockfd, &send_header, sizeof(send_header), 0, (struct sockaddr *)&server_addr, addr_len);

        int n = recvfrom(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)&server_addr, &addr_len);

        if(n < 0) {
            //HEADER not sent safely
            if(errno == EAGAIN || errno == EWOULDBLOCK) {
                printf("Timeout Occured\n");
                continue;
            }
            else {
                printf("Not read properly\n");
                return -1;
            }
        }
        else {
            printf("Ack Received for FILE HEADER\n");
            break;
        }

        retry_count++;
    }

    if(ack == 0) {
        printf("Connection Timeout occured");
        close(sockfd);
        return -1;
    }

    //PHASE-2
    int TOTAL_RECORDS = ceil((double)FILE_SIZE/RECORD_SIZE);

    blastFile(sockfd, TOTAL_RECORDS, fp);

    fclose(fp);
    close(sockfd);

    return 0;
}