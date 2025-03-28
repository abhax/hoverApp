/**
 * @file bluetooth_spp.cpp
 * @brief Bluetooth SPP (Serial Port Profile) implementation using BlueZ sockets without DBus
 */

#include <iostream>
#include <cstring>
#include <unistd.h>
#include <fcntl.h>
#include <sys/socket.h>
#include <sys/ioctl.h>
#include <sys/poll.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <bluetooth/sdp.h>
#include <bluetooth/sdp_lib.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>
#include <errno.h>
#include <vector>
#include <thread>
#include <atomic>
#include <chrono>
#include <string>

class BluetoothSPP {
private:
    int server_sock = -1;
    int client_sock = -1;
    std::thread connection_thread;
    std::atomic<bool> running{false};
    std::string service_name = "HoverApp SPP";
    uint8_t channel = 1;  // RFCOMM channel
    uuid_t spp_uuid;

    // SDP service record handle
    sdp_session_t *sdp_session = nullptr;
    sdp_record_t *record = nullptr;

    // Register SPP service with SDP (Service Discovery Protocol)
    int register_service() {
        printf("Starting register_service function\n");
        int result = -1;  // Initialize result variable
        // Initialize all variables at the start
        bdaddr_t src_addr = {{0, 0, 0, 0, 0, 0}}; // BDADDR_ANY
        bdaddr_t dst_addr = {{0, 0, 0, 0xff, 0xff, 0xff}}; // BDADDR_LOCAL
        sdp_session_t *sdp_session = nullptr;
        sdp_record_t *record = nullptr;
        sdp_list_t *browse_list = nullptr;
        sdp_list_t *service_class_list = nullptr;
        sdp_list_t *protocol_list[2] = {nullptr, nullptr};
        sdp_list_t *rfcomm_list = nullptr;
        sdp_data_t *channel_data = nullptr;
        sdp_list_t *apseq = nullptr;
        sdp_list_t *aproto = nullptr;
        std::string eir_hex;
        std::string name_hex;
        std::string hciconfig_cmd;
        printf("Variables initialized\n");
        
        printf("Attempting to connect to SDP server...\n");
        sdp_session = sdp_connect(&src_addr, &dst_addr, 0);
        if (!sdp_session) {
            printf("Failed to connect to SDP server: %s\n", strerror(errno));
            goto cleanup;
        }
        printf("SDP server connected successfully\n");

        printf("Allocating service record...\n");
        record = sdp_record_alloc();
        printf("Service record allocated\n");

        printf("Setting service ID...\n");
        sdp_uuid16_create(&spp_uuid, SERIAL_PORT_SVCLASS_ID);
        sdp_set_service_id(record, spp_uuid);
        printf("Service ID set\n");

        printf("Setting browse groups...\n");
        sdp_uuid16_create(&spp_uuid, PUBLIC_BROWSE_GROUP);
        browse_list = sdp_list_append(nullptr, &spp_uuid);
        sdp_set_browse_groups(record, browse_list);
        printf("Browse groups set\n");

        printf("Setting service class ID list...\n");
        sdp_uuid16_create(&spp_uuid, SERIAL_PORT_SVCLASS_ID);
        service_class_list = sdp_list_append(nullptr, &spp_uuid);
        sdp_set_service_classes(record, service_class_list);
        printf("Service class ID list set\n");

        printf("Setting protocol information...\n");

        printf("Adding L2CAP protocol...\n");
        sdp_uuid16_create(&spp_uuid, L2CAP_UUID);
        protocol_list[0] = sdp_list_append(nullptr, &spp_uuid);
        apseq = sdp_list_append(0, protocol_list[0]);
        printf("L2CAP protocol added\n");

        printf("Adding RFCOMM protocol...\n");
        sdp_uuid16_create(&spp_uuid, RFCOMM_UUID);
        rfcomm_list = sdp_list_append(nullptr, &spp_uuid);
        printf("Setting RFCOMM channel...\n");
        uuid_t rfcomm_uuid;
        sdp_uuid16_create(&rfcomm_uuid, RFCOMM_UUID);
        protocol_list[1] = sdp_list_append(nullptr, &rfcomm_uuid);
        channel_data = sdp_data_alloc(SDP_UINT8, &channel);
        protocol_list[1] = sdp_list_append(protocol_list[1], channel_data);
        apseq = sdp_list_append(apseq, protocol_list[1]);
        printf("RFCOMM channel set\n");
        aproto = sdp_list_append(nullptr, apseq);

        printf("Setting access protocols...\n");
        sdp_set_access_protos(record, aproto);
        printf("Access protocols set\n");
        
        printf("Setting service name...\n");
        sdp_set_info_attr(record, service_name.c_str(), nullptr, nullptr);
        printf("Service name set\n");

        printf("Registering service record...\n");
        if (sdp_record_register(sdp_session, record, 0) < 0) {
            printf("Failed to register SDP record: %s\n", strerror(errno));
            goto cleanup;
        }
        printf("Service record registered\n");

        printf("Setting up Extended Inquiry Response (EIR) data...\n");
    
           // Add service name
        for (char c : service_name) {
            char hex[3];
            snprintf(hex, sizeof(hex), "%02X", (unsigned char)c);
            name_hex += hex;
        }
        char hex[3];
        snprintf(hex, sizeof(hex), "%02X", (unsigned char)name_hex.length()/2 + 1);
        eir_hex += hex;  // Length and type for name
        eir_hex += "09";
        eir_hex += name_hex;
        //Set Power Level
        eir_hex += "020A";  // Length and type for PowerLevel
        eir_hex += "00";  // Device class (Serial Port)
        //Add device ID with 8 bytes
        eir_hex += "0910";  // Length and type for DeviceID
        eir_hex += "02006B1D46023D05";  // Device ID (Serial Port)
        // Add service UUID (SPP)
        eir_hex += "0303";  // Length and type for UUID
        eir_hex += "0111";  // SPP UUID (0x1101)        
        // Write EIR data using hciconfig
        hciconfig_cmd = "hciconfig hci0 inqdata " + eir_hex;
        printf("Executing command: %s\n", hciconfig_cmd.c_str());
        if (system(hciconfig_cmd.c_str()) != 0) {
            printf("Failed to write EIR data using hciconfig\n");
            goto cleanup;
        }
        printf("EIR data written successfully using hciconfig\n");

        printf("Setting up Bluetooth discoverability and SSP...\n");
        if (system("bluetoothctl power on; hciconfig hci0 sspmode enable; bluetoothctl agent NoInputNoOutput; bluetoothctl default-agent; bluetoothctl agent on; bluetoothctl discoverable on; bluetoothctl pairable on") != 0) {
            printf("Failed to set discoverable mode and SSP using bluetoothctl\n");
            goto cleanup;
        }
        printf("Device is now discoverable with SSP enabled\n");
        result = 0;

cleanup:
        printf("Starting cleanup...\n");
        if (browse_list) sdp_list_free(browse_list, nullptr);
        if (service_class_list) sdp_list_free(service_class_list, nullptr);
        if (protocol_list[0]) sdp_list_free(protocol_list[0], nullptr);
        if (protocol_list[1]) sdp_list_free(protocol_list[1], nullptr);
        if (sdp_session) {
            sdp_close(sdp_session);
            sdp_session = nullptr;
        }
        if (record) {
            sdp_record_free(record);
            record = nullptr;
        }
        printf("Cleanup complete\n");

        printf("SDP service registered on RFCOMM channel %d\n", (int)channel);
        return result;
    }

    // Handle client connections
    void connection_handler() {
        while (running) {
            struct pollfd fds[1];
            fds[0].fd = server_sock;
            fds[0].events = POLLIN;

            // Wait for connections with timeout to check running flag
            int ret = poll(fds, 1, 1000);  // 1 second timeout
            
            if (!running) break;
            
            if (ret < 0) {
                if (errno == EINTR) continue;
                std::cerr << "Poll error: " << strerror(errno) << std::endl;
                break;
            }
            
            if (ret == 0) continue;  // Timeout
            
            if (fds[0].revents & POLLIN) {
                // Accept connection
                struct sockaddr_rc client_addr = { 0 };
                socklen_t addr_len = sizeof(client_addr);
                
                client_sock = accept(server_sock, (struct sockaddr *)&client_addr, &addr_len);
                if (client_sock < 0) {
                    std::cerr << "Accept failed: " << strerror(errno) << std::endl;
                    continue;
                }
                
                // Get the connected device address
                char addr[18] = { 0 };
                ba2str(&client_addr.rc_bdaddr, addr);
                std::cout << "Connection from " << addr << std::endl;
                
                // Handle the client connection (data exchange)
                handle_client();
                
                // Close the client socket
                close(client_sock);
                client_sock = -1;
            }
        }
    }
    
    // Handle client data exchange
    void handle_client() {
        char buf[1024];
        while (running && client_sock >= 0) {
            // Use poll to check for data or client disconnect
            struct pollfd fds[1];
            fds[0].fd = client_sock;
            fds[0].events = POLLIN;
            
            int ret = poll(fds, 1, 1000);  // 1 second timeout
            
            if (!running) break;
            
            if (ret < 0) {
                if (errno == EINTR) continue;
                std::cerr << "Poll error: " << strerror(errno) << std::endl;
                break;
            }
            
            if (ret == 0) continue;  // Timeout
            
            if (fds[0].revents & POLLIN) {
                // Read data from the client
                ssize_t bytes_read = read(client_sock, buf, sizeof(buf) - 1);
                if (bytes_read <= 0) {
                    if (bytes_read < 0) {
                        std::cerr << "Read error: " << strerror(errno) << std::endl;
                    }
                    std::cout << "Client disconnected" << std::endl;
                    break;
                }
                
                buf[bytes_read] = '\0';
                std::cout << "Received: " << buf << std::endl;
                
                // Echo back
                std::string response = "Echo: ";
                response += buf;
                ssize_t bytes_sent = write(client_sock, response.c_str(), response.length());
                if (bytes_sent < 0) {
                    std::cerr << "Failed to send response: " << strerror(errno) << std::endl;
                }
            }
        }
    }

public:
    BluetoothSPP() {
        // Pre-set the SPP UUID
        sdp_uuid16_create(&spp_uuid, SERIAL_PORT_SVCLASS_ID);
    }
    
    ~BluetoothSPP() {
        stop();
    }
    
    // Start the SPP server
    bool start() {
        if (running) {
            std::cerr << "Bluetooth SPP already running" << std::endl;
            return false;
        }

        // Create a socket
        server_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
        if (server_sock < 0) {
            std::cerr << "Failed to create socket: " << strerror(errno) << std::endl;
            return false;
        }
        std::cout << "Socket created successfully." << std::endl;

        // Disable authentication and encryption
        struct bt_security security;
        memset(&security, 0, sizeof(security));
        security.level = BT_SECURITY_LOW;  // No authentication or encryption
        
        if (setsockopt(server_sock, SOL_BLUETOOTH, BT_SECURITY, &security, sizeof(security)) < 0) {
            std::cerr << "Failed to set security options: " << strerror(errno) << std::endl;
            close(server_sock);
            server_sock = -1;
            return false;
        }
        std::cout << "Security level set to NO_MITM." << std::endl;

        // Bind socket to local Bluetooth adapter
        struct sockaddr_rc server_addr = { 0 };
        server_addr.rc_family = AF_BLUETOOTH;
        bdaddr_t bdaddr_any = {{0, 0, 0, 0, 0, 0}};
        server_addr.rc_bdaddr = bdaddr_any;
        server_addr.rc_channel = channel;

        if (bind(server_sock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) {
            std::cerr << "Failed to bind socket: " << strerror(errno) << std::endl;
            close(server_sock);
            server_sock = -1;
            return false;
        }
        std::cout << "Socket bound to channel " << (int)channel << "." << std::endl;
        
        // Listen for connections
        if (listen(server_sock, 1) < 0) {
            std::cerr << "Failed to listen: " << strerror(errno) << std::endl;
            close(server_sock);
            server_sock = -1;
            return false;
        }
        std::cout << "Listening for connections..." << std::endl;
        // Register the service with SDP
        if (register_service() < 0) {
            std::cerr << "Failed to register service with SDP." << std::endl;
            close(server_sock);
            server_sock = -1;
            return false;
        }
        std::cout << "Service registered with SDP." << std::endl;

        // Start the connection handling thread
        running = true;
        connection_thread = std::thread(&BluetoothSPP::connection_handler, this);

        std::cout << "Bluetooth SPP server started on channel " << (int)channel << std::endl;
        return true;
    }
    
    // Stop the SPP server
    void stop() {
        if (!running) return;
        
        running = false;
        
        // Wait for connection thread to finish
        if (connection_thread.joinable()) {
            connection_thread.join();
        }
        
        // Close client socket if open
        if (client_sock >= 0) {
            close(client_sock);
            client_sock = -1;
        }
        
        // Close server socket
        if (server_sock >= 0) {
            close(server_sock);
            server_sock = -1;
        }
        
        // Unregister service from SDP
        if (sdp_session && record) {
            sdp_record_unregister(sdp_session, record);
            sdp_record_free(record);
            record = nullptr;
            sdp_close(sdp_session);
            sdp_session = nullptr;
        }
        
        std::cout << "Bluetooth SPP server stopped" << std::endl;
    }
    
    // Send data to connected client
    bool send(const std::string& message) {
        if (!running || client_sock < 0) {
            std::cerr << "No client connected" << std::endl;
            return false;
        }
        
        ssize_t bytes_sent = write(client_sock, message.c_str(), message.length());
        if (bytes_sent < 0 || (size_t)bytes_sent != message.length()) {
            std::cerr << "Failed to send data: " << strerror(errno) << std::endl;
            return false;
        }
        
        return true;
    }
    
    // Check if a client is connected
    bool is_connected() const {
        return client_sock >= 0;
    }
    
    // Set the service name
    void set_service_name(const std::string& name) {
        if (!running) {
            service_name = name;
        }
    }
    
    // Set the RFCOMM channel
    void set_channel(uint8_t new_channel) {
        if (!running) {
            channel = new_channel;
        }
    }
};

// Sample usage 
int main(int argc, char **argv) {
    BluetoothSPP spp_server;
    spp_server.set_service_name("HoverApp SPP Server");
    
    if (!spp_server.start()) {
        std::cerr << "Failed to start Bluetooth SPP server" << std::endl;
        return 1;
    }
    
    std::cout << "SPP server running. Press Enter to stop." << std::endl;
    
    // Main loop
    std::string cmd;
    while (std::getline(std::cin, cmd)) {
        if (cmd.empty()) {
            break;
        }
        
        if (cmd == "status") {
            std::cout << "Connected: " << (spp_server.is_connected() ? "Yes" : "No") << std::endl;
        } else if (cmd.substr(0, 5) == "send " && cmd.length() > 5) {
            std::string message = cmd.substr(5);
            if (spp_server.send(message)) {
                std::cout << "Message sent" << std::endl;
            }
        } 
    }
    
    spp_server.stop();
    return 0;
}