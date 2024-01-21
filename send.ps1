$serverAddress = "127.0.0.1"
$serverPort = 12345
$message = "Hello, UDP Server!"

$udpClient = New-Object System.Net.Sockets.UdpClient
$bytes = [System.Text.Encoding]::ASCII.GetBytes($message)
$udpClient.Send($bytes, $bytes.Length, $serverAddress, $serverPort)
$udpClient.Close()
