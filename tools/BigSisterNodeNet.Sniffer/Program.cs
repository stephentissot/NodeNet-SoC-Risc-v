
using Newtonsoft.Json;
using System;
using System.Collections.Generic;
using System.IO.Ports;
using System.Linq;
using System.Text;
using System.Threading.Tasks;

namespace BigSisterNodeNet.Sniffer
{
    internal class Program
    {
        static void Main(string[] args)
        {
            byte addr = 5; // PC


            using (var port = new SerialPort("COM4", 1000000))
            {
                port.Open();

                Console.WriteLine("BigSisterNodeNet Sniffer");
                Console.WriteLine("Connected to COM4");
                Console.WriteLine();

                // Réception
                Task.Run(() =>
                {
                    List<byte> frame = new List<byte>();

                    while (true)
                    {

                        try
                        {
                            byte b = (byte)port.ReadByte();
                            //if(b == 0x04) Console.WriteLine($"{b:X2}");
                            //else Console.Write($"{b:X2} ");
                            ProcessByte(b);
                        }
                        catch
                        {
                            // port error only
                        }
                    }
                });

                // Emission
                while (true)
                {
                    Console.Write("> ");

                    string cmd = Console.ReadLine();

                    if (string.IsNullOrWhiteSpace(cmd))
                        continue;                    

                    var frame = Encoder.Encode( 
                        src: addr,          // PC
                        dst: 4,     // NodeNet SoC
                        payload: Encoding.ASCII.GetBytes(cmd));

                        

                    port.Write(frame, 0, frame.Length);
                    //Console.Write("Sending ");
                    //for (int i = 0; i < frame.Length; i++)
                    //{
                    //    Console.Write($"{frame[i]:X2} ");
                    //}
                    Console.WriteLine();
                    Console.ForegroundColor = ConsoleColor.Cyan;
                    Console.WriteLine(
                                        $"[{DateTime.Now.TimeOfDay.ToString(@"hh\:mm\:ss\:fff")}]" +
                                        $"TX [{addr}->{4}] " +
                                        $"{cmd}");

                    Console.ResetColor();
                }
            }

        }




        static List<byte> rx = new List<byte>();

        static void ProcessByte(byte b)
        {
            rx.Add(b);

            while (true)
            {
                int soh = rx.IndexOf(0x01); // SOH

                if (soh < 0)
                {
                    rx.Clear();
                    return;
                }

                // discard garbage before SOH
                if (soh > 0)
                {
                    rx.RemoveRange(0, soh);
                }

                // minimum header:
                // SOH + DST + SRC + LEN1 + LEN2 + STX
                if (rx.Count < 5)
                    return;

                byte dst = rx[1];
                byte src = rx[2];
                ushort len = (ushort)((ushort)rx[3] << 8);
                len |= rx[4];


                //
                // Heartbeat
                //
                if (len == 0)
                {
                    // SOH DST SRC LENH LENL EOT
                    const int heartbeatSize = 6;

                    if (rx.Count < heartbeatSize)
                        return;

                    if (rx[5] != 0x04)   // EOT
                    {
                        // Ce n'est pas un heartbeat valide.
                        // On se resynchronise.
                        rx.RemoveAt(0);
                        continue;
                    }

                    rx.RemoveRange(0, heartbeatSize);

                    HandleMessage(new Message
                    {
                        Source = src,
                        Destination = 0,
                        Payload = Encoding.ASCII.GetBytes("{\"cmd\":\"heartBeat\", \"from\":\"" + src + "\"}"),
                        ShowInConsole = false
                    });

                    continue;
                }

                int payloadStart = 6;

                int frameSize =
                    payloadStart +
                    (len * 2) +   // nibble encoded payload
                    3 +            // ETX + CRC + EOT
                    2;             // LF LF (tail padding)

                if (rx.Count < frameSize)
                    return; // wait more data

                if (rx.Count >= 6 && rx[5] != 0x02)
                {
                    rx.RemoveAt(0);
                    continue;
                }

                // extract full frame
                byte[] frame = rx.Take(frameSize).ToArray();

                rx.RemoveRange(0, frameSize);

                try
                {
                    var msg = Encoder.Decode(frame);

                    HandleMessage(msg);
                }
                catch (Exception ex)
                {
                    Console.WriteLine("Decode error: " + ex.Message);
                    // IMPORTANT: we do NOT clear buffer
                }
            }
        }

        static void HandleMessage(Message msg)
        {
            if (msg.ShowInConsole)
            {
                Console.WriteLine();
                Console.ForegroundColor = ConsoleColor.Green;
                var docTxt = Encoding.ASCII.GetString(msg.Payload);
                Console.WriteLine(
                    $"[{DateTime.Now.TimeOfDay.ToString(@"hh\:mm\:ss\:fff")}]" +
                    $"RX [{msg.Source}->{msg.Destination}] " +
                    $"{docTxt}");
                Console.ResetColor();

                //var doc = JsonConvert.DeserializeObject<AstroNetMessage>(docTxt);


                //if (doc.Cmd == "WhoIs")
                //{

                //}
                Console.Write("> ");
            }
            

        }
    }
}
