#!/usr/bin/env python3
"""Check the translation catalogues without changing them.

- every message has a lowercase dotted ID, unique, with engineering-English source;
- English is complete: each translation equals its source (plural forms filled);
- Polish has the same IDs and sources as English;
- finished translations keep their source's placeholders (%1, %L1, %n).

Exits with status 1 and the first problem found. Also run by ctest ("translations").
"""
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
ENGLISH = ROOT / 'translations' / 'tsre_en.ts'
POLISH = ROOT / 'translations' / 'tsre_pl.ts'
ID = re.compile(r'[a-z0-9]+(?:\.[a-z0-9]+)*')


class Invalid(Exception):
    pass


def text(element):
    return ''.join(element.itertext()) if element is not None else ''


def placeholders(value):
    return sorted(set(re.findall(r'%(?:L?\d+|n)', value)))


def check_placeholders(message_id, source, translation):
    if placeholders(source) != placeholders(translation):
        raise Invalid("Placeholder mismatch for '%s': source [%s] translation [%s]." % (
            message_id, ', '.join(placeholders(source)), ', '.join(placeholders(translation))))


def validate(english_path=ENGLISH, polish_path=POLISH):
    english = {}
    for message in ET.parse(str(english_path)).getroot().iter('message'):
        message_id = message.get('id') or ''
        if not ID.fullmatch(message_id):
            raise Invalid("Translation ID is not lowercase dotted syntax: '%s' (a tr() call?)." % message_id)
        if message_id in english:
            raise Invalid("Duplicate English translation ID: '%s'." % message_id)
        source = text(message.find('source'))
        translation = message.find('translation')
        if not source:
            raise Invalid("English translation ID has no engineering source text: '%s'." % message_id)
        if translation.get('type') == 'unfinished':
            raise Invalid("English translation is unfinished: '%s'." % message_id)
        if message.get('numerus') == 'yes':
            for form in translation.findall('numerusform'):
                if not text(form):
                    raise Invalid("English plural form is empty: '%s'." % message_id)
                check_placeholders(message_id, source, text(form))
        else:
            if text(translation) != source:
                raise Invalid("English translation differs from its engineering source for '%s'." % message_id)
            check_placeholders(message_id, source, text(translation))
        english[message_id] = source

    polish = set()
    for message in ET.parse(str(polish_path)).getroot().iter('message'):
        message_id = message.get('id') or ''
        if message_id in polish:
            raise Invalid("Duplicate Polish translation ID: '%s'." % message_id)
        if message_id not in english:
            raise Invalid("Polish translation ID is absent from English: '%s'." % message_id)
        source = text(message.find('source'))
        if source != english[message_id]:
            raise Invalid("English and Polish engineering sources differ for '%s'." % message_id)
        translation = message.find('translation')
        if translation.get('type') != 'unfinished':
            forms = translation.findall('numerusform') if message.get('numerus') == 'yes' else [translation]
            for form in forms:
                check_placeholders(message_id, source, text(form))
        polish.add(message_id)

    if len(polish) != len(english):
        raise Invalid('English and Polish catalogues differ in ID count (%d vs %d).' % (len(english), len(polish)))
    return len(english)


def main():
    try:
        count = validate()
    except Invalid as problem:
        print(problem, file=sys.stderr)
        return 1
    print('Validated %d translation IDs; English is complete and Polish placeholders are consistent.' % count)
    return 0


if __name__ == '__main__':
    sys.exit(main())
