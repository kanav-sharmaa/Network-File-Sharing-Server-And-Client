#include <iostream>
#include <fstream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <vector>
#include <sstream>
#include <iomanip>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 4096
#define SHARED_DIR ".\\shared_files\\"

using namespace std;

string hashPassword(const string& password) {
    unsigned long hash = 5381;
    for (char c : password) {
        hash = ((hash << 5) + hash) + c;
    }
    
    stringstream ss;
    ss << hex << hash;
    return ss.str();
}

bool authenticateUser(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE] = {0};
    string validUser = "admin";
    string validPassHash = hashPassword("admin123");
    
    recv(clientSocket, buffer, BUFFER_SIZE, 0);
    string username(buffer);
    memset(buffer, 0, BUFFER_SIZE);
    
    recv(clientSocket, buffer, BUFFER_SIZE, 0);
    string password(buffer);
    string passHash = hashPassword(password);
    
    if(username == validUser && passHash == validPassHash) {
        string response = "AUTH_SUCCESS";
        send(clientSocket, response.c_str(), response.length(), 0);
        return true;
    } else {
        string response = "AUTH_FAILED";
        send(clientSocket, response.c_str(), response.length(), 0);
        return false;
    }
}

string listFiles() {
    string fileList;
    WIN32_FIND_DATAA findData;
    string searchPath = string(SHARED_DIR) + "*.*";
    HANDLE hFind = FindFirstFileA(searchPath.c_str(), &findData);
    
    if(hFind == INVALID_HANDLE_VALUE) {
        return "ERROR: Cannot open directory";
    }
    
    int fileCount = 0;
    do {
        if(!(findData.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
            fileCount++;
            fileList += to_string(fileCount) + ". " + string(findData.cFileName) + "\n";
        }
    } while(FindNextFileA(hFind, &findData) != 0);
    
    FindClose(hFind);
    
    if(fileCount == 0) {
        return "No files available in shared directory.";
    }
    
    return fileList;
}

void sendFile(SOCKET clientSocket, const string& filename) {
    string filepath = string(SHARED_DIR) + filename;
    ifstream file(filepath, ios::binary);
    
    if(!file.is_open()) {
        string error = "ERROR: File not found";
        send(clientSocket, error.c_str(), error.length(), 0);
        return;
    }
    
    file.seekg(0, ios::end);
    long fileSize = file.tellg();
    file.seekg(0, ios::beg);
    
    string sizeStr = to_string(fileSize);
    send(clientSocket, sizeStr.c_str(), sizeStr.length(), 0);
    
    char ack[10];
    recv(clientSocket, ack, sizeof(ack), 0);
    
    char buffer[BUFFER_SIZE];
    long totalSent = 0;
    
    while(totalSent < fileSize) {
        file.read(buffer, BUFFER_SIZE);
        long bytesRead = file.gcount();
        send(clientSocket, buffer, bytesRead, 0);
        totalSent += bytesRead;
    }
    
    file.close();
    cout << "File '" << filename << "' sent successfully (" << fileSize << " bytes)" << endl;
}

void receiveFile(SOCKET clientSocket) {
    char buffer[BUFFER_SIZE] = {0};
    
    recv(clientSocket, buffer, BUFFER_SIZE, 0);
    string filename(buffer);
    memset(buffer, 0, BUFFER_SIZE);
    
    string ack = "READY";
    send(clientSocket, ack.c_str(), ack.length(), 0);
    
    recv(clientSocket, buffer, BUFFER_SIZE, 0);
    long fileSize = stol(buffer);
    memset(buffer, 0, BUFFER_SIZE);
    
    send(clientSocket, ack.c_str(), ack.length(), 0);
    
    string filepath = string(SHARED_DIR) + filename;
    ofstream file(filepath, ios::binary);
    
    if(!file.is_open()) {
        cout << "ERROR: Cannot create file" << endl;
        return;
    }
    
    long totalReceived = 0;
    while(totalReceived < fileSize) {
        long bytesToReceive = min((long)BUFFER_SIZE, fileSize - totalReceived);
        long bytesReceived = recv(clientSocket, buffer, bytesToReceive, 0);
        
        if(bytesReceived <= 0) break;
        
        file.write(buffer, bytesReceived);
        totalReceived += bytesReceived;
    }
    
    file.close();
    cout << "File '" << filename << "' received successfully (" << totalReceived << " bytes)" << endl;
}

void handleClient(SOCKET clientSocket) {
    cout << "Client connected. Authenticating..." << endl;
    
    if(!authenticateUser(clientSocket)) {
        cout << "Authentication failed. Closing connection." << endl;
        closesocket(clientSocket);
        return;
    }
    
    cout << "Client authenticated successfully!" << endl;
    
    char buffer[BUFFER_SIZE] = {0};
    
    while(true) {
        memset(buffer, 0, BUFFER_SIZE);
        int bytesReceived = recv(clientSocket, buffer, BUFFER_SIZE, 0);
        
        if(bytesReceived <= 0) {
            cout << "Client disconnected." << endl;
            break;
        }
        
        string command(buffer);
        
        if(command == "LIST") {
            string fileList = listFiles();
            send(clientSocket, fileList.c_str(), fileList.length(), 0);
        }
        else if(command.substr(0, 8) == "DOWNLOAD") {
            string filename = command.substr(9);
            sendFile(clientSocket, filename);
        }
        else if(command == "UPLOAD") {
            receiveFile(clientSocket);
        }
        else if(command == "EXIT") {
            cout << "Client requested disconnection." << endl;
            break;
        }
        else {
            string response = "Unknown command";
            send(clientSocket, response.c_str(), response.length(), 0);
        }
    }
    
    closesocket(clientSocket);
}

int main() {
    CreateDirectoryA(SHARED_DIR, NULL);
    
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed" << endl;
        return 1;
    }
    
    SOCKET serverSocket, clientSocket;
    struct sockaddr_in serverAddr, clientAddr;
    int clientAddrLen = sizeof(clientAddr);
    
    serverSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(serverSocket == INVALID_SOCKET) {
        cerr << "Failed to create socket" << endl;
        WSACleanup();
        return 1;
    }
    
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    serverAddr.sin_port = htons(PORT);
    
    if(bind(serverSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Bind failed" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }
    
    if(listen(serverSocket, 5) == SOCKET_ERROR) {
        cerr << "Listen failed" << endl;
        closesocket(serverSocket);
        WSACleanup();
        return 1;
    }
    
    cout << "==================================" << endl;
    cout << "File Sharing Server Started" << endl;
    cout << "==================================" << endl;
    cout << "Port: " << PORT << endl;
    cout << "Shared Directory: " << SHARED_DIR << endl;
    cout << "Credentials: admin / admin123" << endl;
    cout << "Waiting for connections..." << endl;
    cout << "==================================" << endl;
    
    while(true) {
        clientSocket = accept(serverSocket, (struct sockaddr*)&clientAddr, &clientAddrLen);
        
        if(clientSocket == INVALID_SOCKET) {
            cerr << "Accept failed" << endl;
            continue;
        }
        
        handleClient(clientSocket);
    }
    
    closesocket(serverSocket);
    WSACleanup();
    return 0;
}