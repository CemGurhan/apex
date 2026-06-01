#include "html.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

struct IntraRow {
    int run;
    int seed;
    double final_total_pnl;
    int64_t final_inventory;
    double sharpe_ratio;
    double max_drawdown;
};

struct InterRow {
    double median_pnl;
    double mean_pnl;
    double stdev_pnl;
    double win_rate;
    double worst_max_drawdown;
    double best_max_drawdown;
    double median_sharpe;
    double mean_sharpe;
};

std::vector<std::string> split(const std::string& s, char delim) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, delim)) out.push_back(item);
    return out;
}

std::vector<IntraRow> readIntra(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("failed to open: " + path);
    std::vector<IntraRow> rows;
    std::string line;
    std::getline(in, line); // skip header
    while (std::getline(in, line)) {
        if (line.empty()) continue;
        auto f = split(line, ',');
        if (f.size() < 6) continue;
        rows.push_back(IntraRow{
            .run             = std::stoi(f[0]),
            .seed            = std::stoi(f[1]),
            .final_total_pnl = std::stod(f[2]),
            .final_inventory = std::stoll(f[3]),
            .sharpe_ratio    = std::stod(f[4]),
            .max_drawdown    = std::stod(f[5]),
        });
    }
    return rows;
}

InterRow readInter(const std::string& path) {
    std::ifstream in(path);
    if (!in) throw std::runtime_error("failed to open: " + path);
    std::string header, data;
    std::getline(in, header);
    std::getline(in, data);
    auto f = split(data, ',');
    if (f.size() < 8) throw std::runtime_error("malformed inter_summary row: " + path);
    return InterRow{
        .median_pnl         = std::stod(f[0]),
        .mean_pnl           = std::stod(f[1]),
        .stdev_pnl          = std::stod(f[2]),
        .win_rate           = std::stod(f[3]),
        .worst_max_drawdown = std::stod(f[4]),
        .best_max_drawdown  = std::stod(f[5]),
        .median_sharpe      = std::stod(f[6]),
        .mean_sharpe        = std::stod(f[7]),
    };
}

constexpr const char* kHtmlTemplate = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Apex Sim Report</title>
<script src="https://cdn.jsdelivr.net/npm/chart.js"></script>
<style>
  :root {
    --bg: #ffffff;
    --fg: #1a1a1a;
    --muted: #6b7280;
    --card-bg: #f5f5f7;
    --border: #e5e7eb;
    --positive: #16a34a;
    --negative: #dc2626;
    --accent: #3b82f6;
  }
  @media (prefers-color-scheme: dark) {
    :root {
      --bg: #0a0a0a;
      --fg: #f5f5f5;
      --muted: #9ca3af;
      --card-bg: #161616;
      --border: #2a2a2a;
      --positive: #22c55e;
      --negative: #ef4444;
      --accent: #60a5fa;
    }
  }
  * { box-sizing: border-box; }
  body {
    background: var(--bg);
    color: var(--fg);
    font-family: -apple-system, BlinkMacSystemFont, "Segoe UI", Roboto, sans-serif;
    max-width: 1200px;
    margin: 0 auto;
    padding: 32px 24px;
    line-height: 1.5;
  }
  h1 { margin: 0 0 4px; font-size: 28px; }
  .subtitle { color: var(--muted); margin-bottom: 32px; font-size: 14px; }
  h2 { font-size: 18px; margin: 32px 0 12px; }
  .cards {
    display: grid;
    grid-template-columns: repeat(auto-fit, minmax(180px, 1fr));
    gap: 12px;
  }
  .card {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 16px;
  }
  .card .label {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
  }
  .card .value {
    font-size: 24px;
    font-weight: 600;
    margin-top: 6px;
    font-variant-numeric: tabular-nums;
  }
  .card .value.positive { color: var(--positive); }
  .card .value.negative { color: var(--negative); }
  .chart {
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 10px;
    padding: 16px;
    margin-bottom: 16px;
    height: 320px;
  }
  table {
    width: 100%;
    border-collapse: collapse;
    background: var(--card-bg);
    border: 1px solid var(--border);
    border-radius: 10px;
    overflow: hidden;
    font-variant-numeric: tabular-nums;
  }
  th, td {
    padding: 10px 14px;
    text-align: right;
    border-bottom: 1px solid var(--border);
  }
  th {
    font-size: 11px;
    color: var(--muted);
    text-transform: uppercase;
    letter-spacing: 0.5px;
    background: color-mix(in srgb, var(--card-bg) 90%, transparent);
  }
  th:first-child, td:first-child { text-align: left; }
  tr:last-child td { border-bottom: none; }
  .pos { color: var(--positive); }
  .neg { color: var(--negative); }
</style>
</head>
<body>
<h1>Sim Report</h1>
<div class="subtitle" id="meta"></div>

<h2>Summary</h2>
<div class="cards">
  <div class="card"><div class="label">Mean PnL</div><div class="value" id="mean_pnl"></div></div>
  <div class="card"><div class="label">Median PnL</div><div class="value" id="median_pnl"></div></div>
  <div class="card"><div class="label">PnL Stdev</div><div class="value" id="stdev_pnl"></div></div>
  <div class="card"><div class="label">Win Rate</div><div class="value" id="win_rate"></div></div>
  <div class="card"><div class="label">Mean Sharpe</div><div class="value" id="mean_sharpe"></div></div>
  <div class="card"><div class="label">Median Sharpe</div><div class="value" id="median_sharpe"></div></div>
  <div class="card"><div class="label">Worst Max Drawdown</div><div class="value" id="worst_max_drawdown"></div></div>
  <div class="card"><div class="label">Best Max Drawdown</div><div class="value" id="best_max_drawdown"></div></div>
</div>

<h2>Per-Run PnL</h2>
<div class="chart"><canvas id="pnl_chart"></canvas></div>

<h2>Per-Run Sharpe Ratio</h2>
<div class="chart"><canvas id="sharpe_chart"></canvas></div>

<h2>Per-Run Max Drawdown</h2>
<div class="chart"><canvas id="drawdown_chart"></canvas></div>

<h2>Per-Run Detail</h2>
<table id="runs_table">
  <thead><tr>
    <th>Run</th><th>Seed</th><th>Final PnL</th><th>Final Inventory</th><th>Sharpe</th><th>Max Drawdown</th>
  </tr></thead>
  <tbody></tbody>
</table>

<script>
const inter = __INTER__;
const intra = __INTRA__;

const css = getComputedStyle(document.documentElement);
const colPos = css.getPropertyValue('--positive').trim();
const colNeg = css.getPropertyValue('--negative').trim();
const colAccent = css.getPropertyValue('--accent').trim();
const colMuted = css.getPropertyValue('--muted').trim();
const colBorder = css.getPropertyValue('--border').trim();
const colFg = css.getPropertyValue('--fg').trim();

const fmt = (v, d = 2) => Number.isFinite(v) ? v.toFixed(d) : '-';

const setCard = (id, value, decimals = 2, sign = false) => {
  const el = document.getElementById(id);
  el.textContent = fmt(value, decimals);
  if (sign) el.classList.add(value >= 0 ? 'positive' : 'negative');
};

setCard('mean_pnl', inter.mean_pnl, 2, true);
setCard('median_pnl', inter.median_pnl, 2, true);
setCard('stdev_pnl', inter.stdev_pnl, 2);
document.getElementById('win_rate').textContent = fmt(inter.win_rate * 100, 1) + '%';
setCard('mean_sharpe', inter.mean_sharpe, 4, true);
setCard('median_sharpe', inter.median_sharpe, 4, true);
setCard('worst_max_drawdown', inter.worst_max_drawdown, 2);
setCard('best_max_drawdown', inter.best_max_drawdown, 2);

document.getElementById('meta').textContent =
  intra.length + ' run' + (intra.length === 1 ? '' : 's');

Chart.defaults.color = colFg;
Chart.defaults.borderColor = colBorder;

const labels = intra.map(r => 'Run ' + r.run);
const chartOpts = (yLabel) => ({
  responsive: true,
  maintainAspectRatio: false,
  plugins: { legend: { display: false } },
  scales: {
    x: { grid: { color: colBorder }, ticks: { color: colMuted } },
    y: {
      grid: { color: colBorder },
      ticks: { color: colMuted },
      title: { display: true, text: yLabel, color: colMuted },
    },
  },
});

new Chart(document.getElementById('pnl_chart'), {
  type: 'bar',
  data: {
    labels,
    datasets: [{
      data: intra.map(r => r.final_total_pnl),
      backgroundColor: intra.map(r => r.final_total_pnl >= 0 ? colPos : colNeg),
    }],
  },
  options: chartOpts('Final Total PnL'),
});

new Chart(document.getElementById('sharpe_chart'), {
  type: 'bar',
  data: {
    labels,
    datasets: [{
      data: intra.map(r => r.sharpe_ratio),
      backgroundColor: intra.map(r => r.sharpe_ratio >= 0 ? colPos : colNeg),
    }],
  },
  options: chartOpts('Sharpe Ratio'),
});

new Chart(document.getElementById('drawdown_chart'), {
  type: 'bar',
  data: {
    labels,
    datasets: [{
      data: intra.map(r => r.max_drawdown),
      backgroundColor: colAccent,
    }],
  },
  options: chartOpts('Max Drawdown'),
});

const tbody = document.querySelector('#runs_table tbody');
intra.forEach(r => {
  const tr = document.createElement('tr');
  const pnlCls = r.final_total_pnl >= 0 ? 'pos' : 'neg';
  const shCls = r.sharpe_ratio >= 0 ? 'pos' : 'neg';
  tr.innerHTML =
    '<td>' + r.run + '</td>' +
    '<td>' + r.seed + '</td>' +
    '<td class="' + pnlCls + '">' + fmt(r.final_total_pnl) + '</td>' +
    '<td>' + r.final_inventory + '</td>' +
    '<td class="' + shCls + '">' + fmt(r.sharpe_ratio, 4) + '</td>' +
    '<td>' + fmt(r.max_drawdown) + '</td>';
  tbody.appendChild(tr);
});
</script>
</body>
</html>
)HTML";

std::string replaceAll(std::string s, const std::string& from, const std::string& to) {
    auto pos = s.find(from);
    if (pos != std::string::npos) s.replace(pos, from.size(), to);
    return s;
}

std::string interJson(const InterRow& r) {
    std::ostringstream o;
    o << std::setprecision(std::numeric_limits<double>::max_digits10);
    o << "{"
      << "\"median_pnl\":" << r.median_pnl << ","
      << "\"mean_pnl\":" << r.mean_pnl << ","
      << "\"stdev_pnl\":" << r.stdev_pnl << ","
      << "\"win_rate\":" << r.win_rate << ","
      << "\"worst_max_drawdown\":" << r.worst_max_drawdown << ","
      << "\"best_max_drawdown\":" << r.best_max_drawdown << ","
      << "\"median_sharpe\":" << r.median_sharpe << ","
      << "\"mean_sharpe\":" << r.mean_sharpe
      << "}";
    return o.str();
}

std::string intraJson(const std::vector<IntraRow>& rows) {
    std::ostringstream o;
    o << std::setprecision(std::numeric_limits<double>::max_digits10);
    o << "[";
    for (size_t i = 0; i < rows.size(); ++i) {
        const auto& r = rows[i];
        o << "{"
          << "\"run\":" << r.run << ","
          << "\"seed\":" << r.seed << ","
          << "\"final_total_pnl\":" << r.final_total_pnl << ","
          << "\"final_inventory\":" << r.final_inventory << ","
          << "\"sharpe_ratio\":" << r.sharpe_ratio << ","
          << "\"max_drawdown\":" << r.max_drawdown
          << "}";
        if (i + 1 < rows.size()) o << ",";
    }
    o << "]";
    return o.str();
}

}

void generateHtml(const std::string& run_dir) {
    auto dir = std::filesystem::path(run_dir);
    auto intra = readIntra((dir / "intra_summary.csv").string());
    auto inter = readInter((dir / "inter_summary.csv").string());

    std::string html = kHtmlTemplate;
    html = replaceAll(html, "__INTER__", interJson(inter));
    html = replaceAll(html, "__INTRA__", intraJson(intra));

    auto out_path = (dir / "report.html").string();
    std::ofstream out(out_path);
    if (!out) throw std::runtime_error("failed to open for writing: " + out_path);
    out << html;
}
