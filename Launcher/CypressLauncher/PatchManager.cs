#nullable enable
using System;
using System.Diagnostics;
using System.IO;

namespace CypressLauncher;

public static class PatchManager
{
	public static bool EnsurePatched(
		MessageHandler.PVZGame game,
		string gameDirectory,
		string sourceExeName,
		string patchedExeName,
		Action<string, string> sendStatus)
	{
		string patchedPath = Path.Combine(gameDirectory, patchedExeName);
		if (File.Exists(patchedPath))
			return true;

		sendStatus("Creating patched executable (this might take a while)...", "info");

		string courgetteCmd = game == MessageHandler.PVZGame.BFN ? "-applybsdiff" : "-apply";
		var startInfo = new ProcessStartInfo
		{
			FileName = "courgette.exe",
			Arguments = $"{courgetteCmd} \"{Path.Combine(gameDirectory, sourceExeName)}\" {game}.patch \"{patchedPath}\"",
			Verb = "runas",
			UseShellExecute = true
		};

		try
		{
			var process = Process.Start(startInfo);
			process?.WaitForExit();
			if (process?.ExitCode != 0)
			{
				sendStatus("Patcher failed (Code: " + process?.ExitCode.ToString("X") + ")", "error");
				return false;
			}
			return true;
		}
		catch (Exception ex)
		{
			sendStatus("Failed to start courgette: " + ex.Message, "error");
			return false;
		}
	}
}
