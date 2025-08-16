// Main functions
let streams = [];

/**
 * @brief Load streams from the server
 * Fetches the playlist from the server and updates the UI
 * Shows loading states and handles errors appropriately
 */
async function loadStreams() {
    // Show loading state
    const select = document.getElementById('streamSelect');
    if (select) {
        select.innerHTML = '<option value="">Loading streams...</option>';
        select.disabled = true;
    }
    
    const playlistBody = document.getElementById('playlistBody');
    if (playlistBody) {
        playlistBody.innerHTML = '<tr><td colspan="4">Loading streams...</td></tr>';
    }
    
    try {
        console.log('Loading streams from /api/streams');
        const response = await fetch('/api/streams');
        console.log('Streams response status:', response.status);
        if (!response.ok) {
            throw new Error('Network response was not ok');
        }
        streams = await response.json();
        console.log('Loaded streams:', streams);
        
        if (select) {
            select.innerHTML = '<option value="">Select a stream...</option>';
            streams.forEach(stream => {
                const option = document.createElement('option');
                option.value = stream.url;
                option.textContent = stream.name;
                option.dataset.name = stream.name;
                select.appendChild(option);
            });
            select.disabled = false;
        }
        
        // Also update playlist if on playlist page
        if (playlistBody) {
            renderPlaylist();
        }
    } catch (error) {
        console.error('Error loading streams:', error);
        if (select) {
            select.innerHTML = '<option value="">Error loading streams</option>';
            select.disabled = false;
        }
        if (playlistBody) {
            playlistBody.innerHTML = '<tr><td colspan="4">Error loading streams</td></tr>';
        }
    }
}

/**
 * @brief Show a toast notification
 * Displays a temporary message notification in the top-right corner
 * @param message The message to display
 * @param type The type of message (success, error, warning, info)
 */
function showToast(message, type = 'info') {
    // Create toast container if it doesn't exist
    let toastContainer = document.getElementById('toast-container');
    if (!toastContainer) {
        toastContainer = document.createElement('div');
        toastContainer.id = 'toast-container';
        toastContainer.style.cssText = `
            position: fixed;
            top: 20px;
            right: 20px;
            z-index: 10000;
            display: flex;
            flex-direction: column;
            gap: 10px;
        `;
        document.body.appendChild(toastContainer);
    }
    
    // Create toast element
    const toast = document.createElement('div');
    toast.style.cssText = `
        padding: 12px 20px;
        border-radius: 4px;
        color: white;
        font-weight: bold;
        box-shadow: 0 2px 10px rgba(0,0,0,0.2);
        animation: slideIn 0.3s, fadeOut 0.5s 2.5s forwards;
        max-width: 300px;
        word-wrap: break-word;
    `;
    
    // Set toast style based on type
    switch (type) {
        case 'success':
            toast.style.backgroundColor = '#4CAF50';
            break;
        case 'error':
            toast.style.backgroundColor = '#f44336';
            break;
        case 'warning':
            toast.style.backgroundColor = '#ff9800';
            break;
        default:
            toast.style.backgroundColor = '#2196F3';
    }
    
    toast.textContent = message;
    
    // Add toast to container
    toastContainer.appendChild(toast);
    
    // Remove toast after animation completes
    setTimeout(() => {
        if (toast.parentNode) {
            toast.parentNode.removeChild(toast);
        }
    }, 3000);
}

/**
 * @brief Add CSS styles for toast notifications
 * Injects the required CSS animations for toast notifications into the document head
 */
function addToastStyles() {
    const style = document.createElement('style');
    style.textContent = `
        @keyframes slideIn {
            from {
                transform: translateX(100%);
                opacity: 0;
            }
            to {
                transform: translateX(0);
                opacity: 1;
            }
        }
        
        @keyframes fadeOut {
            from {
                opacity: 1;
            }
            to {
                opacity: 0;
            }
        }
    `;
    document.head.appendChild(style);
}

// WebSocket connection
let ws = null;
let reconnectTimeout = null;
let isConnecting = false;
let reconnectAttempts = 0;
const maxReconnectAttempts = 10;
let heartbeatInterval = null;
let connectionStartTime = 0;
const connectionTimeout = 10000; // 10 seconds timeout for connection

function connectWebSocket() {
    // Clear any existing reconnection timeout
    if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
        reconnectTimeout = null;
    }
    
    // Clear any existing heartbeat interval
    if (heartbeatInterval) {
        clearInterval(heartbeatInterval);
        heartbeatInterval = null;
    }
    
    // Prevent multiple connection attempts
    if (isConnecting || (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) || 
        (ws && ws.readyState === WebSocket.CLOSING)) {
        return;
    }
    
    // Check if we've exceeded max reconnect attempts
    if (reconnectAttempts >= maxReconnectAttempts) {
        console.log('Max reconnect attempts reached. Stopping reconnection.');
        showToast('Connection failed. Please refresh the page.', 'error');
        return;
    }
    
    isConnecting = true;
    connectionStartTime = Date.now();
    
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    // WebSocket server runs on port 81, not the same port as HTTP server
    const host = window.location.hostname;
    const wsUrl = `${protocol}//${host}:81/`;
    
    try {
        ws = new WebSocket(wsUrl);
        
        // Set up connection timeout
        const connectionTimer = setTimeout(() => {
            if (ws && ws.readyState === WebSocket.CONNECTING) {
                console.log('WebSocket connection timeout');
                ws.close(); // Force close to trigger onclose handler
            }
        }, connectionTimeout);
        
        ws.onopen = function() {
            clearTimeout(connectionTimer);
            isConnecting = false;
            reconnectAttempts = 0; // Reset reconnect attempts on successful connection
            console.log('WebSocket connected');
            showToast('Connected to NetTuner', 'success');
            
            // Start heartbeat to detect disconnections
            heartbeatInterval = setInterval(() => {
                if (ws && ws.readyState === WebSocket.OPEN) {
                    try {
                        ws.send('ping');
                    } catch (e) {
                        console.error('Error sending ping:', e);
                    }
                }
            }, 25000); // Send ping every 25 seconds
        };
        
        ws.onmessage = function(event) {
            try {
                const data = JSON.parse(event.data);
                
                // Handle pong messages
                if (data.type === 'pong') {
                    return; // Ignore pong messages
                }
                
                // Handle status updates
                const status = data;
                console.log('Received status update:', status);
                const statusElement = document.getElementById('status');
                const currentElement = document.getElementById('currentStream');
                
                // Show toast notifications for status changes
                if (statusElement) {
                    const wasPlaying = statusElement.classList.contains('playing');
                    const isPlaying = status.playing;
                    
                    if (wasPlaying !== isPlaying) {
                        showToast(isPlaying ? 'Playback started' : 'Playback stopped', 
                                 isPlaying ? 'success' : 'info');
                    }
                }
                
                if (statusElement) {
                    statusElement.textContent = status.playing ? 'Playing' : 'Stopped';
                    statusElement.className = 'status ' + (status.playing ? 'playing' : 'stopped');
                }
                
                if (currentElement) {
                    currentElement.textContent = status.currentStreamName || 'No stream selected';
                }
                
                const volumeControl = document.getElementById('volume');
                const volumeValue = document.getElementById('volumeValue');
                
                if (volumeControl) {
                    volumeControl.value = status.volume;
                }
                
                if (volumeValue) {
                    volumeValue.textContent = status.volume + '%';
                }
            } catch (error) {
                console.error('Error parsing WebSocket message:', error);
            }
        };
        
        ws.onclose = function(event) {
            clearTimeout(connectionTimer);
            isConnecting = false;
            
            // Clear heartbeat interval on close
            if (heartbeatInterval) {
                clearInterval(heartbeatInterval);
                heartbeatInterval = null;
            }
            
            // Check if this was a clean close or an error
            if (event.wasClean) {
                console.log(`WebSocket closed cleanly, code=${event.code}, reason=${event.reason}`);
            } else {
                console.log('WebSocket connection died');
                reconnectAttempts++;
            }
            
            console.log('WebSocket disconnected. Attempt ' + reconnectAttempts + ' of ' + maxReconnectAttempts);
            showToast('Disconnected from NetTuner', 'warning');
            
            // Try to reconnect with exponential backoff, but reset counter on successful connection
            if (reconnectAttempts < maxReconnectAttempts) {
                const timeout = Math.min(1000 * Math.pow(2, reconnectAttempts), 30000);
                console.log(`Reconnecting in ${timeout}ms`);
                reconnectTimeout = setTimeout(connectWebSocket, timeout);
            } else {
                console.log('Max reconnect attempts reached. Stopping reconnection.');
                showToast('Connection failed. Please refresh the page.', 'error');
            }
        };
        
        ws.onerror = function(error) {
            clearTimeout(connectionTimer);
            console.error('WebSocket error:', error);
            // Don't set isConnecting to false here, let onclose handle it
            // This ensures we don't try to reconnect while still connecting
        };
    } catch (error) {
        isConnecting = false;
        reconnectAttempts++;
        console.error('Error creating WebSocket:', error);
        showToast('Failed to connect', 'error');
        
        // Try to reconnect with exponential backoff
        if (reconnectAttempts < maxReconnectAttempts) {
            const timeout = Math.min(1000 * Math.pow(2, reconnectAttempts), 30000);
            console.log(`Reconnecting in ${timeout}ms`);
            reconnectTimeout = setTimeout(connectWebSocket, timeout);
        } else {
            console.log('Max reconnect attempts reached. Stopping reconnection.');
            showToast('Connection failed. Please refresh the page.', 'error');
        }
    }
}

// Function to force reconnect
function forceReconnect() {
    if (ws) {
        ws.close();
    }
    // Reset attempts for manual reconnection
    reconnectAttempts = 0;
    connectWebSocket();
}

async function playStream() {
    const select = document.getElementById('streamSelect');
    const option = select.options[select.selectedIndex];
    const url = select.value;
    const name = option ? option.dataset.name : '';
    
    console.log('Playing stream:', { url, name });
    
    if (!url) {
        alert('Please select a stream');
        return;
    }
    
    // Show loading state
    const playButton = document.querySelector('button[onclick="playStream()"]');
    const originalText = playButton ? playButton.textContent : null;
    if (playButton) {
        playButton.textContent = 'Playing...';
        playButton.disabled = true;
    }
    
    try {
        const response = await fetch('/api/play', {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: JSON.stringify({ url: url, name: name })
        });
        console.log('Play response status:', response.status);
        if (!response.ok) {
            const errorText = await response.text();
            throw new Error('Failed to play stream: ' + errorText);
        }
        showToast('Stream started', 'success');
    } catch (error) {
        console.error('Error playing stream:', error);
        showToast('Error playing stream: ' + error.message, 'error');
    } finally {
        // Restore button state
        if (playButton) {
            playButton.textContent = originalText || 'Play';
            playButton.disabled = false;
        }
    }
}

async function stopStream() {
    // Show loading state
    const stopButton = document.querySelector('button[onclick="stopStream()"]');
    const originalText = stopButton ? stopButton.textContent : null;
    if (stopButton) {
        stopButton.textContent = 'Stopping...';
        stopButton.disabled = true;
    }
    
    try {
        console.log('Stopping current stream');
        const response = await fetch('/api/stop', { 
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            }
        });
        console.log('Stop response status:', response.status);
        if (response.ok) {
            showToast('Stream stopped', 'info');
        }
    } catch (error) {
        console.error('Error stopping stream:', error);
        showToast('Error stopping stream: ' + error.message, 'error');
    } finally {
        // Restore button state
        if (stopButton) {
            stopButton.textContent = originalText || 'Stop';
            stopButton.disabled = false;
        }
    }
}

async function setVolume(volume) {
    // Show loading state
    const volumeControl = document.getElementById('volume');
    if (volumeControl) {
        volumeControl.disabled = true;
    }
    
    try {
        console.log('Setting volume to:', volume);
        const response = await fetch('/api/volume', {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: JSON.stringify({ volume: volume })
        });
        console.log('Volume response status:', response.status);
        if (response.ok) {
            const volumeValue = document.getElementById('volumeValue');
            if (volumeValue) {
                volumeValue.textContent = volume + '%';
            }
            showToast('Volume set to ' + volume + '%', 'info');
        }
    } catch (error) {
        console.error('Error setting volume:', error);
        showToast('Error setting volume: ' + error.message, 'error');
    } finally {
        // Restore control state
        if (volumeControl) {
            volumeControl.disabled = false;
        }
    }
}

// Playlist functions
function renderPlaylist() {
    const tbody = document.getElementById('playlistBody');
    if (!tbody) return;
    
    tbody.innerHTML = '';
    
    streams.forEach((stream, index) => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td data-label="Order">${index + 1}</td>
            <td data-label="Name"><input type="text" value="${stream.name}" onchange="updateStream(${index}, 'name', this.value)"></td>
            <td data-label="URL"><input type="text" value="${stream.url}" onchange="updateStream(${index}, 'url', this.value)"></td>
            <td data-label="Actions" class="actions">
                <button class="btn-danger" onclick="deleteStream(${index})">Delete</button>
            </td>
        `;
        tbody.appendChild(row);
    });
}

function addStream() {
    const name = document.getElementById('name');
    const url = document.getElementById('url');
    
    if (!name || !url) return;
    
    console.log('Adding stream:', { name: name.value, url: url.value });
    
    if (!name.value || !url.value) {
        alert('Please enter both name and URL');
        return;
    }
    
    // Validate URL format
    if (!url.value.startsWith('http://') && !url.value.startsWith('https://')) {
        alert('Please enter a valid URL starting with http:// or https://');
        return;
    }
    
    streams.push({ name: name.value, url: url.value });
    renderPlaylist();
    
    // Clear form
    name.value = '';
    url.value = '';
}

function updateStream(index, field, value) {
    if (streams[index]) {
        // Validate URL format if updating URL field
        if (field === 'url' && value) {
            if (!value.startsWith('http://') && !value.startsWith('https://')) {
                showToast('Invalid URL format. Must start with http:// or https://', 'error');
                return;
            }
        }
        streams[index][field] = value;
    }
}

function deleteStream(index) {
    if (confirm('Are you sure you want to delete this stream?')) {
        streams.splice(index, 1);
        renderPlaylist();
    }
}

async function savePlaylist() {
    // Show loading state
    const saveButton = document.querySelector('button[onclick="savePlaylist()"]');
    const originalText = saveButton ? saveButton.textContent : null;
    if (saveButton) {
        saveButton.textContent = 'Saving...';
        saveButton.disabled = true;
    }
    
    // Convert streams to JSON
    const jsonData = JSON.stringify(streams);
    
    console.log('Saving playlist:', jsonData);
    
    try {
        const response = await fetch('/api/streams', {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: jsonData
        });
        
        console.log('Save playlist response status:', response.status);
        
        if (response.ok) {
            showToast('Playlist saved successfully!', 'success');
        } else {
            const error = await response.text();
            console.error('Error saving playlist:', error);
            showToast('Error saving playlist: ' + error, 'error');
        }
    } catch (error) {
        console.error('Error saving playlist:', error);
        showToast('Error saving playlist: ' + error.message, 'error');
    } finally {
        // Restore button state
        if (saveButton) {
            saveButton.textContent = originalText || 'Save Playlist';
            saveButton.disabled = false;
        }
    }
}

async function uploadJSON() {
    const fileInput = document.getElementById('playlistFile');
    const file = fileInput.files[0];
    
    console.log('Uploading JSON playlist file:', file);
    
    if (!file) {
        alert('Please select a playlist file');
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.json')) {
        alert('Please select a JSON file');
        return;
    }
    
    // Show loading state
    const uploadButton = document.querySelector('button[onclick="uploadJSON()"]');
    const originalText = uploadButton ? uploadButton.textContent : null;
    if (uploadButton) {
        uploadButton.textContent = 'Uploading...';
        uploadButton.disabled = true;
    }
    
    // Read file content
    const reader = new FileReader();
    reader.onload = async function(e) {
        const fileContent = e.target.result;
        console.log('File content:', fileContent);
        
        try {
            const response = await fetch('/api/streams', {
                method: 'POST',
                headers: { 
                    'Content-Type': 'application/json',
                    'X-Requested-With': 'XMLHttpRequest'
                },
                body: fileContent
            });
            
            console.log('Upload response status:', response.status);
            const message = await response.text();
            console.log('Upload response message:', message);
            showToast(message, response.ok ? 'success' : 'error');
            
            if (response.ok) {
                // Reload streams after successful upload
                loadStreams();
                // Clear file input
                fileInput.value = '';
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
            showToast('Error uploading playlist file: ' + error.message, 'error');
        } finally {
            // Restore button state
            if (uploadButton) {
                uploadButton.textContent = originalText || 'Upload JSON';
                uploadButton.disabled = false;
            }
        }
    };
    reader.readAsText(file);
}

async function uploadM3U() {
    const fileInput = document.getElementById('playlistFile');
    const file = fileInput.files[0];
    
    console.log('Uploading M3U playlist file:', file);
    
    if (!file) {
        alert('Please select a playlist file');
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.m3u') && !fileName.endsWith('.m3u8')) {
        alert('Please select an M3U file');
        return;
    }
    
    // Show loading state
    const uploadButton = document.querySelector('button[onclick="uploadM3U()"]');
    const originalText = uploadButton ? uploadButton.textContent : null;
    if (uploadButton) {
        uploadButton.textContent = 'Uploading...';
        uploadButton.disabled = true;
    }
    
    // Read file content
    const reader = new FileReader();
    reader.onload = async function(e) {
        const fileContent = e.target.result;
        console.log('File content:', fileContent);
        
        // Convert M3U to JSON
        const jsonData = convertM3UToJSON(fileContent);
        console.log('JSON content:', jsonData);
    
        try {
            const response = await fetch('/api/streams', {
                method: 'POST',
                headers: { 
                    'Content-Type': 'application/json',
                    'X-Requested-With': 'XMLHttpRequest'
                },
                body: jsonData
            });
            
            console.log('Upload response status:', response.status);
            const message = await response.text();
            console.log('Upload response message:', message);
            showToast(message, response.ok ? 'success' : 'error');
            
            if (response.ok) {
                // Reload streams after successful upload
                loadStreams();
                // Clear file input
                fileInput.value = '';
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
            showToast('Error uploading playlist file: ' + error.message, 'error');
        } finally {
            // Restore button state
            if (uploadButton) {
                uploadButton.textContent = originalText || 'Upload M3U';
                uploadButton.disabled = false;
            }
        }
    };
    reader.readAsText(file);
}

async function downloadJSON() {
    try {
        const response = await fetch('/api/streams', {
            headers: {
                'Accept': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            }
        });
        console.log('Download JSON response status:', response.status);
        
        if (response.ok) {
            const jsonContent = await response.text();
            const blob = new Blob([jsonContent], { type: 'application/json' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'playlist.json';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
            alert('Error downloading JSON: ' + error);
        }
    } catch (error) {
        console.error('Error downloading JSON:', error);
        alert('Error downloading JSON: ' + error.message);
    }
}

async function downloadM3U() {
    try {
        const response = await fetch('/api/streams', {
            headers: {
                'Accept': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            }
        });
        console.log('Download JSON response status:', response.status);
        
        if (response.ok) {
            const jsonData = await response.json();
            const m3uContent = convertJSONToM3U(jsonData);
            const blob = new Blob([m3uContent], { type: 'audio/x-mpegurl' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'playlist.m3u';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
            alert('Error downloading playlist: ' + error);
        }
    } catch (error) {
        console.error('Error downloading playlist:', error);
        alert('Error downloading playlist: ' + error.message);
    }
}

// Convert M3U content to JSON
function convertM3UToJSON(m3uContent) {
    const lines = m3uContent.split('\n');
    const streams = [];
    let currentName = '';
    
    for (let i = 0; i < lines.length; i++) {
        const line = lines[i].trim();
        
        if (line.length === 0) continue;
        
        if (line.startsWith('#EXTINF:')) {
            // Extract name from EXTINF line
            const commaPos = line.indexOf(',');
            if (commaPos !== -1) {
                currentName = line.substring(commaPos + 1).trim();
            }
        } else if (!line.startsWith('#') && (line.startsWith('http://') || line.startsWith('https://'))) {
            // This is a URL line
            streams.push({
                name: currentName || ('Stream ' + (streams.length + 1)),
                url: line
            });
            currentName = ''; // Reset for next entry
        }
    }
    
    return JSON.stringify(streams);
}

// Convert JSON content to M3U
function convertJSONToM3U(jsonData) {
    let m3uContent = '#EXTM3U\n';
    
    jsonData.forEach(item => {
        if (item.name && item.url) {
            m3uContent += '#EXTINF:-1,' + item.name + '\n';
            m3uContent += item.url + '\n';
        }
    });
    
    return m3uContent;
}

// WiFi functions
function scanNetworks() {
    const networksDiv = document.getElementById('networks');
    if (!networksDiv) return;
    
    // Show loading state
    networksDiv.innerHTML = 'Scanning for networks...';
    
    // Disable scan button during scan
    const scanButton = document.querySelector('button[onclick="scanNetworks()"]');
    const originalText = scanButton ? scanButton.textContent : null;
    if (scanButton) {
        scanButton.textContent = 'Scanning...';
        scanButton.disabled = true;
    }
    
    fetch('/api/wifiscan')
        .then(response => response.json())
        .then(data => {
            let html = '<h2>Available Networks:</h2>';
            data.forEach(network => {
                html += `<div class="network" onclick="selectNetwork('${network.ssid}')">${network.ssid} (${network.rssi}dBm)</div>`;
            });
            networksDiv.innerHTML = html;
        })
        .catch(error => {
            networksDiv.innerHTML = 'Error scanning networks';
            console.error('Error:', error);
        })
        .finally(() => {
            // Restore scan button
            if (scanButton) {
                scanButton.textContent = originalText || 'Refresh Networks';
                scanButton.disabled = false;
            }
        });
}

function selectNetwork(ssid) {
    const ssidInput = document.getElementById('ssid');
    if (ssidInput) {
        ssidInput.value = ssid;
    }
}

// Initialize functions
function initMainPage() {
    loadStreams();
    connectWebSocket();
    
    // Add visibility change listener to handle tab switching
    document.addEventListener('visibilitychange', function() {
        if (document.visibilityState === 'visible') {
            // Page became visible, check if we need to reconnect
            if (!ws || (ws.readyState !== WebSocket.OPEN && ws.readyState !== WebSocket.CONNECTING)) {
                console.log('Page became visible, checking WebSocket connection');
                connectWebSocket();
            }
        }
    });
}

function initPlaylistPage() {
    loadStreams();
}

function initWiFiPage() {
    // Setup form submit handler
    const wifiForm = document.getElementById('wifiForm');
    if (wifiForm) {
        wifiForm.addEventListener('submit', function(e) {
            e.preventDefault();
            const ssidInput = document.getElementById('ssid');
            const passwordInput = document.getElementById('password');
            
            if (!ssidInput || !passwordInput) return;
            
            const ssid = ssidInput.value;
            const password = passwordInput.value;
            
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
    }
    
    // Scan networks on page load
    scanNetworks();
}

// Initialize based on current page
document.addEventListener('DOMContentLoaded', function() {
    // Add toast styles to all pages
    addToastStyles();
    
    if (document.getElementById('streamSelect')) {
        initMainPage();
    } else if (document.getElementById('playlistBody')) {
        initPlaylistPage();
    }
});

// WiFi functions
function initWiFiPage() {
    // Setup form submit handler
    const wifiForm = document.getElementById('wifiForm');
    if (wifiForm) {
        wifiForm.addEventListener('submit', function(e) {
            e.preventDefault();
            const ssidInput = document.getElementById('ssid');
            const passwordInput = document.getElementById('password');
            
            if (!ssidInput || !passwordInput) return;
            
            const ssid = ssidInput.value;
            const password = passwordInput.value;
            
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
    }
    
    // Scan networks on page load
    scanNetworks();
}

// Config functions
function initConfigPage() {
    // Load current configuration
    fetch('/api/config')
        .then(response => response.json())
        .then(config => {
            document.getElementById('output').value = config.output || 0;
        })
        .catch(error => console.error('Error loading config:', error));
    
    // Handle form submission
    document.getElementById('configForm').addEventListener('submit', function(e) {
        e.preventDefault();
        const output = document.getElementById('output').value;
        
        fetch('/api/config', {
            method: 'POST',
            headers: { 'Content-Type': 'application/json' },
            body: JSON.stringify({ output: parseInt(output) })
        })
        .then(response => response.text())
        .then(data => {
            alert(data);
            // Reload the page to reflect changes
            location.reload();
        })
        .catch(error => {
            alert('Error saving configuration');
            console.error('Error:', error);
        });
    });
}
