# duckdb-overture

DuckDB extension for [Overture Maps](https://overturemaps.org/) data. Category normalization, spatial tiling, and convenience macros for reading Overture parquet files from S3.

Built against DuckDB v1.5.1. Works in native DuckDB and DuckDB-WASM.

## Install

```sql
SET custom_extension_repository = 'https://cubilica.github.io/duckdb-overture';
INSTALL overture;
LOAD overture;
```

From a local build:

```sql
LOAD 'build/release/extension/overture/overture.duckdb_extension';
```

## Configuration

All macros default to the official Overture S3 bucket and release `2026-03-18.0`. Override with variables:

```sql
SET VARIABLE overture_release = '2025-12-01.0';
SET VARIABLE overture_s3_base = 's3://my-mirror/overture';
```

Or pass `release:=` per-query. Check https://overturemaps.org/download for available releases.

## Functions

### `overture_category(category VARCHAR) -> VARCHAR`

Maps an Overture place category to a normalized one. Returns NULL for unknown categories.

```sql
SELECT overture_category('coffee_shop');   -- 'cafe'
SELECT overture_category('pub');           -- 'bar'
SELECT overture_category('metro_station'); -- 'transit'
SELECT overture_category('unknown');       -- NULL
```

80+ Overture categories map to ~30 normalized ones (cafe, restaurant, bar, grocery, transit, school, ...).

### `st_tile_index(lat DOUBLE, lng DOUBLE [, cell_size DOUBLE]) -> VARCHAR`

Spatial grid tile key. Default cell size is 0.1 degrees (~11km lat, ~7km lng at European latitudes). Truncation toward zero, same as Python `int()`.

```sql
SELECT st_tile_index(52.52, 13.405);       -- '525_134' (Berlin)
SELECT st_tile_index(48.856, 2.352);       -- '488_23'  (Paris)
SELECT st_tile_index(38.72, -9.14);        -- '387_-91' (Lisbon)
SELECT st_tile_index(52.52, 13.405, 1.0);  -- '52_13'   (coarser)
SELECT st_tile_index(52.52, 13.405, 0.01); -- '5252_1340' (finer, ~1km)
```

### `overture_categories() -> TABLE`

All known category mappings as a table.

```sql
SELECT * FROM overture_categories() WHERE mapped_category = 'cafe';
-- overture_category | mapped_category
-- cafe              | cafe
-- coffee_shop       | cafe
```

### `read_overture_places(...)`

Table macro that reads Overture places from S3 with bbox filtering, lat/lng extraction, and category mapping. Requires `spatial` and `httpfs` extensions.

| param | default | |
|---|---|---|
| `release` | `'2026-03-18.0'` | Overture release tag |
| `west` | `-180.0` | bbox west |
| `east` | `180.0` | bbox east |
| `south` | `-90.0` | bbox south |
| `north` | `90.0` | bbox north |

```sql
LOAD spatial; LOAD httpfs;
SET s3_region='us-west-2';

SELECT * FROM read_overture_places(west:=13.0, east:=13.8, south:=52.3, north:=52.7);
-- id | name | raw_category | category | lat | lng
```

### `read_overture_buildings(...)`

Buildings with centroid extraction and height (falls back to `num_floors * 3`). Same bbox params as places.

```sql
SELECT * FROM read_overture_buildings(west:=13.0, east:=13.8, south:=52.3, north:=52.7);
-- id | name | lat | lng | height | num_floors | class
```

### `read_overture_roads(...)`

Road segments with class, surface, and geometry as WKT. Same bbox params.

```sql
SELECT * FROM read_overture_roads(west:=13.0, east:=13.8, south:=52.3, north:=52.7)
WHERE class IN ('residential', 'pedestrian', 'footway');
-- id | name | class | subclass | surface | speed_limits | wkt | start_lat | start_lng
```

### `geocode(query, ...)`

Forward geocoding. Searches Overture addresses by text. Returns matching addresses sorted by relevance.

```sql
SELECT * FROM geocode('nørrebrogade', west:=12.4, east:=12.7, south:=55.6, north:=55.8);
-- address | postcode | city | country | lat | lng
```

Optional params: `n` (max results, default 10), bbox, `release`.

### `reverse_geocode(lat, lng, ...)`

Reverse geocoding. Nearest address(es) to a point.

```sql
SELECT * FROM reverse_geocode(55.6836, 12.5716);
-- address | postcode | city | country | lat | lng
```

Optional params: `radius` (search radius in degrees, default 0.005), `n` (max results, default 1), `release`.

### `read_overture(...)`

Generic Overture reader for any theme/type. Same bbox params, plus `theme` (default `'places'`) and `type` (default `'place'`).

```sql
SELECT * FROM read_overture(theme:='buildings', type:='building',
                            west:=13.0, east:=13.8, south:=52.3, north:=52.7);

SELECT * FROM read_overture(theme:='transportation', type:='segment',
                            west:=13.0, east:=13.8, south:=52.3, north:=52.7);
```

## Full pipeline example

Download, categorize, and tile European POIs in one query:

```sql
LOAD overture; LOAD spatial; LOAD httpfs;
SET s3_region='us-west-2';

COPY (
    SELECT name, category, lat, lng, st_tile_index(lat, lng) AS tile
    FROM read_overture_places(west:=-10, east:=35, south:=34, north:=72)
    WHERE category IS NOT NULL
) TO 'tiles' (FORMAT JSON, PARTITION_BY tile);
```

## Neighborhood DNA grid

`examples/dna_grid.sql` shows how to replace `compute_dna_grid.py` (~200 lines of Python) with a single SQL script. It computes 8-dimension feature vectors on a 250m grid and classifies cells as Commercial Hub, Tourist Area, Dining Quarter, etc.

## WASM

The scalar functions (`overture_category`, `st_tile_index`) and table function (`overture_categories`) work in DuckDB-WASM. The `read_overture*` macros need `spatial` + `httpfs` which may not be available in WASM.

See `examples/wasm.html` for a browser demo.

```javascript
import * as duckdb from '@duckdb/duckdb-wasm';

// after init...
await conn.query(`SET custom_extension_repository = 'https://cubilica.github.io/duckdb-overture';`);
await conn.query(`INSTALL overture; LOAD overture;`);
const result = await conn.query(`SELECT overture_category('coffee_shop')`);
// 'cafe'
```

## Build

```bash
git clone --recurse-submodules https://github.com/cubilica/duckdb-overture.git
cd duckdb-overture
make -j$(nproc)
```

Run tests:

```bash
make test
```

## License

MIT
