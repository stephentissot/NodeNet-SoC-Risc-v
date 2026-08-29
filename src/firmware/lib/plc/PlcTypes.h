#ifndef PLC_TYPES_H
#define PLC_TYPES_H

#include <cstdint>

enum class PointDirection : uint8_t {
    Input = 0,
    Output,
    InOut
};

enum class PointBackend : uint8_t {
    Local = 0,
    Modbus,
    NodeNet
};

enum class PointValueType : uint8_t {
    Bool = 0,
    Uint16,
    Int16,
    Uint32,
    Int32,
    Float,
    Enum,
    String
};

enum class PointQuality : uint8_t {
    Unknown = 0,
    Good,
    UncertainInitialValue,
    BadNotConnected,
    BadNodeMissing,
    BadTimeout,
    BadProtocolError,
    BadConfigError,
    BadInvalidValue,
    BadWriteRejected
};

enum class PointCommandQuality : uint8_t {
    Unknown = 0,
    Pending,
    Acked,
    Timeout,
    InvalidValue,
    Rejected,
    ProtocolError
};

#endif