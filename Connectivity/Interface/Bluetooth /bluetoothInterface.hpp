#ifndef BLUETOOTH_INTERFACE_HPP
#define BLUETOOTH_INTERFACE_HPP

#include "baseInterface.hpp"

struct BluetoothInfo: public InterfaceInfo
{
public:
    std::string device_address;
    std::string device_name;
    std::string device_type;
};

template <typename T>
class BluetoothInterface : BaseInterface<T>
{
public:
    BluetoothInterface(InterfaceInfo &info);
    ~BluetoothInterface();
    void Open();
    void Close();
    int Connect();
    void Send(int id, T &data);
    InterfaceStatus Receive(int id, T &data);:
    void BlockingReceive(int id, T &data);
    InterfaceStatus GetStatus();
    InterfaceInfo &GetInfo();
protected:
};

#endif