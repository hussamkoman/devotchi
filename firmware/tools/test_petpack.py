#!/usr/bin/env python3
"""Tests of petpack.py: every pet of pets/ passes `check`, built-in generation, and the lint rules.

Standard library only; run by the host test project (ctest test_petpack) or directly.
"""
import copy
import importlib.util
import json
import os
import subprocess
import sys
import tempfile
import unittest

TOOLS = os.path.dirname(os.path.abspath(__file__))
PETS = os.path.normpath(os.path.join(TOOLS, "..", "..", "pets"))
TOOL = os.path.join(TOOLS, "petpack.py")

spec = importlib.util.spec_from_file_location("petpack", TOOL)
petpack = importlib.util.module_from_spec(spec)
spec.loader.exec_module(petpack)


def source(pid):
    return petpack.load(os.path.join(PETS, pid + ".pet.json"))


def run(*args):
    return subprocess.run([sys.executable, "-I", TOOL, *args], capture_output=True, text=True, check=False)


class CheckEveryPet(unittest.TestCase):
    def test_every_pet_passes_check_and_has_a_golden_pack(self):
        sources = petpack.pet_sources(PETS)
        self.assertGreaterEqual(len(sources), 3)
        r = run("check", *sources)
        self.assertEqual(r.returncode, 0, r.stderr)
        results = [json.loads(line) for line in r.stdout.splitlines()]
        self.assertTrue(all(x["ok"] and x["golden"] for x in results), results)

    def test_the_template_is_a_valid_pet(self):
        r = run("check", os.path.join(PETS, "_template.pet.json"))
        self.assertNotEqual(r.returncode, 2, r.stderr)
        self.assertIn("does not match the file name", r.stderr)
        petpack.encode(petpack.load(os.path.join(PETS, "_template.pet.json")))

    def test_a_changed_source_differs_from_its_golden_pack(self):
        with tempfile.TemporaryDirectory() as d:
            src = source("sprout")
            src["rev"] = 2
            os.mkdir(os.path.join(d, "golden"))
            for f in ("sprout.pet", "sprout.crc32"):
                with open(os.path.join(PETS, "golden", f), "rb") as a, open(os.path.join(d, "golden", f), "wb") as b:
                    b.write(a.read())
            path = os.path.join(d, "sprout.pet.json")
            with open(path, "w", encoding="utf-8") as fh:
                json.dump(src, fh)
            r = run("check", path)
            self.assertEqual(r.returncode, 1)
            self.assertIn("differs from the encoded source", r.stderr)
            self.assertEqual(run("golden", path).returncode, 0)
            self.assertEqual(run("check", path).returncode, 0, run("check", path).stderr)


class Builtins(unittest.TestCase):
    def test_generates_the_listed_packs_default_first(self):
        h, c = petpack.builtins_c(PETS)
        ids = petpack.builtin_ids(PETS)
        self.assertIn("#define PET_BUILTIN_COUNT    %d\n" % len(ids), h)
        self.assertIn('#define PET_BUILTIN_DEFAULT  "%s"\n' % ids[0], h)
        for pid in ids:
            with open(os.path.join(PETS, "golden", pid + ".pet"), "rb") as fh:
                pack = fh.read()
            self.assertIn("static const uint8_t PACK_%s[%d] = {" % (pid.upper(), len(pack)), c)
            tagline = source(pid).get("tagline", "")
            self.assertIn('{"%s", PACK_%s, sizeof(PACK_%s), "%s"},' % (pid, pid.upper(), pid.upper(), tagline), c)
        self.assertLess(c.index('{"%s",' % ids[0]), c.index('{"%s",' % ids[-1]))

    def test_escapes_the_tagline_and_refuses_a_bad_one(self):
        with tempfile.TemporaryDirectory() as d:
            src = source("sprout")
            src["tagline"] = 'a "quoted" \\ pet'
            with open(os.path.join(d, "sprout.pet.json"), "w", encoding="utf-8") as fh:
                json.dump(src, fh)
            with open(os.path.join(d, "builtin.txt"), "w", encoding="utf-8") as fh:
                fh.write("sprout\n")
            _, c = petpack.builtins_c(d)
            self.assertIn('sizeof(PACK_SPROUT), "a \\"quoted\\" \\\\ pet"},', c)
        for bad in ("x" * 33, "caf\u00e9", 7):
            src = source("sprout")
            src["tagline"] = bad
            with self.assertRaisesRegex(petpack.PackError, "tagline"):
                petpack.encode(src)

    def test_refuses_a_list_without_its_source_or_with_a_repeat(self):
        with tempfile.TemporaryDirectory() as d:
            with open(os.path.join(d, "builtin.txt"), "w", encoding="utf-8") as fh:
                fh.write("# comment\nghost\n")
            r = run("builtins", d, os.path.join(d, "out"))
            self.assertEqual(r.returncode, 1)
            self.assertIn("ghost.pet.json", r.stderr)
            with open(os.path.join(d, "builtin.txt"), "w", encoding="utf-8") as fh:
                fh.write("devo\ndevo\n")
            self.assertIn("each once", run("builtins", d, os.path.join(d, "out")).stderr)


class Lint(unittest.TestCase):
    def lint(self, src, others=("devo", "mochi", "ember", "sprout")):
        return petpack.lint(petpack.lint_input(src), len(petpack.encode(src)),
                            [petpack.lint_input(source(o)) for o in others if o != src["id"]])

    def test_a_copied_silhouette_is_an_error(self):
        twin = source("ember")
        twin["id"] = "twin"
        errors, _ = self.lint(twin)
        self.assertTrue(any("night outline is 100% the same as ember" in e for e in errors), errors)

    def test_moods_that_only_differ_in_colour_are_an_error(self):
        flat = source("sprout")
        flat["id"] = "flat"
        idle = dict(zip(petpack.POSE_CHANNELS, petpack.SHARED_POSE["idle"]))
        idle["lid"] = "off"
        flat["overrides"] = {"happy": {k: v for k, v in idle.items() if k in petpack.CHANNELS}}
        flat["aux"]["happy"] = flat["aux"].get("idle", [0, 0, 0, 0])
        errors, _ = self.lint(flat, others=())
        self.assertTrue(any(e.startswith("happy and idle look the same without colour") for e in errors), errors)

    def test_a_body_on_a_semantic_hue_is_an_error_and_a_near_one_a_warning(self):
        mint = source("sprout")
        mint["id"] = "mint"
        mint["palette"]["base"] = "#5FD38D"
        errors, _ = self.lint(mint, others=())
        self.assertTrue(any("from the UI's mint" in e for e in errors), errors)
        near = source("devo")
        near["palette"]["moods"]["happy"] = "#FFB84D"
        _, warnings = self.lint(near, others=())
        self.assertTrue(any("#FFB84D (happy" in w and "amber" in w for w in warnings), warnings)

    def test_the_pets_of_pets_dir_pass(self):
        for pid in ("devo", "mochi", "ember", "sprout"):
            errors, _ = self.lint(source(pid))
            self.assertEqual(errors, [], pid)

    def test_mood_pose_applies_overrides(self):
        src = copy.deepcopy(source("sprout"))
        src["overrides"] = {"idle": {"mouth_curve": -30, "lid": "off"}}
        pose = petpack.mood_pose(petpack.lint_input(src), "idle")
        self.assertEqual(pose["mouth_curve"], -30)
        self.assertEqual(pose["lid"], petpack.LID_OFF)


if __name__ == "__main__":
    unittest.main()
