#nullable enable
using System;
using System.IO;
using System.Net.Sockets;
using System.Text;
using System.Threading;
using Newtonsoft.Json.Linq;

namespace CypressLauncher;

public class ExternalInstance : IDisposable
{
    public int Pid { get; }
    public string Game { get; }
    public bool IsServer { get; }
    public int Port { get; }
    public string Address { get; }
    public DateTime StartTime { get; }
    public bool IsExternal => true;

    private TcpClient? _client;
    private NetworkStream? _stream;
    private Thread? _recvThread;
    private readonly Action<int, string> _onOutput;
    private readonly Action<int> _onExit;
    private readonly object _writeLock = new();
    private bool _disposed;
    private bool _connected;

    public ExternalInstance(int pid, string game, bool isServer, string address, int port,
        Action<int, string> onOutput, Action<int> onExit)
    {
        Pid = pid;
        Game = game;
        IsServer = isServer;
        Address = address;
        Port = port;
        StartTime = DateTime.Now;
        _onOutput = onOutput;
        _onExit = onExit;
    }

    public bool Connect()
    {
        try
        {
            _client = new TcpClient();
            var connectTask = _client.ConnectAsync(Address, Port);
            if (!connectTask.Wait(5000))
            {
                _client.Dispose();
                _client = null;
                return false;
            }

            _stream = _client.GetStream();
            _connected = true;

            SendRaw("{\"type\":\"subscribe\"}\n");

            _recvThread = new Thread(RecvLoop)
            {
                IsBackground = true,
                Name = $"CypressExternal-{Pid}"
            };
            _recvThread.Start();

            Thread monitorThread = new Thread(MonitorProcess)
            {
                IsBackground = true,
                Name = $"CypressMonitor-{Pid}"
            };
            monitorThread.Start();

            return true;
        }
        catch
        {
            return false;
        }
    }

    private void RecvLoop()
    {
        try
        {
            byte[] buf = new byte[65536];
            StringBuilder sb = new StringBuilder();

            while (_connected && _stream != null)
            {
                int bytesRead = _stream.Read(buf, 0, buf.Length);
                if (bytesRead <= 0) break;

                sb.Append(Encoding.UTF8.GetString(buf, 0, bytesRead));

                string buffer = sb.ToString();
                int pos;
                while ((pos = buffer.IndexOf('\n')) >= 0)
                {
                    string line = buffer.Substring(0, pos).TrimEnd('\r');
                    buffer = buffer.Substring(pos + 1);

                    if (!string.IsNullOrEmpty(line))
                        _onOutput(Pid, line);
                }
                sb.Clear();
                sb.Append(buffer);
            }
        }
        catch { }
        finally
        {
            _connected = false;
        }
    }

    private void MonitorProcess()
    {
        try
        {
            while (_connected)
            {
                Thread.Sleep(2000);
                try
                {
                    var proc = System.Diagnostics.Process.GetProcessById(Pid);
                    if (proc.HasExited)
                    {
                        _connected = false;
                        _onExit(Pid);
                        return;
                    }
                }
                catch (ArgumentException)
                {
                    _connected = false;
                    _onExit(Pid);
                    return;
                }
            }
        }
        catch { }
    }

    public void SendCommand(string command)
    {
        if (!_connected || _stream == null) return;

        try
        {
            var msg = new JObject
            {
                ["type"] = "modCommand",
                ["cmd"] = command
            };
            SendRaw(msg.ToString(Newtonsoft.Json.Formatting.None) + "\n");
        }
        catch { }
    }

    public void SendModAction(string type, JObject data)
    {
        if (!_connected || _stream == null) return;
        try
        {
            data["type"] = type;
            SendRaw(data.ToString(Newtonsoft.Json.Formatting.None) + "\n");
        }
        catch { }
    }

    private void SendRaw(string data)
    {
        if (_stream == null) return;
        byte[] bytes = Encoding.UTF8.GetBytes(data);
        lock (_writeLock)
        {
            _stream.Write(bytes, 0, bytes.Length);
            _stream.Flush();
        }
    }

    public void Kill() => Disconnect();

    public void Disconnect()
    {
        _connected = false;
        try { _stream?.Close(); } catch { }
        try { _client?.Close(); } catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        Disconnect();
    }
}
