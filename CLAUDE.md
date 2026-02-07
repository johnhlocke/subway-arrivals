# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

Single-file Flask web app that displays real-time downtown A train arrivals at 181 St station (stop `A03S`) using the MTA's public GTFS-realtime feed (no API key needed). It also provides a "leave now" / "wait N min" advisory based on a configurable walk time to the platform.

## Commands

```bash
pip install -r requirements.txt   # install dependencies
python subway.py                  # run dev server on port 5001
```

The app serves a single-page frontend at `/` and a JSON API at `/api/arrivals`.

## Architecture

Everything lives in `subway.py`:

- **Backend (Flask):** `fetch_arrivals()` pulls the MTA ACE protobuf feed, filters for route `A` at stop `A03S`, and returns sorted arrival times in minutes. The `/api/arrivals` endpoint returns JSON with the arrival list, walk time, and timestamp.
- **Frontend (inline HTML/JS):** Embedded as the `HTML_PAGE` string. Polls `/api/arrivals` every 30s, re-renders the countdown every 1s by subtracting elapsed time since last fetch. The advisory logic finds the first catchable train (platform wait >= 1 min) and recommends leaving if the platform wait is under 4 min.

## Key Constants

| Constant | Value | Meaning |
|---|---|---|
| `FEED_URL` | MTA ACE feed | GTFS-realtime protobuf endpoint |
| `ROUTE_ID` | `"A"` | A train |
| `STOP_ID` | `"A03S"` | 181 St, southbound platform |
| `WALK_MINUTES` | `10` | Walking time from apartment to platform |
