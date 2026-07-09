// ========== HOSTEL MAPPING ==========
const HOSTELS = {
    SPS: 'Sandipani Sadan',
    RLV: 'Rajalakshmi Vihar',
    PV: 'Paramandha Vihar',
    KMD: 'Kamadhenu',
    V: 'Vashishta'
};

const WARDENS = {
    SPS: 'Prof. Verma',
    RLV: 'Dr. Nair',
    PV: 'Dr. Sharma',
    KMD: 'Mr. Patil',
    V: 'Dr. Gupta'
};

// ========== STATE ==========
let currentHostel = null;
let activeStudents = [];
let returnedStudents = [];

// ========== TOAST ==========
function toast(msg, type) {
    type = type || 'success';
    var container = document.getElementById('toasts');
    if (!container) return;

    var icons = {
        success: 'ti-check-circle',
        error: 'ti-alert-circle',
        warning: 'ti-alert-triangle',
        info: 'ti-info-circle'
    };

    var el = document.createElement('div');
    el.className = 'toast ' + type;
    el.innerHTML = '<i class="ti ' + (icons[type] || icons.info) + '" style="font-size:15px;flex-shrink:0"></i><span>' + msg + '</span>';
    container.appendChild(el);

    setTimeout(function() {
        el.style.transition = 'opacity .28s';
        el.style.opacity = '0';
        setTimeout(function() { el.remove(); }, 300);
    }, 3400);
}

// ========== HELPERS ==========
function getInitials(name) {
    if (!name) return '?';
    return name.split(' ').map(function(w) { return w[0]; }).join('').slice(0, 2).toUpperCase();
}

function getDuration(entryTimeStr) {
    if (!entryTimeStr) return '--';
    var parts = entryTimeStr.split(':');
    if (parts.length < 2) return '--';
    var now = new Date();
    var entry = new Date();
    entry.setHours(parseInt(parts[0], 10), parseInt(parts[1], 10), parseInt(parts[2] || 0, 10), 0);
    var mins = Math.floor((now - entry) / 60000);
    if (mins < 0) return '--';
    if (mins < 1) return '<1m';
    if (mins < 60) return mins + 'm';
    return Math.floor(mins / 60) + 'h ' + (mins % 60) + 'm';
}

function calcDuration(entryStr, exitStr) {
    if (!entryStr || !exitStr) return '--';

    function toMins(s) {
        var p = s.split(':');
        return parseInt(p[0], 10) * 60 + parseInt(p[1], 10);
    }
    var diff = toMins(exitStr) - toMins(entryStr);
    if (diff < 0) return '--';
    var h = Math.floor(diff / 60),
        m = diff % 60;
    return h > 0 ? h + 'h ' + m + 'm' : m + 'm';
}

function isRollCallTime() {
    var now = new Date();
    var h = now.getHours();
    var m = now.getMinutes();
    // 5:30 PM – 6:40 PM
    return (h === 17 && m >= 30) || (h === 18 && m <= 40);
}

// ========== API ==========
function fetchActiveStudents() {
    if (!currentHostel) return;
    var url = '/api/hostel/' + currentHostel + '/active';
    console.log('GET', url);

    fetch(url)
        .then(function(r) {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.json();
        })
        .then(function(data) {
            console.log('active →', data);
            activeStudents = data;
            renderActiveSessions();
            renderStats();
        })
        .catch(function(err) {
            console.error('fetchActive failed:', err);
            toast('Could not load active students', 'error');
        });
}

function fetchReturnedStudents() {
    if (!currentHostel) return;
    var url = '/api/hostel/' + currentHostel + '/returned';
    console.log('GET', url);

    fetch(url)
        .then(function(r) {
            if (!r.ok) throw new Error('HTTP ' + r.status);
            return r.json();
        })
        .then(function(data) {
            console.log('returned →', data);
            returnedStudents = data;
            renderExitLogs();
            renderStats();
        })
        .catch(function(err) {
            console.error('fetchReturned failed:', err);
            toast('Could not load returned students', 'error');
        });
}

// ========== LOGIN ==========
window.login = function(hostelCode) {
    if (!hostelCode) return;
    console.log('login:', hostelCode);

    currentHostel = hostelCode;

    // Hide login, show dashboard
    document.getElementById('loginScreen').style.display = 'none';
    document.getElementById('mainPage').classList.add('show');

    // Update warden identity bar
    var wardenName = WARDENS[hostelCode] || 'Hostel Warden';
    document.getElementById('wAvatar').innerText = getInitials(wardenName);
    document.getElementById('wName').innerText = wardenName;
    document.getElementById('wHostel').innerText = hostelCode + ' · ' + HOSTELS[hostelCode];

    // Update topbar
    document.getElementById('topbarName').innerText = HOSTELS[hostelCode] + ' — Warden View';
    document.getElementById('topbarSub').innerText = wardenName + ' · Library Access Monitor';

    // Fetch data
    fetchActiveStudents();
    fetchReturnedStudents();

    toast('Logged in — ' + HOSTELS[hostelCode], 'success');
};

window.logout = function() {
    currentHostel = null;
    activeStudents = [];
    returnedStudents = [];

    document.getElementById('loginScreen').style.display = 'flex';
    document.getElementById('mainPage').classList.remove('show');
};

// ========== RENDER: STATS ==========
function renderStats() {
    document.getElementById('activeBadge').innerText = activeStudents.length;
    document.getElementById('returnedBadge').innerText = returnedStudents.length;
    document.getElementById('wCount').innerText = activeStudents.length;
    document.getElementById('wCountLbl').innerText = activeStudents.length === 1 ?
        'student in library now' :
        'students in library now';
}

// ========== RENDER: ACTIVE SESSIONS ==========
function renderActiveSessions() {
    var tbody = document.getElementById('activeSessionsBody');
    if (!tbody) return;

    if (!activeStudents || activeStudents.length === 0) {
        tbody.innerHTML = '<tr><td colspan="5" class="empty">No students from your hostel are currently in the library</td></tr>';
        return;
    }

    var html = '';
    for (var i = 0; i < activeStudents.length; i++) {
        var s = activeStudents[i];
        var dur = getDuration(s.entry_time);
        html +=
            '<tr>' +
            '<td><strong>' + s.name + '</strong></td>' +
            '<td>' + (s.room_no || '--') + '</td>' +
            '<td>' + (s.entry_time || '--') + '</td>' +
            '<td><span style="background:rgba(251,191,36,0.1);color:var(--amber);padding:3px 10px;border-radius:99px;font-size:12px;font-weight:500;">' + dur + '</span></td>' +
            '<td><span class="status-in">In Library — No late mark</span></td>' +
            '</tr>';
    }
    tbody.innerHTML = html;

    // Update "Updated" meta label
    var now = new Date();
    var t = now.toLocaleTimeString('en-IN', { hour: '2-digit', minute: '2-digit' });
    document.getElementById('activeUpdated').textContent = 'Updated ' + t;
}

// ========== RENDER: RETURNED / EXIT LOGS ==========
function renderExitLogs() {
    var tbody = document.getElementById('exitLogsBody');
    if (!tbody) return;

    if (!returnedStudents || returnedStudents.length === 0) {
        tbody.innerHTML = '<tr><td colspan="7" class="empty">No students have returned yet today</td></tr>';
        return;
    }

    var html = '';
    for (var i = 0; i < returnedStudents.length; i++) {
        var l = returnedStudents[i];
        html +=
            '<tr>' +
            '<td><strong>' + l.name + '</strong></td>' +
            '<td>' + (l.room_no || '--') + '</td>' +
            '<td>' + (l.entry_time || '--') + '</td>' +
            '<td>' + (l.exit_time || '--') + '</td>' +
            '<td style="font-size:12px;color:var(--ink-3);">' + calcDuration(l.entry_time, l.exit_time) + '</td>' +
            '<td>' + (l.hostel_return || l.exit_time || '--') + '</td>' +
            '<td><span class="status-returned"> Returned</span></td>' +
            '</tr>';
    }
    tbody.innerHTML = html;
}

// ========== RENDER: ROLL CALL BANNER ==========
function renderRollCallBanner() {
    var banner = document.getElementById('rcBanner');
    if (!banner) return;
    banner.style.display = isRollCallTime() ? 'flex' : 'none';
}

// ========== FULL REFRESH ==========
function refreshAll() {
    if (!currentHostel) return;
    fetchActiveStudents();
    fetchReturnedStudents();
    renderRollCallBanner();
}

window.refreshData = function() {
    toast('Syncing...', 'info');
    refreshAll();
};

// ========== POLLING ==========
var poll = null;

function startPoll() {
    if (poll) clearInterval(poll);
    poll = setInterval(function() {
        if (currentHostel) refreshAll();
    }, 15000);
}

document.addEventListener('visibilitychange', function() {
    if (document.hidden) {
        if (poll) { clearInterval(poll);
            poll = null; }
    } else {
        startPoll();
    }
});

// ========== INIT ==========
console.log('ward_dash.js loaded');
renderRollCallBanner();
startPoll();