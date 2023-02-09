param ($hostname="127.0.0.1", $port=38080)

(Invoke-WebRequest -Uri ${hostname}:$port/messages).RawContent
