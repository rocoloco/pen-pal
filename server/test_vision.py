import unittest
import json
import vision
from unittest import mock
from vision import parse_transcription


class VisionTests(unittest.TestCase):
    def result(self, content, finish="stop"):
        return {"choices": [{"finish_reason": finish, "message": {"content": content}}]}

    def test_exact_transcription(self):
        self.assertEqual(parse_transcription(self.result('{"readable":true,"text":"What is 2 + 2?"}')), "What is 2 + 2?")

    def test_rejects_explanations(self):
        for value in ("I cannot see this image", '{"readable":false,"text":"An error occurred"}', '{"readable":"true","text":"bad"}', '{"readable":true,"text":""}'):
            with self.assertRaises(ValueError):
                parse_transcription(self.result(value))

    def test_rejects_partial_generation(self):
        with self.assertRaises(ValueError):
            parse_transcription(self.result('{"readable":true,"text":"partial"}', "length"))

    def sent_headers(self, session):
        """Headers of the request chat() sends, with the network stubbed out."""
        seen = []
        def fake_urlopen(request, timeout):
            seen.append({k.lower(): v for k, v in request.header_items()})
            return mock.MagicMock(__enter__=lambda s: s, __exit__=lambda *a: None,
                                  read=lambda n: json.dumps({"choices": []}).encode())
        with mock.patch.multiple(vision, VISION_MODEL="m", VISION_SESSION=session),                 mock.patch.dict("os.environ", {"INK_VISION_API_KEY": "k"}),                 mock.patch("urllib.request.urlopen", fake_urlopen):
            vision.chat({"model": "m"})
        return seen[0]

    def test_session_header_only_when_configured(self):
        self.assertEqual(self.sent_headers("pen-pal-test")["x-opencode-session"], "pen-pal-test")
        self.assertNotIn("x-opencode-session", self.sent_headers(""))


if __name__ == "__main__":
    unittest.main()
