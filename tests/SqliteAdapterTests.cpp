#include <EStudioUI/SqliteAdapter.h>
#include <EStudioUI/SnapshotModelAdapter.h>
#include <EStudioUI/ModelBuilder.h>
#include <sqlite3.h>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <thread>

using namespace EStudioUI;

namespace
{
int s_Checks = 0;

void Check(bool value, const char* message)
{
    ++s_Checks;
    if (!value)
    {
        throw std::runtime_error(message);
    }
}

template <class F> void Reject(F action, const char* message)
{
    bool rejected = false;
    try
    {
        action();
    }
    catch (const std::exception&)
    {
        rejected = true;
    }
    Check(rejected, message);
}

struct Fixture
{
    std::filesystem::path m_Directory =
        std::filesystem::temp_directory_path() /
        ("EStudioUI-sqlite-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::path m_Path = m_Directory / std::filesystem::path(u8"данные-日本.sqlite");
    sqlite3* m_Db = nullptr;

    Fixture()
    {
        std::filesystem::create_directories(m_Directory);
        const auto utf8 = m_Path.u8string();
        if (sqlite3_open(reinterpret_cast<const char*>(utf8.c_str()), &m_Db) != SQLITE_OK)
        {
            throw std::runtime_error("Cannot create test database");
        }
        Exec(
            "CREATE TABLE records(id INTEGER PRIMARY KEY,n INTEGER NOT NULL CHECK(n>=0),label TEXT COLLATE "
            "NOCASE,bytes BLOB,amount REAL);"
            "INSERT INTO records "
            "VALUES(1,9223372036854775807,'Alpha',x'00FF42',1.125),(2,3,NULL,x'',2.5),(3,3,'alpha',NULL,4.0),(4,9,'',x'"
            "01',2.5);"
            "CREATE TABLE \"odd\"\";DROP TABLE records;--\"(\"key\" TEXT PRIMARY KEY NOT NULL,\"column\"\"name\" TEXT);"
            "INSERT INTO \"odd\"\";DROP TABLE records;--\" VALUES('a','safe');"
            "CREATE TABLE composite(a TEXT,b BLOB,v TEXT,PRIMARY KEY(a,b)) WITHOUT ROWID;INSERT INTO composite "
            "VALUES('a:0',x'00013A','old');"
            "CREATE TABLE shadow(rowid TEXT,_rowid_ TEXT,oid TEXT,value TEXT);INSERT INTO shadow "
            "VALUES('r','r','r','unkeyed');"
            "CREATE TABLE generated(id INTEGER PRIMARY KEY,x INTEGER,y INTEGER GENERATED ALWAYS AS(x*2) STORED);INSERT "
            "INTO generated(id,x) VALUES(1,4);"
            "CREATE VIEW readonly_view AS SELECT label,n FROM records;");
    }

    ~Fixture()
    {
        sqlite3_close(m_Db);
        std::error_code error;
        std::filesystem::remove_all(m_Directory, error);
    }

    void Exec(const char* sql)
    {
        char* error = nullptr;
        if (sqlite3_exec(m_Db, sql, nullptr, nullptr, &error) != SQLITE_OK)
        {
            const std::string text = error ? error : "SQLite failure";
            sqlite3_free(error);
            throw std::runtime_error(text);
        }
    }
};

AdapterPage Edit(const SqliteAdapter& adapter, const AdapterPage& page, std::vector<AdapterEdit> edits)
{
    return adapter.Commit(page.m_Query, edits, {});
}

void Database()
{
    Fixture fixture;
    SqliteAdapter adapter(fixture.m_Path, true);
    const auto catalog = adapter.GetDataSets({});
    Check(catalog.size() == 6, "Schema enumeration includes tables and views");
    const auto view = std::find_if(catalog.begin(), catalog.end(),
                                   [](const ModelDataSet& value) { return value.m_Id == "readonly_view"; });
    Check(view != catalog.end() && !view->m_Editable && !view->m_StableKeys,
          "Views advertise honest read-only unkeyed identity");
    ModelQuery query;
    query.m_DataSet = "records";
    query.m_Limit = 2;
    auto page = adapter.Load(query, {});
    Check(page.m_TotalRows == 4 && page.m_MatchedRows == 4 && page.m_Rows.size() == 2,
          "Coherent counts and bounded loaded window");
    Check(std::get<std::int64_t>(page.m_Rows[0].m_Values[1]) == std::numeric_limits<std::int64_t>::max(),
          "Full signed 64-bit values never pass through double");
    Check(std::get<ModelBlob>(page.m_Rows[0].m_Values[3]) == ModelBlob({0, 255, 66}),
          "Binary values retain zero bytes");
    Check(page.m_Rows[1].m_Values[2].index() == 0 && std::get<ModelBlob>(page.m_Rows[1].m_Values[3]).empty(),
          "NULL and empty binary values are distinct");
    Check(!page.m_DataSet.m_Columns[0].m_Editable && page.m_DataSet.m_Columns[1].m_Editable,
          "Identity fields remain read-only");
    query.m_Offset = 100;
    page = adapter.Load(query, {});
    Check(page.m_Query.m_Offset == 2 && page.m_Rows.size() == 2, "Out-of-range pages clamp to the last result window");
    query.m_Offset = 0;
    query.m_Limit = 256;
    ModelFilter label;
    label.m_Column = 2;
    label.m_Value = std::string("Alpha");
    query.m_Filter = label;
    page = adapter.Load(query, {});
    Check(page.m_MatchedRows == 1, "Explicit case-sensitive equality overrides a NOCASE column collation");
    query.m_Filter.m_CaseSensitive = false;
    page = adapter.Load(query, {});
    Check(page.m_MatchedRows == 2, "Portable ASCII case folding is explicit");
    query.m_Filter.m_Value = std::string("' OR 1=1;--");
    page = adapter.Load(query, {});
    Check(page.m_Rows.empty(), "Filter values are bound parameters, not executable SQL");
    query.m_Filter = label;
    query.m_Filter.m_Operator = ModelFilterOperator::StartsWith;
    query.m_Filter.m_Value = std::string("Al%");
    Check(adapter.Load(query, {}).m_Rows.empty(), "Text matching treats SQL LIKE metacharacters literally");
    ModelFilter null;
    null.m_Column = 2;
    null.m_Operator = ModelFilterOperator::IsNull;
    ModelFilter small;
    small.m_Column = 1;
    small.m_Operator = ModelFilterOperator::Less;
    small.m_Value = std::int64_t{5};
    query.m_Filter = {};
    query.m_Filter.m_Any = true;
    query.m_Filter.m_Children = {null, small};
    Check(adapter.Load(query, {}).m_MatchedRows == 2, "OR groups combine typed predicates across all source records");
    query.m_Filter = {};
    query.m_Sort = {{1, true, false}, {4, false, false}};
    page = adapter.Load(query, {});
    Check(std::get<std::int64_t>(page.m_Rows[0].m_Values[0]) == 3 &&
              std::get<std::int64_t>(page.m_Rows[1].m_Values[0]) == 2,
          "Multi-key sorting uses exact raw values and deterministic ties");
    query.m_Sort = {{2, false, true}};
    page = adapter.Load(query, {});
    Check(page.m_Rows.back().m_Values[2].index() == 0, "Null-last ordering applies independently of direction");
    query.m_Sort = {};
    page = adapter.Load(query, {});
    const auto key = page.m_Rows[0].m_Key;
    page = Edit(adapter, page,
                {{key, 1, page.m_Rows[0].m_Values[1], std::int64_t{42}},
                 {key, 2, page.m_Rows[0].m_Values[2], std::string("changed")}});
    Check(page.m_Rows[0].m_Key == key && std::get<std::int64_t>(page.m_Rows[0].m_Values[1]) == 42,
          "Atomic optimistic writes return the committed source page");
    const auto before = page;
    Reject(
        [&]
        {
            Edit(adapter, page,
                 {{key, 2, page.m_Rows[0].m_Values[2], std::string("rollback")},
                  {key, 1, page.m_Rows[0].m_Values[1], std::int64_t{-1}}});
        },
        "A later constraint failure rejects the transaction");
    page = adapter.Load(query, {});
    Check(page.m_Rows[0].m_Values == before.m_Rows[0].m_Values, "A rejected batch rolls back earlier updates");
    fixture.Exec("UPDATE records SET label='external' WHERE id=1");
    Reject([&] { Edit(adapter, before, {{key, 2, before.m_Rows[0].m_Values[2], std::string("lost update")}}); },
           "An external writer cannot be silently overwritten");
    page = adapter.Load(query, {});
    Reject([&] { Edit(adapter, page, {{key, 0, page.m_Rows[0].m_Values[0], std::int64_t{50}}}); },
           "Primary key updates are rejected");
    SqliteAdapter readOnly(fixture.m_Path);
    Reject([&] { Edit(readOnly, page, {{key, 1, page.m_Rows[0].m_Values[1], std::int64_t{50}}}); },
           "Read-only is the default provider policy");
    query.m_DataSet = "odd\";DROP TABLE records;--";
    query.m_SchemaRevision = 0;
    page = adapter.Load(query, {});
    page = Edit(adapter, page, {{page.m_Rows[0].m_Key, 1, page.m_Rows[0].m_Values[1], std::string("quoted")}});
    Check(std::get<std::string>(page.m_Rows[0].m_Values[1]) == "quoted" && adapter.GetDataSets({}).size() == 6,
          "Quoted identifiers remain identifiers through discovery, reads and writes");
    query.m_DataSet = "composite";
    page = adapter.Load(query, {});
    page = Edit(adapter, page, {{page.m_Rows[0].m_Key, 2, page.m_Rows[0].m_Values[2], std::string("new")}});
    Check(page.m_DataSet.m_StableKeys && std::get<std::string>(page.m_Rows[0].m_Values[2]) == "new",
          "WITHOUT ROWID composite text/binary keys round-trip exactly");
    query.m_DataSet = "shadow";
    page = adapter.Load(query, {});
    Check(!page.m_DataSet.m_Editable && !page.m_DataSet.m_StableKeys,
          "Fully shadowed rowid tables do not invent stable keys");
    query.m_DataSet = "generated";
    page = adapter.Load(query, {});
    Check(!page.m_DataSet.m_Columns[2].m_Editable, "Generated columns remain read-only");
    const auto stale = page.m_Query;
    fixture.Exec("ALTER TABLE generated ADD COLUMN z TEXT");
    Reject([&] { adapter.Load(stale, {}); }, "An old schema query must refresh explicitly");
    query.m_DataSet = "records";
    query.m_SchemaRevision = 0;
    std::stop_source stop;
    stop.request_stop();
    Reject([&] { adapter.Load(query, stop.get_token()); }, "Pre-cancelled database work does not read");
    fixture.Exec("UPDATE records SET label=printf('%0200d',1) WHERE id=1");
    SqliteAdapter smallBudget(fixture.m_Path, false, {32, 256});
    Reject([&] { smallBudget.Load(query, {}); }, "Oversized raw fields fail without destructive truncation");
    SqliteAdapter missing(fixture.m_Directory / "missing.sqlite");
    Reject([&] { missing.GetDataSets({}); }, "Opening a missing user database fails");
    Check(!std::filesystem::exists(missing.GetPath()), "Failed database browsing never creates a user file");
    fixture.Exec("BEGIN EXCLUSIVE");
    std::stop_source busyStop;
    std::jthread cancel(
        [&]
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            busyStop.request_stop();
        });
    Reject([&] { adapter.Load(query, busyStop.get_token()); }, "Busy-lock waits cooperate with cancellation");
    fixture.Exec("ROLLBACK");
}
} // namespace

int main()
{
    try
    {
        Database();
        std::cout << "SQLite adapter: " << s_Checks << " checks passed\n";
        return 0;
    }
    catch (const std::exception& error)
    {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
