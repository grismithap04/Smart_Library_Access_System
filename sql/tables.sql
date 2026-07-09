CREATE TABLE students (
    reg_id          VARCHAR2(20) PRIMARY KEY,
    name            VARCHAR2(100) NOT NULL,
    dept            VARCHAR2(50) NOT NULL,
    course          VARCHAR2(50) NOT NULL,
    year            NUMBER(1) NOT NULL,
    hostel_code     VARCHAR2(10) NOT NULL,
    room_no         VARCHAR2(20) NOT NULL,
    phone           VARCHAR2(15),
    created_at      TIMESTAMP DEFAULT SYSTIMESTAMP
);

CREATE TABLE fingerprint_map (
    fp_slot         NUMBER PRIMARY KEY,
    reg_id          VARCHAR2(20) UNIQUE NOT NULL,
    enrolled_at     TIMESTAMP DEFAULT SYSTIMESTAMP,
    CONSTRAINT fk_fp_student FOREIGN KEY (reg_id) REFERENCES students(reg_id)
);

CREATE TABLE hostel_attendance (
    att_id          NUMBER GENERATED ALWAYS AS IDENTITY PRIMARY KEY,
    reg_id          VARCHAR2(20) NOT NULL,
    att_date        DATE DEFAULT TRUNC(SYSDATE),
    att_time        TIMESTAMP NOT NULL,
    status          VARCHAR2(20) NOT NULL,
    library_log_id  NUMBER,
    message_sent    CHAR(1) DEFAULT 'N',
    verified_by     VARCHAR2(30) DEFAULT 'system',
    CONSTRAINT fk_att_student FOREIGN KEY (reg_id) REFERENCES students(reg_id),
    CONSTRAINT fk_att_library FOREIGN KEY (library_log_id) REFERENCES lib_log(log_id),
    CONSTRAINT chk_status CHECK (status IN ('PRESENT', 'LATE', 'IN_LIBRARY')),
    CONSTRAINT chk_message CHECK (message_sent IN ('Y', 'N'))
);

CREATE INDEX idx_lib_log_date ON lib_log(log_date);
CREATE INDEX idx_lib_log_regid ON lib_log(reg_id);
CREATE INDEX idx_lib_log_regid_date ON lib_log(reg_id, log_date);
CREATE INDEX idx_hostel_att_date ON hostel_attendance(att_date);
CREATE INDEX idx_hostel_att_regid ON hostel_attendance(reg_id);

select * from students;
select * from fingerprint_map;

-- Increase SOURCE column size to VARCHAR2(20)
ALTER TABLE SYSTEM.LIB_LOG MODIFY SOURCE VARCHAR2(20);

-- Update the check constraint to include new values
ALTER TABLE SYSTEM.LIB_LOG DROP CONSTRAINT CHK_SOURCE;

ALTER TABLE SYSTEM.LIB_LOG ADD CONSTRAINT CHK_SOURCE CHECK (
    SOURCE IN ('biometric', 'manual', 'auto-exit', 'force-exit', 'manual-auto-exit')
);

-- Verify the change
DESC SYSTEM.LIB_LOG;

-- Check existing data
SELECT SOURCE, COUNT(*) FROM SYSTEM.LIB_LOG GROUP BY SOURCE;

-- Drop old constraint
ALTER TABLE SYSTEM.LIB_LOG DROP CONSTRAINT CHK_SOURCE;

-- Add new constraint with all values (including manual-exit)
ALTER TABLE SYSTEM.LIB_LOG ADD CONSTRAINT CHK_SOURCE CHECK (
    SOURCE IN ('biometric', 'manual', 'auto-exit', 'force-exit', 'manual-exit', 'manual')
);

-- Commit
COMMIT;

