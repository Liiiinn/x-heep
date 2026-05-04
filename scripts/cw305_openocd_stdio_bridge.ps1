param(
    [string]$HostName = "127.0.0.1",
    [int]$Port = 3333
)

$ErrorActionPreference = "Stop"

Add-Type -TypeDefinition @"
using System;
using System.IO;
using System.Net.Sockets;
using System.Threading;

public static class TcpStdioBridge
{
    private static void Copy(Stream input, Stream output)
    {
        byte[] buffer = new byte[4096];
        while (true)
        {
            int n = input.Read(buffer, 0, buffer.Length);
            if (n <= 0)
            {
                return;
            }
            output.Write(buffer, 0, n);
            output.Flush();
        }
    }

    public static int Run(string host, int port)
    {
        using (TcpClient client = new TcpClient())
        {
            client.NoDelay = true;
            client.Connect(host, port);

            using (NetworkStream socket = client.GetStream())
            using (Stream stdin = Console.OpenStandardInput())
            using (Stream stdout = Console.OpenStandardOutput())
            {
                Exception stdinError = null;
                Thread stdinThread = new Thread(() => {
                    try
                    {
                        Copy(stdin, socket);
                    }
                    catch (Exception ex)
                    {
                        stdinError = ex;
                    }
                    try
                    {
                        client.Client.Shutdown(SocketShutdown.Send);
                    }
                    catch
                    {
                    }
                });

                stdinThread.IsBackground = true;
                stdinThread.Start();
                Copy(socket, stdout);

                if (stdinError != null)
                {
                    Console.Error.WriteLine(stdinError.Message);
                    return 1;
                }
            }
        }

        return 0;
    }
}
"@

try {
    exit [TcpStdioBridge]::Run($HostName, $Port)
}
catch {
    [Console]::Error.WriteLine($_.Exception.Message)
    exit 1
}
