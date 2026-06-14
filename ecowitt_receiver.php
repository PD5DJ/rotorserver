<?php
// =============================================================================
// ecowitt_receiver.php — Ecowitt data receiver + real-time display
// POST     → saves all fields to ecowitt_data.json (same directory)
// GET      → serves HTML page that auto-refreshes every 5 s
// GET?json → returns raw JSON (used by the page's JavaScript)
// GET?fragment → returns only the table HTML (used by JS auto-refresh)
// =============================================================================

$DATA_FILE = __DIR__ . '/ecowitt_data.json';

// ── Unit conversions ─────────────────────────────────────────────────────────
function f_to_c($f)    { return round(($f - 32) * 5 / 9, 1); }
function mph_to_ms($v) { return round($v * 0.44704, 1); }
function inhg_to_hpa($v) { return round($v * 33.8639, 1); }
function in_to_mm($v)  { return round($v * 25.4, 1); }

function deg_to_compass($d) {
    $dirs = ['N','NNE','NE','ENE','E','ESE','SE','SSE',
             'S','SSW','SW','WSW','W','WNW','NW','NNW'];
    return $dirs[round($d / 22.5) % 16];
}

// ── Field definitions: [label, unit, conversion fn or null] ──────────────────
function get_fields() {
    return [
        // Station info
        '_received'      => ['Last received',        '',    null],
        'stationtype'    => ['Station type',         '',    null],
        'dateutc'        => ['Date UTC',             '',    null],
        'runtime'        => ['Runtime',              's',   null],
        // Indoor
        'tempinf'        => ['Indoor temp',     '°F → °C', fn($v) => f_to_c($v) . ' °C  (' . $v . ' °F)'],
        'humidityin'     => ['Indoor humidity',      '%',   null],
        // Outdoor
        'tempf'          => ['Outdoor temp',    '°F → °C', fn($v) => f_to_c($v) . ' °C  (' . $v . ' °F)'],
        'humidity'       => ['Outdoor humidity',     '%',   null],
        // Pressure
        'baromrelin'     => ['Barometer (rel)', 'inHg → hPa', fn($v) => inhg_to_hpa($v) . ' hPa  (' . $v . ' inHg)'],
        'baromabsin'     => ['Barometer (abs)', 'inHg → hPa', fn($v) => inhg_to_hpa($v) . ' hPa  (' . $v . ' inHg)'],
        // Wind
        'winddir'        => ['Wind direction',       '°',   fn($v) => $v . '°  ' . deg_to_compass($v)],
        'windspeedmph'   => ['Wind speed',     'mph → m/s', fn($v) => mph_to_ms($v) . ' m/s  (' . $v . ' mph)'],
        'windgustmph'    => ['Wind gust',      'mph → m/s', fn($v) => mph_to_ms($v) . ' m/s  (' . $v . ' mph)'],
        'maxdailygust'   => ['Max daily gust', 'mph → m/s', fn($v) => mph_to_ms($v) . ' m/s  (' . $v . ' mph)'],
        'windspdmph_avg2m'  => ['Wind speed (2m avg)', 'mph → m/s', fn($v) => mph_to_ms($v) . ' m/s'],
        'winddir_avg10m'    => ['Wind dir (10m avg)',  '°',   fn($v) => $v . '°  ' . deg_to_compass($v)],
        // Rain
        'rainratein'     => ['Rain rate',    'in/h → mm/h', fn($v) => in_to_mm($v) . ' mm/h'],
        'eventrainin'    => ['Rain (event)',   'in → mm',   fn($v) => in_to_mm($v) . ' mm'],
        'hourlyrainin'   => ['Rain (hourly)',  'in → mm',   fn($v) => in_to_mm($v) . ' mm'],
        'dailyrainin'    => ['Rain (daily)',   'in → mm',   fn($v) => in_to_mm($v) . ' mm'],
        'weeklyrainin'   => ['Rain (weekly)',  'in → mm',   fn($v) => in_to_mm($v) . ' mm'],
        'monthlyrainin'  => ['Rain (monthly)', 'in → mm',  fn($v) => in_to_mm($v) . ' mm'],
        'totalrainin'    => ['Rain (total)',   'in → mm',   fn($v) => in_to_mm($v) . ' mm'],
        // Solar / UV
        'solarradiation' => ['Solar radiation',      'W/m²', null],
        'uv'             => ['UV index',              '',    null],
        // Internal
        'heap'           => ['Heap (free)',          'bytes', null],
    ];
}

function render_value($key, $val, $fields_def) {
    if (isset($fields_def[$key]) && $fields_def[$key][2] !== null) {
        try { return ($fields_def[$key][2])($val); } catch (Exception $e) {}
    }
    $unit = isset($fields_def[$key]) ? $fields_def[$key][1] : '';
    return htmlspecialchars($val) . ($unit ? '  <span class="unit">' . $unit . '</span>' : '');
}

function build_table($data, $fields_def) {
    $sections = [
        'STATION'    => ['_received','stationtype','dateutc','runtime','heap'],
        'INDOOR'     => ['tempinf','humidityin'],
        'OUTDOOR'    => ['tempf','humidity'],
        'PRESSURE'   => ['baromrelin','baromabsin'],
        'WIND'       => ['winddir','windspeedmph','windgustmph','maxdailygust',
                         'windspdmph_avg2m','winddir_avg10m'],
        'RAIN'       => ['rainratein','eventrainin','hourlyrainin','dailyrainin',
                         'weeklyrainin','monthlyrainin','totalrainin'],
        'SOLAR / UV' => ['solarradiation','uv'],
    ];

    $shown = [];
    $html  = '<table><tr><th>Field</th><th>Value</th></tr>';

    foreach ($sections as $sec => $keys) {
        $any = false;
        foreach ($keys as $k) if (isset($data[$k])) { $any = true; break; }
        if (!$any) continue;
        $html .= '<tr class="section"><td colspan="2">' . $sec . '</td></tr>';
        foreach ($keys as $k) {
            if (!isset($data[$k])) continue;
            $shown[] = $k;
            $label   = isset($fields_def[$k]) ? $fields_def[$k][0] : $k;
            $html   .= '<tr><td class="label">' . htmlspecialchars($label) . '</td>'
                     . '<td class="value">' . render_value($k, $data[$k], $fields_def) . '</td></tr>';
        }
    }

    // Extra fields not in the definition (future firmware fields etc.)
    $extra = array_diff(array_keys($data), $shown);
    if ($extra) {
        $html .= '<tr class="section"><td colspan="2">OTHER</td></tr>';
        foreach ($extra as $k) {
            $html .= '<tr class="extra"><td class="label unknown">' . htmlspecialchars($k) . '</td>'
                   . '<td class="value unknown">' . htmlspecialchars($data[$k]) . '</td></tr>';
        }
    }

    $html .= '</table>';
    return $html;
}

// ── Handle POST from Ecowitt / N1MM Rotor Server forwarder ───────────────────
if ($_SERVER['REQUEST_METHOD'] === 'POST') {
    $fields = $_POST;
    if (empty($fields)) {
        // also accept raw body (application/x-www-form-urlencoded via --data-binary)
        parse_str(file_get_contents('php://input'), $fields);
    }
    $fields['_received'] = date('Y-m-d H:i:s');
    file_put_contents($DATA_FILE, json_encode($fields, JSON_PRETTY_PRINT));
    http_response_code(200);
    exit;
}

// ── Return JSON for AJAX polling ─────────────────────────────────────────────
if (isset($_GET['json'])) {
    header('Content-Type: application/json');
    echo file_exists($DATA_FILE) ? file_get_contents($DATA_FILE) : '{}';
    exit;
}

// ── Return table HTML fragment for JS auto-refresh ───────────────────────────
if (isset($_GET['fragment'])) {
    header('Content-Type: text/html; charset=UTF-8');
    $data = file_exists($DATA_FILE)
        ? json_decode(file_get_contents($DATA_FILE), true)
        : [];
    if (empty($data)) {
        echo '<div class="nodata">No data received yet.</div>';
    } else {
        echo build_table($data, get_fields());
    }
    exit;
}

// ── Load current data for initial page render ─────────────────────────────────
$data   = file_exists($DATA_FILE)
    ? json_decode(file_get_contents($DATA_FILE), true)
    : [];
$FIELDS = get_fields();
?>
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<title>Ecowitt Weather Station</title>
<style>
  body        { font-family: monospace; background: #1a1a2e; color: #e0e0e0;
                margin: 0; padding: 20px; }
  h1          { color: #7aade0; margin-bottom: 4px; font-size: 1.3em; }
  .subtitle   { color: #888; font-size: 0.85em; margin-bottom: 20px; }
  table       { border-collapse: collapse; width: 100%; max-width: 680px; }
  th          { background: #16213e; color: #7aade0; text-align: left;
                padding: 8px 12px; font-size: 0.85em; letter-spacing: 0.05em; }
  td          { padding: 6px 12px; border-bottom: 1px solid #2a2a4a;
                font-size: 0.92em; }
  td.label    { color: #aaaacc; width: 200px; }
  td.value    { color: #f0f0f0; font-weight: bold; }
  .unit       { color: #888; font-weight: normal; font-size: 0.85em; }
  .unknown    { color: #555; }
  .nodata     { color: #e74c3c; padding: 20px 0; }
  .status     { color: #27ae60; font-size: 0.8em; margin-top: 10px; }
  .extra      { background: #111128; }
  .section    { background: #16213e; }
  .section td { color: #7aade0; font-weight: bold; padding: 10px 12px 4px;
                border-bottom: none; font-size: 0.8em; letter-spacing: 0.08em; }
</style>
</head>
<body>
<h1>Ecowitt Weather Station</h1>
<div class="subtitle">Auto-refresh every 5 s &nbsp;|&nbsp; All values converted to metric</div>

<div id="content">
<?php if (empty($data)): ?>
  <div class="nodata">No data received yet.</div>
<?php else: ?>
  <?php echo build_table($data, $FIELDS); ?>
<?php endif; ?>
</div>

<div class="status" id="status">Live</div>

<script>
async function refresh() {
    try {
        const r   = await fetch('?json&_=' + Date.now());
        const obj = await r.json();
        if (Object.keys(obj).length === 0) return;

        const r2  = await fetch('?fragment&_=' + Date.now());
        const txt = await r2.text();
        document.getElementById('content').innerHTML = txt;
        document.getElementById('status').textContent =
            'Updated ' + new Date().toLocaleTimeString();
    } catch(e) {
        document.getElementById('status').textContent = 'Fetch error: ' + e;
    }
}
setInterval(refresh, 5000);
</script>
</body>
</html>
