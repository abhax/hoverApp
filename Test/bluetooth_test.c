#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <bluetooth/rfcomm.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>

#define BT_CHANNEL 1  // RFCOMM channel
#define MAX_BUF_SIZE 1024

volatile sig_atomic_t running = 1;
int client_sock = -1;

// Signal handler for graceful termination
void handle_signal(int sig) {
    printf("\nSignal %d received, cleaning up and exiting...\n", sig);
    running = 0;
    if (client_sock >= 0) {
        close(client_sock);
        client_sock = -1;
    }
}

// Initialize Bluetooth with default settings
void initialize_bluetooth() {
    printf("Initializing Bluetooth...\n");
    
    // Set device name
    if (system("btmgmt name HoverCraftBasic") != 0) {
        fprintf(stderr, "Failed to set device name\n");
    }
    
    // Set pairable mode
    if (system("btmgmt pairable yes") != 0) {
        fprintf(stderr, "Failed to set pairable mode\n");
    }
    
    // Enable Secure Connections
    if (system("btmgmt sc on") != 0) {
        fprintf(stderr, "Failed to enable Secure Connections\n");
    }
    
    // Set IO capability to NoInputNoOutput (3)
    if (system("btmgmt io-cap 3") != 0) {
        fprintf(stderr, "Failed to set IO capability\n");
    }
    
    // Display/manage keys
    if (system("btmgmt keys") != 0) {
        fprintf(stderr, "Failed to manage keys\n");
    }
    
    // Enable link security
    if (system("btmgmt linksec yes") != 0) {
        fprintf(stderr, "Failed to enable link security\n");
    }
    
    printf("Bluetooth initialization completed.\n");
}

// Enable Bluetooth and make it discoverable
void enable_bluetooth() {
    printf("Enabling Bluetooth and making it discoverable...\n");
    
    // Make device discoverable using bluetoothctl
    if (system("bluetoothctl discoverable on") != 0) {
        fprintf(stderr, "Failed to set discoverable mode with bluetoothctl\n");
    }
    
    // Make device discoverable using btmgmt
    if (system("btmgmt discov on") != 0) {
        fprintf(stderr, "Failed to set discoverable mode with btmgmt\n");
    }
    
    // Select HCI device
    if (system("btmgmt select hci0 &") != 0) {
        fprintf(stderr, "Failed to select HCI device\n");
    }
    
    printf("Bluetooth is now enabled and discoverable.\n");
}

// Disable Bluetooth discoverability
void disable_bluetooth() {
    printf("Disabling Bluetooth discoverability...\n");
    
    // Disable discoverable mode using bluetoothctl
    if (system("bluetoothctl discoverable off") != 0) {
        fprintf(stderr, "Failed to disable discoverable mode with bluetoothctl\n");
    }
    
    // Disable discoverable mode using btmgmt
    if (system("btmgmt discov off") != 0) {
        fprintf(stderr, "Failed to disable discoverable mode with btmgmt\n");
    }
    
    printf("Bluetooth discoverability disabled.\n");
}

// Set up Serial Port Profile
void setup_serial_port_profile() {
    printf("Setting up Serial Port Profile...\n");
    
    // Add SP service
    if (system("sdptool add sp") != 0) {
        fprintf(stderr, "Failed to add Serial Port service\n");
    } else {
        printf("Serial Port Profile has been added successfully.\n");
    }
}

// Handle RFCOMM connection and communication
void open_serial_port() {
    struct sockaddr_rc loc_addr = { 0 }, rem_addr = { 0 };
    int server_sock, status;
    char buf[MAX_BUF_SIZE] = { 0 };
    socklen_t opt = sizeof(rem_addr);
    fd_set read_fds;
    struct timeval tv;
    char input_buf[MAX_BUF_SIZE];
    
    // Create socket
    server_sock = socket(AF_BLUETOOTH, SOCK_STREAM, BTPROTO_RFCOMM);
    if (server_sock < 0) {
        perror("Failed to create socket");
        return;
    }
    
    // Bind socket to local Bluetooth adapter
    loc_addr.rc_family = AF_BLUETOOTH;
    loc_addr.rc_bdaddr = *BDADDR_ANY;
    loc_addr.rc_channel = BT_CHANNEL;
    
    status = bind(server_sock, (struct sockaddr *)&loc_addr, sizeof(loc_addr));
    if (status < 0) {
        perror("Failed to bind socket");
        close(server_sock);
        return;
    }
    
    // Listen for connections
    status = listen(server_sock, 1);
    if (status < 0) {
        perror("Failed to listen on socket");
        close(server_sock);
        return;
    }
    
    printf("Waiting for connection on RFCOMM channel %d...\n", BT_CHANNEL);
    
    // Accept connection from client
    client_sock = accept(server_sock, (struct sockaddr *)&rem_addr, &opt);
    
    if (client_sock < 0) {
        perror("Failed to accept connection");
        close(server_sock);
        return;
    }
    
    ba2str(&rem_addr.rc_bdaddr, buf);
    printf("Accepted connection from %s\n", buf);
    
    // Set up signal handler for cleanup
    signal(SIGINT, handle_signal);
    
    // Main communication loop
    printf("Connected. Type 'exit' to quit.\n");
    while (running) {
        FD_ZERO(&read_fds);
        FD_SET(STDIN_FILENO, &read_fds);
        FD_SET(client_sock, &read_fds);
        
        // Set timeout for select
        tv.tv_sec = 1;
        tv.tv_usec = 0;
        
        status = select(client_sock + 1, &read_fds, NULL, NULL, &tv);
        
        if (status < 0) {
            if (errno == EINTR) {
                // Interrupted by signal, check if we should still be running
                continue;
            }
            perror("Select failed");
            break;
        }
        
        // Check for data from Bluetooth client
        if (FD_ISSET(client_sock, &read_fds)) {
            memset(buf, 0, sizeof(buf));
            int bytes_read = read(client_sock, buf, sizeof(buf) - 1);
            
            if (bytes_read > 0) {
                printf("Received: %s", buf);
                // Add newline if there isn't one
                if (buf[bytes_read-1] != '\n') {
                    printf("\n");
                }
            } else if (bytes_read <= 0) {
                printf("Connection closed by client\n");
                break;
            }
        }
        
        // Check for user input
        if (FD_ISSET(STDIN_FILENO, &read_fds)) {
            memset(input_buf, 0, sizeof(input_buf));
            if (fgets(input_buf, sizeof(input_buf), stdin) != NULL) {
                
                // Check for exit command
                if (strncmp(input_buf, "exit", 4) == 0) {
                    printf("Exiting...\n");
                    break;
                }
                
                // Send input to client
                if (write(client_sock, input_buf, strlen(input_buf)) < 0) {
                    perror("Failed to send data");
                    break;
                }
            }
        }
    }
    
    // Cleanup
    if (client_sock >= 0) {
        close(client_sock);
        client_sock = -1;
    }
    close(server_sock);
    printf("Serial port connection closed.\n");
}

void display_menu() {
    printf("\n===== Bluetooth Test Application =====\n");
    printf("1. Initialize Bluetooth\n");
    printf("2. Enable Bluetooth\n");
    printf("3. Setup Serial Port Profile\n");
    printf("4. Open Serial Port\n");
    printf("5. Disable Bluetooth\n");
    printf("0. Exit\n");
    printf("Enter your choice: ");
}

int main() {
    int choice;
    
    // Configure signal handling
    signal(SIGINT, handle_signal);
    signal(SIGTERM, handle_signal);
    
    while (running) {
        display_menu();
        
        if (scanf("%d", &choice) != 1) {
            // Clear input buffer on invalid input
            int c;
            while ((c = getchar()) != '\n' && c != EOF);
            fprintf(stderr, "Invalid input. Please enter a number.\n");
            continue;
        }
        
        // Consume newline
        getchar();
        
        switch (choice) {
            case 0:
                printf("Exiting application...\n");
                running = 0;
                break;
            case 1:
                initialize_bluetooth();
                break;
            case 2:
                enable_bluetooth();
                break;
            case 3:
                setup_serial_port_profile();
                break;
            case 4:
                open_serial_port();
                break;
            case 5:
                disable_bluetooth();
                break;
            default:
                printf("Invalid choice. Please try again.\n");
                break;
        }
    }
    
    return 0;
} 