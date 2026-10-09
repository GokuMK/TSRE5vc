#!/usr/bin/env python3
"""Make the English catalogue complete: every active entry's translation is its
engineering-English source, and plural entries get their declared English forms.

Run after `cmake --build build --target update_translations` (lupdate). The file
is edited as text, so lupdate's layout is kept as it is: lupdate followed by this
script changes no line but the translations.
"""
import re
import sys
from pathlib import Path

CATALOGUE = Path(__file__).resolve().parent.parent / 'translations' / 'tsre_en.ts'

PLURAL_FORMS = {
    'con.editor.load.consists.count': ('%n consist', '%n consists'),
    'con.editor.load.trainset.directories.count': ('%n trainset directory', '%n trainset directories'),
    'route.properties.group.object.count': ('Group: %n object', 'Group: %n objects'),
    'route.properties.group.children.count': ('%n object', '%n objects'),
    'route.properties.speedpost.fixed.track.items': ('Fixed %n track item.', 'Fixed %n track items.'),
}

MESSAGE = re.compile(r'<message( [^>]*)?>.*?</message>', re.S)
TRANSLATION = re.compile(r'<translation(?: type="([a-z]+)")?(?: ?/>|>(.*?)</translation>)', re.S)


def escape(text):
    return (text.replace('&', '&amp;').replace('<', '&lt;').replace('>', '&gt;')
            .replace("'", '&apos;').replace('"', '&quot;'))


def sync(text):
    def message(match):
        block = match.group(0)
        attributes = match.group(1) or ''
        found = re.search(r'id="([^"]*)"', attributes)
        message_id = found.group(1) if found else ''
        source = re.search(r'<source>(.*?)</source>', block, re.S)
        if not message_id or source is None or not source.group(1):
            raise SystemExit("English catalogue contains an ID or source-text gap near '%s'." % message_id)
        translation = TRANSLATION.search(block)
        if translation is None:
            raise SystemExit("English catalogue entry has no translation element: '%s'." % message_id)
        if translation.group(1) in ('vanished', 'obsolete'):
            return block
        if 'numerus="yes"' in attributes:
            if message_id not in PLURAL_FORMS:
                raise SystemExit("English plural forms are not declared for '%s'." % message_id)
            forms = re.findall(r'<numerusform>.*?</numerusform>|<numerusform ?/>', translation.group(2) or '', re.S)
            if len(forms) != 2:
                raise SystemExit("English catalogue expected two plural forms for '%s'." % message_id)
            inner = translation.group(2)
            for form, value in zip(forms, PLURAL_FORMS[message_id]):
                inner = inner.replace(form, '<numerusform>%s</numerusform>' % escape(value), 1)
            replacement = '<translation>%s</translation>' % inner
        else:
            # The source's own escaped text, so both read the same.
            replacement = '<translation>%s</translation>' % source.group(1)
        return block[:translation.start()] + replacement + block[translation.end():]

    return MESSAGE.sub(message, text)


def main():
    text = CATALOGUE.read_text(encoding='utf-8')
    CATALOGUE.write_text(sync(text), encoding='utf-8', newline='')
    print('Synchronized complete English translations in %s' % CATALOGUE)


if __name__ == '__main__':
    sys.exit(main())
