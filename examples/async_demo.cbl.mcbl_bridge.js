// McBL# MBJKDT JavaScript Adapter
// Provides bidirectional JSON communication with McBL#

const fs   = require('fs');
const path = require('path');

const M2J_PIPE = '/tmp/mcbl_m2j.json';
const J2M_PIPE = '/tmp/mcbl_j2m.json';

class McblBridge {
    constructor() { this.connected = false; }

    connect() { this.connected = true; }
    disconnect() { this.connected = false; }

    // Send JSON to McBL#
    send(data) {
        fs.writeFileSync(J2M_PIPE, JSON.stringify(data));
    }

    // Receive JSON from McBL#
    receive() {
        try {
            const raw = fs.readFileSync(M2J_PIPE, 'utf8');
            return JSON.parse(raw.trim());
        } catch (e) { return null; }
    }

    // Watch for new messages from McBL#
    watch(callback) {
        fs.watchFile(M2J_PIPE, { interval: 100 }, () => {
            const msg = this.receive();
            if (msg) callback(msg);
        });
    }
}

module.exports = { McblBridge };
