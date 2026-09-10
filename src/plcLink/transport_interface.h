#ifndef PLCLINK_TRANSPORT_INTERFACE_H
#define PLCLINK_TRANSPORT_INTERFACE_H

#include <cstddef>
#include <cstdint>

namespace plclink {

struct TransportInterface {
    void* context;
    size_t (*maxPayload)(void* context);
    bool (*sendFrame)(void* context, const uint8_t* data, size_t size);
    bool (*receiveFrame)(void* context, uint8_t* out_data, size_t capacity, size_t* out_size);
};

inline bool transportValid(const TransportInterface& transport)
{
    return (transport.maxPayload != nullptr) &&
           (transport.sendFrame != nullptr) &&
           (transport.receiveFrame != nullptr);
}

inline size_t transportMaxPayload(const TransportInterface& transport)
{
    return transportValid(transport) ? transport.maxPayload(transport.context) : 0u;
}

inline bool transportSend(const TransportInterface& transport, const uint8_t* data, size_t size)
{
    return transportValid(transport) && transport.sendFrame(transport.context, data, size);
}

inline bool transportReceive(const TransportInterface& transport,
                             uint8_t* out_data,
                             size_t capacity,
                             size_t* out_size)
{
    return transportValid(transport) &&
           transport.receiveFrame(transport.context, out_data, capacity, out_size);
}

} // namespace plclink

#endif