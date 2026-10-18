# Mailbox backend

Small Go HTTP service and phone/desktop friendly page for composing messages.
Messages are stored in SQLite in `data/sqlite.db` by default. The database and
its schema are created automatically the first time the service starts.

## Secrets

The ignored `backend/.env` file contains `MAILBOX_ADMIN_PASSWORD` for the sender
page and `MAILBOX_DEVICE_TOKEN` for the device API. The app loads `.env` when
running locally. In deployment, pass these as environment variables or provide
the file with Docker's `--env-file`; do not copy `.env` into the image or commit
it. `.env.example` lists the required settings.

The admin password protects the web sender. The device token is a separate
bearer token and is required for both polling and marking mail read. Set the
token on the ESP32 as an `Authorization: Bearer <token>` header. The form uses
a short-lived server-side session cookie with `Secure`, `HttpOnly`, and
`SameSite=Strict` attributes.

## Run locally

```sh
cd backend
go run .
```

Open <http://localhost:8080>. The sender page uses `MAILBOX_ADMIN_PASSWORD`
from `.env`. The cookie is marked Secure, as it should be for the public HTTPS
deployment; browsers permit Secure cookies on localhost.

Set `MAILBOX_DB_PATH` to use a different SQLite file. If the database is new
and a sibling `mail.json` exists, the service imports its messages once into
SQLite and leaves the JSON file in place as a backup. `MAILBOX_LEGACY_JSON_FILE`
can point to a legacy JSON file in another location. `MAILBOX_DATA_FILE` is
also accepted as a legacy-file path for existing deployments.

## Run with Docker

From the repository root:

```sh
docker build -t mailbox-backend ./backend
docker run --rm --env-file backend/.env -p 127.0.0.1:8080:8080 \
  -v mailbox-data:/data mailbox-backend
```

The loopback port binding is for a TLS reverse proxy on the host. Keep the
backend behind HTTPS at `mailbox.awangyisu.com`; do not expose its plain HTTP
port directly to the internet. Persist `/data` to retain mail across restarts.

## Message size

Messages are limited to **255 UTF-8 bytes**. The embedded `content[256]` buffer
needs one byte for its terminating NUL, so the server enforces 255 bytes. The
form shows a live byte count, including the larger byte size of emoji and other
non-ASCII characters, and the server checks the limit again when submitted.

## Routes

- `GET /` — password protected sender page after sign-in.
- `POST /login`, `POST /logout` — sign in and out of the sender page.
- `POST /send` — enqueue a message from the signed-in form.
- `GET /healthz` — public health check.
- `GET /api/mail/next` — device bearer token required; returns the oldest unread
  message or `204 No Content`. Polling leaves the message queued.
- `POST /api/mail/{id}/read` — device bearer token required; marks a message
  read. Repeating the request is safe.

The sender password and device token are generated into the local ignored
`.env` for this checkout. Keep that file private. Login sessions are held in
memory and expire after 12 hours or when the server restarts. SQLite is stored
in the persistent `/data` Docker volume.
