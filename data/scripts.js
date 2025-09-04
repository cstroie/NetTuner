// Main functions
let streams = [];
let bass = 0;
let midrange = 0;
let treble = 0;

// Function to find favicon URL from a website
async function findFaviconUrl(websiteUrl) {
    try {
        // Handle cases where the URL might be a stream URL
        let baseUrl;
        try {
            const urlObj = new URL(websiteUrl);
            baseUrl = `${urlObj.protocol}//${urlObj.host}`;
        } catch (e) {
            // If URL parsing fails, try to extract domain
            const match = websiteUrl.match(/^(?:https?:\/\/)?([^\/\s]+)/);
            if (match) {
                baseUrl = `http://${match[1]}`;
            } else {
                return null;
            }
        }
        // Common favicon locations to check
        const faviconLocations = [
            '/favicon.ico',
            '/favicon.png',
            '/apple-touch-icon.png',
            '/apple-touch-icon-precomposed.png'
        ];
        // First, try to get favicon from HTML head by fetching the base URL
        try {
            const response = await fetch(baseUrl, { mode: 'no-cors' });
            // For no-cors requests, we can't access the response body
            // So we'll skip HTML parsing and go directly to favicon locations
        } catch (e) {
            console.log('Could not fetch HTML for favicon detection');
        }
        // Check common locations
        for (const location of faviconLocations) {
            try {
                const faviconUrl = new URL(location, baseUrl).href;
                if (await checkImageExists(faviconUrl)) {
                    return faviconUrl;
                }
            } catch (e) {
                // Skip if URL construction fails
                continue;
            }
        }
        return null;
    } catch (error) {
        console.error('Error finding favicon:', error);
        return null;
    }
}

// Helper function to check if image exists
async function checkImageExists(url) {
    return new Promise((resolve) => {
        const img = new Image();
        img.onload = () => resolve(true);
        img.onerror = () => resolve(false);
        // Add crossorigin attribute to handle CORS issues
        img.crossOrigin = 'anonymous';
        img.src = url;
    });
}

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
    // Load existing playlist when page loads
    loadStreams();
}

function initWiFiPage() {
    // Load existing WiFi configuration when page loads
    window.addEventListener('load', function() {
        loadConfiguredNetworks();
    });
}

async function loadConfig() {
    try {
        const response = await fetch('/api/config');
        if (response.ok) {
            const config = await response.json();
            document.getElementById('i2s_bclk').value = config.i2s_bclk || 27;
            document.getElementById('i2s_lrc').value = config.i2s_lrc || 25;
            document.getElementById('i2s_dout').value = config.i2s_dout || 26;
            document.getElementById('led_pin').value = config.led_pin || 2;
            document.getElementById('rotary_clk').value = config.rotary_clk || 18;
            document.getElementById('rotary_dt').value = config.rotary_dt || 19;
            document.getElementById('rotary_sw').value = config.rotary_sw || 23;
            document.getElementById('board_button').value = config.board_button || 0;
            document.getElementById('display_sda').value = config.display_sda || 5;
            document.getElementById('display_scl').value = config.display_scl || 4;
            document.getElementById('display_width').value = config.display_width || 128;
            document.getElementById('display_height').value = config.display_height || 64;
            document.getElementById('display_address').value = config.display_address || 60;
        }
    } catch (error) {
        console.error('Error loading config:', error);
    }
}

async function saveConfig() {
    const config = {
        i2s_bclk: parseInt(document.getElementById('i2s_bclk').value),
        i2s_lrc: parseInt(document.getElementById('i2s_lrc').value),
        i2s_dout: parseInt(document.getElementById('i2s_dout').value),
        led_pin: parseInt(document.getElementById('led_pin').value),
        rotary_clk: parseInt(document.getElementById('rotary_clk').value),
        rotary_dt: parseInt(document.getElementById('rotary_dt').value),
        rotary_sw: parseInt(document.getElementById('rotary_sw').value),
        board_button: parseInt(document.getElementById('board_button').value),
        display_sda: parseInt(document.getElementById('display_sda').value),
        display_scl: parseInt(document.getElementById('display_scl').value),
        display_width: parseInt(document.getElementById('display_width').value),
        display_height: parseInt(document.getElementById('display_height').value),
        display_address: parseInt(document.getElementById('display_address').value)
    };
    
    try {
        const response = await fetch('/api/config', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
            },
            body: JSON.stringify(config)
        });
        
        // Remove any existing modal
        const existingModal = document.getElementById('configModal');
        if (existingModal) {
            existingModal.remove();
        }
        
        // Create modal element
        const modal = document.createElement('dialog');
        modal.id = 'configModal';
        modal.innerHTML = `
            <article>
                <header>
                    <h2>Configuration Saved</h2>
                </header>
                <p>Configuration saved successfully. Device restart required for changes to take effect.</p>
                <footer>
                    <div role="group">
                        <button onclick="document.getElementById('configModal').remove()">OK</button>
                    </div>
                </footer>
            </article>
        `;
        
        if (response.ok) {
            document.body.appendChild(modal);
            modal.showModal();
        } else {
            modal.querySelector('h2').textContent = 'Error';
            modal.querySelector('p').textContent = 'Error saving configuration';
            document.body.appendChild(modal);
            modal.showModal();
        }
    } catch (error) {
        console.error('Error saving config:', error);
        
        // Remove any existing modal
        const existingModal = document.getElementById('configModal');
        if (existingModal) {
            existingModal.remove();
        }
        
        // Create error modal
        const modal = document.createElement('dialog');
        modal.id = 'configModal';
        modal.innerHTML = `
            <article>
                <header>
                    <h2>Error</h2>
                </header>
                <p>Error saving configuration</p>
                <footer>
                    <div role="group">
                        <button onclick="document.getElementById('configModal').remove()">OK</button>
                    </div>
                </footer>
            </article>
        `;
        document.body.appendChild(modal);
        modal.showModal();
    }
}

function initConfigPage() {
    // Load existing configuration when page loads
    loadConfig();
    
    // Set up form submit handler
    const configForm = document.getElementById('configForm');
    if (configForm) {
        configForm.addEventListener('submit', function(e) {
            e.preventDefault();
            saveConfig();
        });
    }
}

/**
 * @brief Load streams from the server
 * Fetches the playlist from the server and updates the UI
 * Shows loading states and handles errors appropriately
 * 
 * This function retrieves the current playlist from the server via the /api/streams endpoint.
 * It updates both the main page stream selector and the playlist management page.
 * The function handles loading states, error conditions, and data validation.
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
        playlistBody.innerHTML = '<span aria-busy="true">Loading streams...</span>';
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
            select.disabled = true;
        }
        if (playlistBody) {
            playlistBody.innerHTML = `<span>Error loading streams: ${errorMessage}</span>`;
        }
    }
}


// WebSocket connection
let ws = null;
let reconnectTimeout = null;
let isConnecting = false;
let reconnectAttempts = 0;
const maxReconnectAttempts = 10;
const connectionTimeout = 10000; // 10 seconds timeout for connection

/**
 * @brief Establish WebSocket connection to server
 * Manages WebSocket connection lifecycle with automatic reconnection and error handling
 * 
 * This function creates a WebSocket connection to the server for real-time status updates.
 * It implements robust connection management including:
 * - Automatic reconnection with exponential backoff
 * - Connection timeout handling
 * - Proper cleanup of previous connections
 * - State synchronization with UI elements
 * 
 * The WebSocket connects to port 81 (different from HTTP server port) and handles
 * various events including open, message, close, and error conditions.
 */
function connectWebSocket() {
    // Clear any existing reconnection timeout
    if (reconnectTimeout) {
        clearTimeout(reconnectTimeout);
        reconnectTimeout = null;
    }
    
    // Prevent multiple connection attempts
    if (isConnecting) {
        return;
    }
    
    // Check connection state
    if (ws && (ws.readyState === WebSocket.OPEN || ws.readyState === WebSocket.CONNECTING)) {
        return;
    }
    
    // Check if we've exceeded max reconnect attempts
    if (reconnectAttempts >= maxReconnectAttempts) {
        console.log('Max reconnect attempts reached. Stopping reconnection.');
        return;
    }
    
    isConnecting = true;
    
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
        
        // Set up connection timeout
        const connectionTimer = setTimeout(() => {
            if (ws && ws.readyState === WebSocket.CONNECTING) {
                console.log('WebSocket connection timeout');
                ws.close();
            }
        }, connectionTimeout);
        
        ws.onopen = function() {
            clearTimeout(connectionTimer);
            isConnecting = false;
            reconnectAttempts = 0; // Reset reconnect attempts on successful connection
            console.log('WebSocket connected');
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
                }
                
                // Update stream name element
                const streamNameElement = document.getElementById('streamName');
                if (streamNameElement) {
                    // Show stream name when playing
                    let displayText = status.streamName || 'No station selected';
                    // Update text content but preserve the favicon element
                    const faviconElement = document.getElementById('stationFavicon');
                    if (faviconElement) {
                        streamNameElement.innerHTML = faviconElement.outerHTML + displayText;
                    } else {
                        streamNameElement.textContent = displayText;
                    }
                }

                // Update stream title element
                const streamTitleElement = document.getElementById('streamTitle');
                if (streamTitleElement) {
                    // Show stream title when playing
                    let displayText = status.streamTitle || 'No stream selected';
                    if (status.playing) {
                        if (status.bitrate) {
                            displayText += ' (' + status.bitrate + ' kbps)';
                        }
                    }
                    streamTitleElement.textContent = displayText;
                }
                
                // Handle ICY URL if available
                if (status.streamIcyURL) {
                    console.log('Received ICY URL:', status.streamIcyURL);
                    // Try to get favicon from the ICY URL
                    findFaviconUrl(status.streamIcyURL).then(faviconUrl => {
                        if (faviconUrl) {
                            console.log('Found favicon:', faviconUrl);
                            // Update the favicon image element
                            const faviconElement = document.getElementById('stationFavicon');
                            if (faviconElement) {
                                faviconElement.src = faviconUrl;
                                faviconElement.style.display = 'inline';
                            }
                        }
                    });
                } else {
                    // Hide favicon if no ICY URL
                    const faviconElement = document.getElementById('stationFavicon');
                    if (faviconElement) {
                        faviconElement.style.display = 'none';
                    }
                }
                
                // Update volume controls
                const volumeControl = document.getElementById('volume');
                const volumeValue = document.getElementById('volumeValue');
                
                if (volumeControl) {
                    volumeControl.value = status.volume;
                }
                
                if (volumeValue) {
                    volumeValue.textContent = status.volume;
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
            clearTimeout(connectionTimer);
            
            // Check if this was a clean close or an error
            if (event.wasClean) {
                console.log(`WebSocket closed cleanly, code=${event.code}, reason=${event.reason}`);
            } else {
                console.log('WebSocket connection died');
                reconnectAttempts++;
            }
            
            console.log('WebSocket disconnected. Attempt ' + reconnectAttempts + ' of ' + maxReconnectAttempts);
            
            isConnecting = false;
            
            // Try to reconnect with exponential backoff, but reset counter on successful connection
            if (reconnectAttempts < maxReconnectAttempts) {
                const timeout = Math.min(1000 * Math.pow(2, reconnectAttempts), 30000);
                console.log(`Reconnecting in ${timeout}ms`);
                reconnectTimeout = setTimeout(connectWebSocket, timeout);
            } else {
                console.log('Max reconnect attempts reached. Stopping reconnection.');
            }
        };
        
        ws.onerror = function(error) {
            clearTimeout(connectionTimer);
            console.error('WebSocket error:', error);
        };
    } catch (error) {
        clearTimeout(connectionTimer);
        reconnectAttempts++;
        console.error('Error creating WebSocket:', error);
        
        isConnecting = false;
        
        // Try to reconnect with exponential backoff
        if (reconnectAttempts < maxReconnectAttempts) {
            const timeout = Math.min(1000 * Math.pow(2, reconnectAttempts), 30000);
            console.log(`Reconnecting in ${timeout}ms`);
            reconnectTimeout = setTimeout(connectWebSocket, timeout);
        } else {
            console.log('Max reconnect attempts reached. Stopping reconnection.');
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
        await sendPlayRequest(url, name, select.selectedIndex);
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
        return false;
    }
    
    // Validate URL format
    try {
        new URL(url);
    } catch (e) {
        return false;
    }
    
    return true;
}

async function sendPlayRequest(url, name, index) {
    const response = await fetch('/api/play', {
        method: 'POST',
        headers: { 
            'Content-Type': 'application/json',
            'X-Requested-With': 'XMLHttpRequest'
        },
        body: JSON.stringify({ url: url, name: name, index: index })
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
    // Validate stream selection
    if (!validateStreamSelection(url)) {
        return;
    }
    console.log('Playing selected stream:', { url, name });
    // Play the selected stream without showing toast
    sendPlayRequest(url, name, select.selectedIndex)
        .catch(error => {
            console.error('Error playing stream:', error);
        });
}

async function stopStream() {
    // Show loading state
    const stopButton = document.querySelector('button[onclick="stopStream()"]');
    const originalText = stopButton ? stopButton.textContent : 'Stop';
    if (stopButton) {
        stopButton.textContent = 'Stopping...';
        stopButton.disabled = true;
    }
    
    try {
        await sendStopRequest();
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
    });
}

// Wrapper function for tone change to handle errors in inline event handlers
function handleToneChange(type, value) {
    setTone(type, value).catch(error => {
        console.error('Error in tone change:', error);
    });
}

async function setVolume(volume) {
    // Validate volume parameter
    const volumeNum = parseInt(volume, 10);
    if (isNaN(volumeNum) || volumeNum < 0 || volumeNum > 22) {
        console.error('Invalid volume value:', volume);
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
                    volumeValue.textContent = volumeNum;
                }
            } else {
                throw new Error(result.message || 'Failed to set volume');
            }
        } else {
            throw new Error(`Failed to set volume: ${response.status} ${response.statusText}`);
        }
    } catch (error) {
        console.error('Error setting volume:', error);
        // Restore original volume value on error
        if (volumeControl) {
            volumeControl.value = originalVolume;
        }
        if (volumeValue) {
            volumeValue.textContent = originalVolume;
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
        urlInput.focus();
        return;
    }
    
    // Validate URL format
    if (!url.startsWith('http://') && !url.startsWith('https://')) {
        urlInput.focus();
        return;
    }
    
    try {
        new URL(url);
    } catch (e) {
        urlInput.focus();
        return;
    }
    
    // Show loading state
    const playButton = document.querySelector('.instant-play button');
    const originalText = playButton ? playButton.textContent : 'Instant Play';
    if (playButton) {
        playButton.textContent = 'Processing...';
        playButton.disabled = true;
    }
    
    try {
        // Check if URL is a playlist by extension
        const lowerUrl = url.toLowerCase();
        if (lowerUrl.endsWith('.m3u') || lowerUrl.endsWith('.m3u8') || 
            lowerUrl.endsWith('.pls') || lowerUrl.endsWith('.json')) {
            // It's a playlist, fetch and parse it
            const playlistData = await getPlaylistData(url);
            if (playlistData && playlistData.length > 0) {
                if (playlistData.length === 1) {
                    // Only one stream, play it directly
                    await sendPlayRequest(playlistData[0].url, playlistData[0].name, 0);
                } else {
                    // Multiple streams, show selection modal
                    showPlaylistSelectionModalForInstantPlay(playlistData, url);
                    // Don't clear input yet, user needs to select
                    if (playButton) {
                        playButton.textContent = originalText;
                        playButton.disabled = false;
                    }
                    return;
                }
            } else {
                throw new Error('No valid streams found in playlist');
            }
        } else {
            // Not a playlist, play directly
            await sendPlayRequest(url, 'Instant Stream', 0);
        }
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

async function getPlaylistData(url) {
    try {
        // Try direct fetch first
        let response = await fetch(url);
        
        // If direct fetch fails due to CORS, try with a proxy
        if (!response.ok) {
            console.log('Direct fetch failed, trying with CORS proxy');
            const proxyUrl = 'https://api.allorigins.win/raw?url=' + encodeURIComponent(url);
            response = await fetch(proxyUrl);
        }
        
        if (!response.ok) {
            throw new Error(`Failed to fetch playlist: ${response.status} ${response.statusText}`);
        }
        
        const contentType = response.headers.get('content-type') || '';
        const textContent = await response.text();
        
        let playlistData;
        
        // Detect format based on content type or content
        const lowerUrl = url.toLowerCase();
        if (lowerUrl.endsWith('.m3u') || lowerUrl.endsWith('.m3u8') || 
            contentType.includes('audio/x-mpegurl') || 
            contentType.includes('application/x-mpegurl') ||
            textContent.includes('#EXTM3U')) {
            // M3U format
            playlistData = JSON.parse(convertM3UToJSON(textContent));
        } else if (lowerUrl.endsWith('.pls') || 
                   contentType.includes('audio/x-scpls') || 
                   textContent.includes('[playlist]')) {
            // PLS format
            playlistData = JSON.parse(convertPLSToJSON(textContent));
        } else if (lowerUrl.endsWith('.json') || 
            contentType.includes('application/json') || 
            (textContent.trim().startsWith('{') || textContent.trim().startsWith('['))) {
            // JSON format
            const jsonData = JSON.parse(textContent);
            
            // Validate the JSON structure
            if (!Array.isArray(jsonData)) {
                throw new Error('Invalid playlist format: expected array of streams');
            }
            
            // Validate each stream in the playlist and skip invalid ones
            const validStreams = [];
            for (let i = 0; i < jsonData.length; i++) {
                const stream = jsonData[i];
                if (!stream || typeof stream !== 'object') {
                    console.warn(`Skipping invalid stream at position ${i+1}: not an object`);
                    continue;
                }
                
                if (!stream.name || !stream.name.trim()) {
                    console.warn(`Skipping stream at position ${i+1}: empty name`);
                    continue;
                }
                
                if (!stream.url) {
                    console.warn(`Skipping stream at position ${i+1}: no URL`);
                    continue;
                }
                
                if (!stream.url.startsWith('http://') && !stream.url.startsWith('https://')) {
                    console.warn(`Skipping stream at position ${i+1}: invalid URL format`);
                    continue;
                }
                
                try {
                    new URL(stream.url);
                } catch (e) {
                    console.warn(`Skipping stream at position ${i+1}: invalid URL`);
                    continue;
                }
                
                // If we get here, the stream is valid
                validStreams.push(stream);
            }
            
            playlistData = validStreams;
        } else {
            throw new Error('Unsupported playlist format');
        }
        
        return playlistData;
    } catch (error) {
        console.error('Error processing playlist:', error);
        throw error;
    }
}

// Playlist functions
/**
 * @brief Render the playlist in the UI
 * Updates the playlist display with current streams and provides editing controls
 * 
 * This function populates the playlist management interface with the current streams.
 * For each stream, it creates editable input fields for name and URL, along with
 * a delete button. It also handles the empty playlist case with a helpful message.
 * The playlist items are draggable for reordering.
 */
function renderPlaylist() {
    const playlistBody = document.getElementById('playlistBody');
    if (!playlistBody) {
        console.warn('Playlist element not found');
        return;
    }
    
    playlistBody.innerHTML = '';
    
    if (streams.length === 0) {
        const item = document.createElement('div');
        item.innerHTML = '<span class="empty-playlist">No streams in playlist. Add some streams to get started!</span>';
        playlistBody.appendChild(item);
        return;
    }
    
    // Make the playlist container a drop zone
    playlistBody.setAttribute('data-dropzone', 'true');
    
    streams.forEach((stream, index) => {
        const item = document.createElement('div');
        item.role = "group";
        item.className = "playlist-item";
        item.draggable = true;
        item.dataset.index = index;
        
        // Create favicon preview if available
        let faviconHtml = '';
        if (stream.favicon) {
            faviconHtml = `<img src="${stream.favicon}" alt="Favicon" style="width: 16px; height: 16px; vertical-align: middle; margin-right: 5px;">`;
        }
        
        item.innerHTML = `
            <div class="drag-handle">⋮⋮</div>
            <input type="text" value="${escapeHtml(stream.name)}" onchange="updateStream(${index}, 'name', this.value)">
            <input type="text" value="${escapeHtml(stream.url)}" onchange="updateStream(${index}, 'url', this.value)">
            <button onclick="playStreamFromPlaylist(${index})">Play</button>
            <button class="secondary" onclick="deleteStream(${index})">Delete</button>
        `;
        
        // Add drag and drop event listeners
        item.addEventListener('dragstart', handleDragStart);
        item.addEventListener('dragover', handleDragOver);
        item.addEventListener('dragleave', handleDragLeave);
        item.addEventListener('drop', handleDrop);
        item.addEventListener('dragend', handleDragEnd);
        
        playlistBody.appendChild(item);
    });
    
    // Add drop event listener to the playlist container
    playlistBody.addEventListener('dragover', handleDragOver);
    playlistBody.addEventListener('dragleave', handleDragLeave);
    playlistBody.addEventListener('drop', handleDropContainer);
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

// Drag and drop functions for playlist reordering
let dragSrcElement = null;

function handleDragStart(e) {
    dragSrcElement = this;
    e.dataTransfer.effectAllowed = 'move';
    e.dataTransfer.setData('text/html', this.innerHTML);
    this.classList.add('dragging');
}

function handleDragOver(e) {
    if (e.preventDefault) {
        e.preventDefault();
    }
    e.dataTransfer.dropEffect = 'move';
    
    // Add visual indicator for drop target
    if (this !== dragSrcElement) {
        this.classList.add('drag-over');
    }
    
    return false;
}

function handleDragLeave(e) {
    this.classList.remove('drag-over');
}

function handleDrop(e) {
    if (e.stopPropagation) {
        e.stopPropagation();
    }
    
    if (dragSrcElement !== this) {
        const srcIndex = parseInt(dragSrcElement.dataset.index);
        const targetIndex = parseInt(this.dataset.index);
        
        // Reorder the streams array
        const movedItem = streams[srcIndex];
        streams.splice(srcIndex, 1);
        streams.splice(targetIndex, 0, movedItem);
        
        // Re-render the playlist to update indices
        renderPlaylist();
    }
    
    this.classList.remove('drag-over');
    return false;
}

function handleDropContainer(e) {
    if (e.stopPropagation) {
        e.stopPropagation();
    }
    
    // If dropping on the container (not on an item), move to the end
    if (dragSrcElement) {
        const srcIndex = parseInt(dragSrcElement.dataset.index);
        
        if (srcIndex !== streams.length - 1) {
            const movedItem = streams[srcIndex];
            streams.splice(srcIndex, 1);
            streams.push(movedItem);
            
            // Re-render the playlist to update indices
            renderPlaylist();
        }
    }
    
    return false;
}

function handleDragEnd(e) {
    const items = document.querySelectorAll('.playlist-item');
    items.forEach(item => {
        item.classList.remove('dragging');
        item.classList.remove('drag-over');
    });
}

/**
 * @brief Add a new stream to the playlist
 * Validates user input and adds a new stream to the playlist
 * 
 * This function handles the addition of a new stream to the playlist. It performs
 * comprehensive validation on both the stream name and URL including:
 * - Non-empty validation
 * - Length limits (128 chars for name, 256 chars for URL)
 * - URL format validation (must start with http:// or https://)
 * - Proper URL structure validation
 * 
 * After successful validation, the stream is added to the playlist and the UI is updated.
 */
function addStream() {
    const name = document.getElementById('name');
    const url = document.getElementById('url');
    
    if (!name || !url) {
        return;
    }
    
    console.log('Adding stream:', { name: name.value, url: url.value });
    
    // Trim and validate name
    const trimmedName = name.value.trim();
    if (!trimmedName) {
        name.focus();
        return;
    }
    
    // Validate name length
    if (trimmedName.length > 128) {
        name.focus();
        return;
    }
    
    // Trim and validate URL
    const trimmedUrl = url.value.trim();
    if (!trimmedUrl) {
        url.focus();
        return;
    }
    
    // Validate URL format
    if (!trimmedUrl.startsWith('http://') && !trimmedUrl.startsWith('https://')) {
        url.focus();
        return;
    }
    
    // Additional URL validation
    try {
        new URL(trimmedUrl);
    } catch (e) {
        url.focus();
        return;
    }
    
    // Validate URL length
    if (trimmedUrl.length > 256) {
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

/**
 * @brief Update a stream field in the playlist
 * Validates and updates a specific field (name or URL) of a stream
 * 
 * This function updates either the name or URL field of a stream at the specified index.
 * It performs appropriate validation based on the field being updated:
 * - For URLs: Validates format, structure, and length (max 256 chars)
 * - For names: Validates non-empty and length (max 128 chars)
 * 
 * @param {number} index - The index of the stream to update
 * @param {string} field - The field to update ('name' or 'url')
 * @param {string} value - The new value for the field
 */
function updateStream(index, field, value) {
    if (index < 0 || index >= streams.length) {
        return;
    }
    
    if (!streams[index]) {
        return;
    }
    
    const trimmedValue = value.trim();
    
    // Validate URL format if updating URL field
    if (field === 'url') {
        if (!trimmedValue) {
            return;
        }
        
        if (!trimmedValue.startsWith('http://') && !trimmedValue.startsWith('https://')) {
            return;
        }
        
        // Additional URL validation
        try {
            new URL(trimmedValue);
        } catch (e) {
            return;
        }
        
        // Validate URL length
        if (trimmedValue.length > 256) {
            return;
        }
    }
    
    // Validate name field
    if (field === 'name') {
        if (!trimmedValue) {
            return;
        }
        
        // Validate name length
        if (trimmedValue.length > 128) {
            return;
        }
    }
    
    streams[index][field] = trimmedValue;
}

function playStreamFromPlaylist(index) {
    if (index < 0 || index >= streams.length) {
        return;
    }
    
    const stream = streams[index];
    if (!stream || !stream.url) {
        return;
    }
    
    // Play the stream directly
    sendPlayRequest(stream.url, stream.name, index)
        .catch(error => {
            console.error('Error playing stream from playlist:', error);
            handlePlayError(error);
        });
}

function deleteStream(index) {
    if (index < 0 || index >= streams.length) {
        return;
    }
    
    // Create modal for confirmation
    const modal = document.createElement('dialog');
    modal.id = 'deleteModal';
    modal.innerHTML = `
        <article>
            <header>
                <h2>Confirm Deletion</h2>
            </header>
            <p>Are you sure you want to delete this stream?</p>
            <footer>
                <div role="group">
                    <button onclick="confirmDeleteStream(${index})">Delete</button>
                    <button class="secondary" onclick="document.getElementById('deleteModal').remove()">Cancel</button>
                </div>
            </footer>
        </article>
    `;
    document.body.appendChild(modal);
    modal.showModal();
}

function confirmDeleteStream(index) {
    // Close and remove modal
    const modal = document.getElementById('deleteModal');
    if (modal) {
        modal.remove();
    }
    
    // Perform deletion
    streams.splice(index, 1);
    renderPlaylist();
}

async function savePlaylist() {
    // Validate playlist before saving
    if (streams.length === 0) {
        // Create modal for confirmation
        const modal = document.createElement('dialog');
        modal.id = 'emptyPlaylistModal';
        modal.innerHTML = `
            <article>
                <header>
                    <h2>Empty Playlist</h2>
                </header>
                <p>Playlist is empty. Do you want to save an empty playlist?</p>
                <footer>
                    <div role="group">
                        <button onclick="confirmSaveEmptyPlaylist()">Save</button>
                        <button class="secondary" onclick="document.getElementById('emptyPlaylistModal').remove()">Cancel</button>
                    </div>
                </footer>
            </article>
        `;
        document.body.appendChild(modal);
        modal.showModal();
        return;
    }
    
    // If playlist is not empty, proceed with saving
    savePlaylistInternal();
}

function confirmSaveEmptyPlaylist() {
    // Close and remove modal
    const modal = document.getElementById('emptyPlaylistModal');
    if (modal) {
        modal.remove();
    }
    
    // Proceed with saving
    savePlaylistInternal();
}

async function savePlaylistInternal() {
    // Validate each stream in the playlist
    for (let i = 0; i < streams.length; i++) {
        const stream = streams[i];
        if (!stream || typeof stream !== 'object') {
            return;
        }
        
        if (!stream.name || !stream.name.trim()) {
            return;
        }
        
        if (!stream.url) {
            return;
        }
        
        if (!stream.url.startsWith('http://') && !stream.url.startsWith('https://')) {
            return;
        }
        
        try {
            new URL(stream.url);
        } catch (e) {
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
                // Success - no toast needed
            } else {
                throw new Error(result.message || 'Unknown error');
            }
        } else {
            const error = await response.text();
            throw new Error(`Error saving playlist: ${response.status} ${response.statusText} - ${error}`);
        }
    } catch (error) {
        console.error('Error saving playlist:', error);
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
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.json')) {
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
                    // Reload streams after successful upload
                    loadStreams();
                }
            } else {
                const error = await response.text();
                console.error('Error uploading playlist: ' + error);
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
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
        fileInput.value = '';
        // Restore button state
        if (uploadButton) {
            uploadButton.textContent = originalText || 'Upload JSON';
            uploadButton.disabled = false;
        }
    };
    reader.readAsText(file);
}

async function importRemotePlaylist() {
    const urlInput = document.getElementById('remotePlaylistUrl');
    const url = urlInput.value.trim();
    
    if (!url) {
        return;
    }
    
    // Validate URL format
    try {
        new URL(url);
    } catch (e) {
        return;
    }
    
    // Show loading state
    const importButton = document.querySelector('button[onclick="importRemotePlaylist()"]');
    const originalText = importButton ? importButton.textContent : null;
    if (importButton) {
        importButton.textContent = 'Importing...';
        importButton.disabled = true;
    }
    
    try {
        // Try direct fetch first
        let response = await fetch(url);
        
        // If direct fetch fails due to CORS, try with a proxy
        if (!response.ok) {
            console.log('Direct fetch failed, trying with CORS proxy');
            const proxyUrl = 'https://api.allorigins.win/raw?url=' + encodeURIComponent(url);
            response = await fetch(proxyUrl);
        }
        
        if (!response.ok) {
            throw new Error(`Failed to fetch playlist: ${response.status} ${response.statusText}`);
        }
        
        const contentType = response.headers.get('content-type') || '';
        const textContent = await response.text();
        
        let playlistData;
        
        // Detect format based on content type or content
        if (url.toLowerCase().endsWith('.m3u') || 
                   url.toLowerCase().endsWith('.m3u8') || 
                   contentType.includes('audio/x-mpegurl') || 
                   contentType.includes('application/x-mpegurl') ||
                   textContent.includes('#EXTM3U')) {
            // M3U format
            playlistData = JSON.parse(convertM3UToJSON(textContent));
        } else if (url.toLowerCase().endsWith('.pls') || 
                   contentType.includes('audio/x-scpls') || 
                   textContent.includes('[playlist]')) {
            // PLS format
            playlistData = JSON.parse(convertPLSToJSON(textContent));
        } else if (contentType.includes('application/json') || 
            url.toLowerCase().endsWith('.json') || 
            (textContent.trim().startsWith('{') || textContent.trim().startsWith('['))) {
            // JSON format
            const jsonData = JSON.parse(textContent);
            
            // Validate the JSON structure
            if (!Array.isArray(jsonData)) {
                throw new Error('Invalid playlist format: expected array of streams');
            }
            
            // Validate each stream in the playlist and skip invalid ones
            const validStreams = [];
            for (let i = 0; i < jsonData.length; i++) {
                const stream = jsonData[i];
                if (!stream || typeof stream !== 'object') {
                    console.warn(`Skipping invalid stream at position ${i+1}: not an object`);
                    continue;
                }
                
                if (!stream.name || !stream.name.trim()) {
                    console.warn(`Skipping stream at position ${i+1}: empty name`);
                    continue;
                }
                
                if (!stream.url) {
                    console.warn(`Skipping stream at position ${i+1}: no URL`);
                    continue;
                }
                
                if (!stream.url.startsWith('http://') && !stream.url.startsWith('https://')) {
                    console.warn(`Skipping stream at position ${i+1}: invalid URL format`);
                    continue;
                }
                
                try {
                    new URL(stream.url);
                } catch (e) {
                    console.warn(`Skipping stream at position ${i+1}: invalid URL`);
                    continue;
                }
                
                // If we get here, the stream is valid
                validStreams.push(stream);
            }
            
            playlistData = validStreams;
        } else {
            throw new Error('Unsupported playlist format');
        }
        
        // Show selection modal
        showPlaylistSelectionModal(playlistData);
        
        // Clear the input field
        urlInput.value = '';
    } catch (error) {
        console.error('Error importing remote playlist:', error);
    } finally {
        // Restore button state
        if (importButton) {
            importButton.textContent = originalText || 'Import';
            importButton.disabled = false;
        }
    }
}

function showPlaylistSelectionModal(playlistData) {
    // Remove any existing modal
    const existingModal = document.getElementById('playlistSelectionModal');
    if (existingModal) {
        existingModal.remove();
    }
    
    // Store playlist data in a global variable for access by modal functions
    window.currentPlaylistData = playlistData;
    
    // Create modal element
    const modal = document.createElement('dialog');
    modal.id = 'playlistSelectionModal';
    modal.className = 'playlist-selection-modal';
    
    // Create modal content
    let modalContent = `
        <article>
            <header>
                <h2>Select Streams to Import</h2>
                <p>${playlistData.length} stream(s) found in the playlist</p>
            </header>
            <div class="playlist-items-container">
                <div class="select-all-container">
                    <label>
                        <input type="checkbox" id="selectAllCheckbox" checked>
                        <strong>Select / Deselect all</strong>
                    </label>
                </div>
    `;
    
    // Add checkbox for each stream
    playlistData.forEach((stream, index) => {
        modalContent += `
            <div class="playlist-item">
                <label>
                    <input type="checkbox" checked data-index="${index}" class="stream-checkbox">
                    <span class="stream-info">
                        <strong>${escapeHtml(stream.name)}</strong><br>
                        <small>${escapeHtml(stream.url)}</small>
                    </span>
                </label>
            </div>
        `;
    });
    
    modalContent += `
            </div>
            <footer class="grid">
                <div role="group">
                    <button id="appendSelectedBtn">Append Selected</button>
                    <button class="secondary" id="replaceAllBtn">Replace All</button>
                    <button class="secondary" onclick="document.getElementById('playlistSelectionModal').remove()">Cancel</button>
                </div>
            </footer>
        </article>
    `;
    
    modal.innerHTML = modalContent;
    document.body.appendChild(modal);
    modal.showModal();
    
    // Add event listeners for buttons
    document.getElementById('appendSelectedBtn').addEventListener('click', function() {
        appendSelectedStreams();
    });
    
    document.getElementById('replaceAllBtn').addEventListener('click', function() {
        replaceWithSelectedStreams();
    });
    
    // Add event listener for select all checkbox
    const selectAllCheckbox = document.getElementById('selectAllCheckbox');
    if (selectAllCheckbox) {
        selectAllCheckbox.addEventListener('change', function() {
            const checkboxes = document.querySelectorAll('#playlistSelectionModal .stream-checkbox');
            checkboxes.forEach(checkbox => {
                checkbox.checked = this.checked;
            });
        });
    }
}

function showPlaylistSelectionModalForInstantPlay(playlistData, originalUrl) {
    // Remove any existing modal
    const existingModal = document.getElementById('instantPlaySelectionModal');
    if (existingModal) {
        existingModal.remove();
    }
    
    // Store playlist data in a global variable for access by modal functions
    window.currentInstantPlayPlaylistData = playlistData;
    
    // Create modal element
    const modal = document.createElement('dialog');
    modal.id = 'instantPlaySelectionModal';
    modal.className = 'playlist-selection-modal';
    
    // Create modal content
    let modalContent = `
        <article>
            <header>
                <h2>Select Stream to Play</h2>
                <p>${playlistData.length} stream(s) found in the playlist</p>
            </header>
            <div class="playlist-items-container">
                <label for="instantStreamSelect">Choose a stream:</label>
                <select id="instantStreamSelect" style="width: 100%; margin-bottom: 1rem;">
    `;
    
    // Add option for each stream
    playlistData.forEach((stream, index) => {
        // Validate stream object
        if (!stream || typeof stream !== 'object') {
            console.warn('Skipping invalid stream object in playlist:', stream);
            return;
        }
        
        const streamName = stream.name || `Stream ${index + 1}`;
        modalContent += `
            <option value="${index}">${escapeHtml(streamName)}</option>
        `;
    });
    
    modalContent += `
                </select>
            </div>
            <footer>
                <div role="group">
                    <button id="playInstantSelectedStreamBtn">Play</button>
                    <button class="secondary" onclick="document.getElementById('instantPlaySelectionModal').remove()">Cancel</button>
                </div>
            </footer>
        </article>
    `;
    
    modal.innerHTML = modalContent;
    document.body.appendChild(modal);
    modal.showModal();
    
    // Add event listener for play button
    document.getElementById('playInstantSelectedStreamBtn').addEventListener('click', function() {
        const selectElement = document.getElementById('instantStreamSelect');
        const selectedIndex = parseInt(selectElement.value);
        playIntantSelectedStreamFromPlaylist(selectedIndex);
    });
    
    // Also allow playing with Enter key
    document.getElementById('instantStreamSelect').addEventListener('keydown', function(e) {
        if (e.key === 'Enter') {
            const selectedIndex = parseInt(this.value);
            playIntantSelectedStreamFromPlaylist(selectedIndex);
        }
    });
}

function appendSelectedStreams() {
    const checkboxes = document.querySelectorAll('#playlistSelectionModal .stream-checkbox');
    const selectedStreams = [];
    const playlistData = window.currentPlaylistData;
    
    checkboxes.forEach(checkbox => {
        if (checkbox.checked) {
            const index = parseInt(checkbox.dataset.index);
            selectedStreams.push(playlistData[index]);
        }
    });
    
    // Append selected streams to current playlist
    streams = streams.concat(selectedStreams);
    renderPlaylist();
    
    // Close modal
    document.getElementById('playlistSelectionModal').remove();
}

function replaceWithSelectedStreams() {
    const checkboxes = document.querySelectorAll('#playlistSelectionModal .stream-checkbox');
    const selectedStreams = [];
    const playlistData = window.currentPlaylistData;
    
    checkboxes.forEach(checkbox => {
        if (checkbox.checked) {
            const index = parseInt(checkbox.dataset.index);
            selectedStreams.push(playlistData[index]);
        }
    });
    
    // Replace current playlist with selected streams
    streams = selectedStreams;
    renderPlaylist();
    
    // Close modal
    document.getElementById('playlistSelectionModal').remove();
}

async function playIntantSelectedStreamFromPlaylist(index) {
    const playlistData = window.currentInstantPlayPlaylistData;
    
    // Validate playlist data
    if (!playlistData || !Array.isArray(playlistData)) {
        console.error('Invalid playlist data:', playlistData);
        return;
    }
    
    // Validate index
    if (index < 0 || index >= playlistData.length) {
        console.error('Invalid stream index:', { index, playlistLength: playlistData.length });
        return;
    }
    
    const selectedStream = playlistData[index];
    console.log('Selected stream to play:', index, selectedStream);
    
    // Validate the selected stream
    if (!selectedStream || typeof selectedStream !== 'object') {
        console.error('Invalid stream object at index:', { index, stream: selectedStream });
        return;
    }
    
    // Check if stream has required properties
    if (!selectedStream.url) {
        console.error('Stream missing URL at index:', { index, stream: selectedStream });
        return;
    }
    
    // Validate URL format
    try {
        new URL(selectedStream.url);
    } catch (e) {
        console.error('Invalid stream URL at index:', { index, url: selectedStream.url });
        return;
    }
    
    // Use stream name if available, otherwise generate one
    const streamName = (selectedStream.name && selectedStream.name.trim()) || `Stream ${index + 1}`;
    
    // Show loading state
    const playButton = document.getElementById('playInstantSelectedStreamBtn');
    const originalText = playButton ? playButton.textContent : 'Play';
    if (playButton) {
        playButton.textContent = 'Playing...';
        playButton.disabled = true;
    }
    
    try {
        await sendPlayRequest(selectedStream.url, streamName, index);
        
        // Close modal
        const modal = document.getElementById('instantPlaySelectionModal');
        if (modal) {
            modal.remove();
        }
        
        // Clear the input field
        const urlInput = document.getElementById('instantUrl');
        if (urlInput) {
            urlInput.value = '';
        }
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

async function uploadM3U() {
    const fileInput = document.getElementById('playlistFile');
    const file = fileInput.files[0];
    
    console.log('Uploading M3U playlist file:', file);
    
    if (!file) {
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.m3u') && !fileName.endsWith('.m3u8')) {
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
                    // Reload streams after successful upload
                    loadStreams();
                }
            } else {
                const error = await response.text();
                console.error('Error uploading playlist: ' + error);
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
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
        fileInput.value = '';
        // Restore button state
        if (uploadButton) {
            uploadButton.textContent = originalText || 'Upload M3U';
            uploadButton.disabled = false;
        }
    };
    reader.readAsText(file);
}

async function uploadPLS() {
    const fileInput = document.getElementById('playlistFile');
    const file = fileInput.files[0];
    
    console.log('Uploading PLS playlist file:', file);
    
    if (!file) {
        return;
    }
    
    // Check file extension
    const fileName = file.name.toLowerCase();
    if (!fileName.endsWith('.pls')) {
        // Clear file input on error
        fileInput.value = '';
        return;
    }
    
    // Show loading state
    const uploadButton = document.querySelector('button[onclick="uploadPLS()"]');
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
            // Convert PLS to JSON
            const jsonData = convertPLSToJSON(fileContent);
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
                    // Reload streams after successful upload
                    loadStreams();
                }
            } else {
                const error = await response.text();
                console.error('Error uploading playlist: ' + error);
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
        } finally {
            // Clear file input in all cases
            fileInput.value = '';
            // Restore button state
            if (uploadButton) {
                uploadButton.textContent = originalText || 'Upload PLS';
                uploadButton.disabled = false;
            }
        }
    };
    reader.onerror = function() {
        fileInput.value = '';
        // Restore button state
        if (uploadButton) {
            uploadButton.textContent = originalText || 'Upload PLS';
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
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
        }
    } catch (error) {
        console.error('Error downloading JSON:', error);
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
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
        }
    } catch (error) {
        console.error('Error downloading playlist:', error);
    } finally {
        // Restore button state
        if (downloadButton) {
            downloadButton.textContent = originalText || 'Download M3U';
            downloadButton.disabled = false;
        }
    }
}

async function downloadPLS() {
    // Show loading state
    const downloadButton = document.querySelector('button[onclick="downloadPLS()"]');
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
            const plsContent = convertJSONToPLS(jsonData);
            const blob = new Blob([plsContent], { type: 'audio/x-scpls' });
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'playlist.pls';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
        } else {
            const error = await response.text();
            console.error('Error downloading JSON:', error);
        }
    } catch (error) {
        console.error('Error downloading playlist:', error);
    } finally {
        // Restore button state
        if (downloadButton) {
            downloadButton.textContent = originalText || 'Download PLS';
            downloadButton.disabled = false;
        }
    }
}

// Convert M3U content to JSON
/**
 * @brief Convert M3U playlist content to JSON format
 * Parses M3U format and converts it to the application's JSON stream format
 * 
 * This function parses M3U playlist content and converts it to the JSON format
 * used by the application. It handles the #EXTM3U header, #EXTINF metadata lines,
 * and URL lines. For each valid stream entry, it creates a JSON object with
 * name and URL properties.
 * 
 * The function performs validation on both names (max 128 chars) and URLs
 * (must be valid HTTP/HTTPS URLs, max 256 chars).
 * 
 * @param {string} m3uContent - The raw M3U file content as a string
 * @returns {string} - JSON string representation of the playlist
 * @throws {Error} - If conversion fails due to parsing errors
 */
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
/**
 * @brief Convert JSON playlist to M3U format
 * Converts the application's JSON stream format to M3U playlist format
 * 
 * This function converts the application's JSON playlist format to M3U format
 * for export. It generates a proper M3U header (#EXTM3U) and creates
 * #EXTINF metadata lines for each stream followed by the URL.
 * 
 * The function performs validation on both names (max 128 chars) and URLs
 * (must be valid HTTP/HTTPS URLs, max 256 chars) and skips invalid entries.
 * 
 * @param {Array} jsonData - Array of stream objects with name and url properties
 * @returns {string} - M3U formatted playlist as a string
 * @throws {Error} - If conversion fails due to invalid data format
 */
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

// Convert PLS content to JSON
/**
 * @brief Convert PLS playlist content to JSON format
 * Parses PLS format and converts it to the application's JSON stream format
 * 
 * This function parses PLS playlist content and converts it to the JSON format
 * used by the application. It handles the [playlist] header, File/Title entries,
 * and URL lines. For each valid stream entry, it creates a JSON object with
 * name and URL properties.
 * 
 * The function performs validation on both names (max 128 chars) and URLs
 * (must be valid HTTP/HTTPS URLs, max 256 chars).
 * 
 * @param {string} plsContent - The raw PLS file content as a string
 * @returns {string} - JSON string representation of the playlist
 * @throws {Error} - If conversion fails due to parsing errors
 */
function convertPLSToJSON(plsContent) {
    try {
        const lines = plsContent.split('\n');
        const streams = [];
        const entries = {};
        let entryCount = 0;
        
        // First pass: parse all entries
        for (let i = 0; i < lines.length; i++) {
            const line = lines[i].trim();
            
            if (line.length === 0) continue;
            
            if (line.startsWith('NumberOfEntries=')) {
                entryCount = parseInt(line.split('=')[1]) || 0;
            } else if (line.startsWith('File')) {
                const match = line.match(/File(\d+)=(.+)/);
                if (match) {
                    const index = match[1];
                    const url = match[2];
                    if (!entries[index]) entries[index] = {};
                    entries[index].url = url;
                }
            } else if (line.startsWith('Title')) {
                const match = line.match(/Title(\d+)=(.+)/);
                if (match) {
                    const index = match[1];
                    const title = match[2];
                    if (!entries[index]) entries[index] = {};
                    entries[index].title = title;
                }
            }
        }
        
        // Second pass: convert to streams array
        for (const index in entries) {
            const entry = entries[index];
            if (entry.url && (entry.url.startsWith('http://') || entry.url.startsWith('https://'))) {
                // Validate URL
                try {
                    new URL(entry.url);
                } catch (e) {
                    console.warn('Skipping invalid URL in PLS file:', entry.url);
                    continue;
                }
                
                // Use title if available, otherwise generate a name
                let name = entry.title || 'Stream ' + (streams.length + 1);
                
                // Validate name
                if (name.length > 128) {
                    name = name.substring(0, 125) + '...';
                }
                
                if (entry.url.length <= 256) {
                    streams.push({
                        name: name,
                        url: entry.url
                    });
                } else {
                    console.warn('Skipping URL that exceeds maximum length:', entry.url);
                }
            }
        }
        
        return JSON.stringify(streams);
    } catch (error) {
        console.error('Error converting PLS to JSON:', error);
        throw new Error('Failed to convert PLS file: ' + error.message);
    }
}

// Convert JSON content to PLS
/**
 * @brief Convert JSON playlist to PLS format
 * Converts the application's JSON stream format to PLS playlist format
 * 
 * This function converts the application's JSON playlist format to PLS format
 * for export. It generates a proper PLS header ([playlist]), NumberOfEntries,
 * and File/Title entries for each stream.
 * 
 * The function performs validation on both names (max 128 chars) and URLs
 * (must be valid HTTP/HTTPS URLs, max 256 chars) and skips invalid entries.
 * 
 * @param {Array} jsonData - Array of stream objects with name and url properties
 * @returns {string} - PLS formatted playlist as a string
 * @throws {Error} - If conversion fails due to invalid data format
 */
function convertJSONToPLS(jsonData) {
    try {
        let plsContent = '[playlist]\n';
        let entryCount = 0;
        
        if (!Array.isArray(jsonData)) {
            throw new Error('Invalid JSON format: expected array of streams');
        }
        
        jsonData.forEach((item, index) => {
            if (item.name && item.url) {
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
                
                // Validate and sanitize name
                let sanitizedName = item.name.trim();
                if (sanitizedName.length > 128) {
                    sanitizedName = sanitizedName.substring(0, 125) + '...';
                }
                
                entryCount++;
                plsContent += `File${entryCount}=${item.url}\n`;
                plsContent += `Title${entryCount}=${sanitizedName}\n`;
            }
        });
        
        plsContent += `NumberOfEntries=${entryCount}\n`;
        plsContent += 'Version=2\n';
        
        return plsContent;
    } catch (error) {
        console.error('Error converting JSON to PLS:', error);
        throw new Error('Failed to convert playlist to PLS: ' + error.message);
    }
}

// WiFi functions
let networkCount = 1;
let configuredNetworks = [];

async function loadWiFiConfiguration() {
    try {
        const response = await fetch('/api/wifi/config');
        const data = await response.json();
        
        // Handle consistent data structure for configured networks
        let configNetworks = [];
        
        if (Array.isArray(data)) {
            // If array of objects with ssid property (new format)
            if (data.length > 0 && typeof data[0] === 'object' && data[0].hasOwnProperty('ssid')) {
                configuredNetworks = data.map(network => network.ssid);
                configNetworks = data.map(network => ({
                    ssid: network.ssid || '',
                    password: network.password || ''
                }));
            }
            // If array of strings (SSIDs only - old format)
            else {
                configuredNetworks = data;
                configNetworks = data.map((ssid, index) => ({ ssid, password: '' }));
            }
        } else if (data.configured && Array.isArray(data.configured)) {
            // If object with configured array
            configuredNetworks = data.configured;
            configNetworks = data.configured.map((ssid, index) => ({ ssid, password: '' }));
        } else if (data.networks && Array.isArray(data.networks)) {
            // If object with networks array containing objects with ssid property
            configuredNetworks = data.networks.map(network => 
                typeof network === 'string' ? network : network.ssid
            ).filter(ssid => ssid);
            configNetworks = data.networks.map(network => ({
                ssid: typeof network === 'string' ? network : network.ssid || '',
                password: network.password || ''
            }));
        } else {
            configuredNetworks = [];
            configNetworks = [];
        }
        
        // Also populate the form with configured networks
        // Clear existing fields except the first one
        const networkFields = document.getElementById('networkFields');
        if (networkFields) {
            while (networkFields.children.length > 1) {
                networkFields.removeChild(networkFields.lastChild);
            }
            
            // Reset network count
            networkCount = 1;
            
            // Populate with configured networks
            if (configNetworks.length > 0) {
                // Fill the first entry
                const firstSSID = document.getElementById('ssid0');
                if (firstSSID) {
                    firstSSID.value = configNetworks[0].ssid || '';
                }
                
                // Add additional entries for each configured network
                for (let i = 1; i < configNetworks.length && i < 5; i++) {
                    addNetworkField();
                    const ssidElement = document.getElementById(`ssid${i}`);
                    if (ssidElement) {
                        ssidElement.value = configNetworks[i].ssid || '';
                    }
                }
                networkCount = configNetworks.length;
            }
        }
        
        return data;
    } catch (error) {
        console.error('Error loading WiFi configuration:', error);
        configuredNetworks = [];
        return [];
    }
}

// Keep the individual functions for backward compatibility but make them use the unified function
async function loadConfiguredNetworks() {
    try {
        await loadWiFiConfiguration();
    } catch (error) {
        console.error('Error loading configured networks:', error);
        configuredNetworks = [];
    }
}

function addNetworkField() {
    if (networkCount >= 5) {
        return;
    }
    
    const networkFields = document.getElementById('networkFields');
    const newEntry = document.createElement('div');
    newEntry.className = "network-entry"
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
        return;
    }
    
    button.parentElement.remove();
    networkCount = document.querySelectorAll('.network-entry').length;
}

function handleWiFiFormSubmit(event) {
    // Prevent default form submission
    event.preventDefault();
    
    const networks = [];
    
    // Collect all network entries
    const networkEntries = document.querySelectorAll('.network-entry');
    networkEntries.forEach((entry, index) => {
        const ssidInput = entry.querySelector(`#ssid${index}`);
        const passwordInput = entry.querySelector(`#password${index}`);
        
        if (ssidInput && ssidInput.value.trim()) {
            const network = {
                ssid: ssidInput.value.trim()
            };
            
            if (passwordInput && passwordInput.value) {
                network.password = passwordInput.value;
            }
            
            networks.push(network);
        }
    });
    
    if (networks.length === 0) {
        return;
    }
    
    // Show saving state
    const saveButton = document.querySelector('button[type="submit"]');
    const originalText = saveButton ? saveButton.textContent : 'Save Configuration';
    if (saveButton) {
        saveButton.textContent = 'Saving...';
        saveButton.disabled = true;
    }
    
    fetch('/api/wifi/save', {
        method: 'POST',
        headers: {
            'Content-Type': 'application/json',
        },
        body: JSON.stringify(networks)
    })
    .then(response => {
        if (response.ok) {
            // Success - no toast needed
        }
    })
    .catch(error => {
        console.error('Error:', error);
    })
    .finally(() => {
        // Restore button state
        if (saveButton) {
            saveButton.textContent = originalText;
            saveButton.disabled = false;
        }
    });
}

// Attach event listener to WiFi form when page loads
window.addEventListener('load', function() {
    const wifiForm = document.getElementById('wifiForm');
    if (wifiForm) {
        wifiForm.addEventListener('submit', handleWiFiFormSubmit);
    }
});

// Override the scanNetworks function to highlight configured networks
function scanNetworks() {
    const networksDiv = document.getElementById('networks');
    if (!networksDiv) return;
    
    networksDiv.innerHTML = '<p class="scanning" aria-busy="true">Scanning for networks...</p>';
    
    // Disable scan button during scan
    const scanButton = document.querySelector('button[onclick="scanNetworks()"]');
    const originalText = scanButton ? scanButton.textContent : null;
    if (scanButton) {
        scanButton.textContent = 'Scanning...';
        scanButton.disabled = true;
    }
    
    fetch('/api/wifi/scan')
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
                networkDiv.className = 'network-item grid';
                
                // Check if this network is already configured
                const isConfigured = data.configured && Array.isArray(data.configured) && data.configured.includes(network.ssid);
                
                // Signal strength indicator
                let signalClass = 'signal-weak';
                if (network.rssi > -60) signalClass = 'signal-strong';
                else if (network.rssi > -70) signalClass = 'signal-medium';
                
                if (isConfigured) {
                    networkDiv.classList.add('configured-network');
                    networkDiv.innerHTML = `
                        <div class="configured-marker">★</div>
                        <div class="network-name">${network.ssid}</div>
                        <div class="network-rssi ${signalClass}">${network.rssi} dBm</div>
                        <button class="btn-small secondary" onclick="selectNetwork('${network.ssid}')">Reconfigure</button>
                    `;
                } else {
                    networkDiv.innerHTML = `
                        <div class="configured-marker"></div>
                        <div class="network-name">${network.ssid}</div>
                        <div class="network-rssi ${signalClass}">${network.rssi} dBm</div>
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
                scanButton.textContent = originalText || 'Refresh networks';
                scanButton.disabled = false;
            }
        });
}

// Load current connection status
function loadConnectionStatus() {
    const statusDiv = document.getElementById('connection-status');
    if (!statusDiv) return;
    
    statusDiv.innerHTML = '<p aria-busy="true">Loading connection status...</p>';
    
    fetch('/api/wifi/status')
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

// Configuration import/export functions
async function exportAllConfiguration() {
    try {
        const response = await fetch('/api/config/export');
        if (response.ok) {
            const blob = await response.blob();
            const url = window.URL.createObjectURL(blob);
            const a = document.createElement('a');
            a.href = url;
            a.download = 'nettuner-config-export.json';
            document.body.appendChild(a);
            a.click();
            window.URL.revokeObjectURL(url);
            document.body.removeChild(a);
        } else {
            // Try to parse JSON error response
            let errorMessage = 'Error exporting configurations: ' + response.status;
            try {
                const errorData = await response.json();
                if (errorData.message) {
                    errorMessage = errorData.message;
                }
            } catch (e) {
                // If JSON parsing fails, use the status text
                if (response.statusText) {
                    errorMessage = 'Error exporting configurations: ' + response.statusText;
                }
            }
            showModal('Error', errorMessage);
        }
    } catch (error) {
        console.error('Error exporting configurations:', error);
        showModal('Error', 'Error exporting configurations: ' + error.message);
    }
}

// Handle import file selection
function handleImportFileSelect() {
    const fileInput = document.getElementById('importFile');
    const importButton = document.getElementById('importButton');
    importButton.disabled = !fileInput.files.length;
}

// Import all configuration
async function importAllConfiguration() {
    const fileInput = document.getElementById('importFile');
    const importButton = document.getElementById('importButton');
    const file = fileInput.files[0];
    
    if (!file) {
        showModal('Import Error', 'Please select a file to import');
        return;
    }
    
    // Show loading state
    const originalText = importButton.textContent;
    importButton.textContent = 'Importing...';
    importButton.disabled = true;
    
    try {
        // Read file content as text
        const fileContent = await readFileAsText(file);
        
        const response = await fetch('/api/config/import', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json',
                'X-Requested-With': 'XMLHttpRequest'
            },
            body: fileContent
        });
        
        if (response.ok) {
            // Try to parse JSON success response
            try {
                const result = await response.json();
                if (result.status === 'success') {
                    showModal('Import Successful', result.message || 'Configuration imported successfully. Device restart required for changes to take effect.');
                } else {
                    showModal('Import Error', result.message || 'Error importing configuration');
                }
            } catch (e) {
                // Fallback if JSON parsing fails
                showModal('Import Successful', 'Configuration imported successfully. Device restart required for changes to take effect.');
            }
            // Clear the file input
            fileInput.value = '';
        } else {
            // Try to parse JSON error response
            let errorMessage = 'Error importing configuration: ' + response.status;
            try {
                const errorData = await response.json();
                if (errorData.message) {
                    errorMessage = errorData.message;
                }
            } catch (e) {
                // If JSON parsing fails, use the status text
                if (response.statusText) {
                    errorMessage = 'Error importing configuration: ' + response.statusText;
                }
            }
            showModal('Import Error', errorMessage);
        }
    } catch (error) {
        console.error('Error importing configurations:', error);
        showModal('Import Error', 'Error importing configurations: ' + error.message);
    } finally {
        // Restore button state
        importButton.textContent = originalText;
        importButton.disabled = false;
    }
}

// Helper function to read file as text
function readFileAsText(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = e => resolve(e.target.result);
        reader.onerror = e => reject(e);
        reader.readAsText(file);
    });
}

// Simple modal dialog function
function showModal(title, message) {
    // Remove any existing modal
    const existingModal = document.getElementById('configModal');
    if (existingModal) {
        existingModal.remove();
    }
    
    // Create modal element
    const modal = document.createElement('dialog');
    modal.id = 'configModal';
    modal.innerHTML = `
        <article>
            <header>
                <h2>${title}</h2>
            </header>
            <p>${message}</p>
            <footer>
                <div role="group">
                    <button onclick="document.getElementById('configModal').remove()">OK</button>
                </div>
            </footer>
        </article>
    `;
    
    document.body.appendChild(modal);
    modal.showModal();
}
