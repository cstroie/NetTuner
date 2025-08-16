let streams = [];

async function loadStreams() {
    try {
        console.log('Loading streams from /api/streams');
        const response = await fetch('/api/streams');
        console.log('Streams response status:', response.status);
        if (!response.ok) {
            throw new Error('Network response was not ok');
        }
        streams = await response.json();
        console.log('Loaded streams:', streams);
        renderPlaylist();
    } catch (error) {
        console.error('Error loading streams:', error);
    }
}

function renderPlaylist() {
    const tbody = document.getElementById('playlistBody');
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
    const name = document.getElementById('name').value;
    const url = document.getElementById('url').value;
    
    console.log('Adding stream:', { name, url });
    
    if (!name || !url) {
        alert('Please enter both name and URL');
        return;
    }
    
    streams.push({ name: name, url: url });
    renderPlaylist();
    
    // Clear form
    document.getElementById('name').value = '';
    document.getElementById('url').value = '';
}

function updateStream(index, field, value) {
    if (streams[index]) {
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
            alert('Playlist saved successfully!');
        } else {
            const error = await response.text();
            console.error('Error saving playlist:', error);
            alert('Error saving playlist: ' + error);
        }
    } catch (error) {
        console.error('Error saving playlist:', error);
        alert('Error saving playlist: ' + error.message);
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
            alert(message);
            
            if (response.ok) {
                // Reload streams after successful upload
                loadStreams();
                // Clear file input
                fileInput.value = '';
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
            alert('Error uploading playlist file: ' + error.message);
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
    
    // Read file content
    const reader = new FileReader();
    reader.onload = async function(e) {
        const fileContent = e.target.result;
        console.log('File content:', fileContent);
        
        // Convert M3U to JSON
        const jsonData = convertM3UToJSON(fileContent);
        
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
            alert(message);
            
            if (response.ok) {
                // Reload streams after successful upload
                loadStreams();
                // Clear file input
                fileInput.value = '';
            }
        } catch (error) {
            console.error('Error uploading playlist:', error);
            alert('Error uploading playlist file: ' + error.message);
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

// Initialize
window.onload = function() {
    loadStreams();
};
