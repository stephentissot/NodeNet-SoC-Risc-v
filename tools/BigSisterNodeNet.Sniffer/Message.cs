using System;

namespace BigSisterNodeNet.Sniffer
{
    public class Message
    {
        public byte Destination { get; set; }
        public byte Source { get; set; }
        public byte[] Payload { get; set; } = Array.Empty<byte>();

        public bool ShowInConsole { get; set; }
    }
    public class AstroNetMessage
    {
        public byte From{ get; set; }
        public byte To { get; set; }

        public string Cmd { get; set; }

    }
}
