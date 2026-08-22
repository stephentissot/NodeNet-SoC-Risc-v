#pragma once

#include <cstdarg>
#include <cstdio>
#include "nodenet.h"

class NodeLogger
{
public:

    // Constructor : send logs to the specified NodeNet instance and destination address
    explicit NodeLogger(NodeNet *nodeNet, uint8_t destAddr) : _nodeNet(nodeNet), _destAddr(destAddr) {}

    void Info(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        Log("INF", format, args);
        va_end(args);
    }

    void Warning(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        Log("WRN", format, args);
        va_end(args);
    }

    void Error(const char* format, ...)
    {
        va_list args;
        va_start(args, format);
        Log("ERR", format, args);
        va_end(args);
    }

private:
    NodeNet* _nodeNet;
    uint8_t _destAddr;

    void Log(const char* level, const char* format, va_list args)
    {
        va_list argsCopy;
        va_copy(argsCopy, args);

        int size = vsnprintf(nullptr, 0, format, argsCopy);

        va_end(argsCopy);

        if (size < 0)
            return;

        char* buffer = new char[size + 1];

        vsnprintf(buffer, size + 1, format, args);
        _nodeNet->Send(_destAddr, buffer);

        delete[] buffer;
    }
};