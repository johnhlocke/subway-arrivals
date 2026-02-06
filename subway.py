import time
import sys

import requests
from google.transit import gtfs_realtime_pb2

FEED_URL = "https://api-endpoint.mta.info/Dataservice/mtagtfsfeeds/nyct%2Fgtfs-ace"
ROUTE_ID = "A"
STOP_ID = "A03S"  # 181 St, downtown platform


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


def main():
    try:
        arrivals = fetch_arrivals()
    except requests.RequestException as e:
        print(f"Error fetching feed: {e}", file=sys.stderr)
        sys.exit(1)

    print("Downtown A Train — 181 St")
    print("━" * 25)

    if not arrivals:
        print("  No upcoming arrivals")
        return

    for mins in arrivals:
        label = "now" if mins < 1 else f"{mins} min"
        print(f"  A  {label}")


if __name__ == "__main__":
    main()
