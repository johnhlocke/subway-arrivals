# Downtown A Train — 181 St

Real-time arrival times for downtown A trains at 181 St station, powered by the MTA's GTFS-realtime feed.

```
Downtown A Train — 181 St
━━━━━━━━━━━━━━━━━━━━━━━━━
  A  2 min
  A  8 min
  A  15 min
  A  22 min
```

## Setup

```bash
pip install -r requirements.txt
python subway.py
```

## How It Works

The script fetches the [MTA ACE feed](https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace), parses the GTFS-realtime protobuf response, and filters for downtown A trains at stop `A03S` (181 St southbound platform). No API key required.
