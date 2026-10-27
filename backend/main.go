package main

import (
	"bufio"
	"crypto/rand"
	"crypto/sha256"
	"crypto/subtle"
	"database/sql"
	"encoding/hex"
	"encoding/json"
	"errors"
	"fmt"
	"html/template"
	"io"
	"log"
	"net/http"
	"os"
	"path/filepath"
	"strconv"
	"strings"
	"sync"
	"time"

	_ "github.com/mattn/go-sqlite3"
)

// The embedded content[256] buffer needs one byte for the terminating NUL.
const maxMailContentBytes = 255

const sessionCookieName = "__Host-mailbox_session"
const sessionLifetime = 12 * time.Hour

type mail struct {
	ID        string    `json:"id"`
	Content   string    `json:"content"`
	CreatedAt time.Time `json:"created_at"`
	Read      bool      `json:"read"`
}

type store struct {
	db *sql.DB
}

func openStore(path string) (*store, error) {
	legacyPath := filepath.Join(filepath.Dir(path), "mail.json")
	return openStoreWithLegacy(path, legacyPath)
}

func openStoreWithLegacy(path, legacyPath string) (*store, error) {
	if path == "" {
		return nil, errors.New("SQLite database path is empty")
	}
	if path != ":memory:" {
		if err := os.MkdirAll(filepath.Dir(path), 0o755); err != nil {
			return nil, fmt.Errorf("create database directory: %w", err)
		}
	}
	dsn := path
	if path != ":memory:" {
		dsn += "?_busy_timeout=5000&_journal_mode=WAL&_foreign_keys=on"
	}
	db, err := sql.Open("sqlite3", dsn)
	if err != nil {
		return nil, fmt.Errorf("open SQLite database: %w", err)
	}
	db.SetMaxOpenConns(1)
	if err := db.Ping(); err != nil {
		db.Close()
		return nil, fmt.Errorf("connect to SQLite database: %w", err)
	}
	if _, err := db.Exec(`
		CREATE TABLE IF NOT EXISTS mail (
			id TEXT PRIMARY KEY,
			content TEXT NOT NULL,
			created_at_ns INTEGER NOT NULL,
			is_read INTEGER NOT NULL DEFAULT 0 CHECK (is_read IN (0, 1))
		);
		CREATE INDEX IF NOT EXISTS mail_unread_order ON mail(is_read, created_at_ns, id);
		CREATE TABLE IF NOT EXISTS schema_migrations (
			name TEXT PRIMARY KEY,
			applied_at_ns INTEGER NOT NULL
		);
	`); err != nil {
		db.Close()
		return nil, fmt.Errorf("create SQLite schema: %w", err)
	}
	s := &store{db: db}
	if path != ":memory:" {
		if err := s.importLegacyJSON(legacyPath); err != nil {
			db.Close()
			return nil, err
		}
	}
	return s, nil
}

func (s *store) enqueue(content string) (mail, error) {
	id, err := newID()
	if err != nil {
		return mail{}, err
	}
	m := mail{ID: id, Content: content, CreatedAt: time.Now().UTC()}
	_, err = s.db.Exec(`INSERT INTO mail (id, content, created_at_ns, is_read) VALUES (?, ?, ?, 0)`,
		m.ID, m.Content, m.CreatedAt.UnixNano())
	if err != nil {
		return mail{}, fmt.Errorf("insert mail: %w", err)
	}
	return m, nil
}

func (s *store) nextUnread() (mail, bool, error) {
	var m mail
	var createdAt int64
	err := s.db.QueryRow(`SELECT id, content, created_at_ns, is_read FROM mail
		WHERE is_read = 0 ORDER BY created_at_ns ASC, id ASC LIMIT 1`).
		Scan(&m.ID, &m.Content, &createdAt, &m.Read)
	if errors.Is(err, sql.ErrNoRows) {
		return mail{}, false, nil
	}
	if err != nil {
		return mail{}, false, fmt.Errorf("query next unread mail: %w", err)
	}
	m.CreatedAt = time.Unix(0, createdAt).UTC()
	return m, true, nil
}

func (s *store) markRead(id string) (mail, bool, error) {
	tx, err := s.db.Begin()
	if err != nil {
		return mail{}, false, fmt.Errorf("begin mark-read transaction: %w", err)
	}
	defer tx.Rollback()
	if _, err := tx.Exec(`UPDATE mail SET is_read = 1 WHERE id = ?`, id); err != nil {
		return mail{}, false, fmt.Errorf("mark mail read: %w", err)
	}
	var m mail
	var createdAt int64
	err = tx.QueryRow(`SELECT id, content, created_at_ns, is_read FROM mail WHERE id = ?`, id).
		Scan(&m.ID, &m.Content, &createdAt, &m.Read)
	if errors.Is(err, sql.ErrNoRows) {
		return mail{}, false, nil
	}
	if err != nil {
		return mail{}, false, fmt.Errorf("read updated mail: %w", err)
	}
	m.CreatedAt = time.Unix(0, createdAt).UTC()
	if err := tx.Commit(); err != nil {
		return mail{}, false, fmt.Errorf("commit mark-read transaction: %w", err)
	}
	return m, true, nil
}

func (s *store) importLegacyJSON(path string) error {
	var imported bool
	err := s.db.QueryRow(`SELECT EXISTS(SELECT 1 FROM schema_migrations WHERE name = 'legacy_json_import')`).Scan(&imported)
	if err != nil {
		return fmt.Errorf("check legacy mail migration: %w", err)
	}
	if imported {
		return nil
	}
	data, err := os.ReadFile(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return fmt.Errorf("read legacy mail file: %w", err)
	}
	var messages []mail
	if err := json.Unmarshal(data, &messages); err != nil {
		return fmt.Errorf("decode legacy mail file: %w", err)
	}
	tx, err := s.db.Begin()
	if err != nil {
		return fmt.Errorf("begin legacy mail migration: %w", err)
	}
	defer tx.Rollback()
	statement, err := tx.Prepare(`INSERT INTO mail (id, content, created_at_ns, is_read) VALUES (?, ?, ?, ?)`)
	if err != nil {
		return fmt.Errorf("prepare legacy mail migration: %w", err)
	}
	defer statement.Close()
	for _, m := range messages {
		read := 0
		if m.Read {
			read = 1
		}
		createdAt := m.CreatedAt
		if createdAt.IsZero() {
			createdAt = time.Unix(0, 0).UTC()
		}
		if _, err := statement.Exec(m.ID, m.Content, createdAt.UTC().UnixNano(), read); err != nil {
			return fmt.Errorf("import legacy mail %q: %w", m.ID, err)
		}
	}
	if _, err := tx.Exec(`INSERT INTO schema_migrations (name, applied_at_ns) VALUES ('legacy_json_import', ?)`, time.Now().UTC().UnixNano()); err != nil {
		return fmt.Errorf("record legacy mail migration: %w", err)
	}
	if err := tx.Commit(); err != nil {
		return fmt.Errorf("commit legacy mail migration: %w", err)
	}
	log.Printf("Imported %d message(s) from legacy JSON file %s", len(messages), path)
	return nil
}

func newID() (string, error) {
	var b [16]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", fmt.Errorf("generate mail id: %w", err)
	}
	b[6] = (b[6] & 0x0f) | 0x40
	b[8] = (b[8] & 0x3f) | 0x80
	return fmt.Sprintf("%s-%s-%s-%s-%s",
		hex.EncodeToString(b[0:4]), hex.EncodeToString(b[4:6]),
		hex.EncodeToString(b[6:8]), hex.EncodeToString(b[8:10]),
		hex.EncodeToString(b[10:16])), nil
}

func newToken() (string, error) {
	var b [32]byte
	if _, err := rand.Read(b[:]); err != nil {
		return "", fmt.Errorf("generate random token: %w", err)
	}
	return hex.EncodeToString(b[:]), nil
}

type loginSession struct {
	csrf      string
	expiresAt time.Time
	flash     string
}

type sessionStore struct {
	mu       sync.Mutex
	sessions map[string]loginSession
}

func newSessionStore() *sessionStore {
	return &sessionStore{sessions: make(map[string]loginSession)}
}

func (s *sessionStore) create() (string, loginSession, error) {
	id, err := newToken()
	if err != nil {
		return "", loginSession{}, err
	}
	csrf, err := newToken()
	if err != nil {
		return "", loginSession{}, err
	}
	session := loginSession{csrf: csrf, expiresAt: time.Now().Add(sessionLifetime)}
	s.mu.Lock()
	s.sessions[id] = session
	s.mu.Unlock()
	return id, session, nil
}

func (s *sessionStore) get(id string) (loginSession, bool) {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, ok := s.sessions[id]
	if !ok {
		return loginSession{}, false
	}
	if time.Now().After(session.expiresAt) {
		delete(s.sessions, id)
		return loginSession{}, false
	}
	return session, true
}

func (s *sessionStore) delete(id string) {
	s.mu.Lock()
	delete(s.sessions, id)
	s.mu.Unlock()
}

func (s *sessionStore) setFlash(id, message string) {
	s.mu.Lock()
	session, ok := s.sessions[id]
	if ok {
		session.flash = message
		s.sessions[id] = session
	}
	s.mu.Unlock()
}

func (s *sessionStore) consumeFlash(id string) string {
	s.mu.Lock()
	defer s.mu.Unlock()
	session, ok := s.sessions[id]
	if !ok {
		return ""
	}
	message := session.flash
	session.flash = ""
	s.sessions[id] = session
	return message
}

type server struct {
	store         *store
	sessions      *sessionStore
	adminPassword string
	deviceToken   string
}

type pageData struct {
	Authenticated bool
	CSRFToken     string
	Error         string
	Success       string
	Content       string
	MaxBytes      int
}

var pageTemplate = template.Must(template.New("page").Parse(`<!doctype html>
<html lang="en">
<head>
  <meta charset="utf-8">
  <meta name="viewport" content="width=device-width, initial-scale=1">
  <title>Mailbox</title>
</head>
<body>
<main>
  <h1>Mailbox</h1>
  {{if .Error}}<p role="alert">{{.Error}}</p>{{end}}
  {{if .Success}}<p role="status">{{.Success}}</p>{{end}}
  {{if .Authenticated}}
    <h2>Send a message</h2>
    <form method="post" action="/send">
      <input type="hidden" name="csrf_token" value="{{.CSRFToken}}">
      <p><label for="content">Message (up to {{.MaxBytes}} UTF-8 bytes)</label></p>
      <p><textarea id="content" name="content" rows="6" cols="30" aria-describedby="byte-count" required>{{.Content}}</textarea></p>
      <p id="byte-count" aria-live="polite">Limit: {{.MaxBytes}} bytes</p>
      <p><button id="send-button" type="submit">Send</button></p>
    </form>
    <form method="post" action="/logout">
      <input type="hidden" name="csrf_token" value="{{.CSRFToken}}">
      <button type="submit">Log out</button>
    </form>
    <script>
      (() => {
        const field = document.getElementById('content');
        const count = document.getElementById('byte-count');
        const button = document.getElementById('send-button');
        const max = {{.MaxBytes}};
        const update = () => {
          const bytes = new TextEncoder().encode(field.value).length;
          count.textContent = bytes + ' / ' + max + ' bytes';
          button.disabled = bytes === 0 || bytes > max || field.value.trim().length === 0;
        };
        field.addEventListener('input', update);
        update();
      })();
    </script>
  {{else}}
    <h2>Sign in</h2>
    <form method="post" action="/login">
      <p>
        <label for="password">Password</label>
        <input id="password" name="password" type="password" autocomplete="current-password" required>
      </p>
      <button type="submit">Sign in</button>
    </form>
  {{end}}
</main>
</body>
</html>`))

func (s *server) ServeHTTP(w http.ResponseWriter, r *http.Request) {
	w.Header().Set("X-Content-Type-Options", "nosniff")
	w.Header().Set("Cache-Control", "no-store")
	w.Header().Set("Referrer-Policy", "no-referrer")

	switch {
	case r.URL.Path == "/healthz":
		if r.Method != http.MethodGet {
			methodNotAllowed(w, http.MethodGet)
			return
		}
		writeJSON(w, http.StatusOK, map[string]string{"status": "ok"})
	case r.URL.Path == "/":
		s.handleHome(w, r)
	case r.URL.Path == "/login":
		s.handleLogin(w, r)
	case r.URL.Path == "/logout":
		s.handleLogout(w, r)
	case r.URL.Path == "/send":
		s.handleSend(w, r)
	case r.URL.Path == "/api/mail/next":
		s.handleNextMail(w, r)
	case strings.HasPrefix(r.URL.Path, "/api/mail/") && strings.HasSuffix(r.URL.Path, "/read"):
		s.handleMarkRead(w, r)
	default:
		http.NotFound(w, r)
	}
}

func (s *server) handleHome(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodGet {
		methodNotAllowed(w, http.MethodGet)
		return
	}
	session, ok := s.sessionForRequest(r)
	if !ok {
		renderPage(w, http.StatusOK, pageData{MaxBytes: maxMailContentBytes})
		return
	}
	renderPage(w, http.StatusOK, pageData{
		Authenticated: true,
		CSRFToken:     session.csrf,
		Success:       s.sessions.consumeFlash(sessionIDFromRequest(r)),
		MaxBytes:      maxMailContentBytes,
	})
}

func (s *server) handleLogin(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, 4*1024)
	if err := r.ParseForm(); err != nil {
		renderPage(w, http.StatusBadRequest, pageData{Error: "Could not read the sign-in form."})
		return
	}
	if !constantTimeEqual(r.FormValue("password"), s.adminPassword) {
		renderPage(w, http.StatusUnauthorized, pageData{Error: "That password did not match."})
		return
	}
	id, _, err := s.sessions.create()
	if err != nil {
		log.Printf("create login session: %v", err)
		writeError(w, http.StatusInternalServerError, "could not start a session")
		return
	}
	setSessionCookie(w, id)
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

func (s *server) handleLogout(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	session, ok := s.sessionForRequest(r)
	if !ok {
		clearSessionCookie(w)
		http.Redirect(w, r, "/", http.StatusSeeOther)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, 4*1024)
	if err := r.ParseForm(); err != nil || !csrfValid(session, r.FormValue("csrf_token")) {
		writeError(w, http.StatusForbidden, "invalid form token")
		return
	}
	s.sessions.delete(sessionIDFromRequest(r))
	clearSessionCookie(w)
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

func (s *server) handleSend(w http.ResponseWriter, r *http.Request) {
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	session, ok := s.sessionForRequest(r)
	if !ok {
		http.Redirect(w, r, "/", http.StatusSeeOther)
		return
	}
	r.Body = http.MaxBytesReader(w, r.Body, 8*1024)
	if err := r.ParseForm(); err != nil {
		renderPage(w, http.StatusBadRequest, pageData{Authenticated: true, CSRFToken: session.csrf, Error: "Could not read the message form.", MaxBytes: maxMailContentBytes})
		return
	}
	content := r.FormValue("content")
	data := pageData{Authenticated: true, CSRFToken: session.csrf, Content: content, MaxBytes: maxMailContentBytes}
	if !csrfValid(session, r.FormValue("csrf_token")) {
		data.Error = "Your form expired. Refresh the page and try again."
		renderPage(w, http.StatusForbidden, data)
		return
	}
	if strings.TrimSpace(content) == "" {
		data.Error = "Write a message before sending."
		renderPage(w, http.StatusBadRequest, data)
		return
	}
	if len([]byte(content)) > maxMailContentBytes {
		data.Error = fmt.Sprintf("Your message is %d bytes over the %d-byte limit.", len([]byte(content))-maxMailContentBytes, maxMailContentBytes)
		renderPage(w, http.StatusBadRequest, data)
		return
	}
	if _, err := s.store.enqueue(content); err != nil {
		log.Printf("store mail: %v", err)
		data.Error = "Could not save the message. Please try again."
		renderPage(w, http.StatusInternalServerError, data)
		return
	}
	s.sessions.setFlash(sessionIDFromRequest(r), "Message sent to the mailbox.")
	http.Redirect(w, r, "/", http.StatusSeeOther)
}

func (s *server) handleNextMail(w http.ResponseWriter, r *http.Request) {
	if !s.deviceAuthorized(r) {
		writeError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	if r.Method != http.MethodGet {
		methodNotAllowed(w, http.MethodGet)
		return
	}
	m, ok, err := s.store.nextUnread()
	if err != nil {
		log.Printf("get next mail: %v", err)
		writeError(w, http.StatusInternalServerError, "could not read mail")
		return
	}
	if !ok {
		w.WriteHeader(http.StatusNoContent)
		return
	}
	writeJSON(w, http.StatusOK, m)
}

func (s *server) handleMarkRead(w http.ResponseWriter, r *http.Request) {
	if !s.deviceAuthorized(r) {
		writeError(w, http.StatusUnauthorized, "unauthorized")
		return
	}
	if r.Method != http.MethodPost {
		methodNotAllowed(w, http.MethodPost)
		return
	}
	const prefix, suffix = "/api/mail/", "/read"
	id := strings.TrimSuffix(strings.TrimPrefix(r.URL.Path, prefix), suffix)
	if id == "" || strings.Contains(id, "/") {
		http.NotFound(w, r)
		return
	}
	m, ok, err := s.store.markRead(id)
	if err != nil {
		log.Printf("mark mail read: %v", err)
		writeError(w, http.StatusInternalServerError, "could not update mail")
		return
	}
	if !ok {
		writeError(w, http.StatusNotFound, "mail not found")
		return
	}
	writeJSON(w, http.StatusOK, m)
}

func (s *server) sessionForRequest(r *http.Request) (loginSession, bool) {
	id := sessionIDFromRequest(r)
	if id == "" {
		return loginSession{}, false
	}
	return s.sessions.get(id)
}

func sessionIDFromRequest(r *http.Request) string {
	cookie, err := r.Cookie(sessionCookieName)
	if err != nil {
		return ""
	}
	return cookie.Value
}

func csrfValid(session loginSession, token string) bool {
	return token != "" && constantTimeEqual(session.csrf, token)
}

func (s *server) deviceAuthorized(r *http.Request) bool {
	const prefix = "Bearer "
	header := r.Header.Get("Authorization")
	if !strings.HasPrefix(header, prefix) {
		return false
	}
	return constantTimeEqual(strings.TrimPrefix(header, prefix), s.deviceToken)
}

func constantTimeEqual(a, b string) bool {
	ha, hb := sha256.Sum256([]byte(a)), sha256.Sum256([]byte(b))
	return subtle.ConstantTimeCompare(ha[:], hb[:]) == 1
}

func setSessionCookie(w http.ResponseWriter, id string) {
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookieName,
		Value:    id,
		Path:     "/",
		HttpOnly: true,
		Secure:   true,
		SameSite: http.SameSiteStrictMode,
		MaxAge:   int(sessionLifetime.Seconds()),
	})
}

func clearSessionCookie(w http.ResponseWriter) {
	http.SetCookie(w, &http.Cookie{
		Name:     sessionCookieName,
		Value:    "",
		Path:     "/",
		HttpOnly: true,
		Secure:   true,
		SameSite: http.SameSiteStrictMode,
		MaxAge:   -1,
	})
}

func renderPage(w http.ResponseWriter, status int, data pageData) {
	var body strings.Builder
	if err := pageTemplate.Execute(&body, data); err != nil {
		log.Printf("render page: %v", err)
		http.Error(w, "could not render page", http.StatusInternalServerError)
		return
	}
	w.Header().Set("Content-Type", "text/html; charset=utf-8")
	w.WriteHeader(status)
	_, _ = io.WriteString(w, body.String())
}

func methodNotAllowed(w http.ResponseWriter, allowed string) {
	w.Header().Set("Allow", allowed)
	writeError(w, http.StatusMethodNotAllowed, "method not allowed")
}

func writeError(w http.ResponseWriter, status int, message string) {
	writeJSON(w, status, map[string]string{"error": message})
}

func writeJSON(w http.ResponseWriter, status int, value any) {
	w.Header().Set("Content-Type", "application/json; charset=utf-8")
	w.WriteHeader(status)
	if err := json.NewEncoder(w).Encode(value); err != nil {
		log.Printf("write response: %v", err)
	}
}

func loadDotEnv(path string) error {
	file, err := os.Open(path)
	if errors.Is(err, os.ErrNotExist) {
		return nil
	}
	if err != nil {
		return err
	}
	defer file.Close()

	scanner := bufio.NewScanner(file)
	lineNumber := 0
	for scanner.Scan() {
		lineNumber++
		line := strings.TrimSpace(scanner.Text())
		if line == "" || strings.HasPrefix(line, "#") {
			continue
		}
		key, value, ok := strings.Cut(line, "=")
		if !ok || strings.TrimSpace(key) == "" {
			return fmt.Errorf("invalid .env entry on line %d", lineNumber)
		}
		key, value = strings.TrimSpace(key), strings.TrimSpace(value)
		if len(value) >= 2 && value[0] == '"' && value[len(value)-1] == '"' {
			unquoted, err := strconv.Unquote(value)
			if err != nil {
				return fmt.Errorf("invalid quoted value in .env on line %d", lineNumber)
			}
			value = unquoted
		} else if len(value) >= 2 && value[0] == '\'' && value[len(value)-1] == '\'' {
			value = value[1 : len(value)-1]
		}
		if _, exists := os.LookupEnv(key); !exists {
			if err := os.Setenv(key, value); err != nil {
				return fmt.Errorf("set .env variable %q: %w", key, err)
			}
		}
	}
	if err := scanner.Err(); err != nil {
		return fmt.Errorf("read .env: %w", err)
	}
	return nil
}

func requiredEnv(name string) (string, error) {
	value := strings.TrimSpace(os.Getenv(name))
	if value == "" {
		return "", fmt.Errorf("%s must be set", name)
	}
	return value, nil
}

func main() {
	if err := loadDotEnv(".env"); err != nil {
		log.Fatalf("load .env: %v", err)
	}
	deviceToken, err := requiredEnv("MAILBOX_DEVICE_TOKEN")
	if err != nil {
		log.Fatal(err)
	}
	adminPassword, err := requiredEnv("MAILBOX_ADMIN_PASSWORD")
	if err != nil {
		log.Fatal(err)
	}
	databasePath := os.Getenv("MAILBOX_DB_PATH")
	if databasePath == "" {
		databasePath = "./data/sqlite.db"
	}
	legacyJSONPath := os.Getenv("MAILBOX_LEGACY_JSON_FILE")
	if legacyJSONPath == "" {
		legacyJSONPath = os.Getenv("MAILBOX_DATA_FILE")
	}
	if legacyJSONPath == "" {
		legacyJSONPath = filepath.Join(filepath.Dir(databasePath), "mail.json")
	}
	addr := os.Getenv("MAILBOX_ADDR")
	if addr == "" {
		addr = ":8080"
	}
	mailStore, err := openStoreWithLegacy(databasePath, legacyJSONPath)
	if err != nil {
		log.Fatalf("open mail store: %v", err)
	}
	httpServer := &http.Server{
		Addr:              addr,
		Handler:           &server{store: mailStore, sessions: newSessionStore(), adminPassword: adminPassword, deviceToken: deviceToken},
		ReadHeaderTimeout: 5 * time.Second,
		ReadTimeout:       10 * time.Second,
		WriteTimeout:      10 * time.Second,
		IdleTimeout:       60 * time.Second,
	}
	log.Printf("mailbox backend listening on %s", addr)
	if err := httpServer.ListenAndServe(); err != nil && !errors.Is(err, http.ErrServerClosed) {
		log.Fatalf("serve HTTP: %v", err)
	}
}
