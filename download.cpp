#include <iostream>
#include <fstream>
#include <string>
#include <openssl/sha.h>
#include <sstream>
#include <iomanip>
#include <cstring>

#include <unistd.h>
#include <arpa/inet.h>
using namespace std;

std::string sha256(const string& input) {
    unsigned char hash[256];
    SHA256(reinterpret_cast<const unsigned char*>(input.c_str()),input.size(),hash);
    stringstream ss;
    for (int i=0;i<256;i++) ss<<hex<<setw(2)<<setfill('0')<<(int)hash[i];
    return ss.str();
}
std::string get_id(){
    std::ifstream f("/sys/class/dmi/id/product_uuid");
    std::string uuid;
    f>>uuid;
    uuid=sha256(uuid);
    return uuid;
}

int main(){
    std::string msg=get_id();
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0) {
        return 1;
    }
    sockaddr_in serverAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(8080);
    inet_pton(AF_INET, "[IP_ADDRESS]", &serverAddr.sin_addr);
    if (connect(sock,(sockaddr*)&serverAddr,sizeof(serverAddr)) < 0) {
        return 1;
    }
    send(sock,msg.c_str(),msg.size(),0);
    close(sock);
    return 0;
}
