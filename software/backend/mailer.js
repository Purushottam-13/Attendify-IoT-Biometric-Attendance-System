const nodemailer = require('nodemailer');
const https = require('https');
require('dotenv').config();

// Sanitize Brevo API key to remove any accidental whitespace or newlines from copy-paste
if (process.env.BREVO_API_KEY) {
    process.env.BREVO_API_KEY = String(process.env.BREVO_API_KEY).trim().replace(/[\r\n\s]+/g, '');
}

// Create reusable transporter object using SMTP transport
const transporter = nodemailer.createTransport({
    service: 'gmail',
    auth: {
        user: process.env.SMTP_EMAIL,
        pass: process.env.SMTP_APP_PASSWORD
    }
});


// Send transactional email via Brevo REST API (HTTP)
function sendBrevoEmail({ toEmail, toName, subject, html, attachments = [] }) {
    return new Promise((resolve) => {
        if (!process.env.BREVO_API_KEY) {
            console.warn('[MAILER] BREVO_API_KEY is missing. Skipping email.');
            return resolve(false);
        }

        // Use BREVO_SENDER_EMAIL if set, otherwise fall back to the Brevo account's verified sender
        const brevoSender = process.env.BREVO_SENDER_EMAIL || 'adminattendify@gmail.com';
        const payload = {
            sender: {
                name: "Attendify System",
                email: brevoSender
            },
            to: [{
                email: toEmail,
                name: toName || toEmail.split('@')[0]
            }],
            subject: subject,
            htmlContent: html
        };

        if (attachments && attachments.length > 0) {
            payload.attachment = attachments.map(att => {
                if (att.content) {
                    return {
                        name: att.filename || att.name,
                        content: att.content
                    };
                } else if (att.path) {
                    try {
                        const fs = require('fs');
                        const fileContent = fs.readFileSync(att.path).toString('base64');
                        return {
                            name: att.filename || att.name || 'attachment',
                            content: fileContent
                        };
                    } catch (e) {
                        console.error('[MAILER] Failed to read attachment path:', att.path, e);
                        return null;
                    }
                }
                return null;
            }).filter(Boolean);
        }

        const dataStr = JSON.stringify(payload);

        const options = {
            hostname: 'api.brevo.com',
            port: 443,
            path: '/v3/smtp/email',
            method: 'POST',
            headers: {
                'accept': 'application/json',
                'api-key': process.env.BREVO_API_KEY,
                'content-type': 'application/json',
                'content-length': Buffer.byteLength(dataStr)
            }
        };

        const req = https.request(options, (res) => {
            let body = '';
            res.on('data', (chunk) => body += chunk);
            res.on('end', () => {
                if (res.statusCode >= 200 && res.statusCode < 300) {
                    try {
                        const resData = JSON.parse(body);
                        console.log(`[MAILER] Brevo email sent successfully: ${resData.messageId}`);
                        resolve(true);
                    } catch (e) {
                        console.log(`[MAILER] Brevo email sent (raw response): ${body}`);
                        resolve(true);
                    }
                } else {
                    console.error(`[MAILER] Brevo API responded with status ${res.statusCode}: ${body}`);
                    resolve(false);
                }
            });
        });

        req.on('error', (err) => {
            console.error('[MAILER] HTTPS request error:', err);
            resolve(false);
        });

        req.write(dataStr);
        req.end();
    });
}

// Override transporter.sendMail to route through Brevo API if configured
const originalSendMail = transporter.sendMail.bind(transporter);
transporter.sendMail = async function(mailOptions) {
    if (process.env.BREVO_API_KEY) {
        const toEmail = mailOptions.to;
        const toName = typeof toEmail === 'string' ? toEmail.split('@')[0] : '';
        const success = await sendBrevoEmail({
            toEmail: toEmail,
            toName: toName,
            subject: mailOptions.subject,
            html: mailOptions.html,
            attachments: mailOptions.attachments
        });
        if (!success) {
            throw new Error('Brevo API send failed');
        }
        return { messageId: 'BrevoMailApi' };
    } else {
        return await originalSendMail(mailOptions);
    }
};

// If using Brevo, provide a dummy SMTP password to bypass internal check-blocks
if (process.env.BREVO_API_KEY && !process.env.SMTP_APP_PASSWORD) {
    process.env.SMTP_APP_PASSWORD = 'brevo-api-active';
}


/**
 * Sends a Welcome email with a Password Reset link
 */
async function sendWelcomeEmail(name, email, password) {
    if (!process.env.SMTP_EMAIL || !process.env.SMTP_APP_PASSWORD) {
        console.warn('[MAILER] SMTP credentials missing in .env. Skipping welcome email.');
        return false;
    }

    const loginUrl = process.env.PUBLIC_BASE_URL 
        ? `${process.env.PUBLIC_BASE_URL.replace(/\/$/, '')}/login.html`
        : 'http://localhost:3003/login.html';

    const htmlContent = `
    <div style="font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 600px; margin: 0 auto; background-color: #ffffff; border-radius: 16px; overflow: hidden; box-shadow: 0 8px 30px rgba(0,0,0,0.08); border: 1px solid #e2e8f0;">
        <div style="background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%); padding: 36px 30px; text-align: center;">
            <div style="display: inline-block; background: rgba(99,102,241,0.15); border: 1px solid rgba(99,102,241,0.3); border-radius: 12px; padding: 10px 14px; margin-bottom: 16px;">
                <span style="font-size: 22px; color: #818cf8;">&#128274;</span>
            </div>
            <h1 style="color: #ffffff; margin: 0 0 6px 0; font-size: 26px; font-weight: 700; letter-spacing: -0.02em;">Welcome to Attendify</h1>
            <p style="color: #94a3b8; margin: 0; font-size: 14px;">Biometric Attendance Management System</p>
        </div>
        <div style="padding: 36px 30px; color: #374151;">
            <p style="font-size: 16px; line-height: 1.6; margin-bottom: 20px;">Hi <strong>${name}</strong>,</p>
            <p style="font-size: 15px; line-height: 1.7; margin-bottom: 24px; color: #475569;">Your student account has been successfully created and approved. You can now log in to the Student Dashboard to track your biometric attendance records in real-time.</p>
            
            <div style="background: linear-gradient(135deg, #eef2ff 0%, #e0e7ff 100%); padding: 28px; border-radius: 12px; margin-bottom: 28px; border: 1px solid #c7d2fe;">
                <p style="font-size: 12px; color: #6366f1; margin: 0 0 16px 0; text-transform: uppercase; font-weight: 800; letter-spacing: 0.08em; text-align: center;">&#128272; Your Login Credentials</p>
                <div style="background: #ffffff; border-radius: 8px; padding: 16px; margin-bottom: 12px; border: 1px solid #e0e7ff;">
                    <div style="font-size: 12px; color: #6b7280; text-transform: uppercase; font-weight: 600; letter-spacing: 0.04em; margin-bottom: 4px;">Email / Username</div>
                    <div style="font-size: 15px; font-family: monospace; color: #1e293b; font-weight: 600;">${email}</div>
                </div>
                <div style="background: #ffffff; border-radius: 8px; padding: 16px; border: 1px solid #e0e7ff;">
                    <div style="font-size: 12px; color: #6b7280; text-transform: uppercase; font-weight: 600; letter-spacing: 0.04em; margin-bottom: 4px;">Temporary Password</div>
                    <div style="font-size: 18px; font-family: monospace; color: #4338ca; font-weight: 700; letter-spacing: 1px;">${password}</div>
                </div>
                <div style="text-align: center; margin-top: 20px;">
                    <a href="${loginUrl}" style="display: inline-block; background: linear-gradient(135deg, #4f46e5 0%, #6366f1 100%); color: #ffffff; padding: 14px 32px; text-decoration: none; border-radius: 8px; font-weight: 700; font-size: 15px; box-shadow: 0 4px 12px rgba(79,70,229,0.3);">Log In to Dashboard &rarr;</a>
                </div>
            </div>
            
            <div style="background: #fef2f2; border-left: 4px solid #ef4444; padding: 14px 16px; border-radius: 0 8px 8px 0; margin-bottom: 20px;">
                <p style="font-size: 13px; color: #991b1b; line-height: 1.5; margin: 0; font-weight: 600;">&#9888; Security Reminder: Please change your password immediately after your first login via Profile settings.</p>
            </div>
            <p style="font-size: 14px; color: #64748b; line-height: 1.5; margin-bottom: 0;">If you have any questions, please reach out to your GFM (Guardian Faculty Member).<br>Welcome aboard!</p>
        </div>
        <div style="background: #f8fafc; padding: 24px; text-align: center; border-top: 1px solid #e2e8f0;">
            <p style="font-size: 13px; color: #94a3b8; margin: 0; font-weight: 500;">Attendify &mdash; Biometric Attendance System</p>
            <p style="font-size: 11px; color: #cbd5e1; margin: 6px 0 0 0;">&copy; ${new Date().getFullYear()} Attendify. All rights reserved.</p>
        </div>
    </div>
    `;

    const mailOptions = {
        from: `"Attendify System" <${process.env.SMTP_EMAIL}>`,
        to: email,
        subject: 'Welcome to Attendify! Your Account is Ready',
        html: htmlContent
    };

    try {
        const info = await transporter.sendMail(mailOptions);
        console.log(`[MAILER] Welcome email sent to ${email}: ${info.messageId}`);
        return true;
    } catch (error) {
        console.error(`[MAILER] Failed to send email to ${email}:`, error);
        return false;
    }
}

/**
 * Sends a Post-Session Summary to the Teacher
 */
async function sendSessionSummaryEmail(teacherName, teacherEmail, subject, studentsPresent, baseUrl) {
    if (!process.env.SMTP_EMAIL || !process.env.SMTP_APP_PASSWORD) return false;

    const presentCount = studentsPresent.length;

    // Resolve dashboard link: prefer explicit baseUrl, then PUBLIC_BASE_URL, then localhost fallback
    const base = (baseUrl && String(baseUrl).trim()) || process.env.PUBLIC_BASE_URL || 'http://localhost:3003';
    const dashboardLink = `${String(base).replace(/\/$/, '')}/teacher/dashboard`;

    const htmlContent = `
    <div style="font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 600px; margin: 0 auto; background-color: #ffffff; border-radius: 16px; overflow: hidden; box-shadow: 0 8px 30px rgba(0,0,0,0.08); border: 1px solid #e2e8f0;">
        <div style="background: linear-gradient(135deg, #0f172a 0%, #1e293b 100%); padding: 36px 30px; text-align: center;">
            <div style="display: inline-block; background: rgba(16,185,129,0.15); border: 1px solid rgba(16,185,129,0.3); border-radius: 12px; padding: 10px 14px; margin-bottom: 16px;">
                <span style="font-size: 22px; color: #34d399;">&#9989;</span>
            </div>
            <h1 style="color: #ffffff; margin: 0 0 6px 0; font-size: 24px; font-weight: 700;">Session Complete</h1>
            <p style="color: #94a3b8; margin: 0; font-size: 14px;">Attendify &mdash; Attendance Synced Successfully</p>
        </div>
        <div style="padding: 36px 30px; color: #374151;">
            <p style="font-size: 16px; margin-bottom: 20px;">Hi <strong>${teacherName}</strong>,</p>
            <p style="font-size: 15px; margin-bottom: 28px; color: #475569; line-height: 1.7;">Your attendance session for <strong style="color: #1e293b;">${subject}</strong> has been successfully processed and synced to the cloud.</p>
            
            <div style="background: linear-gradient(135deg, #ecfdf5 0%, #d1fae5 100%); border: 1px solid #a7f3d0; padding: 28px; border-radius: 12px; text-align: center; margin-bottom: 28px;">
                <p style="font-size: 12px; color: #047857; margin: 0 0 8px 0; text-transform: uppercase; font-weight: 700; letter-spacing: 0.06em;">Students Present</p>
                <p style="font-size: 42px; font-weight: 800; color: #059669; margin: 0; line-height: 1;">${presentCount}</p>
            </div>

            <div style="text-align: center;">
                <a href="${dashboardLink}" style="display: inline-block; background: linear-gradient(135deg, #059669 0%, #10b981 100%); color: #ffffff; padding: 14px 32px; text-decoration: none; border-radius: 8px; font-weight: 700; font-size: 15px; box-shadow: 0 4px 12px rgba(5,150,105,0.3);">View Full Report &rarr;</a>
            </div>
        </div>
        <div style="background: #f8fafc; padding: 24px; text-align: center; border-top: 1px solid #e2e8f0;">
            <p style="font-size: 13px; color: #94a3b8; margin: 0; font-weight: 500;">Attendify &mdash; Biometric Attendance System</p>
            <p style="font-size: 11px; color: #cbd5e1; margin: 6px 0 0 0;">&copy; ${new Date().getFullYear()} Attendify. All rights reserved.</p>
        </div>
    </div>
    `;

    const mailOptions = {
        from: `"Attendify System" <${process.env.SMTP_EMAIL}>`,
        to: teacherEmail,
        subject: `[Attendify] Session Summary: ${subject}`,
        html: htmlContent
    };

    try {
        const info = await transporter.sendMail(mailOptions);
        console.log(`[MAILER] Session summary sent to ${teacherEmail}: ${info.messageId}`);
        return true;
    } catch (error) {
        console.error(`[MAILER] Failed to send summary to ${teacherEmail}:`, error);
        return false;
    }
}

/**
 * Sends a Password Reset email with a temporary password
 */
async function sendPasswordResetEmail(email, temporaryPassword) {
    if (!process.env.SMTP_EMAIL || !process.env.SMTP_APP_PASSWORD) {
        console.warn('[MAILER] SMTP credentials missing in .env. Skipping password reset email.');
        return false;
    }

    const htmlContent = `
    <div style="font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 600px; margin: 0 auto; background-color: #f9fafb; border-radius: 12px; overflow: hidden; box-shadow: 0 4px 6px rgba(0,0,0,0.05);">
        <div style="background-color: #3b82f6; padding: 30px; text-align: center;">
            <h1 style="color: white; margin: 0; font-size: 28px; font-weight: 600;">Attendify Password Reset</h1>
        </div>
        <div style="padding: 40px 30px; background-color: white;">
            <p style="font-size: 16px; color: #4b5563; line-height: 1.6; margin-top: 0;">Hello,</p>
            <p style="font-size: 16px; color: #4b5563; line-height: 1.6;">Your password has been successfully reset. Please use the temporary password below to log in to your account.</p>
            
            <div style="margin: 30px 0; padding: 20px; background-color: #f3f4f6; border-radius: 8px; text-align: center; border: 1px dashed #cbd5e1;">
                <p style="font-size: 14px; color: #64748b; margin-top: 0; margin-bottom: 8px;">TEMPORARY PASSWORD</p>
                <div style="font-family: monospace; font-size: 24px; font-weight: bold; color: #0f172a; letter-spacing: 2px;">
                    ${temporaryPassword}
                </div>
            </div>
            
            <p style="font-size: 16px; color: #4b5563; line-height: 1.6;"><strong>Important:</strong> Please log in immediately and change your password from the dashboard settings for security.</p>
        </div>
        <div style="background-color: #f1f5f9; padding: 20px; text-align: center; border-top: 1px solid #e2e8f0;">
            <p style="font-size: 13px; color: #94a3b8; margin: 0;">This is an automated message. Please do not reply to this email.</p>
        </div>
    </div>
    `;

    const mailOptions = {
        from: `"Attendify System" <${process.env.SMTP_EMAIL}>`,
        to: email,
        subject: 'Your Attendify Password Has Been Reset',
        html: htmlContent
    };

    try {
        const info = await transporter.sendMail(mailOptions);
        console.log(`[MAILER] Password reset email sent to ${email}: ${info.messageId}`);
        return true;
    } catch (error) {
        console.error(`[MAILER] Failed to send password reset email to ${email}:`, error);
        return false;
    }
}

async function sendDailySummaryEmail(teacherName, teacherEmail, dateString, sessionsToday, baseUrl) {
    if (!process.env.SMTP_EMAIL || !process.env.SMTP_APP_PASSWORD) {
        console.warn('[MAILER] SMTP credentials missing in .env. Skipping daily summary email.');
        return false;
    }

    const base = (baseUrl && String(baseUrl).trim()) || process.env.PUBLIC_BASE_URL || 'http://localhost:3003';
    const dashboardLink = `${String(base).replace(/\/$/, '')}/teacher/dashboard`;

    const path = require('path');
    const fs = require('fs');
    const attachments = [];
    let bannerSrc = 'https://placehold.co/600x200/4f46e5/ffffff?text=Attendify'; // Online fallback banner
    
    const bannerPath = path.join(__dirname, 'attendify_email_banner.png');
    if (fs.existsSync(bannerPath)) {
        attachments.push({
            filename: 'attendify_email_banner.png',
            path: bannerPath,
            cid: 'attendify_banner'
        });
        bannerSrc = 'cid:attendify_banner';
    }

    // Build session table rows
    let tableRows = '';
    sessionsToday.forEach(s => {
        tableRows += `
        <tr style="border-bottom: 1px solid #e2e8f0;">
            <td style="padding: 12px 16px; font-weight: 600; color: #1e293b;">${s.subject}</td>
            <td style="padding: 12px 16px; color: #64748b;">${s.time}</td>
            <td style="padding: 12px 16px; text-align: right; font-weight: 700; color: #4f46e5;">${s.presentCount} present</td>
        </tr>
        `;
    });

    const htmlContent = `
    <div style="font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 600px; margin: 0 auto; background-color: #f8fafc; border-radius: 16px; overflow: hidden; box-shadow: 0 10px 25px -5px rgba(0,0,0,0.05); border: 1px solid #e2e8f0;">
        <div style="text-align: center; background-color: #0f172a;">
            <img src="${bannerSrc}" alt="Attendify Header Banner" style="width: 100%; max-width: 600px; display: block;" />
        </div>
        <div style="padding: 32px 24px; background-color: #ffffff;">
            <h2 style="color: #0f172a; margin-top: 0; font-size: 20px; font-weight: 700; border-bottom: 2px solid #f1f5f9; padding-bottom: 12px;">
                Daily Attendance Summary
            </h2>
            <p style="font-size: 15px; color: #475569; line-height: 1.6;">
                Hi <strong>${teacherName}</strong>,<br/>
                Here is your daily attendance summary report for <strong>${dateString}</strong>.
            </p>
            
            <table style="width: 100%; border-collapse: collapse; margin: 24px 0; font-size: 14px; text-align: left; background-color: #f8fafc; border-radius: 8px; overflow: hidden;">
                <thead>
                    <tr style="background-color: #e2e8f0; color: #475569; font-weight: 600;">
                        <th style="padding: 12px 16px;">Subject</th>
                        <th style="padding: 12px 16px;">Time</th>
                        <th style="padding: 12px 16px; text-align: right;">Attendance</th>
                    </tr>
                </thead>
                <tbody>
                    ${tableRows}
                </tbody>
            </table>

            <div style="margin-top: 32px; text-align: center;">
                <a href="${dashboardLink}" style="display: inline-block; background-color: #6366f1; color: #ffffff; padding: 12px 28px; text-decoration: none; border-radius: 8px; font-weight: 600; font-size: 15px; box-shadow: 0 4px 6px -1px rgba(99, 102, 241, 0.2);">
                    Open Teacher Dashboard
                </a>
            </div>
        </div>
        <div style="background-color: #f1f5f9; padding: 20px; text-align: center; border-top: 1px solid #e2e8f0;">
            <p style="font-size: 12px; color: #94a3b8; margin: 0;">&copy; ${new Date().getFullYear()} Attendify College System. All rights reserved.</p>
        </div>
    </div>
    `;

    const mailOptions = {
        from: `"Attendify System" <${process.env.SMTP_EMAIL}>`,
        to: teacherEmail,
        subject: `[Attendify] Daily Attendance Report: ${dateString}`,
        html: htmlContent,
        attachments: attachments
    };

    try {
        const info = await transporter.sendMail(mailOptions);
        console.log(`[MAILER] Daily summary email sent to ${teacherEmail}: ${info.messageId}`);
        return true;
    } catch (error) {
        console.error(`[MAILER] Failed to send daily summary email to ${teacherEmail}:`, error);
        return false;
    }
}

async function sendLowAttendanceWarningEmail(studentName, studentEmail, subjectStats, threshold = 75) {
    if (!process.env.SMTP_EMAIL || !process.env.SMTP_APP_PASSWORD) {
        console.warn('[MAILER] SMTP credentials missing in .env. Skipping warning email.');
        return false;
    }

    let rowsHtml = '';
    subjectStats.forEach(stat => {
        const percentColor = stat.percentage < threshold ? '#b91c1c' : '#15803d';
        const progressWidth = Math.min(100, Math.max(0, stat.percentage));
        rowsHtml += `
        <tr style="border-bottom: 1px solid #e2e8f0;">
            <td style="padding: 12px 16px; font-weight: 600; color: #1e293b;">${stat.subject}</td>
            <td style="padding: 12px 16px; font-weight: 700; color: ${percentColor};">${stat.percentage}%</td>
            <td style="padding: 12px 16px; color: #64748b;">
                <div style="background-color: #e2e8f0; border-radius: 9999px; height: 8px; width: 100px; overflow: hidden; display: inline-block; vertical-align: middle;">
                    <div style="background-color: ${stat.percentage < threshold ? '#ef4444' : '#10b981'}; height: 100%; width: ${progressWidth}%;"></div>
                </div>
            </td>
        </tr>
        `;
    });

    const htmlContent = `
    <div style="font-family: 'Segoe UI', Tahoma, Geneva, Verdana, sans-serif; max-width: 600px; margin: 0 auto; background-color: #ffffff; border-radius: 16px; overflow: hidden; box-shadow: 0 8px 30px rgba(0,0,0,0.08); border: 1px solid #fecaca;">
        <div style="background: linear-gradient(135deg, #7f1d1d 0%, #991b1b 50%, #b91c1c 100%); padding: 36px 30px; text-align: center;">
            <div style="display: inline-block; background: rgba(255,255,255,0.12); border: 1px solid rgba(255,255,255,0.2); border-radius: 12px; padding: 10px 14px; margin-bottom: 16px;">
                <span style="font-size: 24px;">&#9888;</span>
            </div>
            <h1 style="color: #ffffff; margin: 0 0 6px 0; font-size: 24px; font-weight: 700; letter-spacing: -0.02em;">Attendance Warning</h1>
            <p style="color: #fecaca; margin: 0; font-size: 14px;">Attendify &mdash; Academic Standards Alert</p>
        </div>
        <div style="background: linear-gradient(180deg, #fef2f2 0%, #ffffff 40%); padding: 36px 30px; color: #374151;">
            <div style="background: #fff7ed; border: 1px solid #fed7aa; border-radius: 10px; padding: 16px; margin-bottom: 24px; text-align: center;">
                <p style="font-size: 13px; color: #9a3412; margin: 0; font-weight: 700;">&#128680; Your attendance has fallen below the required <strong>${threshold}%</strong> threshold</p>
            </div>
            
            <p style="font-size: 16px; line-height: 1.6; margin-bottom: 20px;">Dear <strong>${studentName}</strong>,</p>
            <p style="font-size: 15px; line-height: 1.7; margin-bottom: 24px; color: #475569;">This is a formal notice that your attendance in the following course(s) is below the minimum academic requirement. Immediate attention is required.</p>
            
            <table style="width: 100%; border-collapse: collapse; margin-bottom: 24px; font-size: 14px; border: 1px solid #e2e8f0; border-radius: 8px; overflow: hidden;">
                <thead>
                    <tr style="background: linear-gradient(135deg, #1e293b 0%, #334155 100%); text-align: left;">
                        <th style="padding: 14px 16px; color: #e2e8f0; font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: 0.05em;">Course</th>
                        <th style="padding: 14px 16px; color: #e2e8f0; font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: 0.05em;">Attendance</th>
                        <th style="padding: 14px 16px; color: #e2e8f0; font-weight: 600; font-size: 12px; text-transform: uppercase; letter-spacing: 0.05em;">Status</th>
                    </tr>
                </thead>
                <tbody>
                    ${rowsHtml}
                </tbody>
            </table>
            
            <div style="background: #fef2f2; border: 1px solid #fecaca; border-left: 5px solid #dc2626; padding: 20px; border-radius: 0 10px 10px 0; margin-bottom: 24px;">
                <p style="font-size: 14px; color: #991b1b; margin: 0 0 8px 0; font-weight: 700;">&#9888; Immediate Action Required</p>
                <p style="font-size: 14px; color: #7f1d1d; margin: 0; line-height: 1.6;">Contact your course instructor or GFM immediately. If absent due to medical reasons, submit a leave application with certificates via your Student Dashboard.</p>
            </div>
            
            <p style="font-size: 13px; color: #94a3b8; line-height: 1.5; margin-bottom: 0; font-style: italic;">This is an automated notice from the Attendify Attendance Monitoring System.<br>Academic Affairs Office</p>
        </div>
        <div style="background: #f8fafc; padding: 24px; text-align: center; border-top: 1px solid #e2e8f0;">
            <p style="font-size: 13px; color: #94a3b8; margin: 0; font-weight: 500;">Attendify &mdash; Biometric Attendance System</p>
            <p style="font-size: 11px; color: #cbd5e1; margin: 6px 0 0 0;">&copy; ${new Date().getFullYear()} Attendify. All rights reserved.</p>
        </div>
    </div>
    `;


    const mailOptions = {
        from: `"Attendify System" <${process.env.SMTP_EMAIL}>`,
        to: studentEmail,
        subject: `[ATTENDIFY WARNING] Attendance Alert: Below ${threshold}% Threshold`,
        html: htmlContent
    };

    try {
        const info = await transporter.sendMail(mailOptions);
        console.log(`[MAILER] Low attendance warning sent to ${studentEmail}: ${info.messageId}`);
        return true;
    } catch (error) {
        console.error(`[MAILER] Failed to send warning to ${studentEmail}:`, error);
        return false;
    }
}

module.exports = {
    sendWelcomeEmail,
    sendSessionSummaryEmail,
    sendPasswordResetEmail,
    sendDailySummaryEmail,
    sendLowAttendanceWarningEmail
};
