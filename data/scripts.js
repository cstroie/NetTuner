// Main functions
let streams = [];
let bass = 0;
let midrange = 0;
let treble = 0;

// Theme handling
function initTheme() {
    const savedTheme = localStorage.getItem('theme') || 'light';
    document.documentElement.setAttribute('data-theme', savedTheme);
    updateThemeToggle();
}

function toggleTheme() {
    const currentTheme = document.documentElement.getAttribute('data-theme');
    const newTheme = currentTheme === 'dark' ? 'light' : 'dark';
    document.documentElement.setAttribute('data-theme', newTheme);
    localStorage.setItem('theme', newTheme);
    updateThemeToggle();
}

function updateThemeToggle() {
    const themeToggle = document.getElementById('themeToggle');
    const currentTheme = document.documentElement.getAttribute('data-theme');
    if (themeToggle) {
        themeToggle.textContent = currentTheme === 'dark' ? '◑' : '◐';
        themeToggle.title = currentTheme === 'dark' ? 'Switch to light theme' : 'Switch to dark theme';
    }
}

// Add event listener for theme toggle
document.addEventListener('DOMContentLoaded', function() {
    const themeToggle = document.getElementById('themeToggle');
    if (themeToggle) {
        themeToggle.addEventListener('click', toggleTheme);
    }
    initTheme();
});

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
            throw new Error(`Failed to load streams: ${response.status} ${response.statusText}`);
        }
        const data = await response.json();
        
        // Validate that we received an array
        if (!Array.isArray(data)) {
            throw new Error('Invalid response format from server: expected array of streams');
        }
        
        streams = data;
        console.log('Loaded streams:', streams);
        
        if (select) {
            select.innerHTML = '<option value="">Select a stream...</option>';
            streams.forEach(stream => {
                // Validate stream object
                if (!stream || typeof stream !== 'object' || !stream.url || !stream.name) {
                    console.warn('Skipping invalid stream object:', stream);
                    return;
                }
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
        const errorMessage = error.message || 'Unknown error occurred';
        if (select) {
            select.innerHTML = '<option value="">Error loading streams</option>';
            select.disabled = false;
        }
        if (playlistBody) {
            playlistBody.innerHTML = `<tr><td colspan="4">Error loading streams: ${errorMessage}</td></tr>`;
        }
        showToast(`Error loading streams: ${errorMessage}. Please refresh the page or check your connection.`, 'error');
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
    
    // Limit the number of toasts to prevent memory leaks
    const maxToasts = 5;
    while (toastContainer.children.length >= maxToasts) {
        toastContainer.removeChild(toastContainer.firstChild);
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
            toast.style.backgroundColor = 'var(--pico-color-success)';
            break;
        case 'error':
            toast.style.backgroundColor = 'var(--pico-color-danger)';
            break;
        case 'warning':
            toast.style.backgroundColor = 'var(--pico-color-warning)';
            break;
        default:
            toast.style.backgroundColor = 'var(--pico-color-primary)';
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

// Add a connection lock to prevent race conditions
let connectionLock = false;

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
    
    // Prevent multiple connection attempts with atomic check
    if (connectionLock || isConnecting) {
        return;
    }
    
    // More comprehensive state check
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return;
    }
    
    // Check if we've exceeded max reconnect attempts
    if (reconnectAttempts >= maxReconnectAttempts) {
        console.log('Max reconnect attempts reached. Stopping reconnection.');
        showToast('Connection failed. Please refresh the page.', 'error');
        return;
    }
    
    // Acquire connection lock
    connectionLock = true;
    isConnecting = true;
    connectionStartTime = Date.now();
    
    const protocol = window.location.protocol === 'https:' ? 'wss:' : 'ws:';
    // WebSocket server runs on port 81, not the same port as HTTP server
    const host = window.location.hostname;
    const wsUrl = `${protocol}//${host}:81/`;
    
    // Ensure proper cleanup of previous connection
    if (ws) {
        ws.onopen = null;
        ws.onmessage = null;
        ws.onclose = null;
        ws.onerror = null;
        if (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING) {
            ws.close();
        }
        ws = null;
    }
    
    try {
        ws = new WebSocket(wsUrl);
        
        // Set up connection timeout with proper cleanup
        let connectionTimer = null;
        const setupConnectionTimeout = () => {
            connectionTimer = setTimeout(() => {
                if (ws && ws.readyState === WebSocket.CONNECTING) {
                    console.log('WebSocket connection timeout');
                    // Force close to trigger onclose handler
                    if (ws) {
                        ws.onclose = null; // Prevent reconnection trigger
                        ws.close();
                        ws = null;
                    }
                    // Schedule reconnection
                    reconnectAttempts++;
                    isConnecting = false;
                    connectionLock = false;
                    if (reconnectAttempts < maxReconnectAttempts) {
                        const timeout = Math.min(1000 * Math.pow(2, reconnectAttempts), 30000);
                        console.log(`Reconnecting in ${timeout}ms`);
                        reconnectTimeout = setTimeout(connectWebSocket, timeout);
                    } else {
                        console.log('Max reconnect attempts reached. Stopping reconnection.');
                        showToast('Connection failed. Please refresh the page.', 'error');
                    }
                }
                // Always clear timer reference
                connectionTimer = null;
            }, connectionTimeout);
        };
        
        setupConnectionTimeout();
        
        ws.onopen = function() {
            if (connectionTimer) {
                clearTimeout(connectionTimer);
                connectionTimer = null;
            }
            isConnecting = false;
            connectionLock = false;
            reconnectAttempts = 0; // Reset reconnect attempts on successful connection
            console.log('WebSocket connected');
            showToast('Connected to NetTuner', 'success');
            
        };
        
        ws.onmessage = function(event) {
            try {
                const data = JSON.parse(event.data);
                
                // Handle status updates
                const status = data;
                console.log('Received status update:', status);
                
                // Update status element
                const statusElement = document.getElementById('status');
                if (statusElement) {
                    statusElement.textContent = status.playing ? 'Playing' : 'Stopped';
                    statusElement.className = 'status ' + (status.playing ? 'playing' : 'stopped');
                    
                    // Show toast notifications for status changes
                    const wasPlaying = statusElement.classList.contains('playing');
                    const isPlaying = status.playing;
                    
                    if (wasPlaying !== isPlaying) {
                        showToast(isPlaying ? 'Playback started' : 'Playback stopped', 
                                 isPlaying ? 'success' : 'info');
                    }
                }
                
                // Update stream name element
                const streamNameElement = document.getElementById('currentStreamName');
                if (streamNameElement) {
                    streamNameElement.textContent = status.currentStreamName || 'No station selected';
                }
                
                // Update stream info element
                const currentElement = document.getElementById('currentStream');
                if (currentElement) {
                    if (status.playing) {
                        // Show stream title when playing
                        let displayText = status.streamTitle || 'Unknown Stream';
                        if (status.bitrate) {
                            displayText += ' (' + status.bitrate + ' kbps)';
                        }
                        currentElement.textContent = displayText;
                    } else {
                        currentElement.textContent = 'No stream selected';
                    }
                }
                
                // Update volume controls
                const volumeControl = document.getElementById('volume');
                const volumeValue = document.getElementById('volumeValue');
                
                if (volumeControl) {
                    volumeControl.value = status.volume;
                }
                
                if (volumeValue) {
                    volumeValue.textContent = status.volume + '%';
                }
                
                // Update tone controls
                const bassControl = document.getElementById('bass');
                const bassValue = document.getElementById('bassValue');
                const midrangeControl = document.getElementById('midrange');
                const midrangeValue = document.getElementById('midrangeValue');
                const trebleControl = document.getElementById('treble');
                const trebleValue = document.getElementById('trebleValue');
                
                if (bassControl && status.bass !== undefined) {
                    bassControl.value = status.bass;
                    bass = status.bass;
                }
                
                if (bassValue && status.bass !== undefined) {
                    bassValue.textContent = status.bass + 'dB';
                }
                
                if (midrangeControl && status.midrange !== undefined) {
                    midrangeControl.value = status.midrange;
                    midrange = status.midrange;
                }
                
                if (midrangeValue && status.midrange !== undefined) {
                    midrangeValue.textContent = status.midrange + 'dB';
                }
                
                if (trebleControl && status.treble !== undefined) {
                    trebleControl.value = status.treble;
                    treble = status.treble;
                }
                
                if (trebleValue && status.treble !== undefined) {
                    trebleValue.textContent = status.treble + 'dB';
                }
            } catch (error) {
                console.error('Error parsing WebSocket message:', error);
            }
        };
        
        ws.onclose = function(event) {
            if (connectionTimer) {
                clearTimeout(connectionTimer);
                connectionTimer = null;
            }
            
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
            
            // Release connection lock and reset connecting flag
            isConnecting = false;
            connectionLock = false;
            
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
            if (connectionTimer) {
                clearTimeout(connectionTimer);
                connectionTimer = null;
            }
            console.error('WebSocket error:', error);
            // Don't set isConnecting to false here, let onclose handle it
            // This ensures we don't try to reconnect while still connecting
        };
    } catch (error) {
        if (connectionTimer) {
            clearTimeout(connectionTimer);
        }
        reconnectAttempts++;
        console.error('Error creating WebSocket:', error);
        showToast('Failed to connect', 'error');
        
        // Release connection lock and reset connecting flag
        isConnecting = false;
        connectionLock = false;
        
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
    const { select, url, name } = getSelectedStream();
    if (!select) {
        showToast('Stream selection not available', 'error');
        return;
    }
    
    if (!validateStreamSelection(url)) {
        return;
    }
    
    // Show loading state
    const playButton = document.querySelector('button[onclick="playStream()"]');
    const originalText = playButton ? playButton.textContent : 'Play';
    if (playButton) {
        playButton.textContent = 'Playing...';
        playButton.disabled = true;
    }
    
    try {
        await sendPlayRequest(url, name);
        showToast('Stream started successfully', 'success');
    } catch (error) {
        handlePlayError(error);
    } finally {
        // Restore button state
        if (playButton) {
            playButton.textContent = originalText;
            playButton.disabled = false;
        }
    }
}

function getSelectedStream() {
    const select = document.getElementById('streamSelect');
    if (!select) {
        return { select: null, url: null, name: null };
    }
    
    const option = select.options[select.selectedIndex];
    const url = select.value;
    const name = option ? option.dataset.name : '';
    
    console.log('Playing stream:', { url, name });
    return { select, url, name };
}

function validateStreamSelection(url) {
    if (!url) {
        showToast('Please select a stream', 'warning');
        return false;
    }
    
    // Validate URL format
    try {
        new URL(url);
    } catch (e) {
        showToast('Invalid URL format', 'error');
        return false;
    }
    
    return true;
}

async function sendPlayRequest(url, name) {
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
        throw new Error(`Failed to play stream: ${response.status} ${response.statusText} - ${errorText}`);
    }
    const result = await response.json();
    if (result.status !== 'success') {
        throw new Error(result.message || 'Failed to play stream');
    }
}

function handlePlayError(error) {
    console.error('Error playing stream:', error);
    const errorMessage = error.message || 'Unknown error occurred';
    showToast('Error playing stream: ' + errorMessage + '. Please check the stream URL and try again.', 'error');
}

function playSelectedStream() {
    const { select, url, name } = getSelectedStream();
    if (!select || select.selectedIndex === 0) {
        return; // No stream selected or it's the placeholder option
    }
    
    if (!validateStreamSelection(url)) {
        return;
    }
    
    console.log('Playing selected stream:', { url, name });
    
    // Play the selected stream without showing toast
    sendPlayRequest(url, name)
        .catch(error => {
            console.error('Error playing stream:', error);
            const errorMessage = error.message || 'Unknown error occurred';
            showToast('Error playing stream: ' + errorMessage + '. Please check the stream URL and try again.', 'error');
        });
}

async function stopStream() {
    // Show loading state
    const stopButton = document.querySelector('button[onclick="stopStream()"]');
    const originalText = stopButton ? stopButton.textContent : 'Stop';
    if (stopButton) {
        stopButton.textContent = 'Stopping...';
        stopButton.disabled = true;
    } else {
        // If no button found, show toast immediately
        showToast('Stopping stream...', 'info');
    }
    
    try {
        await sendStopRequest();
        showToast('Stream stopped successfully', 'info');
    } catch (error) {
        handleStopError(error);
    } finally {
        // Restore button state
        if (stopButton) {
            stopButton.textContent = originalText;
            stopButton.disabled = false;
        }
    }
}

async function sendStopRequest() {
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
        const result = await response.json();
        if (result.status !== 'success') {
            throw new Error(result.message || 'Failed to stop stream');
        }
    } else {
        throw new Error(`Failed to stop stream: ${response.status} ${response.statusText}`);
    }
}

function handleStopError(error) {
    console.error('Error stopping stream:', error);
    const errorMessage = error.message || 'Unknown error occurred';
    showToast('Error stopping stream: ' + errorMessage + '. Please try again.', 'error');
}

// Wrapper function for volume change to handle errors in inline event handlers
function handleVolumeChange(volume) {
    setVolume(volume).catch(error => {
        console.error('Error in volume change:', error);
        showToast('Error setting volume: ' + error.message, 'error');
    });
}

// Wrapper function for tone change to handle errors in inline event handlers
function handleToneChange(type, value) {
    setTone(type, value).catch(error => {
        console.error('Error in tone change:', error);
        showToast('Error setting ' + type + ': ' + error.message, 'error');
    });
}

async function setVolume(volume) {
    // Validate volume parameter
    const volumeNum = parseInt(volume, 10);
    if (isNaN(volumeNum) || volumeNum < 0 || volumeNum > 100) {
        console.error('Invalid volume value:', volume);
        showToast('Invalid volume value. Must be between 0 and 100.', 'error');
        return;
    }
    
    // Show loading state
    const volumeControl = document.getElementById('volume');
    const volumeValue = document.getElementById('volumeValue');
    const originalVolume = volumeControl ? volumeControl.value : '50';
    
    if (volumeControl) {
        volumeControl.disabled = true;
    } else {
        console.warn('Volume control not found');
        return;
    }
    
    try {
        console.log('Setting volume to:', volumeNum);
        const response = await fetch('/api/volume', {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: JSON.stringify({ volume: volumeNum })
        });
        console.log('Volume response status:', response.status);
        if (response.ok) {
            const result = await response.json();
            if (result.status === 'success') {
                if (volumeValue) {
                    volumeValue.textContent = volumeNum + '%';
                }
                showToast(result.message || 'Volume set to ' + volumeNum + '%', 'info');
            } else {
                throw new Error(result.message || 'Failed to set volume');
            }
        } else {
            throw new Error(`Failed to set volume: ${response.status} ${response.statusText}`);
        }
    } catch (error) {
        console.error('Error setting volume:', error);
        const errorMessage = error.message || 'Unknown error occurred';
        showToast('Error setting volume: ' + errorMessage + '. Please try again.', 'error');
        // Restore original volume value on error
        if (volumeControl) {
            volumeControl.value = originalVolume;
        }
        if (volumeValue) {
            volumeValue.textContent = originalVolume + '%';
        }
        throw error; // Re-throw to allow caller to handle
    } finally {
        // Restore control state
        if (volumeControl) {
            volumeControl.disabled = false;
        }
    }
}

async function setTone(type, value) {
    // Validate tone parameter
    const toneValue = parseInt(value, 10);
    if (isNaN(toneValue) || toneValue < -6 || toneValue > 6) {
        console.error('Invalid ' + type + ' value:', value);
        showToast('Invalid ' + type + ' value. Must be between -6 and 6.', 'error');
        return;
    }
    
    // Show loading state
    const toneControl = document.getElementById(type);
    const toneValueElement = document.getElementById(type + 'Value');
    const originalValue = toneControl ? toneControl.value : '0';
    
    if (toneControl) {
        toneControl.disabled = true;
    } else {
        console.warn(type + ' control not found');
        return;
    }
    
    try {
        console.log('Setting ' + type + ' to:', toneValue);
        const response = await fetch('/api/tone', {
            method: 'POST',
            headers: { 
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: JSON.stringify({ [type]: toneValue })
        });
        console.log(type + ' response status:', response.status);
        if (response.ok) {
            const result = await response.json();
            if (result.status === 'success') {
                if (toneValueElement) {
                    toneValueElement.textContent = toneValue + 'dB';
                }
                showToast(result.message || type.charAt(0).toUpperCase() + type.slice(1) + ' set to ' + toneValue + 'dB', 'info');
                // Update global variable
                if (type === 'bass') {
                    bass = toneValue;
                } else if (type === 'midrange') {
                    midrange = toneValue;
                } else if (type === 'treble') {
                    treble = toneValue;
                }
            } else {
                throw new Error(result.message || 'Failed to set ' + type);
            }
        } else {
            throw new Error(`Failed to set ${type}: ${response.status} ${response.statusText}`);
        }
    } catch (error) {
        console.error('Error setting ' + type + ':', error);
        const errorMessage = error.message || 'Unknown error occurred';
        showToast('Error setting ' + type + ': ' + errorMessage + '. Please try again.', 'error');
        // Restore original value on error
        if (toneControl) {
            toneControl.value = originalValue;
        }
        if (toneValueElement) {
            toneValueElement.textContent = originalValue + 'dB';
        }
        throw error; // Re-throw to allow caller to handle
    } finally {
        // Restore control state
        if (toneControl) {
            toneControl.disabled = false;
        }
    }
}

async function playInstantStream() {
    const urlInput = document.getElementById('instantUrl');
    const url = urlInput.value.trim();
    
    if (!url) {
        showToast('Please enter a stream URL', 'warning');
        urlInput.focus();
        return;
    }
    
    // Validate URL format
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
        showToast('Please enter a valid URL starting with http:// or https://', 'warning');
        urlInput.focus();
        return;
    }
    
    try {
        new URL(url);
    } catch (e) {
        showToast('Please enter a valid URL', 'warning');
        urlInput.focus();
        return;
    }
    
    // Show loading state
    const playButton = document.querySelector('.instant-play button');
    const originalText = playButton ? playButton.textContent : 'Instant Play';
    if (playButton) {
        playButton.textContent = 'Playing...';
        playButton.disabled = true;
    }
    
    try {
        await sendPlayRequest(url, 'Instant Stream');
        showToast('Stream started successfully', 'success');
        // Clear the input field after successful play
        urlInput.value = '';
    } catch (error) {
        handlePlayError(error);
    } finally {
        // Restore button state
        if (playButton) {
            playButton.textContent = originalText;
            playButton.disabled = false;
        }
    }
}

// Playlist functions
function renderPlaylist() {
    const tbody = document.getElementById('playlistBody');
    if (!tbody) {
        console.warn('Playlist body not found');
        return;
    }
    
    tbody.innerHTML = '';
    
    if (streams.length === 0) {
        const row = document.createElement('tr');
        row.innerHTML = '<td colspan="4" class="empty-playlist">No streams in playlist. Add some streams to get started!</td>';
        tbody.appendChild(row);
        return;
    }
    
    streams.forEach((stream, index) => {
        const row = document.createElement('tr');
        row.innerHTML = `
            <td data-label="Order">${index + 1}</td>
            <td data-label="Name"><input type="text" value="${escapeHtml(stream.name)}" onchange="updateStream(${index}, 'name', this.value)"></td>
            <td data-label="URL"><input type="text" value="${escapeHtml(stream.url)}" onchange="updateStream(${index}, 'url', this.value)"></td>
            <td data-label="Actions" class="actions">
                <button class="secondary" onclick="deleteStream(${index})">Delete</button>
            </td>
        `;
        tbody.appendChild(row);
    });
}

// Helper function to escape HTML entities
function escapeHtml(unsafe) {
    if (typeof unsafe !== 'string') return '';
    return unsafe
        .replace(/&/g, "&amp;")
        .replace(/</g, "&lt;")
        .replace(/>/g, "&gt;")
        .replace(/"/g, "&quot;")
        .replace(/'/g, "&#039;");
}

function addStream() {
    const name = document.getElementById('name');
    const url = document.getElementById('url');
    
    if (!name || !url) {
        showToast('Form elements not found', 'error');
        return;
    }
    
    console.log('Adding stream:', { name: name.value, url: url.value });
    
    // Trim and validate name
    const trimmedName = name.value.trim();
    if (!trimmedName) {
        showToast('Please enter a stream name', 'warning');
        name.focus();
        return;
    }
    
    // Validate name length
    if (trimmedName.length > 128) {
        showToast('Stream name must be 128 characters or less', 'warning');
        name.focus();
        return;
    }
    
    // Trim and validate URL
    const trimmedUrl = url.value.trim();
    if (!trimmedUrl) {
        showToast('Please enter a stream URL', 'warning');
        url.focus();
        return;
    }
    
    // Validate URL format
    if (!trimmedUrl.startsWith('http://') && !trimmedUrl.startsWith('https://')) {
        showToast('Please enter a valid URL starting with http:// or https://', 'warning');
        url.focus();
        return;
    }
    
    // Additional URL validation
    try {
        new URL(trimmedUrl);
    } catch (e) {
        showToast('Please enter a valid URL', 'warning');
        url.focus();
        return;
    }
    
    // Validate URL length
    if (trimmedUrl.length > 256) {
        showToast('Stream URL must be 256 characters or less', 'warning');
        url.focus();
        return;
    }
    
    streams.push({ name: trimmedName, url: trimmedUrl });
    renderPlaylist();
    
    // Clear form
    name.value = '';
    url.value = '';
    name.focus(); // Focus back to name field for next entry
}

function updateStream(index, field, value) {
    if (index < 0 || index >= streams.length) {
        showToast('Invalid stream index', 'error');
        return;
    }
    
    if (!streams[index]) {
        showToast('Stream not found', 'error');
        return;
    }
    
    const trimmedValue = value.trim();
    
    // Validate URL format if updating URL field
    if (field === 'url') {
        if (!trimmedValue) {
            showToast('URL cannot be empty', 'error');
            return;
        }
        
        if (!trimmedValue.startsWith('http://') && !trimmedValue.startsWith('https://')) {
            showToast('Invalid URL format. Must start with http:// or https://', 'error');
            return;
        }
        
        // Additional URL validation
        try {
            new URL(trimmedValue);
        } catch (e) {
            showToast('Invalid URL format', 'error');
            return;
        }
        
        // Validate URL length
        if (trimmedValue.length > 256) {
            showToast('Stream URL must be 256 characters or less', 'error');
            return;
        }
    }
    
    // Validate name field
    if (field === 'name') {
        if (!trimmedValue) {
            showToast('Name cannot be empty', 'error');
            return;
        }
        
        // Validate name length
        if (trimmedValue.length > 128) {
            showToast('Stream name must be 128 characters or less', 'error');
            return;
        }
    }
    
    streams[index][field] = trimmedValue;
}

function deleteStream(index) {
    if (index < 0 || index >= streams.length) {
        showToast('Invalid stream index', 'error');
        return;
    }
    
    if (confirm('Are you sure you want to delete this stream?')) {
        streams.splice(index, 1);
        renderPlaylist();
    }
}

async function savePlaylist() {
    // Validate playlist before saving
    if (streams.length === 0) {
        if (!confirm('Playlist is empty. Do you want to save an empty playlist?')) {
            return;
        }
    }
    
    // Validate each stream in the playlist
    for (let i = 0; i < streams.length; i++) {
        const stream = streams[i];
        if (!stream || typeof stream !== 'object') {
            showToast(`Invalid stream at position ${i+1}`, 'error');
            return;
        }
        
        if (!stream.name || !stream.name.trim()) {
            showToast(`Stream at position ${i+1} has an empty name`, 'error');
            return;
        }
        
        if (!stream.url) {
            showToast(`Stream at position ${i+1} has no URL`, 'error');
            return;
        }
        
        if (!stream.url.startsWith('http://') && !stream.url.startsWith('https://')) {
            showToast(`Stream at position ${i+1} has invalid URL format`, 'error');
            return;
        }
        
        try {
            new URL(stream.url);
        } catch (e) {
            showToast(`Stream at position ${i+1} has invalid URL`, 'error');
            return;
        }
    }
    
    // Show loading state
    const saveButton = document.querySelector('button[onclick="savePlaylist()"]');
    const originalText = saveButton ? saveButton.textContent : null;
    if (saveButton) {
        saveButton.textContent = 'Saving...';
        saveButton.disabled = true;
    }
    
    // Convert streams to JSON
    let jsonData;
    try {
        jsonData = JSON.stringify(streams);
    } catch (error) {
        console.error('Error serializing playlist:', error);
        showToast('Error serializing playlist: ' + error.message, 'error');
        if (saveButton) {
            saveButton.textContent = originalText || 'Save Playlist';
            saveButton.disabled = false;
        }
        return;
    }
    
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
            // Handle JSON response
            const result = await response.json();
            if (result.status === 'success') {
                showToast('Playlist saved successfully!', 'success');
            } else {
                throw new Error(result.message || 'Unknown error');
            }
        } else {
            const error = await response.text();
            throw new Error(`Error saving playlist: ${response.status} ${response.statusText} - ${error}`);
        }
    } catch (error) {
        console.error('Error saving playlist:', error);
        const errorMessage = error.message || 'Unknown error occurred';
        showToast('Error saving playlist: ' + errorMessage + '. Please check your connection and try again.', 'error');
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
        showToast('Please select a playlist file', 'warning');
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.json')) {
        showToast('Please select a JSON file', 'warning');
        // Clear file input on error
        fileInput.value = '';
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
            
            if (response.ok) {
                // Handle JSON response
                const result = await response.json();
                if (result.status === 'success') {
                    showToast(result.message || 'Playlist uploaded successfully', 'success');
                    // Reload streams after successful upload
                    loadStreams();
                } else {
                    showToast(result.message || 'Error uploading playlist', 'error');
                }
            } else {
                const error = await response.text();
                showToast('Error uploading playlist: ' + error, 'error');
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
            showToast('Error uploading playlist file: ' + error.message, 'error');
        } finally {
            // Clear file input in all cases
            fileInput.value = '';
            // Restore button state
            if (uploadButton) {
                uploadButton.textContent = originalText || 'Upload JSON';
                uploadButton.disabled = false;
            }
        }
    };
    reader.onerror = function() {
        showToast('Error reading file. Please make sure the file is not corrupted and try again.', 'error');
        fileInput.value = '';
        // Restore button state
        if (uploadButton) {
            uploadButton.textContent = originalText || 'Upload JSON';
            uploadButton.disabled = false;
        }
    };
    reader.readAsText(file);
}

async function uploadM3U() {
    const fileInput = document.getElementById('playlistFile');
    const file = fileInput.files[0];
    
    console.log('Uploading M3U playlist file:', file);
    
    if (!file) {
        showToast('Please select a playlist file', 'warning');
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.m3u') && !fileName.endsWith('.m3u8')) {
        showToast('Please select an M3U file', 'warning');
        // Clear file input on error
        fileInput.value = '';
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
        
        try {
            // Convert M3U to JSON
            const jsonData = convertM3UToJSON(fileContent);
            console.log('JSON content:', jsonData);
        
            const response = await fetch('/api/streams', {
                method: 'POST',
                headers: { 
                    'Content-Type': 'application/json',
                    'X-Requested-With': 'XMLHttpRequest'
                },
                body: jsonData
            });
            
            console.log('Upload response status:', response.status);
            
            if (response.ok) {
                // Handle JSON response
                const result = await response.json();
                if (result.status === 'success') {
                    showToast(result.message || 'Playlist uploaded successfully', 'success');
                    // Reload streams after successful upload
                    loadStreams();
                } else {
                    showToast(result.message || 'Error uploading playlist', 'error');
                }
            } else {
                const error = await response.text();
                showToast('Error uploading playlist: ' + error, 'error');
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
            showToast('Error uploading playlist file: ' + error.message, 'error');
        } finally {
            // Clear file input in all cases
            fileInput.value = '';
            // Restore button state
            if (uploadButton) {
                uploadButton.textContent = originalText || 'Upload M3U';
                uploadButton.disabled = false;
            }
        }
    };
    reader.onerror = function() {
        showToast('Error reading file. Please make sure the file is not corrupted and try again.', 'error');
        fileInput.value = '';
        // Restore button state
        if (uploadButton) {
            uploadButton.textContent = originalText || 'Upload M3U';
            uploadButton.disabled = false;
        }
    };
    reader.readAsText(file);
}

async function downloadJSON() {
    // Show loading state
    const downloadButton = document.querySelector('button[onclick="downloadJSON()"]');
    const originalText = downloadButton ? downloadButton.textContent : null;
    if (downloadButton) {
        downloadButton.textContent = 'Downloading...';
        downloadButton.disabled = true;
    }
    
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
            showToast('JSON playlist downloaded', 'success');
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
            showToast('Error downloading JSON: ' + error, 'error');
        }
    } catch (error) {
        console.error('Error downloading JSON:', error);
        showToast('Error downloading JSON: ' + error.message, 'error');
    } finally {
        // Restore button state
        if (downloadButton) {
            downloadButton.textContent = originalText || 'Download JSON';
            downloadButton.disabled = false;
        }
    }
}

async function downloadM3U() {
    // Show loading state
    const downloadButton = document.querySelector('button[onclick="downloadM3U()"]');
    const originalText = downloadButton ? downloadButton.textContent : null;
    if (downloadButton) {
        downloadButton.textContent = 'Downloading...';
        downloadButton.disabled = true;
    }
    
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
            showToast('M3U playlist downloaded', 'success');
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
            showToast('Error downloading playlist: ' + error, 'error');
        }
    } catch (error) {
        console.error('Error downloading playlist:', error);
        showToast('Error downloading playlist: ' + error.message, 'error');
    } finally {
        // Restore button state
        if (downloadButton) {
            downloadButton.textContent = originalText || 'Download M3U';
            downloadButton.disabled = false;
        }
    }
}

// Convert M3U content to JSON
function convertM3UToJSON(m3uContent) {
    try {
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
                // Validate URL
                try {
                    new URL(line);
                } catch (e) {
                    console.warn('Skipping invalid URL in M3U file:', line);
                    currentName = ''; // Reset for next entry
                    continue;
                }
                
                // Validate name
                if (!currentName) {
                    currentName = 'Stream ' + (streams.length + 1);
                }
                
                // Validate lengths
                if (currentName.length > 128) {
                    currentName = currentName.substring(0, 125) + '...';
                }
                
                if (line.length <= 256) {
                    streams.push({
                        name: currentName,
                        url: line
                    });
                } else {
                    console.warn('Skipping URL that exceeds maximum length:', line);
                }
                currentName = ''; // Reset for next entry
            }
        }
        
        return JSON.stringify(streams);
    } catch (error) {
        console.error('Error converting M3U to JSON:', error);
        throw new Error('Failed to convert M3U file: ' + error.message);
    }
}

// Convert JSON content to M3U
function convertJSONToM3U(jsonData) {
    try {
        let m3uContent = '#EXTM3U\n';
        
        if (!Array.isArray(jsonData)) {
            throw new Error('Invalid JSON format: expected array of streams');
        }
        
        jsonData.forEach(item => {
            if (item.name && item.url) {
                // Validate and sanitize name
                let sanitizedName = item.name.trim();
                if (sanitizedName.length > 128) {
                    sanitizedName = sanitizedName.substring(0, 125) + '...';
                }
                
                // Validate URL
                try {
                    new URL(item.url);
                } catch (e) {
                    console.warn('Skipping invalid URL in JSON:', item.url);
                    return;
                }
                
                // Validate URL length
                if (item.url.length > 256) {
                    console.warn('Skipping URL that exceeds maximum length:', item.url);
                    return;
                }
                
                m3uContent += '#EXTINF:-1,' + sanitizedName + '\n';
                m3uContent += item.url + '\n';
            }
        });
        
        return m3uContent;
    } catch (error) {
        console.error('Error converting JSON to M3U:', error);
        throw new Error('Failed to convert playlist to M3U: ' + error.message);
    }
}

// WiFi functions
let networkCount = 1;
let configuredNetworks = [];

function initWiFiPage() {
    // Load existing WiFi configuration when page loads
    window.addEventListener('load', function() {
        loadConfiguredNetworks();
        loadCurrentConfiguration();
        // Don't automatically scan networks - user must click Refresh Networks button
    });
    
    // Setup form submit handler
    const wifiForm = document.getElementById('wifiForm');
    if (wifiForm) {
        wifiForm.addEventListener('submit', function(e) {
            e.preventDefault();
            handleWiFiFormSubmit();
        });
    }
}

function loadConfiguredNetworks() {
    // Load existing configuration to know which networks are already configured
    fetch('/api/wificonfig')
        .then(response => response.json())
        .then(data => {
            // Handle consistent data structure for configured networks
            if (Array.isArray(data)) {
                // If array of strings (SSIDs only)
                configuredNetworks = data;
            } else if (data.configured && Array.isArray(data.configured)) {
                // If object with configured array
                configuredNetworks = data.configured;
            } else if (data.networks && Array.isArray(data.networks)) {
                // If object with networks array containing objects with ssid property
                configuredNetworks = data.networks.map(network => 
                    typeof network === 'string' ? network : network.ssid
                ).filter(ssid => ssid);
            } else {
                configuredNetworks = [];
            }
        })
        .catch(error => {
            console.error('Error loading configured networks:', error);
            configuredNetworks = [];
        });
}

function loadCurrentConfiguration() {
    // Load current WiFi configuration to populate the form
    fetch('/api/wificonfig')
        .then(response => response.json())
        .then(data => {
            // Handle consistent data structure for configuration loading
            let configNetworks = [];
            if (Array.isArray(data)) {
                // If array of strings (SSIDs only)
                configNetworks = data.map((ssid, index) => ({ ssid, password: '' }));
            } else if (data.configured && Array.isArray(data.configured)) {
                // If object with configured array of strings
                configNetworks = data.configured.map((ssid, index) => ({ ssid, password: '' }));
            } else if (data.networks && Array.isArray(data.networks)) {
                // If object with networks array containing objects
                configNetworks = data.networks.map(network => ({
                    ssid: typeof network === 'string' ? network : network.ssid || '',
                    password: network.password || ''
                }));
            }
            
            // Clear existing fields except the first one
            const networkFields = document.getElementById('networkFields');
            while (networkFields.children.length > 1) {
                networkFields.removeChild(networkFields.lastChild);
            }
            
            // Reset network count
            networkCount = 1;
            
            // Populate with configured networks
            if (configNetworks.length > 0) {
                // Fill the first entry
                document.getElementById('ssid0').value = configNetworks[0].ssid || '';
                
                // Add additional entries for each configured network
                for (let i = 1; i < configNetworks.length && i < 5; i++) {
                    addNetworkField();
                    document.getElementById(`ssid${i}`).value = configNetworks[i].ssid || '';
                }
                networkCount = configNetworks.length;
            }
        })
        .catch(error => {
            console.error('Error loading current configuration:', error);
        });
}

function addNetworkField() {
    if (networkCount >= 5) {
        showToast('Maximum of 5 networks allowed', 'warning');
        return;
    }
    
    const networkFields = document.getElementById('networkFields');
    const newEntry = document.createElement('div');
    newEntry.role = "group"
    newEntry.innerHTML = `
        <input type="text" id="ssid${networkCount}" name="ssid${networkCount}" placeholder="Enter SSID">
        <input type="password" id="password${networkCount}" name="password${networkCount}" placeholder="Enter Password">
        <button type="button" class="remove-btn secondary" onclick="removeNetworkField(this)">Remove</button>
    `;
    networkFields.appendChild(newEntry);
    networkCount++;
}

function removeNetworkField(button) {
    if (document.querySelectorAll('.network-entry').length <= 1) {
        showToast('At least one network is required', 'warning');
        return;
    }
    
    button.parentElement.remove();
    networkCount = document.querySelectorAll('.network-entry').length;
}

function handleWiFiFormSubmit() {
    const ssids = [];
    const passwords = [];
    
    // Collect all network entries
    const networkEntries = document.querySelectorAll('.network-entry');
    networkEntries.forEach((entry, index) => {
        const ssidInput = entry.querySelector(`#ssid${index}`);
        const passwordInput = entry.querySelector(`#password${index}`);
        
        if (ssidInput && ssidInput.value.trim()) {
            ssids.push(ssidInput.value.trim());
            passwords.push(passwordInput ? passwordInput.value : '');
        }
    });
    
    if (ssids.length === 0) {
        showToast('At least one SSID is required', 'error');
        return;
    }
    
    const data = {
        ssid: ssids,
        password: passwords
    };
    
    // Show saving state
    const saveButton = document.querySelector('button[type="submit"]');
    const originalText = saveButton ? saveButton.textContent : 'Save Configuration';
    if (saveButton) {
        saveButton.textContent = 'Saving...';
        saveButton.disabled = true;
    }
    
    fetch('/api/wifisave', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(data)
    })
    .then(response => {
        if (response.ok) {
            showToast('WiFi configuration saved successfully', 'success');
        } else {
            showToast('Error saving WiFi configuration', 'error');
        }
    })
    .catch(error => {
        console.error('Error:', error);
        showToast('Error saving WiFi configuration: ' + error.message, 'error');
    })
    .finally(() => {
        // Restore button state
        if (saveButton) {
            saveButton.textContent = originalText;
            saveButton.disabled = false;
        }
    });
}

// Override the scanNetworks function to highlight configured networks
function scanNetworks() {
    const networksDiv = document.getElementById('networks');
    if (!networksDiv) return;
    
    networksDiv.innerHTML = '<p class="scanning">Scanning for networks...</p>';
    
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
            networksDiv.innerHTML = '';
            
            // Handle the actual data structure from the API
            // The API returns an object with "networks" array and "configured" array
            let scanNetworks = [];
            if (data.networks && Array.isArray(data.networks)) {
                scanNetworks = data.networks;
            } else {
                // Fallback for unexpected data structure
                scanNetworks = [];
            }
            
            if (scanNetworks.length === 0) {
                networksDiv.innerHTML = '<p class="no-networks">No networks found. Move closer to your router or check if WiFi is enabled.</p>';
                return;
            }
            
            const networksList = document.createElement('div');
            networksList.className = 'networks-list';
            
            scanNetworks.forEach(network => {
                // Skip invalid networks
                if (!network || !network.ssid) return;
                
                const networkDiv = document.createElement('div');
                networkDiv.className = 'network-item';
                
                // Check if this network is already configured
                const isConfigured = data.configured && Array.isArray(data.configured) && data.configured.includes(network.ssid);
                
                // Signal strength indicator
                let signalClass = 'signal-weak';
                if (network.rssi > -60) signalClass = 'signal-strong';
                else if (network.rssi > -70) signalClass = 'signal-medium';
                
                if (isConfigured) {
                    networkDiv.classList.add('configured-network');
                    networkDiv.innerHTML = `
                        <div class="network-info">
                            <span class="network-name">${network.ssid} <span class="configured-marker">★ Configured</span></span>
                            <span class="network-rssi ${signalClass}">${network.rssi} dBm</span>
                        </div>
                        <button class="btn-small" onclick="selectNetwork('${network.ssid}')">Reconfigure</button>
                    `;
                } else {
                    networkDiv.innerHTML = `
                        <div class="network-info">
                            <span class="network-name">${network.ssid}</span>
                            <span class="network-rssi ${signalClass}">${network.rssi} dBm</span>
                        </div>
                        <button class="btn-small" onclick="selectNetwork('${network.ssid}')">Select</button>
                    `;
                }
                
                networksList.appendChild(networkDiv);
            });
            
            networksDiv.appendChild(networksList);
        })
        .catch(error => {
            console.error('Error scanning networks:', error);
            networksDiv.innerHTML = '<p class="error">Error scanning networks. Please try again.</p>';
        })
        .finally(() => {
            // Restore scan button
            if (scanButton) {
                scanButton.textContent = originalText || 'Refresh Networks';
                scanButton.disabled = false;
            }
        });
}

// Load current connection status
function loadConnectionStatus() {
    const statusDiv = document.getElementById('connection-status');
    if (!statusDiv) return;
    
    statusDiv.innerHTML = '<p>Loading connection status...</p>';
    
    fetch('/api/wifistatus')
        .then(response => response.json())
        .then(data => {
            if (data.connected) {
                statusDiv.innerHTML = `
                    <p><strong>Connected</strong></p>
                    <p>SSID: ${data.ssid || 'Unknown'}</p>
                    <p>IP Address: ${data.ip || 'Unknown'}</p>
                    <p>Signal Strength: ${data.rssi || 'Unknown'} dBm</p>
                `;
            } else {
                statusDiv.innerHTML = '<p><strong>Not connected</strong></p>';
            }
        })
        .catch(error => {
            console.error('Error loading connection status:', error);
            statusDiv.innerHTML = '<p>Error loading connection status</p>';
        });
}

function selectNetwork(ssid) {
    document.getElementById('ssid0').value = ssid;
}

