#include "ExampleDatabase.h"
#include <sqlite3.h>
#include <stdexcept>
#include <string>
#include <chrono>
#include <atomic>

namespace EStudioUI
{
std::filesystem::path CreateExampleDatabase(const std::filesystem::path& path, std::size_t rows, std::stop_token stop)
{
    if (std::filesystem::exists(path))
    {
        return path;
    }
    if (rows > 1000000)
    {
        throw std::length_error("The example creates at most one million rows.");
    }
    if (!path.parent_path().empty())
    {
        std::filesystem::create_directories(path.parent_path());
    }
    // Publish a complete new file without ever truncating an existing destination.
    static std::atomic_uint64_t s_Sequence = 0;
    auto staging = path;
    staging += ".creating-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
               std::to_string(++s_Sequence);
    const auto bytes = staging.u8string();
    const std::string name(reinterpret_cast<const char*>(bytes.data()), bytes.size());
    sqlite3* db = nullptr;
    sqlite3_stmt* insert = nullptr;
    const auto check = [&](int result)
    {
        if (stop.stop_requested())
        {
            throw std::runtime_error("Example database creation cancelled.");
        }
        if (result != SQLITE_OK && result != SQLITE_DONE)
        {
            throw std::runtime_error(db ? sqlite3_errmsg(db) : "Cannot create example database.");
        }
    };
    try
    {
        check(sqlite3_open_v2(name.c_str(), &db, SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr));
        check(sqlite3_exec(
            db,
            "BEGIN IMMEDIATE;"
            "CREATE TABLE load_cases(id INTEGER PRIMARY KEY, part TEXT NOT NULL, case_name TEXT NOT NULL, load_kN REAL "
            "NOT NULL CHECK(load_kN>=0), factor REAL NOT NULL CHECK(factor>0), active BOOLEAN NOT NULL, notes TEXT, "
            "payload BLOB);"
            "CREATE INDEX load_cases_part_load ON load_cases(part,load_kN);"
            "CREATE TABLE materials(code TEXT PRIMARY KEY NOT NULL, density REAL NOT NULL CHECK(density>0), family "
            "TEXT NOT NULL CHECK(family IN('Metal','Composite')), released BOOLEAN NOT NULL);"
            "INSERT INTO materials "
            "VALUES('AL-2024',2780,'Metal',1),('AL-7075',2810,'Metal',1),('CFRP-UD',1580,'Composite',1),('Steel',7850,'"
            "Metal',0);"
            "CREATE TABLE assemblies(id INTEGER PRIMARY KEY,parent_id INTEGER REFERENCES assemblies(id),name TEXT NOT "
            "NULL,mass_kg REAL NOT NULL);"
            "INSERT INTO assemblies "
            "VALUES(1,NULL,'Wing',420),(2,1,'Spar',125),(3,1,'Skin',180),(4,NULL,'Fuselage',740),(5,4,'Frame',75);"
            "CREATE VIEW released_materials AS SELECT code,density,family FROM materials WHERE released=1;",
            nullptr, nullptr, nullptr));
        check(sqlite3_prepare_v2(db, "INSERT INTO load_cases VALUES(?1,?2,?3,?4,?5,?6,?7,?8)", -1, &insert, nullptr));
        for (std::size_t i = 0; i < rows; ++i)
        {
            check(sqlite3_reset(insert));
            check(sqlite3_clear_bindings(insert));
            const auto part = "Part-" + std::to_string(i % 128 + 1);
            const auto title = "LC-" + std::to_string(i + 1);
            check(sqlite3_bind_int64(insert, 1, static_cast<sqlite3_int64>(i + 1)));
            check(sqlite3_bind_text(insert, 2, part.c_str(), -1, SQLITE_TRANSIENT));
            check(sqlite3_bind_text(insert, 3, title.c_str(), -1, SQLITE_TRANSIENT));
            check(sqlite3_bind_double(insert, 4, 20.0 + static_cast<double>(i % 5000) * .25));
            check(sqlite3_bind_double(insert, 5, 1.0 + static_cast<double>(i % 6) * .1));
            check(sqlite3_bind_int(insert, 6, i % 4 ? 1 : 0));
            if (i % 3)
            {
                check(sqlite3_bind_text(insert, 7, "Engineering sample / editable on the worker", -1, SQLITE_STATIC));
            }
            else
            {
                check(sqlite3_bind_null(insert, 7));
            }
            const unsigned char payload[]{static_cast<unsigned char>(i % 256), 0, 255, 42};
            check(sqlite3_bind_blob(insert, 8, payload, 4, SQLITE_TRANSIENT));
            check(sqlite3_step(insert));
        }
        sqlite3_finalize(insert);
        insert = nullptr;
        check(sqlite3_exec(db, "COMMIT", nullptr, nullptr, nullptr));
        sqlite3_close(db);
        db = nullptr;
        std::error_code publishError;
        std::filesystem::create_hard_link(staging, path, publishError);
        if (publishError && !std::filesystem::exists(path))
        {
            throw std::filesystem::filesystem_error("Cannot publish the example database", staging, path, publishError);
        }
        std::filesystem::remove(staging);
        return path;
    }
    catch (...)
    {
        if (insert)
        {
            sqlite3_finalize(insert);
        }
        if (db)
        {
            sqlite3_exec(db, "ROLLBACK", nullptr, nullptr, nullptr);
            sqlite3_close(db);
        }
        std::error_code ignored;
        std::filesystem::remove(staging, ignored);
        throw;
    }
}
} // namespace EStudioUI
