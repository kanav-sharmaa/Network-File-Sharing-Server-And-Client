#include <iostream>
#include <fstream>
#include <string>
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>

#pragma comment(lib, "ws2_32.lib")

#define PORT 8080
#define BUFFER_SIZE 4096
#define DOWNLOAD_DIR ".\\downloads\\"

using namespace std;

bool authenticate(SOCKET socket) {
    string username, password;
    
    cout << "\n=== Authentication Required ===" << endl;
    cout << "Username: ";
    cin >> username;
    cout << "Password: ";
    cin >> password;
    
    send(socket, username.c_str(), username.length(), 0);
    send(socket, password.c_str(), password.length(), 0);
    
    char buffer[BUFFER_SIZE] = {0};
    recv(socket, buffer, BUFFER_SIZE, 0);
    
    string result(buffer);
    
    if(result == "AUTH_SUCCESS") {
        cout << "Authentication successful!" << endl;
        return true;
    } else {
        cout << "Authentication failed!" << endl;
        return false;
    }
}

void listFiles(SOCKET socket) {
    string command = "LIST";
    send(socket, command.c_str(), command.length(), 0);
    
    char buffer[BUFFER_SIZE] = {0};
    recv(socket, buffer, BUFFER_SIZE, 0);
    
    cout << "\n=== Available Files ===" << endl;
    cout << buffer << endl;
}

void downloadFile(SOCKET socket, const string& filename) {
    string command = "DOWNLOAD " + filename;
    send(socket, command.c_str(), command.length(), 0);
    
    char buffer[BUFFER_SIZE] = {0};
    
    recv(socket, buffer, BUFFER_SIZE, 0);
    string sizeStr(buffer);
    
    if(sizeStr.substr(0, 5) == "ERROR") {
        cout << sizeStr << endl;
        return;
    }
    
    long fileSize = stol(sizeStr);
    
    string ack = "ACK";
    send(socket, ack.c_str(), ack.length(), 0);
    
    CreateDirectoryA(DOWNLOAD_DIR, NULL);
    
    string filepath = string(DOWNLOAD_DIR) + filename;
    ofstream file(filepath, ios::binary);
    
    if(!file.is_open()) {
        cout << "ERROR: Cannot create file" << endl;
        return;
    }
    
    long totalReceived = 0;
    cout << "Downloading '" << filename << "' (" << fileSize << " bytes)..." << endl;
    
    while(totalReceived < fileSize) {
        memset(buffer, 0, BUFFER_SIZE);
        long bytesToReceive = min((long)BUFFER_SIZE, fileSize - totalReceived);
        long bytesReceived = recv(socket, buffer, bytesToReceive, 0);
        
        if(bytesReceived <= 0) break;
        
        file.write(buffer, bytesReceived);
        totalReceived += bytesReceived;
        
        int progress = (totalReceived * 100) / fileSize;
        cout << "\rProgress: " << progress << "%" << flush;
    }
    
    file.close();
    cout << "\nFile downloaded successfully to " << filepath << endl;
}

void uploadFile(SOCKET socket, const string& filepath) {
    ifstream file(filepath, ios::binary);
    
    if(!file.is_open()) {
        cout << "ERROR: Cannot open file '" << filepath << "'" << endl;
        return;
    }
    
    size_t pos = filepath.find_last_of("/\\");
    string filename = (pos == string::npos) ? filepath : filepath.substr(pos + 1);
    
    file.seekg(0, ios::end);
    long fileSize = file.tellg();
    file.seekg(0, ios::beg);
    
    string command = "UPLOAD";
    send(socket, command.c_str(), command.length(), 0);
    
    send(socket, filename.c_str(), filename.length(), 0);
    
    char buffer[BUFFER_SIZE] = {0};
    recv(socket, buffer, BUFFER_SIZE, 0);
    
    string sizeStr = to_string(fileSize);
    send(socket, sizeStr.c_str(), sizeStr.length(), 0);
    
    memset(buffer, 0, BUFFER_SIZE);
    recv(socket, buffer, BUFFER_SIZE, 0);
    
    long totalSent = 0;
    cout << "Uploading '" << filename << "' (" << fileSize << " bytes)..." << endl;
    
    while(totalSent < fileSize) {
        memset(buffer, 0, BUFFER_SIZE);
        file.read(buffer, BUFFER_SIZE);
        long bytesRead = file.gcount();
        send(socket, buffer, bytesRead, 0);
        totalSent += bytesRead;
        
        int progress = (totalSent * 100) / fileSize;
        cout << "\rProgress: " << progress << "%" << flush;
    }
    
    file.close();
    cout << "\nFile uploaded successfully!" << endl;
}

void displayMenu() {
    cout << "\n=== File Sharing Client Menu ===" << endl;
    cout << "1. List files" << endl;
    cout << "2. Download file" << endl;
    cout << "3. Upload file" << endl;
    cout << "4. Exit" << endl;
    cout << "Enter choice: ";
}

int main() {
    WSADATA wsaData;
    if(WSAStartup(MAKEWORD(2, 2), &wsaData) != 0) {
        cerr << "WSAStartup failed" << endl;
        return 1;
    }
    
    SOCKET clientSocket;
    struct sockaddr_in serverAddr;
    
    clientSocket = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if(clientSocket == INVALID_SOCKET) {
        cerr << "Failed to create socket" << endl;
        WSACleanup();
        return 1;
    }
    
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    inet_pton(AF_INET, "127.0.0.1", &serverAddr.sin_addr);
    
    cout << "Connecting to server..." << endl;
    if(connect(clientSocket, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) == SOCKET_ERROR) {
        cerr << "Connection failed. Make sure the server is running." << endl;
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }
    
    cout << "Connected to server!" << endl;
    
    if(!authenticate(clientSocket)) {
        closesocket(clientSocket);
        WSACleanup();
        return 1;
    }
    
    while(true) {
        displayMenu();
        
        int choice;
        cin >> choice;
        cin.ignore();
        
        switch(choice) {
            case 1:
                listFiles(clientSocket);
                break;
                
            case 2: {
                string filename;
                cout << "Enter filename to download: ";
                getline(cin, filename);
                downloadFile(clientSocket, filename);
                break;
            }
            
            case 3: {
                string filepath;
                cout << "Enter file path to upload: ";
                getline(cin, filepath);
                uploadFile(clientSocket, filepath);
                break;
            }
            
            case 4: {
                string command = "EXIT";
                send(clientSocket, command.c_str(), command.length(), 0);
                cout << "Disconnecting..." << endl;
                closesocket(clientSocket);
                WSACleanup();
                return 0;
            }
            
            default:
                cout << "Invalid choice. Please try again." << endl;
        }
    }
    
    closesocket(clientSocket);
    WSACleanup();
    return 0;
}