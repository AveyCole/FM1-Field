"""Fetch public test responses as Aporee's map does; run from the repository root."""
from pathlib import Path
from urllib.request import Request, urlopen
headers={'X-Requested-With':'XMLHttpRequest','Referer':'https://aporee.org/maps/'}
for path, endpoint in [('tmp-locations.json','getAllLocations'),('tmp-playlist.json','getPlaylist?locids%5B%5D=23019')]:
    request=Request('https://aporee.org/api/maps/'+endpoint,headers=headers)
    with urlopen(request,timeout=60) as response:
        Path(path).write_bytes(response.read())
    print(path)
