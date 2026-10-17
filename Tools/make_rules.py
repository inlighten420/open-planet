# Makes Godot/rules/<code>.json for every country and state: the limits a road has with no speed sign, from the
# OpenStreetMap wiki's "Default speed limits" (machine-read as osm-legal-default-speeds, Tobias Zwick and contributors,
# BSD-3; the wiki's text CC BY-SA 2.0), and which side each country drives on (the REST Countries data, MPL 2.0).
# Hand-written styles and cross-sections in an existing file are kept; the UK and South Africa keep their built-in
# rules. Run again to refresh:  python Tools/make_rules.py
import json, os, re, sys, tempfile, urllib.request
sys.stdout.reconfigure(encoding='utf-8')
D = os.path.join(tempfile.gettempdir(), 'geodrive_rules') + '/'
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'Godot', 'rules') + '/'
os.makedirs(D, exist_ok=True)
for name, url in (('lds.json', 'https://raw.githubusercontent.com/westnordost/osm-legal-default-speeds/master/demo/distribution/legal_default_speeds.json'),
                  ('countries.json', 'https://gitlab.com/restcountries/restcountries/-/raw/master/src/main/resources/countriesV3.1.json')):
    if not os.path.exists(D + name):
        urllib.request.urlretrieve(url, D + name)
lds = json.load(open(D + 'lds.json', encoding='utf-8'))
limits = lds['speedLimitsByCountryCode']
countries = json.load(open(D + 'countries.json', encoding='utf-8'))
side = {c['cca2']: c.get('car', {}).get('side', 'right') for c in countries}
side['VI'] = 'left'	# (the US Virgin Islands keep left; the data says right)
cname = {c['cca2']: c['name']['common'] for c in countries}

state_names = {
    'AU-ACT': 'Australian Capital Territory', 'AU-NSW': 'New South Wales', 'AU-NT': 'Northern Territory', 'AU-QLD': 'Queensland',
    'AU-SA': 'South Australia', 'AU-TAS': 'Tasmania', 'AU-VIC': 'Victoria', 'AU-WA': 'Western Australia', 'BE-BRU': 'Brussels',
    'BE-VLG': 'Flanders', 'BE-WAL': 'Wallonia', 'GB-SCT': 'Scotland', 'CA-AB': 'Alberta', 'CA-BC': 'British Columbia',
    'CA-MB': 'Manitoba', 'CA-NB': 'New Brunswick', 'CA-NL': 'Newfoundland and Labrador', 'CA-NS': 'Nova Scotia',
    'CA-NT': 'Northwest Territories', 'CA-NU': 'Nunavut', 'CA-ON': 'Ontario', 'CA-PE': 'Prince Edward Island', 'CA-QC': 'Quebec',
    'CA-SK': 'Saskatchewan', 'CA-YT': 'Yukon'}


def kmh(value):
    """km/h from a maxspeed value ("50", "30 mph", "walk"); None if not a number"""
    if value is None:
        return None, False
    v = str(value).strip()
    if v == 'walk':
        return 7, False
    m = re.match(r'^(\d+(?:\.\d+)?)\s*(mph)?$', v)
    if not m:
        return None, False
    n = float(m.group(1))
    return (round(n * 1.609) if m.group(2) else round(n)), bool(m.group(2))


def entry(entries, *names):
    for name in names:
        for e in entries:
            if e.get('name', '(default)') == name:
                return e
    return None


def limit(e):
    if not e:
        return None, False
    t = e.get('tags', {})
    value, mph = kmh(t.get('maxspeed'))
    if value is None and t.get('maxspeed') == 'none':
        value, mph = kmh(t.get('maxspeed:advisory', '130'))
        value = min(value or 130, 130)
    if value is None and 'maxspeed:advisory' in t:
        value, mph = kmh(t['maxspeed:advisory'])
    return value, mph


written = []
for code, entries in sorted(limits.items()):
    if code.startswith('GB') or code == 'ZA':
        continue	# (the UK's own rules: its built-in style, its national limits by road type)
    country = code.split('-')[0]
    urban, m1 = limit(entry(entries, 'urban'))
    residential, m2 = limit(entry(entries, 'residential district', 'urban'))
    rural, m3 = limit(entry(entries, 'rural', '(default)'))
    motorway, m4 = limit(entry(entries, 'motorway', 'rural motorway', 'US interstate highway', 'rural US interstate highway',
                               'rural dual carriageway with 2 or more lanes in each direction', 'dual carriageway with 2 or more lanes in each direction'))
    motorroad, m5 = limit(entry(entries, 'motorroad', 'rural dual carriageway with 2 or more lanes in each direction', 'rural dual carriageway',
                                'rural road with 2 or more lanes in each direction'))
    living, _ = limit(entry(entries, 'living street'))
    mph = m1 or m2 or m3 or m4 or m5
    if not any([urban, rural, motorway]):
        continue	# (no figures: the country file's, or the built-in, stand)
    rural = rural or urban
    urban = urban or rural
    residential = residential or urban
    speeds = {}
    if motorway:
        speeds['motorway'] = motorway
        speeds['motorway_link'] = min(motorway, max(rural or 0, 60))
    if motorroad or rural:
        speeds['trunk'] = motorroad or rural
        speeds['trunk_link'] = min(motorroad or rural, rural)
    for c in ('primary', 'secondary', 'tertiary', 'unclassified'):
        speeds[c] = rural
    speeds['residential'] = residential
    speeds['living_street'] = living or min(residential, 20)
    speeds['service'] = min(residential, 30)
    # the country's own road types, where their filters name road classes: on country roads (or everywhere), or in towns
    urban_by_class = {}
    skip = ('lanes', 'carriageway', 'school', 'unpaved', 'width', 'retail', 'commercial', 'playground', 'park', 'lit', 'surface')
    for e in entries:
        t = lds['roadTypesByName'].get(e.get('name', ''), {})
        value, m6 = limit(e)
        if value is None:
            continue
        for f in (t.get('filter', ''), t.get('fuzzyFilter', '')):
            classes = re.findall(r'highway\s*[~=]\s*"?([a-z_|]+)', f)
            if not classes:
                continue
            if any(w in f for w in skip if not (w == 'lit' and False)):
                break
            mph = mph or m6
            listed = [c for group in classes for c in group.split('|') if c]
            for c in listed:
                if '{urban}' in f:
                    urban_by_class[c] = value
                elif '{rural}' in f or True:
                    speeds[c] = value
            break
    for c in ('residential', 'living_street', 'service'):
        if c in urban_by_class:
            speeds[c] = urban_by_class[c]
    name = state_names.get(code) or cname.get(country, code)
    if '-' in code and code not in state_names:
        name = code
    path = OUT + code + '.json'
    old = json.load(open(path, encoding='utf-8')) if os.path.exists(path) else {}
    rules = {
        'name': old.get('name', name),
        'drive': side.get(country, 'right'),
        'mph': mph,
    }
    if 'style' in old:
        rules['style'] = old['style']
    rules['urban_kmh'] = urban
    if urban_by_class:
        rules['urban_speed_kmh'] = urban_by_class
    rules['speed_kmh'] = speeds
    if 'sections' in old:
        rules['sections'] = old['sections']
    rules['about'] = ('Default (unsigned) limits from the OpenStreetMap wiki\'s "Default speed limits" (via osm-legal-default-speeds, '
                      'Tobias Zwick and contributors): ' + ', '.join('%s %s' % (e.get('name', 'default'), e.get('tags', {}).get('maxspeed', e.get('tags', {}).get('maxspeed:advisory', '-')))
                                                                   for e in entries if e.get('name', '(default)') in ('(default)', 'urban', 'rural', 'motorway', 'motorroad', 'residential district', 'living street')))
    if old.get('about') and 'OpenStreetMap wiki' not in old['about']:
        rules['about'] += '. ' + old['about']
    json.dump(rules, open(path, 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent='\t')
    written.append(code)

# every other country: its side at least (the built-in list has it too, but a file says so plainly)
for code, s in sorted(side.items()):
    path = OUT + code + '.json'
    if os.path.exists(path):
        continue
    json.dump({'name': cname.get(code, code), 'drive': s, 'mph': False,
               'about': 'No national default limits recorded yet (the OpenStreetMap wiki\'s "Default speed limits" has none): the side only.'},
              open(path, 'w', encoding='utf-8', newline='\n'), ensure_ascii=False, indent='\t')
    written.append(code + '*')
print(len(written), 'files;', len([w for w in written if w.endswith('*')]), 'with the side only')
for c in ('ZA', 'GB', 'DE', 'FR', 'US-CA', 'AU-NSW', 'JP', 'IN', 'BR', 'MC', 'IE', 'NL'):
    print(c, open(OUT + c + '.json', encoding='utf-8').read()[:400].replace('\n', ' '))
