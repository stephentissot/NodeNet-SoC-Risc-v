using System;
using System.Collections.Generic;
using System.Text;

namespace BigSisterNodeNet.Sniffer
{
    public static class Encoder
    {
        public static byte[] Encode(byte src, byte dst, byte[] payload)
        {
            const byte LF = 0x0A;
            const byte SOH = 0x01;
            const byte STX = 0x02;
            const byte ETX = 0x03;
            const byte EOT = 0x04;

            int len = payload.Length;

            List<byte> frame = new List<byte>();

            // Header
            frame.Add(LF);
            frame.Add(LF);
            frame.Add(LF);
            frame.Add(SOH);

            // IMPORTANT ORDER IDENTIQUE ARDUINO
       
            frame.Add(dst);
            frame.Add(src);

            // payload Length
            byte lenHi = (byte)((len >> 8) & 0xFF);
            byte lenLo = (byte)(len & 0xFF);

            frame.Add(lenHi);
            frame.Add(lenLo);

            frame.Add(STX);

            byte crc = (byte)(src ^ dst ^ len);

            // Payload encoding EXACT Arduino
            foreach (byte b in payload)
            {
                crc ^= b;

                byte high = (byte)(b & 0xF0);
                high = (byte)(high | (((~high) & 0xF0) >> 4));

                byte low = (byte)(b & 0x0F);
                low = (byte)(low | (~low << 4));

                frame.Add(high);
                frame.Add(low);

            }

            frame.Add(ETX);
            frame.Add(crc);
            frame.Add(EOT);
            frame.Add(LF);
            frame.Add(LF);
            return frame.ToArray();
        }




        public static Message Decode(byte[] frame)
        {
            const byte SOH = 0x01;
            const byte STX = 0x02;
            const byte ETX = 0x03;
            const byte EOT = 0x04;

            int pos = Array.IndexOf(frame, SOH);

            if (pos < 0)
                throw new Exception("SOH not found");

            byte dst = frame[pos + 1];
            byte src = frame[pos + 2];
            ushort len = (ushort)((frame[pos + 3] << 8) | frame[pos + 4]);

            if (frame[pos + 5] != STX)
            {
                if (frame[pos + 5] == EOT) // heartbeat message
                {
                    return new Message
                    {
                        Source = src,
                        Destination = 0,
                        Payload = Encoding.ASCII.GetBytes("{\"cmd\":\"heartBeat\", \"from\":\"" + src + "\"}"),
                        ShowInConsole = true
                    };
                }
                else throw new Exception("Invalid STX");
            }

            int payloadStart = pos + 6;

            byte[] payload = new byte[len];

            byte crc = (byte)(src ^ dst ^ (len >> 8) ^ (len & 0xFF));

            for (int i = 0; i < len; i++)
            {
                byte highEncoded = frame[payloadStart + i * 2];
                byte lowEncoded = frame[payloadStart + i * 2 + 1];

                if ((((~((highEncoded >> 4) | (highEncoded << 4))) & 0xFF)) != highEncoded)
                    throw new Exception($"{DateTime.Now.TimeOfDay.ToString(@"hh\:mm\:ss\:fff")} Invalid high nibble encoding src {src} -> dest {dst}");

                if ((((~((lowEncoded >> 4) | (lowEncoded << 4))) & 0xFF)) != lowEncoded)
                    throw new Exception($"{DateTime.Now.TimeOfDay.ToString(@"hh\:mm\:ss\:fff")} Invalid low nibble encoding src {src} -> dest {dst}");

                byte value =
                    (byte)((highEncoded & 0xF0) |
                           (lowEncoded & 0x0F));

                payload[i] = value;

                crc ^= value;
            }

            int trailer = payloadStart + len * 2;

            if (frame[trailer] != ETX)
                throw new Exception("ETX missing");

            byte receivedCrc = frame[trailer + 1];

            if (receivedCrc != crc)
                throw new Exception("CRC mismatch");

            if (frame[trailer + 2] != EOT)
                throw new Exception("EOT missing");

            return new Message
            {
                Source = src,
                Destination = dst,
                Payload = payload,
                ShowInConsole = true
            };
        }

    }
}
