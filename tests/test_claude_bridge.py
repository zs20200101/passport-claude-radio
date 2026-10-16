import concurrent.futures
import datetime as dt
import hashlib
import hmac
import http.client
import importlib.util
import json
import pathlib
import secrets
import subprocess
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT = pathlib.Path(__file__).resolve().parents[1]
TOOLS = ROOT / 'tools' / 'claude_lan_bridge'
sys.path.insert(0, str(TOOLS))
import bridge
import companion
spec = importlib.util.spec_from_file_location('passport_setup', TOOLS / 'setup.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)
PAYLOAD = {'hook_event_name': 'PermissionRequest', 'tool_name': 'Bash',
           'cwd': 'C:/work', 'tool_input': {'command': 'echo 测试', 'description': 'innocent'}}


class JsonReply:
    def __init__(self, value): self.value = value
    def __enter__(self): return self
    def __exit__(self, *args): pass
    def read(self, size): return json.dumps(self.value).encode()


class BridgeTests(unittest.TestCase):
    def setUp(self):
        self.b = bridge.Bridge(b'abc123', timeout=1)

    def test_utf8_bounds_and_command_priority(self):
        payload = {**PAYLOAD, 'transcript_path': 'private', 'tool_input': {'command': '汉'*500, 'description': 'ignore'}}
        item = self.b.enqueue(payload)
        self.assertLessEqual(len(item.summary.encode()), 256)
        self.assertTrue(set(item.summary) <= {'汉'})
        self.assertNotIn('private', str(item))

    def test_delivery_challenge_repetition_and_wrong_id(self):
        item = self.b.enqueue(PAYLOAD)
        self.assertFalse(self.b.decide(item.request_id, 'allow', 'invented'))
        shown = self.b.next_for_device()
        self.assertEqual(len(shown.challenge), 32)
        self.assertFalse(self.b.decide('wrong', 'allow', shown.challenge))
        self.assertFalse(self.b.decide(item.request_id, 'allow', 'wrong'))
        self.assertFalse(self.b.decide(item.request_id, 'other', shown.challenge))
        self.assertTrue(self.b.decide(item.request_id, 'deny', shown.challenge))
        self.assertFalse(self.b.decide(item.request_id, 'allow', shown.challenge))
        self.assertEqual(self.b.wait_result(item), 'deny')
        self.assertEqual(len(self.b.by_id), 0)

    def test_queue_overflow_and_reuse(self):
        items = [self.b.enqueue(PAYLOAD) for _ in range(bridge.MAX_QUEUE)]
        self.assertIsNone(self.b.enqueue(PAYLOAD))
        item = self.b.next_for_device()
        self.assertTrue(self.b.decide(item.request_id, 'allow', item.challenge))
        self.assertIsNotNone(self.b.enqueue(PAYLOAD))
        self.b.shutdown()
        self.assertTrue(all(i.result is not None for i in items))

    def test_timeout_and_disconnected_device(self):
        self.b.timeout = .05
        item = self.b.enqueue(PAYLOAD)
        self.assertEqual(self.b.wait_result(item), 'deny')
        self.b.timeout = 1
        item = self.b.enqueue(PAYLOAD)
        self.b.next_for_device()
        self.b.last_seen -= bridge.LEASE_SECONDS+1
        self.assertEqual(self.b.wait_result(item), 'deny')
        self.assertFalse(self.b.decide(item.request_id, 'allow', item.challenge))

    def test_hmac_vectors_tamper_and_nonce_replay(self):
        nonce = 'a'*32
        expected = hmac.new(b'abc123', b'GET\n/v1/pending\n'+nonce.encode()+b'\n', hashlib.sha256).hexdigest()
        self.assertEqual(self.b.signature('GET', '/v1/pending', b'', nonce), expected)
        self.assertFalse(self.b.authorize('POST', '/v1/pending', b'', nonce, expected))
        self.assertTrue(self.b.authorize('GET', '/v1/pending', b'', nonce, expected))
        self.assertFalse(self.b.authorize('GET', '/v1/pending', b'', nonce, expected))
        self.assertFalse(self.b.authorize('GET', '/v1/pending', b'', 'bad', expected))
        self.assertNotEqual(self.b.response_signature(nonce, 200, b'allow'), self.b.response_signature(nonce, 200, b'deny'))


class HttpHookTests(unittest.TestCase):
    def setUp(self):
        self.b = bridge.Bridge(b'c'*64, timeout=2)
        self.server = bridge.Server(('127.0.0.1', 0), self.b)
        self.thread = threading.Thread(target=self.server.serve_forever, daemon=True)
        self.thread.start()
        self.pool = concurrent.futures.ThreadPoolExecutor(1)
        self.temp = tempfile.TemporaryDirectory()
        self.config = pathlib.Path(self.temp.name)/'config.json'
        setup.atomic_json(self.config, {'token': 'c'*64, 'port': self.server.server_port})

    def tearDown(self):
        self.b.shutdown()
        self.pool.shutdown(wait=True)
        self.server.shutdown()
        self.server.server_close()
        self.thread.join(2)
        self.temp.cleanup()

    def request(self, method, path, value=None, headers=None, signed=True):
        body = b'' if value is None else json.dumps(value, ensure_ascii=False).encode()
        nonce = secrets.token_hex(16)
        hdr = {'Content-Type': 'application/json'}
        if signed:
            hdr.update({'X-Passport-Nonce': nonce, 'X-Claude-Bridge-Signature': self.b.signature(method, path, body, nonce)})
        hdr.update(headers or {})
        client = http.client.HTTPConnection('127.0.0.1', self.server.server_port, timeout=4)
        client.request(method, path, body, hdr)
        response = client.getresponse()
        data = response.read()
        status, signature = response.status, response.getheader('X-Passport-Response')
        client.close()
        if signed and status not in (401, 403):
            self.assertEqual(signature, self.b.response_signature(nonce, status, data))
        return status, json.loads(data) if data else None

    def run_hook(self, payload=PAYLOAD):
        return subprocess.run([sys.executable, str(TOOLS/'permission_hook.py'), '--config', str(self.config)],
                              input=json.dumps(payload, ensure_ascii=False).encode(), capture_output=True, timeout=6)

    def pending(self):
        deadline = time.monotonic()+3
        while time.monotonic()<deadline:
            status, value = self.request('GET', '/v1/pending')
            if status == 200:
                return value
            time.sleep(.02)
        self.fail('hook request did not reach device endpoint')

    def test_allow_and_deny_real_hook_subprocess(self):
        for decision in ('allow', 'deny'):
            future = self.pool.submit(self.run_hook)
            item = self.pending()
            self.assertEqual(item['summary'], 'echo 测试')
            status, _ = self.request('POST', '/v1/decision', {'id': item['id'], 'challenge': item['challenge'], 'decision': decision})
            self.assertEqual(status, 200)
            result = future.result(5)
            self.assertEqual(result.returncode, 0)
            self.assertEqual(json.loads(result.stdout)['hookSpecificOutput']['decision']['behavior'], decision)
            self.assertEqual(self.request('POST', '/v1/decision', {'id': item['id'], 'challenge': item['challenge'], 'decision': 'allow'})[0], 409)
        self.assertEqual(self.request('GET', '/v1/pending')[0], 204)

    def test_http_auth_origin_bad_event_and_idle(self):
        self.assertEqual(self.request('GET', '/v1/pending', signed=False)[0], 401)
        self.assertEqual(self.request('POST', '/hook', PAYLOAD, {'Origin': 'http://evil.local'})[0], 403)
        self.assertEqual(self.request('POST', '/hook', {'hook_event_name': 'Wrong'})[0], 400)
        status, idle = self.request('GET', '/v1/idle')
        self.assertEqual(status, 200)
        self.assertIn('农历', idle['lunar'])
        self.assertIn('定位中', idle['weather'])

    def test_hook_timeout_and_shutdown_deny(self):
        self.b.timeout = .05
        result = self.run_hook()
        self.assertEqual(json.loads(result.stdout)['hookSpecificOutput']['decision']['behavior'], 'deny')
        self.b.timeout = 2
        future = self.pool.submit(self.run_hook)
        self.pending()
        self.b.shutdown()
        self.assertEqual(json.loads(future.result(5).stdout)['hookSpecificOutput']['decision']['behavior'], 'deny')

    def test_hook_rejects_forged_response(self):
        self.b.response_signature = lambda *args: '0'*64
        self.b.timeout = .05
        result = self.run_hook()
        self.assertIn(b'unavailable', result.stderr)
        self.assertEqual(json.loads(result.stdout)['hookSpecificOutput']['decision']['behavior'], 'deny')

    def test_wrong_event_and_missing_config_fail_closed(self):
        self.assertEqual(json.loads(self.run_hook({}).stdout)['hookSpecificOutput']['decision']['behavior'], 'deny')
        self.config.unlink()
        self.assertEqual(json.loads(self.run_hook().stdout)['hookSpecificOutput']['decision']['behavior'], 'deny')


class SetupCalendarWeatherTests(unittest.TestCase):
    def setUp(self):
        # Isolate all host tests from real IP geolocation services.
        self.geo_patch = patch('companion.network_city', return_value=None)
        self.geo_mock = self.geo_patch.start()
        self.addCleanup(self.geo_patch.stop)

    def test_domestic_network_city_beats_stale_global_location(self):
        self.geo_mock.return_value = ('云南', '昆明')
        geocode = {'results': [
            {'country_code': 'CN', 'name': '昆明', 'admin1': '其他省', 'latitude': 30, 'longitude': 110},
            {'country_code': 'CN', 'name': '昆明', 'admin1': '云南', 'latitude': 25.03889, 'longitude': 102.71833}]}
        c = companion.Companion({})
        with patch('urllib.request.urlopen', side_effect=[JsonReply(geocode),
                JsonReply({'current': {'temperature_2m': 20, 'weather_code': 3}})]) as network:
            c.refresh()
            self.assertIn('昆明 阴 20°C', c.snapshot()['weather'])
            self.assertEqual(c.location, ('昆明', 25.03889, 102.71833))
            self.assertIn('geocoding-api.open-meteo.com', network.call_args_list[0].args[0])
            self.assertIn('latitude=25.03889', network.call_args_list[1].args[0])
            self.assertTrue(all('ipwho.is' not in args.args[0] for args in network.call_args_list))

    def test_network_geocoding_failure_keeps_city_and_recovers(self):
        self.geo_mock.return_value = ('云南', '昆明')
        c = companion.Companion({})
        c.location, c.location_updated = ('昆明', 25.04, 102.72), 100
        with patch('companion.time.monotonic', return_value=1001), \
                patch('urllib.request.urlopen', side_effect=[JsonReply({'results': []}),
                    JsonReply({'current': {'temperature_2m': 20, 'weather_code': 0}})]):
            c.refresh()
            self.assertIn('昆明 晴 20°C (定位缓存)', c.snapshot()['weather'])
        self.geo_mock.return_value = ('四川', '成都')
        with patch('companion.time.monotonic', return_value=1902), \
                patch('urllib.request.urlopen', side_effect=[JsonReply({'results': [
                    {'country_code': 'CN', 'name': '成都', 'admin1': '四川', 'latitude': 30.67, 'longitude': 104.07}]}),
                    JsonReply({'current': {'temperature_2m': 23, 'weather_code': 0}})]):
            c.refresh()
            self.assertIn('成都 晴 23°C', c.snapshot()['weather'])
            self.assertNotIn('缓存', c.snapshot()['weather'])

    def test_known_domestic_city_retained_on_provider_outage(self):
        self.geo_mock.side_effect = OSError('offline')
        c = companion.Companion({})
        c.location, c.location_updated, c.domestic_location = ('昆明', 25.04, 102.72), 100, True
        with patch('companion.time.monotonic', return_value=1001), \
                patch('urllib.request.urlopen', return_value=JsonReply(
                    {'current': {'temperature_2m': 22, 'weather_code': 0}})) as network:
            c.refresh()
            self.assertEqual(network.call_count, 1)
            self.assertIn('api.open-meteo.com/', network.call_args.args[0])
            self.assertIn('昆明 晴 22°C (定位缓存)', c.snapshot()['weather'])

    def test_domestic_service_failure_falls_back_to_global(self):
        self.geo_mock.side_effect = OSError('offline')
        c = companion.Companion({})
        with patch('urllib.request.urlopen', side_effect=[
                JsonReply({'success': True, 'city': '测试城', 'latitude': 30, 'longitude': 120}),
                JsonReply({'current': {'temperature_2m': 20, 'weather_code': 0}})]):
            c.refresh()
        self.assertIn('测试城 晴 20°C', c.snapshot()['weather'])

    def test_domestic_network_parser_and_direct_connection(self):
        class TextReply:
            def __init__(self, text): self.text = text
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size): return self.text.encode()
        self.geo_patch.stop()
        for text, expected in (
                ('当前 IP：2409:1234::1  来自于：中国 云南 昆明 移动\n', ('云南', '昆明')),
                ('当前 IP：1.2.3.4  来自于：中国 北京 北京 联通', ('北京', '北京')),
                ('当前 IP：1.2.3.4 来自于：美国 加州 洛杉矶', None)):
            with self.subTest(text=text), patch('urllib.request.build_opener') as build:
                build.return_value.open.return_value = TextReply(text)
                self.assertEqual(companion.network_city(), expected)
                self.assertEqual(build.call_args.args[0].proxies, {})
        for invalid in ('<html>blocked</html>', '当前 IP：bad 来自于：中国 云南 昆明 移动',
                        '当前 IP：1.2.3.4 来自于：中国 云南'):
            with self.subTest(invalid=invalid), patch('urllib.request.build_opener') as build:
                build.return_value.open.return_value = TextReply(invalid)
                with self.assertRaises(ValueError): companion.network_city()

    def test_merge_preserves_other_hooks_and_is_idempotent(self):
        ours = {'type': 'command', 'command': 'python', 'args': ['/claude_lan_bridge/permission_hook.py'], 'timeout': 140}
        other = {'type': 'command', 'command': 'another-command'}
        before = {'env': {'KEEP': 'unchanged'}, 'hooks': {'Stop': [], 'PermissionRequest': [{'matcher': 'Bash', 'hooks': [ours, other]}]}}
        merged = setup.merge_hook(before, ours)
        self.assertEqual(merged['env'], before['env'])
        self.assertEqual(merged['hooks']['PermissionRequest'][0]['hooks'], [other])
        self.assertEqual(merged, setup.merge_hook(merged, ours))
        self.assertEqual(len(before['hooks']['PermissionRequest'][0]['hooks']), 2)

    def test_install_backup_and_argument_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            settings = pathlib.Path(directory)/'settings.json'
            config = pathlib.Path(directory)/'中文 config.json'
            setup.atomic_json(settings, {'permissions': {'allow': ['Read']}})
            setup.install_hook(settings, config)
            after = json.loads(settings.read_text(encoding='utf-8'))
            self.assertEqual(after['permissions'], {'allow': ['Read']})
            handler = after['hooks']['PermissionRequest'][0]['hooks'][0]
            self.assertEqual(handler['args'][-1], str(config.resolve()))
            setup.install_hook(settings, config)
            self.assertEqual(len(list(pathlib.Path(directory).glob('*.passport-backup-*'))), 1)

    def test_known_lunar_new_year(self):
        self.assertEqual(companion.lunar_text(dt.date(2026, 2, 17)), '农历正月初一')
        self.assertEqual(companion.lunar_text(dt.date(2025, 1, 29)), '农历正月初一')

    def test_setup_auto_location_preserves_token_and_settings(self):
        with tempfile.TemporaryDirectory() as directory:
            config = pathlib.Path(directory)/'config.json'
            settings = pathlib.Path(directory)/'settings.json'
            setup.atomic_json(config, {'token': 'a'*64, 'city': '旧城', 'latitude': 30,
                                      'longitude': 120, 'location_mode': 'manual'})
            setup.atomic_json(settings, {'permissions': {'allow': ['Read']}})
            original_settings = settings.read_bytes()
            argv = ['setup.py', '--config', str(config), '--settings', str(settings), '--auto-location']
            with patch.object(sys, 'argv', argv), patch('builtins.print'), \
                 patch.object(setup.subprocess, 'check_output', return_value=b'"\xff-localized-user","S-1-5-21-123-456-789-1001"'), \
                 patch.object(setup.subprocess, 'run', return_value=subprocess.CompletedProcess([], 0)) as acl:
                self.assertEqual(setup.main(), 0)
                self.assertEqual(setup.main(), 0)
                if setup.os.name == 'nt':
                    self.assertIn('*S-1-5-21-123-456-789-1001:(OI)(CI)F', acl.call_args.args[0])
            value = json.loads(config.read_text(encoding='utf-8'))
            self.assertEqual(value['token'], 'a'*64)
            self.assertEqual(value['location_mode'], 'auto')
            self.assertFalse({'city', 'latitude', 'longitude'} & value.keys())
            self.assertEqual(settings.read_bytes(), original_settings)

    def test_setup_rejects_conflicting_or_partial_locations(self):
        for args in (['--auto-location', '--city', '城'], ['--latitude', '30'], ['--city', '城']):
            with self.subTest(args=args), patch.object(sys, 'argv', ['setup.py']+args), \
                 patch.object(sys, 'stderr'):
                with self.assertRaises(SystemExit) as error:
                    setup.main()
                self.assertEqual(error.exception.code, 2)

    def test_auto_location_weather_and_quarter_hour_refresh(self):
        c = companion.Companion({})
        geo = {'success': True, 'city': '测试城', 'latitude': 30, 'longitude': 120}
        weather = {'current': {'temperature_2m': 21.2, 'weather_code': 0}}
        with patch('companion.time.monotonic', return_value=100), \
             patch('urllib.request.urlopen', side_effect=[JsonReply(geo), JsonReply(weather)]) as network:
            c.refresh()
            self.assertIn('ipwho.is/', network.call_args_list[0].args[0])
            self.assertIn('latitude=30.0', network.call_args_list[1].args[0])
            self.assertIn('测试城 晴 21°C', c.snapshot()['weather'])
        with patch('companion.time.monotonic', return_value=500), \
             patch('urllib.request.urlopen', return_value=JsonReply(weather)) as network:
            c.refresh()
            self.assertEqual(network.call_count, 1)
            self.assertIn('api.open-meteo.com/', network.call_args.args[0])
        with patch('companion.time.monotonic', return_value=1001), \
             patch('urllib.request.urlopen', side_effect=[JsonReply({**geo, 'city': '新城'}), JsonReply(weather)]):
            c.refresh()
            self.assertIn('新城', c.snapshot()['weather'])

    def test_auto_location_failure_and_recovery(self):
        c = companion.Companion({})
        with patch('urllib.request.urlopen', side_effect=OSError('offline')):
            c.refresh()
        self.assertIn('城市定位暂不可用', c.snapshot()['weather'])
        with patch('urllib.request.urlopen', side_effect=[
            JsonReply({'success': True, 'city': '测试城', 'latitude': 30, 'longitude': 120}),
            JsonReply({'current': {'temperature_2m': 18, 'weather_code': 3}})]):
            c.refresh()
        self.assertIn('测试城 阴 18°C', c.snapshot()['weather'])
        self.assertNotIn('缓存', c.snapshot()['weather'])

    def test_expired_location_retained_and_recovered(self):
        c = companion.Companion({})
        c.location, c.location_updated = ('旧城', 30, 120), 100
        with patch('companion.time.monotonic', return_value=4000), \
             patch('urllib.request.urlopen', side_effect=[OSError('geo offline'),
                 JsonReply({'current': {'temperature_2m': 20, 'weather_code': 0}})]):
            c.refresh()
            self.assertIn('旧城 晴 20°C (定位缓存)', c.snapshot()['weather'])
        with patch('companion.time.monotonic', return_value=4900), \
             patch('urllib.request.urlopen', side_effect=[
                 JsonReply({'success': True, 'city': '新城', 'latitude': 31, 'longitude': 121}),
                 JsonReply({'current': {'temperature_2m': 22, 'weather_code': 0}})]):
            c.refresh()
            self.assertIn('新城 晴 22°C', c.snapshot()['weather'])
            self.assertNotIn('缓存', c.snapshot()['weather'])

    def test_auto_location_rejects_invalid_responses(self):
        for value in ({'success': False}, [], {'success': True, 'city': '城', 'latitude': 91, 'longitude': 0},
                      {'success': True, 'city': '城', 'latitude': float('nan'), 'longitude': 0},
                      {'success': True, 'city': '城', 'latitude': 0, 'longitude': float('inf')},
                      {'success': True, 'city': '', 'latitude': 0, 'longitude': 0},
                      {'success': True, 'city': '城', 'latitude': True, 'longitude': 0}):
            with self.subTest(value=value):
                c = companion.Companion({})
                with patch('urllib.request.urlopen', return_value=JsonReply(value)) as network:
                    c.refresh()
                    self.assertEqual(network.call_count, 1)
                self.assertIsNone(c.location)
                self.assertIn('城市定位暂不可用', c.snapshot()['weather'])

    def test_manual_location_bypasses_geo_and_bounds_city(self):
        c = companion.Companion({'city': '\n城'*40, 'latitude': 30, 'longitude': 120})
        with patch('urllib.request.urlopen', return_value=JsonReply(
            {'current': {'temperature_2m': 19, 'weather_code': 0}})) as network:
            c.refresh()
            self.assertEqual(network.call_count, 1)
            self.assertIn('api.open-meteo.com/', network.call_args.args[0])
        city, _, _ = c.resolve_location()
        self.assertLessEqual(len(city.encode()), 48)
        self.assertNotIn('\n', city)

    def test_malformed_weather_does_not_stop_refresh(self):
        for value in ([], {'current': []}, {'current': {'temperature_2m': float('nan'), 'weather_code': 0}},
                      {'current': {'temperature_2m': 20, 'weather_code': float('inf')}}):
            with self.subTest(value=value):
                c = companion.Companion({'city': '测试城', 'latitude': 30, 'longitude': 120})
                with patch('urllib.request.urlopen', return_value=JsonReply(value)):
                    c.refresh()
                self.assertIn('天气暂不可用', c.snapshot()['weather'])

    def test_weather_cache_retained_on_failure(self):
        c = companion.Companion({'city': '测试', 'latitude': 30, 'longitude': 120})
        class Reply:
            def __enter__(self): return self
            def __exit__(self, *args): pass
            def read(self, size): return b'{"current":{"temperature_2m":21.2,"weather_code":0}}'
        with patch('urllib.request.urlopen', return_value=Reply()): c.refresh()
        self.assertIn('21°C', c.snapshot()['weather'])
        with patch('urllib.request.urlopen', side_effect=OSError()): c.refresh()
        self.assertIn('缓存', c.snapshot()['weather'])
        self.assertIn('21°C', c.snapshot()['weather'])
        self.assertEqual(companion.weather_name(95), '雷雨')
        self.assertEqual(companion.weather_name(999), '未知天气')


if __name__ == '__main__':
    unittest.main()
