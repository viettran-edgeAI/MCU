#pragma once

const char index_html[] = R"rawliteral(
<!DOCTYPE html>
<html>
<head>
    <title>ESP32-CAM ML Inference</title>
    <meta name="viewport" content="width=device-width, initial-scale=1">
    <style>
        body { font-family: Arial; text-align: center; margin: 20px; background: #1a1a1a; color: white; }
        .container { max-width: 800px; margin: auto; }
        h1 { color: #4CAF50; }
        img { width: 100%; max-width: 640px; border: 3px solid #4CAF50; border-radius: 10px; }
        .info { 
            background: #2a2a2a; 
            padding: 20px; 
            margin: 20px auto; 
            border-radius: 10px;
            max-width: 640px;
        }
        .prediction { 
            font-size: 2em; 
            font-weight: bold; 
            color: #4CAF50; 
            margin: 10px 0;
        }
        .time { color: #888; }
        .status { 
            display: inline-block; 
            width: 12px; 
            height: 12px; 
            border-radius: 50%; 
            background: #4CAF50;
            animation: pulse 1s infinite;
        }
        @keyframes pulse {
            0%, 100% { opacity: 1; }
            50% { opacity: 0.3; }
        }
    </style>
</head>
<body>
    <div class="container">
        <h1>🤖 ESP32-CAM ML Inference</h1>
        <div class="info">
            <div>Prediction: <span class="prediction" id="pred">Loading...</span></div>
            <div class="time">Inference Time: <span id="time">--</span> ms</div>
            <div><span class="status"></span> Live</div>
        </div>
        <img id="stream" alt="Video stream loading...">
    </div>
    <script>
        console.log('Script loaded');
    const streamImg = document.getElementById('stream');
        const baseHost = window.location.hostname || window.location.host.split(':')[0];
        const isIPv6 = baseHost && baseHost.includes(':');
        const streamUrl = isIPv6
            ? `${window.location.protocol}//[${baseHost}]:81/stream`
            : `${window.location.protocol}//${baseHost}:81/stream`;
        streamImg.src = streamUrl;
        console.log('Stream URL:', streamUrl);

        function updateStatus() {
            const url = `/status?ts=${Date.now()}`;
            fetch(url, { cache: 'no-store' })
                .then(response => {
                    console.log('Status response:', response.status);
                    return response.json();
                })
                .then(data => {
                    console.log('Status data:', data);
                    document.getElementById('pred').textContent = data.prediction;
                    document.getElementById('time').textContent = data.inference_time;
                })
                .catch(err => {
                    console.error('Status fetch error:', err);
                    document.getElementById('pred').textContent = 'Error: ' + err.message;
                });
        }
        setInterval(updateStatus, 500);
        setTimeout(updateStatus, 100);
    </script>
</body>
</html>
)rawliteral";

