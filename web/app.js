const $ = (id) => document.getElementById(id);
const src = $("source"), gutter = $("gutter");

const CATEGORY = {
  program: "decl", function: "decl", params: "decl", param: "decl", var_decl: "decl", array_decl: "decl",
  block: "stmt", if: "stmt", while: "stmt", do_while: "stmt", for: "stmt", break: "stmt",
  continue: "stmt", return: "stmt", expr_stmt: "stmt", empty: "stmt", printf: "stmt",
  num: "leaf", var: "leaf",
};
const COLOR = (t) => `var(--${CATEGORY[t] || "expr"})`;

let errorLines = new Set();

function renderGutter() {
  const n = src.value.split("\n").length;
  gutter.innerHTML = Array.from({ length: n }, (_, i) =>
    `<div class="${errorLines.has(i + 1) ? "err" : ""}">${i + 1}</div>`).join("");
  gutter.scrollTop = src.scrollTop;
}

src.addEventListener("input", () => { errorLines.clear(); renderGutter(); });
src.addEventListener("scroll", () => { gutter.scrollTop = src.scrollTop; });
src.addEventListener("keydown", (e) => {
  if (e.key === "Tab") {
    e.preventDefault();
    src.setRangeText("    ", src.selectionStart, src.selectionEnd, "end");
    renderGutter();
  }
  if (e.key === "Enter" && (e.ctrlKey || e.metaKey)) { e.preventDefault(); compile(); }
});

function goto(line, col) {
  const lines = src.value.split("\n");
  let pos = 0;
  for (let i = 0; i < line - 1 && i < lines.length; i++) pos += lines[i].length + 1;
  const end = pos + (lines[line - 1] || "").length;
  src.focus();
  src.setSelectionRange(pos + Math.max(0, (col || 1) - 1), end);
  src.scrollTop = Math.max(0, (line - 4) * 20);
}

function selectTab(name) {
  document.querySelectorAll(".tabs [role=tab]").forEach((b) => {
    const on = b.dataset.tab === name;
    b.setAttribute("aria-selected", on);
    $("tab-" + b.dataset.tab).hidden = !on;
  });
}

document.querySelectorAll(".tabs [role=tab]").forEach((b) =>
  b.addEventListener("click", () => selectTab(b.dataset.tab)));

const esc = (s) => s.replace(/[&<>]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;" }[c]));

function highlightPython(code) {
  const re = /("(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|\b(def|return|if|elif|else|while|for|break|continue|pass|global|import|not|and|or|lambda|True|False|None|raise)\b|\b(\d+)\b|\b([A-Za-z_]\w*)(?=\()/g;
  let out = "", last = 0;
  for (const m of code.matchAll(re)) {
    out += esc(code.slice(last, m.index));
    const cls = m[1] ? "str" : m[2] ? "kw" : m[3] ? "num" : "fn";
    out += `<span class="${cls}">${esc(m[0])}</span>`;
    last = m.index + m[0].length;
  }
  return out + esc(code.slice(last));
}

function label(d) {
  const n = d.data;
  if (n.type === "num" || n.type === "array_decl") return n.name ? `${n.name}[${n.value}]` : String(n.value);
  if (n.type === "printf") return JSON.stringify(n.name);
  return n.op && n.name ? `${n.op} ${n.name}` : n.name || n.op || "";
}

const tree = { svg: null, g: null, zoom: null, root: null };

function drawTree(ast) {
  const host = $("tree");
  host.innerHTML = "";
  if (!ast) { host.innerHTML = '<p class="empty">No tree: the source did not parse.</p>'; return; }
  tree.svg = d3.select(host).append("svg");
  tree.g = tree.svg.append("g");
  tree.zoom = d3.zoom().scaleExtent([0.15, 3]).on("zoom", (e) => tree.g.attr("transform", e.transform));
  tree.svg.call(tree.zoom).on("dblclick.zoom", null);
  tree.root = d3.hierarchy(ast, (d) => d.children);
  tree.root.each((d) => { d.id = Math.random().toString(36).slice(2); });
  update(tree.root, true);
}

function update(source, fit) {
  const layout = d3.tree().nodeSize([26, 150]);
  layout(tree.root);
  const nodes = tree.root.descendants(), links = tree.root.links();
  const t = tree.svg.transition().duration(250);
  const move = (sel) => (fit ? sel : sel.transition(t));

  const link = tree.g.selectAll("path.link").data(links, (d) => d.target.id).join(
    (enter) => enter.append("path").attr("class", "link")
      .attr("d", () => d3.linkHorizontal()({ source: [source.y0 ?? source.y, source.x0 ?? source.x], target: [source.y0 ?? source.y, source.x0 ?? source.x] })),
    (u) => u,
    (exit) => exit.transition(t).attr("opacity", 0).remove(),
  );
  move(link).attr("d", d3.linkHorizontal().x((d) => d.y).y((d) => d.x));

  const node = tree.g.selectAll("g.node").data(nodes, (d) => d.id).join(
    (enter) => {
      const g = enter.append("g").attr("class", "node")
        .attr("transform", `translate(${source.y0 ?? source.y},${source.x0 ?? source.x})`)
        .on("click", (e, d) => {
          if (d.children) { d._children = d.children; d.children = null; }
          else if (d._children) { d.children = d._children; d._children = null; }
          update(d, false);
        });
      g.append("title");
      g.append("circle").attr("r", 6);
      const text = g.append("text").attr("x", 10).attr("dy", "0.32em");
      text.append("tspan").attr("class", "type").attr("font-weight", 600);
      text.append("tspan").attr("class", "detail").attr("dx", 6);
      return g;
    },
    (u) => u,
    (exit) => exit.transition(t).attr("opacity", 0).remove(),
  );

  node.classed("collapsed", (d) => !!d._children);
  node.select("title").text((d) => `${d.data.type} @ ${d.data.line}:${d.data.col}`);
  node.select("circle")
    .attr("stroke", (d) => COLOR(d.data.type))
    .attr("fill", (d) => (d._children ? COLOR(d.data.type) : "var(--card)"));
  node.select(".type").text((d) => d.data.type);
  node.select(".detail").text(label);
  move(node).attr("transform", (d) => `translate(${d.y},${d.x})`);

  nodes.forEach((d) => { d.x0 = d.x; d.y0 = d.y; });
  if (fit) fitTree(0);
}

function fitTree(duration = 300) {
  if (!tree.root) return;
  const box = tree.g.node().getBBox(), host = $("tree").getBoundingClientRect();
  if (!box.width || !host.width) return;
  const k = Math.min(1.2, 0.92 * Math.min(host.width / box.width, host.height / box.height));
  const tx = (host.width - box.width * k) / 2 - box.x * k;
  const ty = (host.height - box.height * k) / 2 - box.y * k;
  tree.svg.transition().duration(duration).call(tree.zoom.transform, d3.zoomIdentity.translate(tx, ty).scale(k));
}

function setAll(open) {
  if (!tree.root) return;
  tree.root.each((d) => {
    if (open && d._children) { d.children = d._children; d._children = null; }
    if (!open && d.depth >= 2 && d.children) { d._children = d.children; d.children = null; }
  });
  update(tree.root, false);
  setTimeout(() => fitTree(), 280);
}

$("expand").addEventListener("click", () => setAll(true));
$("collapse").addEventListener("click", () => setAll(false));
$("fit").addEventListener("click", () => fitTree());

function showOutput(id, text, code) {
  const pane = $(id);
  pane.textContent = text || "";
  if (code !== null && code !== undefined) {
    const s = document.createElement("span");
    s.className = "exit";
    s.textContent = `exit code ${code}`;
    pane.appendChild(s);
  }
}

function showErrors(errors) {
  const pane = $("tab-errors");
  pane.innerHTML = errors.length ? "" : '<p class="none">No errors.</p>';
  errors.forEach((e) => {
    const row = document.createElement("div");
    row.className = "error-row";
    row.innerHTML = `<span class="loc">${e.line ? `${e.line}:${e.col}` : "—"}</span><span class="stage">${e.stage}</span><span></span>`;
    row.lastChild.textContent = e.msg;
    if (e.line) row.addEventListener("click", () => goto(e.line, e.col));
    pane.appendChild(row);
  });
  const c = $("errcount");
  c.textContent = errors.length;
  c.classList.toggle("bad", errors.length > 0);
  errorLines = new Set(errors.filter((e) => e.line).map((e) => e.line));
  renderGutter();
}

async function compile() {
  const btn = $("compile"), status = $("status");
  btn.disabled = true;
  status.className = "pill";
  status.textContent = "compiling…";
  const t0 = performance.now();
  try {
    const r = await fetch("/api/compile", {
      method: "POST",
      headers: { "Content-Type": "application/json" },
      body: JSON.stringify({ source: src.value }),
    });
    const res = await r.json();
    if (!r.ok) throw new Error(res.error || r.statusText);
    const ms = Math.round(performance.now() - t0);
    drawTree(res.ast);
    $("tab-python").innerHTML = highlightPython(res.python || "");
    showOutput("tab-interp", res.interp_out, res.exit_codes.interp);
    showOutput("tab-pyout", res.python_out, res.exit_codes.python);
    showErrors(res.errors);
    const match = $("match");
    if (res.python) {
      const same = res.interp_out === res.python_out && res.exit_codes.interp === res.exit_codes.python;
      match.className = "match " + (same ? "ok" : "bad");
      match.textContent = same ? "✓ interpreter and python agree" : "✗ outputs differ";
    } else { match.className = "match"; match.textContent = ""; }
    status.className = "pill " + (res.errors.length ? "bad" : "ok");
    status.textContent = res.errors.length ? `${res.errors.length} error${res.errors.length > 1 ? "s" : ""}` : `ok · ${ms} ms`;
    if (res.errors.length) selectTab("errors");
    else if (!$("tab-errors").hidden) selectTab("interp");
  } catch (err) {
    status.className = "pill bad";
    status.textContent = "server error";
    showErrors([{ stage: "server", msg: String(err.message || err) }]);
    selectTab("errors");
  } finally {
    btn.disabled = false;
  }
}

$("compile").addEventListener("click", compile);

async function init() {
  const examples = await (await fetch("/api/examples")).json();
  const sel = $("examples");
  const names = Object.keys(examples);
  names.forEach((n) => sel.add(new Option(n + ".c", n)));
  sel.value = names.includes("fact") ? "fact" : names[0];
  sel.addEventListener("change", () => { src.value = examples[sel.value]; errorLines.clear(); renderGutter(); compile(); });
  src.value = examples[sel.value] || "";
  renderGutter();
  window.addEventListener("resize", () => fitTree(0));
  await compile();
}

init();
