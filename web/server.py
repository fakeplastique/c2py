import json
import os
import re
import subprocess
import sys
import tempfile
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WEB = ROOT / "web"
C2PY = ROOT / "build" / "c2py"
EXAMPLES = ROOT / "examples"
MAX_BODY = 64 * 1024
TIMEOUT = 2
ERROR_RE = re.compile(r"^[^:\n]*:(\d+):(\d+): error: (.*)$", re.M)


def run(cmd, cwd):
    try:
        p = subprocess.run(cmd, cwd=cwd, capture_output=True, text=True, timeout=TIMEOUT)
        return p.returncode, p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return None, "", ""


def errors_of(stage, stderr):
    return [{"stage": stage, "line": int(m[1]), "col": int(m[2]), "msg": m[3]}
            for m in ERROR_RE.finditer(stderr)]


def compile_source(source):
    res = {"ast": None, "python": "", "interp_out": "", "python_out": "",
           "errors": [], "exit_codes": {"interp": None, "python": None}}
    with tempfile.TemporaryDirectory() as tmp:
        Path(tmp, "input.c").write_text(source)
        code, out, err = run([str(C2PY), "input.c", "--ast-json", "--py", "out.py"], tmp)
        if code is None:
            res["errors"].append({"stage": "compile", "msg": "timeout"})
            return res
        if out.strip():
            res["ast"] = json.loads(out)
        res["errors"] += errors_of("compile", err)
        if code != 0:
            return res
        res["python"] = Path(tmp, "out.py").read_text()

        code, out, err = run([str(C2PY), "input.c", "--run"], tmp)
        res["interp_out"], res["exit_codes"]["interp"] = out, code
        res["errors"] += errors_of("interp", err) if code is not None else [
            {"stage": "interp", "msg": f"timeout after {TIMEOUT} s"}]

        code, out, err = run([sys.executable, "-I", "out.py"], tmp)
        res["python_out"], res["exit_codes"]["python"] = out, code
        if code is None:
            res["errors"].append({"stage": "python", "msg": f"timeout after {TIMEOUT} s"})
        elif err.strip():
            res["errors"].append({"stage": "python", "msg": err.strip().splitlines()[-1]})
    return res


class Handler(SimpleHTTPRequestHandler):
    def log_message(self, fmt, *args):
        pass

    def send_json(self, status, obj):
        body = json.dumps(obj).encode()
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        if self.path == "/api/examples":
            self.send_json(200, {p.stem: p.read_text() for p in sorted(EXAMPLES.glob("*.c"))})
        else:
            super().do_GET()

    def do_POST(self):
        if self.path != "/api/compile":
            return self.send_json(404, {"error": "not found"})
        length = int(self.headers.get("Content-Length") or 0)
        if length <= 0 or length > MAX_BODY:
            return self.send_json(413, {"error": f"body must be 1..{MAX_BODY} bytes"})
        try:
            source = json.loads(self.rfile.read(length))["source"]
            if not isinstance(source, str):
                raise TypeError
        except (ValueError, KeyError, TypeError):
            return self.send_json(400, {"error": "expected JSON {\"source\": string}"})
        self.send_json(200, compile_source(source))


def main():
    port = int(os.environ.get("PORT", 8000))
    server = ThreadingHTTPServer(("127.0.0.1", port), partial(Handler, directory=str(WEB)))
    print(f"c2py web UI: http://127.0.0.1:{server.server_address[1]}/", flush=True)
    try:
        server.serve_forever()
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
