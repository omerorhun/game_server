#include <iostream>

#include <sys/types.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <netdb.h>
#include <unistd.h>

#include <thread>
#include <mutex>

#include "Server.h"
#include "Requests.h"

using namespace std;

Server *Server::p_instance = NULL;

Server::Server(void) {
    p_instance = this;
    FD_ZERO(&_ready_sockets);
}

int Server::init_server() {
    int result = -1;
    
    // open a socket
    if ((_main_socket = socket(AF_INET, SOCK_STREAM, 0)) == -1) {
        cerr << "Can't create a socket!";
        return result;
    }
    
    // bind the socket
    sockaddr_in server_addr;
    server_addr.sin_family = AF_INET;
    inet_pton(AF_INET, "0.0.0.0", &server_addr.sin_addr);
    server_addr.sin_port = htons(SERVER_PORT);
    
    if (bind(_main_socket, (const sockaddr *)&server_addr, sizeof(server_addr)) == -1) {
        cerr << "Can't bind to IP/port" << endl;
        return result;
    }
    
    // listen the socket
    if ((result = listen(_main_socket, SOMAXCONN)) == -1) {
        cerr << "Can't listen!" << endl;
        return result;
    }
    
    // add main listening socket to the set
    FD_SET(_main_socket, &_ready_sockets);
    
    return result;
}

int Server::wait_clients() {
    int result = -1;
    
    cout << "Waiting for clients.." << endl;
    
    std::vector<std::thread> thread_vector;
    fd_set temp_set;
    for (;;) {
        
        temp_set = _ready_sockets;
        int ret = select(FD_SETSIZE, &temp_set, NULL, NULL, NULL);
        
        if (ret == 0) {
            printf("select timeout\n");
            continue;
        }
        else if (ret == -1) {
            printf("select error\n");
            continue;
        }
        else {
            for (int conn = 0; conn < FD_SETSIZE; conn++) {
                if (FD_ISSET(conn, &temp_set)) {
                    printf("conn %d\n", conn);
                    if (conn == _main_socket) {
                        // new connection
                        add_new_connection(&_ready_sockets);
                    }
                    else {
                        // existing connection, new request
                        printf("new request from %d\n", conn);
                        handle_client(conn);
                        //thread_vector.emplace_back(thread(&Server::handle_client, this, conn));
                    }
                }
            }
        }
    }
    
    return result;
}

ErrorCodes Server::add_new_connection(fd_set *p_set) {
    int client_socket;
    sockaddr_in client_addr;
    socklen_t client_len = sizeof(client_addr);
    
    if ((client_socket = accept(_main_socket, (sockaddr *)&client_addr, &client_len)) == -1) {
        // acception error
        cerr << "Error on accept\n";
        close(client_socket);
        return ERR_SRV_ACCEPT_CONN;
    }
    
    // add client socket to the set
    FD_SET(client_socket, p_set);
    
    print_client_status(client_addr);
    
    return ERR_SUCCESS;
}

#include <string.h>
void Server::handle_client(int sock) {
    Requests request(sock);
    
    printf("handle_client: %d\n", sock);
    
    ErrorCodes err = request.handle_request();
    if (err == ERR_REQ_DISCONNECTED) {
        // Close socket
        close(sock);
        
        // Remove socket from the set
        FD_CLR(sock, &_ready_sockets);
    }
    
#if 0
    // TODO: add timeout here
    recv(sock, NULL, 0, 0);
    close(sock);
    
    printf("socket closed\n");
#endif
}

Server *Server::get_instance() {
    return p_instance;
}

mutex mtx;

void Server::add_messagebox(int sesion_id) {
    vector<string> subqueue;
    int size = message_queue.size();
    
    cout << "add messagebox for " << sesion_id << endl;
    
    mtx.lock();
    message_queue.insert(pair<int,vector<string> >(sesion_id, vector<string>()));
    mtx.unlock();
    
    if (size == message_queue.size())
        cout << "error on adding user\n";
}

void Server::add_message_by_id(int sesion_id, string msg) {
    if (message_queue.find(sesion_id) == message_queue.end()) {
        return;
    }
    
    message_queue.at(sesion_id).push_back(msg);
}

string Server::get_message_by_id(int sesion_id) {
    string msg("empty");
    
    if (message_queue.find(sesion_id) == message_queue.end()) {
        return msg;
    }
    
    vector<string> *p_user = &message_queue.at(sesion_id);
    
    if (message_queue.at(sesion_id).size() > 0) {
        msg = (*p_user).front();
        p_user->erase(p_user->begin());
    }
    
    return msg;
}

int Server::check_for_messages(int session_id) {
    int size = 0;
    
    try {
        mtx.lock();
        size = message_queue.at(session_id).size();
        mtx.unlock();
        
        if (size > 0)
            printf("%d messages for user%d\n", size, session_id);
    }
    catch (const out_of_range& oor) {
        cout << "queue size: " << message_queue.size() << endl;
        cout << "id: " << session_id << endl;
        cout << "out of range " << oor.what() << endl;
        cout << "-------------------------------------------------------------------" << endl;
    }
    
    return size;
}

vector<int> Server::get_online_clients() {
    return online_clients;
}

#include <algorithm>

bool Server::is_client_online(int uid) {
    auto it = find(online_clients.begin(), online_clients.end(), uid);
    if (it == online_clients.end())
        return false;
    
    return true;
}

mutex mtx_waiting;

int Server::login(int uid) {
    vector<int>::iterator it;
    
    // check if client logged in
    if (lookup(uid, &it)) {
        printf("already logged in\n");
        return 0;
    }
    
    // add client
    mtx_waiting.lock();
    online_clients.push_back(uid);
    mtx_waiting.unlock();
    
    return 1;
}

int Server::logout(int uid) {
    vector<int>::iterator it;
    if (!lookup(uid, &it))
        return 0;
    
    online_clients.erase(it);
    
    return 1;
}

bool Server::lookup(int uid, vector<int>::iterator *it) {
    *it = find(online_clients.begin(), online_clients.end(), uid);
    if (*it == online_clients.end())
        return false;
    
    return true;
}

#include <string.h>
void Server::print_client_status(sockaddr_in client) {
    char host[NI_MAXHOST];
    char svc[NI_MAXSERV];
    
    memset(&host, 0, NI_MAXHOST);
    memset(&svc, 0, NI_MAXSERV);
    
    int result = getnameinfo((const sockaddr *)&client, sizeof(client), 
                                            host, NI_MAXHOST, svc, NI_MAXSERV, 0);
    
    if (result) {
        cout << string(host) << " connected to " << string(svc) << endl;
    }
    else {
        inet_ntop(AF_INET, &client.sin_addr, host, NI_MAXHOST);
        cout << string(host) << " connected to " << ntohs(client.sin_port) << endl;
    }
    
    return;
}
