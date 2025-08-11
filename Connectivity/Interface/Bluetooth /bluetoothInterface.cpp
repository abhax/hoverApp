#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <sys/socket.h>
#include <bluetooth/bluetooth.h>
#include <pthread.h>
#include <stdbool.h>
#include <signal.h>
#include "bluetoothInterface.hpp"

BluetoothInterface::BluetoothInterface(InterfaceInfo &info) : BaseInterface<T>(info)
{
}

BluetoothInterface::~BluetoothInterface()
{
}

void BluetoothInterface::Open()
{
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

void BluetoothInterface::Close()
{
}

int BluetoothInterface::Connect()
{
}

void BluetoothInterface::Send(int id, T &data)
{
}

InterfaceStatus BluetoothInterface::Receive(int id, T &data)
{
}

InterfaceStatus BluetoothInterface::GetStatus()
{
}

InterfaceInfo &BluetoothInterface::GetInfo()
{
}
