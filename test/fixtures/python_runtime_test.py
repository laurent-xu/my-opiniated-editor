import sys
import unittest


class PythonRuntimeTest(unittest.TestCase):
    def test_uses_python3(self):
        self.assertGreaterEqual(sys.version_info.major, 3)


if __name__ == "__main__":
    unittest.main()

