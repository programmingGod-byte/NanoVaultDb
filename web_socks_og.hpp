#pragma once
#include<iostream>
#include<string>
#include<cstring>     
#include<netdb.h>   
#include<unistd.h> 
#include<openssl/sha.h>
#include<openssl/bio.h>
#include<openssl/evp.h>
#include<openssl/buffer.h>
#include<netinet/tcp.h>
#include<sys/epoll.h>
#include<fcntl.h> 
#include<vector>
#include<algorithm>
#include "global.hpp"
#include <stop_token>
#include "utils/cpu_affinity.hpp"

#define PORT "6969"
std::vector<int>clients;
unsigned char bit1=0b10000001;
unsigned char bit2=0b0001100;
unsigned char final[14]={bit1,bit2,'0','0','0','0','0','0','0','0','T','r','u','e'};     

std::string base64_encode(const unsigned char* input, int length) {
    BIO *bmem, *b64;
    BUF_MEM *bptr;

    b64 = BIO_new(BIO_f_base64());
    BIO_set_flags(b64, BIO_FLAGS_BASE64_NO_NL);

    bmem = BIO_new(BIO_s_mem());
    b64 = BIO_push(b64, bmem);

    BIO_write(b64, input, length);
    BIO_flush(b64);
    BIO_get_mem_ptr(b64, &bptr);

    std::string result(bptr->data, bptr->length);
    BIO_free_all(b64);

    return result;
}

bool do_handshake(int new_fd){
    char buffer[2048];
    int bytes=recv(new_fd,buffer,2047,0);
    if(bytes<=0) return false;
    buffer[bytes]='\0';
    std::string req(buffer);
    std::string swk;
    std::string tp1 = "Sec-WebSocket-Key: ";

    size_t p = req.find(tp1);
    if (p != std::string::npos) {
        size_t start = p + tp1.length();
            size_t end = req.find("\r\n", start);
            swk = req.substr(start, end - start);
    } else {
        return false;
    }
    // std::cout<<"Sec-WebSocket-Key: "<<swk<<std::endl;
    std::string GUID="258EAFA5-E914-47DA-95CA-C5AB0DC85B11";
    std::string tp=swk+GUID;
    unsigned char hash[SHA_DIGEST_LENGTH];
    SHA1((unsigned char*)tp.c_str(),tp.length(),hash);
    std::string acceptance_key=base64_encode(hash,SHA_DIGEST_LENGTH);
    std::string r="HTTP/1.1 101 Switching Protocols\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Accept: " + acceptance_key + "\r\n\r\n";

    if(send(new_fd,r.c_str(),r.length(),0)==-1){
        perror("send");
        std::cout<<"send failed";
        return false;
    }
    return true;
}

void init_web_sockets(std::stop_token st, int cpu_id){
    pin_thread_to_cpu(cpu_id);
    struct sockaddr_storage their_addr;
    socklen_t addr_size;
    struct addrinfo hints,*res;
    int sockfd,new_fd;
    memset(&hints,0,sizeof hints);
    hints.ai_family=AF_UNSPEC;
    hints.ai_socktype=SOCK_STREAM;
    hints.ai_flags=AI_PASSIVE;

    if (getaddrinfo(NULL,PORT,&hints,&res)!=0){
        perror("getaddrinfo");
        return;
    }
    sockfd=socket(res->ai_family,res->ai_socktype,res->ai_protocol);
    if (sockfd==-1){
        perror("socket");
        return;
    }
    if(bind(sockfd,res->ai_addr,res->ai_addrlen)==-1){
        perror("bind");
        return;
    }
    listen(sockfd,10);
    fcntl(sockfd,F_SETFL,O_NONBLOCK);

    int epollfd=epoll_create1(0);
    struct epoll_event e,events[100];

    e.events=EPOLLIN;
    e.data.fd=sockfd;
    epoll_ctl(epollfd,EPOLL_CTL_ADD,sockfd,&e);

    while(!st.stop_requested()){
        int n=epoll_wait(epollfd,events,100,100);
        
        for (int i=0;i<n;i++){
            int fd=events[i].data.fd;
            if (fd==sockfd){
                int newfd=accept(sockfd,NULL,NULL);
                if (newfd==-1) continue;            
                if (!do_handshake(newfd)) {
                    close(newfd);
                    continue;
                }
                fcntl(newfd,F_SETFL,O_NONBLOCK);
                int flag=1;
                setsockopt(newfd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag));
                
                e.events=EPOLLIN;
                e.data.fd=newfd;
                epoll_ctl(epollfd,EPOLL_CTL_ADD,newfd,&e);
                clients.push_back(newfd);
                
            }else{
                char tmp[512];
                int r=recv(fd,tmp,sizeof(tmp),MSG_DONTWAIT);
                if (r<=0){
                    close(fd);
                    clients.erase(std::remove(clients.begin(), clients.end(), fd),clients.end());
                }
            }
        }
        web_socket_Packet pkt;
        if (web_socket_queue.pop(pkt)){
            #pragma GCC unroll 4
            for (int i=0;i<4;i++){
                final[5-i]='0'+pkt.symbol%10;
                pkt.symbol/=10;
            }
            #pragma GCC unroll 4
            for (int i=0;i<4;i++){
                final[9-i]='0'+pkt.strategyIndex%10;
                pkt.strategyIndex/=10;
            }
            for (int newfd:clients){
                if (send(newfd,final,14,0)<=0){
                    close(newfd);
                }
            }
        }
    }
    // char*msg="True";
    // int len=strlen(msg);
    // if (send(new_fd,msg,len,0)==-1){
    //     perror("send");
    //     return 0;
    // }

}