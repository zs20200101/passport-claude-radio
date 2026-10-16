"""Cached weather + offline Chinese calendar. Network never blocks permission polls."""
from __future__ import annotations
import datetime as dt
import json
import ipaddress
import re
import threading
import time
import urllib.parse
import urllib.request
from lunar_python import Solar

TZ = dt.timezone(dt.timedelta(hours=8), "Asia/Shanghai")


def lunar_text(day):
    lunar = Solar.fromYmd(day.year, day.month, day.day).getLunar()
    return "农历" + lunar.getMonthInChinese() + "月" + lunar.getDayInChinese()


def weather_name(code):
    if code == 0: return "晴"
    if code in (1, 2, 3): return "多云" if code != 3 else "阴"
    if code in (45, 48): return "雾"
    if 51 <= code <= 67: return "雨"
    if 71 <= code <= 77 or code in (85, 86): return "雪"
    if 80 <= code <= 82: return "阵雨"
    if code in (95, 96, 99): return "雷雨"
    return "未知天气"


def network_city():
    """Resolve this connection's Chinese city, bypassing application proxies.

    Domestic IPv6/mobile exits can have stale city data in the global database.
    The response is bounded; never retain or send its public IP elsewhere.
    """
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}))
    with opener.open("https://myip.ipip.net/", timeout=5) as response:
        text = response.read(4096).decode("utf-8").strip()
    match = re.fullmatch(r"当前 IP[：:]\s*(\S+)\s+来自于[：:]\s*([^\r\n]+)", text)
    if not match:
        raise ValueError("Invalid network location response")
    ipaddress.ip_address(match[1])
    parts = match[2].split()
    if not parts or parts[0] != "中国":
        return None
    if len(parts) < 3:
        raise ValueError("Incomplete domestic network location")
    return parts[1], parts[2]


def geocode_network_city(region, city):
    query = urllib.parse.urlencode({"name": city, "count": 10,
                                    "language": "zh", "countryCode": "CN"})
    with urllib.request.urlopen("https://geocoding-api.open-meteo.com/v1/search?"+query, timeout=8) as response:
        value = json.loads(response.read(32768))
    for place in value.get("results", []):
        if (place.get("country_code") == "CN" and
                place.get("name", "").removesuffix("市") == city.removesuffix("市") and
                place.get("admin1", "").removesuffix("省").removesuffix("市") ==
                region.removesuffix("省").removesuffix("市")):
            return Companion.valid_location(city, place.get("latitude"), place.get("longitude"))
    raise ValueError("Network city coordinates unavailable")


class Companion:
    def __init__(self, config):
        self.config = config
        self.lock = threading.Lock()
        self.automatic_location = config.get("location_mode", "manual" if config.get("city") else "auto") == "auto"
        self.weather = "天气：城市定位中" if self.automatic_location else "天气：等待更新"
        self.location = None
        self.location_updated = 0.0
        self.location_error = False
        self.domestic_location = False
        self.fetched = 0.0
        self.error = False
        self.stop_event = threading.Event()
        self.thread = None

    def start(self):
        self.thread = threading.Thread(target=self.run, daemon=True, name="weather-cache")
        self.thread.start()

    def stop(self):
        self.stop_event.set()

    def run(self):
        while not self.stop_event.is_set():
            self.refresh()
            self.stop_event.wait(900)

    @staticmethod
    def valid_location(city, latitude, longitude):
        city = "".join(c for c in str(city or "") if c >= " " and c != "\x7f")
        city = city.encode("utf-8")[:48].decode("utf-8", errors="ignore").strip()
        if isinstance(latitude, bool) or isinstance(longitude, bool):
            raise ValueError("invalid coordinates")
        latitude, longitude = float(latitude), float(longitude)
        if not city or not -90 <= latitude <= 90 or not -180 <= longitude <= 180:
            raise ValueError("invalid location")
        return city, latitude, longitude

    def resolve_location(self):
        if not self.automatic_location:
            return self.valid_location(self.config.get("city"), self.config.get("latitude"), self.config.get("longitude"))
        if self.location and time.monotonic() - self.location_updated < 900:
            return self.location
        try:
            try:
                hint = network_city()
            except (OSError, ValueError, UnicodeError):
                if self.domestic_location:
                    raise ValueError("Domestic location refresh unavailable")
                hint = None
            if hint:
                # Keep the actual network city even if its geocoding temporarily
                # fails; don't silently replace a known Kunming exit with Beijing.
                location = geocode_network_city(*hint)
                domestic_location = True
            else:
                url = "https://ipwho.is/?fields=success,city,latitude,longitude&lang=zh-CN"
                with urllib.request.urlopen(url, timeout=8) as response:
                    value = json.loads(response.read(32768))
                if value.get("success") is not True:
                    raise ValueError("IP location unavailable")
                location = self.valid_location(value.get("city"), value.get("latitude"), value.get("longitude"))
                domestic_location = False
            with self.lock:
                self.location, self.location_updated, self.location_error = location, time.monotonic(), False
                self.domestic_location = domestic_location
        except (OSError, ValueError, KeyError, TypeError, AttributeError):
            with self.lock:
                self.location_error = True
        return self.location

    def refresh(self):
        try:
            location = self.resolve_location()
            if location is None:
                raise ValueError("IP location unavailable")
            city, lat, lon = location
        except (ValueError, TypeError):
            with self.lock:
                self.error = True
                if not self.fetched:
                    self.weather = "天气：城市定位暂不可用" if self.automatic_location else "天气：城市配置不完整"
            return
        query = urllib.parse.urlencode({"latitude": lat, "longitude": lon,
                                      "current": "temperature_2m,weather_code", "timezone": "Asia/Shanghai"})
        try:
            with urllib.request.urlopen("https://api.open-meteo.com/v1/forecast?"+query, timeout=8) as response:
                value = json.loads(response.read(32768))
            current = value["current"]
            temp = float(current["temperature_2m"])
            if not -90 <= temp <= 65:
                raise ValueError("temperature out of range")
            message = f"{city} {weather_name(int(current['weather_code']))} {temp:.0f}°C"
            with self.lock:
                self.weather, self.fetched, self.error = message, time.monotonic(), False
        except (OSError, ValueError, KeyError, TypeError, AttributeError, OverflowError):
            with self.lock:
                self.error = True
                if not self.fetched:
                    self.weather = f"{city} 天气暂不可用"

    def snapshot(self):
        now = dt.datetime.now(TZ)
        with self.lock:
            age = int(time.monotonic()-self.fetched) if self.fetched else -1
            weather = self.weather
            stale = self.error or age > 1800
            location_stale = self.location_error
        if location_stale and age >= 0:
            weather += " (定位缓存)"
        if stale and age >= 0:
            weather += " (缓存)"
        return {"date": now.strftime("%Y-%m-%d") + " 周" + "一二三四五六日"[now.weekday()],
                "lunar": lunar_text(now.date()), "weather": weather,
                "weather_age_seconds": age, "epoch": int(now.timestamp()), "timezone": "Asia/Shanghai"}
