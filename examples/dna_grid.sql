-- Neighborhood DNA grid computation in pure SQL.
-- Computes 8-dimension feature vectors on a 250m grid for a city,
-- classifying each cell as Commercial Hub, Tourist Area, Dining Quarter, etc.

LOAD overture;
LOAD spatial;
LOAD httpfs;
SET s3_region='us-west-2';

SET VARIABLE city_name = 'Berlin';
SET VARIABLE city_south = 52.35;
SET VARIABLE city_north = 52.65;
SET VARIABLE city_west = 13.15;
SET VARIABLE city_east = 13.65;
SET VARIABLE grid_step = 0.0025;

CREATE OR REPLACE TEMP TABLE grid AS
SELECT
    city_south + (row_number() OVER () - 1) / cols * grid_step AS lat,
    city_west + (row_number() OVER () - 1) % cols * grid_step AS lng
FROM (
    SELECT CAST(CEIL((city_north - city_south) / grid_step) AS INTEGER) AS rows,
           CAST(CEIL((city_east - city_west) / grid_step) AS INTEGER) AS cols
) dims,
generate_series(1, dims.rows * dims.cols);

CREATE OR REPLACE TEMP TABLE pois AS
SELECT name, category, lat, lng
FROM read_overture_places(
    west := getvariable('city_west'),
    east := getvariable('city_east'),
    south := getvariable('city_south'),
    north := getvariable('city_north')
)
WHERE category IS NOT NULL;

CREATE OR REPLACE TEMP TABLE cell_counts AS
WITH poi_cells AS (
    SELECT
        st_tile_index(p.lat, p.lng, 0.0025) AS cell_key,
        g.lat AS cell_lat,
        g.lng AS cell_lng,
        p.category
    FROM pois p
    JOIN grid g ON abs(p.lat - g.lat) <= 0.0036
                AND abs(p.lng - g.lng) <= 0.0056
)
SELECT
    cell_lat AS lat,
    cell_lng AS lng,
    count(*) AS total,
    count(*) FILTER (WHERE category IN ('cafe', 'restaurant', 'bakery')) AS dining_n,
    count(*) FILTER (WHERE category = 'bar') AS nightlife_n,
    count(*) FILTER (WHERE category IN ('clothing', 'grocery', 'electronics', 'bookstore')) AS retail_n,
    count(*) FILTER (WHERE category IN ('hairdresser', 'dentist', 'pharmacy', 'optician', 'nail_salon')) AS services_n,
    count(*) FILTER (WHERE category = 'gym') AS wellness_n,
    count(*) FILTER (WHERE category = 'hotel') AS accommodation_n,
FROM poi_cells
GROUP BY cell_lat, cell_lng
HAVING count(*) >= 3;

SELECT
    lat,
    lng,
    round(dining_n::DOUBLE / total, 3) AS dining,
    round(nightlife_n::DOUBLE / total, 3) AS nightlife,
    round(retail_n::DOUBLE / total, 3) AS retail,
    round(services_n::DOUBLE / total, 3) AS services,
    round(wellness_n::DOUBLE / total, 3) AS wellness,
    round(accommodation_n::DOUBLE / total, 3) AS accommodation,
    round(least(total / 80.0, 1.0), 3) AS commercial,
    CASE
        WHEN total / 80.0 > 0.6 AND dining_n::DOUBLE / total > 0.2 AND retail_n::DOUBLE / total > 0.15
            THEN 'Commercial Hub'
        WHEN accommodation_n::DOUBLE / total > 0.2 AND dining_n::DOUBLE / total > 0.2
            THEN 'Tourist Area'
        WHEN nightlife_n::DOUBLE / total > 0.15 AND dining_n::DOUBLE / total > 0.25
            THEN 'Entertainment'
        WHEN dining_n::DOUBLE / total > 0.3
            THEN 'Dining Quarter'
        WHEN retail_n::DOUBLE / total > 0.25
            THEN 'Shopping'
        WHEN services_n::DOUBLE / total > 0.2
            THEN 'Services'
        ELSE 'Mixed-Use'
    END AS label
FROM cell_counts
ORDER BY lat, lng;
