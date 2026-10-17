#!/usr/bin/env python3
"""Read release identity from an archive and structured fields from its appcast."""
import json
import plistlib
import sys
import xml.etree.ElementTree as ET
import zipfile

archive, feed = sys.argv[1:]
with zipfile.ZipFile(archive) as payload:
    name = 'Callayers.app/Contents/Info.plist'
    assert payload.namelist().count(name) == 1, 'Missing or duplicate app identity'
    assert payload.getinfo(name).file_size < 65536, 'Oversized app identity'
    info = plistlib.loads(payload.read(name))
data = open(feed, 'rb').read(1024 * 1024 + 1)
assert len(data) <= 1024 * 1024 and b'<!DOCTYPE' not in data and b'<!ENTITY' not in data, 'Invalid appcast'
root = ET.fromstring(data)
assert root.tag == 'rss' and len(root.findall('channel')) == 1, 'Invalid appcast root'
namespace = '{http://www.andymatuschak.org/xml-namespaces/sparkle}'
items = []
for item in root.findall('./channel/item'):
    enclosures = item.findall('enclosure')
    assert len(enclosures) == 1 and len(item.findall(namespace + 'deltas')) == 0, 'Unexpected update enclosure'
    def value(name):
        found = item.findall(namespace + name)
        assert len(found) == 1, 'Missing or duplicate update field'
        return found[0].text
    items.append({'build': value('version'), 'version': value('shortVersionString'),
                  'minimumSystemVersion': value('minimumSystemVersion'),
                  'enclosure': enclosures[0].attrib})
print(json.dumps({'info': info, 'items': items}))
