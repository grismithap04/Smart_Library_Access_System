from flask import Flask, jsonify, send_from_directory, request
from flask_cors import CORS
from datetime import datetime
import cx_Oracle
import threading
import time
import logging
import atexit

app = Flask(__name__)
CORS(app)

# ========== CONFIGURATION ==========
AUTO_EXIT_HOUR, AUTO_EXIT_MINUTE = 20, 10          # 8:10 PM
ENTRY_CUTOFF_HOUR, ENTRY_CUTOFF_MINUTE = 18, 10    # 6:10 PM

# ========== LOGGING ==========
logging.basicConfig(level=logging.INFO, format='%(asctime)s - %(levelname)s - %(message)s')
logger = logging.getLogger(__name__)

# ========== ORACLE DATABASE ==========
DB_CONFIG = {'user': 'system', 'password': '1234', 'dsn': 'localhost:1521/XE'}

# ========== ENROLLMENT QUEUE ==========
enrollment_queue = {}

# ========== AUTO-EXIT TRACKING ==========
auto_exit_ran_today = False
auto_exit_lock = threading.Lock()
scheduler_running = True

# ========== DATABASE CONNECTION ==========
def get_db():
    try:
        return cx_Oracle.connect(**DB_CONFIG)
    except cx_Oracle.DatabaseError as e:
        logger.error(f"Database connection error: {e}")
        return None

# ========== EXECUTE QUERY HELPER ==========
def execute_query(query, params=None, fetch=False, commit=False):
    """Execute a database query with optional fetch or commit"""
    conn = get_db()
    if not conn: return None if not fetch else []
    cursor = conn.cursor()
    try:
        if params: cursor.execute(query, params)
        else: cursor.execute(query)
        if commit: conn.commit()
        if fetch:
            result = cursor.fetchall()
            cursor.close(); conn.close()
            return result
        cursor.close(); conn.close()
        return True
    except cx_Oracle.DatabaseError as e:
        logger.error(f"Database error: {e}")
        cursor.close(); conn.close()
        return None if not fetch else []

# ========== CORE AUTO-EXIT ==========
def perform_auto_exit(source='auto-exit', is_manual=False):
    global auto_exit_ran_today
    if len(source) > 20: source = source[:20]
    logger.info("=" * 55)
    logger.info(f"🖐️ MANUAL AUTO-EXIT TRIGGERED (Button click)" if is_manual else "🕐 AUTOMATIC AUTO-EXIT TRIGGERED")
    logger.info("=" * 55)
    
    active = execute_query("""
        SELECT l.log_id, l.reg_id, s.name, s.hostel_code, s.room_no
        FROM lib_log l JOIN students s ON l.reg_id = s.reg_id
        WHERE TRUNC(l.log_date) = TRUNC(SYSDATE) AND l.exit_time IS NULL
    """, fetch=True)
    
    if not active:
        logger.info("No students currently in library.")
        if not is_manual:
            with auto_exit_lock: auto_exit_ran_today = True
        return {'ok': True, 'message': 'No students currently in library', 'exited_count': 0, 'students': []}
    
    exited, students = 0, []
    for log_id, reg_id, name, hostel_code, room_no in active:
        execute_query(
            "UPDATE lib_log SET exit_time = SYSTIMESTAMP, source = :source WHERE log_id = :log_id",
            {'source': source, 'log_id': log_id}, commit=True
        )
        exited += 1
        students.append({'reg_id': reg_id, 'name': name, 'hostel_code': hostel_code, 'room_no': room_no})
        try:
            execute_query("""
                INSERT INTO hostel_attendance (reg_id, att_date, att_time, status, library_log_id, verified_by)
                VALUES (:reg_id, TRUNC(SYSDATE), SYSTIMESTAMP, 'IN_LIBRARY', :log_id, :verified_by)
            """, {'reg_id': reg_id, 'log_id': log_id, 'verified_by': 'manual' if is_manual else 'auto'}, commit=True)
        except Exception as e:
            logger.warning(f"Could not insert hostel attendance for {reg_id}: {e}")
    
    if not is_manual:
        with auto_exit_lock: auto_exit_ran_today = True
    
    logger.info(f"✅ Auto-exited {exited} students:")
    for s in students: logger.info(f"   - {s['name']} ({s['reg_id']})")
    return {'ok': True, 'message': f'Successfully exited {exited} students', 'exited_count': exited, 'students': students, 'source': source, 'is_manual': is_manual}

# ========== BACKGROUND SCHEDULER ==========
def auto_exit_scheduler():
    global auto_exit_ran_today, scheduler_running
    logger.info("🔄 Auto-exit scheduler started.")
    logger.info(f"⏰ Auto-exit will run ANYTIME after {AUTO_EXIT_HOUR:02d}:{AUTO_EXIT_MINUTE:02d} (8:10 PM)")
    while scheduler_running:
        try:
            now = datetime.now()
            h, m = now.hour, now.minute
            if h == 0 and m <= 5:
                with auto_exit_lock:
                    if auto_exit_ran_today: logger.info("🔄 New day detected. Resetting auto-exit flag.")
                    auto_exit_ran_today = False
            if (h > AUTO_EXIT_HOUR or (h == AUTO_EXIT_HOUR and m >= AUTO_EXIT_MINUTE)) and not auto_exit_ran_today:
                logger.info(f"⏰ Auto-exit triggered at {h:02d}:{m:02d}")
                perform_auto_exit(source='auto-exit', is_manual=False)
            time.sleep(30)
        except Exception as e:
            logger.error(f"Error in auto-exit scheduler: {e}")
            time.sleep(60)

def start_auto_exit_scheduler():
    t = threading.Thread(target=auto_exit_scheduler, daemon=True)
    t.start()
    logger.info("✅ Auto-exit scheduler thread started")
    return t

def stop_auto_exit_scheduler():
    global scheduler_running
    scheduler_running = False
    logger.info("🛑 Auto-exit scheduler stopped")

atexit.register(stop_auto_exit_scheduler)

# ========== URL TRIGGER ==========
@app.route('/api/run-auto-exit-now', methods=['GET'])
def run_auto_exit_now():
    logger.info("🖐️ AUTO-EXIT TRIGGERED via URL")
    return jsonify({'status': 'completed', 'result': perform_auto_exit(source='auto-exit', is_manual=False), 'time': datetime.now().strftime('%Y-%m-%d %H:%M:%S')})

# ========== SERVE DASHBOARDS ==========
@app.route('/')
def serve_librarian(): return send_from_directory('lib_dash', 'lib_dash.html')
@app.route('/warden')
def serve_warden(): return send_from_directory('ward_dash', 'ward_dash.html')
@app.route('/lib_dash/<path:filename>')
def serve_lib_dash_files(filename):
    try: return send_from_directory('lib_dash', filename)
    except: return f"File not found: {filename}", 404
@app.route('/ward_dash/<path:filename>')
def serve_ward_dash_files(filename):
    try: return send_from_directory('ward_dash', filename)
    except: return f"File not found: {filename}", 404

# ========== API ENDPOINTS ==========
@app.route('/api/test')
def test_api():
    return jsonify({'status': 'ok', 'message': 'API is running!', 'timestamp': datetime.now().isoformat()})

@app.route('/api/auto-exit-status')
def get_auto_exit_status():
    h, m = datetime.now().hour, datetime.now().minute
    is_after = h > AUTO_EXIT_HOUR or (h == AUTO_EXIT_HOUR and m >= AUTO_EXIT_MINUTE)
    return jsonify({
        'ran_today': auto_exit_ran_today,
        'is_after_cutoff': is_after,
        'cutoff_time': f"{AUTO_EXIT_HOUR:02d}:{AUTO_EXIT_MINUTE:02d}",
        'current_time': datetime.now().strftime('%H:%M'),
        'auto_exit_completed': auto_exit_ran_today and is_after,
        'scheduler_running': scheduler_running
    })

@app.route('/api/auto-exit', methods=['POST'])
def auto_exit_endpoint():
    try:
        return jsonify(perform_auto_exit(source='manual-exit', is_manual=True))
    except Exception as e:
        logger.error(f"Error in auto_exit_endpoint: {e}")
        return jsonify({'ok': False, 'error': str(e)}), 500

# ========== IS_AFTER_ENTRY_CUTOFF ==========
def is_after_entry_cutoff():
    h, m = datetime.now().hour, datetime.now().minute
    return h > ENTRY_CUTOFF_HOUR or (h == ENTRY_CUTOFF_HOUR and m >= ENTRY_CUTOFF_MINUTE)

# ========== MOCK FINGERPRINT ==========
@app.route('/api/mock-fingerprint', methods=['POST'])
def mock_fingerprint():
    try:
        data = request.json
        reg_id, action_type = data.get('reg_id'), data.get('type')
        if not reg_id or not action_type: return jsonify({'error': 'reg_id and type required'}), 400
        
        # Entry cutoff check (6:10 PM)
        if is_after_entry_cutoff():
            conn, cursor = get_db(), None
            if conn:
                cursor = conn.cursor()
                cursor.execute("SELECT log_id FROM lib_log WHERE reg_id = :reg_id AND TRUNC(log_date) = TRUNC(SYSDATE) AND exit_time IS NULL", {'reg_id': reg_id})
                if not cursor.fetchone():
                    cursor.close(); conn.close()
                    logger.warning(f"🚫 Entry blocked for {reg_id} (after 6:10 PM)")
                    return jsonify({'ok': False, 'error': 'Entry closed after 6:10 PM. Please see the librarian.', 'blocked': True, 'reason': 'entry_cutoff'}), 403
                cursor.close(); conn.close()
        
        conn, cursor = get_db(), None
        if not conn: return jsonify({'error': 'Database connection failed'}), 500
        cursor = conn.cursor()
        
        cursor.execute("SELECT reg_id FROM students WHERE reg_id = :reg_id", {'reg_id': reg_id})
        if not cursor.fetchone():
            cursor.close(); conn.close()
            return jsonify({'error': f'Student {reg_id} not found'}), 404
        
        cursor.execute("SELECT log_id FROM lib_log WHERE reg_id = :reg_id AND TRUNC(log_date) = TRUNC(SYSDATE) AND exit_time IS NULL", {'reg_id': reg_id})
        open_session = cursor.fetchone()
        
        if open_session and action_type == 'entry':
            cursor.close(); conn.close()
            logger.warning(f"🚫 Duplicate entry blocked for {reg_id}")
            return jsonify({'ok': False, 'error': 'Student already has an open session.', 'blocked': True, 'reason': 'duplicate_entry'}), 400
        
        if open_session:
            cursor.execute("UPDATE lib_log SET exit_time = SYSTIMESTAMP, source = 'biometric' WHERE log_id = :log_id", {'log_id': open_session[0]})
            msg, act = 'Exit logged', 'exit'
        else:
            cursor.execute("INSERT INTO lib_log (reg_id, log_date, entry_time, source) VALUES (:reg_id, TRUNC(SYSDATE), SYSTIMESTAMP, 'biometric')", {'reg_id': reg_id})
            msg, act = 'Entry logged', 'entry'
        
        conn.commit(); cursor.close(); conn.close()
        logger.info(f"✅ {msg} for {reg_id}")
        return jsonify({'ok': True, 'message': msg, 'reg_id': reg_id, 'type': act})
    except Exception as e:
        logger.error(f"Error in mock_fingerprint: {e}")
        return jsonify({'error': str(e)}), 500

# ========== ACTIVE SESSIONS ==========
@app.route('/api/active')
def get_active():
    rows = execute_query("""
        SELECT l.log_id, l.reg_id, s.name, s.hostel_code, s.room_no,
               TO_CHAR(l.entry_time, 'HH24:MI:SS') as entry_time,
               TO_CHAR(l.entry_time, 'YYYY-MM-DD"T"HH24:MI:SS') as entry_full
        FROM lib_log l JOIN students s ON l.reg_id = s.reg_id
        WHERE TRUNC(l.log_date) = TRUNC(SYSDATE) AND l.exit_time IS NULL ORDER BY l.entry_time
    """, fetch=True)
    if rows is None: return jsonify({'error': 'Database error'}), 500
    return jsonify([{
        'log_id': r[0], 'reg_id': r[1], 'name': r[2], 'hostel_code': r[3],
        'room_no': r[4], 'entry_time': r[5], 'entry_full': r[6]
    } for r in rows])

# ========== TODAY'S LOG ==========
@app.route('/api/today-log')
def get_today_log():
    rows = execute_query("""
        SELECT l.log_id, l.reg_id, s.name, s.hostel_code, s.room_no,
               TO_CHAR(l.entry_time, 'HH24:MI:SS'), TO_CHAR(l.exit_time, 'HH24:MI:SS'),
               l.source,
               CASE WHEN l.exit_time IS NULL THEN 'IN_LIBRARY' ELSE 'EXITED' END as status,
               CASE WHEN l.exit_time IS NOT NULL THEN 
                    TO_CHAR(EXTRACT(HOUR FROM (l.exit_time - l.entry_time)), 'FM00') || ':' ||
                    TO_CHAR(EXTRACT(MINUTE FROM (l.exit_time - l.entry_time)), 'FM00') || ':' ||
                    TO_CHAR(EXTRACT(SECOND FROM (l.exit_time - l.entry_time)), 'FM00')
               ELSE NULL END as duration
        FROM lib_log l JOIN students s ON l.reg_id = s.reg_id
        WHERE TRUNC(l.log_date) = TRUNC(SYSDATE) ORDER BY l.entry_time
    """, fetch=True)
    if rows is None: return jsonify({'error': 'Database error'}), 500
    unique = set(r[1] for r in rows)
    return jsonify({
        'logs': [{
            'log_id': r[0], 'reg_id': r[1], 'name': r[2], 'hostel_code': r[3],
            'room_no': r[4], 'entry_time': r[5], 'exit_time': r[6],
            'source': r[7], 'status': r[8], 'duration': r[9]
        } for r in rows],
        'unique_students': len(unique)
    })

# ========== STUDENTS ==========
@app.route('/api/students')
def get_students():
    rows = execute_query("""
        SELECT s.reg_id, s.name, s.dept, s.course, s.year, s.hostel_code, s.room_no, s.phone,
               CASE WHEN f.fp_slot IS NOT NULL THEN 1 ELSE 0 END as has_fingerprint
        FROM students s LEFT JOIN fingerprint_map f ON s.reg_id = f.reg_id ORDER BY s.name
    """, fetch=True)
    if rows is None: return jsonify({'error': 'Database error'}), 500
    return jsonify([{
        'reg_id': r[0], 'name': r[1], 'dept': r[2], 'course': r[3], 'year': r[4],
        'hostel_code': r[5], 'room_no': r[6], 'phone': r[7], 'has_fingerprint': bool(r[8])
    } for r in rows])

# ========== STUDENT LOOKUP ==========
@app.route('/api/student/<reg_id>')
def get_student(reg_id):
    row = execute_query("""
        SELECT reg_id, name, dept, course, year, hostel_code, room_no, phone
        FROM students WHERE reg_id = :reg_id
    """, {'reg_id': reg_id}, fetch=True)
    if row is None: return jsonify({'error': 'Database error'}), 500
    if not row: return jsonify({'error': 'Student not found'}), 404
    r = row[0]
    return jsonify({
        'reg_id': r[0], 'name': r[1], 'dept': r[2], 'course': r[3],
        'year': r[4], 'hostel_code': r[5], 'room_no': r[6], 'phone': r[7]
    })

# ========== FINGERPRINT LOOKUP ==========
@app.route('/api/fingerprint-lookup', methods=['GET'])
def fingerprint_lookup():
    try:
        fp_slot = request.args.get('fp_slot')
        if not fp_slot: return jsonify({'error': 'fp_slot required'}), 400
        row = execute_query("""
            SELECT s.reg_id, s.name, s.hostel_code, s.room_no
            FROM fingerprint_map f JOIN students s ON f.reg_id = s.reg_id
            WHERE f.fp_slot = :fp_slot
        """, {'fp_slot': fp_slot}, fetch=True)
        if row is None: return jsonify({'error': 'Database error'}), 500
        if not row: return jsonify({'error': 'Fingerprint not registered'}), 404
        r = row[0]
        return jsonify({'reg_id': r[0], 'name': r[1], 'hostel_code': r[2], 'room_no': r[3]})
    except Exception as e:
        logger.error(f"Error in fingerprint_lookup: {e}")
        return jsonify({'error': str(e)}), 500

# ========== FORCE EXIT ==========
@app.route('/api/force-exit', methods=['POST'])
def force_exit():
    try:
        log_id = request.json.get('log_id')
        if not log_id: return jsonify({'error': 'log_id required'}), 400
        result = execute_query(
            "UPDATE lib_log SET exit_time = SYSTIMESTAMP, source = 'force-exit' WHERE log_id = :log_id AND exit_time IS NULL",
            {'log_id': log_id}, commit=True
        )
        if result is False: return jsonify({'error': 'Database error'}), 500
        return jsonify({'ok': True, 'message': 'Exit recorded successfully'})
    except Exception as e:
        logger.error(f"Error in force_exit: {e}")
        return jsonify({'error': str(e)}), 500

# ========== HOSTEL ENDPOINTS ==========
@app.route('/api/hostel/<hostel_code>/active')
def get_hostel_active(hostel_code):
    rows = execute_query("""
        SELECT l.log_id, l.reg_id, s.name, s.room_no, TO_CHAR(l.entry_time, 'HH24:MI:SS') as entry_time
        FROM lib_log l JOIN students s ON l.reg_id = s.reg_id
        WHERE TRUNC(l.log_date) = TRUNC(SYSDATE) AND l.exit_time IS NULL AND s.hostel_code = :hc ORDER BY l.entry_time
    """, {'hc': hostel_code}, fetch=True)
    if rows is None: return jsonify({'error': 'Database error'}), 500
    return jsonify([{'log_id': r[0], 'reg_id': r[1], 'name': r[2], 'room_no': r[3], 'entry_time': r[4]} for r in rows])

@app.route('/api/hostel/<hostel_code>/returned')
def get_hostel_returned(hostel_code):
    rows = execute_query("""
        SELECT l.log_id, l.reg_id, s.name, s.room_no,
               TO_CHAR(l.entry_time, 'HH24:MI:SS'), TO_CHAR(l.exit_time, 'HH24:MI:SS'),
               TO_CHAR(l.exit_time, 'HH24:MI:SS')
        FROM lib_log l JOIN students s ON l.reg_id = s.reg_id
        WHERE TRUNC(l.log_date) = TRUNC(SYSDATE) AND l.exit_time IS NOT NULL AND s.hostel_code = :hc
        ORDER BY l.exit_time DESC
    """, {'hc': hostel_code}, fetch=True)
    if rows is None: return jsonify({'error': 'Database error'}), 500
    return jsonify([{
        'log_id': r[0], 'reg_id': r[1], 'name': r[2], 'room_no': r[3],
        'entry_time': r[4], 'exit_time': r[5], 'hostel_return': r[6]
    } for r in rows])

# ========== MANUAL ENTRY ==========
@app.route('/api/manual-entry', methods=['POST'])
def manual_entry():
    try:
        data = request.json
        reg_id, entry_type, reason = data.get('reg_id'), data.get('type'), data.get('reason')
        if not reg_id or not entry_type or not reason:
            return jsonify({'error': 'reg_id, type, and reason required'}), 400
        
        # Entry cutoff check (6:10 PM) — only for ENTRY
        if entry_type == 'entry' and is_after_entry_cutoff():
            return jsonify({'ok': False, 'error': 'Manual entry not allowed after 6:10 PM.', 'blocked': True, 'reason': 'entry_cutoff'}), 403
        
        conn, cursor = get_db(), None
        if not conn: return jsonify({'error': 'Database connection failed'}), 500
        cursor = conn.cursor()
        
        cursor.execute("SELECT reg_id FROM students WHERE reg_id = :reg_id", {'reg_id': reg_id})
        if not cursor.fetchone():
            cursor.close(); conn.close()
            return jsonify({'error': f'Student {reg_id} not found'}), 404
        
        # Duplicate entry check
        if entry_type == 'entry':
            cursor.execute("SELECT log_id FROM lib_log WHERE reg_id = :reg_id AND TRUNC(log_date) = TRUNC(SYSDATE) AND exit_time IS NULL", {'reg_id': reg_id})
            if cursor.fetchone():
                cursor.close(); conn.close()
                return jsonify({'ok': False, 'error': f'Student {reg_id} already has an open session.', 'blocked': True, 'reason': 'duplicate_entry'}), 400
        
        if entry_type == 'exit':
            cursor.execute("SELECT log_id FROM lib_log WHERE reg_id = :reg_id AND TRUNC(log_date) = TRUNC(SYSDATE) AND exit_time IS NULL", {'reg_id': reg_id})
            active = cursor.fetchone()
            if not active:
                cursor.close(); conn.close()
                return jsonify({'error': 'No active session found for this student'}), 404
            cursor.execute("UPDATE lib_log SET exit_time = SYSTIMESTAMP, source = 'manual', manual_reason = :reason WHERE log_id = :log_id",
                          {'reason': reason, 'log_id': active[0]})
        else:
            cursor.execute("INSERT INTO lib_log (reg_id, log_date, entry_time, source, manual_reason) VALUES (:reg_id, TRUNC(SYSDATE), SYSTIMESTAMP, 'manual', :reason)",
                          {'reg_id': reg_id, 'reason': reason})
        
        conn.commit(); cursor.close(); conn.close()
        return jsonify({'ok': True, 'message': f'{entry_type.capitalize()} recorded manually for {reg_id}'})
    except Exception as e:
        logger.error(f"Error in manual_entry: {e}")
        return jsonify({'error': str(e)}), 500

# ========== ENROLLMENT ENDPOINTS ==========
@app.route('/api/enrollment-queue', methods=['GET'])
def get_enrollment_queue():
    esp_id = request.args.get('esp_id', 'library1')
    reg_id = enrollment_queue.get(esp_id)
    if reg_id:
        enrollment_queue[esp_id] = None
        return jsonify({'reg_id': reg_id})
    return jsonify({'reg_id': None})

@app.route('/api/request-enrollment', methods=['POST'])
def request_enrollment():
    data = request.json
    reg_id, esp_id = data.get('reg_id'), data.get('esp_id', 'library1')
    if not reg_id: return jsonify({'error': 'reg_id required'}), 400
    
    conn, cursor = get_db(), None
    if not conn: return jsonify({'error': 'Database connection failed'}), 500
    cursor = conn.cursor()
    
    cursor.execute("SELECT reg_id, name FROM students WHERE reg_id = :reg_id", {'reg_id': reg_id})
    student = cursor.fetchone()
    cursor.close(); conn.close()
    if not student: return jsonify({'error': 'Student not found'}), 404
    
    conn, cursor = get_db(), None
    if not conn: return jsonify({'error': 'Database connection failed'}), 500
    cursor = conn.cursor()
    cursor.execute("SELECT fp_slot FROM fingerprint_map WHERE reg_id = :reg_id", {'reg_id': reg_id})
    if cursor.fetchone():
        cursor.close(); conn.close()
        return jsonify({'error': 'Student already has fingerprint registered'}), 409
    cursor.close(); conn.close()
    
    enrollment_queue[esp_id] = reg_id
    return jsonify({'ok': True, 'message': 'Enrollment requested. Ask student to place finger on sensor.', 'reg_id': reg_id, 'name': student[1]})

@app.route('/api/complete-enrollment', methods=['POST'])
def complete_enrollment():
    try:
        data = request.json
        reg_id, fp_slot = data.get('reg_id'), data.get('fp_slot')
        if not reg_id or not fp_slot: return jsonify({'error': 'reg_id and fp_slot required'}), 400
        if execute_query("INSERT INTO fingerprint_map (fp_slot, reg_id) VALUES (:fp_slot, :reg_id)",
                        {'fp_slot': fp_slot, 'reg_id': reg_id}, commit=True) is False:
            return jsonify({'error': 'Database error'}), 500
        return jsonify({'ok': True, 'message': 'Fingerprint enrolled successfully', 'reg_id': reg_id, 'fp_slot': fp_slot})
    except Exception as e:
        logger.error(f"Error in complete_enrollment: {e}")
        return jsonify({'error': str(e)}), 500

@app.route('/api/enroll-status/<reg_id>', methods=['GET'])
def enroll_status(reg_id):
    row = execute_query("SELECT fp_slot FROM fingerprint_map WHERE reg_id = :reg_id", {'reg_id': reg_id}, fetch=True)
    if row is None: return jsonify({'status': 'error', 'message': 'Database error'}), 500
    return jsonify({'status': 'success', 'fp_slot': row[0][0]} if row else {'status': 'pending', 'message': 'Waiting for enrollment'})

@app.route('/api/enrollment-failed', methods=['POST'])
def enrollment_failed():
    try:
        data = request.json
        reg_id = data.get('reg_id')
        reason = data.get('reason', 'Unknown error')
        logger.error(f"❌ Enrollment failed for {reg_id}: {reason}")
        for esp_id, pending in enrollment_queue.items():
            if pending == reg_id: enrollment_queue[esp_id] = None
        return jsonify({'ok': True})
    except Exception as e:
        logger.error(f"Error in enrollment_failed: {e}")
        return jsonify({'error': str(e)}), 500

# ========== RUN ==========
if __name__ == '__main__':
    print("=" * 55)
    print("🚀 Starting Library Smart Access System (Oracle)")
    print("=" * 55)
    print("📚 Librarian: http://localhost:5000/")
    print("🏠 Warden: http://localhost:5000/warden")
    print("📡 API Test: http://localhost:5000/api/test")
    print("=" * 55)
    print(f"⏰ AUTO-EXIT: Runs AUTOMATICALLY after {AUTO_EXIT_HOUR:02d}:{AUTO_EXIT_MINUTE:02d} (8:10 PM)")
    print(f"🚫 ENTRY CUTOFF: No entries after {ENTRY_CUTOFF_HOUR:02d}:{ENTRY_CUTOFF_MINUTE:02d} (6:10 PM)")
    print("🖐️ MANUAL: Click 'Force Exit All' button or visit /api/run-auto-exit-now")
    print("=" * 55)
    start_auto_exit_scheduler()
    app.run(host='0.0.0.0', port=5000, debug=True, use_reloader=False)