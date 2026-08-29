using System;

namespace BigSisterNodeNet.Core.PlcCore
{
    public enum PointBackend : byte
    {
        Local = 0,
        Modbus = 1,
        NodeNet = 2
    }

    public enum PointDirection : byte
    {
        Input = 0,
        Output = 1,
        InOut = 2
    }

    public enum PointValueType : byte
    {
        Bool = 0,
        Uint16 = 1,
        Int16 = 2,
        Uint32 = 3,
        Int32 = 4,
        Float = 5,
        Enum = 6,
        String = 7
    }

    public enum ModbusTable : byte
    {
        Coils = 1,
        DiscreteInputs = 2,
        HoldingRegisters = 3,
        InputRegisters = 4
    }

    public enum ModbusAccess : byte
    {
        Read = 1,
        Write = 2,
        ReadWrite = 3
    }

    public enum PointQuality : byte
    {
        Unknown = 0,
        Good = 1,
        UncertainInitialValue = 2,
        BadNotConnected = 3,
        BadNodeMissing = 4,
        BadTimeout = 5,
        BadProtocolError = 6,
        BadConfigError = 7,
        BadInvalidValue = 8,
        BadWriteRejected = 9
    }
}
