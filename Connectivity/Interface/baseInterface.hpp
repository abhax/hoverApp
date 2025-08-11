#ifndef BASE_INTERFACE_HPP
#define BASE_INTERFACE_HPP

enum class InterfaceStatus
{
    OPEN,
    CLOSED,
    CONNECTED,
    DISCONNECTED,
    TIMEOUT,
    ERROR,
};

struct InterfaceInfo
{
public:
    std::string name;
    std::string description;
    std::string version;
};

template <typename T>
class BaseInterface
{
public:
    BaseInterface(InterfaceInfo &info);
    ~BaseInterface();
    virtual void Open() = 0;
    virtual void Close() = 0;
    virtual int Connect() = 0;
    virtual void Send(int id, T &data) = 0;
    virtual InterfaceStatus Receive(int id,   T &data) = 0;
    virtual void BlockingReceive(int id, T &data) = 0;
    virtual InterfaceStatus GetStatus() = 0;
    virtual InterfaceInfo &GetInfo() = 0;

protected:
    InterfaceInfo info;
}

#endif

