"""Public workflow regressions. Run with Python 3.11 and requirements-cli.txt."""
import json
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import unittest
import zipfile

import yaml

ROOT = Path(__file__).resolve().parents[1]


class PublicCLITests(unittest.TestCase):
    def cli(self, root, *args, cpu=None):
        env = os.environ.copy()
        env["PATH"] = str(Path(sys.executable).parent) + ":" + env["PATH"]
        command = [sys.executable, "-m", "hellohpc", *args]
        if cpu is not None:
            command = ["taskset", "-c", str(cpu), *command]
        return subprocess.run(command,
            cwd=root, env=env, capture_output=True, text=True, timeout=25)

    def fixture(self, root):
        for name in ("src", "judge"):
            shutil.copytree(ROOT / name, root / name, ignore=shutil.ignore_patterns("__pycache__"))
        for name in ("timing.py", "env.sh", "problem.yaml"):
            shutil.copyfile(ROOT / name, root / name)
        (root / "data").mkdir()
        for name in ("sample", "public-large-b"):
            for suffix in (".npz", ".ref.txt"):
                shutil.copyfile(ROOT / f"data/sample{suffix}", root / f"data/{name}{suffix}")
        # Only this temporary fixture uses tiny performance data and one core.
        config = yaml.safe_load((root / "problem.yaml").read_text())
        config["workflow"]["matrix"]["cpu_count"] = [1, 1]
        (root / "problem.yaml").write_text(yaml.safe_dump(config, sort_keys=False))

    def test_actual_public_config_and_sample(self):
        config = yaml.safe_load((ROOT / "problem.yaml").read_text())
        matrix = config["workflow"]["matrix"]
        self.assertEqual(["sample", "public-large-b"], matrix["id"])
        self.assertEqual([0, 50], matrix["score"])
        self.assertEqual([1, 32], matrix["cpu_count"])
        self.assertEqual([1, 1200], matrix["full_time_ms"])
        self.assertEqual([2, 35000], matrix["max_time_ms"])
        self.assertEqual(0, self.cli(ROOT, "validate").returncode)
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / "result.json"
            result = self.cli(ROOT, "test", "--case", "sample", "--output", str(out))
            self.assertEqual(0, result.returncode, result.stderr)
            payload = json.loads(out.read_text())
            self.assertEqual("completed", payload["run"]["status"])
            self.assertEqual(["accepted", "skipped"], [c["status"] for c in payload["cases"]])
            self.assertEqual(50, payload["max-score"])

    def test_default_test_runs_complete_public_workflow(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            result = self.cli(root, "test")
            self.assertEqual(0, result.returncode, result.stderr)
            payload = json.loads((root / "result.json").read_text())
            self.assertEqual(50, payload["score"], result.stdout + result.stderr)
            self.assertEqual([1, 3], [len(c["metrics"]["performance"]["samples"])
                                     for c in payload["cases"]])
            self.assertTrue(all(c["checks"]["correctness"]["passed"] for c in payload["cases"]))
            self.assertTrue(all(c["metrics"]["performance"]["aggregation"] == "max"
                                for c in payload["cases"]))

    def test_submission_remains_two_files_and_env_is_loaded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            (root / "env.sh").write_text("export KERNEL_PUBLIC_TEST=yes\n")
            solver = root / "src/solver.py"
            solver.write_text(solver.read_text().replace("import math", "import os\n"
                "assert os.environ['KERNEL_PUBLIC_TEST'] == 'yes'\nimport math"))
            result = self.cli(root, "pack", "--output", "test.zip")
            self.assertEqual(0, result.returncode, result.stderr)
            with zipfile.ZipFile(root / "test.zip") as bundle:
                self.assertEqual({"src/solver.py", "env.sh"}, set(bundle.namelist()))
            (root / "env.sh").write_text("false\n")
            result = self.cli(root, "test", "--submission", "test.zip")
            payload = json.loads((root / "result.json").read_text())
            self.assertEqual(50, payload["score"], result.stdout + result.stderr)

    def test_missing_public_resources_fail_validation(self):
        for relative in ("judge/run_case.py", "judge/limits.py", "timing.py",
                         "data/public-large-b.npz", "data/sample.ref.txt"):
            with self.subTest(relative=relative), tempfile.TemporaryDirectory() as tmp:
                root = Path(tmp)
                self.fixture(root)
                (root / relative).unlink()  # disposable fixture only
                result = self.cli(root, "validate")
                self.assertEqual(3, result.returncode, result.stdout + result.stderr)
                self.assertIn("required workspace", result.stderr)

    def test_wrong_answer_has_no_performance_score(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            solver = root / "src/solver.py"
            solver.write_text(solver.read_text().replace("return output", "return output + 100"))
            result = self.cli(root, "test", "--case", "public-large-b")
            payload = json.loads((root / "result.json").read_text())
            self.assertEqual("wrong_answer", payload["cases"][1]["status"], result.stderr)
            self.assertEqual(0, payload["score"])

    def test_function_timeout_is_reported_as_time_limit_exceeded(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            solver = root / "src/solver.py"
            solver.write_text(solver.read_text().replace("import math", "import math\nimport time")
                              .replace("q_count, dimension =", "time.sleep(2)\n    q_count, dimension ="))
            config_path = root / "problem.yaml"
            config = yaml.safe_load(config_path.read_text())
            for step in config["workflow"]["steps"]:
                if step["id"] == "benchmark":
                    step["run"] += " --call-timeout-seconds=0.1"
            config_path.write_text(yaml.safe_dump(config, sort_keys=False))
            result = self.cli(root, "test", "--case", "public-large-b", "--set", "warmup=0")
            payload = json.loads((root / "result.json").read_text())
            self.assertEqual("time_limit_exceeded", payload["cases"][1]["status"], result.stderr)
            self.assertEqual(0, payload["score"])

    def test_performance_requires_32_physical_cores(self):
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            self.fixture(root)
            config_path = root / "problem.yaml"
            config = yaml.safe_load(config_path.read_text())
            config["workflow"]["matrix"]["cpu_count"] = [1, 32]
            config_path.write_text(yaml.safe_dump(config, sort_keys=False))
            result = self.cli(root, "--verbose", "test", "--case", "public-large-b",
                              cpu=min(os.sched_getaffinity(0)))
            payload = json.loads((root / "result.json").read_text())
            self.assertEqual(4, result.returncode)
            self.assertEqual("infrastructure_error", payload["run"]["status"])
            self.assertIn("need 32 physical cores, only 1 available", result.stdout + result.stderr)
            self.assertEqual(0, payload["score"])


if __name__ == "__main__":
    unittest.main()
