import os
import pathlib
import unittest


def runfile_path(path: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


class BazelRcTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = set(
            runfile_path(".bazelrc").read_text(encoding="ascii").splitlines()
        )

    def test_disk_cache_is_shared_by_worktrees(self):
        self.assertIn(
            "build --disk_cache=~/.cache/my-opiniated-editor/bazel/disk",
            self.lines,
        )

    def test_disk_cache_has_a_size_limit(self):
        self.assertIn(
            "build --experimental_disk_cache_gc_max_size=10G",
            self.lines,
        )


if __name__ == "__main__":
    unittest.main()
