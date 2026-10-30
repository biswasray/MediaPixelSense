# MediaPixelSense

An image and video editor you drive through chat. Type an edit like *"trim to the first 5 seconds, make it black and white and add a caption"*. A local Qwen model turns it into a list of engine operations, and the C++ engine runs them through FFmpeg.

```
┌──────────── web (React) ────────────┐     ┌──────── server (Node) ────────┐     ┌──── PixelSenseEngine (C++) ────┐
│ File Manager │ Preview │ LLM Chat   │ ──▶ │ /api/files   file manager     │ ──▶ │ ops     operation registry     │
└─────────────────────────────────────┘     │ /media       range streaming  │     │ probe   ffprobe wrapper        │
                                            │ /api/chat ─▶ Ollama (Qwen) ──┐│     │ apply   validate → ffmpeg      │
                                            │   plan → run pipeline ◀──────┘│     └────────────────────────────────┘
                                            └───────────────────────────────┘
```

## How an edit works

1. The UI sends the message, the selected file and recent chat history to `POST /api/chat`.
2. The server probes the file through the engine. It builds a system prompt from the engine's operation list (`PixelSenseEngine ops`) plus the file's size and duration.
3. Qwen is called through Ollama with **structured output**. The JSON schema restricts `operations[].name` to real operation names, and video-only operations are hidden when the file is an image. This keeps small models from inventing operations.
4. Each planned step goes through `PixelSenseEngine apply`. The engine fills in defaults, clamps out-of-range numbers (and reports a warning), rejects bad values, and runs FFmpeg. Each step's output feeds the next step.
5. The final result is saved as a new file (`name_editN.ext`). Its parent and the steps used are recorded in `workspace/.pixelsense.json`, so originals are never overwritten and the preview can compare before and after.

## Operations

| Image and video | Video only |
|---|---|
| resize, crop, rotate, flip, brightness, contrast, saturation, hue, grayscale, sepia, invert, blur, sharpen, denoise, vignette, text, convert | trim, speed, reverse, mute, volume, fade, extract_frame, extract_audio, to_gif |

To add an operation, add an entry in `makeOperations()` in `PixelSenseEngine/src/Operations.cpp` and rebuild. The server and the LLM prompt pick it up automatically.

## Setup (Windows)

Requirements: Visual Studio with the C++ workload, Node 20+, FFmpeg on `PATH`, and [Ollama](https://ollama.com).

```powershell
# 1. Local model
winget install Ollama.Ollama
ollama pull qwen2.5:1.5b

# 2. Engine
.\build-engine.bat            # -> out\build\x64-release\PixelSenseEngine\PixelSenseEngine.exe

# 3. Server + UI
npm run setup
npm start                     # http://127.0.0.1:8787
```

For UI development, run `npm run dev:server` and `npm run dev:web` (Vite on http://localhost:5173, which proxies `/api` and `/media` to the server).

### Choosing the model

The default is `qwen2.5:1.5b`, the smallest size that follows multi-step instructions reliably. To use a different model:

```powershell
$env:OLLAMA_MODEL = "qwen2.5:0.5b"   # fastest, weaker at multi-step edits
$env:OLLAMA_MODEL = "qwen3:1.7b"     # thinking is switched off automatically
$env:OLLAMA_MODEL = "qwen2.5:3b"     # more accurate, slower
```

### Configuration

| Variable | Default |
|---|---|
| `PORT` | `8787` |
| `PSE_WORKSPACE` | `./workspace` |
| `PSE_ENGINE` | `out/build/x64-release/PixelSenseEngine/PixelSenseEngine.exe` |
| `OLLAMA_URL` | `http://127.0.0.1:11434` |
| `OLLAMA_MODEL` | `qwen2.5:1.5b` |
| `PSE_FFMPEG` / `PSE_FFPROBE` | `ffmpeg` / `ffprobe` from `PATH` |
| `PSE_FONT` | `C:/Windows/Fonts/arial.ttf` (font for the `text` operation) |

## Engine CLI

```
PixelSenseEngine ops
PixelSenseEngine probe photo.jpg
PixelSenseEngine apply crop photo.jpg out.jpg width=800 height=800
PixelSenseEngine apply trim clip.mp4 cut.mp4 start=2 end=5 --dry-run   # print the ffmpeg command only
```

Every command prints one JSON object. Failures set `"ok": false` and include an `"error"` message.

## API

| Method | Path | |
|---|---|---|
| GET | `/api/health` | engine and model status |
| GET | `/api/operations` | operation registry |
| GET / POST | `/api/files` | list files / upload (multipart `files`) |
| PATCH / DELETE | `/api/files/:name` | rename / delete |
| GET | `/api/files/:name/info` | probe info and edit history |
| GET | `/media/:name` | the file itself (supports range requests) |
| POST | `/api/chat` | `{ file, message, history }` → `{ reply, steps, output?, error? }` |
| POST | `/api/apply` | `{ file, operations: [{ name, params }] }`, runs edits without the LLM |
