/**
 * ============================================================
 *  ATTENDIFY - PHASE 4 AUTH MIDDLEWARE
 *  Firebase Authentication & Role-Based Access Control
 * ============================================================
 */

const admin = require('firebase-admin');
const crypto = require('crypto');

function normalizeAuthMode(value) {
    const mode = String(value || '').trim().toUpperCase();
    return mode === 'LOCAL' ? 'LOCAL' : 'FIREBASE';
}

function normalizeStorageMode(value) {
    const mode = String(value || '').trim().toUpperCase();
    if (mode === 'LOCAL' || mode === 'JSON') return 'LOCAL';
    return 'CLOUD';
}

const STORAGE_MODE = normalizeStorageMode(process.env.STORAGE_MODE);
const AUTH_MODE = normalizeAuthMode(process.env.AUTH_MODE || (STORAGE_MODE === 'LOCAL' ? 'LOCAL' : 'FIREBASE'));

function decodeBase64Url(input) {
    const normalized = String(input).replace(/-/g, '+').replace(/_/g, '/');
    const padLength = (4 - (normalized.length % 4)) % 4;
    const padded = normalized + '='.repeat(padLength);
    return Buffer.from(padded, 'base64').toString('utf-8');
}

function verifyAttendifySessionToken(token) {
    if (!token || !token.startsWith('ATD_TOKEN_')) return null;

    const raw = token.substring('ATD_TOKEN_'.length);
    const parts = raw.split('.');
    if (parts.length !== 2) return null;

    const payloadEncoded = parts[0];
    const signature = parts[1];
    const secret = process.env.ATTENDIFY_SESSION_SECRET || process.env.ERP_SSO_SECRET || 'ATTENDIFY_DEV_SESSION_SECRET';
    const expectedSignature = crypto.createHmac('sha256', secret).update(payloadEncoded).digest('base64url');

    if (signature !== expectedSignature) return null;

    try {
        const payloadJson = decodeBase64Url(payloadEncoded);
        const payload = JSON.parse(payloadJson);
        if (!payload || !payload.email) return null;

        const now = Math.floor(Date.now() / 1000);
        if (payload.exp && now > Number(payload.exp)) return null;

        return payload;
    } catch (error) {
        return null;
    }
}

/**
 * Middleware: Verify Firebase ID Token
 * Extracts token from Authorization header and verifies it
 * Attaches user info to req.user
 */
async function verifyToken(req, res, next) {
    const authHeader = req.headers.authorization;

    if (!authHeader || !authHeader.startsWith('Bearer ')) {
        return res.status(401).json({
            success: false,
            message: 'No authentication token provided'
        });
    }

    const token = authHeader.split('Bearer ')[1];

    try {
        const decodedToken = await admin.auth().verifyIdToken(token);
        req.user = {
            uid: decodedToken.uid,
            email: decodedToken.email
        };
        next();
    } catch (error) {
        console.error('Token verification failed:', error.message);
        return res.status(401).json({
            success: false,
            message: 'Invalid or expired token'
        });
    }
}

/**
 * Middleware: Verify Firebase ID Token AND fetch role from Firestore
 */
function verifyTokenWithRole(storage) {
    if (!storage || typeof storage.getUserByEmail !== 'function') {
        console.error('[AUTH_DEBUG] CRITICAL: Invalid storage object passed to verifyTokenWithRole');
        console.error('[AUTH_DEBUG] Storage is:', storage);
    }
    return async (req, res, next) => {
        const authHeader = req.headers.authorization;

        if (!authHeader || !authHeader.startsWith('Bearer ')) {
            return res.status(401).json({
                success: false,
                message: 'No authentication token provided'
            });
        }

        const token = authHeader.split('Bearer ')[1];

        const attendifySession = verifyAttendifySessionToken(token);
        if (attendifySession) {
            const user = await storage.getUserByEmail(attendifySession.email);
            if (!user) {
                return res.status(403).json({ success: false, message: 'Session user not found' });
            }

            req.user = {
                uid: attendifySession.uid || `erp_${Buffer.from(attendifySession.email).toString('base64').substring(0, 12)}`,
                email: user.email,
                role: user.role,
                is_gfm: user.is_gfm === true
            };
            return next();
        }

        if (token.startsWith('LOCAL_TOKEN_')) {
            if (AUTH_MODE !== 'LOCAL') {
                return res.status(401).json({ success: false, message: `Local tokens are disabled while AUTH_MODE=${AUTH_MODE}` });
            }
            try {
                const encodedEmail = token.replace('LOCAL_TOKEN_', '');
                const email = Buffer.from(encodedEmail, 'base64').toString('utf-8');
                const user = await storage.getUserByEmail(email);

                if (!user) {
                    return res.status(403).json({ success: false, message: 'Local user not found' });
                }

                req.user = {
                    uid: `local_${encodedEmail.substring(0, 8)}`,
                    email: user.email,
                    role: user.role,
                    is_gfm: user.is_gfm === true
                };
                return next();
            } catch (e) {
                console.error('Local token parse failed:', e.message);
                return res.status(401).json({ success: false, message: 'Invalid local token' });
            }
        }

        if (AUTH_MODE !== 'FIREBASE') {
            return res.status(401).json({ success: false, message: `Firebase tokens are disabled while AUTH_MODE=${AUTH_MODE}` });
        }

        try {
            const decodedToken = await admin.auth().verifyIdToken(token);
            let user = await storage.getUserByEmail(decodedToken.email);

            // AUTO-REGISTER: If Firebase Auth user doesn't exist in local DB, create them
            if (!user) {
                console.log(`[AUTH] Auto-registering new user: ${decodedToken.email}`);
                try {
                    // First user becomes ADMIN, rest become TEACHER
                    const existingUsers = await storage.getUsers();
                    const hasAdmin = existingUsers.some(u => u.role === 'ADMIN');
                    const autoRole = hasAdmin ? 'TEACHER' : 'ADMIN';

                    const newUser = {
                        email: decodedToken.email,
                        role: autoRole,
                        uid: decodedToken.uid,
                        name: decodedToken.name || decodedToken.email.split('@')[0],
                        createdAt: new Date().toISOString(),
                        autoRegistered: true
                    };
                    await storage.createUser(newUser);
                    await storage.addAuditLog({ event: 'USER_AUTO_REGISTERED', email: decodedToken.email, role: autoRole });
                    console.log(`[AUTH] Auto-registered ${decodedToken.email} as ${autoRole}`);
                    user = newUser;
                } catch (regError) {
                    console.error('[AUTH] Auto-registration failed:', regError.message);
                    return res.status(403).json({
                        success: false,
                        message: 'User not authorized and auto-registration failed'
                    });
                }
            }

            req.user = {
                uid: decodedToken.uid,
                email: decodedToken.email,
                role: user.role,
                is_gfm: user.is_gfm === true
            };

            next();
        } catch (error) {
            console.error('RBAC Middleware error:', error.message);
            return res.status(401).json({
                success: false,
                message: 'Authentication failed: ' + error.message
            });
        }
    };
}

/**
 * Middleware Factory: Require specific role(s)
 */
function requireRole(allowedRoles) {
    const roles = Array.isArray(allowedRoles) ? allowedRoles : [allowedRoles];

    return (req, res, next) => {
        if (!req.user || !req.user.role) {
            return res.status(403).json({
                success: false,
                message: 'Access denied: No role assigned'
            });
        }

        const userRole = req.user.role;
        const isGfmUser = req.user.is_gfm === true;

        // A user matches if: 
        // 1. Their direct role is in the list
        // 2. OR the list requires 'GFM' and they have the is_gfm flag
        const hasDirectRole = roles.includes(userRole);
        const hasGfmPermission = roles.includes('GFM') && isGfmUser;

        if (!hasDirectRole && !hasGfmPermission) {
            return res.status(403).json({
                success: false,
                message: `Access denied: Requires ${roles.join(' or ')} role`
            });
        }

        next();
    };
}

module.exports = { verifyToken, verifyTokenWithRole, requireRole };
