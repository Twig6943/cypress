package master

import (
	"crypto/rand"
	"database/sql"
	"encoding/hex"
	"fmt"
	"log"
	"net/http"
	"os"
	"strings"
	"time"

	"cypress-servers/internal/ea"
	"cypress-servers/internal/ticket"
)

const (
	playerSessionExpiry = 7 * 24 * time.Hour // cypress session lasts 7 days
	ticketTTL           = 30 * time.Minute
)

// db schema for auth tables
const authSchema = `
	CREATE TABLE IF NOT EXISTS players (
		pid TEXT PRIMARY KEY,
		psid INTEGER NOT NULL DEFAULT 0,
		uid TEXT NOT NULL DEFAULT '',
		display_name TEXT NOT NULL DEFAULT '',
		device_id TEXT NOT NULL DEFAULT '',
		first_seen REAL NOT NULL,
		last_seen REAL NOT NULL,
		banned INTEGER NOT NULL DEFAULT 0,
		ban_reason TEXT NOT NULL DEFAULT '',
		banned_by TEXT NOT NULL DEFAULT ''
	);
	CREATE TABLE IF NOT EXISTS player_sessions (
		token TEXT PRIMARY KEY,
		pid TEXT NOT NULL,
		created_at REAL NOT NULL,
		expires_at REAL NOT NULL,
		FOREIGN KEY (pid) REFERENCES players(pid)
	);
	CREATE INDEX IF NOT EXISTS idx_player_sessions_pid ON player_sessions(pid);
	CREATE INDEX IF NOT EXISTS idx_player_sessions_expires ON player_sessions(expires_at);
`

func initAuthDB(db *sql.DB) error {
	_, err := db.Exec(authSchema)
	if err != nil {
		return err
	}
	db.Exec("ALTER TABLE players ADD COLUMN uid TEXT NOT NULL DEFAULT ''")
	return nil
}

// validate a cypress session token from request header
func (s *masterState) validatePlayerSession(r *http.Request) (pid string, displayName string, ok bool) {
	auth := r.Header.Get("Authorization")
	if !strings.HasPrefix(auth, "Bearer ") {
		return
	}
	token := auth[7:]
	now := float64(time.Now().Unix())

	var p string
	var dn string
	err := s.db.QueryRow(
		`SELECT ps.pid, p.display_name FROM player_sessions ps
		 JOIN players p ON ps.pid = p.pid
		 WHERE ps.token = ? AND ps.expires_at > ?`,
		token, now,
	).Scan(&p, &dn)
	if err != nil {
		return
	}

	// update last_seen
	s.db.Exec("UPDATE players SET last_seen = ? WHERE pid = ?", now, p)

	return p, dn, true
}

func generateSessionToken() string {
	b := make([]byte, 48)
	rand.Read(b)
	return hex.EncodeToString(b)
}

// POST /auth/login, exchange EA JWT for cypress session
func (s *masterState) handleAuthLogin(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "auth_login", 10, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	eaToken := getString(data, "token")
	if eaToken == "" {
		errResp(w, 400, "EA token required")
		return
	}

	// validate token with ea's tokeninfo endpoint
	tokenInfo, err := ea.FetchTokenInfo(eaToken)
	if err != nil {
		log.Printf("[auth] ea tokeninfo failed from %s: %v", ip, err)
		errResp(w, 401, fmt.Sprintf("EA token invalid: %v", err))
		return
	}

	pid := tokenInfo.PIDId
	psid := tokenInfo.PersonaID
	uid := tokenInfo.UserID
	displayName := ""
	deviceID := ""

	// try to extract richer claims from jwt payload if it's a JWS token
	if jwtClaims, err := ea.ParseClaimsUnsafe(eaToken); err == nil {
		if jwtClaims.Nexus.UserInfo.Status != "ACTIVE" {
			errResp(w, 403, "EA account is not active")
			return
		}
		displayName = jwtClaims.DisplayName()
		deviceID = jwtClaims.Nexus.DVID
		if jwtClaims.Nexus.PSID != 0 {
			psid = jwtClaims.Nexus.PSID
		}
		if jwtClaims.Nexus.UID != "" {
			uid = jwtClaims.Nexus.UID
		}
	} else {
		log.Printf("[auth] jwt claims parse failed (using tokeninfo only): %v", err)
	}

	now := float64(time.Now().Unix())

	// check if player is banned
	var banned int
	var banReason string
	err = s.db.QueryRow("SELECT banned, ban_reason FROM players WHERE pid = ?", pid).Scan(&banned, &banReason)
	if err == nil && banned != 0 {
		errResp(w, 403, fmt.Sprintf("Account banned: %s", banReason))
		return
	}

	// upsert player
	_, err = s.db.Exec(`
		INSERT INTO players (pid, psid, uid, display_name, device_id, first_seen, last_seen)
		VALUES (?, ?, ?, ?, ?, ?, ?)
		ON CONFLICT(pid) DO UPDATE SET
			psid = excluded.psid,
			uid = excluded.uid,
			display_name = excluded.display_name,
			device_id = excluded.device_id,
			last_seen = excluded.last_seen
	`, pid, psid, uid, displayName, deviceID, now, now)
	if err != nil {
		log.Printf("[auth] db error upserting player %s: %v", pid, err)
		errResp(w, 500, "Internal error")
		return
	}

	// create session
	sessionToken := generateSessionToken()
	expiresAt := float64(time.Now().Add(playerSessionExpiry).Unix())
	s.db.Exec("INSERT INTO player_sessions (token, pid, created_at, expires_at) VALUES (?, ?, ?, ?)",
		sessionToken, pid, now, expiresAt)

	auditLog(s.db, "player_login", displayName, fmt.Sprintf("pid=%s", pid), ip)
	log.Printf("[auth] player login: %s (%s) from %s", displayName, pid, ip)

	jsonResp(w, 200, map[string]any{
		"ok":          true,
		"token":       sessionToken,
		"pid":         pid,
		"psid":        psid,
		"uid":         uid,
		"displayName": displayName,
		"expiresAt":   expiresAt,
	})
}

// POST /auth/refresh, refresh a cypress session token
func (s *masterState) handleAuthRefresh(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "auth_refresh", 15, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	pid, _, ok := s.validatePlayerSession(r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	// check if banned since last refresh
	var banned int
	var banReason string
	s.db.QueryRow("SELECT banned, ban_reason FROM players WHERE pid = ?", pid).Scan(&banned, &banReason)
	if banned != 0 {
		errResp(w, 403, fmt.Sprintf("Account banned: %s", banReason))
		return
	}

	// invalidate old token
	auth := r.Header.Get("Authorization")
	oldToken := auth[7:]
	s.db.Exec("DELETE FROM player_sessions WHERE token = ?", oldToken)

	// issue new session
	now := float64(time.Now().Unix())
	newToken := generateSessionToken()
	expiresAt := float64(time.Now().Add(playerSessionExpiry).Unix())
	s.db.Exec("INSERT INTO player_sessions (token, pid, created_at, expires_at) VALUES (?, ?, ?, ?)",
		newToken, pid, now, expiresAt)

	jsonResp(w, 200, map[string]any{
		"ok":        true,
		"token":     newToken,
		"expiresAt": expiresAt,
	})
}

// GET /auth/me, get current player info
func (s *masterState) handleAuthMe(w http.ResponseWriter, r *http.Request) {
	pid, displayName, ok := s.validatePlayerSession(r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}
	var psid uint64
	var uid string
	s.db.QueryRow("SELECT psid, uid FROM players WHERE pid = ?", pid).Scan(&psid, &uid)
	jsonResp(w, 200, map[string]any{
		"ok":          true,
		"pid":         pid,
		"psid":        psid,
		"uid":         uid,
		"displayName": displayName,
	})
}

// POST /auth/ticket, issue a short-lived session ticket for game servers
func (s *masterState) handleAuthTicket(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	if !s.rl.check(ip, "auth_ticket", 20, 60*time.Second) {
		errResp(w, 429, "Rate limited")
		return
	}

	pid, displayName, ok := s.validatePlayerSession(r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	// check ban status
	var banned int
	s.db.QueryRow("SELECT banned FROM players WHERE pid = ?", pid).Scan(&banned)
	if banned != 0 {
		errResp(w, 403, "Account banned")
		return
	}

	var psid uint64
	s.db.QueryRow("SELECT psid FROM players WHERE pid = ?", pid).Scan(&psid)

	t := ticket.Issue(s.ticketSecret, pid, psid, displayName, ticketTTL)

	jsonResp(w, 200, map[string]any{
		"ok":     true,
		"ticket": t,
	})
}

// POST /auth/logout, invalidate session
func (s *masterState) handleAuthLogout(w http.ResponseWriter, r *http.Request) {
	auth := r.Header.Get("Authorization")
	if strings.HasPrefix(auth, "Bearer ") {
		s.db.Exec("DELETE FROM player_sessions WHERE token = ?", auth[7:])
	}
	jsonResp(w, 200, map[string]any{"ok": true})
}

// mod endpoints for player bans

// POST /mod/ban-player, ban an EA player account
func (s *masterState) handleBanPlayer(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, modUsername, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	pid := strings.TrimSpace(getString(data, "pid"))
	reason := truncStr(getString(data, "reason"), 512)
	if pid == "" {
		errResp(w, 400, "Player ID required")
		return
	}

	res, err := s.db.Exec("UPDATE players SET banned = 1, ban_reason = ?, banned_by = ? WHERE pid = ?",
		reason, modUsername, pid)
	if err != nil {
		errResp(w, 500, "Database error")
		return
	}
	rows, _ := res.RowsAffected()
	if rows == 0 {
		errResp(w, 404, "Player not found")
		return
	}

	// kill all active sessions
	s.db.Exec("DELETE FROM player_sessions WHERE pid = ?", pid)

	auditLog(s.db, "player_ban", modUsername, fmt.Sprintf("pid=%s reason=%s", pid, reason), ip)
	log.Printf("[mod] %s banned player %s", modUsername, pid)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// POST /mod/unban-player, unban an EA player account
func (s *masterState) handleUnbanPlayer(w http.ResponseWriter, r *http.Request) {
	ip := getRealIP(r, s.behindProxy)
	_, modUsername, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	pid := strings.TrimSpace(getString(data, "pid"))
	if pid == "" {
		errResp(w, 400, "Player ID required")
		return
	}

	s.db.Exec("UPDATE players SET banned = 0, ban_reason = '', banned_by = '' WHERE pid = ?", pid)
	auditLog(s.db, "player_unban", modUsername, fmt.Sprintf("pid=%s", pid), ip)
	jsonResp(w, 200, map[string]any{"ok": true})
}

// GET /mod/banned-players, list banned players
func (s *masterState) handleBannedPlayersList(w http.ResponseWriter, r *http.Request) {
	_, _, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	rows, err := s.db.Query(
		"SELECT pid, display_name, ban_reason, banned_by, last_seen FROM players WHERE banned = 1 ORDER BY last_seen DESC")
	if err != nil {
		errResp(w, 500, "Database error")
		return
	}
	defer rows.Close()

	var players []map[string]any
	for rows.Next() {
		var pid, name, reason, by string
		var lastSeen float64
		rows.Scan(&pid, &name, &reason, &by, &lastSeen)
		players = append(players, map[string]any{
			"pid": pid, "displayName": name, "reason": reason,
			"banned_by": by, "last_seen": lastSeen,
		})
	}
	if players == nil {
		players = []map[string]any{}
	}
	jsonResp(w, 200, map[string]any{"ok": true, "players": players})
}

// GET /mod/players, search/list players (for mod tools)
func (s *masterState) handlePlayersList(w http.ResponseWriter, r *http.Request) {
	_, _, _, ok := validateModToken(s.db, r)
	if !ok {
		errResp(w, 401, "Not authenticated")
		return
	}

	search := r.URL.Query().Get("q")
	var rows *sql.Rows
	var err error
	if search != "" {
		rows, err = s.db.Query(
			"SELECT pid, display_name, banned, last_seen FROM players WHERE display_name LIKE ? OR pid LIKE ? ORDER BY last_seen DESC LIMIT 50",
			"%"+search+"%", "%"+search+"%")
	} else {
		rows, err = s.db.Query(
			"SELECT pid, display_name, banned, last_seen FROM players ORDER BY last_seen DESC LIMIT 50")
	}
	if err != nil {
		errResp(w, 500, "Database error")
		return
	}
	defer rows.Close()

	var players []map[string]any
	for rows.Next() {
		var pid, name string
		var banned int
		var lastSeen float64
		rows.Scan(&pid, &name, &banned, &lastSeen)
		players = append(players, map[string]any{
			"pid": pid, "displayName": name, "banned": banned != 0, "last_seen": lastSeen,
		})
	}
	if players == nil {
		players = []map[string]any{}
	}
	jsonResp(w, 200, map[string]any{"ok": true, "players": players})
}

// validate ticket endpoint, for servers to verify a player ticket via http (optional)
func (s *masterState) handleValidateTicket(w http.ResponseWriter, r *http.Request) {
	data, err := readJSON(r, maxBodySize)
	if err != nil {
		errResp(w, 400, "Invalid JSON")
		return
	}

	raw := getString(data, "ticket")
	if raw == "" {
		errResp(w, 400, "Ticket required")
		return
	}

	payload, err := ticket.Validate(s.ticketSecret, raw)
	if err != nil {
		jsonResp(w, 200, map[string]any{"valid": false, "error": err.Error()})
		return
	}

	// also check if player is banned
	var banned int
	s.db.QueryRow("SELECT banned FROM players WHERE pid = ?", payload.PID).Scan(&banned)

	jsonResp(w, 200, map[string]any{
		"valid":       banned == 0,
		"pid":         payload.PID,
		"displayName": payload.DisplayName,
		"banned":      banned != 0,
	})
}

func getOrCreateTicketSecret() ([]byte, error) {
	path := "ticket_secret.bin"
	data, err := os.ReadFile(path)
	if err == nil && len(data) == 32 {
		return data, nil
	}
	secret := make([]byte, 32)
	rand.Read(secret)
	if err := os.WriteFile(path, secret, 0600); err != nil {
		return nil, fmt.Errorf("failed to write ticket secret: %w", err)
	}
	log.Printf("[auth] generated ticket signing secret at %s", path)
	return secret, nil
}
