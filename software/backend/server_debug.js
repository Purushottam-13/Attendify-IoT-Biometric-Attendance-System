/**
 * ============================================================
 *  ATTENDIFY - PHASE 4 BACKEND SERVER
 *  Authentication & Role-Based Access Control
 * ============================================================
 */

const path = require('path');
// Load root .env first, then backend/.env for backend-specific defaults.
// NOTE: we allow backend/.env to override root .env so local project-wide files
// (e.g. .env in repo root) do not unintentionally shadow backend settings.
require('dotenv').config({ silent: true });
require('dotenv').config({ path: path.join(__dirname, '.env'), override: true, silent: true });

// Helpful startup logging to diagnose multiple .env files and precedence.
try {
    const rootPath = path.resolve(process.cwd(), '.env');
    const backendPath = path.join(__dirname, '.env');
    const fs = require('fs');
    console.log('[ENV] Root .env exists:', fs.existsSync(rootPath) ? rootPath : 'no');
    console.log('[ENV] Backend .env exists:', fs.existsSync(backendPath) ? backendPath : 'no');
} catch (e) {
    // non-fatal
}
const express = require('express');
const bodyParser = require('body-parser');
const cors = require('cors');
const crypto = require('crypto');
const os = require('os');
const dgram = require('dgram');
const { LocalStorage, CloudStorage } = require('./storage');
const { verifyTokenWithRole, requireRole } = require('./authMiddleware');
const admin = require('firebase-admin');
const mailer = require('./mailer');

function normalizeMode(value, allowed, fallback, aliases = {}) {
    const raw = String(value || '').trim().toUpperCase();
    const normalized = aliases[raw] || raw || fallback;
    if (!allowed.includes(normalized)) {
        console.warn(`[MODE] Invalid mode "${value}". Falling back to ${fallback}.`);
        return fallback;
    }
    return normalized;
}

// Explicit runtime modes. Supported combinations:
// - LOCAL storage + LOCAL auth: local development/demo
// - CLOUD storage + FIREBASE auth: production/cloud
const STORAGE_MODE = normalizeMode(process.env.STORAGE_MODE, ['LOCAL', 'CLOUD'], 'CLOUD', {
    FIREBASE: 'CLOUD',
    FIRESTORE: 'CLOUD',
    JSON: 'LOCAL'
});
const AUTH_MODE = normalizeMode(process.env.AUTH_MODE, ['LOCAL', 'FIREBASE'], STORAGE_MODE === 'LOCAL' ? 'LOCAL' : 'FIREBASE');
const firebaseRequired = STORAGE_MODE === 'CLOUD' || AUTH_MODE === 'FIREBASE';
let firebaseAdminAvailable = false;
const supportedLocalMode = STORAGE_MODE === 'LOCAL' && AUTH_MODE === 'LOCAL';
const supportedCloudMode = STORAGE_MODE === 'CLOUD' && AUTH_MODE === 'FIREBASE';

console.log('[SERVER] Storage Mode:', STORAGE_MODE);
console.log('[SERVER] Auth Mode:', AUTH_MODE);

if (supportedLocalMode) {
    console.log('[MODE] Running in LOCAL DEV MODE');
} else if (supportedCloudMode) {
    console.log('[MODE] Running in CLOUD MODE');
} else {
    console.error(`[MODE] Unsupported runtime mode: STORAGE_MODE=${STORAGE_MODE}, AUTH_MODE=${AUTH_MODE}. Expected LOCAL+LOCAL or CLOUD+FIREBASE.`);
    console.error('[MODE] Refusing to start with a mixed or invalid configuration. Update .env or environment variables and restart.');
    process.exit(1);
}

// Initialize Firebase Admin (Global)
if (firebaseRequired && admin.apps.length === 0) {
    try {
        let serviceAccount;
        if (process.env.FIREBASE_SERVICE_ACCOUNT) {
            serviceAccount = JSON.parse(process.env.FIREBASE_SERVICE_ACCOUNT);
        } else {
            serviceAccount = require('./serviceAccountKey.json');
        }

        admin.initializeApp({
            credential: admin.credential.cert(serviceAccount)
        });
        firebaseAdminAvailable = true;
        console.log('[SERVER] Firebase Admin initialized globally');
    } catch (error) {
        console.error('[SERVER] Firebase init failed:', error.message);
    }
} else if (admin.apps.length > 0) {
    firebaseAdminAvailable = true;
} else {
    console.log('[SERVER] Firebase Admin skipped for LOCAL auth/storage mode');
}

const app = express();
const PORT = process.env.PORT || 3003;
const DISCOVERY_PORT = parseInt(process.env.DISCOVERY_PORT || '4210', 10);
const ADVERTISED_SERVER_URL = String(process.env.ADVERTISED_SERVER_URL || '').trim();
const ADVERTISED_SERVER_IP = normalizeV4(String(process.env.ADVERTISED_SERVER_IP || '').trim());
let discoveryServer = null;

function normalizeV4(ip) {
    if (!ip) return '';
    return String(ip).replace('::ffff:', '');
}

function parseIPv4(ip) {
    const clean = normalizeV4(ip);
    const parts = clean.split('.').map(Number);
    if (parts.length !== 4 || parts.some(n => Number.isNaN(n) || n < 0 || n > 255)) {
        return null;
    }
    return parts;
}

function normalizeBaseUrl(url) {
    let value = String(url || '').trim();
    if (!value) return '';
    if (!/^https?:\/\//i.test(value)) {
        value = `http://${value}`;
    }
    while (value.endsWith('/')) {
        value = value.slice(0, -1);
    }
    return value;
}

function hostFromUrl(url) {
    try {
        const parsed = new URL(url);
        return normalizeV4(parsed.hostname || '');
    } catch (_) {
        return '';
    }
}

function same24(ipA, ipB) {
    const a = parseIPv4(ipA);
    const b = parseIPv4(ipB);
    return !!a && !!b && a[0] === b[0] && a[1] === b[1] && a[2] === b[2];
}

function scoreInterface(name, iface) {
    let score = 0;
    const lower = String(name || '').toLowerCase();

    if (iface.internal) score -= 100;
    if (lower.includes('virtual') || lower.includes('vmware') || lower.includes('vethernet') || lower.includes('hyper-v') || lower.includes('loopback') || lower.includes('tap') || lower.includes('vbox') || lower.includes('docker') || lower.includes('wsl')) {
        score -= 250;
    }

    if (lower.includes('wi-fi') || lower.includes('wifi') || lower.includes('wireless') || lower.includes('wlan')) {
        score += 80;
    }
    if (lower.includes('ethernet')) {
        score += 35;
    }

    const addr = parseIPv4(iface.address);
    if (addr) {
        if (addr[0] === 10) score += 10;
        if (addr[0] === 172 && addr[1] >= 16 && addr[1] <= 31) score += 10;
        if (addr[0] === 192 && addr[1] === 168) score += 10;
        if (addr[0] === 169 && addr[1] === 254) score -= 200;
    }

    return score;
}

function getPrimaryLocalIPv4(preferredRemoteIp) {
    const interfaces = os.networkInterfaces();
    const candidates = [];

    for (const name of Object.keys(interfaces)) {
        for (const iface of interfaces[name]) {
            if (iface.family === 'IPv4' && !iface.internal) {
                candidates.push({
                    name,
                    address: normalizeV4(iface.address),
                    score: scoreInterface(name, iface)
                });
            }
        }
    }

    if (candidates.length === 0) {
        return '127.0.0.1';
    }

    const filtered = candidates.filter(c => c.score > -200);
    const pool = filtered.length > 0 ? filtered : candidates;

    if (preferredRemoteIp) {
        const match = pool.find(c => same24(c.address, preferredRemoteIp));
        if (match) {
            return normalizeV4(match.address);
        }
    }

    pool.sort((a, b) => b.score - a.score);
    return normalizeV4(pool[0].address);
}

function getAdvertisedServer(preferredRemoteIp) {
    const manualUrl = normalizeBaseUrl(ADVERTISED_SERVER_URL);
    if (manualUrl) {
        const host = hostFromUrl(manualUrl);
        return {
            ip: host || ADVERTISED_SERVER_IP || getPrimaryLocalIPv4(preferredRemoteIp),
            url: manualUrl
        };
    }

    // In cloud deployment, PUBLIC_BASE_URL is the canonical public URL
    const publicUrl = normalizeBaseUrl(String(process.env.PUBLIC_BASE_URL || '').trim());
    if (publicUrl) {
        const host = hostFromUrl(publicUrl);
        return { ip: host || 'localhost', url: publicUrl };
    }

    const ip = ADVERTISED_SERVER_IP || getPrimaryLocalIPv4(preferredRemoteIp);
    return {
        ip,
        url: `http://${ip}:${PORT}`
    };
}

function startDiscoveryResponder() {
    discoveryServer = dgram.createSocket('udp4');

    discoveryServer.on('error', (err) => {
        if (err && err.code === 'EADDRINUSE') {
            console.error(`[DISCOVERY] UDP port ${DISCOVERY_PORT} is already in use. Discovery responder disabled for this process.`);
            if (discoveryServer) {
                discoveryServer.close();
                discoveryServer = null;
            }
            return;
        }
        console.error('[DISCOVERY] UDP error:', err.message);
    });

    discoveryServer.on('message', (msg, rinfo) => {
        const text = String(msg || '').trim();
        if (text !== 'ATTENDIFY_DISCOVER') {
            return;
        }

        const advertised = getAdvertisedServer(rinfo.address);
        const payload = JSON.stringify({
            type: 'ATTENDIFY_DISCOVERY_RESPONSE',
            ip: advertised.ip,
            port: Number(PORT),
            url: advertised.url
        });

        discoveryServer.send(Buffer.from(payload), rinfo.port, rinfo.address);
        console.log(`[DISCOVERY] Responded to ${rinfo.address}:${rinfo.port} with ${advertised.url}`);
    });

    discoveryServer.bind(DISCOVERY_PORT, '0.0.0.0', () => {
        console.log(`[DISCOVERY] UDP responder listening on 0.0.0.0:${DISCOVERY_PORT}`);
    });
}

let storage;
if (STORAGE_MODE === 'CLOUD') {
    storage = new CloudStorage(
        path.join(__dirname, 'database.json'),
        path.join(__dirname, 'audit.log')
    );
} else {
    storage = new LocalStorage(
        path.join(__dirname, 'database.json'),
        path.join(__dirname, 'audit.log')
    );
}

console.log('[SERVER] Storage initialized:', typeof storage);
console.log('[SERVER] Has getUserByEmail:', typeof storage.getUserByEmail);

// Authorized ESP32 Devices
const AUTHORIZED_DEVICES = [
    {
        deviceId: "ESP32_CLASSROOM_01",
        secret: "A1B2C3D4_SECRET"
    },
    {
        deviceId: "ESP32_CLASSROOM_02",
        secret: "A1B2C3D4_SECRET"
    }
];

// Middleware
app.use(cors());
app.use(bodyParser.json());
app.use(express.static(path.join(__dirname, '../frontend')));

function getRequestIp(req) {
    const forwarded = String(req.headers['x-forwarded-for'] || '').split(',')[0].trim();
    const rawIp = forwarded || req.socket?.remoteAddress || req.ip || '';
    return String(rawIp).replace(/^::ffff:/, '');
}

function touchDevice(deviceId, req) {
    if (!deviceId || typeof storage.updateDeviceStatus !== 'function') return;
    storage.updateDeviceStatus(deviceId, {
        deviceId,
        lastSeen: new Date().toISOString(),
        lastIp: getRequestIp(req),
        userAgent: req.headers['user-agent'] || ''
    }).catch(error => {
        console.error('[DEVICE] Failed to update device heartbeat:', error.message);
    });
}

// Device Auth Middleware
function validateDevice(req, res, next) {
    const deviceId = req.headers['x-device-id'];
    const deviceSecret = req.headers['x-device-secret'];

    if (!deviceId || !deviceSecret) {
        storage.addAuditLog({ event: 'DEVICE_REJECTED', details: 'Missing auth headers' });
        return res.status(401).json({ success: false, message: 'Device authentication required' });
    }

    const device = AUTHORIZED_DEVICES.find(d => d.deviceId === deviceId && d.secret === deviceSecret);
    if (!device) {
        storage.addAuditLog({ event: 'DEVICE_REJECTED', details: `Invalid: ${deviceId}` });
        return res.status(401).json({ success: false, message: 'Device not authorized' });
    }

    req.deviceId = deviceId;
    touchDevice(deviceId, req);
    next();
}

// Queue Status Constants
const QUEUE_STATUS = {
    WAITING: 'WAITING',
    AUTHORIZED: 'AUTHORIZED',
    IN_PROGRESS: 'IN_PROGRESS',
    COMPLETED: 'COMPLETED',
    FAILED: 'FAILED'
};

function getModeInfo() {
    return {
        storageMode: STORAGE_MODE,
        authMode: AUTH_MODE,
        modeLabel: supportedLocalMode ? 'LOCAL DEV MODE' : (supportedCloudMode ? 'CLOUD MODE' : 'UNSUPPORTED MODE'),
        supported: supportedLocalMode || supportedCloudMode,
        firebaseAdmin: admin.apps.length > 0 || firebaseAdminAvailable ? 'available' : 'unavailable'
    };
}

// Resolve a public base URL for links in emails. Prefer explicit PUBLIC_BASE_URL,
// otherwise derive from the incoming request (respect x-forwarded-proto when behind proxy).
function resolveBaseUrl(req) {
    if (process.env.PUBLIC_BASE_URL && String(process.env.PUBLIC_BASE_URL).trim()) {
        return String(process.env.PUBLIC_BASE_URL).replace(/\/$/, '');
    }
    if (!req) return 'http://localhost:3003';
    const proto = String(req.headers['x-forwarded-proto'] || req.protocol || 'http');
    const host = req.get && req.get('host') ? req.get('host') : `${req.hostname || 'localhost'}:${PORT}`;
    return `${proto}://${host}`;
}

/**
 * Generate a temporary password for new accounts.
 * Strong enough for initial login, encourages password change.
 * Format: 2 uppercase + 2 lowercase + 2 digits + 2 special + 4 random mixed
 */
function generateTemporaryPassword(length = 12) {
    const uppercase = 'ABCDEFGHIJKLMNOPQRSTUVWXYZ';
    const lowercase = 'abcdefghijklmnopqrstuvwxyz';
    const digits = '0123456789';
    const special = '!@#$%^&*';
    const all = uppercase + lowercase + digits + special;

    let password = '';
    password += uppercase[Math.floor(Math.random() * uppercase.length)];
    password += uppercase[Math.floor(Math.random() * uppercase.length)];
    password += lowercase[Math.floor(Math.random() * lowercase.length)];
    password += lowercase[Math.floor(Math.random() * lowercase.length)];
    password += digits[Math.floor(Math.random() * digits.length)];
    password += digits[Math.floor(Math.random() * digits.length)];
    password += special[Math.floor(Math.random() * special.length)];
    password += special[Math.floor(Math.random() * special.length)];

    // Fill remaining with random chars
    while (password.length < length) {
        password += all[Math.floor(Math.random() * all.length)];
    }

    // Shuffle password
    return password.split('').sort(() => Math.random() - 0.5).join('');
}

async function probeDatabaseHealth() {
    try {
        const snapshot = await storage.loadData();
        await storage.saveData(snapshot);
        return { status: 'ok', read: 'ok', write: 'ok' };
    } catch (error) {
        console.error('[HEALTH] Database probe failed:', error.message);
        return {
            status: 'error',
            read: 'error',
            write: 'error',
            reason: error.message
        };
    }
}

function getSmtpHealth() {
    const configured = Boolean(process.env.SMTP_EMAIL && process.env.SMTP_APP_PASSWORD);
    return {
        status: configured ? 'configured' : 'missing',
        configured,
        email: process.env.SMTP_EMAIL ? 'set' : 'missing',
        password: process.env.SMTP_APP_PASSWORD ? 'set' : 'missing'
    };
}

function createLocalToken(email) {
    return `LOCAL_TOKEN_${Buffer.from(email).toString('base64')}`;
}

// ============================================================
// AUTHENTICATION ENDPOINTS
// ============================================================

// Local Login Fallback (For Full Offline Mode)
app.post('/api/auth/login-local', async (req, res) => {
    const { email, password } = req.body;
    try {
        if (AUTH_MODE !== 'LOCAL') {
            return res.status(403).json({
                success: false,
                message: `Local login is disabled while AUTH_MODE=${AUTH_MODE}`
            });
        }

        const user = await storage.getUserByEmail(email);
        if (!user) {
            return res.status(401).json({ success: false, message: 'User not found in local DB' });
        }

        // For absolute offline mode, we check a local password field
        // If it doesn't exist, we fallback to a default (e.g., admin123 or roll number)
        // FOR NOW: We allow login if user exists in DB and password matches email prefix or '123'
        // In production, you would store hashed passwords in database.json
        const expectedPass = user.password || '123';

        if (password !== expectedPass) {
            return res.status(401).json({ success: false, message: 'Invalid local password' });
        }

        const localToken = createLocalToken(email);

        res.json({
            success: true,
            token: localToken,
            user: {
                email: user.email,
                role: user.role,
                is_gfm: user.is_gfm === true || user.role === 'GFM'
            },
            mode: getModeInfo()
        });
    } catch (error) {
        console.error('Local login error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.get('/api/auth/me', verifyTokenWithRole(storage), async (req, res) => {
    try {
        const user = await storage.getUserByEmail(req.user.email);
        if (!user) {
            return res.status(404).json({ success: false, message: 'User not found' });
        }
        res.json({
            success: true,
            user: {
                uid: req.user.uid,
                email: user.email,
                role: user.role,
                is_gfm: user.is_gfm === true || user.role === 'GFM',  // Support both flag and role
                displayName: user.name || user.email.split('@')[0],
                assignedDevices: user.assignedDevices || []
            },
            mode: getModeInfo()
        });
    } catch (error) {
        console.error('auth-me error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.post('/api/auth/forgot-password', async (req, res) => {
    const { email } = req.body;
    if (!email) return res.status(400).json({ success: false, message: 'Email required' });

    try {
        const user = await storage.getUserByEmail(email);
        if (!user) {
            // Do not reveal if user exists or not for security, just say "If email exists..."
            // But for ease of use in Attendify, we can return an error
            return res.status(404).json({ success: false, message: 'User not found' });
        }

        const tempPassword = generateTemporaryPassword(12);
        await storage.updateUser(email, { password: tempPassword });
        
        const mailer = require('./mailer');
        const emailSent = await mailer.sendPasswordResetEmail(email, tempPassword);
        
        if (emailSent) {
            res.json({ success: true, message: 'A temporary password has been sent to your email.' });
        } else {
            // Revert password change if email failed so they aren't locked out? 
            // Or just give them the error. Better to leave it and tell them to contact admin.
            res.status(500).json({ success: false, message: 'Could not send email. Please contact the administrator to reset your password manually.' });
        }
    } catch (error) {
        console.error('forgot-password error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.post('/api/auth/change-password', verifyTokenWithRole(storage), async (req, res) => {
    const { currentPassword, newPassword } = req.body;
    if (!currentPassword || !newPassword) {
        return res.status(400).json({ success: false, message: 'Current and new passwords are required' });
    }

    try {
        const user = await storage.getUserByEmail(req.user.email);
        if (!user) return res.status(404).json({ success: false, message: 'User not found' });

        // Verify current password
        if (user.password !== currentPassword) {
            return res.status(401).json({ success: false, message: 'Incorrect current password' });
        }

        await storage.updateUser(user.email, { password: newPassword });
        await storage.addAuditLog({ event: 'PASSWORD_CHANGED', target: user.email, by: user.email });
        
        res.json({ success: true, message: 'Password updated successfully' });
    } catch (error) {
        console.error('change-password error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

function decodeBase64Url(input) {
    const normalized = String(input).replace(/-/g, '+').replace(/_/g, '/');
    const padLength = (4 - (normalized.length % 4)) % 4;
    const padded = normalized + '='.repeat(padLength);
    return Buffer.from(padded, 'base64').toString('utf-8');
}

function verifyErpJwt(token) {
    if (!token) {
        throw new Error('ERP token missing');
    }

    const parts = String(token).split('.');
    if (parts.length !== 3) {
        throw new Error('ERP token format invalid');
    }

    const [headerEncoded, payloadEncoded, signature] = parts;
    const secret = process.env.ERP_SSO_SECRET;
    if (!secret) {
        throw new Error('ERP_SSO_SECRET is not configured');
    }

    const expected = crypto
        .createHmac('sha256', secret)
        .update(`${headerEncoded}.${payloadEncoded}`)
        .digest('base64url');

    if (signature !== expected) {
        throw new Error('ERP token signature invalid');
    }

    const header = JSON.parse(decodeBase64Url(headerEncoded));
    if (!header || header.alg !== 'HS256') {
        throw new Error('ERP token algorithm not supported');
    }

    const payload = JSON.parse(decodeBase64Url(payloadEncoded));
    const now = Math.floor(Date.now() / 1000);
    if (payload.exp && now > Number(payload.exp)) {
        throw new Error('ERP token expired');
    }

    return payload;
}

function createAttendifySessionToken(sessionPayload) {
    const secret = process.env.ATTENDIFY_SESSION_SECRET || process.env.ERP_SSO_SECRET || 'ATTENDIFY_DEV_SESSION_SECRET';
    const payloadEncoded = Buffer.from(JSON.stringify(sessionPayload)).toString('base64url');
    const signature = crypto.createHmac('sha256', secret).update(payloadEncoded).digest('base64url');
    return `ATD_TOKEN_${payloadEncoded}.${signature}`;
}

async function ensureStudentLinkedAccount({ email, roll, name }) {
    const normalizedEmail = String(email || '').trim().toLowerCase();
    const normalizedRoll = String(roll || '').trim();
    const normalizedName = String(name || '').trim() || normalizedEmail.split('@')[0];

    if (!normalizedEmail || !normalizedRoll) {
        throw new Error('ERP payload must include email and roll');
    }

    const user = await storage.getUserByEmail(normalizedEmail);
    if (!user) {
        await storage.createUser({
            email: normalizedEmail,
            name: normalizedName,
            roll: normalizedRoll,
            role: 'STUDENT',
            createdAt: new Date().toISOString(),
            source: 'ERP_SSO'
        });
    }

    const students = await storage.getStudents();
    const hasStudentByRoll = students.some(s => String(s.roll) === normalizedRoll);
    if (!hasStudentByRoll) {
        await storage.addStudent({
            roll: normalizedRoll,
            name: normalizedName,
            email: normalizedEmail,
            enrolledAt: new Date().toISOString(),
            source: 'ERP_SSO'
        });
    }

    return {
        email: normalizedEmail,
        roll: normalizedRoll,
        name: normalizedName
    };
}

app.post('/api/auth/erp-login', async (req, res) => {
    try {
        const erpToken = req.body?.erpToken;
        const erpClaims = verifyErpJwt(erpToken);

        const roleClaim = String(erpClaims.role || 'STUDENT').toUpperCase();
        if (roleClaim !== 'STUDENT') {
            return res.status(403).json({ success: false, message: 'Only student ERP login is allowed on this endpoint' });
        }

        const studentIdentity = await ensureStudentLinkedAccount({
            email: erpClaims.email,
            roll: erpClaims.roll,
            name: erpClaims.name
        });

        const now = Math.floor(Date.now() / 1000);
        const sessionTtlSeconds = Number(process.env.ATTENDIFY_SESSION_TTL_SECONDS || 3600);
        const sessionPayload = {
            uid: `erp_${Buffer.from(studentIdentity.email).toString('base64').substring(0, 12)}`,
            email: studentIdentity.email,
            role: 'STUDENT',
            src: 'ERP_SSO',
            iat: now,
            exp: now + sessionTtlSeconds
        };

        const attendifyToken = createAttendifySessionToken(sessionPayload);

        await storage.addAuditLog({
            event: 'ERP_STUDENT_LOGIN',
            details: `${studentIdentity.email} (${studentIdentity.roll})`
        });

        res.json({
            success: true,
            token: attendifyToken,
            user: {
                email: studentIdentity.email,
                role: 'STUDENT',
                roll: studentIdentity.roll,
                name: studentIdentity.name
            }
        });
    } catch (error) {
        console.error('erp-login error:', error.message);
        res.status(401).json({ success: false, message: error.message || 'ERP login failed' });
    }
});

function inferStudentRollFromEmail(email, students) {
    if (!email || !Array.isArray(students)) return null;
    const local = String(email).split('@')[0].toLowerCase();

    const exact = students.find(s => String(s.roll || '').toLowerCase() === local);
    if (exact) return String(exact.roll);

    const digits = local.match(/\d+/g);
    if (!digits) return null;

    for (const token of digits) {
        const found = students.find(s => String(s.roll || '') === token);
        if (found) return String(found.roll);
    }

    return null;
}

function toIsoOrDefault(value) {
    const date = new Date(value);
    return Number.isNaN(date.getTime()) ? null : date.toISOString();
}

function toMonthKey(value) {
    const date = new Date(value);
    if (Number.isNaN(date.getTime())) return null;
    const year = date.getFullYear();
    const month = String(date.getMonth() + 1).padStart(2, '0');
    return `${year}-${month}`;
}

function toSessionKey(rec) {
    return String(rec.sessionId || `${rec.subject || 'NA'}_${String(rec.timestamp || '').substring(0, 10)}`);
}

function csvEscape(value) {
    const raw = value == null ? '' : String(value);
    if (/[",\n\r]/.test(raw)) {
        return `"${raw.replace(/"/g, '""')}"`;
    }
    return raw;
}

function buildStudentLookup(students = []) {
    const lookup = new Map();
    for (const student of students) {
        const roll = String(student.roll || '').trim();
        const fingerprintId = String(student.fingerprintId || '').trim();
        if (roll) lookup.set(roll, student);
        if (fingerprintId) lookup.set(fingerprintId, student);
    }
    return lookup;
}

function resolveAttendanceStudentName(record, studentLookup) {
    const existingName = String(record.name || '').trim();
    if (existingName && !existingName.includes('Unknown')) {
        return existingName;
    }

    const roll = String(record.roll || '').trim();
    const fingerprintId = String(record.fingerprintId || '').trim();
    const student = studentLookup.get(roll) || studentLookup.get(fingerprintId);
    return (student && student.name) ? student.name : existingName;
}

function hydrateAttendanceNames(attendance = [], students = []) {
    const studentLookup = buildStudentLookup(students);
    return attendance.map(record => {
        const resolvedName = resolveAttendanceStudentName(record, studentLookup);
        return resolvedName ? { ...record, name: resolvedName } : record;
    });
}

async function resolveStudentForRequest(email) {
    const user = await storage.getUserByEmail(email);
    const students = await storage.getStudents();
    const attendance = await storage.getAttendance();

    const byEmail = students.find(s => s.email && String(s.email).toLowerCase() === String(email).toLowerCase());
    const byConfiguredRoll = (user && user.roll) ? students.find(s => String(s.roll) === String(user.roll)) : null;
    const inferredRoll = inferStudentRollFromEmail(email, students);
    const byInferredRoll = inferredRoll ? students.find(s => String(s.roll) === String(inferredRoll)) : null;

    const student = byEmail || byConfiguredRoll || byInferredRoll;
    if (!student) {
        return { user, student: null, attendance: [], myLogs: [] };
    }

    const studentRoll = String(student.roll);
    const myLogs = attendance
        .filter(rec => String(rec.roll) === studentRoll)
        .sort((a, b) => new Date(b.timestamp || 0) - new Date(a.timestamp || 0));

    return { user, student, attendance, myLogs };
}

// ============================================================
// STUDENT DASHBOARD ENDPOINTS
// ============================================================
app.get('/api/student/dashboard-data', verifyTokenWithRole(storage), requireRole(['STUDENT']), async (req, res) => {
    try {
        const { user, student, attendance, myLogs } = await resolveStudentForRequest(req.user.email);

        if (!student) {
            return res.status(404).json({
                success: false,
                message: 'Student profile not linked to this account yet',
                hint: 'Link this login email to a student roll (users.roll) or enroll with matching email.'
            });
        }

        const studentRoll = String(student.roll);

        // Fetch student leave applications to apply excused credit
        const apps = await storage.getApplications();
        const studentApps = apps.filter(a => String(a.studentRoll) === studentRoll);
        const approvedLeaves = studentApps.filter(a => a.status === 'APPROVED');

        const isExcused = (timestampStr) => {
            if (!timestampStr) return false;
            const time = new Date(timestampStr).getTime();
            return approvedLeaves.some(app => {
                const start = new Date(app.startDate + 'T00:00:00').getTime();
                const end = new Date(app.endDate + 'T23:59:59').getTime();
                return time >= start && time <= end;
            });
        };

        const uniqueSessionsAll = new Set(attendance.map(rec => toSessionKey(rec)));
        const uniqueSessionsMine = new Set(myLogs.map(rec => toSessionKey(rec)));

        // Add excused sessions to student's attended sessions
        for (const rec of attendance) {
            if (isExcused(rec.timestamp || rec.receivedAt)) {
                uniqueSessionsMine.add(toSessionKey(rec));
            }
        }

        const allSessionCount = uniqueSessionsAll.size || 1;
        const mySessionCount = uniqueSessionsMine.size;
        const overallPercent = Math.min(100, Math.round((mySessionCount / allSessionCount) * 100));
        const threshold = 75;

        const subjectSessionMap = new Map();
        for (const rec of attendance) {
            const subject = rec.subject || 'Unknown';
            const key = `${subject}::${toSessionKey(rec)}`;
            if (!subjectSessionMap.has(subject)) subjectSessionMap.set(subject, new Set());
            subjectSessionMap.get(subject).add(key);
        }

        const attendedBySubject = new Map();
        for (const rec of myLogs) {
            const subject = rec.subject || 'Unknown';
            const key = `${subject}::${toSessionKey(rec)}`;
            if (!attendedBySubject.has(subject)) attendedBySubject.set(subject, new Set());
            attendedBySubject.get(subject).add(key);
        }

        // Credit excused sessions subject-wise
        for (const rec of attendance) {
            if (isExcused(rec.timestamp || rec.receivedAt)) {
                const subject = rec.subject || 'Unknown';
                const key = `${subject}::${toSessionKey(rec)}`;
                if (!attendedBySubject.has(subject)) attendedBySubject.set(subject, new Set());
                attendedBySubject.get(subject).add(key);
            }
        }

        const subjects = Array.from(subjectSessionMap.keys())
            .sort((a, b) => a.localeCompare(b))
            .map(subject => {
                const total = subjectSessionMap.get(subject).size || 1;
                const attended = attendedBySubject.has(subject) ? attendedBySubject.get(subject).size : 0;
                const percent = Math.min(100, Math.round((attended / total) * 100));
                return { subject, attended, total, percent };
            });

        const monthTotalSessions = new Map();
        const monthMySessions = new Map();

        for (const rec of attendance) {
            const monthKey = toMonthKey(rec.timestamp);
            if (!monthKey) continue;
            if (!monthTotalSessions.has(monthKey)) monthTotalSessions.set(monthKey, new Set());
            monthTotalSessions.get(monthKey).add(toSessionKey(rec));
        }

        for (const rec of myLogs) {
            const monthKey = toMonthKey(rec.timestamp);
            if (!monthKey) continue;
            if (!monthMySessions.has(monthKey)) monthMySessions.set(monthKey, new Set());
            monthMySessions.get(monthKey).add(toSessionKey(rec));
        }

        // Credit excused sessions monthly trend
        for (const rec of attendance) {
            if (isExcused(rec.timestamp || rec.receivedAt)) {
                const monthKey = toMonthKey(rec.timestamp || rec.receivedAt);
                if (monthKey) {
                    if (!monthMySessions.has(monthKey)) monthMySessions.set(monthKey, new Set());
                    monthMySessions.get(monthKey).add(toSessionKey(rec));
                }
            }
        }

        const monthlyTrend = Array.from(monthTotalSessions.keys())
            .sort((a, b) => b.localeCompare(a))
            .map(month => {
                const totalSessions = monthTotalSessions.get(month)?.size || 0;
                const attendedSessions = monthMySessions.get(month)?.size || 0;
                const percent = totalSessions > 0 ? Math.min(100, Math.round((attendedSessions / totalSessions) * 100)) : 0;
                return {
                    month,
                    attended: attendedSessions,
                    total: totalSessions,
                    percent
                };
            });

        const recentLogs = myLogs.slice(0, 20).map(rec => ({
            roll: rec.roll,
            name: rec.name || student.name,
            subject: rec.subject || 'Unknown',
            teacher: rec.teacher || 'N/A',
            sessionId: rec.sessionId || null,
            timestamp: toIsoOrDefault(rec.timestamp) || rec.timestamp,
            deviceId: rec.deviceId || 'N/A'
        }));

        res.json({
            success: true,
            student: {
                name: student.name || (user?.name || req.user.email.split('@')[0]),
                roll: studentRoll,
                email: student.email || req.user.email,
                enrolledAt: student.enrolledAt || null
            },
            stats: {
                threshold,
                overallPercent,
                uniqueSessionsAttended: mySessionCount,
                uniqueSessionsObserved: uniqueSessionsAll.size,
                totalLogs: myLogs.length,
                isDefaulter: overallPercent < threshold
            },
            subjects,
            monthlyTrend,
            recentLogs,
            applications: studentApps.map(a => ({
                id: a.id,
                reason: a.reason,
                startDate: a.startDate,
                endDate: a.endDate,
                certificateUrl: a.certificateUrl || '',
                status: a.status,
                submittedAt: a.submittedAt,
                gfmComments: a.gfmComments || '',
                reviewedBy: a.reviewedBy || '',
                reviewedAt: a.reviewedAt || null
            }))
        });
    } catch (error) {
        console.error('student-dashboard-data error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.get('/api/student/attendance-report.csv', verifyTokenWithRole(storage), requireRole(['STUDENT']), async (req, res) => {
    try {
        const { student, myLogs } = await resolveStudentForRequest(req.user.email);

        if (!student) {
            return res.status(404).json({
                success: false,
                message: 'Student profile not linked to this account yet'
            });
        }

        const header = ['Timestamp', 'Roll', 'Student Name', 'Subject', 'Teacher', 'SessionId', 'DeviceId'];
        const rows = myLogs.map(rec => ([
            toIsoOrDefault(rec.timestamp) || rec.timestamp || '',
            rec.roll || '',
            rec.name || student.name || '',
            rec.subject || '',
            rec.teacher || '',
            rec.sessionId || '',
            rec.deviceId || ''
        ].map(csvEscape).join(',')));

        const csvContent = [header.join(','), ...rows].join('\n');
        const timestamp = new Date().toISOString().replace(/[:.]/g, '-');
        const safeRoll = String(student.roll || 'student').replace(/[^a-zA-Z0-9_-]/g, '_');

        res.setHeader('Content-Type', 'text/csv; charset=utf-8');
        res.setHeader('Content-Disposition', `attachment; filename="attendance_${safeRoll}_${timestamp}.csv"`);
        res.send(csvContent);
    } catch (error) {
        console.error('student-attendance-report error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// DEVICE CONFIG MANAGEMENT (Admin)
// ============================================================

app.get('/api/device/configs', verifyTokenWithRole(storage), requireRole(['ADMIN']), async (req, res) => {
    try {
        const data = await storage.loadData();
        const configs = data.deviceConfigs || [];
        res.json({ success: true, configs });
    } catch (error) {
        console.error('device-configs error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.post('/api/device/config', verifyTokenWithRole(storage), requireRole(['ADMIN']), async (req, res) => {
    try {
        const { deviceId, admins, teachers } = req.body || {};

        if (!deviceId || typeof deviceId !== 'string' || !deviceId.trim()) {
            return res.status(400).json({ success: false, message: 'deviceId is required' });
        }

        const normalizedDeviceId = deviceId.trim();
        const normalizedAdmins = Array.isArray(admins)
            ? admins.map(a => String(a || '').trim()).filter(Boolean)
            : [];

        if (normalizedAdmins.length === 0) {
            return res.status(400).json({ success: false, message: 'At least one admin email is required' });
        }

        const normalizedTeachers = Array.isArray(teachers)
            ? teachers
                .map(t => {
                    if (!t || typeof t !== 'object') {
                        return null;
                    }

                    const name = String(t.name || '').trim();
                    const email = String(t.email || '').trim().toLowerCase();
                    const subjectsRaw = String(t.subjects || '').trim();
                    const subjects = subjectsRaw
                        ? subjectsRaw.split(',').map(s => s.trim()).filter(Boolean)
                        : ['All'];

                    if (!name || !email) {
                        return null;
                    }

                    return {
                        name,
                        email,
                        subjects,
                        is_gfm: t.is_gfm === true
                    };
                })
                .filter(Boolean)
            : [];

        const data = await storage.loadData();
        if (!Array.isArray(data.deviceConfigs)) {
            data.deviceConfigs = [];
        }

        const nowIso = new Date().toISOString();
        const existingIndex = data.deviceConfigs.findIndex(c => c.deviceId === normalizedDeviceId);
        const existingVersion = existingIndex >= 0 ? Number(data.deviceConfigs[existingIndex].version || 0) : 0;

        const updatedConfig = {
            deviceId: normalizedDeviceId,
            admins: normalizedAdmins,
            teachers: normalizedTeachers,
            version: existingVersion + 1,
            updatedAt: nowIso
        };

        if (existingIndex >= 0) {
            data.deviceConfigs[existingIndex] = {
                ...data.deviceConfigs[existingIndex],
                ...updatedConfig
            };
        } else {
            data.deviceConfigs.push(updatedConfig);
        }

        await storage.saveData(data);
        await storage.addAuditLog({
            event: 'DEVICE_CONFIG_SAVED',
            deviceId: normalizedDeviceId,
            by: req.user.email,
            teacherCount: normalizedTeachers.length
        });

        res.json({ success: true, config: updatedConfig });
    } catch (error) {
        console.error('save-device-config error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.delete('/api/device/config/:deviceId', verifyTokenWithRole(storage), requireRole(['ADMIN']), async (req, res) => {
    try {
        const deviceId = String(req.params.deviceId || '').trim();
        if (!deviceId) {
            return res.status(400).json({ success: false, message: 'deviceId is required' });
        }

        const data = await storage.loadData();
        const configs = data.deviceConfigs || [];
        const nextConfigs = configs.filter(c => c.deviceId !== deviceId);

        if (nextConfigs.length === configs.length) {
            return res.status(404).json({ success: false, message: 'Device config not found' });
        }

        data.deviceConfigs = nextConfigs;
        await storage.saveData(data);
        await storage.addAuditLog({ event: 'DEVICE_CONFIG_DELETED', deviceId, by: req.user.email });

        res.json({ success: true, message: 'Device config deleted' });
    } catch (error) {
        console.error('delete-device-config error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// PUBLIC ENDPOINTS (No auth)
// ============================================================

app.get('/api/health', async (req, res) => {
    const mode = getModeInfo();
    const smtp = getSmtpHealth();
    const database = await probeDatabaseHealth();
    const firebaseStatus = mode.firebaseAdmin;

    res.json({
        success: true,
        server: 'ok',
        storageMode: mode.storageMode,
        authMode: mode.authMode,
        modeLabel: mode.modeLabel,
        supported: mode.supported,
        firebaseAdmin: firebaseStatus,
        smtp: smtp.status,
        smtpConfigured: smtp.configured,
        database: database.status,
        databaseRead: database.read,
        databaseWrite: database.write,
        ...(database.reason ? { databaseReason: database.reason } : {}),
        details: {
            mode,
            smtp,
            database
        }
    });
});

// Auto-detect server IP for QR code generation
app.get('/api/server-info', (req, res) => {
    const remoteIp = normalizeV4(req.socket?.remoteAddress);
    const advertised = getAdvertisedServer(remoteIp);

    res.json({
        success: true,
        ip: advertised.ip,
        port: PORT,
        url: advertised.url,
        mode: getModeInfo()
    });
});

// Duplicate route removed (see line 380)

// ============================================================
// ESP32 DEVICE CONFIG ENDPOINT (No auth - device uses deviceId)
// ============================================================

app.get('/api/device/config', async (req, res) => {
    const { deviceId } = req.query;

    if (!deviceId) {
        return res.status(400).json({ success: false, message: 'deviceId required' });
    }

    try {
        const data = await storage.loadData();
        const deviceConfigs = data.deviceConfigs || [];
        const users = data.users || [];

        // Find config for this device
        const config = deviceConfigs.find(c => c.deviceId === deviceId);

        if (!config) {
            console.log(`[DEVICE_CONFIG] No config found for device: ${deviceId}`);
            return res.status(404).json({ success: false, message: 'Device not configured' });
        }

        // Build teachers array with is_gfm flag from USERS table (source of truth)
        const teachers = (config.teachers || []).map(t => {
            const teacherEmail = typeof t === 'object' ? t.email : null;

            // Read is_gfm from USERS table (updated by admin dashboard GFM toggle)
            // Fall back to device config value if user not found
            const userRecord = teacherEmail ? users.find(u => u.email === teacherEmail) : null;
            const isGfm = userRecord ? (userRecord.is_gfm === true) : (typeof t === 'object' ? (t.is_gfm === true) : false);

            return {
                name: typeof t === 'object' ? t.name : t,
                subjects: typeof t === 'object' ? (Array.isArray(t.subjects) ? t.subjects : [t.subjects || 'All']) : ['All'],
                email: teacherEmail || '',
                is_gfm: isGfm
            };
        });

        // Build admins array
        const admins = (config.admins || []).map(a => typeof a === 'object' ? a.email : a);

        console.log(`[DEVICE_CONFIG] Sending config for ${deviceId}: ${teachers.length} teachers`);

        res.json({
            deviceId: config.deviceId,
            version: config.version || 1,
            admins: admins,
            teachers: teachers
        });

    } catch (error) {
        console.error('device-config error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// TEACHER PROTECTED ENDPOINTS
// ============================================================

app.get('/api/teacher/my-classes', verifyTokenWithRole(storage), requireRole(['TEACHER', 'ADMIN']), async (req, res) => {
    try {
        const data = await storage.loadData();
        const configs = data.deviceConfigs || [];
        const myEmail = req.user.email;

        // Find configs where this teacher is assigned
        // We assume teachers array in config contains objects { email, subjects } or strings (legacy)
        const myClasses = [];

        configs.forEach(config => {
            const teacherEntry = config.teachers.find(t => {
                if (typeof t === 'string') return t.toLowerCase() === myEmail.toLowerCase(); // Legacy/Simple match
                return t.email && t.email.toLowerCase() === myEmail.toLowerCase();
            });

            if (teacherEntry) {
                myClasses.push({
                    deviceId: config.deviceId,
                    subjects: typeof teacherEntry === 'object' ? teacherEntry.subjects : ['All'], // Default if no subjects defined
                    role: 'TEACHER'
                });
            }
        });

        res.json({ success: true, classes: myClasses });
    } catch (error) {
        console.error('my-classes error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// DASHBOARD DATA ENDPOINT (GFM / TEACHER)
// ============================================================
app.get('/api/dashboard-data', verifyTokenWithRole(storage), requireRole(['TEACHER', 'ADMIN', 'GFM']), async (req, res) => {
    try {
        const data = await storage.loadData();
        const queue = data.queue || [];
        const students = data.students || [];
        let attendance = data.attendance || [];

        attendance = hydrateAttendanceNames(attendance, students);

        // Log removed to reduce spam from live sync polling

        res.json({
            success: true,
            queue,
            students,
            attendance,
            stats: {
                queueCount: queue.length,
                studentCount: students.length,
                attendanceCount: attendance.length
            }
        });
    } catch (error) {
        console.error('dashboard-data error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.get('/api/teacher/sessions', verifyTokenWithRole(storage), requireRole(['TEACHER', 'ADMIN', 'GFM']), async (req, res) => {
    try {
        const data = await storage.loadData();
        const students = data.students || [];
        const attendance = hydrateAttendanceNames(data.attendance || [], students);
        const myEmail = req.user.email || '';
        const myRole = req.user.role;
        const isGfmUser = req.user.is_gfm === true;
        const scope = req.query.scope; // 'global' or 'mine'

        // GFM / ADMIN sees ALL only if explicitly requested
        if ((myRole === 'GFM' || myRole === 'ADMIN' || isGfmUser) && scope === 'global') {
            return res.json({ success: true, sessions: attendance });
        }

        // Regular TEACHER: Filter by Name linked to Email
        const configs = data.deviceConfigs || [];
        const users = data.users || [];
        const normalizeTeacherWords = value => String(value || '').toLowerCase().replace(/[._\s]+/g, ' ').trim();
        const normalizeTeacherCompact = value => String(value || '').toLowerCase().replace(/[^a-z0-9]/g, '');


        // 1. Find my Teacher Name(s) from configs
        const myNames = new Set();
        const myDeviceIds = new Set();
        const myEmailLower = myEmail.toLowerCase();
        const myProfile = users.find(user => String(user.email || '').toLowerCase() === myEmailLower);
        if (myProfile && myProfile.name) {
            myNames.add(myProfile.name);
        }

        configs.forEach(c => {
            if (c.teachers) {
                c.teachers.forEach(t => {
                    if (typeof t === 'object' && t.email && t.email.toLowerCase() === myEmailLower) {
                        if (t.name) myNames.add(t.name);
                        myDeviceIds.add(c.deviceId);
                    } else if (typeof t === 'string' && t.toLowerCase() === myEmailLower) {
                        // Legacy string match
                        myDeviceIds.add(c.deviceId);
                    }
                });
            }
        });

        // 2. Filter Attendance
        const myAttendance = attendance.filter(record => {
            // A. New Format: Has 'teacher' field
            if (record.teacher) {
                const recName = normalizeTeacherWords(record.teacher);
                const recNameCompact = normalizeTeacherCompact(record.teacher);

                // Check against myNames
                for (const myName of myNames) {
                    const myNameNorm = normalizeTeacherWords(myName);
                    const myNameCompact = normalizeTeacherCompact(myName);
                    if (recName.includes(myNameNorm) || myNameNorm.includes(recName)) return true;
                    if (myNameCompact && recNameCompact && (recNameCompact.includes(myNameCompact) || myNameCompact.includes(recNameCompact))) return true;
                }

                // Fallback: Check against Email parts (e.g. "kasangottuwar" in "Mrs. Kasangottuwar")
                const emailLocal = myEmailLower.split('@')[0] || '';
                const emailName = normalizeTeacherWords(emailLocal);
                const emailNameCompact = normalizeTeacherCompact(emailLocal);
                if (emailName && recName.includes(emailName)) return true;
                if (emailNameCompact && recNameCompact.includes(emailNameCompact)) return true;

                return false;
            }
            // B. Legacy Format: No 'teacher' field -> Fallback to Device ID
            return myDeviceIds.has(record.deviceId);
        });

        res.json({ success: true, sessions: myAttendance });
    } catch (error) {
        console.error('teacher-sessions error:', error);
        res.status(500).json({ success: false, message: 'Server error check console' });
    }
});

// duplicate /api/dashboard-data removed

// Legacy authorize-enrollment removed to favor RESTful GFM endpoints

app.delete('/api/clear-queue', verifyTokenWithRole(storage), requireRole(['TEACHER', 'ADMIN']), async (req, res) => {
    try {
        await storage.clearQueue();
        await storage.addAuditLog({ event: 'QUEUE_CLEARED', email: req.user.email });
        res.json({ success: true, message: 'Queue cleared' });
    } catch (error) {
        console.error('clear-queue error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// ADMIN ONLY ENDPOINTS
// ============================================================

app.post('/api/create-teacher', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { email, role, name } = req.body;
    if (!email) {
        return res.status(400).json({ success: false, message: 'Email required' });
    }

    // Validate role
    const newRole = (role === 'GFM') ? 'GFM' : 'TEACHER';

    // Generate temporary password server-side
    const temporaryPassword = generateTemporaryPassword(12);

    try {
        const createStoredTeacher = async ({ uid = null, includePassword = false, authProvider = 'FIREBASE', authWarning = null } = {}) => {
            const userData = {
                email,
                role: newRole,
                name: name || email.split('@')[0],
                authProvider,
                authMode: AUTH_MODE,
                createdAt: new Date().toISOString()
            };
            if (uid) userData.uid = uid;
            if (includePassword) userData.password = temporaryPassword;
            if (authWarning) userData.authWarning = authWarning;
            await storage.createUser(userData);
        };

        if (AUTH_MODE === 'LOCAL') {
            await createStoredTeacher({ includePassword: true, authProvider: 'LOCAL' });
            await storage.addAuditLog({ event: 'USER_CREATED_LOCAL', newUser: email, role: newRole, createdBy: req.user.email });
            return res.json({ 
                success: true, 
                email,
                temporaryPassword,
                role: newRole,
                mode: 'LOCAL',
                message: `${newRole} created in local mode`,
                modeInfo: getModeInfo()
            });
        }

        const admin = require('firebase-admin');
        if (admin.apps.length === 0) {
            return res.status(503).json({
                success: false,
                message: 'Firebase Admin is unavailable while AUTH_MODE=FIREBASE. Check Firebase credentials or switch AUTH_MODE=LOCAL for local development.',
                mode: getModeInfo()
            });
        }

        try {
            const userRecord = await admin.auth().createUser({ email, password: temporaryPassword, emailVerified: true });
            await createStoredTeacher({ uid: userRecord.uid, authProvider: 'FIREBASE' });
            await storage.addAuditLog({ event: 'USER_CREATED', newUser: email, role: newRole, createdBy: req.user.email });
            return res.json({ 
                success: true, 
                email,
                temporaryPassword,
                role: newRole,
                mode: 'FIREBASE',
                message: `${newRole} created in Firebase`,
                modeInfo: getModeInfo()
            });
        } catch (authError) {
            throw authError;
        }
    } catch (error) {
        console.error('create-teacher error:', error);
        const status = /already exists/i.test(error.message || '') ? 409 : 500;
        res.status(status).json({ success: false, message: error.message });
    }
});

app.get('/api/users', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    try {
        const users = await storage.getUsers();
        res.json({ success: true, users });
    } catch (error) {
        console.error('get-users error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.post('/api/admin/update-user', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { email, role, is_gfm } = req.body;
    if (!email) return res.status(400).json({ success: false, message: 'Email required' });

    try {
        const users = await storage.getUsers();
        const targetUser = users.find(u => u.email === email);
        if (!targetUser) return res.status(404).json({ success: false, message: 'User not found in DB' });

        const updates = {};
        if (role !== undefined) Object.assign(updates, { role });
        if (is_gfm !== undefined) Object.assign(updates, { is_gfm });

        await storage.updateUser(email, updates);

        // Also sync custom claims if Firebase Auth is available. Local fallback
        // accounts still work through /api/auth/login-local if this fails.
        if (AUTH_MODE === 'FIREBASE' && admin.apps.length > 0 && targetUser.uid && is_gfm !== undefined) {
            try {
                const adminAuth = require('firebase-admin').auth();
                await adminAuth.setCustomUserClaims(targetUser.uid, { role: is_gfm ? 'GFM' : targetUser.role });
            } catch (claimError) {
                console.warn('[AUTH] Could not sync custom claims:', claimError.message);
            }
        }

        await storage.addAuditLog({ event: 'USER_UPDATED', target: email, updates, by: req.user.email });
        res.json({ success: true, message: 'User updated' });
    } catch (e) {
        console.error('Update user error:', e);
        res.status(500).json({ success: false, message: e.message });
    }
});

app.delete('/api/admin/delete-user', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { email } = req.body;
    if (!email) return res.status(400).json({ success: false, message: 'Email required' });

    try {
        const users = await storage.getUsers();
        const targetUser = users.find(u => u.email === email);

        if (AUTH_MODE === 'FIREBASE' && admin.apps.length > 0 && targetUser && targetUser.uid) {
            try {
                const adminAuth = require('firebase-admin').auth();
                await adminAuth.deleteUser(targetUser.uid);
            } catch (err) {
                console.log(`[AUTH] Failed to delete Firebase Auth user (maybe already deleted):`, err.message);
            }
        }

        await storage.deleteUser(email);
        await storage.addAuditLog({ event: 'USER_DELETED', target: email, by: req.user.email });
        res.json({ success: true, message: 'User permanently deleted' });
    } catch (e) {
        console.error('Delete user error:', e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// Admin: Reset local password for an existing user (returns temporary password)
app.post('/api/admin/reset-local-password', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { email } = req.body;
    if (!email) return res.status(400).json({ success: false, message: 'Email required' });
    try {
        const users = await storage.getUsers();
        const targetUser = users.find(u => u.email === email);
        if (!targetUser) return res.status(404).json({ success: false, message: 'User not found' });

        const temp = generateTemporaryPassword(12);
        // Update local storage password (works in LOCAL mode and acts as fallback)
        await storage.updateUser(email, { password: temp });
        await storage.addAuditLog({ event: 'USER_RESET_LOCAL_PASSWORD', target: email, by: req.user.email });

        return res.json({ success: true, temporaryPassword: temp });
    } catch (e) {
        console.error('Reset password error:', e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// Admin: Create or recreate Firebase Auth user for an existing DB user
app.post('/api/admin/recreate-firebase', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { email } = req.body;
    if (!email) return res.status(400).json({ success: false, message: 'Email required' });
    if (!admin || admin.apps.length === 0) {
        return res.status(503).json({ success: false, message: 'Firebase Admin not initialized' });
    }

    try {
        const users = await storage.getUsers();
        const targetUser = users.find(u => u.email === email);
        if (!targetUser) return res.status(404).json({ success: false, message: 'User not found' });

        // Generate a temporary password and create/authenticate in Firebase
        const temp = generateTemporaryPassword(12);
        try {
            const adminAuth = require('firebase-admin').auth();
            const userRecord = await adminAuth.createUser({ email, password: temp, emailVerified: true });
            // Update stored user with uid and authProvider
            await storage.updateUser(email, { uid: userRecord.uid, authProvider: 'FIREBASE' });
            await storage.addAuditLog({ event: 'USER_RECREATED_FIREBASE', target: email, by: req.user.email });
            return res.json({ success: true, message: 'Firebase user created', temporaryPassword: temp });
        } catch (authErr) {
            console.error('Firebase recreate error:', authErr);
            return res.status(500).json({ success: false, message: authErr.message || 'Firebase error' });
        }
    } catch (e) {
        console.error('Recreate firebase error:', e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// STUDENT REGISTRATION ENDPOINTS (Remote)
// ============================================================

// 1. Student submits registration form
app.post('/api/register-queue', async (req, res) => {
    const { name, roll, email } = req.body;
    if (!name || !roll) return res.status(400).json({ success: false, message: 'Name and Roll required' });

    try {
        const queue = await storage.getQueue();
        const students = await storage.getStudents();

        // Allow 're-registration' for failed attempts, but check enrolled
        if (students.find(s => s.roll === roll)) {
            return res.status(400).json({ success: false, message: 'Student already enrolled' });
        }

        const existing = queue.find(q => q.roll === roll);
        if (existing) {
            return res.json({ success: true, message: 'Updated registration' });
        }

        await storage.addToQueue({ name, roll, email, status: 'PENDING', requestedAt: new Date().toISOString() });
        res.json({ success: true, message: 'Registration submitted' });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 2. GFM Authorizes Enrollment (Triggers Hardware)
app.post('/api/gfm/enrollment-queue/:roll/authorize', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    const { roll } = req.params;
    try {
        const queue = await storage.getQueue();
        const item = queue.find(q => q.roll === roll);

        if (!item) return res.status(404).json({ success: false, message: 'Not found in queue' });

        // Ensure we don't have another active enrollment (ESP32 can only handle 1 at a time)
        const active = queue.find(q =>
            (q.status === QUEUE_STATUS.AUTHORIZED || q.status === QUEUE_STATUS.IN_PROGRESS) && q.roll !== roll
        );
        if (active) {
            return res.status(400).json({ success: false, message: `${active.name} is currently enrolling` });
        }

        // Set/re-set status to AUTHORIZED. This intentionally supports retrying
        // FAILED attempts without requiring the student to submit the form again.
        await storage.updateQueueItem(roll, {
            status: QUEUE_STATUS.AUTHORIZED,
            authorizedAt: new Date().toISOString(),
            startedAt: null,
            failedAt: null,
            failReason: null,
            retryCount: Number(item.retryCount || 0) + (item.status === QUEUE_STATUS.FAILED ? 1 : 0)
        });
        await storage.addAuditLog({ event: 'ENROLL_AUTHORIZED', roll, by: req.user.email });

        res.json({ success: true, message: 'Authorized. Hardware will scan finger now.' });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 2b. GFM fetches enrollment queue
app.get('/api/gfm/enrollment-queue', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    try {
        const queue = await storage.getQueue();
        // Sort by requestedAt descending
        queue.sort((a, b) => new Date(b.requestedAt || 0) - new Date(a.requestedAt || 0));
        res.json({ success: true, queue });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 3. GFM/Admin Deletes Enrolled Student (Clean start)

// Update student subjects (electives/practicals)
app.put('/api/students/:roll/subjects', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    try {
        const { roll } = req.params;
        const { enrolledSubjects } = req.body;
        
        if (!Array.isArray(enrolledSubjects)) {
            return res.status(400).json({ success: false, message: 'enrolledSubjects must be an array' });
        }
        
        if (!storage.updateStudentSubjects) {
             return res.status(501).json({ success: false, message: 'Feature not implemented in storage' });
        }

        const updatedStudent = await storage.updateStudentSubjects(roll, enrolledSubjects);
        if (!updatedStudent) {
            return res.status(404).json({ success: false, message: 'Student not found' });
        }
        
        await storage.addAuditLog({
            event: 'STUDENT_SUBJECTS_UPDATED',
            user: req.user.email,
            details: `Updated subjects for roll ${roll}`
        });

        res.json({ success: true, message: 'Subjects updated successfully', student: updatedStudent });
    } catch (error) {
        console.error('Update student subjects error:', error);
        res.status(500).json({ success: false, message: 'Failed to update subjects' });
    }
});

app.delete('/api/students/:roll', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    const { roll } = req.params;
    try {
        await storage.removeStudent(roll);
        // Also remove from queue if present (cleanup)
        await storage.removeFromQueue(roll);
        await storage.addAuditLog({ event: 'STUDENT_DELETED', roll, by: req.user.email });
        res.json({ success: true, message: 'Student deleted from dashboard' });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// GFM ENDPOINTS
// ============================================================

app.get('/api/devices/overview', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    try {
        const data = await storage.loadData();
        const configs = data.deviceConfigs || [];
        const statusByDevice = data.deviceStatus || {};
        const configByDevice = new Map(configs.map(config => [config.deviceId, config]));
        const deviceIds = new Set([
            ...AUTHORIZED_DEVICES.map(device => device.deviceId),
            ...configs.map(config => config.deviceId).filter(Boolean),
            ...Object.keys(statusByDevice)
        ]);
        const onlineWindowMs = Math.max(Number(process.env.DEVICE_ONLINE_WINDOW_MS || 120000), 30000);
        const now = Date.now();

        const devices = Array.from(deviceIds).map(deviceId => {
            const c = configByDevice.get(deviceId) || {};
            const heartbeat = statusByDevice[deviceId] || {};
            const lastSeenMs = Date.parse(heartbeat.lastSeen || '');
            const isOnline = Number.isFinite(lastSeenMs) && (now - lastSeenMs) <= onlineWindowMs;

            return {
                deviceId,
                status: isOnline ? 'ONLINE' : 'OFFLINE',
                version: c.version || null,
                adminCount: Array.isArray(c.admins) ? c.admins.length : 0,
                teacherCount: Array.isArray(c.teachers) ? c.teachers.length : 0,
                lastUpdated: c.updatedAt || null,
                lastSeen: heartbeat.lastSeen || null,
                lastIp: heartbeat.lastIp || null
            };
        });

        res.json({ success: true, devices });
    } catch (error) {
        console.error('devices-overview error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// GET Timetable
app.get('/api/gfm/timetable', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    try {
        const data = await storage.loadData();
        // Return empty timetable structure if not set
        const defaultTimetable = {
            "Monday": [], "Tuesday": [], "Wednesday": [],
            "Thursday": [], "Friday": [], "Saturday": []
        };
        // Timetable is stored in deviceConfigs for now, or we can make a new 'settings' collection
        // Let's store it in the root of data object for simplicity in JSON, or 'settings' collection in Firestore

        // For LocalStorage, we'll look for data.timetable
        const timetable = data.timetable || defaultTimetable;
        res.json({ success: true, timetable });
    } catch (error) {
        console.error('get-timetable error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// SAVE Timetable
app.post('/api/gfm/timetable', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    const { timetable } = req.body;
    if (!timetable) return res.status(400).json({ success: false, message: 'Timetable data required' });

    try {
        const data = await storage.loadData();
        data.timetable = timetable;

        await storage.saveData(data);
        await storage.addAuditLog({ event: 'TIMETABLE_UPDATE', email: req.user.email });

        res.json({ success: true, message: 'Timetable saved' });
    } catch (error) {
        console.error('save-timetable error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// ESP32 DEVICE ENDPOINTS
// ============================================================

app.get('/api/poll-status', validateDevice, async (req, res) => {
    try {
        const queue = await storage.getQueue();

        const parseTimeoutWithMin = (rawValue, defaultValue, minValue) => {
            const parsed = Number(rawValue);
            if (!Number.isFinite(parsed) || parsed <= 0) {
                return defaultValue;
            }
            return Math.max(parsed, minValue);
        };

        // Auto-expire stale entries (safety net for device crashes/WiFi loss)
        const IN_PROGRESS_TIMEOUT_MS = parseTimeoutWithMin(
            process.env.ENROLL_IN_PROGRESS_TIMEOUT_MS,
            10 * 60 * 1000,
            30 * 1000
        );
        const AUTHORIZED_TIMEOUT_MS = parseTimeoutWithMin(
            process.env.ENROLL_AUTHORIZED_TIMEOUT_MS,
            48 * 60 * 60 * 1000,
            60 * 1000
        );
        for (const item of queue) {
            let timestamp = null;
            let timeoutMs = 0;
            if (item.status === QUEUE_STATUS.IN_PROGRESS && item.startedAt) {
                timestamp = item.startedAt;
                timeoutMs = IN_PROGRESS_TIMEOUT_MS;
            } else if (item.status === QUEUE_STATUS.AUTHORIZED && item.authorizedAt) {
                timestamp = item.authorizedAt;
                timeoutMs = AUTHORIZED_TIMEOUT_MS;
            }
            if (timestamp) {
                const elapsed = Date.now() - new Date(timestamp).getTime();
                if (elapsed > timeoutMs) {
                    console.log(`[POLL] Auto-expiring stale ${item.status}: roll=${item.roll} (${Math.round(elapsed / 1000)}s old)`);
                    await storage.updateQueueItem(item.roll, {
                        status: QUEUE_STATUS.FAILED,
                        failedAt: new Date().toISOString(),
                        failReason: 'Auto-expired before device confirmation'
                    });
                    await storage.addAuditLog({ event: 'ENROLL_AUTO_EXPIRED', roll: item.roll, deviceId: req.deviceId });
                    item.status = QUEUE_STATUS.FAILED; // Update in-memory too
                }
            }
        }

        const active = queue.find(q => q.status === QUEUE_STATUS.AUTHORIZED || q.status === QUEUE_STATUS.IN_PROGRESS);
        if (active) {
            if (active.status !== QUEUE_STATUS.IN_PROGRESS) {
                await storage.updateQueueItem(active.roll, { status: QUEUE_STATUS.IN_PROGRESS, startedAt: new Date().toISOString() });
                await storage.addAuditLog({ event: 'ENROLL_STARTED', roll: active.roll, deviceId: req.deviceId });
            }
            res.json({ action: 'ENROLL', status: QUEUE_STATUS.IN_PROGRESS, name: active.name, roll: active.roll });
        } else {
            res.json({ action: 'IDLE', status: null });
        }
    } catch (error) {
        console.error('poll-status error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.post('/api/confirm-enrollment', validateDevice, async (req, res) => {
    const { roll, fingerprintId, result } = req.body;
    if (!roll) {
        return res.status(400).json({ success: false, message: 'Roll required' });
    }
    try {
        const queue = await storage.getQueue();
        const student = queue.find(q => q.roll === roll);
        if (!student) {
            await storage.addAuditLog({ event: 'ENROLL_ERROR', roll, details: 'Not in queue' });
            return res.status(400).json({ success: false, message: 'Roll not in queue' });
        }
        if (result === 'SUCCESS') {
            const enrolled = {
                name: student.name,
                roll: student.roll,
                fingerprintId: fingerprintId || parseInt(roll),
                enrolledAt: new Date().toISOString()
            };
            await storage.addStudent(enrolled);
            await storage.removeFromQueue(roll);
            await storage.addAuditLog({ event: 'ENROLL_SUCCESS', roll, fingerprintId: enrolled.fingerprintId });
            res.json({ success: true, message: 'Enrollment confirmed' });
        } else if (result === 'TIMEOUT') {
            await storage.updateQueueItem(roll, {
                status: QUEUE_STATUS.FAILED,
                failedAt: new Date().toISOString(),
                failReason: 'Enrollment timed out'
            });
            await storage.addAuditLog({ event: 'ENROLL_TIMEOUT', roll });
            res.json({ success: true, message: 'Timeout recorded' });
        } else if (result === 'CANCELLED') {
            await storage.removeFromQueue(roll);
            await storage.addAuditLog({ event: 'ENROLL_CANCELLED', roll });
            res.json({ success: true, message: 'Cancelled' });
        } else {
            await storage.updateQueueItem(roll, {
                status: QUEUE_STATUS.FAILED,
                failedAt: new Date().toISOString(),
                failReason: 'Fingerprint enrollment failed',
                lastFingerprintId: fingerprintId || null
            });
            await storage.addAuditLog({ event: 'ENROLL_FAILED', roll });
            res.json({ success: false, message: 'Enrollment failed' });
        }
    } catch (error) {
        console.error('confirm-enrollment error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

app.post('/api/upload-attendance', validateDevice, async (req, res) => {
    // 1. Check for Phase 7 Session Upload (Batch)
    const { sessionId, teacher, subject, students: studentIds } = req.body;

    if (studentIds && Array.isArray(studentIds)) {
        console.log(`[UPLOAD] Received session: ${subject} by ${teacher} (${studentIds.length} students)`);

        try {
            const allStudents = await storage.getStudents();
            const studentMap = {};
            allStudents.forEach(s => {
                if (s.roll) studentMap[String(s.roll)] = s.name;
                if (s.fingerprintId) studentMap[String(s.fingerprintId)] = s.name;
            });

            const existingAttendance = await storage.getAttendance();
            const timestamp = new Date().toISOString();
            let count = 0;
            let skipped = 0;

            for (const fid of studentIds) {
                // DUPLICATE CHECK: Skip if this session+fingerprintId already exists
                const isDuplicate = existingAttendance.some(
                    rec => rec.sessionId === sessionId && rec.fingerprintId == fid
                );

                if (isDuplicate) {
                    skipped++;
                    continue; // Skip this duplicate
                }

                // Find by FP ID or Roll
                const student = allStudents.find(s => s.fingerprintId == fid || s.roll == fid);

                if (student) {
                    const record = {
                        roll: student.roll,
                        name: student.name,
                        fingerprintId: fid,
                        timestamp, // Batch timestamp
                        receivedAt: new Date().toISOString(),
                        deviceId: req.deviceId,
                        sessionId,
                        subject,
                        teacher
                    };
                    await storage.addAttendance(record);
                    count++;
                } else {
                    // Fallback: Student not in DB (e.g. data cleared), but trusted device sent it.
                    // Recover what we can.
                    console.warn(`[UPLOAD] Student ID ${fid} not found in DB. saving as Unknown.`);
                    const record = {
                        roll: fid, // Best guess
                        name: studentMap[fid] || `Unknown (ID: ${fid})`,
                        fingerprintId: fid,
                        timestamp,
                        receivedAt: new Date().toISOString(),
                        deviceId: req.deviceId,
                        sessionId,
                        subject,
                        teacher
                    };
                    await storage.addAttendance(record);
                    count++;
                }
            }

            if (skipped > 0) {
                console.log(`[UPLOAD] Skipped ${skipped} duplicates`);
            }
            await storage.addAuditLog({ event: 'SESSION_UPLOAD', deviceId: req.deviceId, details: `${count} new, ${skipped} duplicates skipped` });

            // Send Session Summary Email (always send if students were recorded in the session, even if they are duplicates)
            if (studentIds && studentIds.length > 0 && teacher) {
                try {
                    const users = await storage.getUsers();
                    // Try to find the teacher's email by matching the name they used on the device
                    const teacherUser = users.find(u => u.name === teacher || u.email.split('@')[0] === teacher.toLowerCase() || u.email === teacher);
                    if (teacherUser && teacherUser.email) {
                        const baseUrl = resolveBaseUrl(req);
                        mailer.sendSessionSummaryEmail(teacher, teacherUser.email, subject, studentIds, baseUrl);
                    }
                } catch (e) {
                    console.error('[MAILER] Failed to lookup teacher email for summary:', e.message);
                }
            }

            return res.json({ success: true, message: `Processed ${count} records, ${skipped} duplicates skipped` });

        } catch (error) {
            console.error('session-upload error:', error);
            return res.status(500).json({ success: false, message: 'Server error' });
        }
    }

    // 2. Fallback: Phase 1 Legacy (Single Record)
    const { roll, fingerprintId, timestamp } = req.body;
    if (!roll || !timestamp) {
        return res.status(400).json({ success: false, message: 'Invalid payload' });
    }
    try {
        const students = await storage.getStudents();
        const student = students.find(s => s.roll === roll);
        if (!student) {
            return res.status(400).json({ success: false, message: 'Roll not enrolled' });
        }

        const record = {
            roll, name: student.name, fingerprintId, timestamp,
            receivedAt: new Date().toISOString(), deviceId: req.deviceId
        };
        await storage.addAttendance(record);
        res.json({ success: true, message: 'Attendance recorded' });
    } catch (error) {
        console.error('upload-attendance error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// WEB SCANNER ENDPOINT (Teacher Auth)
// ============================================================
app.post('/api/teacher/mark-attendance', verifyTokenWithRole(storage), requireRole(['TEACHER', 'ADMIN', 'GFM']), async (req, res) => {
    const { sessionId, teacher, subject, students: studentIds } = req.body;

    // Logic similar to upload-attendance but trusted via Token
    if (studentIds && Array.isArray(studentIds)) {
        try {
            const allStudents = await storage.getStudents();
            const timestamp = new Date().toISOString();
            let count = 0;
            const deviceId = 'WEB-SCANNER';

            for (const fid of studentIds) {
                const student = allStudents.find(s => s.fingerprintId == fid || s.roll == fid);
                if (student) {
                    const record = {
                        roll: student.roll,
                        name: student.name,
                        fingerprintId: student.fingerprintId || 'QR', // Ensure ID exists
                        timestamp,
                        receivedAt: new Date().toISOString(),
                        deviceId,
                        sessionId,
                        subject,
                        teacher // Trusted from Body? or req.user?
                        // Ideally strictly use req.user.email but Body handles GFM logic etc
                    };
                    await storage.addAttendance(record);
                    count++;
                }
            }
            await storage.addAuditLog({ event: 'WEB_SCAN', user: req.user.email, details: `${count} records` });

            // Send session summary email
            try {
                const baseUrl = resolveBaseUrl(req);
                mailer.sendSessionSummaryEmail(req.user.name || req.user.email, req.user.email, subject, studentIds, baseUrl);
            } catch (e) {
                console.error('[MAILER] Failed to send session summary:', e.message);
            }

            return res.json({ success: true, message: `Marked ${count} students` });

        } catch (error) {
            console.error('web-mark error:', error);
            return res.status(500).json({ success: false, message: 'Server error' });
        }
    }
    res.status(400).json({ success: false, message: 'Invalid format' });
});


// ============================================================
// GFM ADVANCED ANALYTICS
// ============================================================


// ============================================================
// ADMIN: REMOTE SD CARD VIEW
// ============================================================

// ============================================================
// GFM: CREATE STUDENT ACCOUNT FROM QUEUE
// ============================================================
app.post('/api/gfm/enrollment-queue/:id/create-account', verifyTokenWithRole(storage), requireRole(['GFM', 'ADMIN']), async (req, res) => {
    const queueId = req.params.id;
    try {
        // Find queue item
        const queue = await storage.getQueue();
        const item = queue.find(q => String(q.id || q.roll) === String(queueId));
        if (!item) return res.status(404).json({ success: false, message: 'Queue item not found' });
        if (!item.email || !item.roll || !item.name) {
            return res.status(400).json({ success: false, message: 'Missing required student data in queue item' });
        }

        let resetLink = null;
        let uid = null;
        let localPassword = null;

        if (AUTH_MODE === 'FIREBASE') {
            if (admin.apps.length === 0) {
                return res.status(503).json({
                    success: false,
                    message: 'Firebase Admin is unavailable while AUTH_MODE=FIREBASE. Check Firebase credentials or switch AUTH_MODE=LOCAL for local development.',
                    mode: getModeInfo()
                });
            }

            let userRecord;
            try {
                userRecord = await admin.auth().getUserByEmail(item.email);
            } catch (e) {
                if (e.code === 'auth/user-not-found') {
                    userRecord = await admin.auth().createUser({
                        email: item.email,
                        displayName: item.name,
                        emailVerified: false,
                        password: Math.random().toString(36).slice(-10) + Date.now(),
                        disabled: false
                    });
                } else {
                    throw e;
                }
            }
            uid = userRecord.uid;

            try {
                resetLink = await admin.auth().generatePasswordResetLink(item.email);
                mailer.sendWelcomeEmail(item.name, item.email, resetLink);
            } catch (linkErr) {
                console.warn('[AUTH] Could not generate reset link:', linkErr.message);
            }
        } else {
            localPassword = String(item.roll);
        }

        const userData = {
            email: item.email,
            name: item.name,
            roll: item.roll,
            role: 'STUDENT',
            authProvider: AUTH_MODE === 'LOCAL' ? 'LOCAL' : 'FIREBASE',
            authMode: AUTH_MODE,
            createdAt: new Date().toISOString()
        };
        if (uid) userData.uid = uid;
        if (localPassword) userData.password = localPassword;

        try {
            await storage.createUser(userData);
        } catch (e) {
            if (!/already exists/i.test(e.message)) throw e;
        }

        await storage.addAuditLog({ event: 'GFM_CREATE_STUDENT_FROM_QUEUE', by: req.user.email, student: item.email });
        res.json({
            success: true,
            message: AUTH_MODE === 'LOCAL' ? 'Student account created locally.' : 'Student account created successfully.',
            resetLink,
            localPassword,
            studentEmail: item.email,
            studentName: item.name,
            mode: getModeInfo()
        });
    } catch (error) {
        console.error('gfm-create-student-from-queue error:', error);
        res.status(500).json({ success: false, message: error.message || 'Server error' });
    }
});

// ============================================================
// ADMIN: CREATE STUDENT ACCOUNT (ID/PASSWORD)
// ============================================================
app.post('/api/admin/create-student', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { email, roll, name } = req.body;
    if (!email || !roll || !name) {
        return res.status(400).json({ success: false, message: 'Missing required fields (email, roll, name)' });
    }
    try {
        let resetLink = null;
        let uid = null;
        let localPassword = null;

        if (AUTH_MODE === 'FIREBASE') {
            if (admin.apps.length === 0) {
                return res.status(503).json({
                    success: false,
                    message: 'Firebase Admin is unavailable while AUTH_MODE=FIREBASE. Check Firebase credentials or switch AUTH_MODE=LOCAL for local development.',
                    mode: getModeInfo()
                });
            }

            let userRecord;
            try {
                userRecord = await admin.auth().getUserByEmail(email);
            } catch (e) {
                if (e.code === 'auth/user-not-found') {
                    userRecord = await admin.auth().createUser({
                        email,
                        displayName: name,
                        emailVerified: false,
                        password: Math.random().toString(36).slice(-10) + Date.now(), // random temp password
                        disabled: false
                    });
                } else {
                    throw e;
                }
            }
            uid = userRecord.uid;

            try {
                resetLink = await admin.auth().generatePasswordResetLink(email);
                mailer.sendWelcomeEmail(name, email, resetLink);
            } catch (linkErr) {
                console.warn('[AUTH] Could not generate reset link:', linkErr.message);
            }
        } else {
            localPassword = String(roll);
        }

        const userData = {
            email,
            name,
            roll,
            role: 'STUDENT',
            authProvider: AUTH_MODE === 'LOCAL' ? 'LOCAL' : 'FIREBASE',
            authMode: AUTH_MODE,
            createdAt: new Date().toISOString()
        };
        if (uid) userData.uid = uid;
        if (localPassword) userData.password = localPassword;

        try {
            await storage.createUser(userData);
            await storage.addToQueue({ 
                name, 
                roll, 
                email, 
                status: 'PENDING', 
                requestedAt: new Date().toISOString() 
            });
        } catch (e) {
            if (!/already exists/i.test(e.message)) throw e;
        }

        await storage.addAuditLog({ event: 'ADMIN_CREATE_STUDENT', by: req.user.email, student: email });
        res.json({
            success: true,
            message: AUTH_MODE === 'LOCAL' ? 'Student account created locally.' : 'Student account created successfully.',
            resetLink,
            localPassword,
            studentEmail: email,
            studentName: name,
            mode: getModeInfo()
        });
    } catch (error) {
        console.error('create-student error:', error);
        res.status(500).json({ success: false, message: error.message || 'Server error' });
    }
});

// 1. Request File List (Queues Command)
app.post('/api/devices/:deviceId/cmd/list-files', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { deviceId } = req.params;
    try {
        await storage.addAuditLog({ event: 'CMD_LIST_FILES', deviceId, by: req.user.email });

        // Add to queue logic via data
        const data = await storage.loadData();
        if (!data.deviceCommands) data.deviceCommands = {};

        data.deviceCommands[deviceId] = {
            action: 'LIST_FILES',
            timestamp: new Date().toISOString()
        };
        await storage.saveData(data);

        res.json({ success: true, message: 'Command queued. Device will respond on next poll.' });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 2. Get Cached File List
app.get('/api/devices/:deviceId/files', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { deviceId } = req.params;
    try {
        const data = await storage.loadData();
        const files = (data.deviceFiles && data.deviceFiles[deviceId]) ? data.deviceFiles[deviceId] : [];
        const lastUpdated = (data.deviceFiles && data.deviceFiles[deviceId + '_updated']) || null;
        res.json({ success: true, files, lastUpdated });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 1. Request File List (Queues Command)
app.post('/api/devices/:deviceId/cmd/list-files', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { deviceId } = req.params;
    try {
        await storage.addAuditLog({ event: 'CMD_LIST_FILES', deviceId, by: req.user.email });

        // Add to queue logic via data
        const data = await storage.loadData();
        if (!data.deviceCommands) data.deviceCommands = {};

        data.deviceCommands[deviceId] = {
            action: 'LIST_FILES',
            timestamp: new Date().toISOString()
        };
        await storage.saveData(data);

        res.json({ success: true, message: 'Command queued. Device will respond on next poll.' });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 2. Get Cached File List
app.get('/api/devices/:deviceId/files', verifyTokenWithRole(storage), requireRole('ADMIN'), async (req, res) => {
    const { deviceId } = req.params;
    try {
        const data = await storage.loadData();
        const files = (data.deviceFiles && data.deviceFiles[deviceId]) ? data.deviceFiles[deviceId] : [];
        const lastUpdated = (data.deviceFiles && data.deviceFiles[deviceId + '_updated']) || null;
        res.json({ success: true, files, lastUpdated });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// 3. Upload File List (Called by Hardware)
app.post('/api/devices/:deviceId/upload-file-list', validateDevice, async (req, res) => {
    const { deviceId } = req.params;
    const { files } = req.body; // Expecting array of {name, size, dir}

    try {
        const data = await storage.loadData();
        if (!data.deviceFiles) data.deviceFiles = {};

        data.deviceFiles[deviceId] = files;
        data.deviceFiles[deviceId + '_updated'] = new Date().toISOString();

        // Clear command
        if (data.deviceCommands && data.deviceCommands[deviceId] && data.deviceCommands[deviceId].action === 'LIST_FILES') {
            delete data.deviceCommands[deviceId];
        }

        await storage.saveData(data);
        await storage.addAuditLog({ event: 'FILE_LIST_UPLOAD', deviceId, count: files.length });

        res.json({ success: true });
    } catch (e) {
        console.error(e);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// STUDENT DIRECTORY (for QR scanner name resolution)
// ============================================================
app.get('/api/students', verifyTokenWithRole(storage), requireRole(['TEACHER', 'GFM', 'ADMIN']), async (req, res) => {
    try {
        const students = await storage.getStudents();
        res.json({ success: true, students });
    } catch (error) {
        console.error('get-students error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// SERVE FRONTEND
// ============================================================
app.get('/', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'index.html')));
app.get('/login', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'login.html')));
app.get('/dashboard', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'dashboard.html')));
app.get('/device-config', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'device-config.html')));

// Role-based dashboards
app.get('/admin/dashboard', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'admin/dashboard.html')));
app.get('/teacher/dashboard', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'teacher/dashboard.html')));
app.get('/teacher/qr-scanner', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'teacher/qr-scanner.html')));
app.get('/student/dashboard', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'student/dashboard.html')));
// app.get('/gfm/dashboard', (req, res) => res.sendFile(path.join(__dirname, '../frontend', 'gfm/dashboard.html')));

// Daily attendance reporter scheduler
function startDailyReportScheduler() {
    console.log('[SCHEDULER] Daily report scheduler started.');
    let lastReportDate = '';

    setInterval(async () => {
        try {
            const now = new Date();
            // Run at 8:00 PM (20:00) every day
            const currentHour = now.getHours();
            const currentDateStr = now.toDateString(); // e.g. "Sat Jun 13 2026"

            if (currentHour === 20 && lastReportDate !== currentDateStr) {
                console.log(`[SCHEDULER] Running daily attendance report trigger for ${currentDateStr}...`);
                
                const allAttendance = await storage.getAttendance();
                const users = await storage.getUsers();
                const teachers = users.filter(u => u.role === 'TEACHER' || u.role === 'GFM');

                // Filter attendance records from today
                const startOfToday = new Date();
                startOfToday.setHours(0, 0, 0, 0);
                const endOfToday = new Date();
                endOfToday.setHours(23, 59, 59, 999);

                const todayAttendance = allAttendance.filter(rec => {
                    const recDate = new Date(rec.timestamp || rec.receivedAt);
                    return recDate >= startOfToday && recDate <= endOfToday;
                });

                if (todayAttendance.length === 0) {
                    console.log('[SCHEDULER] No attendance sessions logged today. Skipping email summaries.');
                    lastReportDate = currentDateStr; // Avoid repeating today
                    return;
                }

                // Group today's attendance by teacher email/name
                for (const teacherUser of teachers) {
                    const teacherEmail = teacherUser.email;
                    const teacherName = teacherUser.name || teacherEmail.split('@')[0];

                    // Find today's attendance for this teacher
                    const teacherRecs = todayAttendance.filter(rec => {
                        const t = rec.teacher || '';
                        return t.toLowerCase() === teacherName.toLowerCase() || 
                               t.toLowerCase() === teacherEmail.toLowerCase() ||
                               (teacherEmail.split('@')[0] && t.toLowerCase() === teacherEmail.split('@')[0].toLowerCase());
                    });

                    if (teacherRecs.length > 0) {
                        // Group by sessionId to get distinct lectures
                        const sessionsMap = {};
                        teacherRecs.forEach(rec => {
                            const sid = rec.sessionId || 'default';
                            if (!sessionsMap[sid]) {
                                sessionsMap[sid] = {
                                    subject: rec.subject || 'N/A',
                                    time: rec.timestamp ? new Date(rec.timestamp).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }) : new Date(rec.receivedAt).toLocaleTimeString([], { hour: '2-digit', minute: '2-digit' }),
                                    students: new Set()
                                };
                            }
                            sessionsMap[sid].students.add(rec.fingerprintId || rec.roll);
                        });

                        const sessionsToday = Object.values(sessionsMap).map(s => ({
                            subject: s.subject,
                            time: s.time,
                            presentCount: s.students.size
                        }));

                        console.log(`[SCHEDULER] Sending daily report to ${teacherEmail} with ${sessionsToday.length} lectures.`);
                        
                        const dateFormatted = now.toLocaleDateString('en-US', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' });
                        await mailer.sendDailySummaryEmail(teacherName, teacherEmail, dateFormatted, sessionsToday);
                    }
                }

                lastReportDate = currentDateStr;
            }
        } catch (err) {
            console.error('[SCHEDULER] Error in daily report job:', err.message);
        }
    }, 15 * 60 * 1000); // Check every 15 minutes
}

// ============================================================
// LEAVE & MEDICAL APPLICATION ENDPOINTS
// ============================================================

// Submit application (Student)
app.post('/api/student/applications', verifyTokenWithRole(storage), requireRole(['STUDENT']), async (req, res) => {
    try {
        const { user, student } = await resolveStudentForRequest(req.user.email);
        if (!student) {
            return res.status(404).json({ success: false, message: 'Student profile not linked to this account.' });
        }

        const { reason, startDate, endDate, certificateUrl } = req.body;
        if (!reason || !startDate || !endDate) {
            return res.status(400).json({ success: false, message: 'Reason, startDate, and endDate are required.' });
        }

        const id = 'App_' + Date.now() + '_' + Math.random().toString(36).substr(2, 9);
        const application = {
            id,
            studentRoll: String(student.roll),
            studentName: student.name || req.user.email.split('@')[0],
            studentEmail: req.user.email,
            reason,
            startDate,
            endDate,
            certificateUrl: certificateUrl || '',
            status: 'PENDING',
            submittedAt: new Date().toISOString(),
            gfmComments: '',
            reviewedBy: '',
            reviewedAt: null
        };

        await storage.addApplication(application);
        await storage.addAuditLog({ event: 'LEAVE_APPLICATION_SUBMITTED', email: req.user.email, roll: student.roll, applicationId: id });

        res.json({ success: true, message: 'Leave application submitted successfully.', application });
    } catch (error) {
        console.error('submit-leave-application error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// Get all applications (GFM / Teacher / Admin)
app.get('/api/gfm/applications', verifyTokenWithRole(storage), requireRole(['GFM', 'TEACHER', 'ADMIN']), async (req, res) => {
    try {
        const applications = await storage.getApplications();
        res.json({ success: true, applications });
    } catch (error) {
        console.error('get-gfm-applications error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// Review application (GFM / Teacher / Admin)
app.post('/api/gfm/applications/:id/review', verifyTokenWithRole(storage), requireRole(['GFM', 'TEACHER', 'ADMIN']), async (req, res) => {
    try {
        const { id } = req.params;
        const { status, comments } = req.body;

        if (!status || !['APPROVED', 'REJECTED'].includes(status)) {
            return res.status(400).json({ success: false, message: 'Status must be APPROVED or REJECTED.' });
        }

        const apps = await storage.getApplications();
        const app = apps.find(a => a.id === id);
        if (!app) {
            return res.status(404).json({ success: false, message: 'Leave application not found.' });
        }

        const updates = {
            status,
            gfmComments: comments || '',
            reviewedBy: req.user.email,
            reviewedAt: new Date().toISOString()
        };

        const updatedApp = await storage.updateApplication(id, updates);
        await storage.addAuditLog({ event: 'LEAVE_APPLICATION_REVIEWED', email: req.user.email, status, applicationId: id });

        res.json({ success: true, message: `Application ${status.toLowerCase()} successfully.`, application: updatedApp });
    } catch (error) {
        console.error('review-leave-application error:', error);
        res.status(500).json({ success: false, message: 'Server error' });
    }
});

// ============================================================
// START SERVER
// ============================================================
async function startServer() {
    try {
        try { await storage.init(); } catch (e) { console.error('INIT THREW:', e); }
        try { firebaseAdminAvailable = admin.apps.length > 0; } catch (e) { console.error('ADMIN THREW:', e); }
        try { const httpServer = app.listen(PORT, '0.0.0.0', () => {
            console.log('============================================================');
            console.log('  ATTENDIFY - Phase 4 Backend Server');
            console.log(`  Storage: ${STORAGE_MODE} | Auth: ${AUTH_MODE} | Port: ${PORT}`);
            console.log('============================================================');
            console.log('  Public: / , /api/register-queue');
            console.log('  Protected: /dashboard, /api/dashboard-data, /api/authorize-enrollment');
            console.log('  Admin: /api/create-teacher, /api/users');
            console.log('  ESP32: /api/poll-status, /api/confirm-enrollment, /api/upload-attendance');
            console.log('  QR Scanner: /teacher/qr-scanner');
            console.log('============================================================');

            startDailyReportScheduler();

            Promise.resolve(storage.addAuditLog({ event: 'SERVER_START', port: PORT, storageMode: STORAGE_MODE, authMode: AUTH_MODE }))
                .catch((error) => {
                    console.error('[SERVER] Failed to write startup audit log:', error && error.message ? error.message : error);
                });
        });

        httpServer.on('error', (error) => {
            if (error && error.code === 'EADDRINUSE') {
                console.error(`[SERVER] Port ${PORT} is already in use. Stop the existing backend process, then start again.`);
                process.exit(1);
            }
            console.error('FAILED TO START HTTP SERVER:', error);
            process.exit(1);
        });

        startDiscoveryResponder(); } catch (e) { console.error('LISTEN THREW:', e); }
    } catch (error) {
        console.error('FAILED TO START:', error);
        process.exit(1);
    }
}

startServer();

process.on('SIGINT', () => {
    if (discoveryServer) {
        discoveryServer.close();
    }
    process.exit(0);
});
