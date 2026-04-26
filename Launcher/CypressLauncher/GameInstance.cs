#nullable enable
using System;
using System.Diagnostics;
using System.IO;
using System.Text;
using System.Threading;

namespace CypressLauncher;

public class GameInstance : IDisposable
{
    public int Pid { get; }
    public string Game { get; }
    public bool IsServer { get; }
    public int ClientGamePort { get; }
    public int ServerGamePort { get; }
    public DateTime StartTime { get; }
    public Process Process { get; }

    private readonly StreamWriter? _stdin;
    private readonly Thread? _stdoutThread;
    private readonly Action<int, string> _onOutput;
    private readonly Action<int> _onExit;
    private StreamWriter? _logWriter;
    private bool _disposed;

    public GameInstance(Process process, string game, bool isServer, int clientGamePort, int serverGamePort,
        Action<int, string> onOutput, Action<int> onExit)
    {
        Process = process;
        Pid = process.Id;
        Game = game;
        IsServer = isServer;
        ClientGamePort = clientGamePort;
        ServerGamePort = serverGamePort;
        StartTime = DateTime.Now;
        _onOutput = onOutput;
        _onExit = onExit;

        if (process.StartInfo.RedirectStandardInput)
            _stdin = process.StandardInput;

        if (process.StartInfo.RedirectStandardOutput)
        {
            _stdoutThread = new Thread(ReadStdout)
            {
                IsBackground = true,
                Name = $"CypressStdout-{Pid}"
            };
            _stdoutThread.Start();
        }

        process.EnableRaisingEvents = true;
        process.Exited += (_, _) => _onExit(Pid);
    }

    private void ReadStdout()
    {
        try
        {
            using var reader = Process.StandardOutput;
            while (!reader.EndOfStream)
            {
                string? line = reader.ReadLine();
                if (line != null)
                {
                    _onOutput(Pid, line);
                    try { _logWriter?.WriteLine(line); } catch { }
                }
            }
        }
        catch { }
    }

    public void EnableFileLog(string path)
    {
        try
        {
            Directory.CreateDirectory(Path.GetDirectoryName(path)!);
            _logWriter = new StreamWriter(path, append: true, Encoding.UTF8) { AutoFlush = true };
        }
        catch { }
    }

    public void SendCommand(string command)
    {
        if (_stdin != null && !Process.HasExited)
        {
            try
            {
                _stdin.WriteLine(command);
                _stdin.Flush();
            }
            catch { }
        }
    }

    public void Kill()
    {
        try
        {
            if (!Process.HasExited)
                Process.Kill();
        }
        catch { }
    }

    public void Dispose()
    {
        if (_disposed) return;
        _disposed = true;
        try { _logWriter?.Dispose(); } catch { }
        try { _stdin?.Dispose(); } catch { }
        try { Process.Dispose(); } catch { }
    }
}
