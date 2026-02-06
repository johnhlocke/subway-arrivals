import time
from datetime import datetime

import requests
from flask import Flask, jsonify
from google.transit import gtfs_realtime_pb2

FEED_URL = "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace"
ROUTE_ID = "A"
STOP_ID = "A03S"  # 181 St, downtown platform
WALK_MINUTES = 10  # time to walk from apartment to platform

app = Flask(__name__)


def fetch_arrivals():
    response = requests.get(FEED_URL, timeout=10)
    response.raise_for_status()

    feed = gtfs_realtime_pb2.FeedMessage()
    feed.ParseFromString(response.content)

    now = time.time()
    arrivals = []

    for entity in feed.entity:
        if not entity.HasField("trip_update"):
            continue
        trip = entity.trip_update
        if trip.trip.route_id != ROUTE_ID:
            continue
        for stu in trip.stop_time_update:
            if stu.stop_id != STOP_ID:
                continue
            arr_time = stu.arrival.time if stu.arrival.time else stu.departure.time
            if arr_time and arr_time > now:
                minutes = int((arr_time - now) / 60)
                arrivals.append(minutes)

    arrivals.sort()
    return arrivals


@app.route("/api/arrivals")
def api_arrivals():
    try:
        arrivals = fetch_arrivals()
    except requests.RequestException:
        return jsonify(arrivals=[], updated=datetime.now().strftime("%-I:%M:%S %p"), error="Feed unavailable"), 502
    return jsonify(
        arrivals=[{"minutes": m} for m in arrivals],
        updated=datetime.now().strftime("%-I:%M:%S %p"),
        walk_minutes=WALK_MINUTES,
    )


@app.route("/")
def index():
    return HTML_PAGE


HTML_PAGE = """\
<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>A Train — 181 St</title>
<style>
  * { margin: 0; padding: 0; box-sizing: border-box; }
  body {
    font-family: -apple-system, "Helvetica Neue", Arial, sans-serif;
    background: #121212;
    color: #e0e0e0;
    display: flex;
    justify-content: center;
    padding: 48px 16px;
    min-height: 100vh;
  }
  .container { max-width: 420px; width: 100%; }
  h1 {
    font-size: 1.5rem;
    font-weight: 600;
    margin-bottom: 24px;
    color: #ffffff;
  }
  .badge {
    display: inline-flex;
    align-items: center;
    justify-content: center;
    width: 36px;
    height: 36px;
    border-radius: 50%;
    background: #0039A6;
    color: #fff;
    font-weight: 700;
    font-size: 1.1rem;
    flex-shrink: 0;
  }
  .arrival-row {
    display: flex;
    align-items: center;
    gap: 16px;
    padding: 14px 0;
    border-bottom: 1px solid #2a2a2a;
  }
  .arrival-row:last-child { border-bottom: none; }
  .time-label {
    font-size: 1.15rem;
    font-variant-numeric: tabular-nums;
  }
  .updated {
    margin-top: 28px;
    font-size: 0.8rem;
    color: #666;
  }
  .advisory {
    font-size: 1.4rem;
    font-weight: 700;
    padding: 16px 0;
    margin-bottom: 20px;
    border-bottom: 2px solid #2a2a2a;
  }
  .advisory.go { color: #4caf50; }
  .advisory.wait { color: #ff9800; }
  .advisory.none { color: #888; font-weight: 400; font-size: 1rem; }
  .detail {
    font-size: 0.8rem;
    color: #888;
    margin-top: -14px;
    margin-bottom: 20px;
    padding-bottom: 16px;
    border-bottom: 2px solid #2a2a2a;
  }
  .error { color: #cf6679; margin-top: 12px; font-size: 0.85rem; }
  .empty { color: #888; padding: 14px 0; }
</style>
</head>
<body>
<div class="container">
  <h1>Downtown A Train &mdash; 181 St</h1>
  <div id="advisory" class="advisory"></div>
  <div id="detail" class="detail"></div>
  <div id="list"></div>
  <div id="updated" class="updated"></div>
  <div id="error" class="error"></div>
</div>
<script>
let arrivals = [];
let fetchedAt = null;
let walkMinutes = 10;

function render() {
  const list = document.getElementById("list");
  const advEl = document.getElementById("advisory");
  const detEl = document.getElementById("detail");
  if (!arrivals.length) {
    list.innerHTML = '<div class="empty">No upcoming arrivals</div>';
    advEl.textContent = "";
    advEl.className = "advisory";
    detEl.textContent = "";
    return;
  }
  const elapsed = fetchedAt ? (Date.now() - fetchedAt) / 60000 : 0;

  // Advisory: find the first train you can catch (platform wait >= 1 min)
  let advised = false;
  for (const a of arrivals) {
    const adj = a.minutes - elapsed;
    const platformWait = adj - walkMinutes;
    if (platformWait < 1) continue;
    const trainMin = Math.round(adj);
    const waitMin = Math.round(platformWait);
    if (platformWait < 4) {
      advEl.textContent = "YES, leave now";
      advEl.className = "advisory go";
    } else {
      const waitHome = Math.round(platformWait - 4);
      advEl.textContent = "NO, wait " + waitHome + " min";
      advEl.className = "advisory wait";
    }
    detEl.textContent = "The next train is in " + trainMin + " min. With a " + walkMinutes + " minute walk to the 181st A Stop, you will need to wait " + waitMin + " min on the platform.";
    advised = true;
    break;
  }
  if (!advised) {
    advEl.textContent = "";
    advEl.className = "advisory";
    detEl.textContent = "";
  }

  list.innerHTML = arrivals.map(a => {
    const adj = Math.max(0, Math.round(a.minutes - elapsed));
    const label = adj < 1 ? "now" : adj + " min";
    return '<div class="arrival-row"><span class="badge">A</span><span class="time-label">' + label + '</span></div>';
  }).join("");
}

async function fetchData() {
  try {
    const res = await fetch("/api/arrivals");
    const data = await res.json();
    if (data.error) {
      document.getElementById("error").textContent = data.error;
    } else {
      document.getElementById("error").textContent = "";
    }
    arrivals = data.arrivals || [];
    walkMinutes = data.walk_minutes || walkMinutes;
    fetchedAt = Date.now();
    document.getElementById("updated").textContent = "Last updated " + data.updated;
    render();
  } catch (e) {
    document.getElementById("error").textContent = "Network error";
  }
}

fetchData();
setInterval(fetchData, 30000);
setInterval(render, 1000);
</script>
</body>
</html>
"""


if __name__ == "__main__":
    app.run(port=5001)
