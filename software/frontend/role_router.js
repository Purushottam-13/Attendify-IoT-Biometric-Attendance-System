/**
 * ============================================================
 *  ATTENDIFY - ROLE ROUTER
 *  Handles role-based UI rendering
 * ============================================================
 */

const RoleRouter = {
    currentUser: null,

    async init() {
        try {
            const token = localStorage.getItem('attendify_token');
            if (!token) {
                window.location.href = '/login';
                return;
            }

            const res = await fetch('/api/auth/me', {
                headers: { 'Authorization': `Bearer ${token}` }
            });

            if (!res.ok) {
                throw new Error('Auth failed');
            }

            const data = await res.json();
            this.currentUser = data.user;

            console.log('[ROUTER] Logged in as:', this.currentUser.role);
            this.routeUser(this.currentUser.role);

        } catch (error) {
            console.error('[ROUTER] Init error:', error);
            // Clear everything to prevent loops
            localStorage.removeItem('attendify_token');
            localStorage.removeItem('attendify_email');
            try {
                if (window.firebase) await firebase.auth().signOut();
            } catch (e) { console.error('SignOut error', e); }
            window.location.href = '/login';
        }
    },

    routeUser(role) {
        // Redirect to role-specific dashboard
        switch (role) {
            case 'ADMIN':
                window.location.href = '/admin/dashboard';
                break;
            case 'GFM':
            case 'TEACHER':
                // UNIFIED ACADEMIC DASHBOARD (Subject Teachers & GFMs)
                window.location.href = '/teacher/dashboard';
                break;
            case 'STUDENT':
                window.location.href = '/student/dashboard';
                break;
            default:
                const container = document.getElementById('dashboard-content');
                if (container) {
                    container.innerHTML = '<div class="error">Unknown Role</div>';
                }
        }
    },

    loadAdminUI(container) {
        console.log('[ROUTER] Loading Admin UI');
        // Dynamic import or simple script loading
        // For Phase 3, we'll inject HTML directly or load components
        container.innerHTML = `
            <div class="role-dashboard admin-dashboard">
                <h1>🛡️ Admin Dashboard</h1>
                <div class="card-grid">
                    <div class="card" onclick="window.location.href='/device-config'">
                        <h3>📱 Device Config</h3>
                        <p>Create & Manage Device Configurations</p>
                    </div>
                    <div class="card">
                        <h3>👥 User Management</h3>
                        <p>Manage Admins & Teachers</p>
                    </div>
                </div>
            </div>
        `;
    },

    loadGfmUI(container) {
        console.log('[ROUTER] Loading GFM UI');
        container.innerHTML = `
            <div class="role-dashboard gfm-dashboard">
                <h1>🌍 GFM Dashboard</h1>
                <div id="device-overview">Loading devices...</div>
            </div>
        `;
        this.fetchGfmData();
    },

    loadTeacherUI(container) {
        console.log('[ROUTER] Loading Teacher UI');
        container.innerHTML = `
            <div class="role-dashboard teacher-dashboard">
                <h1>👨‍🏫 Teacher Dashboard</h1>
                <div id="teacher-classes">Loading classes...</div>
            </div>
        `;
        this.fetchTeacherData();
    },

    async fetchTeacherData() {
        try {
            const token = localStorage.getItem('attendify_token');
            const res = await fetch('/api/teacher/my-classes', {
                headers: { 'Authorization': `Bearer ${token}` }
            });
            const data = await res.json();

            const list = document.getElementById('teacher-classes');
            if (!data.classes || data.classes.length === 0) {
                list.innerHTML = `
                    <div class="card">
                        <h3>No Classes Assigned</h3>
                        <p>Contact your administrator to assign you to a device.</p>
                    </div>`;
                return;
            }

            list.innerHTML = data.classes.map(c => `
                <div class="card">
                    <h3>📱 ${c.deviceId}</h3>
                    <p><strong>Subjects:</strong> ${c.subjects.join(', ')}</p>
                    <div class="status-badge authorized">Authorized</div>
                </div>
            `).join('');
        } catch (error) {
            console.error('Teacher Fetch Error:', error);
            document.getElementById('teacher-classes').innerHTML = '<p class="error">Failed to load classes</p>';
        }
    },

    async fetchGfmData() {
        try {
            const token = localStorage.getItem('attendify_token');
            const res = await fetch('/api/devices/overview', {
                headers: { 'Authorization': `Bearer ${token}` }
            });
            const data = await res.json();

            const list = document.getElementById('device-overview');
            if (data.devices.length === 0) {
                list.innerHTML = '<p>No devices found</p>';
                return;
            }

            list.innerHTML = `
                <table class="data-table">
                    <thead>
                        <tr>
                            <th>Device ID</th>
                            <th>Version</th>
                            <th>Admins</th>
                            <th>Teachers</th>
                            <th>Status</th>
                        </tr>
                    </thead>
                    <tbody>
                        ${data.devices.map(d => `
                            <tr>
                                <td>${d.deviceId}</td>
                                <td>v${d.version}</td>
                                <td>${d.adminCount}</td>
                                <td>${d.teacherCount}</td>
                                <td><span class="status-badge ${d.status.toLowerCase()}">${d.status}</span></td>
                            </tr>
                        `).join('')}
                    </tbody>
                </table>
            `;
        } catch (error) {
            console.error('GFM Fetch Error:', error);
        }
    }
};

// Initialize on load
document.addEventListener('DOMContentLoaded', () => RoleRouter.init());
