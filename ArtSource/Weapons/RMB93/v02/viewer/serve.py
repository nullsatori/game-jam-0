"""Local-only asset review. Serves this v02 asset directory, never a parent."""
from http.server import ThreadingHTTPServer, SimpleHTTPRequestHandler
from pathlib import Path
from urllib.parse import unquote, urlsplit
import base64
import binascii
import json
import struct

ROOT = Path(__file__).resolve().parent.parent

class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=str(ROOT), **kwargs)

    def do_GET(self):
        path = unquote(urlsplit(self.path).path)
        if path == "/api/assets":
            data = {folder: [p.relative_to(ROOT).as_posix() for p in sorted((ROOT/folder).glob("*")) if p.is_file()]
                    for folder in ("exports", "textures")}
            body = json.dumps(data).encode()
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        resolved = (ROOT/path.lstrip("/")).resolve()
        if not resolved.is_relative_to(ROOT):
            self.send_error(403)
            return
        return super().do_GET()

    def do_POST(self):
        # Fixed output names only. No client path, shell, or filesystem selectors.
        if self.path != "/api/capture":
            self.send_error(404)
            return
        if self.headers.get("Host") != "127.0.0.1:8733" or self.headers.get("Origin", "http://127.0.0.1:8733") != "http://127.0.0.1:8733":
            self.send_error(403)
            return
        try:
            length = int(self.headers.get("Content-Length", "0"))
            if not 0 < length < 20_000_000 or self.headers.get("Content-Type", "").split(";")[0] != "application/json":
                raise ValueError("Expected bounded JSON capture")
            data = json.loads(self.rfile.read(length))
            if set(data) != {"png", "metrics"} or not isinstance(data["metrics"], dict):
                raise ValueError("Expected png and metrics only")
            prefix = "data:image/png;base64,"
            if not isinstance(data["png"], str) or not data["png"].startswith(prefix):
                raise ValueError("Expected PNG data URI")
            png = base64.b64decode(data["png"][len(prefix):], validate=True)
            if png[:8] != b"\x89PNG\r\n\x1a\n" or len(png) < 32 or png[12:16] != b"IHDR" or struct.unpack(">II", png[16:24]) != (1672, 941):
                raise ValueError("Capture must be 1672 by 941 PNG")
            metrics = json.dumps(data["metrics"], ensure_ascii=False, indent=2)
            if len(metrics) > 20000:
                raise ValueError("Metrics too large")
            dest = ROOT/"renders"
            dest.mkdir(exist_ok=True)
            (dest/"live_latest.png").write_bytes(png)
            (dest/"viewer_metrics.json").write_text(metrics, encoding="utf-8")
            body = b'{"saved":["renders/live_latest.png","renders/viewer_metrics.json"]}'
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
        except (ValueError, KeyError, TypeError, binascii.Error):
            self.send_error(400, "Invalid capture payload")

    def end_headers(self):
        self.send_header("Cache-Control", "no-cache")
        super().end_headers()

if __name__ == "__main__":
    assert ROOT.name == "v02" and (ROOT/"viewer"/"serve.py").is_file()
    print(f"Asset root: {ROOT}\nhttp://127.0.0.1:8733/viewer/", flush=True)
    ThreadingHTTPServer(("127.0.0.1", 8733), Handler).serve_forever()
