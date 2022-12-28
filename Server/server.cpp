#include <bits/stdc++.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <sys/types.h>
#include <chrono>

using namespace std;

static const int ACK_PACKET_SIZE = 8;
static const int PORT = 8000;
static const int MSS = 508;
static const int CHUNK_SIZE = 499;


struct packet {
    uint16_t cksum;
    uint16_t len;
    uint32_t seqno;
    char data [500];
};

struct not_sent_packet{
    int seqno;
    chrono::time_point<chrono::system_clock> timer;
    bool done;
};

struct ack_packet {
    uint16_t cksum;
    uint16_t len;
    uint32_t ackno;
};


// some global variables
enum fsm_state {slow_start, congestion_avoidance, fast_recovery};
int RandomSeedGen;
double PLP;
vector<packet> sent_packets;
vector<not_sent_packet> not_sent_packets;

// prototype of the functions
void handle_client_request(int server_socket, int client_socket, struct sockaddr_in client_addr, char rec_buffer [] , int bufferSize);
long getFileSize(string fileName);
vector<string> readFileData(string fileName);
void sendData_handleCongestion(int client_socket, struct sockaddr_in client_addr , vector<string> data);
bool send_packet(int client_socket, struct sockaddr_in client_addr , string temp_packet, int seqNum);
packet create_packet_data(string packetString, int seqNum);
bool datagramIsCorrupted();
uint16_t get_ack_checksum (uint16_t len , uint32_t ackno);
uint16_t get_data_checksum (string content, uint16_t len , uint32_t seqno);
void exitWithError(char * msg);

vector<string> readCommand(){
    string fileName = "command.txt";
    vector<string> commands;
    string line;
    ifstream f;
    f.open(fileName);
    while(getline(f, line))
    {
        commands.push_back(line);
    }
    return commands;
}

int main()
{
    vector<string> the_args = readCommand();
    int port = stoi(the_args[0]);
    RandomSeedGen = stoi(the_args[1]);
    PLP = stod(the_args[2]);
    //PLP = stod("0");
    srand(RandomSeedGen);
    int server_socket, client_socket;

    // create the server socket and check for errors
    if ((server_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) exitWithError((char *) "Error in creating server socket!");

    struct sockaddr_in server_address{};
    struct sockaddr_in client_address{};
    memset(&server_address, 0, sizeof(server_address));
    memset(&client_address, 0, sizeof(client_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(PORT);
    server_address.sin_addr.s_addr = INADDR_ANY;
    memset(&(server_address.sin_zero), '\0', ACK_PACKET_SIZE);

    // bind the address to the server socket
    if (bind(server_socket, (struct sockaddr *) &server_address, sizeof(server_address)) < 0) exitWithError((char *)"Error in binding server");
    
    // trying to receive from the client
    while (true){
        socklen_t client_addrlen = sizeof(struct sockaddr);
        cout << "Waiting For A New Connection ... " << endl << flush;
        char received_buffer[MSS];
        ssize_t received_bytes = recvfrom(server_socket, received_buffer, MSS, 0, (struct sockaddr*)&client_address, &client_addrlen);
        if (received_bytes <= 0) exitWithError((char *)"Error in receiving the bytes of the file name from client!");
        
        /** forking to handle request **/
        pid_t pid = fork();
        if (pid == -1){
        perror("Error in forking a child process to handle client request!");
        } 
        else if (pid == 0){

        // create the client socket and check for errors
        if ((client_socket = socket(PF_INET, SOCK_DGRAM, IPPROTO_UDP)) < 0) exitWithError((char *) "Error in creating client socket!");

        // if no errors then handle the client request
        handle_client_request(server_socket,client_socket, client_address, received_buffer , MSS);
            exit(0);
        }

    }
    close(server_socket);
    return 0;
}

// this function is used to handle the client request by check if the file is exist or not, if exist send it to the client and wait for acknowledgement
void handle_client_request(int server_socket, int client_socket, struct sockaddr_in client_addr, char rec_buffer [] , int bufferSize) {

    auto* rec_packet = (struct packet*) rec_buffer;
    string fileName = string(rec_packet->data);
    cout << "The file requested by client is : " << fileName << endl;
    int fileSize = getFileSize(fileName);
    if (fileSize == -1) return;

    int numberOfPackets = ceil(fileSize * 1.0 / CHUNK_SIZE);
    cout << "File Size : " << fileSize << " Bytes.\n" << "The file will be transfered in " << numberOfPackets << " packets." << endl << flush;

    /** send ack to file name **/
    struct ack_packet ack;
    ack.cksum = 0;
    ack.len = numberOfPackets;
    ack.ackno = 0;
    char* buf = new char[MSS];
    memset(buf, 0, MSS);
    memcpy(buf, &ack, sizeof(ack));

    /** send ack to the client and check for errors **/
    if((sendto(client_socket, buf, MSS, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr))) == -1) 
        exitWithError((char *) "Error in Sending The Ack!");
    else cout << "Ack of file name is sent successfully." << endl << flush;    

    /** read data from file **/
    vector<string> DataPackets = readFileData(fileName);

    /** start sending data and handling congestion control using the SM **/
    sendData_handleCongestion(client_socket, client_addr, DataPackets);

}

// check for file existence and return its size
long getFileSize(string fileName){
    ifstream file(fileName.c_str(), ifstream::ate | ifstream::binary);
    if (!file.is_open()) {
    cout << "File not exist." << endl << flush;
    return -1;
    }
    long size = file.tellg();
    file.close();
    return size;
}

// read the content of the file
vector<string> readFileData(string fileName){
    vector<string> DataPackets;
    string temp = "";
    ifstream fin;
    fin.open(fileName);
    if (fin){
        char c;
        int char_size = 0;
        while(fin.get(c)){
            if(char_size < CHUNK_SIZE) {
                temp += c;
                char_size++;
            }
            else{
                DataPackets.push_back(temp);
                temp.clear();
                temp += c;
                char_size = 1;
                continue;
            }
        }
        if (char_size > 0){
            DataPackets.push_back(temp);
        }
    }
    fin.close();
    return DataPackets;
}

void sendData_handleCongestion(int client_socket, struct sockaddr_in client_addr , vector<string> data){
    ofstream outfile;
    // File Open
    outfile.open("File_1.txt");

    int cwnd_base = 0;
    double cwnd = 1;

    // Write to the file
    outfile << cwnd << endl;
    int base_packet_number = 0;
    int sst = 128;
    bool flag = true;
    int seqNum = 0;
    long sentPacketsNotAcked = 0;
    fsm_state st = slow_start;
    long numberOfDupAcks = 0;
    int lastAckedSeqNum = -1;
    bool stillExistAcks = true;
    char rec_buf[MSS];
    socklen_t client_addrlen = sizeof(struct sockaddr);
    int alreadySentPackets = 0;
    int totalPackets = data.size();

    while (flag){

        /**
        this part will run first to send first datagram as stated in pdf.
        **/
        while(cwnd_base < cwnd && alreadySentPackets + not_sent_packets.size() < totalPackets){
            seqNum = base_packet_number + cwnd_base;
            string temp_packet = data[seqNum];
            /**
                in case error simulated won't send the packet so the seqnumber will not correct at the receiver so will send duplicate ack.
            **/
            bool isSent = send_packet(client_socket, client_addr, temp_packet,seqNum);
            if (isSent == false) {
                cout << "Data is corrupted,unable to send the packet!\n";
            } else {
                sentPacketsNotAcked++;
                alreadySentPackets++;
                cout << "Seq Num of current packet : " << seqNum << endl << flush;
            }
            cwnd_base++;
        }

        /*** receiving ACKs ***/
        if (sentPacketsNotAcked > 0){
            stillExistAcks = true;
            while (stillExistAcks){
                cout << "waiting for ack... " << endl << flush;
                ssize_t Received_bytes = recvfrom(client_socket, rec_buf, ACK_PACKET_SIZE, 0, (struct sockaddr*)&client_addr, &client_addrlen);
                if (Received_bytes < 0) exitWithError((char *) "Error in receiving ack bytes!");
                else if (Received_bytes != ACK_PACKET_SIZE) exitWithError((char *) "Expecting Ack Got Something Else!");
                else {
                    auto ack = (ack_packet*) malloc(sizeof(ack_packet));
                    memcpy(ack, rec_buf, ACK_PACKET_SIZE);
                    cout << "Ack. " << ack->ackno << " Received." << endl << flush;

                    if (get_ack_checksum(ack->len, ack->ackno) != ack->cksum){
                        cout << "Corrupt Ack. received" << endl << flush ;
                    }

                    int ack_seqno = ack->ackno;
                    if (lastAckedSeqNum == ack_seqno){

                        numberOfDupAcks++;
                        sentPacketsNotAcked--;
                        if (st == fast_recovery){
                            cwnd++;
                            
                            // Write to the file
                            outfile << cwnd << endl;
                        } else if (numberOfDupAcks == 3){
                            sst = cwnd / 2;
                            cwnd = sst + 3;
                            cout << "//////////////////////////////////////////////// Triple duplicate Ack ////////////////////////////////////////////////" << endl;
                            // Write to the file
                            outfile << cwnd << endl;
                            st = fast_recovery;
                            /** retransmit the lost packet **/
                            seqNum = ack_seqno;
                            bool isFound = false;
                            for (int j = 0; j < not_sent_packets.size() ;j++){
                                not_sent_packet nspkt = not_sent_packets[j];
                                if (nspkt.seqno == seqNum){
                                    isFound = true;
                                    string temp_packet = data[seqNum];
                                    struct packet data_packet = create_packet_data(temp_packet, seqNum);
                                    char sendBuffer [MSS];
                                    memset(sendBuffer, 0, MSS);
                                    memcpy(sendBuffer, &data_packet, sizeof(data_packet));
                                    ssize_t bytesSent = sendto(client_socket, sendBuffer, MSS, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
                                    if (bytesSent == -1) exitWithError((char *) "Error in re-sending data packet!");
                                    else {
                                        sentPacketsNotAcked++;
                                        alreadySentPackets++;
                                        not_sent_packets.erase(not_sent_packets.begin() + j);
                                    }
                                    break;
                                }
                            }

                            /** handle checksum error **/
                            if (!isFound){
                                for (int j = 0; j < sent_packets.size() ;j++){
                                    packet spkt = sent_packets[j];
                                    if (spkt.seqno == seqNum){
                                        isFound = true;
                                        string temp_packet = data[seqNum];
                                        struct packet data_packet = create_packet_data(temp_packet, seqNum);
                                        char sendBuffer [MSS];
                                        memset(sendBuffer, 0, MSS);
                                        memcpy(sendBuffer, &data_packet, sizeof(data_packet));
                                        ssize_t bytesSent = sendto(client_socket, sendBuffer, MSS, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
                                        if (bytesSent == -1) exitWithError((char *) "Error in re-sending data packet!");
                                        else {
                                            alreadySentPackets++;
                                            sent_packets.erase(sent_packets.begin() + j);
                                        }
                                        break;
                                    }
                                }

                            }

                        }

                    } else if (lastAckedSeqNum < ack_seqno) {
                        /** new ack : compute new base and packet no. and handling congestion control FSM **/
                        // cout << "newAck " << endl;
                        numberOfDupAcks = 0;
                        lastAckedSeqNum = ack_seqno;
                        int advance = lastAckedSeqNum - base_packet_number;
                        cwnd_base = cwnd_base - advance;
                        base_packet_number = lastAckedSeqNum;
                        if (st == slow_start){
                           if (cwnd*2 >= sst){
                                st = congestion_avoidance;
                                cwnd++;
                           }
                           else{
                               cwnd=cwnd*2;
                           }
                           // Write to the file
                           outfile << cwnd << endl;
                           if (cwnd >= sst){
                                st = congestion_avoidance;
                           }
                        } else if (st == congestion_avoidance){
                            cwnd ++;
                            // Write to the file
                            outfile << cwnd << endl;
                        } else if (st == fast_recovery){
                            st = congestion_avoidance;
                            cwnd = sst;
                            // Write to the file
                            outfile << cwnd << endl;
                        }
                        sentPacketsNotAcked--;
                    } else {
                        sentPacketsNotAcked--;
                    }

                    if (sentPacketsNotAcked == 0){
                        stillExistAcks = false;
                    }

                }

            }

        }


        /** Handle Time Out **/
        bool isTimedOut = false;
        for (int j = 0; j < not_sent_packets.size() ;j++){
            not_sent_packet nspkt = not_sent_packets[j];
            chrono::time_point<chrono::system_clock> current_time = chrono::system_clock::now();
            chrono::duration<double> elapsed_time = current_time - nspkt.timer;
            if (elapsed_time.count() >= 5){
                isTimedOut = true;
                
                cout << "Timed Out!" << endl << flush;
                cout << "Re-transmitting the packet..." << endl << flush;
                seqNum = nspkt.seqno;
                string temp_packet = data[seqNum];
                struct packet data_packet = create_packet_data(temp_packet, seqNum);
                char sendBuffer [MSS];
                memset(sendBuffer, 0, MSS);
                memcpy(sendBuffer, &data_packet, sizeof(data_packet));
                ssize_t bytesSent = sendto(client_socket, sendBuffer, MSS, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr));
                if (bytesSent == -1) exitWithError((char *) "Error in resending the data packet!");
                else {
                    sentPacketsNotAcked++;
                    alreadySentPackets++;
                    not_sent_packets.erase(not_sent_packets.begin() + j);
                    j--;
                    cout << "Seq Num of current packet : " << seqNum << endl << flush;
                }
            }
        }
        if(isTimedOut){
            isTimedOut=false;
            cwnd=1;
            st = slow_start;
            // Write to the file
            outfile << cwnd << endl;
        }
    }
    // File Close
    outfile.close();
}

// send packets to the client 
bool send_packet(int client_socket, struct sockaddr_in client_addr , string temp_packet, int seqNum){
    char sendBuffer [MSS];
    struct packet data_packet = create_packet_data(temp_packet, seqNum);
    bool isCorrupted = datagramIsCorrupted();
    if(isCorrupted){
        data_packet.cksum=data_packet.cksum-1;
    }
    memset(sendBuffer, 0, MSS);
    memcpy(sendBuffer, &data_packet, sizeof(data_packet));

    if (!isCorrupted){

        if((sendto(client_socket, sendBuffer, MSS, 0, (struct sockaddr *)&client_addr, sizeof(struct sockaddr))) == -1) return false;
        else {
            sent_packets.push_back(data_packet);
            return true;
        }
    } 
    else {
        struct not_sent_packet nspacket{};
        nspacket.seqno = seqNum;
        nspacket.done = false;
        nspacket.timer = chrono::system_clock::now();
        not_sent_packets.push_back(nspacket);
        return false;
    }
}

// create packet to send it to the client
packet create_packet_data(string packetString, int seqNum) {
    struct packet p{};
    memset(p.data,0,500);
    strcpy(p.data, packetString.c_str());
    p.seqno = seqNum;
    p.len = packetString.size();
    p.cksum = get_data_checksum(packetString,p.len,p.seqno);
    return p;
}

// check if the datagram is corrupted or not
bool datagramIsCorrupted(){
    int res = rand() % 100;
    double isLost = res * PLP;
    if (isLost >= 5){
        return true;
    }
    return false;
}

// return the checksum in the ack message
uint16_t get_ack_checksum (uint16_t len , uint32_t ackno){
    uint32_t sum = 0;
    sum += len;
    sum += ackno;
    while (sum >> 16){
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t OCSum = (uint16_t) (~sum);
    return OCSum;
}

// return the checksum in the data
uint16_t get_data_checksum (string content, uint16_t len , uint32_t seqno){
    uint32_t sum = 0;
    sum += len;
    sum += seqno;
    int n = content.length();
    char arr[n+1];
    strcpy(arr, content.c_str());
    for (int i = 0; i < n; i++){
        sum += arr[i];
    }
    while (sum >> 16){
        sum = (sum & 0xFFFF) + (sum >> 16);
    }
    uint16_t OCSum = (uint16_t) (~sum);
    return OCSum;
}


// display error message and exit
void exitWithError(char * msg){
    perror(msg);
    exit(EXIT_FAILURE);
}