# Movie Server

A small C program that scans your movies folder, serves it over HTTP,
and shows your network IP so you can watch from any device on your LAN.

## Features
- Lists all video files with a clean title (parsed from the filename)
- Auto-generates thumbnails (via ffmpeg) — falls back to a placeholder
- Shows your local IP prominently so other devices can connect
- Auto-refreshes when you add/remove movies (no restart needed)
- Edit `www/index.html`, `www/style.css`, `www/app.js` to change the look

## Build
```
make
```
Requires gcc (MinGW) — already installed on this machine.

## Run
```
./movie_server
```
It prints:
- `Local:   http://localhost:8080`
- `Network: http://<your-ip>:8080`

Open that network URL on another device (phone/tablet/laptop) on the same Wi-Fi.

## Layout
```
movie_server.c   <- everything (scanner, HTTP server, file watcher)
www/
  index.html     <- movie grid + IP display  (EDIT THIS)
  style.css      <- styling                  (EDIT THIS)
  app.js         <- behavior                 (EDIT THIS)
movies/          <- put your video files here
cache/           <- generated thumbnails (auto-created)
```

## Supported formats
`.mp4 .mkv .avi .mov .wmv .flv .webm .m4v`

## Troubleshooting
- Port 8080 in use? Edit `PORT` at the top of `movie_server.c` and rebuild.
- No real thumbnails? ffmpeg must be installed (it is here).
- Firewall blocks remote access? Allow inbound on the printed port.