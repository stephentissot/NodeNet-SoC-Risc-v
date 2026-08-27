using System;
using System.Collections.Generic;
using System.Text;
using System.IO.Ports;

namespace BigSisterNodeNet.Core.Transport
{
    
    public class SerialPortTransport : ISerialTransport
    {
        private SerialPort _serialPort;
        public string Port { get; set; }

        public bool IsOpen =>
            _serialPort?.IsOpen ?? false;

        public int BytesToRead =>
            _serialPort?.BytesToRead ?? 0;


        public void Open(string port)
        {
            Port = port;
            _serialPort = new SerialPort(
                Port,
                921600,
                Parity.None,
                8,
                StopBits.One);

            _serialPort.Open();
        }


        public string ReadLine(int timeoutMs)
        {
            _serialPort.ReadTimeout = timeoutMs;

            return _serialPort.ReadLine();
        }


        public void WriteLine(string data)
        {
            _serialPort.WriteLine(data);            
        }

        public int Read(byte[] buffer, int offset, int count)
        {
            return _serialPort.Read(buffer, offset, count);
        }

        public int ReadByte()
        {
            return _serialPort.ReadByte();
        }

        public void Write(byte[] buffer, int offset, int count)
        {
            _serialPort.Write(buffer, offset, count);
        }

        public bool HasIncomingData()
        {
            return _serialPort.BytesToRead > 0;
        }

        public void Close()
        {
            if (IsOpen)
                _serialPort.Close();
        }
    }
}
