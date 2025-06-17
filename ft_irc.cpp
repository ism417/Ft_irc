#include <iostream>
#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <cstring>
#include <poll.h>
#include <vector>
#include <string>
#include <sstream>
#include <map>
#include <cctype> 
#include <set>

struct Channel {
    std::string name;
    std::set<int> members; // client file descriptors
    
    Channel () : name(""){}
    Channel(const std::string& channel_name) : name(channel_name) {}
};

struct Client {
    int fd;
    std::string nickname;
    bool authenticated;
    bool has_password;
    bool has_nickname;
    
    Client() : fd(-1), authenticated(false), has_password(false), has_nickname(false) {}
    Client(int socket_fd) : fd(socket_fd), authenticated(false), has_password(false), has_nickname(false) {}
};

std::map<int, Client> clients;
const std::string SERVER_PASSWORD = "mypass123"; // Set your server password

void sendMessage(int client_fd, const std::string& message) {
    std::string msg = message + "\r\n";
    send(client_fd, msg.c_str(), msg.length(), 0);
}
std::map<std::string, Channel> channels;
void sendToChannel(const std::string& channel_name, const std::string& message, int sender_fd = -1) {
    if (channels.find(channel_name) != channels.end()) {
        for (int client_fd : channels[channel_name].members) {
            if (client_fd != sender_fd) { // Don't send back to sender
                sendMessage(client_fd, message);
            }
        }
    }
}

void processCommand(int client_fd, const std::string& command) {
     // Check if client exists in map
    if (clients.find(client_fd) == clients.end()) {
        std::cerr << "Client " << client_fd << " not found in clients map" << std::endl;
        return;
    }
    
    std::istringstream iss(command);
    std::string cmd;
    iss >> cmd;
    
    // Convert command to uppercase
    for (char& c : cmd) {
        c = std::toupper(c);
    }
    if (cmd == "PASS") {
        std::string password;
        iss >> password;
        
        if (password == SERVER_PASSWORD) {
            clients[client_fd].has_password = true;
            std::cout << "Client " << client_fd << " provided correct password" << std::endl;
        } else {
            sendMessage(client_fd, ":server 464 * :Password incorrect");
            std::cout << "Client " << client_fd << " provided incorrect password" << std::endl;
        }
    }
    else if (cmd == "NICK") {
        std::string nickname;
        iss >> nickname;
        
        if (nickname.empty()) {
            sendMessage(client_fd, ":server 431 * :No nickname given");
            return;
        }
        
        // Check if nickname is already in use
        for (const auto& pair : clients) {
            if (pair.second.nickname == nickname && pair.first != client_fd) {
                sendMessage(client_fd, ":server 433 * " + nickname + " :Nickname is already in use");
                return;
            }
        }
        
        clients[client_fd].nickname = nickname;
        clients[client_fd].has_nickname = true;
        std::cout << "Client " << client_fd << " set nickname to: " << nickname << std::endl;
        
        // Check if client is now fully authenticated
        if (clients[client_fd].has_password && clients[client_fd].has_nickname) {
            clients[client_fd].authenticated = true;
            sendMessage(client_fd, ":server 001 " + nickname + " :Welcome to the IRC server!");
            std::cout << "Client " << client_fd << " (" << nickname << ") is now authenticated" << std::endl;
        }
    }
    else if (cmd == "USER") {
        // Basic USER command handling (required by IRC protocol)
        if (clients[client_fd].has_password && clients[client_fd].has_nickname) {
            clients[client_fd].authenticated = true;
            sendMessage(client_fd, ":server 001 " + clients[client_fd].nickname + " :Welcome to the IRC server!");
        }
    }
    else if (cmd == "JOIN") {
    if (!clients[client_fd].authenticated) {
        sendMessage(client_fd, ":server 451 * :You have not registered");
        return;
    }
    
    std::string channel_name;
    iss >> channel_name;
    
    if (channel_name.empty() || channel_name[0] != '#') {
        sendMessage(client_fd, ":server 403 " + clients[client_fd].nickname + " " + channel_name + " :No such channel");
        return;
    }
    
    // Create channel if it doesn't exist
    if (channels.find(channel_name) == channels.end()) {
        channels.emplace(channel_name, Channel(channel_name));
        std::cout << "Created new channel: " << channel_name << std::endl;
    }
    
    // Check if client is already in channel
    if (channels[channel_name].members.find(client_fd) != channels[channel_name].members.end()) {
        return; // Already in channel
    }
    
    // Add client to channel
    channels[channel_name].members.insert(client_fd);
    
    std::cout << "Client " << clients[client_fd].nickname << " joined channel " << channel_name << std::endl;
    
    // Send JOIN confirmation to all channel members
    std::string join_msg = ":" + clients[client_fd].nickname + " JOIN " + channel_name;
    sendToChannel(channel_name, join_msg, -1); // Send to all including sender
    sendMessage(client_fd, join_msg);
    
    // Send channel topic (if any) and member list
    sendMessage(client_fd, ":server 332 " + clients[client_fd].nickname + " " + channel_name + " :Welcome to " + channel_name);
    
    // Send names list
    std::string names_list = ":server 353 " + clients[client_fd].nickname + " = " + channel_name + " :";
    for (int member_fd : channels[channel_name].members) {
        if (clients.find(member_fd) != clients.end()) {
            names_list += clients[member_fd].nickname + " ";
        }
    }
    sendMessage(client_fd, names_list);
    sendMessage(client_fd, ":server 366 " + clients[client_fd].nickname + " " + channel_name + " :End of /NAMES list");
}
    else if (cmd == "QUIT") {
        std::cout << "Client " << client_fd << " is quitting" << std::endl;
        // Client will be removed in the main loop
    }
    else {
        if (!clients[client_fd].authenticated) {
            sendMessage(client_fd, ":server 451 * :You have not registered");
        } else {
            // Handle other IRC commands here
            std::cout << "Received command from " << clients[client_fd].nickname << ": " << command << std::endl;
        }
    }
}

int main(int ac, char **av){
    // Create a socket
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd == -1) {
        std::cerr << "Failed to create socket" << std::endl;
        return 1;
    }
    
    std::cout << "Socket created successfully with fd: " << server_fd << std::endl;
    
    // Set up server address structure
    struct sockaddr_in server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_addr.s_addr = INADDR_ANY;
    server_addr.sin_port = htons(6667);
    
    // Bind the socket to the address and port
    if (bind(server_fd, (struct sockaddr*)&server_addr, sizeof(server_addr)) == -1) {
        std::cerr << "Failed to bind socket" << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Socket bound to port 6667" << std::endl;
    
    // Listen for incoming connections
    if (listen(server_fd, 10) == -1) {
        std::cerr << "Failed to listen on socket" << std::endl;
        close(server_fd);
        return 1;
    }
    
    std::cout << "Server listening for connections..." << std::endl;
    
    // Set up poll for monitoring multiple file descriptors
    std::vector<struct pollfd> poll_fds;
    
    // Add server socket to poll
    struct pollfd server_poll_fd;
    server_poll_fd.fd = server_fd;
    server_poll_fd.events = POLLIN;
    server_poll_fd.revents = 0;
    poll_fds.push_back(server_poll_fd);
    
    // Main server loop
    while (true) {
        // Wait for events on any of the monitored file descriptors
        int poll_result = poll(poll_fds.data(), poll_fds.size(), -1);
        
        if (poll_result == -1) {
            std::cerr << "Poll failed" << std::endl;
            break;
        }
        
        // Check each file descriptor for events
        for (size_t i = 0; i < poll_fds.size(); ++i) {
            if (poll_fds[i].revents & POLLIN) {
                if (poll_fds[i].fd == server_fd) {
                    // New connection on server socket
                    struct sockaddr_in client_addr;
                    socklen_t client_len = sizeof(client_addr);
                    int client_fd = accept(server_fd, (struct sockaddr*)&client_addr, &client_len);
                    
                    if (client_fd != -1) {
                        std::cout << "New client connected: " << client_fd << std::endl;
                        
                        // Create new client and add to map
                        clients[client_fd] = Client(client_fd);
                        
                        // Add new client to poll
                        struct pollfd client_poll_fd;
                        client_poll_fd.fd = client_fd;
                        client_poll_fd.events = POLLIN;
                        client_poll_fd.revents = 0;
                        poll_fds.push_back(client_poll_fd);
                        
                        // Send welcome message
                        sendMessage(client_fd, ":server NOTICE * :Please authenticate with PASS <password> and NICK <nickname>");
                    }
                } else {
                    // Data available from existing client
                    char buffer[1024];
                    ssize_t bytes_read = recv(poll_fds[i].fd, buffer, sizeof(buffer) - 1, 0);
                    
                    if (bytes_read <= 0) {
                        // Client disconnected
                        std::cout << "Client disconnected: " << poll_fds[i].fd << std::endl;
                        clients.erase(poll_fds[i].fd);
                        close(poll_fds[i].fd);
                        poll_fds.erase(poll_fds.begin() + i);
                        --i;
                    } else {
                        // Process client data
                        buffer[bytes_read] = '\0';
                        std::string data(buffer);
                        
                        // Split by lines (IRC commands end with \r\n)
                        size_t pos = 0;
                        while ((pos = data.find('\n')) != std::string::npos) {
                            std::string line = data.substr(0, pos);
                            if (!line.empty() && line.back() == '\r') {
                                line.pop_back();
                            }
                            
                            if (!line.empty()) {
                                processCommand(poll_fds[i].fd, line);
                            }
                            
                            data.erase(0, pos + 1);
                        }
                    }
                }
            }
        }
    }
    
    // Clean up
    for (const auto& pfd : poll_fds) {
        close(pfd.fd);
    }
    
    return 0;
}