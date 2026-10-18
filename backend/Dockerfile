FROM golang:1.22-alpine AS build
WORKDIR /src
RUN apk add --no-cache gcc musl-dev
COPY go.mod ./
COPY go.sum ./
ENV CGO_ENABLED=1
RUN go mod download
COPY main.go ./
RUN CGO_ENABLED=1 go build -trimpath -ldflags="-s -w" -o /out/mailbox-backend .

FROM alpine:3.20
RUN apk add --no-cache libgcc \
    && addgroup -S mailbox && adduser -S -G mailbox mailbox \
    && mkdir -p /data && chown mailbox:mailbox /data
WORKDIR /app
COPY --from=build /out/mailbox-backend /usr/local/bin/mailbox-backend
ENV MAILBOX_ADDR=:8080 MAILBOX_DB_PATH=/data/sqlite.db
VOLUME ["/data"]
EXPOSE 8080
USER mailbox
ENTRYPOINT ["/usr/local/bin/mailbox-backend"]
