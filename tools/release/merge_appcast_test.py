#!/usr/bin/env python3
"""Unit tests for merge_appcast.py."""

from __future__ import annotations

import unittest
import xml.etree.ElementTree as ET

import merge_appcast

SPARKLE_NS = merge_appcast.SPARKLE_NS


def _make_generated_appcast(version: str, sig: str = "SIG123") -> str:
    return f"""<?xml version="1.0" standalone="yes"?>
<rss xmlns:sparkle="{SPARKLE_NS}" version="2.0">
  <channel>
    <title>FasterSwiper</title>
    <item>
      <title>{version}</title>
      <pubDate>Sat, 21 Nov 2026 20:00:00 +0000</pubDate>
      <sparkle:version>{version}</sparkle:version>
      <sparkle:shortVersionString>{version}</sparkle:shortVersionString>
      <sparkle:minimumSystemVersion>26.0</sparkle:minimumSystemVersion>
      <enclosure url="https://github.com/mgbowen/FasterSwiper/releases/download/v{version}/FasterSwiper.dmg" length="12345" type="application/octet-stream" sparkle:edSignature="{sig}" />
    </item>
  </channel>
</rss>
"""


class MergeAppcastTest(unittest.TestCase):
    def test_initial_appcast_creation(self) -> None:
        merged = merge_appcast.merge_appcast(
            existing_xml=None,
            new_xml=_make_generated_appcast("1.7.0", "SIG_170"),
            full_release_notes_url="https://github.com/mgbowen/FasterSwiper/releases/tag/v1.7.0",
            release_notes_markdown="## Changes\n- Added automatic updates",
        )
        root = ET.fromstring(merged)
        items = root.findall("./channel/item")
        self.assertEqual(len(items), 1)
        self.assertEqual(items[0].findtext(f"{{{SPARKLE_NS}}}version"), "1.7.0")
        self.assertEqual(
            items[0].findtext(f"{{{SPARKLE_NS}}}fullReleaseNotesLink"),
            "https://github.com/mgbowen/FasterSwiper/releases/tag/v1.7.0",
        )
        desc = items[0].find("description")
        self.assertIsNotNone(desc)
        self.assertEqual(desc.attrib.get(f"{{{SPARKLE_NS}}}format"), "markdown")
        self.assertIn("Added automatic updates", desc.text or "")
        self.assertIsNone(items[0].find(f"{{{SPARKLE_NS}}}channel"))

    def test_prepends_new_release_and_preserves_history(self) -> None:
        v170 = merge_appcast.merge_appcast(
            existing_xml=None,
            new_xml=_make_generated_appcast("1.7.0", "SIG_170"),
        )
        merged = merge_appcast.merge_appcast(
            existing_xml=v170,
            new_xml=_make_generated_appcast("1.8.0", "SIG_180"),
        )
        root = ET.fromstring(merged)
        versions = [
            el.findtext(f"{{{SPARKLE_NS}}}version")
            for el in root.findall("./channel/item")
        ]
        self.assertEqual(versions, ["1.8.0", "1.7.0"])

    def test_idempotent_upsert_replaces_same_version(self) -> None:
        v170 = merge_appcast.merge_appcast(
            existing_xml=None,
            new_xml=_make_generated_appcast("1.7.0", "SIG_OLD"),
        )
        merged = merge_appcast.merge_appcast(
            existing_xml=v170,
            new_xml=_make_generated_appcast("1.7.0", "SIG_NEW"),
        )
        root = ET.fromstring(merged)
        items = root.findall("./channel/item")
        self.assertEqual(len(items), 1)
        enclosure = items[0].find("enclosure")
        self.assertIsNotNone(enclosure)
        self.assertEqual(enclosure.attrib.get(f"{{{SPARKLE_NS}}}edSignature"), "SIG_NEW")

    def test_prerelease_sets_beta_channel(self) -> None:
        merged = merge_appcast.merge_appcast(
            existing_xml=None,
            new_xml=_make_generated_appcast("1.8.0-beta.1", "SIG_BETA"),
            channel="beta",
        )
        root = ET.fromstring(merged)
        item = root.find("./channel/item")
        self.assertIsNotNone(item)
        self.assertEqual(item.findtext(f"{{{SPARKLE_NS}}}channel"), "beta")


if __name__ == "__main__":
    unittest.main()
