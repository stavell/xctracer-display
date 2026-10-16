"""Extract towns/cities and main roads from the Geofabrik Bulgaria extract into a pickle.
usage: osm_extract.py bulgaria.osm.pbf OUT.pkl      (needs pyosmium)
Keeps: place=city|town nodes (name, name:en, population) and highway=motorway|trunk|primary|secondary ways
(geometry as lon/lat lists). Links (*_link) are dropped."""
import pickle, sys
import osmium

pbf, out = sys.argv[1], sys.argv[2]
HW = {"motorway", "trunk", "primary", "secondary"}
places, roads = [], []
fp = osmium.FileProcessor(pbf, osmium.osm.NODE | osmium.osm.WAY).with_locations()
for o in fp:
    if o.is_node():
        t = o.tags
        if t.get("place") in ("city", "town"):
            pop = t.get("population", "")
            pop = int(pop.replace(" ", "").replace(",", "")) if pop.replace(" ", "").replace(",", "").isdigit() else 0
            places.append(dict(kind=t["place"], name=t.get("name", ""), name_en=t.get("name:en", ""), pop=pop,
                               lat=o.location.lat, lon=o.location.lon))
    elif o.is_way():
        h = o.tags.get("highway")
        if h in HW:
            pts = [(n.lon, n.lat) for n in o.nodes if n.location.valid()]
            if len(pts) >= 2:
                roads.append(dict(cls=h, ref=o.tags.get("ref", ""), pts=pts))
pickle.dump(dict(places=places, roads=roads), open(out, "wb"))
print(len(places), "places;", len(roads), "road ways;", {c: sum(1 for r in roads if r["cls"] == c) for c in HW})
