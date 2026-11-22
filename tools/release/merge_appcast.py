#!/usr/bin/env python3
"""Merges a newly generated Sparkle appcast entry into an existing appcast.xml."""

from __future__ import annotations

import argparse
import pathlib
import xml.etree.ElementTree as ET

SPARKLE_NS = "http://www.andymatuschak.org/xml-namespaces/sparkle"
DC_NS = "http://purl.org/dc/elements/1.1/"

ET.register_namespace("sparkle", SPARKLE_NS)
ET.register_namespace("dc", DC_NS)


def _sparkle_tag(name: str) -> str:
    return f"{{{SPARKLE_NS}}}{name}"


def _item_version(item: ET.Element) -> str | None:
    version_el = item.find(_sparkle_tag("version"))
    if version_el is not None and version_el.text:
        return version_el.text.strip()
    enclosure = item.find("enclosure")
    if enclosure is not None:
        return enclosure.attrib.get(_sparkle_tag("version"))
    return None


def _upsert_subelement(
    parent: ET.Element,
    tag: str,
    text: str,
    attrib: dict[str, str] | None = None,
) -> None:
    existing = parent.find(tag)
    if existing is None:
        existing = ET.SubElement(parent, tag)
    existing.text = text
    if attrib:
        existing.attrib.update(attrib)


def merge_appcast(
    existing_xml: str | None,
    new_xml: str,
    *,
    title: str = "FasterSwiper",
    channel: str | None = None,
    full_release_notes_url: str | None = None,
    release_notes_markdown: str | None = None,
) -> str:
    """Merge <item> elements from new_xml into existing_xml, preserving history."""
    new_root = ET.fromstring(new_xml)
    new_channel = new_root.find("channel")
    if new_channel is None:
        raise ValueError("New appcast is missing <channel>")

    new_items = new_channel.findall("item")
    if not new_items:
        raise ValueError("New appcast contains no <item> elements")

    if existing_xml and existing_xml.strip():
        base_root = ET.fromstring(existing_xml)
        base_channel = base_root.find("channel")
        if base_channel is None:
            raise ValueError("Existing appcast is missing <channel>")
    else:
        base_root = ET.Element("rss", {"version": "2.0"})
        base_channel = ET.SubElement(base_root, "channel")
        title_el = ET.SubElement(base_channel, "title")
        title_el.text = title

    for item in new_items:
        if channel:
            _upsert_subelement(item, _sparkle_tag("channel"), channel)
        if full_release_notes_url:
            _upsert_subelement(
                item,
                _sparkle_tag("fullReleaseNotesLink"),
                full_release_notes_url,
            )
        if release_notes_markdown and release_notes_markdown.strip():
            _upsert_subelement(
                item,
                "description",
                release_notes_markdown.strip(),
                {_sparkle_tag("format"): "markdown"},
            )

    new_versions = {v for item in new_items if (v := _item_version(item)) is not None}

    # Remove any pre-existing items with the same sparkle:version (idempotent re-runs).
    for old_item in list(base_channel.findall("item")):
        if _item_version(old_item) in new_versions:
            base_channel.remove(old_item)

    # Insert new items ahead of existing items (right after channel metadata elements).
    insert_index = 0
    for idx, child in enumerate(list(base_channel)):
        if child.tag == "item":
            insert_index = idx
            break
        insert_index = idx + 1

    for offset, item in enumerate(new_items):
        base_channel.insert(insert_index + offset, item)

    ET.indent(base_root, space="  ")
    serialized = ET.tostring(base_root, encoding="unicode", xml_declaration=True)
    return serialized + "\n"


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--existing",
        type=pathlib.Path,
        required=True,
        help="Path to existing appcast.xml (created if absent).",
    )
    parser.add_argument(
        "--new",
        type=pathlib.Path,
        required=True,
        help="Path to newly generated appcast.xml from Sparkle generate_appcast.",
    )
    parser.add_argument(
        "--output",
        type=pathlib.Path,
        required=True,
        help="Destination path for merged appcast.xml.",
    )
    parser.add_argument(
        "--title",
        default="FasterSwiper",
        help="Channel title when initializing a new appcast.xml.",
    )
    parser.add_argument(
        "--channel",
        default=None,
        help="Optional Sparkle update channel (e.g. 'beta').",
    )
    parser.add_argument(
        "--full-release-notes-url",
        default=None,
        help="Optional URL to full release notes (e.g. GitHub release page).",
    )
    parser.add_argument(
        "--release-notes-file",
        type=pathlib.Path,
        default=None,
        help="Optional Markdown file containing release notes for <description>.",
    )
    args = parser.parse_args()

    existing_xml = args.existing.read_text(encoding="utf-8") if args.existing.exists() else None
    new_xml = args.new.read_text(encoding="utf-8")
    release_notes = (
        args.release_notes_file.read_text(encoding="utf-8")
        if args.release_notes_file and args.release_notes_file.exists()
        else None
    )

    merged = merge_appcast(
        existing_xml,
        new_xml,
        title=args.title,
        channel=args.channel,
        full_release_notes_url=args.full_release_notes_url,
        release_notes_markdown=release_notes,
    )

    # Validate well-formed XML before writing.
    ET.fromstring(merged)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(merged, encoding="utf-8")


if __name__ == "__main__":
    main()
