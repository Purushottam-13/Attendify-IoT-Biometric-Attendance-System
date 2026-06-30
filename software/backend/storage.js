const fs = require('fs');
const path = require('path');
const admin = require('firebase-admin');

// ============================================================
// STORAGE ABSTRACTION LAYER
// ============================================================
class Storage {
    async init() { }
    getHealthStatus() { return { status: 'unknown', read: 'not_checked', write: 'not_checked' }; }
    async getQueue() { }
    async addToQueue(data) { }
    async updateQueueItem(roll, data) { }
    async removeFromQueue(roll) { }
    async clearQueue() { }
    async addStudent(student) { }
    async getStudents() { }
    async removeStudent(roll) { }
    async updateStudentAcademics(roll, details) { }
    async addAttendance(record) { }
    async getAttendance() { }
    async addAuditLog(entry) { }
    async getUserByEmail(email) { }
    async createUser(userData) { }
    async updateUser(email, updates) { }
    async deleteUser(email) { }
    async getUsers() { }
    async loadData() { }
    async saveData(data) { }
    async updateDeviceStatus(deviceId, statusData) { }
    async getApplications() { }
    async addApplication(data) { }
    async updateApplication(id, data) { }
    async getWifiProfiles() { return []; }
    async addWifiProfile(profile) { }
    async removeWifiProfile(id) { }
    
    // Timetable & Announcements
    async getTimetable(className) { }
    async saveTimetable(className, timetableData) { }
    async getAnnouncements(className) { }
    async addAnnouncement(announcement) { }
    async removeAnnouncement(id) { }
}

// ============================================================
// LOCAL STORAGE (JSON FILE)
// ============================================================
class LocalStorage extends Storage {
    constructor(dbFile, auditFile) {
        super();
        this.dbFile = dbFile;
        this.auditFile = auditFile;
    }

    async init() {
        if (!fs.existsSync(this.dbFile)) {
            this.writeDb({ queue: [], students: [], attendance: [], users: [] });
        }
        if (!fs.existsSync(this.auditFile)) {
            fs.writeFileSync(this.auditFile, `=== ATTENDIFY AUDIT LOG ===\n`, 'utf8');
        }
        const db = this.readDb();
        if (!db.users || db.users.length === 0) {
            db.users = [
                { email: 'admin@attendify.com', role: 'ADMIN', createdAt: new Date().toISOString() },
                { email: 'teacher@attendify.com', role: 'TEACHER', createdAt: new Date().toISOString() }
            ];
            this.writeDb(db);
            console.log('[STORAGE] Seeded default admin and teacher users');
        }
        console.log('[STORAGE] Local JSON storage initialized');
    }

    getHealthStatus() {
        return { status: 'ok', read: 'not_checked', write: 'not_checked' };
    }

    readDb() {
        try {
            const data = fs.readFileSync(this.dbFile, 'utf8');
            if (!data) return { queue: [], students: [], attendance: [], users: [] };
            const json = JSON.parse(data);
            if (!Array.isArray(json.queue)) json.queue = [];
            if (!Array.isArray(json.users)) json.users = [];
            return json;
        } catch (error) {
            return { queue: [], students: [], attendance: [], users: [] };
        }
    }

    writeDb(data) {
        fs.writeFileSync(this.dbFile, JSON.stringify(data, null, 2), 'utf8');
    }

    async getQueue() {
        return this.readDb().queue || [];
    }

    async addToQueue(data) {
        const db = this.readDb();
        const existingIndex = db.queue.findIndex(q => q.roll === data.roll);
        if (existingIndex >= 0) {
            db.queue[existingIndex] = data;
        } else {
            db.queue.push(data);
        }
        this.writeDb(db);
        return data;
    }

    async updateQueueItem(roll, data) {
        const db = this.readDb();
        const index = db.queue.findIndex(q => q.roll === roll);
        if (index >= 0) {
            db.queue[index] = { ...db.queue[index], ...data };
            this.writeDb(db);
            return db.queue[index];
        }
        return null;
    }

    async removeFromQueue(roll) {
        const db = this.readDb();
        db.queue = db.queue.filter(q => q.roll !== roll);
        this.writeDb(db);
    }

    async clearQueue() {
        const db = this.readDb();
        db.queue = [];
        this.writeDb(db);
    }

    async addStudent(student) {
        const db = this.readDb();
        db.students.push(student);
        this.writeDb(db);
        return student;
    }

    async getStudents() {
        return this.readDb().students;
    }

    async removeStudent(roll) {
        const db = this.readDb();
        db.students = db.students.filter(s => s.roll !== roll);
        this.writeDb(db);
        return true;
    }

    async updateStudentAcademics(roll, details) {
        const db = this.readDb();
        const student = db.students.find(s => s.roll === roll);
        if (student) {
            if (details.enrolledSubjects !== undefined) student.enrolledSubjects = details.enrolledSubjects;
            if (details.studentClass !== undefined) student.studentClass = details.studentClass;
            if (details.practicalBatch !== undefined) student.practicalBatch = details.practicalBatch;
            if (details.lastWarningSent !== undefined) student.lastWarningSent = details.lastWarningSent;
            this.writeDb(db);
            return student;
        }
        return null;
    }

    async addAttendance(record) {
        const db = this.readDb();

        // Deduplication Logic: Check if same student in same session already exists
        const isDuplicate = db.attendance.some(a =>
            a.sessionId === record.sessionId && a.roll === record.roll
        );

        if (isDuplicate) {
            // Already recorded, skip adding but return record to signal 'success' to hardware
            return record;
        }

        db.attendance.push(record);
        this.writeDb(db);
        return record;
    }

    async getAttendance() {
        return this.readDb().attendance;
    }

    async addAuditLog(entry) {
        const timestamp = new Date().toLocaleTimeString('en-US', { hour12: false });
        const details = entry.details || JSON.stringify(entry);
        const logLine = `[${timestamp}] ${entry.event} ${details}\n`;
        fs.appendFileSync(this.auditFile, logLine, 'utf8');
        console.log(logLine.trim());
    }

    async getUserByEmail(email) {
        return this.readDb().users.find(u => u.email === email) || null;
    }

    async createUser(userData) {
        const db = this.readDb();
        if (db.users.find(u => u.email === userData.email)) {
            throw new Error('User already exists');
        }
        db.users.push(userData);
        this.writeDb(db);
        return userData;
    }

    async updateUser(email, updates) {
        const db = this.readDb();
        const index = db.users.findIndex(u => u.email === email);
        if (index < 0) {
            throw new Error('User not found');
        }
        const cleanUpdates = Object.fromEntries(
            Object.entries(updates || {}).filter(([, value]) => value !== undefined)
        );
        db.users[index] = {
            ...db.users[index],
            ...cleanUpdates,
            updatedAt: new Date().toISOString()
        };
        this.writeDb(db);
        return db.users[index];
    }

    async deleteUser(email) {
        const db = this.readDb();
        const before = db.users.length;
        const targetEmail = String(email || '').toLowerCase().trim();
        db.users = db.users.filter(u => String(u.email || '').toLowerCase().trim() !== targetEmail);
        if (db.users.length === before) {
            throw new Error('User not found');
        }
        this.writeDb(db);
        return true;
    }


    async getUsers() {
        return this.readDb().users;
    }

    async loadData() {
        return this.readDb();
    }

    async saveData(data) {
        this.writeDb(data);
    }

    async updateDeviceStatus(deviceId, statusData) {
        const db = this.readDb();
        if (!db.deviceStatus || typeof db.deviceStatus !== 'object' || Array.isArray(db.deviceStatus)) {
            db.deviceStatus = {};
        }
        db.deviceStatus[deviceId] = {
            ...(db.deviceStatus[deviceId] || {}),
            ...statusData
        };
        this.writeDb(db);
        return db.deviceStatus[deviceId];
    }

    async getApplications() {
        return this.readDb().applications || [];
    }

    async addApplication(data) {
        const db = this.readDb();
        if (!db.applications) db.applications = [];
        db.applications.push(data);
        this.writeDb(db);
        return data;
    }

    async updateApplication(id, data) {
        const db = this.readDb();
        if (!db.applications) db.applications = [];
        const index = db.applications.findIndex(a => a.id === id);
        if (index >= 0) {
            db.applications[index] = { ...db.applications[index], ...data };
            this.writeDb(db);
            return db.applications[index];
        }
        return null;
    }

    async getWifiProfiles() {
        return this.readDb().wifiProfiles || [];
    }

    async addWifiProfile(profile) {
        const db = this.readDb();
        if (!db.wifiProfiles) db.wifiProfiles = [];
        // Deduplicate by SSID - update if exists
        const existingIndex = db.wifiProfiles.findIndex(p => p.ssid === profile.ssid);
        if (existingIndex >= 0) {
            db.wifiProfiles[existingIndex] = { ...db.wifiProfiles[existingIndex], ...profile, updatedAt: new Date().toISOString() };
        } else {
            db.wifiProfiles.push(profile);
        }
        this.writeDb(db);
        return profile;
    }

    async removeWifiProfile(id) {
        const db = this.readDb();
        if (!db.wifiProfiles) return false;
        const before = db.wifiProfiles.length;
        db.wifiProfiles = db.wifiProfiles.filter(p => p.id !== id);
        this.writeDb(db);
        return db.wifiProfiles.length < before;
    }

    async getTimetable(className) {
        const db = this.readDb();
        if (!db.timetable) db.timetable = {};
        return db.timetable[className] || null;
    }

    async saveTimetable(className, timetableData) {
        const db = this.readDb();
        if (!db.timetable) db.timetable = {};
        db.timetable[className] = timetableData;
        this.writeDb(db);
        return timetableData;
    }

    async getAnnouncements(className) {
        const db = this.readDb();
        if (!db.announcements) db.announcements = [];
        if (className) {
            return db.announcements.filter(a => a.targetClass === className || a.targetClass === 'ALL');
        }
        return db.announcements;
    }

    async addAnnouncement(announcement) {
        const db = this.readDb();
        if (!db.announcements) db.announcements = [];
        db.announcements.push(announcement);
        this.writeDb(db);
        return announcement;
    }

    async removeAnnouncement(id) {
        const db = this.readDb();
        if (!db.announcements) return false;
        const before = db.announcements.length;
        db.announcements = db.announcements.filter(a => a.id !== id);
        this.writeDb(db);
        return db.announcements.length < before;
    }
}

// ============================================================
// CLOUD STORAGE (FIREBASE FIRESTORE)
// ============================================================
class CloudStorage extends Storage {
    constructor(dbFile, auditFile) {
        super();
        this.db = null;
        this.dbFile = dbFile;
        this.auditFile = auditFile;
        this.mirroringEnabled = !!dbFile;
        this.ready = false;
        this.initError = null;
    }

    async init() {
        try {
            let serviceAccount;
            if (process.env.FIREBASE_SERVICE_ACCOUNT) {
                // Parse from Render/Vercel Env Variable
                console.log('[STORAGE] Loading Firebase credentials from environment variable');
                serviceAccount = JSON.parse(process.env.FIREBASE_SERVICE_ACCOUNT);
            } else {
                // Fallback to local file for development
                console.log('[STORAGE] Loading Firebase credentials from local file');
                serviceAccount = require('./serviceAccountKey.json');
            }

            if (admin.apps.length === 0) {
                admin.initializeApp({
                    credential: admin.credential.cert(serviceAccount)
                });
            }
            this.db = admin.firestore();
            this.ready = true;
            this.initError = null;
            console.log('[STORAGE] Firebase Firestore initialized');
            
            if (this.mirroringEnabled) {
                console.log('[STORAGE] Local mirroring enabled for Cloud mode');
                if (!fs.existsSync(this.dbFile)) {
                    fs.writeFileSync(this.dbFile, JSON.stringify({ queue: [], students: [], attendance: [], users: [] }, null, 2));
                }
                if (!fs.existsSync(this.auditFile)) {
                   fs.writeFileSync(this.auditFile, `=== ATTENDIFY AUDIT LOG (MIRROR) ===\n`, 'utf8');
                }
            }
            if (process.env.SEED_DEFAULT_USERS === 'true') {
                try {
                    await this.seedDefaultUsers();
                } catch (error) {
                    this.initError = error.message;
                    console.warn('[STORAGE] Default user seed skipped:', error.message);
                }
            } else {
                console.log('[STORAGE] Default user seed skipped. Set SEED_DEFAULT_USERS=true to enable.');
            }
        } catch (error) {
            this.ready = false;
            this.initError = error.message;
            console.error('[STORAGE] Firebase init failed:', error.message);
            console.warn('[WARNING] Running with limited/no DB connection. Quota may be exceeded.');
            // Do not throw, so the server can at least start up!
        }
    }

    getHealthStatus() {
        return {
            status: this.ready ? 'ok' : 'degraded',
            read: 'not_checked',
            write: 'not_checked',
            ...(this.initError ? { reason: this.initError } : {})
        };
    }

    readLocalMirrorFallback() {
        if (!this.mirroringEnabled || !fs.existsSync(this.dbFile)) {
            return { queue: [], students: [], attendance: [], users: [] };
        }
        try {
            const data = fs.readFileSync(this.dbFile, 'utf8');
            return JSON.parse(data);
        } catch (e) {
            console.error('[STORAGE] Local mirror read failed:', e.message);
            return { queue: [], students: [], attendance: [], users: [] };
        }
    }

    async mirrorLocal() {
        if (!this.mirroringEnabled) return;
        try {
            const data = await this.loadData();
            fs.writeFileSync(this.dbFile, JSON.stringify(data, null, 2), 'utf8');
        } catch (e) {
            console.error('[MIRROR] Failed to update local mirror:', e.message);
        }
    }

    async seedDefaultUsers() {
        const usersRef = this.db.collection('users');
        const snapshot = await usersRef.limit(1).get();
        if (snapshot.empty) {
            console.log('[STORAGE] Seeding default users...');
            await usersRef.doc('admin@attendify.com').set({
                email: 'admin@attendify.com',
                role: 'ADMIN',
                createdAt: admin.firestore.FieldValue.serverTimestamp()
            });
            await usersRef.doc('teacher@attendify.com').set({
                email: 'teacher@attendify.com',
                role: 'TEACHER',
                createdAt: admin.firestore.FieldValue.serverTimestamp()
            });
            console.log('[STORAGE] Default users seeded');
        }
    }

    async getQueue() {
        const snapshot = await this.db.collection('queue').get();
        const items = snapshot.docs.map(doc => doc.data());
        // Sort in-memory to avoid requiring a Firestore composite index
        items.sort((a, b) => (a.requestedAt || '').localeCompare(b.requestedAt || ''));
        return items;
    }

    async addToQueue(data) {
        await this.db.collection('queue').doc(data.roll.toString()).set(data);
        await this.mirrorLocal();
        return data;
    }

    async updateQueueItem(roll, data) {
        await this.db.collection('queue').doc(roll.toString()).update(data);
        await this.mirrorLocal();
        const doc = await this.db.collection('queue').doc(roll.toString()).get();
        return doc.data();
    }

    async removeFromQueue(roll) {
        await this.db.collection('queue').doc(roll.toString()).delete();
        await this.mirrorLocal();
    }

    async clearQueue() {
        const snapshot = await this.db.collection('queue').get();
        const batch = this.db.batch();
        snapshot.docs.forEach(doc => batch.delete(doc.ref));
        await batch.commit();
        await this.mirrorLocal();
    }

    async addStudent(student) {
        await this.db.collection('students').doc(student.roll.toString()).set(student);
        await this.mirrorLocal();
        return student;
    }

    async getStudents() {
        const snapshot = await this.db.collection('students').get();
        return snapshot.docs.map(doc => doc.data());
    }

    async removeStudent(roll) {
        await this.db.collection('students').doc(roll.toString()).delete();
        await this.mirrorLocal();
        return true;
    }

    async updateStudentAcademics(roll, details) {
        try {
            const docRef = this.db.collection('students').doc(roll.toString());
            await docRef.set(details, { merge: true });
            await this.mirrorLocal();
            const doc = await docRef.get();
            return doc.exists ? doc.data() : null;
        } catch (error) {
            console.warn(`[STORAGE] Firestore failed to update student academics. Falling back to local mirror for: ${roll}`);
            if (this.mirroringEnabled) {
                const localData = this.readLocalMirrorFallback();
                const student = localData.students.find(s => s.roll === roll);
                if (student) {
                    if (details.enrolledSubjects !== undefined) student.enrolledSubjects = details.enrolledSubjects;
                    if (details.studentClass !== undefined) student.studentClass = details.studentClass;
                    if (details.practicalBatch !== undefined) student.practicalBatch = details.practicalBatch;
                    if (details.lastWarningSent !== undefined) student.lastWarningSent = details.lastWarningSent;
                    fs.writeFileSync(this.dbFile, JSON.stringify(localData, null, 2), 'utf8');
                    return student;
                }
                return null;
            }
            throw error;
        }
    }

    async addAttendance(record) {
        // Deduplication Logic for Cloud
        const query = await this.db.collection('attendance')
            .where('sessionId', '==', record.sessionId)
            .where('roll', '==', record.roll)
            .limit(1).get();

        if (!query.empty) {
            return record;
        }

        const data = { ...record, serverTime: admin.firestore.FieldValue.serverTimestamp() };
        await this.db.collection('attendance').add(data);
        await this.mirrorLocal();
        return data;
    }


    async getAttendance() {
        const snapshot = await this.db.collection('attendance').orderBy('timestamp', 'desc').get();
        return snapshot.docs.map(doc => doc.data());
    }

    async addAuditLog(entry) {
        const data = { ...entry, timestamp: admin.firestore.FieldValue.serverTimestamp() };
        await this.db.collection('audit_logs').add(data);

        const timestampStr = new Date().toLocaleTimeString('en-US', { hour12: false });
        const logLine = `[${timestampStr}] ${entry.event} ${entry.details || JSON.stringify(entry)}\n`;
        console.log(logLine.trim());

        if (this.mirroringEnabled) {
            try { fs.appendFileSync(this.auditFile, logLine, 'utf8'); } catch(e) {}
        }
    }

    async getUserByEmail(email) {
        try {
            // Query by the actual 'email' field rather than relying on Document ID
            const snapshot = await this.db.collection('users').where('email', '==', email).limit(1).get();
            if (!snapshot.empty) {
                return snapshot.docs[0].data();
            }
            // Fallback: Check if the document ID matches (just in case)
            const doc = await this.db.collection('users').doc(email).get();
            return doc.exists ? doc.data() : null;
        } catch (error) {
            console.warn(`[STORAGE] Firestore failed (${error.message}). Falling back to local mirror for user: ${email}`);
            const localData = this.readLocalMirrorFallback();
            return localData.users ? localData.users.find(u => u.email === email) || null : null;
        }
    }

    async createUser(userData) {
        try {
            const existing = await this.getUserByEmail(userData.email);
            if (existing && existing.createdAt) throw new Error('User already exists');
            const data = { ...userData, createdAt: admin.firestore.FieldValue.serverTimestamp() };
            await this.db.collection('users').doc(userData.email).set(data);
            await this.mirrorLocal();
            return data;
        } catch (error) {
            console.warn(`[STORAGE] Firestore failed to create user. Falling back to local mirror for: ${userData.email}`);
            if (this.mirroringEnabled) {
                const localData = this.readLocalMirrorFallback();
                if (!localData.users) localData.users = [];
                if (localData.users.find(u => u.email === userData.email)) throw new Error('User already exists');
                const data = { ...userData, createdAt: new Date().toISOString() };
                localData.users.push(data);
                fs.writeFileSync(this.dbFile, JSON.stringify(localData, null, 2), 'utf8');
                return data;
            }
            throw error;
        }
    }

    async getUserRefByEmail(email) {
        const snapshot = await this.db.collection('users').where('email', '==', email).limit(1).get();
        if (!snapshot.empty) {
            return snapshot.docs[0].ref;
        }
        const ref = this.db.collection('users').doc(email);
        const doc = await ref.get();
        return doc.exists ? ref : null;
    }

    async updateUser(email, updates) {
        const ref = await this.getUserRefByEmail(email);
        if (!ref) {
            throw new Error('User not found');
        }
        const cleanUpdates = Object.fromEntries(
            Object.entries(updates || {}).filter(([, value]) => value !== undefined)
        );
        await ref.set({
            ...cleanUpdates,
            updatedAt: admin.firestore.FieldValue.serverTimestamp()
        }, { merge: true });
        await this.mirrorLocal();
        const updated = await ref.get();
        return updated.data();
    }

    async deleteUser(email) {
        let ref = await this.getUserRefByEmail(email);
        if (!ref) {
            try {
                const snapshot = await this.db.collection('users').get();
                const targetDoc = snapshot.docs.find(doc => {
                    const data = doc.data();
                    return String(data.email || '').toLowerCase().trim() === String(email || '').toLowerCase().trim();
                });
                if (targetDoc) {
                    ref = targetDoc.ref;
                }
            } catch (e) {
                console.error('[STORAGE] Error finding user for case-insensitive delete:', e);
            }
        }
        if (!ref) {
            throw new Error('User not found');
        }
        await ref.delete();
        await this.mirrorLocal();
        return true;
    }


    async getUsers() {
        try {
            const snapshot = await this.db.collection('users').get();
            return snapshot.docs.map(doc => doc.data());
        } catch (error) {
            console.warn(`[STORAGE] Firestore failed (${error.message}). Falling back to local mirror for getUsers`);
            return this.readLocalMirrorFallback().users || [];
        }
    }

    async loadData() {
        try {
            // FETCH AS AGGREGATE OBJECT
            const queueSnap = await this.db.collection('queue').get();
            const studentsSnap = await this.db.collection('students').get();
            const attendanceSnap = await this.db.collection('attendance').get();
            const settingsSnap = await this.db.collection('settings').doc('deviceConfigs').get();
            const usersSnap = await this.db.collection('users').get();
            const settings = settingsSnap.exists ? settingsSnap.data() : {};

            let applications = [];
            try {
                const appsSnap = await this.db.collection('applications').get();
                applications = appsSnap.docs.map(d => d.data());
            } catch (err) {
                console.warn('[STORAGE] Failed to fetch applications from firestore:', err.message);
            }

            let announcements = [];
            try {
                const announcementsSnap = await this.db.collection('announcements').get();
                announcements = announcementsSnap.docs.map(d => d.data());
            } catch (err) {
                console.warn('[STORAGE] Failed to fetch announcements from firestore:', err.message);
            }

            const data = {
                queue: queueSnap.docs.map(d => d.data()),
                students: studentsSnap.docs.map(d => d.data()),
                attendance: attendanceSnap.docs.map(d => d.data()),
                users: usersSnap.docs.map(d => d.data()),
                applications,
                announcements,
                deviceConfigs: settings.deviceConfigs || [],
                timetable: settings.timetable || {},
                deviceStatus: settings.deviceStatus || {}
            };

            return data;
        } catch (error) {
            console.warn(`[STORAGE] Firestore failed (${error.message}). Falling back to local mirror for loadData`);
            return this.readLocalMirrorFallback();
        }
    }

    async saveData(data) {
        // If data contains full object, save specific parts to settings
        if (data.deviceConfigs || data.timetable) {
            const updates = {};
            if (data.deviceConfigs) updates.deviceConfigs = data.deviceConfigs;
            if (data.timetable) updates.timetable = data.timetable;
            if (data.deviceStatus) updates.deviceStatus = data.deviceStatus;
            await this.db.collection('settings').doc('deviceConfigs').set(updates, { merge: true });
        }
        await this.mirrorLocal();
    }

    async updateDeviceStatus(deviceId, statusData) {
        const ref = this.db.collection('settings').doc('deviceConfigs');
        await ref.set({ deviceStatus: { [deviceId]: statusData } }, { merge: true });
        await this.mirrorLocal();
        return statusData;
    }

    async getApplications() {
        const snapshot = await this.db.collection('applications').get();
        return snapshot.docs.map(doc => doc.data());
    }

    async addApplication(data) {
        await this.db.collection('applications').doc(data.id).set(data);
        await this.mirrorLocal();
        return data;
    }

    async updateApplication(id, data) {
        await this.db.collection('applications').doc(id).update(data);
        await this.mirrorLocal();
        const doc = await this.db.collection('applications').doc(id).get();
        return doc.data();
    }

    async getWifiProfiles() {
        try {
            const snapshot = await this.db.collection('wifiProfiles').get();
            return snapshot.docs.map(doc => doc.data());
        } catch (error) {
            console.warn('[STORAGE] Firestore getWifiProfiles failed:', error.message);
            return this.readLocalMirrorFallback().wifiProfiles || [];
        }
    }

    async addWifiProfile(profile) {
        try {
            await this.db.collection('wifiProfiles').doc(profile.id).set(profile);
            await this.mirrorLocal();
            return profile;
        } catch (error) {
            console.warn('[STORAGE] Firestore addWifiProfile failed:', error.message);
            if (this.mirroringEnabled) {
                const localData = this.readLocalMirrorFallback();
                if (!localData.wifiProfiles) localData.wifiProfiles = [];
                const idx = localData.wifiProfiles.findIndex(p => p.ssid === profile.ssid);
                if (idx >= 0) {
                    localData.wifiProfiles[idx] = { ...localData.wifiProfiles[idx], ...profile };
                } else {
                    localData.wifiProfiles.push(profile);
                }
                const fs = require('fs');
                fs.writeFileSync(this.dbFile, JSON.stringify(localData, null, 2), 'utf8');
                return profile;
            }
            throw error;
        }
    }

    async removeWifiProfile(id) {
        try {
            await this.db.collection('wifiProfiles').doc(id).delete();
            await this.mirrorLocal();
            return true;
        } catch (error) {
            console.warn('[STORAGE] Firestore removeWifiProfile failed:', error.message);
            return false;
        }
    }

    async getTimetable(className) {
        try {
            const doc = await this.db.collection('settings').doc('deviceConfigs').get();
            const settings = doc.exists ? doc.data() : {};
            const timetable = settings.timetable || {};
            return timetable[className] || null;
        } catch (error) {
            console.warn(`[STORAGE] Firestore getTimetable failed. Checking local mirror.`);
            const localData = this.readLocalMirrorFallback();
            return (localData.timetable && localData.timetable[className]) || null;
        }
    }

    async saveTimetable(className, timetableData) {
        try {
            const ref = this.db.collection('settings').doc('deviceConfigs');
            await ref.set({ timetable: { [className]: timetableData } }, { merge: true });
            await this.mirrorLocal();
            return timetableData;
        } catch (error) {
            console.warn(`[STORAGE] Firestore saveTimetable failed. Saving to local mirror.`);
            if (this.mirroringEnabled) {
                const localData = this.readLocalMirrorFallback();
                if (!localData.timetable) localData.timetable = {};
                localData.timetable[className] = timetableData;
                fs.writeFileSync(this.dbFile, JSON.stringify(localData, null, 2), 'utf8');
                return timetableData;
            }
            throw error;
        }
    }

    async getAnnouncements(className) {
        try {
            const snapshot = await this.db.collection('announcements').get();
            let items = snapshot.docs.map(doc => doc.data());
            items.sort((a, b) => (b.postedAt || '').localeCompare(a.postedAt || ''));
            if (className) {
                return items.filter(a => a.targetClass === className || a.targetClass === 'ALL');
            }
            return items;
        } catch (error) {
            console.warn(`[STORAGE] Firestore getAnnouncements failed. Checking local mirror.`);
            const localData = this.readLocalMirrorFallback();
            const items = localData.announcements || [];
            items.sort((a, b) => (b.postedAt || '').localeCompare(a.postedAt || ''));
            if (className) {
                return items.filter(a => a.targetClass === className || a.targetClass === 'ALL');
            }
            return items;
        }
    }

    async addAnnouncement(announcement) {
        try {
            await this.db.collection('announcements').doc(announcement.id).set(announcement);
            await this.mirrorLocal();
            return announcement;
        } catch (error) {
            console.warn(`[STORAGE] Firestore addAnnouncement failed. Saving to local mirror.`);
            if (this.mirroringEnabled) {
                const localData = this.readLocalMirrorFallback();
                if (!localData.announcements) localData.announcements = [];
                localData.announcements.push(announcement);
                fs.writeFileSync(this.dbFile, JSON.stringify(localData, null, 2), 'utf8');
                return announcement;
            }
            throw error;
        }
    }

    async removeAnnouncement(id) {
        try {
            await this.db.collection('announcements').doc(id).delete();
            await this.mirrorLocal();
            return true;
        } catch (error) {
            console.warn(`[STORAGE] Firestore removeAnnouncement failed. Deleting from local mirror.`);
            if (this.mirroringEnabled) {
                const localData = this.readLocalMirrorFallback();
                if (!localData.announcements) return false;
                const before = localData.announcements.length;
                localData.announcements = localData.announcements.filter(a => a.id !== id);
                fs.writeFileSync(this.dbFile, JSON.stringify(localData, null, 2), 'utf8');
                return localData.announcements.length < before;
            }
            throw error;
        }
    }
}

module.exports = { LocalStorage, CloudStorage };
