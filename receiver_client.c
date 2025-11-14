#include <stdio.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <string.h>
#include <arpa/inet.h>
#include <sys/time.h>
#include <math.h>

#define RECORD_SIZE 512 // Bytes
#define BLAST_SIZE 200 // Records 
#define MAX_RECORDS_PER_PACKET 16

int PACKETS_PER_BLAST = ceil((double)BLAST_SIZE / MAX_RECORDS_PER_PACKET);

char SERVER_IP[20];
int PORT;

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


struct sockaddr_in server_add, client_addr;
socklen_t client_addr_len = sizeof(client_addr);

void recvBlast(int sockfd, FILE *fp) {    
    struct Packet packetToRecv;
    memset(&packetToRecv, 0, sizeof(packetToRecv));

    // packetToRecv.TYPE = 1;
    packetToRecv.SEND_PACKETS = 1;
    int packetListCheck[13];

    while(packetToRecv.SEND_PACKETS == 1) {
        int n = recvfrom(sockfd, &packetToRecv, sizeof(packetToRecv), 0, (struct sockaddr *)&client_addr, &client_addr_len); 

        int packNum = packetToRecv.PACKET_NUMBER;
        packetListCheck[packNum] = 1;

        //Packet Data
        if(packetToRecv.TYPE == 1) {
            int start = packetToRecv.recordList[0];
            int end =   packetToRecv.recordList[1];
            
            //Inserting All Records of Packet in File
            for(int i = start; i <= end; i++) {
                fwrite(packetToRecv.storedRecords[i], 1, RECORD_SIZE, fp);
            }
        }
        
        //Packet -> IS_BLAST_OVER
        else if(packetToRecv.TYPE == 0) {
            //Make a list of packets not received
            int listEmpty = 1;

            for(int i = 0;i < packetToRecv.PACKET_LEN; i++) {
                if(packetListCheck[i] == 0) {
                    packetToRecv.packetList[i] = 1;
                    listEmpty = 0;
                }
                else if(packetListCheck[i] == 1) {
                    packetToRecv.packetList[i] = 0;
                }
            }
            
            if(listEmpty == 1)
            packetToRecv.SEND_PACKETS = 0;

            sendto(sockfd, &packetToRecv, sizeof(packetToRecv), 0, (struct sockaddr *)&client_addr, client_addr_len);
        }
    }
}

int main() {
    FILE *fp;
    fp = fopen("RecvVideo.mp4", "wb");
    int sockfd = socket(AF_INET, SOCK_DGRAM ,0);

    if(sockfd < 0) {
        printf("Socket Creation Error\n");
        return - 1;
    }

    printf("[+] Socket is Created\n");

    printf("Enter the Receiver's IP : ");
    scanf("%s", SERVER_IP);

    printf("Enter the PORT Number : ");
    scanf("%d", &PORT);

    server_add.sin_family = AF_INET;
    server_add.sin_port = htons(PORT);
    //IP address of SENDER
    server_add.sin_addr.s_addr = inet_addr(SERVER_IP);

    socklen_t sock_len = sizeof(server_add);
    
    int bindfd = bind(sockfd, (struct sockaddr *)&server_add, sock_len);

    if(bindfd < 0) {
        printf("Bind Failed\n");
        return -1;
    }

    printf("[+] Binded With IP Address and Port\n");
  
    //PHASE-1
    struct FILE_HEADER recv_header;

    recvfrom(sockfd, &recv_header, sizeof(recv_header), 0, (struct sockaddr *)&client_addr, &client_addr_len);
    
    int ack = 1;
    sendto(sockfd, &ack, sizeof(int), 0, (struct sockaddr *)&client_addr, client_addr_len);

    //PHASE-2
    recvBlast(sockfd, fp);

    fclose(fp);
    close(sockfd);

    return 0;
}