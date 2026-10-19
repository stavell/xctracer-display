# Map packs

`bulgaria.pack` (1,724,652 bytes, 82 % of the 2 MiB QSPI flash): Bulgaria + 10 km, one CRC-protected file with the
200 m mean DEM, the 500 m MAX layer, 256 towns (Latin + Cyrillic names), main roads and the attribution strings.
Format: `host/terrain/FORMAT.md`; reader: `firmware/src/platform/terrain/`.

Built with `host/terrain/mkmappack.py` from the cached study grids (about 1 s):

    cd host/terrain && $TERRAIN_DATA/venv/bin/python mkmappack.py ../../maps/bulgaria.pack

Data origin and processing: `host/terrain/README.md`.

## Attribution and licence

**The repository's code licence does not cover `maps/`.** The data here is under the licences below.

* Roads and towns in `bulgaria.pack`: (c) OpenStreetMap contributors, made available under the Open Database License
  (ODbL 1.0), https://opendatacommons.org/licenses/odbl/1-0/ , see https://www.openstreetmap.org/copyright .
  The pack is a derived database and is itself offered under the ODbL: you may copy, adapt and redistribute it if you keep
  this attribution and share derivatives under the same licence. The attribution is also stored in the pack and shown on the box.
* Elevation: "Contains modified Copernicus data (DEM GLO-90) (c) DLR e.V. 2010-2014 and (c) Airbus Defence and Space GmbH
  2014-2018, provided under COPERNICUS by the European Union and ESA; all rights reserved."

See `LICENSE-DATA.md`.

## Rebuilding

Inputs: Copernicus GLO-90 tiles N41..44 x E022..028 (AWS bucket `copernicus-dem-90m`) and the Geofabrik
`bulgaria-latest.osm.pbf` extract (download date of the extract used: unknown, not recorded), reduced by
`host/terrain/fetch.py` and `osm_extract.py` into the cached grids that `mkmappack.py` reads.

## Load onto the box

    .venv/bin/python host/load_pack.py maps/bulgaria.pack

The emulator and tests find it as `../../maps/bulgaria.pack` (from `firmware/emu`) and fall back to
`firmware/emu/fixtures/sopot.pack` when it is missing.
