"""tools/ci/check_names.py on an invented repository. Run: python3 -I tools/ci/check_names_test.py"""
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

SCRIPT = Path(__file__).resolve().with_name("check_names.py")


class CheckNames(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory()
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        subprocess.run(["git", "init", "-q"], cwd=self.root, check=True)
        self.write("tools/ci/naming_allow.txt", "# kept\ndocs/naming.md .\n* skylauncher-game\\.log\n")
        self.write("docs/naming.md", "The launcher was Skylauncher.\n")
        self.write("src/log.cpp", 'const char* log = "skylauncher-game.log";\n')

    def write(self, path, text):
        (self.root / path).parent.mkdir(parents=True, exist_ok=True)
        (self.root / path).write_text(text)
        subprocess.run(["git", "add", path], cwd=self.root, check=True)

    def run_check(self):
        return subprocess.run([sys.executable, "-I", str(SCRIPT), "--root", str(self.root)], capture_output=True,
                              text=True)

    def test_allowed_names_pass(self):
        result = self.run_check()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def test_a_new_old_name_fails(self):
        self.write("src/ui.cpp", "// Shown as in SkyBuild.\n")
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("src/ui.cpp:1:", result.stdout)

    def test_a_file_name_fails(self):
        self.write("resources/skylauncher.svg", "<svg/>\n")
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("resources/skylauncher.svg:0:", result.stdout)

    def test_an_unused_entry_fails(self):
        self.write("tools/ci/naming_allow.txt", "docs/naming.md .\n* skylauncher-game\\.log\nsrc/gone.cpp .\n")
        result = self.run_check()
        self.assertEqual(result.returncode, 1)
        self.assertIn("allows nothing any more", result.stdout)

    def test_untracked_files_are_not_checked(self):
        (self.root / "notes.txt").write_text("Skylauncher\n")
        self.assertEqual(self.run_check().returncode, 0)


if __name__ == "__main__":
    unittest.main()
