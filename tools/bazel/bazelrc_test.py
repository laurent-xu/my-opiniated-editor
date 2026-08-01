import os
import pathlib
import tomllib
import unittest


def runfile_path(path: str) -> pathlib.Path:
    return pathlib.Path(os.environ["TEST_SRCDIR"], os.environ["TEST_WORKSPACE"], path)


class BazelRcTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.lines = set(
            runfile_path(".bazelrc").read_text(encoding="ascii").splitlines()
        )
        cls.codex_config = tomllib.loads(
            runfile_path(".codex/config.toml").read_text(encoding="ascii")
        )

    def test_disk_cache_is_shared_by_worktrees(self):
        self.assertIn(
            "build --disk_cache=~/.cache/my-opiniated-editor/bazel/disk",
            self.lines,
        )

    def test_codex_can_write_the_shared_disk_cache(self):
        self.assertEqual("workspace-write", self.codex_config["sandbox_mode"])
        self.assertIn(
            "~/.cache/my-opiniated-editor/bazel/disk",
            self.codex_config["sandbox_workspace_write"]["writable_roots"],
        )

    def test_disk_cache_has_a_size_limit(self):
        self.assertIn(
            "build --experimental_disk_cache_gc_max_size=10G",
            self.lines,
        )


if __name__ == "__main__":
    unittest.main()
