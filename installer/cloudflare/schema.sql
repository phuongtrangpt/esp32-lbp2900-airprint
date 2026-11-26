CREATE TABLE IF NOT EXISTS access_codes (hash TEXT PRIMARY KEY, expires INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS codes_expiry ON access_codes(expires);
CREATE TABLE IF NOT EXISTS access_sessions (hash TEXT PRIMARY KEY, expires INTEGER NOT NULL);
CREATE INDEX IF NOT EXISTS sessions_expiry ON access_sessions(expires);
CREATE TABLE IF NOT EXISTS access_limits (hash TEXT PRIMARY KEY, count INTEGER NOT NULL, expires INTEGER NOT NULL);
