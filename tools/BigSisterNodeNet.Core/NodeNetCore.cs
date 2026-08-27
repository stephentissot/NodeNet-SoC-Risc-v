using BigSisterNodeNet.Core.HandleCommands;
using BigSisterNodeNet.Core.Models;
using BigSisterNodeNet.Core.PlcCore;
using BigSisterNodeNet.Core.Services;
using BigSisterNodeNet.Core.Transport;
using Microsoft.Extensions.DependencyInjection;
using Newtonsoft.Json;
using Serilog;
using System;
using System.Collections.Concurrent;
using System.Collections.Generic;
using System.Collections.ObjectModel;
using System.IO;
using System.Linq;
using System.Runtime.CompilerServices;
using System.Text;
using System.Threading;
using System.Threading.Tasks;
using System.Windows.Markup;


namespace BigSisterNodeNet.Core
{
    public class NodeNetCore
    {

        private BlockingCollection<NodeNetMessage> _incoming;

        private BlockingCollection<NodeNetMessage> _outgoing;

        protected BlockingCollection<INode> _nodeList;

        // Special objects for UI binding, to avoid cross-thread exceptions
        public event EventHandler<INode> NodeUpdated;
        public event EventHandler<INode> NodeHeartbeat;

        private ISerialTransport _transport;

        // Incoming and outgoing threads
        private Thread _receiveThread;
        private Thread _sendThread;
        private Thread _commandThread;
        private Thread _aliveThread;

        private CancellationTokenSource _cts;
        private int _readTimeoutMs;
        public static Status Status;
        private NodeDiscovery _discoveryHandler;
        private IPointCatalogService _pointCatalogService;
        private IServiceProvider _serviceProvider;
        private readonly JsonObjectFrameReader _jsonFrameReader = new JsonObjectFrameReader();
        private readonly object _transportIoLock = new object();
        private readonly object _transportSessionLock = new object();
        private readonly object _incomingWaitersLock = new object();
        private readonly List<PendingIncomingMessageWaiter> _incomingWaiters = new List<PendingIncomingMessageWaiter>();

        public event EventHandler<string> CoreMessageEvent;

        // --------------------------------------------------------------------
        // Constructors
        // --------------------------------------------------------------------
        // Standalone CTOR
        // --------------------------------------------------------------------
        public NodeNetCore() : this(new SerialPortTransport())
        {           
        }
        // --------------------------------------------------------------------
        // Constructor from ASCOM driver (or other), with existing serial transport
        // --------------------------------------------------------------------
        public NodeNetCore(ISerialTransport serial, int readTimeoutMs = 150)
        {
            // Add Serilog
            Log.Logger = new LoggerConfiguration()
                .MinimumLevel.Debug()
                .WriteTo.Console()
                .CreateLogger();

            if(serial  == null)
            {
                _transport = new SerialPortTransport();
            }
            else
            {
                _transport = serial;
            }

            _incoming = new BlockingCollection<NodeNetMessage>();
            _outgoing = new BlockingCollection<NodeNetMessage>();
            _nodeList = new BlockingCollection<INode>();
            _readTimeoutMs = readTimeoutMs;
            _cts = new CancellationTokenSource();

            // Configuration du conteneur DI
            var services = new ServiceCollection();
            ConfigureServices(services);
            _serviceProvider = services.BuildServiceProvider();

            // Initialiser le Service Locator pour les objets désérialisés
            var nodeUpdateService = _serviceProvider.GetRequiredService<INodeUpdateService>();
            NodeUpdateServiceLocator.SetInstance(nodeUpdateService);
            PointCatalogServiceLocator.SetInstance(_serviceProvider.GetRequiredService<IPointCatalogService>());
            PlcProgramUploadServiceLocator.SetInstance(_serviceProvider.GetRequiredService<IPlcProgramUploadService>());

            // Récupération du handler via DI
            _discoveryHandler = _serviceProvider.GetRequiredService<NodeDiscovery>();
            _pointCatalogService = _serviceProvider.GetRequiredService<IPointCatalogService>();
        }

        private void ConfigureServices(IServiceCollection services)
        {
            // Enregistrement des collections comme singletons
            services.AddSingleton(_outgoing);
            services.AddSingleton(_nodeList);

            // Enregistrement des services
            services.AddSingleton<IMessageQueue, MessageQueue>();
            services.AddSingleton<INodeRepository, NodeRepository>();
            services.AddSingleton<INodeUpdateService, NodeUpdateService>();
            services.AddSingleton<IPointCatalogService, PointCatalogService>();
            services.AddSingleton<IPlcProgramUploadService>(sp => new PlcProgramUploadService(this));
            services.AddSingleton<INodeEventPublisher>(sp => 
                new NodeEventPublisher(
                    (INode n) =>
                    {
                        // CallBack for NodeAdded event
                        NodeUpdated?.Invoke(this, n);
                        // And save the nodes to file
                        if (!Directory.Exists(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BigSisterNodeNet")))
                        {
                            Directory.CreateDirectory(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BigSisterNodeNet"));
                        }
                        SaveNodesToFile(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BigSisterNodeNet", "nodes.json"));
                    },
                    (INode n) =>
                    {
                        // CallBack for NodeHeartbeat event
                        NodeHeartbeat?.Invoke(this, n);
                    }
                )
            );

            // Node discovery service
            services.AddSingleton<NodeDiscovery>(); 
        }

        internal TResult ExecuteExclusiveTransportSession<TResult>(Func<Transport.ISerialTransport, TResult> action)
        {
            if (action == null)
            {
                throw new ArgumentNullException(nameof(action));
            }

            lock (_transportSessionLock)
            {
                return action(_transport);
            }
        }

        internal void EnqueueOutgoingMessage(NodeNetMessage message)
        {
            if (message == null)
            {
                throw new ArgumentNullException(nameof(message));
            }

            _outgoing.Add(message);
        }

        internal void SendRawFrame(byte[] buffer, int offset, int count)
        {
            if (buffer == null)
            {
                throw new ArgumentNullException(nameof(buffer));
            }

            if (_transport == null || !_transport.IsOpen)
            {
                throw new InvalidOperationException("The NodeNet serial transport must be started before sending PLC upload frames.");
            }

            lock (_transportIoLock)
            {
                _transport.Write(buffer, offset, count);
            }
        }

        internal NodeNetMessage WaitForIncomingMessage(Func<NodeNetMessage, bool> predicate, TimeSpan timeout)
        {
            if (predicate == null)
            {
                throw new ArgumentNullException(nameof(predicate));
            }

            var waiter = new PendingIncomingMessageWaiter(predicate);
            lock (_incomingWaitersLock)
            {
                _incomingWaiters.Add(waiter);
            }

            try
            {
                return waiter.Wait(timeout);
            }
            finally
            {
                lock (_incomingWaitersLock)
                {
                    _incomingWaiters.Remove(waiter);
                }
            }
        }

        internal NodeNetMessage ExecuteWithIncomingMessageWait(
            Func<NodeNetMessage, bool> predicate,
            TimeSpan timeout,
            Action action)
        {
            if (predicate == null)
            {
                throw new ArgumentNullException(nameof(predicate));
            }

            if (action == null)
            {
                throw new ArgumentNullException(nameof(action));
            }

            var waiter = new PendingIncomingMessageWaiter(predicate);
            lock (_incomingWaitersLock)
            {
                _incomingWaiters.Add(waiter);
            }

            try
            {
                action();
                return waiter.Wait(timeout);
            }
            finally
            {
                lock (_incomingWaitersLock)
                {
                    _incomingWaiters.Remove(waiter);
                }
            }
        }

        // --------------------------------------------------------------------
        // Start and Stop methods
        // --------------------------------------------------------------------        
        public void Start(string port)
        {
            _transport.Port = port;
            if(_transport.IsOpen)
            {
                CoreMessageEvent?.Invoke(this, $"Transport {port} is already open.");
                return;
            }
            _transport.Open(port);
            Start();
        }
        public void Start()
        {
            if (string.IsNullOrEmpty(_transport.Port)) return;
            
            Status = Status.INIT;
            _receiveThread = new Thread(ReceiveLoop)
            {
                IsBackground = true,
                Name = "NodeNet RX"
            };
            _sendThread = new Thread(SendLoop)
            {
                IsBackground = true,
                Name = "NodeNet TX"
            };
            _commandThread = new Thread(CommandLoop)
            {
                IsBackground = true,
                Name = "NodeNet CMD"
            };
            _aliveThread = new Thread(() =>
            {
                while (!_cts.Token.IsCancellationRequested)
                {
                    try
                    {
                        // Send a heartbeat message every 5 seconds
                        _discoveryHandler.Heartbeat();
                        // And check for nodes that have not sent a heartbeat in the last 10 seconds
                        _discoveryHandler.CheckNodeHeartbeats();
                        Thread.Sleep(5000);
                    }
                    catch (OperationCanceledException)
                    {
                        break;
                    }
                }
            })
            {
                IsBackground = true,
                Name = "NodeNet Alive"
            };
            _receiveThread.Start();
            _sendThread.Start();
            _commandThread.Start();
            _aliveThread.Start();
            // Load nodes from appData file
            LoadNodesFromFile(Path.Combine(Environment.GetFolderPath(Environment.SpecialFolder.ApplicationData), "BigSisterNodeNet", "nodes.json"));
            // Send the first discovery message to find all nodes on the network
            Task.Run(async () =>
            {
                await Task.Delay(100);

                _discoveryHandler.WhoIs();                
            });
        }
        public void Stop()
        {
            _cts.Cancel();

            if (_receiveThread != null && _receiveThread.IsAlive)
            {
                _receiveThread.Join(1000);
            }
            if (_sendThread != null && _sendThread.IsAlive)
            {
                _sendThread.Join(1000);
            }
            if (_commandThread != null && _commandThread.IsAlive)
            {
                _commandThread.Join(1000);
            }

            _transport?.Close();
        }
        // --------------------------------------------------------------------
        // COM methods
        // --------------------------------------------------------------------
        public bool IsConnected()
        {
            return _transport != null && _transport.IsOpen;
        }
        public string GetPort()
        {
            return _transport?.Port ?? "COM1";
        }
        public void SetPort(string port)
        {
            if (_transport != null)
            {
                _transport.Port = port;
            }
        }
        // --------------------------------------------------------------------
        // Core Threads
        // --------------------------------------------------------------------
        private void CommandLoop()
        {
            try
            {
                foreach (var message in
                         _incoming.GetConsumingEnumerable(_cts.Token))
                {
                    try
                    {
                        _discoveryHandler.OnNodePulse(message);
                        switch (message.Command)
                        {
                            case NodeNetCommands.IAm:
                                _discoveryHandler.UpdateNodes(message);
                                break;

                            case NodeNetCommands.WhoIs:
                                _discoveryHandler.UpdateNodes(message);
                                _discoveryHandler.IAm();
                                break;

                            case NodeNetCommands.RemoteResponseData:
                                _discoveryHandler.UpdateNodes(message);
                                break;
                            case NodeNetCommands.HeartBeat:
                                // Already done !
                                break;
                            case NodeNetCommands.Network_Res:
                                _discoveryHandler.UpdateNodes(message);
                                break;
                            case NodeNetCommands.PointDefinitionsRes:
                                if (message is PointDefinitionsResponse pointDefinitionsResponse)
                                {
                                    _pointCatalogService.HandlePointDefinitionsResponse(pointDefinitionsResponse);
                                }
                                break;
                            case NodeNetCommands.PointStatesRes:
                                if (message is PointStatesResponse pointStatesResponse)
                                {
                                    _pointCatalogService.HandlePointStatesResponse(pointStatesResponse);
                                }
                                break;
                            default:                                
                                break;
                        }
                    }
                    catch (Exception ex)
                    {
                        Log.Logger.Error($"CommandLoop error ({message.Command}) : {ex}");
                    }
                }
            }
            catch (OperationCanceledException ex)
            {
                Log.Logger.Information($"CommandLoop stopped: {ex.Message}");
            }

        }
        private void ReceiveLoop()
        {
            var buffer = new byte[512];
            while (!_cts.Token.IsCancellationRequested)
            {
                try
                {
                    bool hasIncomingData;
                    lock (_transportIoLock)
                    {
                        hasIncomingData = _transport.HasIncomingData();
                    }

                    if (hasIncomingData)
                    {
                        int count;
                        lock (_transportIoLock)
                        {
                            var bytesToRead = _transport.BytesToRead;
                            if (bytesToRead <= 0)
                            {
                                continue;
                            }

                            count = _transport.Read(buffer, 0, Math.Min(buffer.Length, bytesToRead));
                        }

                        for (var index = 0; index < count; index += 1)
                        {
                            if (!_jsonFrameReader.TryAppend(buffer[index], out var frame))
                            {
                                continue;
                            }

                            var line = frame.Trim();
                            if (string.IsNullOrEmpty(line))
                            {
                                continue;
                            }

                            try
                            {
                                if (line.StartsWith("[INF]"))
                                {
                                    Log.Logger.Information("ESP32 {Message}",
                                        line.Substring(5));
                                }
                                else if (line.StartsWith("[WRN]"))
                                {
                                    Log.Logger.Warning("ESP32 {Message}",
                                        line.Substring(5));
                                }
                                else if (line.StartsWith("[ERR]"))
                                {
                                    Log.Logger.Error("ESP32 {Message}",
                                        line.Substring(5));
                                }
                                else
                                {
                                    NodeNetMessage message = JsonConvert.DeserializeObject<NodeNetMessage>(line);
                                    if (message != null)
                                    {
                                        switch (message.Command)
                                        {
                                            case NodeNetCommands.PointDefinitionsRes:
                                                message = JsonConvert.DeserializeObject<PointDefinitionsResponse>(line);
                                                break;
                                            case NodeNetCommands.PointStatesRes:
                                                message = JsonConvert.DeserializeObject<PointStatesResponse>(line);
                                                break;
                                        }

                                        if (message.Command != NodeNetCommands.HeartBeat) Log.Information($"CORE Receive " + line);
                                        DispatchIncomingMessageToWaiters(message);
                                        _incoming.Add(message);
                                    }
                                    else
                                    {
                                        Log.Error($"Unable to deserialize message {line}");
                                    }
                                }
                            }
                            catch
                            {
                                Log.Logger.Error($"Failed to income message: \"{line}\"");
                            }
                        }
                    }
                }
                catch (TimeoutException)
                {
                    // Normal : aucune donnée reçue pendant le timeout
                    continue;
                }
                catch
                {
                    //Log.Logger.Error($"ReceiveLoop error: {ex}");
                }
            }
        }
        private void SendLoop()
        {
            foreach (var message in _outgoing.GetConsumingEnumerable())
            {
                try
                {
                    string json = JsonConvert.SerializeObject(message, new JsonSerializerSettings
                    {
                        NullValueHandling = NullValueHandling.Ignore
                    });

                    lock (_transportIoLock)
                    {
                        _transport.WriteLine(json);
                    }
                }
                catch (Exception ex)
                {
                    Log.Logger.Error($"Error in SendLoop: {ex.Message}");
                }
            }
        }

        private void DispatchIncomingMessageToWaiters(NodeNetMessage message)
        {
            PendingIncomingMessageWaiter[] waiters;
            lock (_incomingWaitersLock)
            {
                waiters = _incomingWaiters.ToArray();
            }

            foreach (var waiter in waiters)
            {
                waiter.TryMatch(message);
            }
        }
        // --------------------------------------------------------------------
        // Storage methods
        // --------------------------------------------------------------------
        private void SaveNodesToFile(string filePath)
        {
            try
            {
                var nodes = _nodeList.ToArray();
                var settings = new JsonSerializerSettings
                {
                    TypeNameHandling = TypeNameHandling.Auto
                };
                string json = JsonConvert.SerializeObject(nodes, Formatting.Indented, settings);
                File.WriteAllText(filePath, json);
            }
            catch (Exception ex)
            {
                Log.Logger.Error($"Error saving nodes to file: {ex.Message}");
            }
        }
        private void LoadNodesFromFile(string filePath)
        {
            try
            {
                if (File.Exists(filePath))
                {
                    string json = File.ReadAllText(filePath);
                    var settings = new JsonSerializerSettings
                    {
                        TypeNameHandling = TypeNameHandling.Auto
                    };
                    var nodes = JsonConvert.DeserializeObject<INode[]>(json, settings);
                    if (nodes != null)
                    {
                        foreach (var node in nodes)
                        {
                            if(_nodeList.Any(n => n.DeviceId == node.DeviceId))
                            {
                                
                                continue;
                            }
                            else
                            {
                                node.IsOnline = false; // Mark as offline until discovered
                                _nodeList.Add(node);
                                NodeUpdated?.Invoke(this, node);
                            }                            
                        }
                    }
                }
            }
            catch (Exception ex)
            {
                Log.Logger.Error($"Error loading nodes from file: {ex.Message}");
            }
        }


        public void Dispose()
        {
            Stop();

            _incoming.Dispose();
            _outgoing.Dispose();
            _cts.Dispose();
        }

        private sealed class JsonObjectFrameReader
        {
            private readonly StringBuilder _builder = new StringBuilder();
            private bool _inString;
            private bool _escape;
            private int _depth;
            private bool _started;

            public bool TryAppend(byte value, out string frame)
            {
                frame = null;
                var character = (char)value;

                if (!_started)
                {
                    if (character != '{')
                    {
                        return false;
                    }

                    _started = true;
                    _depth = 1;
                    _builder.Clear();
                    _builder.Append(character);
                    return false;
                }

                _builder.Append(character);

                if (_escape)
                {
                    _escape = false;
                    return false;
                }

                if (character == '\\')
                {
                    if (_inString)
                    {
                        _escape = true;
                    }
                    return false;
                }

                if (character == '"')
                {
                    _inString = !_inString;
                    return false;
                }

                if (_inString)
                {
                    return false;
                }

                if (character == '{')
                {
                    _depth += 1;
                }
                else if (character == '}')
                {
                    _depth -= 1;
                    if (_depth == 0)
                    {
                        frame = _builder.ToString();
                        Reset();
                        return true;
                    }
                }

                return false;
            }

            private void Reset()
            {
                _builder.Clear();
                _inString = false;
                _escape = false;
                _depth = 0;
                _started = false;
            }
        }

        private sealed class PendingIncomingMessageWaiter
        {
            private readonly Func<NodeNetMessage, bool> _predicate;
            private readonly TaskCompletionSource<NodeNetMessage> _completion = new TaskCompletionSource<NodeNetMessage>();

            public PendingIncomingMessageWaiter(Func<NodeNetMessage, bool> predicate)
            {
                _predicate = predicate ?? throw new ArgumentNullException(nameof(predicate));
            }

            public void TryMatch(NodeNetMessage message)
            {
                if (message == null || _completion.Task.IsCompleted)
                {
                    return;
                }

                bool isMatch;
                try
                {
                    isMatch = _predicate(message);
                }
                catch
                {
                    isMatch = false;
                }

                if (isMatch)
                {
                    _completion.TrySetResult(message);
                }
            }

            public NodeNetMessage Wait(TimeSpan timeout)
            {
                if (!_completion.Task.Wait(timeout))
                {
                    throw new TimeoutException("Timed out while waiting for a matching NodeNet response.");
                }

                return _completion.Task.Result;
            }
        }

        
    }
}
