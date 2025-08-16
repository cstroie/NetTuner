function scanNetworks() {
    document.getElementById('networks').innerHTML = 'Scanning for networks...';
    fetch('/api/wifiscan')
        .then(response => response.json())
        .then(data => {
            let html = '<h2>Available Networks:</h2>';
            data.forEach(network => {
                html += `<div class="network" onclick="selectNetwork('${network.ssid}')">${network.ssid} (${network.rssi}dBm)</div>`;
            });
            document.getElementById('networks').innerHTML = html;
        })
        .catch(error => {
            document.getElementById('networks').innerHTML = 'Error scanning networks';
            console.error('Error:', error);
        });
}

function selectNetwork(ssid) {
    document.getElementById('ssid').value = ssid;
}

document.getElementById('wifiForm').addEventListener('submit', function(e) {
    e.preventDefault();
    const ssid = document.getElementById('ssid').value;
    const password = document.getElementById('password').value;
    
    fetch('/api/wifisave', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ ssid: ssid, password: password })
    })
    .then(response => response.text())
    .then(data => {
        alert(data);
        if (data === 'WiFi configuration saved') {
            window.location.href = '/';
        }
    })
    .catch(error => {
        alert('Error saving configuration');
        console.error('Error:', error);
    });
});

// Scan networks on page load
scanNetworks();
