param ($msg, $wc=2, $port=38080)

(Invoke-WebRequest -Headers @{} -Method POST `
                  -Body (@{message=$msg; w=$wc}|ConvertTo-Json) `
                  -Uri 127.0.0.1:$port/message `
                  -ContentType application/json).RawContent