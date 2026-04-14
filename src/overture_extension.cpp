#define DUCKDB_EXTENSION_MAIN

#include "overture_extension.hpp"

#include "duckdb.hpp"
#include "duckdb/catalog/default/default_functions.hpp"
#include "duckdb/catalog/default/default_table_functions.hpp"
#include "duckdb/common/exception.hpp"
#include "duckdb/common/vector_operations/binary_executor.hpp"
#include "duckdb/function/scalar_function.hpp"
#include "duckdb/function/table_function.hpp"
#include "duckdb/main/extension/extension_loader.hpp"

#include <algorithm>

namespace duckdb {

// ============================================================================
// Default category mappings -- Overture place categories to normalized ones.
// This is the data source for overture_categories() and overture_category().
// Users can override by replacing the overture_category macro to point at
// their own table.
// ============================================================================

struct CategoryEntry {
	const char *overture;
	const char *mapped;
};

// clang-format off
static constexpr CategoryEntry DEFAULT_CATEGORIES[] = {
    {"cafe", "cafe"},                   {"coffee_shop", "cafe"},
    {"restaurant", "restaurant"},       {"fast_food_restaurant", "restaurant"},
    {"bar", "bar"},                     {"pub", "bar"},                         {"night_club", "bar"},
    {"bakery", "bakery"},
    {"supermarket", "grocery"},         {"convenience_store", "grocery"},
    {"grocery_store", "grocery"},       {"grocery", "grocery"},
    {"clothing_store", "clothing"},     {"shoe_store", "clothing"},
    {"electronics_store", "electronics"}, {"mobile_phone_store", "electronics"},
    {"bookstore", "bookstore"},         {"book_store", "bookstore"},
    {"hair_salon", "hairdresser"},      {"barber_shop", "hairdresser"},         {"beauty_salon", "hairdresser"},
    {"nail_salon", "nail_salon"},
    {"dentist", "dentist"},             {"dental_clinic", "dentist"},
    {"optician", "optician"},
    {"pharmacy", "pharmacy"},           {"drugstore", "pharmacy"},
    {"laundromat", "laundry"},          {"laundry_service", "laundry"},         {"dry_cleaner", "laundry"},
    {"tattoo_parlor", "tattoo"},        {"tattoo_shop", "tattoo"},
    {"auto_repair", "auto_repair"},     {"car_repair", "auto_repair"},          {"car_wash", "auto_repair"},
    {"gym", "gym"},                     {"fitness_center", "gym"},
    {"hotel", "hotel"},                 {"motel", "hotel"},                     {"hostel", "hotel"},
    {"bed_and_breakfast", "hotel"},      {"inn", "hotel"},                       {"resort", "hotel"},
    {"guest_house", "hotel"},
    {"pet_store", "pet_store"},
    {"veterinarian", "vet"},            {"animal_hospital", "vet"},
    {"florist", "florist"},             {"flower_shop", "florist"},
    {"school", "school"},               {"primary_school", "school"},
    {"secondary_school", "school"},     {"high_school", "school"},
    {"kindergarten", "kindergarten"},    {"preschool", "kindergarten"},
    {"daycare", "kindergarten"},         {"child_care", "kindergarten"},         {"nursery", "kindergarten"},
    {"university", "university"},       {"college", "university"},
    {"doctor", "doctor"},               {"clinic", "doctor"},
    {"hospital", "hospital"},
    {"park", "park"},                   {"garden", "park"},
    {"playground", "playground"},
    {"library", "library"},
    {"swimming_pool", "pool"},
    {"sports_centre", "sports"},        {"sports_center", "sports"},
    {"bus_station", "transit"},          {"bus_stop", "transit"},
    {"train_station", "transit"},        {"subway_station", "transit"},
    {"metro_station", "transit"},        {"tram_stop", "transit"},
    {"light_rail_station", "transit"},   {"transit_station", "transit"},
};
// clang-format on

static constexpr idx_t NUM_CATEGORIES = sizeof(DEFAULT_CATEGORIES) / sizeof(DEFAULT_CATEGORIES[0]);

// ============================================================================
// overture_categories() -> TABLE(overture_category, mapped_category)
//
// Returns the default category mappings. This is the data source that
// overture_category() queries against. To customize, create your own table
// and redefine the macro:
//
//   CREATE TABLE my_map AS SELECT * FROM overture_categories();
//   INSERT INTO my_map VALUES ('my_cat', 'my_mapped');
//   CREATE OR REPLACE MACRO overture_category(cat) AS (
//       SELECT mapped_category FROM my_map WHERE overture_category = cat
//   );
// ============================================================================

struct OvertureCategoriesBind : public TableFunctionData {};

struct OvertureCategoriesState : public GlobalTableFunctionState {
	idx_t offset = 0;
};

static unique_ptr<FunctionData> OvertureCategoriesBindFunc(ClientContext &context, TableFunctionBindInput &input,
                                                           vector<LogicalType> &return_types, vector<string> &names) {
	return_types = {LogicalType::VARCHAR, LogicalType::VARCHAR};
	names = {"overture_category", "mapped_category"};
	return make_uniq<OvertureCategoriesBind>();
}

static unique_ptr<GlobalTableFunctionState> OvertureCategoriesInitGlobal(ClientContext &context,
                                                                         TableFunctionInitInput &input) {
	return make_uniq<OvertureCategoriesState>();
}

static void OvertureCategoriesScan(ClientContext &context, TableFunctionInput &input, DataChunk &output) {
	auto &gstate = input.global_state->Cast<OvertureCategoriesState>();

	idx_t count = 0;
	while (gstate.offset < NUM_CATEGORIES && count < STANDARD_VECTOR_SIZE) {
		auto &entry = DEFAULT_CATEGORIES[gstate.offset];
		output.SetValue(0, count, Value(entry.overture));
		output.SetValue(1, count, Value(entry.mapped));
		gstate.offset++;
		count++;
	}
	output.SetCardinality(count);
}

// ============================================================================
// st_tile_index(lat DOUBLE, lng DOUBLE [, cell_size DOUBLE]) -> VARCHAR
// ============================================================================

static inline string TileKey(int64_t tlat, int64_t tlng) {
	return to_string(tlat) + "_" + to_string(tlng);
}

static void STTileIndex2Function(DataChunk &args, ExpressionState &state, Vector &result) {
	BinaryExecutor::Execute<double, double, string_t>(
	    args.data[0], args.data[1], result, args.size(), [&](double lat, double lng) {
		    return StringVector::AddString(result,
		                                   TileKey(static_cast<int64_t>(lat * 10.0), static_cast<int64_t>(lng * 10.0)));
	    });
}

static void STTileIndex3Function(DataChunk &args, ExpressionState &state, Vector &result) {
	auto count = args.size();
	auto &lat_vec = args.data[0];
	auto &lng_vec = args.data[1];
	auto &cell_vec = args.data[2];

	UnifiedVectorFormat lat_data, lng_data, cell_data;
	lat_vec.ToUnifiedFormat(count, lat_data);
	lng_vec.ToUnifiedFormat(count, lng_data);
	cell_vec.ToUnifiedFormat(count, cell_data);

	result.SetVectorType(VectorType::FLAT_VECTOR);
	auto result_ptr = FlatVector::GetData<string_t>(result);
	auto &result_validity = FlatVector::Validity(result);

	for (idx_t i = 0; i < count; i++) {
		auto lat_idx = lat_data.sel->get_index(i);
		auto lng_idx = lng_data.sel->get_index(i);
		auto cell_idx = cell_data.sel->get_index(i);

		if (!lat_data.validity.RowIsValid(lat_idx) || !lng_data.validity.RowIsValid(lng_idx) ||
		    !cell_data.validity.RowIsValid(cell_idx)) {
			result_validity.SetInvalid(i);
			continue;
		}

		auto lat = (reinterpret_cast<const double *>(lat_data.data))[lat_idx];
		auto lng = (reinterpret_cast<const double *>(lng_data.data))[lng_idx];
		auto cell_size = (reinterpret_cast<const double *>(cell_data.data))[cell_idx];

		if (cell_size <= 0) {
			throw InvalidInputException("st_tile_index: cell_size must be positive, got %f", cell_size);
		}

		double inv = 1.0 / cell_size;
		result_ptr[i] =
		    StringVector::AddString(result, TileKey(static_cast<int64_t>(lat * inv), static_cast<int64_t>(lng * inv)));
	}
}

// ============================================================================
// Table macros
// ============================================================================

// clang-format off
static const DefaultTableMacro overture_table_macros[] = {
	{DEFAULT_SCHEMA, "read_overture", {nullptr}, {
		{"release", "COALESCE(getvariable('overture_release'), '2026-03-18.0')"},
		{"theme", "'places'"},
		{"type", "'place'"},
		{"west", "-180.0"},
		{"east", "180.0"},
		{"south", "-90.0"},
		{"north", "90.0"},
		{nullptr, nullptr}
	}, R"(
SELECT *
FROM read_parquet(
    COALESCE(getvariable('overture_s3_base'), 's3://overturemaps-us-west-2/release') || '/' || release || '/theme=' || theme || '/type=' || type || '/*',
    filename := true,
    hive_partitioning := true
)
WHERE bbox.xmin > west AND bbox.xmax < east
  AND bbox.ymin > south AND bbox.ymax < north
)"},

	{DEFAULT_SCHEMA, "read_overture_places", {nullptr}, {
		{"release", "COALESCE(getvariable('overture_release'), '2026-03-18.0')"},
		{"west", "-180.0"},
		{"east", "180.0"},
		{"south", "-90.0"},
		{"north", "90.0"},
		{nullptr, nullptr}
	}, R"(
SELECT
    id,
    names.primary AS name,
    categories.primary AS raw_category,
    overture_category(categories.primary) AS category,
    ST_Y(geometry) AS lat,
    ST_X(geometry) AS lng
FROM read_parquet(
    COALESCE(getvariable('overture_s3_base'), 's3://overturemaps-us-west-2/release') || '/' || release || '/theme=places/type=place/*',
    filename := true,
    hive_partitioning := true
)
WHERE bbox.xmin > west AND bbox.xmax < east
  AND bbox.ymin > south AND bbox.ymax < north
  AND categories.primary IS NOT NULL
)"},

	// Buildings with height extraction and centroid. Requires spatial extension.
	{DEFAULT_SCHEMA, "read_overture_buildings", {nullptr}, {
		{"release", "COALESCE(getvariable('overture_release'), '2026-03-18.0')"},
		{"west", "-180.0"},
		{"east", "180.0"},
		{"south", "-90.0"},
		{"north", "90.0"},
		{nullptr, nullptr}
	}, R"(
SELECT
    id,
    names.primary AS name,
    ST_Y(ST_Centroid(geometry)) AS lat,
    ST_X(ST_Centroid(geometry)) AS lng,
    COALESCE(height, num_floors * 3, NULL) AS height,
    num_floors,
    class
FROM read_parquet(
    COALESCE(getvariable('overture_s3_base'), 's3://overturemaps-us-west-2/release') || '/' || release || '/theme=buildings/type=building/*',
    filename := true,
    hive_partitioning := true
)
WHERE bbox.xmin > west AND bbox.xmax < east
  AND bbox.ymin > south AND bbox.ymax < north
)"},

	// Road segments with class, surface, and geometry. Requires spatial extension.
	{DEFAULT_SCHEMA, "read_overture_roads", {nullptr}, {
		{"release", "COALESCE(getvariable('overture_release'), '2026-03-18.0')"},
		{"west", "-180.0"},
		{"east", "180.0"},
		{"south", "-90.0"},
		{"north", "90.0"},
		{nullptr, nullptr}
	}, R"(
SELECT
    id,
    names.primary AS name,
    class,
    subclass,
    surface,
    speed_limits,
    ST_AsText(geometry) AS wkt,
    ST_Y(ST_PointN(geometry, 1)) AS start_lat,
    ST_X(ST_PointN(geometry, 1)) AS start_lng
FROM read_parquet(
    COALESCE(getvariable('overture_s3_base'), 's3://overturemaps-us-west-2/release') || '/' || release || '/theme=transportation/type=segment/*',
    filename := true,
    hive_partitioning := true
)
WHERE bbox.xmin > west AND bbox.xmax < east
  AND bbox.ymin > south AND bbox.ymax < north
)"},

	// Forward geocoding: search addresses by street name. Requires spatial extension.
	{DEFAULT_SCHEMA, "geocode", {"query", nullptr}, {
		{"release", "COALESCE(getvariable('overture_release'), '2026-03-18.0')"},
		{"west", "-180.0"},
		{"east", "180.0"},
		{"south", "-90.0"},
		{"north", "90.0"},
		{"n", "10"},
		{nullptr, nullptr}
	}, R"(
SELECT
    street || ' ' || COALESCE(number, '') AS address,
    postcode,
    postal_city AS city,
    country,
    ST_Y(geometry) AS lat,
    ST_X(geometry) AS lng
FROM read_parquet(
    COALESCE(getvariable('overture_s3_base'), 's3://overturemaps-us-west-2/release') || '/' || release || '/theme=addresses/type=address/*',
    filename := true,
    hive_partitioning := true
)
WHERE bbox.xmin > west AND bbox.xmax < east
  AND bbox.ymin > south AND bbox.ymax < north
  AND street IS NOT NULL
  AND contains(lower(street) || ' ' || COALESCE(lower(number), '') || ' ' || COALESCE(lower(postal_city), ''), lower(query))
LIMIT n
)"},

	// Reverse geocoding: nearest address to a point. Requires spatial extension.
	{DEFAULT_SCHEMA, "reverse_geocode", {"lat", "lng", nullptr}, {
		{"release", "COALESCE(getvariable('overture_release'), '2026-03-18.0')"},
		{"radius", "0.005"},
		{"n", "1"},
		{nullptr, nullptr}
	}, R"(
SELECT
    street || ' ' || COALESCE(number, '') AS address,
    postcode,
    postal_city AS city,
    country,
    ST_Y(geometry) AS lat,
    ST_X(geometry) AS lng
FROM read_parquet(
    COALESCE(getvariable('overture_s3_base'), 's3://overturemaps-us-west-2/release') || '/' || release || '/theme=addresses/type=address/*',
    filename := true,
    hive_partitioning := true
)
WHERE bbox.xmin > lng - radius AND bbox.xmax < lng + radius
  AND bbox.ymin > lat - radius AND bbox.ymax < lat + radius
ORDER BY (ST_Y(geometry) - lat) * (ST_Y(geometry) - lat) + (ST_X(geometry) - lng) * (ST_X(geometry) - lng)
LIMIT n
)"},

	{nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}
};
// clang-format on

// ============================================================================
// Scalar macro: overture_category(cat) -- looks up from overture_categories()
// ============================================================================

// clang-format off
static const DefaultMacro overture_scalar_macros[] = {
	{DEFAULT_SCHEMA, "overture_category", {"cat", nullptr}, {{nullptr, nullptr}},
	 "(SELECT mapped_category FROM overture_categories() WHERE overture_category = cat)"},
	{nullptr, nullptr, {nullptr}, {{nullptr, nullptr}}, nullptr}
};
// clang-format on

// ============================================================================
// Extension loading
// ============================================================================

static void LoadInternal(ExtensionLoader &loader) {
	// overture_categories() table function
	TableFunction categories_func("overture_categories", {}, OvertureCategoriesScan, OvertureCategoriesBindFunc);
	categories_func.init_global = OvertureCategoriesInitGlobal;
	loader.RegisterFunction(categories_func);

	// st_tile_index() scalar function
	ScalarFunctionSet tile_set("st_tile_index");
	tile_set.AddFunction(
	    ScalarFunction({LogicalType::DOUBLE, LogicalType::DOUBLE}, LogicalType::VARCHAR, STTileIndex2Function));
	tile_set.AddFunction(ScalarFunction({LogicalType::DOUBLE, LogicalType::DOUBLE, LogicalType::DOUBLE},
	                                    LogicalType::VARCHAR, STTileIndex3Function));
	loader.RegisterFunction(tile_set);

	// overture_category() scalar macro
	for (idx_t i = 0; overture_scalar_macros[i].name != nullptr; i++) {
		auto info = DefaultFunctionGenerator::CreateInternalMacroInfo(overture_scalar_macros[i]);
		loader.RegisterFunction(*info);
	}

	// read_overture(), read_overture_places() table macros
	for (idx_t i = 0; overture_table_macros[i].name != nullptr; i++) {
		auto info = DefaultTableFunctionGenerator::CreateTableMacroInfo(overture_table_macros[i]);
		loader.RegisterFunction(*info);
	}
}

void OvertureExtension::Load(ExtensionLoader &loader) {
	LoadInternal(loader);
}

string OvertureExtension::Name() {
	return "overture";
}

string OvertureExtension::Version() const {
	return DefaultVersion();
}

} // namespace duckdb

extern "C" {
DUCKDB_CPP_EXTENSION_ENTRY(overture, loader) {
	duckdb::LoadInternal(loader);
}
}
