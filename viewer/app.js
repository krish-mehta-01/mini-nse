// Mini NSE viewer: plays back a trace written by the C++ engine (mini_nse_trace). All the trading logic lives in
// the engine; this file only displays what the trace says and explains each step in words.
"use strict";

const PHASES = [
  ["CLOSED", "Before 9:00", "no orders yet"],
  ["PREOPEN", "9:00–9:05", "limit + market orders"],
  ["LIMIT_ONLY", "9:05 → random close", "limit orders only"],
  ["AUCTION", "9:08–9:10 close", "one opening price"],
  ["CONTINUOUS", "9:15 onwards", "normal market"],
];

const RULES = {
  MaxTradable: "Rule 1: the most shares can trade at this price.",
  MinImbalance: "Rule 2: tied on shares traded, and this price leaves the fewest shares unmatched.",
  NearestPreviousClose: "Rule 3: tied again, and this price is the closest to yesterday's close.",
  HalfwayPreviousClose: "Rule 3: two prices tie and yesterday's close sits exactly halfway between them, " +
                        "so the close itself becomes the opening price, even though nobody quoted it.",
  MarketOrdersOnly: "Only market orders were waiting, so they trade at yesterday's close.",
  NoPrice: "No price lets anyone trade (every buyer bids below every seller, or one side is empty).",
};

const state = { trace: null, step: 0, samples: [], id: null };
const $ = (id) => document.getElementById(id);
const rs = (p) => `₹${Math.floor(p / 100)}.${String(p % 100).padStart(2, "0")}`;  // prices are in paise
const n = (x) => x.toLocaleString("en-IN");
const esc = (s) => String(s).replace(/[&<>"]/g, (c) => ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" })[c]);
const who = (side, id) => `${side === "BUY" ? "buyer" : "seller"} #${id}`;
const priceText = (t) => { const [w, f = ""] = t.split("."); return Number(w) * 100 + Number(f.padEnd(2, "0")); };

// ---------- loading ----------

async function start() {
  try {
    state.samples = await (await fetch("traces/index.json")).json();
  } catch {
    state.samples = [];
  }
  const select = $("trace-select");
  select.innerHTML = state.samples.map((s) => `<option value="${esc(s.id)}">${esc(s.title)}</option>`).join("") ||
                     "<option>no samples: run viewer/make_traces.py</option>";
  select.onchange = () => openSample(select.value, 0);
  $("trace-file").onchange = (e) => {
    const file = e.target.files[0];
    if (file) file.text().then((text) => show(JSON.parse(text), "file", 0));
  };
  for (const [id, delta] of [["first", -1e9], ["prev", -1], ["next", 1], ["last", 1e9]]) $(id).onclick = () => go(delta);
  $("slider").oninput = (e) => goTo(Number(e.target.value));
  document.addEventListener("keydown", (e) => {
    if (e.target.tagName === "SELECT" || e.target.tagName === "INPUT") return;
    const delta = { ArrowRight: 1, ArrowLeft: -1, Home: -1e9, End: 1e9 }[e.key];
    if (delta) { e.preventDefault(); go(delta); }
  });
  const hash = new URLSearchParams(location.hash.slice(1));
  const wanted = state.samples.find((s) => s.id === hash.get("morning")) || state.samples[0];
  if (wanted) openSample(wanted.id, Math.max(0, Number(hash.get("step") || 1) - 1));
}

async function openSample(id, step) {
  const sample = state.samples.find((s) => s.id === id);
  $("trace-select").value = id;
  show(await (await fetch(`traces/${sample.file}`)).json(), id, step);
}

function show(trace, id, step) {
  state.trace = trace;
  state.id = id;
  $("slider").max = trace.steps.length - 1;
  goTo(step);
}

const go = (delta) => goTo(state.step + delta);

function goTo(step) {
  const last = state.trace.steps.length - 1;
  state.step = Math.min(Math.max(step, 0), last);
  if (state.id !== "file") history.replaceState(null, "", `#morning=${state.id}&step=${state.step + 1}`);
  render();
}

// ---------- rendering ----------

function render() {
  const { steps } = state.trace;
  const step = steps[state.step];
  const before = state.step > 0 ? steps[state.step - 1].book : { kind: "pre-open", orders: [] };
  $("position").textContent = `Step ${state.step + 1} of ${steps.length}`;
  $("slider").value = state.step;
  $("first").disabled = $("prev").disabled = state.step === 0;
  $("next").disabled = $("last").disabled = state.step === steps.length - 1;
  renderTimeline(step.phase);
  renderEvent(step, before);
  renderBook(step.book, before);
  renderRight(steps, step);
}

function renderTimeline(current) {
  const at = PHASES.findIndex(([id]) => id === current);
  $("timeline").innerHTML = PHASES.map(([id, when, what], k) =>
    `<div class="phase ${k < at ? "done" : ""} ${k === at ? "now" : ""}"><b>${when}</b>${what}</div>`).join("");
}

function renderEvent(step, before) {
  const traded = step.trades.reduce((sum, t) => sum + t.qty, 0);
  const badge = step.reject
    ? `<span class="badge rejected">✕ Rejected: ${esc(step.reject)}</span>`
    : `<span class="badge ok">✓ Accepted</span>` +
      (step.trades.length ? ` <span class="badge trade">⇄ ${step.trades.length} trade${step.trades.length > 1 ? "s" : ""}, ${n(traded)} shares</span>` : "");
  $("event").innerHTML = `<div class="line"><span class="where">line ${step.line}</span>` +
    `<span class="cmd">${esc(step.text)}</span>${badge}</div><p>${explain(step, before)}</p>`;
}

function explain(step, before) {
  const w = step.text.split(" ");
  if (step.reject) return rejectText(step.reject);
  if (w[0] === "PHASE") return phaseText(w[1], step);
  const id = Number(w[1]);
  if (w[0] === "CANCEL") return `Order #${id} is cancelled and leaves the book.`;
  if (w[0] === "MODIFY") {
    const old = before.orders.find((o) => o.id === id);
    const price = priceText(w[2]), qty = Number(w[3]);
    const samePrice = !old || old.type === "MARKET" || old.price === price;
    let text = `Order #${id} changes to ${n(qty)} shares${old && old.type === "MARKET" ? "" : ` at ${rs(price)}`}. `;
    text += samePrice && old && qty <= old.qty
      ? "Asking for less at the same price keeps its place in the queue."
      : "Any change other than asking for less sends it to the back of the queue at its price.";
    return text + tradesText(step, old ? old.side : "BUY", "LIMIT", price, qty);
  }
  // ADD
  const side = w[2], type = w[3], qty = Number(w[w.length - 1]);
  const price = type === "LIMIT" ? priceText(w[4]) : null;
  const wants = `${side === "BUY" ? "Buyer" : "Seller"} #${id} wants to ${side === "BUY" ? "buy" : "sell"} ${n(qty)} shares ` +
    (type === "MARKET" ? "at any price (a market order)" : `${side === "BUY" ? "paying up to" : "for at least"} ${rs(price)}`);
  if (step.phase === "PREOPEN" || step.phase === "LIMIT_ONLY") {
    return `${wants}. In pre-open nothing trades: the order just joins the book and waits for the auction.`;
  }
  return `${wants}. ` + tradesText(step, side, type, price, qty);
}

function tradesText(step, side, type, price, qty) {
  if (step.phase !== "CONTINUOUS") return "";
  const filled = step.trades.reduce((sum, t) => sum + t.qty, 0);
  let text = "";
  if (step.trades.length) {
    const parts = step.trades.map((t) => `${n(t.qty)} with ${who(side === "BUY" ? "SELL" : "BUY", side === "BUY" ? t.sell : t.buy)} at ${rs(t.price)}`);
    text += `It trades at once: ${parts.join(", ")}. Each trade is at the waiting order's price. `;
  } else {
    text += `Nobody on the other side will ${type === "MARKET" ? "trade at all" : "meet that price"}. `;
  }
  const left = qty - filled;
  if (left > 0) text += type === "MARKET" ? `The other ${n(left)} are cancelled: market orders never wait.` : `The other ${n(left)} wait in the book.`;
  return text;
}

function phaseText(phase, step) {
  if (phase === "PREOPEN") return "9:00: pre-open order entry opens. Orders are collected, but nothing trades yet, so " +
    "nobody can rush in on overnight news. One fair price is set later for everyone.";
  if (phase === "LIMIT_ONLY") return "9:05: market orders close. No new market orders are accepted, and the ones " +
    "already in are locked: they can't be changed or cancelled. Limit orders are still fine.";
  if (phase === "CONTINUOUS") return "9:15: the normal market opens. Every unfilled order moves over with its " +
    "original place in the queue, and from now on each new order trades the moment it arrives.";
  const a = step.auction;
  const result = a.price === null ? "no opening price: nothing trades." :
    `the opening price is <b>${rs(a.price)}</b> and <b>${n(a.quantity)}</b> shares trade, everyone at that one price.`;
  return `Order entry closes at a random moment between 9:08 and 9:10. The auction tries every price and ${result} ` +
    `${RULES[a.rule]}`;
}

function rejectText(reason) {
  const t = state.trace;
  return {
    WrongPhase: "The market isn't taking this kind of event in the current phase (for example during the 9:12–9:15 buffer).",
    BadTransition: "Phases must go in order: pre-open → limit only → auction → normal market.",
    InvalidOrder: "The quantity must be positive, and a limit order needs a price.",
    DuplicateId: "Order ids are unique for the whole day, even after the order is gone.",
    UnknownOrder: "No waiting order has that id: it may have filled or been cancelled already.",
    MarketOrdersClosed: "After 9:05 no new market orders are accepted, and existing ones can't be changed or cancelled.",
    OffTick: `Prices must be a multiple of the tick size, ${rs(t.tick)}.`,
    OutsideBand: t.band ? `The price is outside today's allowed band, ${rs(t.band[0])} to ${rs(t.band[1])}.` : "The price is outside the band.",
  }[reason] + " Nothing changes.";
}

function renderBook(book, before) {
  const title = { "pre-open": "Waiting orders: the pre-open book", "auction leftovers": "Left over after the auction",
                  "normal market": "Waiting orders: the normal market" }[book.kind];
  const old = new Map(before.orders.map((o) => [o.id, o]));
  const changed = (o) => !old.has(o.id) || old.get(o.id).qty !== o.qty || old.get(o.id).price !== o.price;
  const chip = (o) => `<span class="chip ${o.side === "BUY" ? "buy" : "sell"} ${changed(o) ? "changed" : ""}" ` +
    `title="${o.side === "BUY" ? "Buy" : "Sell"} order #${o.id}, arrival ${o.seq}">#${o.id} · ${n(o.qty)}</span>`;
  const orders = book.orders;
  if (!orders.length) {
    $("book").innerHTML = `<h2>${title}</h2><p class="empty">No orders are waiting.</p>`;
    return;
  }
  const market = orders.filter((o) => o.type === "MARKET");
  const prices = [...new Set(orders.filter((o) => o.type === "LIMIT").map((o) => o.price))].sort((a, b) => b - a);
  const buys = orders.filter((o) => o.side === "BUY" && o.type === "LIMIT");
  const sells = orders.filter((o) => o.side === "SELL" && o.type === "LIMIT");
  const bestBid = buys.length ? Math.max(...buys.map((o) => o.price)) : null;
  const bestAsk = sells.length ? Math.min(...sells.map((o) => o.price)) : null;
  let rows = "";
  if (market.length) {
    rows += `<tr><td class="buys">${market.filter((o) => o.side === "BUY").map(chip).join("")}</td>` +
            `<td class="price">any price<br><small class="muted">market</small></td>` +
            `<td>${market.filter((o) => o.side === "SELL").map(chip).join("")}</td></tr>`;
  }
  for (const p of prices) {
    const cls = p === bestBid ? "best-bid" : p === bestAsk ? "best-ask" : "";
    rows += `<tr class="${cls}"><td class="buys">${buys.filter((o) => o.price === p).map(chip).join("")}</td>` +
            `<td class="price">${rs(p)}</td><td>${sells.filter((o) => o.price === p).map(chip).join("")}</td></tr>`;
  }
  $("book").innerHTML = `<h2>${title}</h2><table class="ladder"><thead><tr><th style="text-align:right">Buyers</th>` +
    `<th>Price</th><th style="text-align:left">Sellers</th></tr></thead><tbody>${rows}</tbody></table>` +
    `<div class="legend"><span><i class="swatch" style="background:var(--buy)"></i>buy order (#id · shares)</span>` +
    `<span><i class="swatch" style="background:var(--sell)"></i>sell order</span>` +
    `<span><i class="swatch" style="background:#eda100"></i>changed in this step</span>` +
    `<span>At each price, the queue runs left to right: first come, first filled.</span></div>`;
}

function renderRight(steps, step) {
  const upto = steps.slice(0, state.step + 1);
  const auctionStep = steps.find((s) => s.auction);
  let html = "";
  if (step.auction) {
    html += auctionHtml(step.auction);
  } else if (auctionStep && upto.includes(auctionStep)) {
    const a = auctionStep.auction;
    html += `<h2>The opening auction</h2><p class="result">${a.price === null ? "No opening price" :
      `Opened at <strong>${rs(a.price)}</strong> · ${n(a.quantity)} shares`}</p>` +
      `<p class="why">${RULES[a.rule]} <a href="#" id="to-auction">See the auction step.</a></p>`;
  } else {
    html += `<h2>The opening auction</h2><p class="why">The opening price is decided when order entry closes` +
      (auctionStep ? ` (step ${steps.indexOf(auctionStep) + 1}). <a href="#" id="to-auction">Jump there.</a>` : ".") + `</p>`;
  }
  const trades = upto.flatMap((s) => s.trades.map((t) => ({ ...t, stage: s.stage, fresh: s === step })));
  html += `<h2 class="section-gap">Trades so far</h2>` + (trades.length
    ? `<ul class="trades">${trades.map((t) => `<li class="${t.fresh ? "fresh" : ""}"><span class="tag">${t.stage === "auction" ? "auction" : "9:15+"}</span>` +
        `buyer #${t.buy} ← seller #${t.sell}: <b>${n(t.qty)}</b> at ${rs(t.price)}</li>`).join("")}</ul>`
    : `<p class="empty">No trades yet.</p>`);
  $("right").innerHTML = html;
  const link = $("to-auction");
  if (link) link.onclick = (e) => { e.preventDefault(); goTo(steps.indexOf(auctionStep)); };
}

function auctionHtml(a) {
  const verdict = (r) => r.out_at === 1 ? "out: fewer shares can trade (rule 1)" :
    r.out_at === 2 ? "out: more left unmatched (rule 2)" : r.out_at === 3 ? "out: further from the close (rule 3)" :
    r.price === a.price ? "✓ opening price" : "tied: the close is halfway";
  const rows = a.table.map((r) => `<tr class="${r.out_at ? "out" : "winner"}"><td class="num">${rs(r.price)}</td>` +
    `<td class="num">${n(r.demand)}</td><td class="num">${n(r.supply)}</td><td class="num">${n(r.tradable)}</td>` +
    `<td class="num">${n(r.imbalance)}</td><td class="verdict">${verdict(r)}</td></tr>`).join("");
  const markets = a.market_buy || a.market_sell
    ? `<p class="why">Market orders (any price): ${n(a.market_buy)} to buy, ${n(a.market_sell)} to sell, counted at every price below.</p>` : "";
  return `<h2>The opening auction</h2>` +
    `<p class="result">${a.price === null ? "No opening price" : `Opens at <strong>${rs(a.price)}</strong> · ${n(a.quantity)} shares trade`}</p>` +
    `<p class="why">${RULES[a.rule]}</p>${markets}` +
    (a.table.length ? `<table class="auction"><thead><tr><th>Price</th><th>Buyers willing</th><th>Sellers willing</th>` +
      `<th>Can trade</th><th>Unmatched</th><th style="text-align:left">Result</th></tr></thead><tbody>${rows}</tbody></table>` +
      chart(a) : "");
}

// Demand falls and supply rises as the price goes up; the auction looks for where they cross.
function chart(a) {
  const rows = a.table, W = 560, H = 224, L = 52, R = 40, T = 36, B = 34;  // T leaves room for the price label
  const lo = Math.min(...rows.map((r) => r.price), a.price ?? Infinity);
  const hi = Math.max(...rows.map((r) => r.price), a.price ?? -Infinity);
  const top = Math.max(...rows.map((r) => Math.max(r.demand, r.supply)), 1);
  const x = (p) => L + (hi === lo ? (W - L - R) / 2 : (p - lo) / (hi - lo) * (W - L - R));
  const y = (q) => H - B - q / top * (H - T - B);
  const line = (key, colour) => `<polyline fill="none" stroke="${colour}" stroke-width="2" stroke-linejoin="round" ` +
    `points="${rows.map((r) => `${x(r.price)},${y(r[key])}`).join(" ")}"/>` +
    rows.map((r) => `<circle cx="${x(r.price)}" cy="${y(r[key])}" r="4.5" fill="${colour}" stroke="var(--surface)" stroke-width="2"/>`).join("");
  const every = Math.ceil(rows.length / 8);
  const ticks = rows.filter((_, k) => k % every === 0).map((r) =>
    `<text x="${x(r.price)}" y="${H - B + 16}" font-size="10.5" fill="var(--muted)" text-anchor="middle">${rs(r.price)}</text>`).join("");
  const grid = [0, 0.5, 1].map((f) => `<line x1="${L}" x2="${W - R}" y1="${y(top * f)}" y2="${y(top * f)}" stroke="var(--grid)"/>` +
    `<text x="${L - 6}" y="${y(top * f) + 4}" font-size="10.5" fill="var(--muted)" text-anchor="end">${n(Math.round(top * f))}</text>`).join("");
  const mark = a.price === null ? "" : `<line x1="${x(a.price)}" x2="${x(a.price)}" y1="${T - 8}" y2="${H - B}" stroke="var(--ink)" stroke-width="1"/>` +
    `<text x="${x(a.price) + (x(a.price) > W - 120 ? -6 : 6)}" y="${T - 12}" font-size="11" font-weight="600" fill="var(--ink)" ` +
    `text-anchor="${x(a.price) > W - 120 ? "end" : "start"}">opens at ${rs(a.price)}</text>`;
  return `<div class="legend"><span><i class="swatch" style="background:var(--buy)"></i>buyers willing (demand)</span>` +
    `<span><i class="swatch" style="background:var(--sell)"></i>sellers willing (supply)</span></div>` +
    `<svg class="chart" viewBox="0 0 ${W} ${H}" width="100%" role="img" aria-label="Demand and supply at each price">` +
    `${grid}${ticks}${line("demand", "var(--buy)")}${line("supply", "var(--sell)")}${mark}</svg>`;
}

start();
