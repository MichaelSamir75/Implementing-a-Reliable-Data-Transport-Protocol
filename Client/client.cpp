#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <netdb.h>
#include <string>
#include <bits/stdc++.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

using namespace std;

static const int MAXIMUM_SEGMENT_SIZE = 508;

// the packet for normal data
struct packet {
    uint16_t cksum;
    uint16_t len;
    uint32_t seqno;
    char data [500];
};

// packet for acknowledge
struct ack_packet {
    uint16_t cksum;
    uint16_t len;
    uint32_t ackno;
};

// function that takes the file name and put it in packet
packet createDataPacket(string file){
    struct packet packet{};
    strcpy(packet.data, file.c_str());
    packet.seqno = 0;
    packet.cksum = 0;
    packet.len = file.length() + sizeof(packet.cksum) + sizeof(packet.len) + sizeof(packet.seqno);
    return packet;
}

// function that gets the checksum for acknowledge packet
uint16_t checksumForAck(uint16_t length,  uint32_t ackNo){
    uint32_t s = 0;
    s += length;
    s += ackNo;
    while(s >> 16)
        s = (s & 0xFFFF) + (s >> 16);
    uint16_t checksum = (uint16_t)(~s);
    return checksum;
}

// function that gets the checksum for data packet
uint16_t checksumForData(string content, uint16_t length, uint32_t seqNo){
    uint32_t s = 0;
    s += length;
    s += seqNo;
    int n;
    n = content.length();
    char arr[n+1];
    strcpy(arr, content.c_str());
    for (int i = 0; i < n; i++){
        s += arr[i];
    }
    while (s >> 16){
        s = (s & 0xFFFF) + (s >> 16);
    }
    uint16_t checksum = (uint16_t) (~s);
    return checksum;
}

// function that takes sequence number of the reached packet and send acknowledge to the server
void send_ack(int client_socket, struct sockaddr_in server_address, int seqNum){
    // define the details of the ack msg
    struct ack_packet ack;
    ack.ackno = seqNum;
    ack.len = sizeof(ack);
    ack.cksum = checksumForAck(ack.len, ack.ackno);
    char* ack_buf = new char[MAXIMUM_SEGMENT_SIZE];
    memset(ack_buf, 0, MAXIMUM_SEGMENT_SIZE);
    memcpy(ack_buf, &ack, sizeof(ack));
    // send it to the server
    ssize_t bytesSent = sendto(client_socket, ack_buf, MAXIMUM_SEGMENT_SIZE, 0, (struct sockaddr *)&server_address, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("Error in sending the ack!");
        exit(1);
    } else {
        cout << "Ack with seq no. " << seqNum << " is sent." << endl << flush;
    }
}

// function that takes file as input and return vector of content of it
// each index in the vector contains a command
vector<string> readDetails(){
    string fileName = "run.txt";
    vector<string> c;
    string line;
    ifstream fin;
    fin.open(fileName);
    while(getline(fin, line))
    {
        c.push_back(line);
    }
    return c;
}

//function that takes name of new file and String of contents
//It add new file and put the content in it
void saveFile (string file, string content){
    ofstream f(file.c_str());
    f.write(content.c_str(), content.length());
}

int main() {
    // read the IP address, port number and filename you want to transfer
    vector<string> v = readDetails();
    string IP = v[0];
    // stoi() turns a string into an integer.
    int port = stoi(v[1]);
    string fileName = v[2];
    struct sockaddr_in server_address;
    int client_socket;
    // memset() intialize a string with the size of it with all zeros
    memset(&client_socket, '0', sizeof(client_socket));

    // socket doesn't connected sucessfully
    if ((client_socket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) {
        perror("failed");
        exit(1);
    }

    // Define address structure
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = INADDR_ANY;
    server_address.sin_port = htons(port);

    cout << "File Name is : " << fileName << endl << flush;

    // function to convert the file content to data packets
    struct packet fileName_packet = createDataPacket(fileName);
    char* buffer = new char[MAXIMUM_SEGMENT_SIZE ];
    memset(buffer, 0, MAXIMUM_SEGMENT_SIZE );
    memcpy(buffer, &fileName_packet, sizeof(fileName_packet));
    ssize_t bytesSent = sendto(client_socket, buffer, MAXIMUM_SEGMENT_SIZE , 0, (struct sockaddr *)&server_address, sizeof(struct sockaddr));
    if (bytesSent == -1) {
        perror("Error in sending the file name! ");
        exit(1);
    } else {
        cout << "Client Sent The file Name ." << endl << flush;
    }

    char rec_buffer[MAXIMUM_SEGMENT_SIZE];
    socklen_t addrlen = sizeof(server_address);
    // The recvfrom() function receives data on a socket named by descriptor socket and stores it in a buffer.
    ssize_t Received_bytes = recvfrom(client_socket, rec_buffer, MAXIMUM_SEGMENT_SIZE, 0, (struct sockaddr*)&server_address, &addrlen);
    if (Received_bytes < 0){
        perror("Error in receiving file name ack .");
        exit(1);
    }
    auto* ackPacket = (struct ack_packet*) rec_buffer;
    cout << "Number of packets " << ackPacket->len << endl;
    long numberOfPackets = ackPacket->len;
    string fileContents [numberOfPackets];
     
    // intialize a boolean array with all false as intially no packets are receive
    bool recieved[numberOfPackets] = {false};
    for( int i=1;i<= numberOfPackets;i++){
        memset(rec_buffer, 0, MAXIMUM_SEGMENT_SIZE);
        // The recvfrom() function receives data on a socket named by descriptor socket and stores it in a buffer.
        ssize_t bytesReceived = recvfrom(client_socket, rec_buffer, MAXIMUM_SEGMENT_SIZE, 0, (struct sockaddr*)&server_address, &addrlen);
        if (bytesReceived == -1){
            perror("Error in receiving data packet.");
            break;
        }
        auto* data_packet = (struct packet*) rec_buffer;
        cout <<"packet no. "<<i<<" received with seq no. " << data_packet->seqno << endl << flush;
        int len = data_packet->len;
        for (int j = 0 ; j < len ; j++){
            fileContents[data_packet->seqno] += data_packet->data[j];
        }
        // if the data is corrupted and it knows by using the checksum
        if (checksumForData(fileContents[data_packet->seqno], data_packet->len, data_packet->seqno) != data_packet->cksum){
            cout << "corrupted data packet !" << endl << flush;
        }
        // function that construct an ack msg and send it to the sender
        send_ack(client_socket, server_address , data_packet->seqno);
    }
    // loop on each packet and get the content in it and save it in file
    string content = "";
    for (int i = 0; i < numberOfPackets ; i++){
        content += fileContents[i];
    }
    saveFile(fileName, content);
    cout << "File is received successfully . " << endl << flush;
    return 0;
}