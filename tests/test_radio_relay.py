import io
import pathlib
import sys
import unittest
from unittest.mock import Mock, patch
sys.path.insert(0, str(pathlib.Path(__file__).resolve().parents[1]/"tools/claude_lan_bridge"))
import radio_relay

class RelayTests(unittest.TestCase):
    def handler(self, path=radio_relay.RADIO_PATH, address="192.168.1.73"):
        h = Mock()
        h.path = path
        h.client_address = (address, 1234)
        h.wfile = io.BytesIO()
        return h

    def test_no_arbitrary_source_or_public_listener(self):
        relay = radio_relay.RadioRelay()
        for path, address in (("/radio/phoenix.mp3?url=bad", "127.0.0.1"),
                              (radio_relay.RADIO_PATH, "8.8.8.8")):
            h = self.handler(path, address)
            with patch.object(radio_relay.subprocess, "Popen") as popen:
                relay.serve(h)
                popen.assert_not_called()
                h.reply.assert_called_once_with(404)

    def test_missing_transcoder_and_bounded_listeners(self):
        relay = radio_relay.RadioRelay()
        h = self.handler()
        with patch.object(radio_relay.shutil, "which", return_value=None):
            relay.serve(h)
        self.assertEqual(h.reply.call_args.args[0], 503)
        self.assertTrue(relay.slots.acquire(False))
        self.assertTrue(relay.slots.acquire(False))
        h = self.handler()
        with patch.object(radio_relay.shutil, "which", return_value="ffmpeg"):
            relay.serve(h)
        self.assertEqual(h.reply.call_args.args[0], 503)
        relay.slots.release(); relay.slots.release()

    def test_stream_cleanup_and_command_has_one_fixed_input(self):
        relay = radio_relay.RadioRelay()
        data = b"MP3" * 1800
        process = Mock()
        process.stdout = io.BytesIO(data)
        process.poll.return_value = None
        h = self.handler()
        with patch.object(radio_relay.shutil, "which", return_value="ffmpeg"), \
             patch.object(radio_relay.subprocess, "Popen", return_value=process) as popen:
            relay.serve(h)
            args = popen.call_args.args[0]
            self.assertEqual(args[args.index("-i")+1], radio_relay.PHOENIX_AUDIO)
            self.assertEqual(args.count("-i"), 1)
            self.assertNotIn("shell", popen.call_args.kwargs)
        self.assertEqual(h.wfile.getvalue(), data)
        h.send_response.assert_called_once_with(200)
        process.kill.assert_called_once()
        process.wait.assert_called_once_with(timeout=5)
        self.assertTrue(process.stdout.closed)
        self.assertTrue(relay.slots.acquire(False))
        self.assertTrue(relay.slots.acquire(False))
        relay.slots.release(); relay.slots.release()

    def test_disconnect_reaps_transcoder(self):
        relay = radio_relay.RadioRelay()
        process = Mock()
        process.stdout = io.BytesIO(b"audio" * 1000)
        process.poll.return_value = None
        h = self.handler()
        h.wfile = Mock()
        h.wfile.write.side_effect = BrokenPipeError()
        with patch.object(radio_relay.shutil, "which", return_value="ffmpeg"), \
             patch.object(radio_relay.subprocess, "Popen", return_value=process):
            relay.serve(h)
        process.kill.assert_called_once()
        self.assertTrue(process.stdout.closed)

if __name__ == "__main__": unittest.main()
