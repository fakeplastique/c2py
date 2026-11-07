import glob
import json
import os
import re
import socket
import subprocess
import sys
import tempfile
import time
import unittest
import urllib.error
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
C2PY = str(ROOT / "build" / "c2py")
EXAMPLES = sorted(ROOT.glob("examples/*.c"))
ERRORS = sorted(ROOT.glob("tests/errors/*.c"))


def c2py(*args, timeout=10):
    return subprocess.run([C2PY, *map(str, args)], capture_output=True, text=True, timeout=timeout)


def expected(example):
    text = example.with_suffix(".out").read_text()
    out, code = text.rsplit("exit ", 1)
    return out, int(code)


def write_tmp(source):
    f = tempfile.NamedTemporaryFile("w", suffix=".c", delete=False)
    f.write(source)
    f.close()
    return f.name


class Cli(unittest.TestCase):
    def test_01_backends_agree_with_gcc(self):
        for ex in EXAMPLES:
            with self.subTest(ex.name):
                out, code = expected(ex)
                interp = c2py(ex, "--run")
                gen = c2py(ex, "--py", "-")
                py = subprocess.run([sys.executable, "-I", "-"], input=gen.stdout,
                                    capture_output=True, text=True, timeout=10)
                self.assertEqual((interp.stdout, interp.returncode), (out, code))
                self.assertEqual((py.stdout, py.returncode), (out, code))

    def test_02_ast_json(self):
        def walk(n):
            self.assertIn("type", n)
            self.assertIn("line", n)
            for c in n["children"]:
                walk(c)
        for ex in EXAMPLES:
            with self.subTest(ex.name):
                ast = json.loads(c2py(ex, "--ast-json").stdout)
                self.assertEqual(ast["type"], "program")
                walk(ast)

    def test_03_syntax_error_location(self):
        r = c2py(write_tmp("int main( { }\n"))
        self.assertEqual(r.returncode, 1)
        self.assertRegex(r.stderr, r":\d+:\d+: error: syntax error, unexpected")

    def test_04_no_error_cascade(self):
        r = c2py(write_tmp("int main() { a = 1; a = a + 1; b = a; return b + a; }\n"))
        self.assertEqual(r.returncode, 1)
        self.assertEqual(len(r.stderr.strip().splitlines()), 2, r.stderr)

    def test_05_step_limit(self):
        t = time.monotonic()
        r = c2py(write_tmp("int main() { while (1); return 0; }\n"), "--run", timeout=5)
        self.assertLess(time.monotonic() - t, 5)
        self.assertEqual(r.returncode, 1)
        self.assertIn("step limit exceeded", r.stderr)

    def test_06_trace(self):
        r = c2py(EXAMPLES[0], "--trace")
        self.assertIn("Shifting token", r.stderr)
        self.assertIn("Reducing stack", r.stderr)

    def test_07_conflicts_demo(self):
        r = subprocess.run(["make", "-s", "conflicts"], cwd=ROOT, capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("shift/reduce", r.stdout)
        self.assertIn('"else"', r.stdout)

    def test_15_error_cases(self):
        for f in ERRORS:
            with self.subTest(f.name):
                r = c2py(f)
                self.assertEqual(r.returncode, 1)
                self.assertIn(f.with_suffix(".expected").read_text().strip(), r.stderr)


class Web(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.proc = subprocess.Popen([sys.executable, str(ROOT / "web" / "server.py")],
                                    env={**os.environ, "PORT": "0"}, stdout=subprocess.PIPE, text=True)
        cls.port = int(re.search(r":(\d+)/", cls.proc.stdout.readline())[1])
        cls.base = f"http://127.0.0.1:{cls.port}"

    @classmethod
    def tearDownClass(cls):
        cls.proc.terminate()
        cls.proc.wait()
        cls.proc.stdout.close()

    def get(self, path):
        try:
            with urllib.request.urlopen(self.base + path, timeout=5) as r:
                return r.status, r.headers.get("Content-Type", ""), r.read()
        except urllib.error.HTTPError as e:
            e.close()
            return e.code, "", b""

    def compile(self, source):
        req = urllib.request.Request(self.base + "/api/compile", json.dumps({"source": source}).encode(),
                                     {"Content-Type": "application/json"})
        with urllib.request.urlopen(req, timeout=10) as r:
            return json.load(r)

    def test_08_static_assets_local(self):
        status, ctype, body = self.get("/")
        self.assertEqual(status, 200)
        self.assertIn("text/html", ctype)
        self.assertNotRegex(body.decode(), r"(src|href)=\"https?://")
        for asset in ("/d3.v7.min.js", "/app.js", "/style.css"):
            self.assertEqual(self.get(asset)[0], 200, asset)

    def test_09_compile_example(self):
        ex = ROOT / "examples" / "fact.c"
        res = self.compile(ex.read_text())
        out, code = expected(ex)
        self.assertEqual(res["errors"], [])
        self.assertEqual(res["interp_out"], out)
        self.assertEqual(res["python_out"], out)
        self.assertEqual(res["exit_codes"], {"interp": code, "python": code})
        self.assertEqual(res["ast"]["type"], "program")
        self.assertIn("def fact(n):", res["python"])

    def test_10_compile_syntax_error(self):
        res = self.compile("int main() { return 1 + ; }")
        self.assertEqual(res["python"], "")
        self.assertTrue(res["errors"])
        self.assertEqual((res["errors"][0]["line"], res["errors"][0]["col"]), (1, 25))

    def test_11_infinite_loop_does_not_hang(self):
        t = time.monotonic()
        res = self.compile("int main() { int i = 0; while (1) i++; return 0; }")
        self.assertLess(time.monotonic() - t, 8)
        self.assertTrue(any("limit" in e["msg"] or "timeout" in e["msg"] for e in res["errors"]), res["errors"])
        self.assertEqual(self.get("/")[0], 200)

    def test_12_path_traversal(self):
        with socket.create_connection(("127.0.0.1", self.port), timeout=5) as s:
            s.sendall(b"GET /../Makefile HTTP/1.0\r\n\r\n")
            reply = s.recv(4096).decode(errors="replace")
        self.assertIn(" 404 ", reply.splitlines()[0])
        self.assertEqual(self.get("/%2e%2e/Makefile")[0], 404)

    def test_13_loopback_only(self):
        try:
            with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as s:
                s.connect(("192.0.2.1", 9))
                ip = s.getsockname()[0]
        except OSError:
            self.skipTest("no external interface")
        if ip.startswith("127."):
            self.skipTest("no external interface")
        with self.assertRaises(OSError):
            socket.create_connection((ip, self.port), timeout=2).close()

    def test_14_ui_renders_tree(self):
        shells = sorted(glob.glob(os.path.expanduser("~/.cache/ms-playwright/chromium_headless_shell-*/*/chrome-headless-shell")))
        if not shells:
            self.skipTest("headless chromium not found")
        r = subprocess.run([shells[-1], "--no-sandbox", "--virtual-time-budget=5000", "--dump-dom", self.base + "/"],
                           capture_output=True, text=True, timeout=60)
        dom = r.stdout
        self.assertIn("<svg", dom)
        self.assertGreaterEqual(dom.count('class="node'), 5)
        self.assertIn("10! = 3628800", dom)


if __name__ == "__main__":
    unittest.main(verbosity=2)
