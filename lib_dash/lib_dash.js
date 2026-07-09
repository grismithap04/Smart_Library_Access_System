// ========== HOSTEL MAPPING ==========
const HOSTELS = {
    SPS: 'Sandipani Sadan',
    RLV: 'Rajalakshmi Vihar',
    PV: 'Paramandha Vihar',
    KMD: 'Kamadhenu',
    V: 'Vashishta'
};

// ========== DATA ==========
let studentsDB = [];
let activeSessions = [];
let todayLog = [];
let exitTarget = null;
window.uniqueStudentsToday = 0;

// ========== TOAST ==========
function toast(msg, type = 'success') {
    const container = document.getElementById('toasts');
    if (!container) return;

    const icons = {
        success: 'ti-check-circle',
        error: 'ti-alert-circle',
        warning: 'ti-alert-triangle',
        info: 'ti-info-circle'
    };

    const el = document.createElement('div');
    el.className = `toast ${type}`;
    el.innerHTML = `<i class="ti ${icons[type] || icons.info}" style="font-size:15px;flex-shrink:0" aria-hidden="true"></i><span>${msg}</span>`;
    container.appendChild(el);

    setTimeout(() => {
        el.style.transition = 'opacity .28s';
        el.style.opacity = '0';
        setTimeout(() => el.remove(), 300);
    }, 3400);
}

// ========== HELPERS ==========
function getDur(entry) {
    if (!entry) return '--';
    const mins = Math.floor((Date.now() - entry) / 60000);
    if (mins < 1) return '<1m';
    if (mins < 60) return `${mins}m`;
    return `${Math.floor(mins / 60)}h ${mins % 60}m`;
}

function fmtNow() {
    return new Date().toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit' })
        .toUpperCase().replace('AM', ' AM').replace('PM', ' PM');
}

// ========== MODAL FUNCTIONS ==========
function openModal(id) {
    document.getElementById(id).classList.add('open');
}

function closeModal(id) {
    document.getElementById(id).classList.remove('open');
}

// ========== API FUNCTIONS ==========

async function fetchActiveSessions() {
    try {
        const response = await fetch('/api/active');
        if (!response.ok) throw new Error('API error');
        const data = await response.json();

        activeSessions = data.map(session => ({
            id: session.log_id,
            reg: session.reg_id,
            name: session.name,
            hostel_code: session.hostel_code,
            hostel_name: HOSTELS[session.hostel_code] || session.hostel_code,
            room_no: session.room_no,
            entry_time: session.entry_time,
            entry_full: session.entry_full,
            entry_timestamp: new Date(session.entry_full)
        }));

        render();
    } catch (error) {
        console.error('Error fetching active sessions:', error);
        toast('Failed to fetch active sessions', 'error');
    }
}

async function fetchTodayLog() {
    try {
        const response = await fetch('/api/today-log');
        if (!response.ok) throw new Error('API error');
        const data = await response.json();
        
        const logs = data.logs || data;
        
        todayLog = logs.map(log => ({
            id: log.log_id,
            reg: log.reg_id,
            name: log.name,
            hostel_code: log.hostel_code,
            hostel_name: HOSTELS[log.hostel_code] || log.hostel_code,
            room_no: log.room_no,
            entry: log.entry_time,
            exit: log.exit_time || null,
            dur: log.duration || '--',
            type: log.source || 'auto',
            status: log.status || 'IN_LIBRARY',
            entry_full: log.entry_time
        }));

        if (data.unique_students !== undefined) {
            window.uniqueStudentsToday = data.unique_students;
        } else {
            const uniqueRegs = new Set(todayLog.map(l => l.reg));
            window.uniqueStudentsToday = uniqueRegs.size;
        }

        render();
    } catch (error) {
        console.error('Error fetching today log:', error);
        toast('Failed to fetch today log', 'error');
    }
}

async function fetchStudents() {
    try {
        const response = await fetch('/api/students');
        if (!response.ok) throw new Error('API error');
        const data = await response.json();

        studentsDB = data.map(s => ({
            id: s.reg_id,
            reg: s.reg_id,
            name: s.name,
            hostel_code: s.hostel_code,
            hostel_name: HOSTELS[s.hostel_code] || s.hostel_code,
            room_no: s.room_no,
            dept: s.dept,
            year: s.year,
            fp: s.has_fingerprint
        }));

        render();
    } catch (error) {
        console.error('Error fetching students:', error);
        toast('Failed to fetch students', 'error');
    }
}

async function updateAutoExitStatus() {
    try {
        const response = await fetch('/api/auto-exit-status');
        const data = await response.json();
        
        // Update the badge if it exists
        const badge = document.getElementById('autoExitBadge');
        const text = document.getElementById('autoExitText');
        
        if (badge && text) {
            if (data.auto_exit_completed) {
                badge.style.background = 'var(--green-bg)';
                badge.style.color = 'var(--green)';
                badge.style.borderColor = 'rgba(74, 222, 128, 0.15)';
                badge.querySelector('i').className = 'ti ti-check-circle';
                text.textContent = '✅ Auto-exit completed';
            } else if (data.is_after_cutoff) {
                badge.style.background = 'var(--amber-bg)';
                badge.style.color = 'var(--amber)';
                badge.style.borderColor = 'rgba(251, 191, 36, 0.15)';
                badge.querySelector('i').className = 'ti ti-clock-exclamation';
                text.textContent = '⏰ Auto-exit pending';
            } else {
                badge.style.background = 'var(--blue-bg)';
                badge.style.color = 'var(--blue)';
                badge.style.borderColor = 'rgba(108, 140, 255, 0.15)';
                badge.querySelector('i').className = 'ti ti-clock';
                text.textContent = `⏰ Auto-exit at 8:10 PM`;
            }
        }
    } catch (e) {
        // Silent fail
    }
}

// ========== RENDER ==========

function renderTopbar() {
    const now = new Date();
    document.getElementById('topbarDate').textContent =
        now.toLocaleDateString('en-IN', { weekday: 'long', year: 'numeric', month: 'long', day: 'numeric' });

    const h = now.getHours(),
          m = now.getMinutes();
    // Entry cutoff: 6:10 PM (18:10) ✅ FIXED
    const pastCutoff = h > 18 || (h === 18 && m >= 10);
    
    const badge = document.getElementById('cutoffBadge');
    const txt = document.getElementById('cutoffText');
    if (pastCutoff) {
        badge.classList.add('passed');
        badge.querySelector('i').className = 'ti ti-ban';
        txt.textContent = 'Entry cutoff passed (6:10 PM)';  // ✅ FIXED
    } else {
        badge.classList.remove('passed');
        badge.querySelector('i').className = 'ti ti-clock';
        txt.textContent = 'Entry open until 6:10 PM';  // ✅ FIXED
    }

    const updated = now.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit' });
    document.getElementById('lastUpdated').textContent = `Updated ${updated}`;

    updateAutoExitStatus();
}

function renderStats() {
    document.getElementById('statIn').textContent = activeSessions.length;
    document.getElementById('activeBadge').textContent = activeSessions.length;
    
    const uniqueCount = window.uniqueStudentsToday || todayLog.length;
    document.getElementById('statToday').textContent = uniqueCount;
    
    document.getElementById('statAlerts').textContent = 0;
    document.getElementById('statRegistered').textContent = studentsDB.filter(s => s.fp).length;
}

function renderActive() {
    const body = document.getElementById('activeBody');
    if (!activeSessions.length) {
        body.innerHTML = '<tr><td colspan="6" class="empty">No students currently in library</td></tr>';
        return;
    }
    body.innerHTML = activeSessions.map(s => `
        <tr>
            <td><strong>${s.name}</strong></td>
            <td style="color:var(--ink-3);font-size:12px">${s.reg}</td>
            <td>${s.entry_time}</td>
            <td><span class="dur">${getDur(s.entry_timestamp)}</span></td>
            <td><span class="pill pill-green"><span class="pill-dot"></span>In library</span></td>
            <td style="text-align:right">
                <button class="btn btn-ghost btn-sm" onclick="forceExit(${s.id},'${s.name}')">
                    <i class="ti ti-door-exit" aria-hidden="true"></i> Force exit
                </button>
            </td>
        </tr>`).join('');
}

function renderLog() {
    const body = document.getElementById('logBody');
    const badge = document.getElementById('logBadge');
    
    if (!todayLog.length) {
        body.innerHTML = '<tr><td colspan="7" class="empty">No records for today</td></tr>';
        if (badge) badge.textContent = '0';
        return;
    }
    
    if (badge) badge.textContent = todayLog.length;
    
    body.innerHTML = todayLog.map(l => `
        <tr class="${l.status === 'EXITED' ? 'status-exited' : ''}">
            <td><strong>${l.name}</strong></td>
            <td style="color:var(--ink-3);font-size:12px">${l.reg}</td>
            <td>${l.entry}</td>
            <td>${l.exit || '—'}</td>
            <td style="font-size:12px;color:var(--ink-3)">${l.dur || '—'}</td>
            <td><span style="font-size:11px;color:var(--ink-3)">${l.type === 'manual' ? 'Manual' : 'Auto'}</span></td>
            <td>${l.status === 'IN_LIBRARY'
                ? '<span class="pill pill-green"><span class="pill-dot"></span>Inside</span>'
                : '<span class="pill pill-gray"><span class="pill-dot"></span>Exited</span>'
            }</td>
        </tr>`).join('');
}

function render() {
    renderTopbar();
    renderStats();
    renderActive();
    renderLog();
}

// ========== ACTIONS ==========

window.scrollToTop = () => window.scrollTo({ top: 0, behavior: 'smooth' });

window.forceExit = async function(id, name) {
    if (!confirm(`Mark ${name} as exited?`)) return;
    
    try {
        const response = await fetch('/api/force-exit', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ log_id: id })
        });
        
        const data = await response.json();
        
        if (data.ok) {
            toast(`${name} exited successfully`, 'success');
            refreshData();
        } else {
            toast(data.error || 'Failed to record exit', 'error');
        }
    } catch (error) {
        console.error('Error forcing exit:', error);
        toast('Failed to record exit', 'error');
    }
};

window.confirmExit = function() {
    const id = exitTarget;
    if (!id) {
        toast('No exit target found', 'error');
        return;
    }
    const session = activeSessions.find(s => s.id === id);
    if (session) {
        forceExit(id, session.name);
    } else {
        toast('Session not found', 'error');
    }
    closeModal('exitModal');
};

// ========== REFRESH ==========

window.refreshData = async function() {
    toast('Syncing with database...', 'info');
    await Promise.all([
        fetchActiveSessions(),
        fetchTodayLog(),
        fetchStudents()
    ]);
    toast('Sync completed', 'success');
};

window.exportCSV = function() {
    const rows = [
        ['Reg No', 'Name', 'Entry', 'Exit', 'Duration', 'Type', 'Status']
    ];
    todayLog.forEach(l => rows.push([l.reg, l.name, l.entry, l.exit || '', l.dur || '', l.type, l.status]));
    const csv = rows.map(r => r.join(',')).join('\n');
    const a = document.createElement('a');
    a.href = URL.createObjectURL(new Blob([csv], { type: 'text/csv' }));
    a.download = `library_log_${new Date().toISOString().split('T')[0]}.csv`;
    a.click();
    URL.revokeObjectURL(a.href);
    toast('Log exported');
};

// ========== MANUAL ENTRY MODAL ==========

window.showManualModal = function() {
    document.getElementById('m_reg').value = '';
    document.getElementById('m_reason').value = '';
    document.getElementById('m_type').value = 'entry';
    const now = new Date();
    document.getElementById('m_time').value = now.toISOString().slice(0, 16);
    openModal('manualModal');
};

window.saveManual = async function() {
    const reg = document.getElementById('m_reg').value.trim().toUpperCase();
    const type = document.getElementById('m_type').value;
    const reason = document.getElementById('m_reason').value.trim();
    const timeRaw = document.getElementById('m_time').value;
    
    if (!reg) {
        toast('Please enter a register number', 'error');
        return;
    }
    if (!reason) {
        toast('Please enter a reason for manual entry', 'error');
        return;
    }
    if (!timeRaw) {
        toast('Please select date and time', 'error');
        return;
    }
    
    try {
        const response = await fetch('/api/manual-entry', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({
                reg_id: reg,
                type: type,
                reason: reason,
                entry_time: timeRaw
            })
        });
        
        const data = await response.json();
        
        if (response.ok) {
            toast(data.message || 'Manual entry saved successfully', 'success');
            closeModal('manualModal');
            refreshData();
        } else {
            toast(data.error || 'Failed to save manual entry', 'error');
        }
    } catch (error) {
        console.error('Error saving manual entry:', error);
        toast('Failed to save manual entry', 'error');
    }
};

// ========== FINGERPRINT ENROLLMENT ==========

window.showEnrollModal = function() {
    document.getElementById('e_reg').value = '';
    document.getElementById('enrollStudentInfo').style.display = 'none';
    document.getElementById('enrollInstructions').style.display = 'block';
    openModal('enrollModal');
};

document.addEventListener('DOMContentLoaded', function() {
    const eReg = document.getElementById('e_reg');
    if (eReg) {
        eReg.addEventListener('input', function() {
            const reg_id = this.value.trim();
            const infoDiv = document.getElementById('enrollStudentInfo');
            const nameSpan = document.getElementById('e_student_name');
            const detailsSpan = document.getElementById('e_student_details');
            const instructions = document.getElementById('enrollInstructions');
            
            if (reg_id.length >= 8) {
                fetch(`/api/student/${reg_id}`)
                    .then(response => response.json())
                    .then(data => {
                        if (data.name) {
                            infoDiv.style.display = 'block';
                            nameSpan.innerHTML = `<strong>${data.name}</strong>`;
                            detailsSpan.innerHTML = `${data.dept}, Year ${data.year} · ${data.hostel_code} Room ${data.room_no}`;
                            instructions.style.display = 'none';
                        }
                    })
                    .catch(() => {
                        infoDiv.style.display = 'none';
                        instructions.style.display = 'block';
                    });
            } else {
                infoDiv.style.display = 'none';
                instructions.style.display = 'block';
            }
        });
    }
});

window.startEnrollment = function() {
    const reg_id = document.getElementById('e_reg').value.trim();
    
    if (!reg_id) {
        toast('Please enter a register number', 'error');
        return;
    }
    
    toast('Requesting enrollment...', 'info');
    
    fetch('/api/request-enrollment', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({ reg_id: reg_id })
    })
    .then(response => response.json())
    .then(data => {
        if (data.ok) {
            toast('Enrollment requested. Ask student to place finger on sensor.', 'success');
            closeModal('enrollModal');
            checkEnrollmentStatus(reg_id);
        } else {
            toast(data.error || 'Failed to request enrollment', 'error');
        }
    })
    .catch(error => {
        toast('Error requesting enrollment', 'error');
        console.error(error);
    });
};

function checkEnrollmentStatus(reg_id) {
    let attempts = 0;
    const maxAttempts = 60;
    
    const interval = setInterval(() => {
        attempts++;
        
        fetch(`/api/enroll-status/${reg_id}`)
            .then(response => response.json())
            .then(data => {
                if (data.status === 'success') {
                    clearInterval(interval);
                    toast(`✅ Fingerprint enrolled successfully! Slot: ${data.fp_slot}`, 'success');
                    refreshData();
                } else if (attempts >= maxAttempts) {
                    clearInterval(interval);
                    toast('Enrollment timed out. Please try again.', 'warning');
                }
            })
            .catch(() => {});
    }, 1000);
}

// ========== AUTO-EXIT (8:10 PM) ==========

window.autoExit = async function() {
    // Check if auto-exit already ran today
    try {
        const statusResponse = await fetch('/api/auto-exit-status');
        const statusData = await statusResponse.json();
        
        if (statusData.auto_exit_completed) {
            toast('✅ Auto-exit already completed today at 8:10 PM', 'info');
            if (!confirm('Auto-exit already ran today. Force it again anyway?')) {
                return;
            }
        }
    } catch (e) {
        // Silent fail - continue with confirmation
    }
    
    // First confirmation
    const confirmMsg = '⚠️ FORCE AUTO-EXIT\n\n' +
                       'This will mark ALL remaining students as exited.\n' +
                       'Use this for:\n' +
                       '• Emergency closures\n' +
                       '• Testing\n' +
                       '• Early closing\n\n' +
                       'Students will be logged as "manual-auto-exit".\n\n' +
                       'Are you sure?';
    
    if (!confirm(confirmMsg)) return;
    
    // Second confirmation for safety
    if (!confirm('🔄 FINAL CONFIRMATION: Force-exit everyone now?')) return;
    
    try {
        toast('⏳ Force auto-exit in progress...', 'info');
        
        const response = await fetch('/api/auto-exit', {
            method: 'POST',
            headers: {
                'Content-Type': 'application/json'
            },
            body: JSON.stringify({ force: true })
        });
        
        const data = await response.json();
        
        if (data.ok) {
            if (data.exited_count === 0) {
                toast('ℹ️ No students were in the library', 'info');
            } else {
                toast(`✅ Force auto-exited ${data.exited_count} students`, 'success');
                if (data.students && data.students.length > 0) {
                    const names = data.students.map(s => s.name).join(', ');
                    toast(`📋 Exited: ${names}`, 'info');
                }
                toast(`📌 Source: ${data.source || 'manual-auto-exit'}`, 'info');
            }
            refreshData();
        } else {
            toast(data.error || 'Failed to force auto-exit', 'error');
        }
    } catch (error) {
        console.error('Error in auto-exit:', error);
        toast('Failed to force auto-exit students', 'error');
    }
};

// ========== POLLING ==========

let poll;

function startPoll() {
    poll = setInterval(refreshData, 15000);
}

function stopPoll() {
    clearInterval(poll);
}
document.addEventListener('visibilitychange', () =>
    document.hidden ? stopPoll() : startPoll()
);

// ========== INIT ==========

render();
refreshData();
startPoll();