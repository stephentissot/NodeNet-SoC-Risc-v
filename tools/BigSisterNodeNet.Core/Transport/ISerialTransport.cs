using System;
using System.Collections.Generic;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Core.Transport
{
    public interface ISerialTransport
    {
        bool IsOpen { get; }
        void Open(string port);

        string ReadLine(int timeoutMs);
        void WriteLine(string data);

        int BytesToRead { get; }
        int Read(byte[] buffer, int offset, int count);
        int ReadByte();
        void Write(byte[] buffer, int offset, int count);

        bool HasIncomingData();

        string Port { get; set; }

        void Close();

    }
}
