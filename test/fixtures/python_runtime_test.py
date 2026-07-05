import sys
import unittest


class PythonRuntimeTest(unittest.TestCase):
    def test_uses_python3(self):
        self.assertGreaterEqual(sys.version_info.major, 3)

    def test_uses_bazel_owned_python(self):
        self.assertNotIn("/run/current-system", sys.executable)
        self.assertNotIn("/usr/bin", sys.executable)


if __name__ == "__main__":
    unittest.main()
