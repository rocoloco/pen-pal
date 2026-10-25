import unittest
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


if __name__ == "__main__":
    unittest.main()
